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
    std::vector<WebRendererWorldSurfaceRange> surfaceRanges;
    std::vector<WebRendererPrimaryLightDesc> primaryLights;
    std::vector<WebRendererSpotShadowCasterDesc> spotShadowCasters;
    std::vector<WebRendererSpotShadowStaticModelDesc> spotShadowStaticModels;
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

struct WebRendererWorldLightTechniqueContext
{
    const WebRendererPrimaryLightDesc *primaryLights = nullptr;
    std::uint32_t primaryLightCount = 0u;
    std::uint32_t sunPrimaryLightIndex = 0u;
    bool sunShadowEnabled = false;
};

// Validates the per-scene primary-light constants copied from refdef. Unlike
// world-command validation this deliberately does not require attenuation
// images: those immutable resources are already retained by light index.
bool WebRenderer_ValidatePrimaryLightFrame(
    const WebRendererPrimaryLightDesc *primaryLights,
    std::uint32_t primaryLightCount) noexcept;

// Builds material/lightmap-aware portable world batches from the canonical
// renderer-owned GfxWorld. Surface order follows Kisak's canonical lit, decal,
// and emissive camera ranges; fixtures without initialized DPVS ranges fall
// back to the world-model range. Contiguous batches preserve that order,
// Material identity, first-pass state bits and base/lightmap GfxImage identity.
// Geometry remains resident as the camera moves. Per-surface index spans allow
// completed canonical DPVS to select camera runs without filtering shadows.
// The command contains world-space vertices; projection remains the frontend matrix
// carried by WebRendererSceneViewDesc.
WebRendererWorldSceneResult WebRenderer_BuildWorldSceneCommand(
    const GfxWorld &world,
    const WebRendererSceneViewDesc &view,
    WebRendererWorldSceneCommand &destination,
    const WebRendererWorldLightTechniqueContext *lightContext = nullptr);

struct WebRendererWorldCameraRange
{
    std::uint32_t batchIndex;
    std::uint32_t firstIndex;
    std::uint32_t indexCount;
    std::uint32_t surfaceCount;
};

using WebRendererWorldShadowRange = WebRendererWorldCameraRange;

bool WebRenderer_ValidateWorldSurfaceRanges(
    const WebRendererWorldSurfaceDesc &surface) noexcept;

// Consumes validated spans in command order; merges only adjacent visible spans
// within the same batch. Failure clears the camera result, never shadow storage.
bool WebRenderer_BuildWorldCameraRanges(
    const std::vector<WebRendererWorldSurfaceRange> &surfaces,
    const std::uint8_t *visibility, std::uint32_t visibilityCount,
    bool visibilityComputed,
    std::vector<WebRendererWorldCameraRange> &destination);

// Builds one sun-cascade selection from canonical surface bounds. Camera DPVS
// visibility is deliberately absent; near and far partitions call this
// independently with their own light-space matrices.
bool WebRenderer_BuildWorldShadowRanges(
    const std::vector<WebRendererWorldSurfaceRange> &surfaces,
    const std::array<float, 16> &shadowMatrix,
    std::vector<WebRendererWorldShadowRange> &destination);

// Expands canonical scene-brush submissions from their GfxBrushModel surface
// ranges. Native retains the placement separately; the portable boundary
// applies that same rigid transform while preserving material/lightmap state.
WebRendererWorldSceneResult WebRenderer_BuildBrushModelSceneCommand(
    const GfxWorld &world,
    const WebRendererBrushModelSubmission *submissions,
    std::uint32_t submissionCount,
    WebRendererBrushModelSceneCommand &destination,
    const WebRendererWorldLightTechniqueContext *lightContext = nullptr);

const char *WebRenderer_WorldSceneResultString(
    WebRendererWorldSceneResult result) noexcept;

// maximumCoordinate bounds position, normal and tangent magnitudes of validated
// retained vertices. Ordinary placements need no per-vertex revalidation; near
// float overflow, check the actual transformed values before GPU submission.
bool WebRenderer_BrushPlacementIsFinite(
    const WebRendererBrushModelInstanceDesc &instance,
    const std::vector<WebRendererSurfaceVertex> &vertices,
    float maximumCoordinate) noexcept;
