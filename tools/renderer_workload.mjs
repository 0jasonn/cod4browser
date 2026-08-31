import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';

// Sparse, existing production refdef telemetry; no engine state is authored here.
export function validateWorkload(views) {
    assert.equal(views.length, 6, 'expected views 60 through 360');
    const trace = views.map((view, index) => {
        assert.equal(view.submissionGeneration, 60 + index * 60, 'missing canonical view checkpoint');
        assert.equal(view.geometrySubmitted, true);
        assert(view.worldName?.toLowerCase().includes('cargoship'));
        assert(Number.isInteger(view.time));
        assert.equal(view.time - views[0].time, index * 60 * 16, 'fixedtime 16 was not honored');
        for (const vector of [view.viewOrigin, view.viewForward]) {
            assert.equal(vector?.length, 3);
            assert(vector.every(Number.isFinite));
        }
        for (const key of ['tanHalfFovX', 'tanHalfFovY', 'zNear',
            'worldSurfaceCount', 'worldVertexCount', 'worldIndexCount']) {
            assert(Number.isFinite(view[key]) && view[key] > 0, key);
        }
        const { submissionGeneration, time, viewOrigin, viewForward, viewport,
            tanHalfFovX, tanHalfFovY, zNear, worldSurfaceCount, worldVertexCount, worldIndexCount } = view;
        return { submissionGeneration, time, viewOrigin, viewForward, viewport,
            tanHalfFovX, tanHalfFovY, zNear, worldSurfaceCount, worldVertexCount, worldIndexCount };
    });
    return { fixedtime: 16, firstView: 60, lastView: 360, trace };
}

export function compareWorkloads(runs) {
    assert(runs.length >= 2, 'supply at least two controlled runs in execution order');
    for (const run of runs) {
        assert(run.workload, 'legacy/uncontrolled runs are not comparable');
        assert.equal(run.cleanTiming.intervals.sampleCount, 300);
        assert.equal(run.cleanTiming.profilerActive, false);
        assert.equal(run.cleanTiming.foreground.performanceWindowValid, true);
        assert.deepEqual(run.environment, runs[0].environment, 'benchmark environments differ');
        assert.deepEqual(run.workload, runs[0].workload, 'camera/time/geometry checkpoints differ');
    }
    return runs.map(run => ({ artifactSha256: run.artifactSha256,
        averageMs: run.cleanTiming.intervals.average, p95Ms: run.cleanTiming.intervals.p95 }));
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const runs = await Promise.all(process.argv.slice(2).map(async path => JSON.parse(await readFile(path, 'utf8'))));
    console.log(JSON.stringify(compareWorkloads(runs), null, 2));
}
