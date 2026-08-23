#pragma once

#include <web/web_renderer.h>

#include <cstdint>
#include <vector>

struct GfxWorld;
struct GfxBrushModel;

struct WebRendererWorldSceneCommand
{
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<WebRendererWorldBatchDesc> batches;
    std::vector<WebRendererPrimaryLightDesc> primaryLights;
    std::uint32_t sunPrimaryLightIndex = 0u;
    std::uint32_t surfaceCount = 0u;
    std::uint32_t staticModelInstanceCount = 0u;
    std::uint32_t staticModelSurfaceCount = 0u;
    std::uint32_t waterSurfaceCount = 0u;
    std::uint32_t waterMaterialCount = 0u;
    std::uint32_t resolvedSceneSurfaceCount = 0u;
    std::uint32_t resolvedPostSunSurfaceCount = 0u;
    std::uint32_t firstSurfaceIndex = UINT32_MAX;
    std::uint32_t lastSurfaceIndex = UINT32_MAX;
};

struct WebRendererBrushModelSubmission
{
    const GfxBrushModel *model = nullptr;
    float origin[3]{};
    float axis[3][3]{};
    std::uint16_t entityNumber = 0u;
};

struct WebRendererBrushModelSceneCommand
{
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<WebRendererWorldBatchDesc> batches;
    std::uint32_t modelCount = 0u;
    std::uint32_t surfaceCount = 0u;
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

// Expands canonical scene-brush submissions from their GfxBrushModel surface
// ranges. Native retains the placement separately; the portable boundary
// applies that same rigid transform while preserving material/lightmap state.
WebRendererWorldSceneResult WebRenderer_BuildBrushModelSceneCommand(
    const GfxWorld &world,
    const WebRendererBrushModelSubmission *submissions,
    std::uint32_t submissionCount,
    WebRendererBrushModelSceneCommand &destination);

const char *WebRenderer_WorldSceneResultString(
    WebRendererWorldSceneResult result) noexcept;
