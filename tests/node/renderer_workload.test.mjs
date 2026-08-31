import assert from 'node:assert/strict';
import test from 'node:test';
import { validateWorkload, compareWorkloads, validateProfileWindow } from '../../tools/renderer_workload.mjs';

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
        'markBatchesDrawn', 'shadowCasterDraws', 'submittedIndices', 'bufferUploadBytes'].map(key => [key, 3]));
    const frames = profileViews.map(view => ({ viewSubmissionGeneration: view.submissionGeneration, counters }));
    assert.deepEqual(validateProfileWindow(frames, profileViews, run.workload), frames.map(frame => frame.counters));
    assert.throws(() => validateProfileWindow(frames.slice(1), profileViews, run.workload));
    assert.throws(() => validateProfileWindow(frames, profileViews.map(view => ({ ...view, time: view.time + 1 })), run.workload));
    assert.throws(() => validateProfileWindow(frames.map(frame => ({ ...frame, viewSubmissionGeneration: 600 })), profileViews, run.workload));
    assert.throws(() => validateProfileWindow(frames.map(frame => ({ ...frame, counters: { ...counters, shadowCasterDraws: NaN } })), profileViews, run.workload));
});
