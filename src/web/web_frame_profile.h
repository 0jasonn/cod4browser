#pragma once

#include <cstdint>

enum class WebFrameProfileCaptureState : std::uint8_t
{
    Idle,
    Active,
    Complete,
    Incomplete,
};

enum class WebFrameProfileIncompleteReason : std::uint8_t
{
    None,
    Timeout,
    ContextChanged,
    WorldChanged,
};

enum class WebFrameProfilePumpResult : std::uint8_t
{
    Ignored,
    SampleCollected,
    CaptureComplete,
    CaptureIncomplete,
};

enum class WebFrameProfileGpuStage : std::uint8_t
{
    None,
    World,
    StaticModels,
    SunShadows,
    SpotShadows,
    DynamicFx,
    UiPost,
};

constexpr WebFrameProfileGpuStage WebFrameProfile_GpuStageForOrdinal(
    std::uint32_t ordinal) noexcept
{
    switch (ordinal % 6u)
    {
    case 0u: return WebFrameProfileGpuStage::World;
    case 1u: return WebFrameProfileGpuStage::StaticModels;
    case 2u: return WebFrameProfileGpuStage::SunShadows;
    case 3u: return WebFrameProfileGpuStage::SpotShadows;
    case 4u: return WebFrameProfileGpuStage::DynamicFx;
    default: return WebFrameProfileGpuStage::UiPost;
    }
}

constexpr const char *WebFrameProfile_GpuStageName(
    WebFrameProfileGpuStage stage) noexcept
{
    switch (stage)
    {
    case WebFrameProfileGpuStage::None: return "none";
    case WebFrameProfileGpuStage::World: return "world";
    case WebFrameProfileGpuStage::StaticModels: return "staticModels";
    case WebFrameProfileGpuStage::SunShadows: return "sunShadows";
    case WebFrameProfileGpuStage::SpotShadows: return "spotShadows";
    case WebFrameProfileGpuStage::DynamicFx: return "dynamicFx";
    case WebFrameProfileGpuStage::UiPost: return "uiPost";
    }
    return "unknown";
}

struct WebFrameProfileCapture
{
    void Begin(std::uint32_t targetSamples, double nowMilliseconds,
        double timeoutMilliseconds) noexcept
    {
        state = WebFrameProfileCaptureState::Active;
        incompleteReason = WebFrameProfileIncompleteReason::None;
        requestedSamples = targetSamples;
        collectedSamples = 0u;
        deadlineMilliseconds = nowMilliseconds + timeoutMilliseconds;
        contextGeneration = 0u;
        worldGeneration = 0u;
        identityBound = false;
    }

    bool Poll(double nowMilliseconds) noexcept
    {
        if (state != WebFrameProfileCaptureState::Active ||
            nowMilliseconds < deadlineMilliseconds)
            return false;
        state = WebFrameProfileCaptureState::Incomplete;
        incompleteReason = WebFrameProfileIncompleteReason::Timeout;
        return true;
    }

    WebFrameProfilePumpResult FinishPump(double nowMilliseconds,
        bool gameplayFrame, bool rendererSubmitted,
        std::uint32_t sampleContextGeneration,
        std::uint32_t sampleWorldGeneration) noexcept
    {
        if (state != WebFrameProfileCaptureState::Active)
            return WebFrameProfilePumpResult::Ignored;
        if (Poll(nowMilliseconds))
            return WebFrameProfilePumpResult::CaptureIncomplete;
        if (!gameplayFrame || !rendererSubmitted)
            return WebFrameProfilePumpResult::Ignored;
        if (!identityBound)
        {
            contextGeneration = sampleContextGeneration;
            worldGeneration = sampleWorldGeneration;
            identityBound = true;
        }
        else if (contextGeneration != sampleContextGeneration)
        {
            state = WebFrameProfileCaptureState::Incomplete;
            incompleteReason = WebFrameProfileIncompleteReason::ContextChanged;
            return WebFrameProfilePumpResult::CaptureIncomplete;
        }
        else if (worldGeneration != sampleWorldGeneration)
        {
            state = WebFrameProfileCaptureState::Incomplete;
            incompleteReason = WebFrameProfileIncompleteReason::WorldChanged;
            return WebFrameProfilePumpResult::CaptureIncomplete;
        }

        ++collectedSamples;
        if (collectedSamples == requestedSamples)
        {
            state = WebFrameProfileCaptureState::Complete;
            return WebFrameProfilePumpResult::CaptureComplete;
        }
        return WebFrameProfilePumpResult::SampleCollected;
    }

    std::uint32_t Remaining() const noexcept
    {
        return requestedSamples - collectedSamples;
    }

    WebFrameProfileCaptureState state = WebFrameProfileCaptureState::Idle;
    WebFrameProfileIncompleteReason incompleteReason =
        WebFrameProfileIncompleteReason::None;
    std::uint32_t requestedSamples = 0u;
    std::uint32_t collectedSamples = 0u;
    double deadlineMilliseconds = 0.0;
    std::uint32_t contextGeneration = 0u;
    std::uint32_t worldGeneration = 0u;
    bool identityBound = false;
};

#if KISAK_WEB_DIAGNOSTICS
struct WebFrameProfileSample
{
    std::uint32_t pumpTick = 0u;
    std::uint32_t contextGeneration = 0u;
    std::uint32_t worldGeneration = 0u;
    std::uint32_t viewSubmissionGeneration = 0u;
    bool gameplayFrame = false;
    bool rendererSubmitted = false;
    bool gpuTimingsAvailable = false;
    bool gpuQueryIssued = false;
    bool gpuQueryDropped = false;
    WebFrameProfileGpuStage gpuStage = WebFrameProfileGpuStage::None;

    double filesystemMs = 0.0;
    double commandMs = 0.0;
    double serverMs = 0.0;
    double clientOnceMs = 0.0;
    double commandBufferMs = 0.0;
    double clientFrameMs = 0.0;
    double cgameFrameMs = 0.0;
    double sceneBuildMs = 0.0;
    // Disjoint intervals within sceneBuildMs, alongside dobjBuildMs.
    // Dynamic submission includes backend validation/copy and uploads;
    // renderer upload timers overlap it and must not be added to this total.
    double sceneSetupMs = 0.0;
    double sceneAssemblyMs = 0.0;
    double sceneImageResolveMs = 0.0;
    double sceneDynamicSubmitMs = 0.0;
    double sceneCameraVisibilityMs = 0.0;
    double sceneViewSubmitMs = 0.0;
    // Three disjoint parts of sceneAssemblyMs. Cloud append nests in command
    // append; cloud construction and other appends remain in its parent.
    double sceneEffectsPrepareMs = 0.0;
    double sceneModelBuildMs = 0.0;
    double sceneCommandAppendMs = 0.0;
    double sceneCloudAppendMs = 0.0;
    // Disjoint parts of sceneDynamicSubmitMs; upload timers also overlap them.
    double dynamicCopyMs = 0.0;
    double dynamicGeometryUploadMs = 0.0;
    double dynamicTextureUploadMs = 0.0;
    double dynamicPublishMs = 0.0;
    // DObj build is nested in sceneBuildMs. The four substages are disjoint;
    // validation, LOD/hide tests and other overhead remain in the build total.
    double dobjBuildMs = 0.0;
    double dobjPoseMs = 0.0;
    double dobjLightingMs = 0.0;
    double dobjSkinningMs = 0.0;
    double dobjGeometryMs = 0.0;
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
    std::uint32_t worldGeneration,
    std::uint32_t viewSubmissionGeneration,
    WebFrameProfileGpuStage stage,
    const char *mapName,
    double gpuMilliseconds,
    std::uint32_t queryLagFrames,
    const char *status);
#endif
