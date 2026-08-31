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
    return { mode: 'paused-renderer', fixedtime: 16, pauseAfterView: 60, firstView: 240, lastView: 540, trace };
}

export function compareWorkloads(runs) {
    assert(runs.length >= 2, 'supply at least two controlled runs in execution order');
    for (const run of runs) {
        assert(run.workload, 'legacy/uncontrolled runs are not comparable');
        assert.equal(run.workload.mode, 'paused-renderer');
        assert.equal(run.pageErrorCount, 0);
        assert.equal(run.cleanTiming.intervals.sampleCount, 300);
        assert.equal(run.cleanTiming.profilerActive, false);
        assert.equal(run.cleanTiming.diagnosticBuild, false);
        assert.equal(run.cleanTiming.foreground.performanceWindowValid, true);
        assert.deepEqual(run.environment, runs[0].environment, 'benchmark environments differ');
        assert.deepEqual(run.workload, runs[0].workload, 'camera/time/geometry checkpoints differ');
    }
    return runs.map(run => ({ artifactSha256: run.artifactSha256,
        averageMs: run.cleanTiming.intervals.average, p95Ms: run.cleanTiming.intervals.p95 }));
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
            'markBatchesDrawn', 'shadowCasterDraws', 'submittedIndices', 'bufferUploadBytes']) {
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

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const profiles = process.argv[2] === '--profiles';
    const runs = await Promise.all(process.argv.slice(profiles ? 3 : 2).map(async path => JSON.parse(await readFile(path, 'utf8'))));
    console.log(JSON.stringify((profiles ? compareProfileWorkloads : compareWorkloads)(runs), null, 2));
}
