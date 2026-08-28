export const CPU_PROFILE_FIELDS = [
    "filesystemMs", "commandMs", "serverMs", "clientOnceMs",
    "commandBufferMs", "clientFrameMs", "cgameFrameMs", "sceneBuildMs",
    "rendererFrontendMs", "soundMs", "rendererBackendMs", "totalMs",
];

export const RENDERER_PROFILE_FIELDS = [
    "setupMs", "lodMs", "sunShadowPrepareMs", "sunShadowDrawMs",
    "spotShadowPrepareMs", "spotShadowDrawMs", "skyMs", "worldMs",
    "staticModelsMs", "dynamicModelsMs", "fxModelsMs", "particlesMs",
    "marksMs", "uiMs", "postProcessMs", "bufferUploadMs", "textureUploadMs",
];

export const COUNTER_PROFILE_FIELDS = [
    "worldSurfacesSubmitted", "worldSurfacesDrawn",
    "staticModelInstancesRetained", "staticModelInstanceDraws",
    "dynamicBatchesDrawn", "fxModelBatchesDrawn", "particleBatchesDrawn",
    "markBatchesDrawn", "worldDrawCalls", "staticModelDrawCalls",
    "dynamicDrawCalls", "fxDrawCalls", "shadowDrawCalls", "uiDrawCalls",
    "postProcessDrawCalls", "queryDrawCalls", "resolveBlits",
    "submittedIndices", "submittedTriangles", "textureBindCalls",
    "programSwitches", "bufferUploadBytes", "textureUploadBytes",
    "unmeasuredTextureUploads", "lodChanges", "shadowCasterDraws",
];

export const GPU_PROFILE_STAGES = [
    "world", "staticModels", "sunShadows", "spotShadows", "dynamicFx", "uiPost",
];

export function summarizeProfileSamples(values)
{
    const finite = values.filter(Number.isFinite)
        .sort((left, right) => left - right);
    if (!finite.length) return null;
    const at = (fraction) => finite[Math.min(finite.length - 1,
        Math.ceil(finite.length * fraction) - 1)];
    return {
        sampleCount: finite.length,
        average: finite.reduce((sum, value) => sum + value, 0) / finite.length,
        p50: at(0.50),
        p95: at(0.95),
        p99: at(0.99),
        maximum: finite.at(-1),
    };
}

function summarizeFields(frames, container, fields)
{
    return Object.fromEntries(fields.map((field) => [field,
        summarizeProfileSamples(frames.map((entry) =>
            entry[container]?.[field]))]));
}

export function aggregateGameplayProfile({
    frames,
    gpuResults,
    capture,
    cleanAverageFrameIntervalMs,
})
{
    const framePumpTicks = new Set(frames.map(({ pumpTick }) => pumpTick));
    const matchedGpuResults = gpuResults.filter(({ pumpTick }) =>
        framePumpTicks.has(pumpTick));
    const gpuStatusCounts = Object.fromEntries(matchedGpuResults.reduce(
        (counts, entry) => {
            const status = entry.gpu?.status ?? "unknown";
            counts.set(status, (counts.get(status) ?? 0) + 1);
            return counts;
        }, new Map()));
    const validGpuResults = matchedGpuResults.filter(
        (entry) => entry.gpu?.status === "valid");
    const frameIntervals = frames.slice(1).map((entry, index) =>
        entry.observedMs - frames[index].observedMs).filter((value) => value >= 0);
    const profiledAverageFrameIntervalMs = frameIntervals.length
        ? frameIntervals.reduce((sum, value) => sum + value, 0) /
            frameIntervals.length
        : null;
    const profilerOverheadPercent = cleanAverageFrameIntervalMs > 0 &&
        profiledAverageFrameIntervalMs !== null
        ? (profiledAverageFrameIntervalMs / cleanAverageFrameIntervalMs - 1) * 100
        : null;

    return {
        sampleCount: frames.length,
        profileComplete: capture.profileComplete,
        profileSamplesRequested: capture.profileSamplesRequested,
        profileSamplesCollected: capture.profileSamplesCollected,
        profileIncompleteReason: capture.profileIncompleteReason,
        captureDurationMs: capture.observedDurationMs,
        cpu: summarizeFields(frames, "cpu", CPU_PROFILE_FIELDS),
        renderer: summarizeFields(frames, "renderer", RENDERER_PROFILE_FIELDS),
        counters: summarizeFields(frames, "counters", COUNTER_PROFILE_FIELDS),
        gpu: {
            gpuStageProfilingAvailable: frames.some(
                (entry) => entry.gpu?.timingsAvailable === true),
            timingsAvailable: frames.some(
                (entry) => entry.gpu?.timingsAvailable === true),
            queriesIssued: frames.filter(
                (entry) => entry.gpu?.queryIssued === true).length,
            queriesDropped: frames.filter(
                (entry) => entry.gpu?.queryDropped === true).length,
            results: matchedGpuResults.length,
            statusCounts: gpuStatusCounts,
            stages: Object.fromEntries(GPU_PROFILE_STAGES.map((stage) => [
                stage,
                summarizeProfileSamples(validGpuResults
                    .filter((entry) => entry.gpu?.stage === stage)
                    .map((entry) => entry.gpu.stageMs)),
            ])),
            queryLagFrames: summarizeProfileSamples(matchedGpuResults
                .map((entry) => entry.gpu?.queryLagFrames)),
        },
        overhead: {
            cleanAverageFrameIntervalMs,
            profiledAverageFrameIntervalMs,
            profilerOverheadPercent,
        },
    };
}
