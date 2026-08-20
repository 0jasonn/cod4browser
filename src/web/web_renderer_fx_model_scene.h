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

WebRendererFxModelSceneResult WebRenderer_BuildFxModelSceneCommand(
    const WebRendererFxModelSubmission *submissions,
    std::uint32_t submissionCount,
    WebRendererFxModelSceneCommand &destination);

const char *WebRenderer_FxModelSceneResultString(
    WebRendererFxModelSceneResult result) noexcept;
