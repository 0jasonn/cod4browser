import { readFileSync, writeFileSync } from "node:fs";

const [outputPath, inputPath] = process.argv.slice(2);
if (!outputPath || !inputPath) {
    throw new Error(
        "usage: node tools/sanitize_retail_decode.mjs <output> <retail-log>");
}

const prefix = "KISAK_RETAIL_DECODE_RESULT ";
const line = readFileSync(inputPath, "utf8").split(/\r?\n/).find(
    (candidate) => candidate.startsWith(prefix));
if (!line) throw new Error(`retail decode result missing from ${inputPath}`);
const result = JSON.parse(line.slice(prefix.length));
const requiredChain = [
    "killhouse",
    "cargoship",
    "blackout",
    "hunted",
    "bog_a",
    "airplane",
    "killhouse",
];
if (result.source?.dirty !== false ||
    JSON.stringify(result.chain) !== JSON.stringify(requiredChain) ||
    result.validationResult !== "pass") {
    throw new Error("decode evidence must be a passing clean exact-chain run");
}

const sum = (read) => result.maps.reduce(
    (total, map) => total + read(map), 0);
const output = {
    schemaVersion: 1,
    recordedAtUtc: result.recordedAtUtc,
    source: {
        commitSha: result.source.commitSha,
        dirty: false,
        inputKind: "sanitized aggregate retail decode validator output",
    },
    environment: result.environment,
    methodology: {
        chain: requiredChain,
        settleFramesAfterFirstWorldFrame: 30,
        contextRecoveryPerMap: true,
        encodedImageInspectionCount:
            "new retained 2D image source lookup attempts",
        metadataParseCount: "IWI header parses performed by pixel decode",
        pixelDecodeCount: "2D encoded-source decoder invocations",
        initialUploadDecodeCount:
            "decoder invocations before or during initial WebGL upload",
        contextRecoveryDecodeCount:
            "decoder invocations while rebuilding a restored WebGL context",
        duplicateDecodeCount:
            "initial upload decodes repeated after a successful retention decode",
    },
    maps: result.maps,
    totals: result.totals,
    summary: {
        averageFirstWorldFrameLatencyMs:
            sum((map) => map.firstWorldFrameLatencyMs) / result.maps.length,
        averageRecoveryLatencyMs: sum((map) =>
            map.recovery.durationToFirstRecoveredWorldFrameMs) /
                result.maps.length,
        initialUploadDecodedBytes: sum((map) =>
            map.initialUploadDecode.decodedBytes),
        initialUploadDecodeCpuMilliseconds: sum((map) =>
            map.initialUploadDecode.cpuMilliseconds),
        initialUploadDecodeCount: sum((map) =>
            map.initialUploadDecode.initialUploadDecodeCount),
        immediateDuplicateDecodeCount: sum((map) =>
            map.initialUploadDecode.duplicateDecodeCount),
        recoveryDecodedBytes: sum((map) =>
            map.recovery.decode.decodedBytes),
        recoveryDecodeCpuMilliseconds: sum((map) =>
            map.recovery.decode.cpuMilliseconds),
        maximumSourceCacheBytes: Math.max(...result.maps.map(
            (map) => map.sourceCache.encodedPayloadBytes)),
        sourceCacheBudgetBytes: result.maps[0].sourceCache.budgetBytes,
        sourceCacheEvictionCount: result.maps.at(-1).sourceCache.evictionCount,
        mapOwnedRendererSourcesRetired:
            result.mapOwnedRendererSourcesRetired,
        contextRecoveryValidatedForEveryMap:
            result.contextRecoveryValidatedForEveryMap,
    },
};

writeFileSync(outputPath, `${JSON.stringify(output, null, 2)}\n`);
