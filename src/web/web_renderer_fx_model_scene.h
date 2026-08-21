#pragma once

#include <gfx_d3d/gfx_placement_types.h>
#include <web/web_renderer.h>

#include <cstdint>
#include <vector>

struct XModel;

// The WebGL scene is assembled after refdef is available. This keeps the
// retained R_FilterXModelIntoScene submission free of raw view pointers while
// choosing a deterministic rigid-model LOD from the active view distance.
int WebRenderer_SelectFxModelLod(
    const XModel *model, const GfxScaledPlacement &placement,
    const float viewOrigin[3]) noexcept;

struct WebRendererFxModelSubmission
{
    const XModel *model = nullptr;
    GfxScaledPlacement placement{};
    std::uint16_t lod = 0u;
};

constexpr std::uint32_t WEB_RENDERER_MAX_FX_MODEL_SUBMISSIONS = 256u;

enum class WebRendererFxModelRetainResult : std::uint8_t
{
    Accepted = 0,
    InvalidSubmission,
    LimitReached,
};

bool WebRenderer_FxModelPlacementIsValid(
    const GfxScaledPlacement &placement) noexcept;

WebRendererFxModelRetainResult WebRenderer_RetainFxModelSubmission(
    WebRendererFxModelSubmission *storage,
    std::uint32_t *count,
    const XModel *model,
    const GfxScaledPlacement *placement,
    std::uint16_t lod) noexcept;

struct WebRendererFxModelSceneCommand
{
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<WebRendererWorldBatchDesc> batches;
    std::uint32_t modelCount = 0u;
    std::uint32_t surfaceCount = 0u;
};

enum class WebRendererFxModelSceneResult : std::uint8_t
{
    Success = 0,
    NoFxModel,
    InvalidSubmission,
    InvalidPlacement,
    InvalidModel,
    UnsupportedSurface,
    IndexOutOfRange,
    OutputTooLarge,
    AllocationFailed,
};

enum class WebRendererFxModelAppendResult : std::uint8_t
{
    Success = 0,
    InvalidCommand,
    OutputTooLarge,
    AllocationFailed,
};

WebRendererFxModelAppendResult WebRenderer_ValidateFxModelAppendCounts(
    std::size_t sourceVertexCount,
    std::size_t sourceIndexCount,
    std::size_t sourceBatchCount,
    std::uint32_t sourceSurfaceCount,
    std::size_t destinationVertexCount,
    std::size_t destinationIndexCount,
    std::size_t destinationBatchCount,
    std::uint32_t destinationSurfaceCount) noexcept;

WebRendererFxModelSceneResult WebRenderer_BuildFxModelSceneCommand(
    const WebRendererFxModelSubmission *submissions,
    std::uint32_t submissionCount,
    WebRendererFxModelSceneCommand &destination,
    std::uint32_t *droppedCount = nullptr);

WebRendererFxModelAppendResult WebRenderer_AppendFxModelSceneCommand(
    const WebRendererFxModelSceneCommand &source,
    std::vector<WebRendererSurfaceVertex> &vertices,
    std::vector<std::uint32_t> &indices,
    std::vector<WebRendererWorldBatchDesc> &batches,
    std::uint32_t &surfaceCount);

const char *WebRenderer_FxModelSceneResultString(
    WebRendererFxModelSceneResult result) noexcept;
