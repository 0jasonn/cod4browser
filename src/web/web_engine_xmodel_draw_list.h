#pragma once

#include <web/web_engine_xmodel_material.h>
#include <web/web_renderer_surface_storage.h>

#include <array>
#include <cstdint>
#include <vector>

struct WebEngineXModelDrawPublication
{
    std::uint32_t surfaceIndex = 0u;
    std::uint32_t materialIdentity = 0u;
    std::uint32_t vertexCount = 0u;
    std::uint32_t triangleCount = 0u;
    std::uint32_t textureSlot = 0u;
    WebEngineXModelMaterialResult materialResult =
        WebEngineXModelMaterialResult::InvalidSurfaceMaterial;
    bool retained = false;
};

struct WebEngineXModelDrawList
{
    WebRendererOwnedDrawList renderer;
    std::vector<WebEngineXModelMaterialImageBinding> textures;
    std::vector<WebEngineXModelDrawPublication> surfaces;
    std::array<float, 3> mins{};
    std::array<float, 3> maxs{};
    std::uint8_t horizontalAxis = 0u;
    std::uint8_t verticalAxis = 1u;
    std::uint32_t firstLodSurfaceIndex = 0u;
    std::uint32_t firstLodSurfaceCount = 0u;
};

enum class WebEngineXModelDrawListResult : std::uint8_t
{
    Success = 0,
    InvalidModel,
    NoSupportedDraw,
    OutputTooLarge,
    AllocationFailed,
};

// Builds the first LOD only. Unsupported individual surfaces are recorded and
// skipped; every successfully retained draw survives failures in later draws.
// Destination changes only when at least one complete draw is available.
WebEngineXModelDrawListResult WebEngine_BuildXModelDrawList(
    const kisak::fastfile::RetailWorldXModel &model,
    WebEngineXModelDrawList &destination);

const char *WebEngine_XModelDrawListResultString(
    WebEngineXModelDrawListResult result) noexcept;
