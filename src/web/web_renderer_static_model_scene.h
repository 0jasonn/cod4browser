#pragma once

#include <web/web_renderer.h>
#include <web/web_renderer_lighting.h>

#include <array>
#include <cstdint>
#include <vector>
#include <span>

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

// Shared light-space AABB test for retained dynamic draws. The static wrapper
// above remains the named frontend seam used by existing static-model code.
bool WebRenderer_ShadowBoundsIntersectPartition(
    const WebRendererStaticModelShadowBounds &bounds,
    const std::array<float, 16> &shadowMatrix) noexcept;

// Repack authored spot membership once per selected light. Batches then scan
// this packed mask without repeating canonical-index searches per surface.
bool WebRenderer_BuildStaticModelSpotShadowVisibility(
    const WebRendererStaticModelInstanceDesc *instances,
    std::uint32_t instanceCount,
    const WebRendererSpotShadowStaticModelDesc *memberships,
    std::uint32_t membershipCount,
    std::uint32_t primaryLightIndex,
    std::uint8_t *visibility,
    std::uint32_t visibilityCount) noexcept;

// LOD-packed instances and bounds keep matching slots; canonical IDs address
// camera DPVS. The mask is per-pass scratch, independent of shadow membership.
bool WebRenderer_BuildStaticModelLightVisibility(
    std::span<const WebRendererStaticModelInstanceDesc> instances,
    std::span<const WebRendererStaticModelShadowBounds> bounds,
    std::span<const std::uint8_t> cameraVisibility, bool cameraVisibilityComputed,
    const GfxLight &light, float spotNearPlaneOffset,
    std::span<std::uint8_t> destination) noexcept;

// Backend packing only: canonical indices address DPVS, never group offsets.
// Destination is separate from the LOD-packed shadow range and has sourceCount capacity.
bool WebRenderer_PackStaticModelCameraInstances(
    const WebRendererStaticModelInstanceDesc *source, std::uint32_t sourceCount,
    const std::int8_t *selectedLods, const std::uint8_t *visibility,
    std::uint32_t visibilityCount, bool visibilityComputed,
    WebRendererStaticModelInstanceDesc *destination,
    std::array<std::uint32_t, 4> &lodOffsets,
    std::array<std::uint32_t, 4> &lodCounts) noexcept;
