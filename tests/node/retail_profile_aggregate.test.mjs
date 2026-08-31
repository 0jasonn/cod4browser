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
            { pumpTick: 10, gpu: { status: "valid", stage: "world", stageMs: 5,
                queryLagFrames: 2 } },
            { pumpTick: 11, gpu: { status: "valid", stage: "world", stageMs: 99,
                queryLagFrames: 1 } },
            { pumpTick: 12, gpu: { status: "disjoint", stage: "staticModels",
                queryLagFrames: 3 } },
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
    // Historical samples do not invent zero-valued DObj measurements.
    assert.equal(profile.cpu.dobjBuildMs, null);
    assert.equal(profile.counters.worldDrawCalls.maximum, 8);
    assert.equal(profile.gpu.results, 2);
    assert.deepEqual(profile.gpu.statusCounts, { valid: 1, disjoint: 1 });
    assert.equal(profile.gpu.gpuStageProfilingAvailable, true);
    assert.equal(profile.gpu.stages.world.sampleCount, 1);
    assert.equal(profile.gpu.stages.world.average, 5);
    assert.equal(profile.gpu.stages.staticModels, null);
    assert.equal(profile.overhead.profiledAverageFrameIntervalMs, 20);
    assert.equal(profile.overhead.profilerOverheadPercent, 25);
});

test("DObj build and disjoint substages survive profile aggregation", () => {
    const fields = ["dobjBuildMs", "dobjPoseMs", "dobjLightingMs",
        "dobjSkinningMs", "dobjGeometryMs"];
    const profile = aggregateGameplayProfile({
        frames: [1, 2].map((scale) => ({
            pumpTick: scale, observedMs: scale * 10,
            cpu: Object.fromEntries(fields.map((field, index) =>
                [field, scale * (index === 0 ? 20 : index)])),
        })),
        gpuResults: [],
        capture: {
            profileComplete: true, profileSamplesRequested: 2,
            profileSamplesCollected: 2, profileIncompleteReason: null,
            observedDurationMs: 20,
        },
    });
    for (const [index, field] of fields.entries()) {
        const value = index === 0 ? 20 : index;
        assert.equal(profile.cpu[field].sampleCount, 2);
        assert.equal(profile.cpu[field].average, value * 1.5);
        assert.equal(profile.cpu[field].p95, value * 2);
    }
});
