#pragma once

#include <web/web_renderer.h>

#include <cstdint>
#include <vector>

struct GfxWorld;

struct WebRendererWorldSceneCommand
{
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<WebRendererWorldBatchDesc> batches;
    std::uint32_t surfaceCount = 0u;
    std::uint32_t staticModelInstanceCount = 0u;
    std::uint32_t staticModelSurfaceCount = 0u;
    std::uint32_t firstSurfaceIndex = UINT32_MAX;
    std::uint32_t lastSurfaceIndex = UINT32_MAX;
};

enum class WebRendererWorldSceneResult : std::uint8_t
{
    Success = 0,
    InvalidWorld,
    InvalidView,
    InvalidSurfaceRange,
    InvalidSurfaceBounds,
    IndexOutOfRange,
    OutputTooLarge,
    NoVisibleSurface,
    AllocationFailed,
};

// Builds material/lightmap-aware portable world batches from the canonical
// renderer-owned GfxWorld. Surface order follows Kisak's canonical lit, decal,
// and emissive camera ranges; fixtures without initialized DPVS ranges fall
// back to the world-model range. Contiguous batches preserve that order while
// exposing Material identity,
// first-pass state bits, base GfxImage identity, and lightmap GfxImage identity.
// Conservative visibility is deliberately disabled for this initial WebGL2
// backend: the static command remains valid as the canonical cgame camera
// moves, while homogeneous clipping stays in WebGL. The command contains
// world-space vertices; projection remains the canonical frontend matrix
// carried by WebRendererSceneViewDesc.
WebRendererWorldSceneResult WebRenderer_BuildWorldSceneCommand(
    const GfxWorld &world,
    const WebRendererSceneViewDesc &view,
    WebRendererWorldSceneCommand &destination);

const char *WebRenderer_WorldSceneResultString(
    WebRendererWorldSceneResult result) noexcept;
