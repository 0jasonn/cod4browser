#pragma once

#include <web/web_renderer.h>
#include <web/web_renderer_lighting.h>

#include <array>
#include <cstdint>
#include <vector>

struct GfxWorld;

using WebRendererStaticMaterialResolver = Material *(*)(Material *) noexcept;

struct WebRendererStaticModelSceneCommand
{
    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<WebRendererStaticModelInstanceDesc> instances;
    std::vector<WebRendererStaticModelShadowBounds> shadowBounds;
    std::vector<WebRendererStaticModelBatchDesc> batches;
    WebRendererModelLightingAtlas modelLightingAtlas;
    std::uint32_t modelCount = 0u;
    std::uint32_t surfaceCount = 0u;
    std::uint32_t canonicalInstanceCount = 0u;
    std::uint32_t modelLightingFailureCount = 0u;
    bool modelLightingSourceAvailable = false;
};

enum class WebRendererStaticModelSceneResult : std::uint8_t
{
    Success = 0,
    NoStaticModels,
    InvalidWorld,
    InvalidModel,
    InvalidPlacement,
    IndexOutOfRange,
    OutputTooLarge,
    AllocationFailed,
};

WebRendererStaticModelSceneResult WebRenderer_BuildStaticModelSceneCommand(
    const GfxWorld &world,
    WebRendererStaticModelSceneCommand &destination,
    const WebRendererModelLightingCallbacks *lightingCallbacks = nullptr,
    WebRendererStaticMaterialResolver materialResolver = nullptr);

const char *WebRenderer_StaticModelSceneResultString(
    WebRendererStaticModelSceneResult result) noexcept;

// Sun-shadow partition visibility uses authored world bounds and the light
// matrix only. Camera DPVS visibility is intentionally absent from this seam.
bool WebRenderer_StaticModelIntersectsShadowPartition(
    const WebRendererStaticModelShadowBounds &bounds,
    const std::array<float, 16> &shadowMatrix) noexcept;

// Backend packing only: canonical indices address DPVS, never group offsets.
// Destination is separate from the LOD-packed shadow range and has sourceCount capacity.
bool WebRenderer_PackStaticModelCameraInstances(
    const WebRendererStaticModelInstanceDesc *source, std::uint32_t sourceCount,
    const std::int8_t *selectedLods, const std::uint8_t *visibility,
    std::uint32_t visibilityCount, bool visibilityComputed,
    WebRendererStaticModelInstanceDesc *destination,
    std::array<std::uint32_t, 4> &lodOffsets,
    std::array<std::uint32_t, 4> &lodCounts) noexcept;
