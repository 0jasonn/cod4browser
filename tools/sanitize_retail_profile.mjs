import { readFileSync, writeFileSync } from "node:fs";

const [outputPath, ...inputPaths] = process.argv.slice(2);
if (!outputPath || inputPaths.length === 0) {
    throw new Error("usage: node tools/sanitize_retail_profile.mjs <output> <retail-log>...");
}

const resultPrefixes = [
    "KISAK_RETAIL_RESULT ",
    "KISAK_RETAIL_PHASE3_RESULT ",
];

function readResult(inputPath)
{
    const line = readFileSync(inputPath, "utf8").split(/\r?\n/).find(
        (candidate) => resultPrefixes.some((prefix) => candidate.startsWith(prefix)));
    if (!line) throw new Error(`retail result missing from ${inputPath}`);
    const prefix = resultPrefixes.find((candidate) => line.startsWith(candidate));
    return JSON.parse(line.slice(prefix.length));
}

function sanitizeContextRecovery(value)
{
    if (!value) return null;
    if (!value.validationResult) {
        return Object.fromEntries(Object.entries(value).map(([map, result]) => [
            map, sanitizeContextRecovery(result),
        ]));
    }
    return {
        validationResult: value.validationResult,
        durationToFirstRecoveredWorldFrameMs: value.durationToFirstRecoveredWorldFrameMs,
        contextLossesBefore: value.contextLossesBefore,
        contextLossesAfter: value.contextLossesAfter,
        surfaceRecoveryCountBefore: value.surfaceRecoveryCountBefore,
        surfaceRecoveryCountAfter: value.surfaceRecoveryCountAfter,
        resourceGenerationBefore: value.resourceGenerationBefore,
        resourceGenerationAfter: value.resourceGenerationAfter,
        framesResumed: value.framesResumed,
        inputResumed: value.inputResumed,
    };
}

function sanitizeMap(map)
{
    const steady = map.memoryLifecycle?.steadyState;
    return {
        validationResult: map.validationResult,
        checks: map.checks,
        classification: map.classification,
        classificationSource: map.classificationSource,
        cleanPerformance: {
            window: map.cleanPerformanceWindow,
            actualWorldFrames: map.actualWorldFrames,
            averageFrameIntervalMs: map.averageFrameIntervalMs,
            averageFpsEquivalent: map.averageFpsEquivalent,
            p50FrameTimeMs: map.p50FrameTimeMs,
            p95FrameTimeMs: map.p95FrameTimeMs,
            p99FrameTimeMs: map.p99FrameTimeMs,
            minimumFpsEquivalent: map.minimumFpsEquivalent,
            gameTimeAdvancementMs: map.gameTimeAdvancementMs,
            wallTimeAdvancementMs: map.wallTimeAdvancementMs,
            gameTimeWallTimeRatio: map.gameTimeWallTimeRatio,
        },
        profile: map.frameProfile,
        memory: {
            wasmLinearMemoryCapacityBytes: map.wasmLinearMemoryCapacityBytes,
            aggregateCpuRecoveryBytes: map.aggregateCpuRecoveryBytes,
            encodedImageRecoveryBytes: map.encodedImageRecoveryBytes,
            textureRecoverySourceBytes: map.textureRecoverySourceBytes,
            decodedTextureRecoveryBytes: map.decodedTextureRecoveryBytes,
            estimatedGpuTextureBytes: map.estimatedGpuTextureBytes,
            geometryBytes: map.geometryBytes,
            temporaryUploadBytes: map.temporaryUploadBytes,
            steadyState: steady ? {
                imageLoadDefCacheEntryCount: steady.imageLoadDefCacheEntryCount,
                imageLoadDefCacheEncodedPayloadBytes: steady.imageLoadDefCacheEncodedPayloadBytes,
                imageLoadDefCacheBudgetBytes: steady.imageLoadDefCacheBudgetBytes,
                imageLoadDefCacheEvictionCount: steady.imageLoadDefCacheEvictionCount,
                wasmAllocatorInUseBytes: steady.wasmAllocatorInUseBytes,
                wasmAllocatorFreeBytes: steady.wasmAllocatorFreeBytes,
                wasmAllocatorFootprintBytes: steady.wasmAllocatorFootprintBytes,
                imageRecoverySources: steady.imageRecoverySources,
            } : null,
        },
    };
}

const results = inputPaths.map(readResult);
const first = results[0];
const sourceCommitSha = first.source?.commitSha;
if (!sourceCommitSha || results.some((result) =>
    result.source?.commitSha !== sourceCommitSha || result.source?.dirty !== false)) {
    throw new Error("retail logs must come from the same clean commit");
}

const maps = {};
const contextRecovery = {};
for (const result of results) {
    if (result.maps) {
        for (const [name, map] of Object.entries(result.maps)) maps[name] = sanitizeMap(map);
        Object.assign(contextRecovery, sanitizeContextRecovery(result.contextRecovery));
    } else if (result.map) {
        maps[result.map.map] = sanitizeMap(result.map);
        contextRecovery[result.map.map] = sanitizeContextRecovery(result.contextRecovery);
    }
}

const requiredMaps = ["airplane", "killhouse", "blackout", "bog_a", "hunted", "cargoship"];
for (const map of requiredMaps) {
    if (!maps[map]) throw new Error(`required map ${map} missing from retail logs`);
}

const output = {
    schemaVersion: 1,
    recordedAtUtc: new Date().toISOString(),
    source: {
        commitSha: sourceCommitSha,
        dirty: false,
        inputKind: "sanitized aggregate retail validator output",
    },
    environment: first.environment,
    methodology: {
        cleanPerformanceWindowMs: 60000,
        cleanPerformanceProfilingEnabled: false,
        profileTargetCompletedGameplayFrames: 300,
        profileClassificationAuthoritative: false,
        gpuStageSampling: "rotating non-nested EXT_disjoint_timer_query_webgl2",
    },
    maps: Object.fromEntries(requiredMaps.map((name) => [name, maps[name]])),
    contextRecovery,
};

writeFileSync(outputPath, `${JSON.stringify(output, null, 2)}\n`);
