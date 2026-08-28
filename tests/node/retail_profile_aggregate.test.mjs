import assert from "node:assert/strict";
import test from "node:test";

import {
    aggregateGameplayProfile,
    summarizeProfileSamples,
} from "../browser/retail_profile_aggregate.mjs";

test("profile summaries persist complete percentile aggregates", () => {
    assert.deepEqual(summarizeProfileSamples([4, Number.NaN, 1, 3, 2]), {
        sampleCount: 4,
        average: 2.5,
        p50: 2,
        p95: 4,
        p99: 4,
        maximum: 4,
    });
    assert.equal(summarizeProfileSamples([Number.NaN]), null);
});

test("gameplay profile aggregation keeps populations and overhead explicit", () => {
    const frames = [
        {
            pumpTick: 10, observedMs: 100,
            cpu: { totalMs: 8 }, renderer: { worldMs: 2 },
            counters: { worldDrawCalls: 4 },
            gpu: { timingsAvailable: true, queryIssued: true, queryDropped: false },
        },
        {
            pumpTick: 12, observedMs: 120,
            cpu: { totalMs: 12 }, renderer: { worldMs: 6 },
            counters: { worldDrawCalls: 8 },
            gpu: { timingsAvailable: true, queryIssued: true, queryDropped: false },
        },
        {
            pumpTick: 15, observedMs: 140,
            cpu: { totalMs: 10 }, renderer: { worldMs: 4 },
            counters: { worldDrawCalls: 6 },
            gpu: { timingsAvailable: true, queryIssued: false, queryDropped: true },
        },
    ];
    const profile = aggregateGameplayProfile({
        frames,
        gpuResults: [
            { pumpTick: 10, gpu: { status: "valid", backendDrawMs: 5,
                queryLagFrames: 2 } },
            { pumpTick: 11, gpu: { status: "valid", backendDrawMs: 99,
                queryLagFrames: 1 } },
            { pumpTick: 12, gpu: { status: "disjoint", queryLagFrames: 3 } },
        ],
        capture: {
            profileComplete: true,
            profileSamplesRequested: 3,
            profileSamplesCollected: 3,
            profileIncompleteReason: null,
            observedDurationMs: 45,
        },
        cleanAverageFrameIntervalMs: 16,
    });

    assert.equal(profile.sampleCount, 3);
    assert.equal(profile.profileComplete, true);
    assert.deepEqual(profile.cpu.totalMs, {
        sampleCount: 3,
        average: 10,
        p50: 10,
        p95: 12,
        p99: 12,
        maximum: 12,
    });
    assert.equal(profile.renderer.worldMs.average, 4);
    assert.equal(profile.counters.worldDrawCalls.maximum, 8);
    assert.equal(profile.gpu.results, 2);
    assert.deepEqual(profile.gpu.statusCounts, { valid: 1, disjoint: 1 });
    assert.equal(profile.gpu.backendDrawMs.sampleCount, 1);
    assert.equal(profile.overhead.profiledAverageFrameIntervalMs, 20);
    assert.equal(profile.overhead.profilerOverheadPercent, 25);
});
