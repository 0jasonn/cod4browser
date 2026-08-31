import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';

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

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const profiles = process.argv[2] === '--profiles';
    const retained = process.argv[2] === '--retained';
    const shadows = process.argv[2] === '--shadow-ranges';
    const runs = await Promise.all(process.argv.slice(profiles || retained || shadows ? 3 : 2).map(async path => JSON.parse(await readFile(path, 'utf8'))));
    console.log(JSON.stringify((shadows ? compareShadowRangeWorkloads : retained ? compareRetainedRendererWorkloads : profiles ? compareProfileWorkloads : compareWorkloads)(runs), null, 2));
}
