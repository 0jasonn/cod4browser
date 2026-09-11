import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';
import { aggregateGameplayProfile, summarizeProfileSamples } from '../tests/browser/retail_profile_aggregate.mjs';

// Select authored work before measuring. The opening movie can shift the first
// rendered world view by one fixed step without changing the canonical clock.
export const ACTIVE_WORKLOAD_TIMES = Object.freeze({ first: 4139, last: 8939,
    profileFirst: 9915, profileLast: 11819, step: 16 });

// Idle realtime regression view, with normal physics and scripts still running.
// The server command takes eye coordinates and preserves the settled spawn.
export const KILLHOUSE_REALTIME_CAMERA = Object.freeze({ atCanonicalTime: 30000,
    command: 'setviewpos 3072 -1155 64.125 100.9 0' });

// Executed directly in the engine Worker by the local benchmark.
// Engine state and input remain canonical; this only retains completed timings.
export function installFrameTimingCapture({ map, realtime = false, timeWindow = null, capacity = 16384 }) {
    if (!Number.isInteger(capacity) || capacity < 301 || capacity > 32768)
        throw new RangeError('frame capture capacity must be 301..32768');
    const state = { samples: [], dropped: 0, complete: false,
        invalidReason: null, map, timeWindow, startupPhase: null,
        started: null, timeOrigin: globalThis.performance?.timeOrigin ?? 0 };
    globalThis.__frameTimingCapture = state;
    globalThis.kisakFrameTiming = { record(sample) {
        if (state.complete) return;
        const scene = sample.scene;
        // Select the opening render phase before retaining any timing samples.
        // An extra early view changes canonical per-view RNG consumption even
        // when later camera checkpoints have the same authored game time.
        if (timeWindow && !realtime && !state.startupPhase &&
            scene?.world?.toLowerCase().includes(map) && scene.generation >= 30) {
            state.startupPhase = { generation: scene.generation, time: scene.time };
            if (scene.generation !== 30 || scene.time !== 779) {
                state.invalidReason = `startup phase differs: view 30 must be at time 779; observed view ${scene.generation} at ${scene.time}`;
                state.complete = true;
                globalThis.kisakFrameTiming = null;
                return;
            }
        }
        if (state.started === null && (!scene?.world?.toLowerCase().includes(map) ||
            (timeWindow ? scene.time < timeWindow.first : scene.generation < 240) ||
            (realtime && scene.time < 45000))) return;
        const previous = state.samples.at(-1);
        if (!scene?.geometry || !scene.world?.toLowerCase().includes(map) ||
            !Number.isSafeInteger(scene.generation) || !Number.isSafeInteger(scene.time) ||
            (timeWindow && scene.time !== timeWindow.first + state.samples.length * timeWindow.step) ||
            (previous && (scene.generation !== previous.scene.generation + 1 ||
                scene.contextGeneration !== previous.scene.contextGeneration ||
                scene.worldGeneration !== previous.scene.worldGeneration)) ||
            (realtime && (sample.simulationMilliseconds <= 0 || previous && scene.time <= previous.scene.time))) {
            state.invalidReason = 'missing, stale, paused, or changed canonical gameplay view';
            state.complete = true;
        } else if (state.samples.length === capacity) {
            ++state.dropped;
            state.complete = true;
        } else {
            if (state.started === null) state.started = sample.completed;
            state.samples.push(sample);
            state.complete = realtime ? sample.completed - state.started >= 60000
                : timeWindow ? scene.time === timeWindow.last : scene.generation === 540;
        }
        if (state.complete) globalThis.kisakFrameTiming = null;
    } };
}

export function validateFrameTimingCapture(capture, realtime = false) {
    assert.equal(capture.complete, true, 'incomplete Worker frame window');
    assert.equal(capture.dropped, 0, 'Worker frame buffer overflowed');
    assert.equal(capture.invalidReason, null, 'canonical gameplay view invalid');
    const frames = capture.samples;
    assert(frames.length >= 2);
    if (!realtime) assert.equal(frames.length, 301);
    if (capture.timeWindow) {
        assert.equal(realtime, false, 'canonical fixed-work window cannot qualify realtime');
        assert.deepEqual(capture.timeWindow, ACTIVE_WORKLOAD_TIMES, 'canonical workload window changed');
        assert.deepEqual(capture.startupPhase, { generation: 30, time: 779 }, 'canonical startup phase changed or missing');
    } else if (realtime) {
        assert(frames[0].scene.generation >= 240 && frames[0].scene.time >= 45000 && frames[0].scene.time < 50000,
            'realtime window must begin after the defined opening warmup');
    } else assert.equal(frames[0].scene.generation, 240, 'missing start of frame-matched window');
    for (const [index, frame] of frames.entries()) {
        for (const key of ['started', 'simulationStarted', 'submissionStarted', 'completed',
            'wallMilliseconds', 'simulationMilliseconds']) assert(Number.isFinite(frame[key]), key);
        assert(frame.started <= frame.simulationStarted && frame.simulationStarted <= frame.submissionStarted &&
            frame.submissionStarted <= frame.completed, 'invalid CPU timing order');
        assert(frame.wallMilliseconds > 0 && frame.wallMilliseconds <= 5000);
        assert(frame.scene?.geometry === true && frame.scene.world?.toLowerCase().includes(capture.map));
        assert(Number.isSafeInteger(frame.scene.time));
        assert(Number.isSafeInteger(frame.scene.generation) && frame.scene.generation > 0);
        if (capture.timeWindow) assert.equal(frame.scene.time, ACTIVE_WORKLOAD_TIMES.first + index * ACTIVE_WORKLOAD_TIMES.step,
            'missing canonical fixed-work time');
        if (realtime) assert(frame.simulationMilliseconds > 0, 'paused simulation cannot qualify gameplay cadence');
        if (index) {
            const previous = frames[index - 1];
            assert.equal(frame.scene.generation, previous.scene.generation + 1, 'missing submitted canonical frame');
            for (const key of ['world', 'contextGeneration', 'worldGeneration'])
                assert.equal(frame.scene[key], previous.scene[key], `canonical ${key} changed`);
            assert(frame.completed > previous.completed, 'nonmonotonic Worker clock');
            if (realtime) assert(frame.scene.time > previous.scene.time, 'canonical game clock stopped');
        }
    }
    if (realtime) assert(frames.at(-1).completed - frames[0].completed >= 60000,
        'realtime cadence window must include all 60 seconds');
    return frames.slice(1).map((frame, index) => frame.completed - frames[index].completed);
}

// The same clean-window endpoints preserve stalls; canonical time may advance
// less than wall time because Com_ModifyMsec retains its native frame clamp.
export function summarizeClockAdvancement(capture) {
    const first = capture.samples[0];
    const last = capture.samples.at(-1);
    const canonicalMilliseconds = last.scene.time - first.scene.time;
    const workerWallMilliseconds = last.completed - first.completed;
    return { canonicalMilliseconds, workerWallMilliseconds,
        canonicalToWallRatio: canonicalMilliseconds / workerWallMilliseconds };
}

// Raw generations stay in the result. Only comparison coordinates are relative;
// time, camera, projection, geometry, and draw counts remain exact.
export function normalizeActiveViews(views, profile = false) {
    const firstTime = profile ? ACTIVE_WORKLOAD_TIMES.profileFirst : ACTIVE_WORKLOAD_TIMES.first;
    const stride = profile ? 1 : 60;
    assert.equal(views.length, profile ? 120 : 6, 'canonical view window incomplete');
    const firstGeneration = views[0].submissionGeneration;
    assert(Number.isSafeInteger(firstGeneration) && firstGeneration > 0);
    return views.map((view, index) => {
        assert.equal(view.time, firstTime + index * stride * ACTIVE_WORKLOAD_TIMES.step,
            'missing canonical view time');
        assert.equal(view.submissionGeneration, firstGeneration + index * stride,
            'missing canonical view generation');
        assert.equal(view.geometrySubmitted, true);
        for (const vector of [view.viewOrigin, view.viewForward])
            assert(vector?.length === 3 && vector.every(Number.isFinite), 'invalid canonical camera');
        return { ...view, submissionGeneration: view.submissionGeneration - firstGeneration };
    });
}

export function normalizeRealtimeKillhouseViews(workload, frameTiming) {
    assert.equal(workload.mode, 'realtime');
    assert.equal(workload.map, 'killhouse');
    assert.equal(frameTiming.map, 'killhouse');
    assert.deepEqual(workload.requestedCamera, KILLHOUSE_REALTIME_CAMERA);
    assert.equal(workload.trace?.length, 12, 'expected realtime camera checkpoints at 45..100 seconds');
    return workload.trace.map((view, index) => {
        const checkpoint = 45000 + index * 5000;
        const frame = frameTiming.samples.find(sample => sample.scene.time >= checkpoint);
        assert(frame, 'missing realtime camera checkpoint frame');
        assert.equal(view.time, frame.scene.time, 'camera checkpoint must be the first frame at its canonical time');
        assert.equal(view.submissionGeneration, frame.scene.generation, 'camera checkpoint differs from captured frame');
        assert.equal(view.worldName, frame.scene.world);
        assert.equal(view.geometrySubmitted, true);
        assert.deepEqual(view.viewport, { x: 0, y: 0, width: 1920, height: 1080 });
        for (const key of ['tanHalfFovX', 'tanHalfFovY', 'zNear'])
            assert(Number.isFinite(view[key]) && view[key] > 0, `invalid camera ${key}`);
        for (const key of ['worldSurfaceCount', 'worldVertexCount', 'worldIndexCount'])
            assert(Number.isSafeInteger(view[key]) && view[key] > 0, `invalid camera ${key}`);
        // Admit only native float/angle quantization around the requested view.
        // Across runs every camera/projection/geometry value still matches exactly.
        for (const [key, expected, tolerance] of [
            ['viewOrigin', [3072, -1155, 64.125], 0.01],
            ['viewForward', [-0.18909545, 0.98195869, 0], 0.0001],
        ]) {
            assert.equal(view[key]?.length, 3, `missing camera ${key}`);
            for (let component = 0; component < 3; ++component)
                assert(Number.isFinite(view[key][component]) &&
                    Math.abs(view[key][component] - expected[component]) <= tolerance,
                    `requested Killhouse camera ${key} was not retained`);
        }
        // Realtime frame identity and time remain raw evidence, not comparison
        // coordinates. Do not demand identical timestamps from different cadences.
        const { time, submissionGeneration, ...canonicalView } = view;
        return canonicalView;
    });
}

export function validateActiveWorkload(views, frameTiming) {
    assert.deepEqual(frameTiming.timeWindow, ACTIVE_WORKLOAD_TIMES, 'active capture must select canonical time');
    validateFrameTimingCapture(frameTiming);
    normalizeActiveViews(views);
    for (const [index, view] of views.entries()) {
        const scene = frameTiming.samples[index * 60].scene;
        assert.equal(view.submissionGeneration, scene.generation, 'view differs from captured canonical frame');
        assert.equal(view.time, scene.time);
        assert.equal(view.worldName, scene.world);
    }
    return { mode: 'active', requestedMapSeed: 1, fixedtime: 16,
        startupPhase: { ...frameTiming.startupPhase }, trace: views };
}

export function validateActiveProfileViews(views, frames, frameTiming) {
    normalizeActiveViews(views, true);
    assert.equal(frames.length, 120);
    const firstGeneration = frameTiming.samples[0].scene.generation +
        (ACTIVE_WORKLOAD_TIMES.profileFirst - ACTIVE_WORKLOAD_TIMES.first) / ACTIVE_WORKLOAD_TIMES.step;
    for (const [index, frame] of frames.entries()) {
        assert.equal(views[index].submissionGeneration, firstGeneration + index, 'profile lost canonical frames');
        assert.equal(frame.viewSubmissionGeneration, firstGeneration + index, 'profile and view generation differ');
    }
}

export function validateGpuDrain(frames, results) {
    assert(Array.isArray(frames) && frames.length > 0 && frames.length <= 120, 'GPU frame window must be bounded');
    assert(Array.isArray(results), 'raw GPU results missing');
    const issued = frames.filter(frame => frame.gpu?.queryIssued);
    assert.equal(results.length, issued.length, 'GPU results incomplete or outside the captured window');
    assert(frames.every(frame => frame.gpu?.timingsAvailable), 'hardware GPU timers unavailable');
    assert(frames.every(frame => !frame.gpu?.queryDropped), 'GPU query capacity exceeded');
    for (const frame of issued) {
        const matched = results.filter(result => result.pumpTick === frame.pumpTick &&
            result.gpu?.stage === frame.gpu.stage);
        assert.equal(matched.length, 1, 'GPU query result missing or duplicated');
        assert.equal(matched[0].gpu.status, 'valid', 'GPU query disjoint or stale');
        assert(Number.isFinite(matched[0].gpu.stageMs) && matched[0].gpu.stageMs >= 0, 'invalid GPU elapsed time');
        assert(Number.isSafeInteger(matched[0].gpu.queryLagFrames) && matched[0].gpu.queryLagFrames >= 0, 'invalid GPU query lag');
        for (const key of ['contextGeneration', 'worldGeneration', 'viewSubmissionGeneration'])
            assert.equal(matched[0][key], frame[key], `GPU ${key} changed`);
    }
    assert(issued.length > 0, 'capture issued no GPU queries');
    return { complete: true, issued: issued.length, results: issued.length };
}

// Select a comparable browser launch before importing assets or retaining game
// timings. This does not change the browser's scheduling or comparison limits.
export function validatePreImportCadence(cadence, target) {
    if (target === undefined) return;
    assert(Number.isFinite(target) && target > 0, 'invalid requested browser cadence');
    assert(cadence.sampleCount >= 120 && Number.isFinite(cadence.average));
    assert(Math.abs(cadence.average / target - 1) < 0.02,
        `pre-import browser cadence differs: requested ${target} ms, observed ${cadence.average} ms`);
}

export function validateBenchmarkEnvironment(environment) {
    if (environment.executionMode === 'headless-muted') {
        assert.equal(environment.headless, true, 'background measurements must not open windows');
        assert.equal(environment.audioMuted, true, 'background measurements must be muted');
    } else {
        assert.equal(environment.executionMode, undefined, 'unknown browser execution mode');
        assert.equal(environment.headless, false, 'unlabelled headless measurement');
    }
    assert.equal(environment.renderSize.width, 1920);
    assert.equal(environment.renderSize.height, 1080);
    assert.equal(environment.foreground.performanceWindowValid, true, 'background window');
    const identity = environment.gpu;
    assert(identity?.renderer && identity.vendor && identity.version, 'actual engine GPU identity is required');
    assert(!/swiftshader|llvmpipe|software|microsoft basic render/i.test(identity.renderer), 'software renderer');
    assert(environment.systemGpu?.devices?.some(device => device.driverVersion || device.driverVendor),
        'GPU driver identity missing');
    assert(environment.displayCadence?.sampleCount >= 120, 'display cadence missing');
    assert(environment.graphicsSettings && Object.keys(environment.graphicsSettings).length >= 10,
        'canonical graphics settings missing');
}

export function canonicalWorkCounts(frames) {
    return frames.map(frame => Object.fromEntries([
        'worldSurfacesSubmitted', 'worldSurfacesDrawn', 'staticModelInstancesRetained',
        'staticModelInstanceDraws', 'dynamicBatchesDrawn', 'fxModelBatchesDrawn',
        'particleBatchesDrawn', 'markBatchesDrawn', 'shadowCasterDraws',
        'sunShadowMergedRanges', 'submittedIndices', 'dynamicCommandVertices',
        'dynamicCommandIndices', 'uiCommandVertices', 'uiCommandIndices',
    ].map(key => {
        const value = frame.counters[key];
        assert(Number.isSafeInteger(value) && value >= 0, key);
        return [key, value];
    })));
}

export function compareMeasuredWorkloads(runs) {
    assert(runs.length >= 2);
    const comparableWorkload = ({ workload, frameTiming }) => workload.mode === 'active'
        ? { ...workload, trace: normalizeActiveViews(workload.trace) }
        : workload.requestedCamera
            ? { ...workload, trace: normalizeRealtimeKillhouseViews(workload, frameTiming) } : workload;
    for (const run of runs) {
        assert.equal(run.schemaVersion, 2, 'old throttled telemetry cannot qualify');
        assert.match(run.benchmarkDriver?.sha256 ?? '', /^[a-f0-9]{64}$/, 'benchmark driver SHA-256 missing or invalid');
        assert.equal(run.benchmarkDriver.sha256, runs[0].benchmarkDriver.sha256, 'benchmark driver differs');
        assert.equal(run.provenance?.verified, true, 'build receipt unverified');
        assert.equal(run.pageErrorCount, 0);
        validateBenchmarkEnvironment(run.environment);
        const summary = summarizeProfileSamples(validateFrameTimingCapture(run.frameTiming, run.workload.mode === 'realtime'));
        assert.deepEqual(run.cleanTiming.intervals, summary, 'reported timing summary differs from Worker samples');
        assert.equal(run.cleanTiming.averageSubmissionFps, 1000 / summary.average);
        if (run.workload.mode === 'realtime')
            assert.deepEqual(run.cleanTiming.clockAdvancement, summarizeClockAdvancement(run.frameTiming),
                'reported game-clock advancement differs from Worker samples');
        assert.equal(run.cleanTiming.qualified, run.workload.mode === 'realtime' &&
            1000 / summary.average >= 60 && summary.p95 <= 20 && summary.p99 <= 33.3);
        const receipt = run.provenance.receipt;
        assert.equal(run.artifactSha256, receipt?.site?.['kisakcod.wasm']?.sha256, 'artifact differs from embedded receipt');
        assert.equal(run.source.commitSha, receipt.revision, 'source revision differs from embedded receipt');
        assert(receipt.buildInputs?.files && receipt.buildInputs?.archive?.sha256, 'source inputs missing');
        assert(receipt.buildInputs.files['src/web/web_main.cpp'], 'canonical frame source input missing');
        if (run.source.commitSha === runs[0].source.commitSha)
            assert.deepEqual(receipt.buildInputs.files, runs[0].provenance.receipt.buildInputs.files,
                'same-source A/B input inventories differ');
        if (run.workload.mode === 'active') validateActiveWorkload(run.workload.trace, run.frameTiming);
        assert.deepEqual(comparableWorkload(run), comparableWorkload(runs[0]),
            'seeded frame/camera/time workload differs');
        const comparableEnvironment = environment => ({ ...environment, foreground: undefined,
            displayCadence: undefined, systemGpu: { devices: environment.systemGpu.devices } });
        assert.deepEqual(comparableEnvironment(run.environment), comparableEnvironment(runs[0].environment),
            'GPU, driver, browser, graphics or display environment differs');
        const cadence = run.environment.displayCadence.average;
        assert(Math.abs(cadence / runs[0].environment.displayCadence.average - 1) < 0.05,
            'display cadence differs');
        if (run.profile) {
            assert.deepEqual(run.gpuDrain, validateGpuDrain(run.frameSamples, run.gpuResults),
                'GPU drain summary differs from raw results');
            assert.deepEqual(run.profile.gpu, aggregateGameplayProfile({ frames: run.frameSamples,
                gpuResults: run.gpuResults, capture: run.profile }).gpu, 'GPU summary differs from raw results');
            assert.deepEqual(run.workCounts, canonicalWorkCounts(run.frameSamples), 'work counts differ from raw frames');
            if (run.workload.mode === 'active') {
                validateActiveProfileViews(run.profileViews, run.frameSamples, run.frameTiming);
                assert.deepEqual(normalizeActiveViews(run.profileViews, true), normalizeActiveViews(runs[0].profileViews, true),
                    'active profile frame/camera/time differs');
            } else assert.deepEqual(run.profileViews, runs[0].profileViews, 'profile frame/camera/time differs');
            assert.deepEqual(run.workCounts, runs[0].workCounts, 'frame-matched draw work differs');
        }
    }
    return runs.map(run => ({ artifactSha256: run.artifactSha256,
        ...run.cleanTiming.intervals, qualified: run.cleanTiming.qualified }));
}

// Sparse, existing production refdef telemetry; no engine state is authored here.
export function validateWorkload(views, warmup) {
    assert.equal(warmup.length, 2);
    assert.deepEqual(warmup.map(view => view.submissionGeneration), [30, 60]);
    assert.equal(warmup[1].time - warmup[0].time, 30 * 16, 'fixedtime 16 was not honored');
    assert.equal(views.length, 6, 'expected views 240 through 540');
    const trace = views.map((view, index) => {
        assert.equal(view.submissionGeneration, 240 + index * 60, 'missing canonical view checkpoint');
        assert.equal(view.geometrySubmitted, true);
        assert(view.worldName?.toLowerCase().includes('cargoship'));
        assert(Number.isInteger(view.time));
        assert.equal(view.time, warmup[1].time, 'canonical pause did not freeze the selected scene time');
        for (const vector of [view.viewOrigin, view.viewForward]) {
            assert.equal(vector?.length, 3);
            assert(vector.every(Number.isFinite));
        }
        assert.deepEqual(view.viewOrigin, [-9732, -9384, 2101], 'free camera did not settle at the requested position');
        assert.deepEqual(view.viewForward, views[0].viewForward, 'free camera direction changed');
        for (const key of ['tanHalfFovX', 'tanHalfFovY', 'zNear',
            'worldSurfaceCount', 'worldVertexCount', 'worldIndexCount']) {
            assert(Number.isFinite(view[key]) && view[key] > 0, key);
        }
        const { submissionGeneration, time, viewOrigin, viewForward, viewport,
            tanHalfFovX, tanHalfFovY, zNear, worldSurfaceCount, worldVertexCount, worldIndexCount } = view;
        return { submissionGeneration, time, viewOrigin, viewForward, viewport,
            tanHalfFovX, tanHalfFovY, zNear, worldSurfaceCount, worldVertexCount, worldIndexCount };
    });
    return { mode: 'paused-renderer', requestedMapSeed: 1, fixedtime: 16, pauseAfterView: 60, firstView: 240, lastView: 540, trace };
}

export function compareWorkloads(runs) {
    assert(runs.length >= 2, 'supply at least two controlled runs in execution order');
    for (const run of runs) {
        assert(run.workload, 'legacy/uncontrolled runs are not comparable');
        assert.equal(run.workload.mode, 'paused-renderer');
        assert.equal(run.pageErrorCount, 0);
        assert.equal(run.cleanTiming.clock, runs[0].cleanTiming.clock, 'timing methods differ');
        if (run.cleanTiming.checkpointSpanFrames === 60) {
            assert.equal(run.cleanTiming.intervals.sampleCount, 5);
            assert.equal(run.cleanTiming.frameIntervalsCovered, 300);
            const checkpoints = run.cleanTiming.checkpointTimes;
            assert.equal(checkpoints.length, 6);
            for (let index = 0; index < checkpoints.length; ++index) {
                assert.equal(checkpoints[index].generation, 240 + index * 60);
                assert(Number.isFinite(checkpoints[index].at));
                if (index) assert(checkpoints[index].at > checkpoints[index - 1].at);
            }
            assert(Math.abs(run.cleanTiming.intervals.average -
                (checkpoints[5].at - checkpoints[0].at) / 300) < 1e-9);
        } else assert.equal(run.cleanTiming.intervals.sampleCount, 300);
        assert.equal(run.cleanTiming.profilerActive, false);
        assert.equal(run.cleanTiming.diagnosticBuild, false);
        assert.equal(run.cleanTiming.foreground.performanceWindowValid, true);
        assert.deepEqual(run.environment, runs[0].environment, 'benchmark environments differ');
        assert.deepEqual(run.workload, runs[0].workload, 'camera/time/geometry checkpoints differ');
    }
    return runs.map(run => ({ artifactSha256: run.artifactSha256,
        averageMs: run.cleanTiming.intervals.average,
        p95Ms: run.cleanTiming.checkpointSpanFrames ? undefined : run.cleanTiming.intervals.p95,
        spanMeanP95Ms: run.cleanTiming.checkpointSpanFrames ? run.cleanTiming.intervals.p95 : undefined }));
}

export function validateProfileWindow(frames, views, workload) {
    assert.equal(frames.length, 120);
    assert.equal(views.length, 120);
    return frames.map((frame, index) => {
        assert.equal(frame.viewSubmissionGeneration, 601 + index, 'profile missed its canonical view window');
        assert.equal(views[index].submissionGeneration, 601 + index);
        for (const [key, value] of Object.entries(workload.trace[0])) {
            if (key !== 'submissionGeneration') assert.deepEqual(views[index][key], value, `profile ${key} differs from clean window`);
        }
        const counts = {};
        for (const key of ['worldSurfacesSubmitted', 'worldSurfacesDrawn', 'staticModelInstancesRetained',
            'staticModelInstanceDraws', 'dynamicBatchesDrawn', 'fxModelBatchesDrawn', 'particleBatchesDrawn',
            'markBatchesDrawn', 'shadowCasterDraws', 'sunShadowMergedRanges', 'submittedIndices', 'bufferUploadBytes',
            'dynamicCommandVertices', 'dynamicCommandIndices', 'uiCommandVertices', 'uiCommandIndices']) {
            assert(Number.isSafeInteger(frame.counters[key]) && frame.counters[key] >= 0, key);
            counts[key] = frame.counters[key];
        }
        return counts;
    });
}

export function compareProfileWorkloads(runs) {
    assert(runs.length >= 2, 'supply at least two diagnostic runs');
    for (const run of runs) {
        assert.equal(run.workload?.mode, 'paused-renderer');
        assert.equal(run.cleanTiming.diagnosticBuild, true);
        assert.equal(run.cleanTiming.profilerActive, false);
        assert.equal(run.cleanTiming.foreground.performanceWindowValid, true);
        assert.equal(run.methodology.foreground.performanceWindowValid, true);
        assert.equal(run.pageErrors.length, 0);
        assert.deepEqual(run.environment, runs[0].environment, 'diagnostic environments differ');
        assert.deepEqual(run.workload, runs[0].workload, 'diagnostic camera/time checkpoints differ');
        assert.equal(run.workCounts.length, 120);
        for (let index = 0; index < 120; ++index) {
            for (const [key, value] of Object.entries(runs[0].workCounts[index])) {
                assert.equal(run.workCounts[index][key], value, `diagnostic view ${601 + index}: ${key} differs`);
            }
        }
    }
    return runs.map(run => ({ artifactSha256: run.artifactSha256, matchingWorkCountSamples: 120 }));
}

// Explicit qualification for retained brushes and opaque sun-range merging.
// The original comparator remains strict for optimizations with unchanged work.
export function compareRetainedRendererWorkloads(runs) {
    assert(runs.length >= 2);
    const baseline = runs[0];
    const normalized = [baseline];
    const changes = [];
    for (const run of runs.slice(1)) {
        assert.equal(run.workCounts.length, 120);
        const deltas = run.workCounts.map((counts, index) => {
            const original = baseline.workCounts[index];
            const vertices = original.dynamicCommandVertices - counts.dynamicCommandVertices;
            const indices = original.dynamicCommandIndices - counts.dynamicCommandIndices;
            const bytes = original.bufferUploadBytes - counts.bufferUploadBytes;
            assert(vertices > 0 && indices > 0);
            assert.equal(bytes, vertices * 72 + indices * 4, 'upload reduction must equal retained brush geometry');
            assert(Number.isSafeInteger(counts.sunShadowMergedRanges) && counts.sunShadowMergedRanges > 0);
            assert.equal(counts.shadowCasterDraws + counts.sunShadowMergedRanges,
                original.shadowCasterDraws, 'logical caster ranges changed');
            return { vertices, indices, bytes, mergedSunRanges: counts.sunShadowMergedRanges };
        });
        for (const delta of deltas) assert.deepEqual(delta, deltas[0], 'optimization work changed within paused window');
        normalized.push({ ...run, workCounts: run.workCounts.map((counts, index) => ({
            ...counts,
            dynamicCommandVertices: counts.dynamicCommandVertices + deltas[index].vertices,
            dynamicCommandIndices: counts.dynamicCommandIndices + deltas[index].indices,
            bufferUploadBytes: counts.bufferUploadBytes + deltas[index].bytes,
            shadowCasterDraws: counts.shadowCasterDraws + counts.sunShadowMergedRanges,
        })) });
        changes.push({ artifactSha256: run.artifactSha256, matchingLogicalWorkSamples: 120, ...deltas[0] });
    }
    compareProfileWorkloads(normalized); // all other counts and workload metadata must match exactly
    return changes;
}

// Only physical sun submissions may change; vertices, indices, uploads,
// camera selection and the sum of submitted/merged caster ranges stay exact.
export function compareShadowRangeWorkloads(runs) {
    const normalized = runs.map(run => ({ ...run, workCounts: run.workCounts.map(counts => {
        assert(Number.isSafeInteger(counts.sunShadowMergedRanges) && counts.sunShadowMergedRanges >= 0);
        assert.equal(counts.sunShadowMergedRanges, run.workCounts[0].sunShadowMergedRanges,
            'merged range count changed within paused window');
        assert.equal(counts.shadowCasterDraws, run.workCounts[0].shadowCasterDraws,
            'submitted caster count changed within paused window');
        return { ...counts, shadowCasterDraws: counts.shadowCasterDraws + counts.sunShadowMergedRanges,
            sunShadowMergedRanges: 0 };
    }) }));
    compareProfileWorkloads(normalized);
    return runs.map(run => ({ artifactSha256: run.artifactSha256, matchingLogicalWorkSamples: 120,
        shadowCasterDraws: run.workCounts[0].shadowCasterDraws,
        sunShadowMergedRanges: run.workCounts[0].sunShadowMergedRanges }));
}

// Static sun casters may reduce both submitted instances and their index work.
// Camera/static retention, dynamic commands, uploads and merged ranges stay exact.
export function compareStaticShadowPartitionWorkloads(runs) {
    assert(runs.length >= 2);
    const baseline = runs[0];
    const normalized = [baseline];
    const changes = [];
    for (const run of runs.slice(1)) {
        assert.equal(run.workCounts.length, 120);
        const deltas = run.workCounts.map((counts, index) => {
            const original = baseline.workCounts[index];
            const casterDraws = original.shadowCasterDraws - counts.shadowCasterDraws;
            const submittedIndices = original.submittedIndices - counts.submittedIndices;
            assert(casterDraws > 0, 'static partition culling must reduce caster instances');
            assert(submittedIndices > 0, 'static partition culling must reduce submitted indices');
            return { casterDraws, submittedIndices };
        });
        for (const delta of deltas)
            assert.deepEqual(delta, deltas[0],
                'static shadow work changed within paused window');
        normalized.push({ ...run, workCounts: run.workCounts.map((counts, index) => ({
            ...counts,
            shadowCasterDraws: counts.shadowCasterDraws + deltas[index].casterDraws,
            submittedIndices: counts.submittedIndices + deltas[index].submittedIndices,
        })) });
        changes.push({ artifactSha256: run.artifactSha256,
            matchingLogicalWorkSamples: 120, ...deltas[0] });
    }
    compareProfileWorkloads(normalized);
    return changes;
}

// Light-space partition culling may reduce shadow draws, opaque merges and
// submitted indices. Every camera, model, command and upload count must remain
// exact, and the reduction must be stable throughout the paused view.
function compareReducedShadowPartitionWorkloads(runs, family) {
    assert(runs.length >= 2);
    const baseline = runs[0];
    const normalized = [baseline];
    const changes = [];
    for (const run of runs.slice(1)) {
        assert.equal(run.workCounts.length, 120);
        const deltas = run.workCounts.map((counts, index) => {
            const original = baseline.workCounts[index];
            const casterDraws = original.shadowCasterDraws - counts.shadowCasterDraws;
            const mergedRanges = original.sunShadowMergedRanges - counts.sunShadowMergedRanges;
            const submittedIndices = original.submittedIndices - counts.submittedIndices;
            assert(casterDraws > 0, `${family} culling must reduce shadow draws`);
            assert(mergedRanges > 0, `${family} culling must reduce merged ranges`);
            assert(submittedIndices > 0, `${family} culling must reduce submitted indices`);
            return { casterDraws, mergedRanges, submittedIndices };
        });
        for (const delta of deltas)
            assert.deepEqual(delta, deltas[0],
                `${family} work changed within paused window`);
        normalized.push({ ...run, workCounts: run.workCounts.map((counts, index) => ({
            ...counts,
            shadowCasterDraws: counts.shadowCasterDraws + deltas[index].casterDraws,
            sunShadowMergedRanges: counts.sunShadowMergedRanges + deltas[index].mergedRanges,
            submittedIndices: counts.submittedIndices + deltas[index].submittedIndices,
        })) });
        changes.push({ artifactSha256: run.artifactSha256,
            matchingLogicalWorkSamples: 120, ...deltas[0] });
    }
    compareProfileWorkloads(normalized);
    return changes;
}

export function compareWorldShadowPartitionWorkloads(runs) {
    return compareReducedShadowPartitionWorkloads(runs, 'world shadow partition');
}

export function compareDynamicShadowPartitionWorkloads(runs) {
    return compareReducedShadowPartitionWorkloads(runs, 'dynamic shadow partition');
}

// Dynamic spot integration adds only light-space-qualified caster submissions.
// Camera work, retained geometry, uploads and the independent sun path remain
// byte-for-byte/count-for-count stable throughout the paused view.
export function compareDynamicSpotShadowWorkloads(runs) {
    assert(runs.length >= 2);
    const baseline = runs[0];
    const normalized = [baseline];
    const changes = [];
    for (const run of runs.slice(1)) {
        assert.equal(run.workCounts.length, 120);
        const deltas = run.workCounts.map((counts, index) => {
            const original = baseline.workCounts[index];
            const casterDraws = counts.shadowCasterDraws -
                original.shadowCasterDraws;
            const submittedIndices = counts.submittedIndices -
                original.submittedIndices;
            assert(casterDraws > 0,
                'dynamic spot submission must add caster draws');
            assert(submittedIndices > 0,
                'dynamic spot submission must add caster indices');
            return { casterDraws, submittedIndices };
        });
        for (const delta of deltas)
            assert.deepEqual(delta, deltas[0],
                'dynamic spot work changed within paused window');
        normalized.push({ ...run, workCounts: run.workCounts.map(
            (counts, index) => ({
                ...counts,
                shadowCasterDraws:
                    counts.shadowCasterDraws - deltas[index].casterDraws,
                submittedIndices:
                    counts.submittedIndices - deltas[index].submittedIndices,
            })) });
        changes.push({ artifactSha256: run.artifactSha256,
            matchingUnchangedWorkSamples: 120, ...deltas[0] });
    }
    compareProfileWorkloads(normalized);
    return changes;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    if (process.argv[2] === '--measured') {
        const runs = await Promise.all(process.argv.slice(3).map(async path => JSON.parse(await readFile(path, 'utf8'))));
        console.log(JSON.stringify(compareMeasuredWorkloads(runs), null, 2));
        process.exit(0);
    }
    const profiles = process.argv[2] === '--profiles';
    const retained = process.argv[2] === '--retained';
    const shadows = process.argv[2] === '--shadow-ranges';
    const staticShadowPartitions =
        process.argv[2] === '--static-shadow-partitions';
    const worldShadowPartitions =
        process.argv[2] === '--world-shadow-partitions';
    const dynamicShadowPartitions =
        process.argv[2] === '--dynamic-shadow-partitions';
    const dynamicSpotShadows =
        process.argv[2] === '--dynamic-spot-shadows';
    const qualified = profiles || retained || shadows || staticShadowPartitions ||
        worldShadowPartitions || dynamicShadowPartitions || dynamicSpotShadows;
    const runs = await Promise.all(process.argv.slice(qualified ? 3 : 2)
        .map(async path => JSON.parse(await readFile(path, 'utf8'))));
    console.log(JSON.stringify((dynamicSpotShadows
        ? compareDynamicSpotShadowWorkloads
        : dynamicShadowPartitions
        ? compareDynamicShadowPartitionWorkloads
        : worldShadowPartitions
        ? compareWorldShadowPartitionWorkloads
        : staticShadowPartitions
        ? compareStaticShadowPartitionWorkloads
        : shadows ? compareShadowRangeWorkloads
        : retained ? compareRetainedRendererWorkloads
        : profiles ? compareProfileWorkloads : compareWorkloads)(runs), null, 2));
}
