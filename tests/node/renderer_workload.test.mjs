import assert from 'node:assert/strict';
import test from 'node:test';
import { runInNewContext } from 'node:vm';
import { aggregateGameplayProfile, summarizeProfileSamples } from '../browser/retail_profile_aggregate.mjs';
import { validateWorkload, compareWorkloads, validateProfileWindow, compareProfileWorkloads, installFrameTimingCapture, validateFrameTimingCapture, validateGpuDrain, validateBenchmarkEnvironment, compareMeasuredWorkloads, ACTIVE_WORKLOAD_TIMES, normalizeActiveViews, validateActiveWorkload, validateActiveProfileViews, summarizeClockAdvancement, KILLHOUSE_REALTIME_CAMERA, normalizeRealtimeKillhouseViews, validatePreImportCadence } from '../../tools/renderer_workload.mjs';

test('controlled comparison rejects clock, camera, geometry and sampling mismatches', () => {
    const warmup = [{ submissionGeneration: 30, time: 1480 }, { submissionGeneration: 60, time: 1960 }];
    const views = Array.from({ length: 6 }, (_, index) => ({ submissionGeneration: 240 + index * 60,
        time: 1960, geometrySubmitted: true, worldName: 'cargoship',
        viewOrigin: [-9732, -9384, 2101], viewForward: [1, 0, 0], viewport: { x: 0, y: 0, width: 640, height: 480 },
        tanHalfFovX: 1, tanHalfFovY: 0.75, zNear: 4,
        worldSurfaceCount: 2, worldVertexCount: 6, worldIndexCount: 6 }));
    const run = { workload: validateWorkload(views, warmup), artifactSha256: 'a', environment: { browser: 'test' },
        pageErrorCount: 0, cleanTiming: { profilerActive: false, diagnosticBuild: false, foreground: { performanceWindowValid: true },
            intervals: { sampleCount: 300, average: 20, p95: 22 } } };
    assert.equal(compareWorkloads([run, { ...run, artifactSha256: 'b' }]).length, 2);
    for (const mutate of [v => { v[1].time++; }, v => { v[2].submissionGeneration++; },
        v => { v[3].viewOrigin[0] = NaN; }, v => { v[0].viewOrigin[2]++; },
        v => { v[3].viewForward[0] = 0.5; }, v => { v.pop(); }]) {
        const changed = structuredClone(views);
        mutate(changed);
        assert.throws(() => validateWorkload(changed, warmup));
    }
    assert.throws(() => validateWorkload(views, [{ ...warmup[0], time: 1479 }, warmup[1]]));
    for (const mutate of [r => { delete r.workload; }, r => { r.workload.trace[1].viewOrigin[0]++; },
        r => { r.workload.trace[0].worldIndexCount++; }, r => { r.environment.browser = 'other'; },
        r => { r.cleanTiming.intervals.sampleCount--; }, r => { r.cleanTiming.profilerActive = true; },
        r => { r.cleanTiming.diagnosticBuild = true; }, r => { r.pageErrorCount = 1; },
        r => { r.cleanTiming.foreground.performanceWindowValid = false; }]) {
        const changed = structuredClone(run);
        mutate(changed);
        assert.throws(() => compareWorkloads([run, changed]));
    }
    const profileViews = Array.from({ length: 120 }, (_, index) => ({ ...views[0], submissionGeneration: 601 + index }));
    const counters = Object.fromEntries(['worldSurfacesSubmitted', 'worldSurfacesDrawn', 'staticModelInstancesRetained',
        'staticModelInstanceDraws', 'dynamicBatchesDrawn', 'fxModelBatchesDrawn', 'particleBatchesDrawn',
        'markBatchesDrawn', 'shadowCasterDraws', 'sunShadowMergedRanges', 'submittedIndices', 'bufferUploadBytes',
        'dynamicCommandVertices', 'dynamicCommandIndices', 'uiCommandVertices', 'uiCommandIndices'].map(key => [key, 3]));
    const frames = profileViews.map(view => ({ viewSubmissionGeneration: view.submissionGeneration, counters }));
    assert.deepEqual(validateProfileWindow(frames, profileViews, run.workload), frames.map(frame => frame.counters));
    assert.throws(() => validateProfileWindow(frames.slice(1), profileViews, run.workload));
    assert.throws(() => validateProfileWindow(frames, profileViews.map(view => ({ ...view, time: view.time + 1 })), run.workload));
    assert.throws(() => validateProfileWindow(frames.map(frame => ({ ...frame, viewSubmissionGeneration: 600 })), profileViews, run.workload));
    assert.throws(() => validateProfileWindow(frames.map(frame => ({ ...frame, counters: { ...counters, shadowCasterDraws: NaN } })), profileViews, run.workload));
    const missingCounter = { ...counters };
    delete missingCounter.sunShadowMergedRanges;
    assert.throws(() => validateProfileWindow(frames.map(frame => ({ ...frame, counters: missingCounter })), profileViews, run.workload));
    const diagnostic = { ...run, cleanTiming: { ...run.cleanTiming, diagnosticBuild: true },
        workCounts: frames.map(frame => frame.counters), methodology: { foreground: { performanceWindowValid: true } }, pageErrors: [] };
    assert.equal(compareProfileWorkloads([diagnostic, structuredClone(diagnostic)]).length, 2);
    for (const key of ['bufferUploadBytes', 'submittedIndices', 'shadowCasterDraws',
        'dynamicCommandVertices', 'dynamicCommandIndices', 'uiCommandVertices', 'uiCommandIndices']) {
        const changed = structuredClone(diagnostic);
        // Preserve every camera checkpoint and all other draw counts.
        changed.workCounts[10][key]++;
        assert.throws(() => compareProfileWorkloads([diagnostic, changed]));
    }
});


test('bounded Worker batches retain individual frame tails and reject lost samples', () => {
    const listeners = new Map();
    const context = { addEventListener: (name, fn) => listeners.set(name, fn) };
    runInNewContext(`(${installFrameTimingCapture.toString()})({ map: 'cargoship', capacity: 301 })`, context);
    for (let frame = 240; frame <= 540; ++frame) {
        const at = frame * 17 + (frame >= 300 ? 200 : 0);
        context.kisakFrameTiming.record({ started: at, simulationStarted: at + 1,
            submissionStarted: at + 2, completed: at + 3, wallMilliseconds: frame === 300 ? 217 : 17,
            simulationMilliseconds: 16, scene: { world: 'cargoship', generation: frame, time: frame * 16,
                geometry: true, contextGeneration: 1, worldGeneration: 1 } });
    }
    const capture = structuredClone(context.__frameTimingCapture);
    assert.equal(context.kisakFrameTiming, null);
    const intervals = validateFrameTimingCapture(capture);
    assert.equal(intervals.length, 300);
    assert.equal(Math.max(...intervals), 217, 'gameplay stall must remain in the distribution');
    for (const change of [c => { c.complete = false; }, c => { c.dropped = 1; },
        c => { c.samples.splice(1, 1); }, c => { c.samples[10].scene.generation++; },
        c => { c.samples[10].completed = NaN; }, c => { c.samples[10].simulationStarted = -1; }]) {
        const invalid = structuredClone(capture); change(invalid);
        assert.throws(() => validateFrameTimingCapture(invalid));
    }
    assert.throws(() => validateFrameTimingCapture(capture, true));
    runInNewContext(`(${installFrameTimingCapture.toString()})({ map: 'cargoship', realtime: true, capacity: 301 })`, context);
    for (let frame = 240; frame <= 541; ++frame) {
        context.kisakFrameTiming.record({ completed: frame, simulationMilliseconds: 16,
            scene: { world: 'cargoship', generation: frame, time: 45000 + (frame - 240) * 16, geometry: true, contextGeneration: 1, worldGeneration: 1 } });
    }
    assert.equal(context.__frameTimingCapture.dropped, 1);
    assert.throws(() => validateFrameTimingCapture(context.__frameTimingCapture, true));
});

test('GPU drain and hardware checks reject incomplete or incomparable windows', () => {
    const frame = { pumpTick: 5, contextGeneration: 2, worldGeneration: 3, viewSubmissionGeneration: 601,
        gpu: { queryIssued: true, queryDropped: false, timingsAvailable: true, stage: 'world' } };
    const result = { ...frame, gpu: { stage: 'world', status: 'valid', stageMs: 1, queryLagFrames: 1 } };
    assert.equal(validateGpuDrain([frame], [result]).complete, true);
    assert.throws(() => validateGpuDrain([frame], []));
    assert.throws(() => validateGpuDrain([frame], undefined));
    assert.throws(() => validateGpuDrain([frame], [{ ...result, gpu: { ...result.gpu, stageMs: NaN } }]));
    assert.throws(() => validateGpuDrain([frame], [{ ...result, gpu: { ...result.gpu, queryLagFrames: -1 } }]));
    assert.throws(() => validateGpuDrain([frame], [result, result]));
    assert.throws(() => validateGpuDrain([frame], [{ ...result, worldGeneration: 9 }]));
    assert.throws(() => validateGpuDrain([frame], [{ ...result, gpu: { stage: 'world', status: 'disjoint' } }]));
    const environment = { headless: false, renderSize: { width: 1920, height: 1080 },
        foreground: { performanceWindowValid: true }, gpu: { renderer: 'ANGLE NVIDIA GPU', vendor: 'NVIDIA', version: 'WebGL 2' },
        systemGpu: { devices: [{ driverVersion: '1' }] }, displayCadence: { sampleCount: 240 },
        graphicsSettings: Object.fromEntries(Array.from({ length: 10 }, (_, index) => [`r_${index}`, '1'])) };
    validateBenchmarkEnvironment(environment);
    for (const mutate of [e => { e.headless = true; }, e => { e.gpu.renderer = 'ANGLE SwiftShader'; },
        e => { e.renderSize.width = 1440; }, e => { e.foreground.performanceWindowValid = false; },
        e => { e.systemGpu.devices = []; }, e => { e.displayCadence.sampleCount = 3; },
        e => { e.graphicsSettings = {}; }]) {
        const changed = structuredClone(environment); mutate(changed);
        assert.throws(() => validateBenchmarkEnvironment(changed));
    }
});

test('fixed active work aligns exact canonical times while preserving raw generation and workload checks', () => {
    const makeSample = (index, firstGeneration) => {
        const at = index * 17 + (index >= 60 ? 200 : 0);
        return { started: at, simulationStarted: at + 1, submissionStarted: at + 2, completed: at + 3,
            wallMilliseconds: index === 60 ? 217 : 17, simulationMilliseconds: 16,
            scene: { world: 'cargoship', generation: firstGeneration + index,
                time: ACTIVE_WORKLOAD_TIMES.first + index * 16, geometry: true,
                contextGeneration: 1, worldGeneration: 2 } };
    };
    const capture = (firstGeneration, change = () => {}) => {
        const context = {};
        runInNewContext(`(${installFrameTimingCapture.toString()})(${JSON.stringify({
            map: 'cargoship', timeWindow: ACTIVE_WORKLOAD_TIMES })})`, context);
        const startup = makeSample(0, firstGeneration);
        startup.scene.generation = 30;
        startup.scene.time = 779;
        context.kisakFrameTiming.record(startup);
        // Earlier canonical work is excluded by its authored time, before its
        // measured performance is known, even when its generation reaches 240.
        context.kisakFrameTiming.record(makeSample(-1, firstGeneration));
        assert.equal(context.__frameTimingCapture.samples.length, 0);
        const samples = Array.from({ length: 301 }, (_, index) => makeSample(index, firstGeneration));
        change(samples);
        for (const sample of samples) context.kisakFrameTiming?.record(sample);
        assert.equal(context.kisakFrameTiming, null, 'bounded capture must release per-view telemetry');
        return structuredClone(context.__frameTimingCapture);
    };
    const makeView = (time, submissionGeneration) => ({ time, submissionGeneration,
        worldName: 'cargoship', geometrySubmitted: true, viewOrigin: [10, 20, 30],
        viewForward: [1, 0, 0], viewport: { width: 1920, height: 1080 },
        worldSurfaceCount: 7, worldVertexCount: 21, worldIndexCount: 21 });
    const first = capture(240);
    const delayed = capture(241);
    assert.equal(first.samples[0].scene.generation, 240);
    assert.equal(delayed.samples[0].scene.generation, 241, 'raw frame identity must not be rewritten');
    assert.equal(Math.max(...validateFrameTimingCapture(delayed)), 217, 'stalls must remain');
    assert.deepEqual(validateFrameTimingCapture(first), validateFrameTimingCapture(delayed));
    for (const change of [frames => { frames.shift(); }, frames => { frames[100].scene.time += 16; },
        frames => { frames[100].scene.generation++; }, frames => { frames.splice(100, 1); }])
        assert.throws(() => validateFrameTimingCapture(capture(241, change)));
    const incomplete = structuredClone(delayed);
    incomplete.samples.pop();
    assert.throws(() => validateFrameTimingCapture(incomplete));
    for (const phase of [null, { generation: 30, time: 763 }, { generation: 31, time: 779 }]) {
        const invalid = structuredClone(first);
        invalid.startupPhase = phase;
        assert.throws(() => validateFrameTimingCapture(invalid));
    }

    const counters = Object.fromEntries(['worldSurfacesSubmitted', 'worldSurfacesDrawn', 'staticModelInstancesRetained',
        'staticModelInstanceDraws', 'dynamicBatchesDrawn', 'fxModelBatchesDrawn', 'particleBatchesDrawn',
        'markBatchesDrawn', 'shadowCasterDraws', 'sunShadowMergedRanges', 'submittedIndices',
        'dynamicCommandVertices', 'dynamicCommandIndices', 'uiCommandVertices', 'uiCommandIndices'].map(key => [key, 1]));
    const makeRun = frameTiming => {
        const firstGeneration = frameTiming.samples[0].scene.generation;
        const views = Array.from({ length: 6 }, (_, index) => makeView(4139 + index * 960, firstGeneration + index * 60));
        const profileViews = Array.from({ length: 120 }, (_, index) => makeView(9915 + index * 16, firstGeneration + 361 + index));
        const frameSamples = profileViews.map((view, index) => ({ pumpTick: 1000 + index, observedMs: index * 17,
            contextGeneration: 1, worldGeneration: 2, viewSubmissionGeneration: view.submissionGeneration, counters,
            gpu: { timingsAvailable: true, queryIssued: true, queryDropped: false, stage: 'world' } }));
        const gpuResults = frameSamples.map(frame => ({ ...frame,
            gpu: { stage: 'world', status: 'valid', stageMs: 2, queryLagFrames: 1 } }));
        validateActiveProfileViews(profileViews, frameSamples, frameTiming);
        const intervals = summarizeProfileSamples(validateFrameTimingCapture(frameTiming));
        return { schemaVersion: 2, benchmarkDriver: { sha256: 'd'.repeat(64) },
            pageErrorCount: 0, artifactSha256: 'artifact', source: { commitSha: 'revision' },
            provenance: { verified: true, receipt: { revision: 'revision', site: { 'kisakcod.wasm': { sha256: 'artifact' } },
                buildInputs: { files: { 'src/web/web_main.cpp': { sha256: 'source' } }, archive: { sha256: 'inputs' } } } },
            environment: { headless: false, renderSize: { width: 1920, height: 1080 },
                foreground: { performanceWindowValid: true }, gpu: { renderer: 'ANGLE NVIDIA GPU', vendor: 'NVIDIA', version: 'WebGL 2' },
                systemGpu: { devices: [{ driverVersion: '1' }] }, displayCadence: { sampleCount: 240, average: 1000 / 144 },
                graphicsSettings: Object.fromEntries(Array.from({ length: 10 }, (_, index) => [`r_${index}`, '1'])) },
            frameTiming, workload: validateActiveWorkload(views, frameTiming),
            cleanTiming: { intervals, averageSubmissionFps: 1000 / intervals.average, qualified: false },
            profileViews, frameSamples, gpuResults, gpuDrain: validateGpuDrain(frameSamples, gpuResults),
            workCounts: frameSamples.map(frame => frame.counters),
            profile: aggregateGameplayProfile({ frames: frameSamples, gpuResults, capture: { profileComplete: true } }) };
    };
    const a = makeRun(first);
    const b = makeRun(delayed);
    assert.equal(compareMeasuredWorkloads([a, b]).length, 2);
    assert.deepEqual(normalizeActiveViews(a.workload.trace), normalizeActiveViews(b.workload.trace));
    assert.equal(b.profileViews[0].submissionGeneration, 602, 'raw profile generations must remain available');
    for (const mutate of [r => { r.workload.trace[2].viewOrigin[0]++; },
        r => { r.workload.trace[2].time += 16; }, r => { r.workload.trace[2].submissionGeneration++; },
        r => { r.workload.trace.pop(); }, r => { r.profileViews[10].viewForward[1] = 0.5; },
        r => { r.profileViews[10].time += 16; }, r => { r.frameSamples[10].viewSubmissionGeneration++; },
        r => { r.profileViews.forEach(view => view.submissionGeneration++); },
        r => { r.frameSamples[10].counters.worldSurfacesDrawn++; }]) {
        const changed = structuredClone(b); mutate(changed);
        assert.throws(() => compareMeasuredWorkloads([a, changed]));
    }
});

test('active startup phase rejects alternate or missing view 30 before collecting any timings', () => {
    const createCapture = realtime => {
        const context = {};
        runInNewContext(`(${installFrameTimingCapture.toString()})(${JSON.stringify({
            map: 'cargoship', realtime, timeWindow: realtime ? null : ACTIVE_WORKLOAD_TIMES })})`, context);
        return context;
    };
    const sample = (generation, time) => ({ started: time, simulationStarted: time + 1,
        submissionStarted: time + 2, completed: time + 3, wallMilliseconds: 16, simulationMilliseconds: 16,
        scene: { world: 'cargoship', generation, time, geometry: true, contextGeneration: 1, worldGeneration: 1 } });
    for (const [generation, time] of [[30, 763], [30, 795], [31, 795], [240, 4139]]) {
        const context = createCapture(false);
        context.kisakFrameTiming.record(sample(29, 763));
        context.kisakFrameTiming.record(sample(generation, time));
        const state = structuredClone(context.__frameTimingCapture);
        assert.deepEqual(state.startupPhase, { generation, time }, 'record the observed rejected phase');
        assert.equal(state.samples.length, 0, 'phase selection must precede measured timing');
        assert.equal(state.started, null);
        assert.equal(state.complete, true);
        assert.match(state.invalidReason, /startup phase differs/);
        assert.equal(context.kisakFrameTiming, null);
    }
    const accepted = createCapture(false);
    accepted.kisakFrameTiming.record(sample(30, 779));
    assert.equal(accepted.__frameTimingCapture.samples.length, 0);
    assert.equal(accepted.__frameTimingCapture.complete, false);
    accepted.kisakFrameTiming.record(sample(240, 4139));
    assert.equal(accepted.__frameTimingCapture.samples.length, 1);
    assert.deepEqual(structuredClone(accepted.__frameTimingCapture.startupPhase), { generation: 30, time: 779 });
    const realtime = createCapture(true);
    realtime.kisakFrameTiming.record(sample(30, 1234));
    assert.equal(realtime.__frameTimingCapture.complete, false, 'realtime must not select startup phase');
    realtime.kisakFrameTiming.record(sample(240, 9000));
    assert.equal(realtime.__frameTimingCapture.samples.length, 0, 'retain the defined opening warmup');
    realtime.kisakFrameTiming.record(sample(900, 44000));
    assert.equal(realtime.__frameTimingCapture.samples.length, 0, 'settings transition warmup must remain excluded');
    realtime.kisakFrameTiming.record(sample(960, 45000));
    assert.equal(realtime.__frameTimingCapture.samples.length, 1);
    assert.equal(realtime.__frameTimingCapture.startupPhase, null);
});


test('realtime qualification rejects stale/menu/paused frames and forged summaries or build identities', () => {
    const samples = Array.from({ length: 6001 }, (_, index) => {
        const at = index * 10;
        return { started: at, simulationStarted: at + 1, submissionStarted: at + 2, completed: at + 3,
            wallMilliseconds: 10, simulationMilliseconds: 10, scene: { world: 'cargoship',
                generation: 240 + index, time: 45000 + index * 10, geometry: true,
                contextGeneration: 1, worldGeneration: 2 } };
    });
    const frameTiming = { samples, map: 'cargoship', dropped: 0, invalidReason: null, complete: true };
    const intervals = summarizeProfileSamples(validateFrameTimingCapture(frameTiming, true));
    const environment = { headless: false, renderSize: { width: 1920, height: 1080 },
        foreground: { performanceWindowValid: true }, gpu: { renderer: 'ANGLE NVIDIA GPU', vendor: 'NVIDIA', version: 'WebGL 2' },
        systemGpu: { devices: [{ driverVersion: '1' }] }, displayCadence: { sampleCount: 240, average: 1000 / 144 },
        graphicsSettings: Object.fromEntries(Array.from({ length: 10 }, (_, index) => [`r_${index}`, '1'])) };
    const run = { schemaVersion: 2, benchmarkDriver: { sha256: 'd'.repeat(64) },
        pageErrorCount: 0, artifactSha256: 'artifact', source: { commitSha: 'revision' },
        provenance: { verified: true, receipt: { revision: 'revision', site: { 'kisakcod.wasm': { sha256: 'artifact' } },
            buildInputs: { files: { 'src/web/web_main.cpp': { sha256: 'source' } }, archive: { sha256: 'inputs' } } } },
        environment, frameTiming, workload: { mode: 'realtime', map: 'cargoship' },
        cleanTiming: { intervals, averageSubmissionFps: 1000 / intervals.average, qualified: true,
            clockAdvancement: summarizeClockAdvancement(frameTiming) } };
    assert.equal(compareMeasuredWorkloads([run, structuredClone(run)]).length, 2);
    for (const sha256 of [undefined, '', 'd'.repeat(63), 'g'.repeat(64), 'e'.repeat(64)]) {
        const changed = structuredClone(run);
        changed.benchmarkDriver = sha256 === undefined ? undefined : { sha256 };
        assert.throws(() => compareMeasuredWorkloads([run, changed]), /benchmark driver/);
    }
    assert.deepEqual(run.cleanTiming.clockAdvancement, { canonicalMilliseconds: 60000,
        workerWallMilliseconds: 60000, canonicalToWallRatio: 1 });
    const stalled = structuredClone(frameTiming);
    for (const sample of stalled.samples.slice(100)) {
        for (const key of ['started', 'simulationStarted', 'submissionStarted', 'completed']) sample[key] += 190;
        sample.scene.time += 90;
    }
    stalled.samples[100].wallMilliseconds = 200;
    stalled.samples[100].simulationMilliseconds = 100;
    assert.equal(Math.max(...validateFrameTimingCapture(stalled, true)), 200, 'retain the entire wall stall');
    assert.deepEqual(summarizeClockAdvancement(stalled), { canonicalMilliseconds: 60090,
        workerWallMilliseconds: 60190, canonicalToWallRatio: 60090 / 60190 });

    const killhouseRun = (generationOffset = 0, timeOffset = 0) => {
        const fixture = structuredClone(run);
        fixture.frameTiming.map = 'killhouse';
        for (const frame of fixture.frameTiming.samples) {
            frame.scene.world = 'maps/killhouse.d3dbsp';
            frame.scene.generation += generationOffset;
            frame.scene.time += timeOffset;
        }
        fixture.workload = { ...fixture.workload, map: 'killhouse',
            requestedCamera: KILLHOUSE_REALTIME_CAMERA,
            trace: Array.from({ length: 12 }, (_, index) => {
                const { scene } = fixture.frameTiming.samples.find(frame => frame.scene.time >= 45000 + index * 5000);
                return { time: scene.time, submissionGeneration: scene.generation, worldName: scene.world,
                    viewOrigin: [3072, -1155, 64.125], viewForward: [-0.18909545, 0.98195869, 0],
                    viewport: { x: 0, y: 0, width: 1920, height: 1080 }, tanHalfFovX: 1, tanHalfFovY: 0.5625,
                    zNear: 4, geometrySubmitted: true, worldSurfaceCount: 2, worldVertexCount: 6, worldIndexCount: 6 };
            }) };
        return fixture;
    };
    const killhouse = killhouseRun();
    const later = killhouseRun(500, 7);
    assert.equal(compareMeasuredWorkloads([killhouse, later]).length, 2,
        'exact camera work may have different realtime timestamps and generations');
    assert.equal(later.workload.trace[0].time, 45007, 'raw checkpoint time must remain available');
    assert.equal(later.workload.trace[0].submissionGeneration, 740, 'raw generation must remain available');
    for (const mutate of [r => { delete r.workload.trace; }, r => { r.workload.trace.pop(); },
        r => { r.workload.trace[3].time++; }, r => { r.workload.trace[3].submissionGeneration++; },
        r => { r.workload.trace[3].worldName = 'maps/cargoship.d3dbsp'; },
        r => { r.workload.trace[3].geometrySubmitted = false; },
        r => { r.workload.trace[3].viewOrigin[2] = 16065; },
        r => { r.workload.trace[3].viewForward = [-0.01647975, 0.0855836, -0.9961947]; },
        r => { r.workload.trace[3].tanHalfFovX = NaN; },
        r => { r.workload.trace[3].worldIndexCount = 0; }]) {
        const changed = structuredClone(killhouse); mutate(changed);
        assert.throws(() => compareMeasuredWorkloads([changed, structuredClone(changed)]),
            'identically invalid views must not qualify, including the floor-facing startup view');
    }
    for (const mutate of [r => { r.workload.trace[3].viewOrigin[2] += 0.001; },
        r => { r.workload.trace[3].viewForward[0] += 0.00001; },
        r => { r.workload.trace[3].tanHalfFovX += 0.001; },
        r => { r.workload.trace[3].worldIndexCount += 3; }]) {
        const changed = structuredClone(killhouse); mutate(changed);
        normalizeRealtimeKillhouseViews(changed.workload, changed.frameTiming);
        assert.throws(() => compareMeasuredWorkloads([killhouse, changed]),
            'target float tolerances must not relax exact camera/projection/geometry comparisons');
    }
    const counters = Object.fromEntries(['worldSurfacesSubmitted', 'worldSurfacesDrawn', 'staticModelInstancesRetained',
        'staticModelInstanceDraws', 'dynamicBatchesDrawn', 'fxModelBatchesDrawn', 'particleBatchesDrawn',
        'markBatchesDrawn', 'shadowCasterDraws', 'sunShadowMergedRanges', 'submittedIndices',
        'dynamicCommandVertices', 'dynamicCommandIndices', 'uiCommandVertices', 'uiCommandIndices'].map(key => [key, 1]));
    const frameSamples = [{ pumpTick: 9, observedMs: 100, contextGeneration: 1, worldGeneration: 2,
        viewSubmissionGeneration: 601, counters, gpu: { timingsAvailable: true, queryIssued: true, queryDropped: false, stage: 'world' } }];
    const gpuResults = [{ ...frameSamples[0], gpu: { stage: 'world', status: 'valid', stageMs: 2, queryLagFrames: 1 } }];
    const profiled = { ...run, frameSamples, gpuResults, workCounts: [counters], profileViews: [{ generation: 601 }],
        gpuDrain: validateGpuDrain(frameSamples, gpuResults),
        profile: aggregateGameplayProfile({ frames: frameSamples, gpuResults, capture: { profileComplete: true } }) };
    assert.equal(compareMeasuredWorkloads([profiled, structuredClone(profiled)]).length, 2);
    for (const mutate of [r => { delete r.gpuResults; }, r => { r.gpuResults[0].gpu.status = 'disjoint'; },
        r => { r.gpuResults[0].gpu.stageMs = 3; }, r => { r.profile.gpu.results = 0; },
        r => { r.gpuDrain.results = 0; }, r => { r.workCounts[0].worldSurfacesDrawn = 2; }]) {
        const changed = structuredClone(profiled); mutate(changed);
        assert.throws(() => compareMeasuredWorkloads([profiled, changed]));
    }

    for (const change of [r => { r.frameTiming.samples[100].scene.generation--; },
        r => { r.frameTiming.samples[100].scene.geometry = false; },
        r => { r.frameTiming.samples[100].scene.world = 'menu'; },
        r => { r.frameTiming.samples[100].scene.time = r.frameTiming.samples[99].scene.time; },
        r => { r.frameTiming.samples[100].simulationMilliseconds = 0; },
        r => { r.cleanTiming.intervals.average = 1; }, r => { r.cleanTiming.qualified = false; },
        r => { r.cleanTiming.clockAdvancement.canonicalMilliseconds++; },
        r => { r.cleanTiming.clockAdvancement.workerWallMilliseconds--; },
        r => { r.cleanTiming.clockAdvancement.canonicalToWallRatio = 0.5; },
        r => { r.artifactSha256 = 'other'; },
        r => { r.provenance.receipt.buildInputs.files['src/web/web_main.cpp'].sha256 = 'other'; }]) {
        const changed = structuredClone(run); change(changed);
        assert.throws(() => compareMeasuredWorkloads([run, changed]));
    }
    for (const change of [sample => { sample.scene = null; }, sample => { sample.scene.generation = 240; },
        sample => { sample.simulationMilliseconds = 0; }, sample => { sample.scene.time = 1000; }]) {
        const context = {};
        runInNewContext(`(${installFrameTimingCapture.toString()})({ map: 'cargoship', realtime: true })`, context);
        context.kisakFrameTiming.record(structuredClone(samples[0]));
        const bad = structuredClone(samples[1]); change(bad);
        context.kisakFrameTiming.record(bad);
        assert.equal(context.__frameTimingCapture.complete, true);
        assert.notEqual(context.__frameTimingCapture.invalidReason, null);
        assert.throws(() => validateFrameTimingCapture(context.__frameTimingCapture, true));
    }
});


test('pre-import cadence selection rejects mismatches before gameplay timing', () => {
    const cadence = { sampleCount: 240, average: 3.97 };
    validatePreImportCadence(cadence, undefined);
    validatePreImportCadence(cadence, 4);
    for (const target of [0, -1, NaN, Infinity, 2.78, 8])
        assert.throws(() => validatePreImportCadence(cadence, target));
    assert.throws(() => validatePreImportCadence({ ...cadence, sampleCount: 1 }, 4));
    assert.throws(() => validatePreImportCadence({ ...cadence, average: NaN }, 4));
});
