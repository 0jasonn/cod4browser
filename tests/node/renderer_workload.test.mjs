import assert from 'node:assert/strict';
import test from 'node:test';
import { validateWorkload, compareWorkloads } from '../../tools/renderer_workload.mjs';

test('controlled comparison rejects clock, camera, geometry and sampling mismatches', () => {
    const views = Array.from({ length: 6 }, (_, index) => ({ submissionGeneration: 60 + index * 60,
        time: 1960 + index * 960, geometrySubmitted: true, worldName: 'cargoship',
        viewOrigin: [1, 2, 3], viewForward: [1, 0, 0], viewport: { x: 0, y: 0, width: 640, height: 480 },
        tanHalfFovX: 1, tanHalfFovY: 0.75, zNear: 4,
        worldSurfaceCount: 2, worldVertexCount: 6, worldIndexCount: 6 }));
    const run = { workload: validateWorkload(views), artifactSha256: 'a', environment: { browser: 'test' },
        cleanTiming: { profilerActive: false, foreground: { performanceWindowValid: true },
            intervals: { sampleCount: 300, average: 20, p95: 22 } } };
    assert.equal(compareWorkloads([run, { ...run, artifactSha256: 'b' }]).length, 2);
    for (const mutate of [v => { v[1].time++; }, v => { v[2].submissionGeneration++; },
        v => { v[3].viewOrigin[0] = NaN; }, v => { v.pop(); }]) {
        const changed = structuredClone(views);
        mutate(changed);
        assert.throws(() => validateWorkload(changed));
    }
    for (const mutate of [r => { delete r.workload; }, r => { r.workload.trace[1].viewOrigin[0]++; },
        r => { r.workload.trace[0].worldIndexCount++; }, r => { r.environment.browser = 'other'; },
        r => { r.cleanTiming.intervals.sampleCount--; }, r => { r.cleanTiming.profilerActive = true; }]) {
        const changed = structuredClone(run);
        mutate(changed);
        assert.throws(() => compareWorkloads([run, changed]));
    }
});
