#pragma once

#include <cstdint>

#if KISAK_WEB_DIAGNOSTICS
struct WebFrameProfileSample
{
    std::uint32_t pumpTick = 0u;
    std::uint32_t contextGeneration = 0u;
    std::uint32_t viewSubmissionGeneration = 0u;
    bool gameplayFrame = false;
    bool rendererSubmitted = false;
    bool gpuTimingsAvailable = false;
    bool gpuQueryIssued = false;
    bool gpuQueryDropped = false;

    double filesystemMs = 0.0;
    double commandMs = 0.0;
    double serverMs = 0.0;
    double clientOnceMs = 0.0;
    double commandBufferMs = 0.0;
    double clientFrameMs = 0.0;
    double cgameFrameMs = 0.0;
    double sceneBuildMs = 0.0;
    double rendererFrontendMs = 0.0;
    double soundMs = 0.0;
    double rendererBackendMs = 0.0;
    double totalMs = 0.0;

    double rendererSetupMs = 0.0;
    double lodMs = 0.0;
    double sunShadowPrepareMs = 0.0;
    double sunShadowDrawMs = 0.0;
    double spotShadowPrepareMs = 0.0;
    double spotShadowDrawMs = 0.0;
    double skyMs = 0.0;
    double worldMs = 0.0;
    double staticModelsMs = 0.0;
    double dynamicModelsMs = 0.0;
    double fxModelsMs = 0.0;
    double particlesMs = 0.0;
    double marksMs = 0.0;
    double uiMs = 0.0;
    double postProcessMs = 0.0;
    double bufferUploadMs = 0.0;
    double textureUploadMs = 0.0;

    std::uint64_t worldSurfacesSubmitted = 0u;
    std::uint64_t worldSurfacesDrawn = 0u;
    std::uint64_t staticModelInstancesRetained = 0u;
    std::uint64_t staticModelInstanceDraws = 0u;
    std::uint64_t dynamicBatchesDrawn = 0u;
    std::uint64_t fxModelBatchesDrawn = 0u;
    std::uint64_t particleBatchesDrawn = 0u;
    std::uint64_t markBatchesDrawn = 0u;
    std::uint64_t worldDrawCalls = 0u;
    std::uint64_t staticModelDrawCalls = 0u;
    std::uint64_t dynamicDrawCalls = 0u;
    std::uint64_t fxDrawCalls = 0u;
    std::uint64_t shadowDrawCalls = 0u;
    std::uint64_t uiDrawCalls = 0u;
    std::uint64_t postProcessDrawCalls = 0u;
    std::uint64_t queryDrawCalls = 0u;
    std::uint64_t resolveBlits = 0u;
    std::uint64_t submittedIndices = 0u;
    std::uint64_t submittedTriangles = 0u;
    std::uint64_t textureBindCalls = 0u;
    std::uint64_t programSwitches = 0u;
    std::uint64_t bufferUploadBytes = 0u;
    std::uint64_t textureUploadBytes = 0u;
    std::uint64_t unmeasuredTextureUploads = 0u;
    std::uint64_t lodChanges = 0u;
    std::uint64_t shadowCasterDraws = 0u;
};

bool WebFrameProfile_BeginPump(std::uint32_t pumpTick) noexcept;
WebFrameProfileSample *WebFrameProfile_Current() noexcept;
double WebFrameProfile_Now() noexcept;
void WebFrameProfile_EndPump(bool gameplayFrame, bool rendererSubmitted);
void WebFrameProfile_PublishGpuResult(
    std::uint32_t pumpTick,
    std::uint32_t contextGeneration,
    std::uint32_t viewSubmissionGeneration,
    double gpuMilliseconds,
    std::uint32_t queryLagFrames,
    const char *status);
#endif
