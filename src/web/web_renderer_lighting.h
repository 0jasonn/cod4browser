#pragma once

#include <array>
#include <cstdint>
#include <vector>

struct GfxLightGrid;

struct WebRendererLightGridColors
{
    std::uint8_t rgb[56][3];
};

// CPU reference for the normalized-rgba8 math emitted by the native
// lm_r0c0_sm2 pixel shader. The browser fragment shader mirrors this helper.
std::array<float, 3> WebRenderer_EvaluateSecondaryDirectionalLighting(
    const std::array<float, 4> &base,
    const std::array<float, 4> &vertexColor,
    const std::array<float, 4> &secondaryLobe0,
    const std::array<float, 4> &secondaryLobe1) noexcept;

// Native R_LightGridLookup uses a collision sight trace only for corners
// carrying the corresponding needsTrace bit. The callback returns true when
// that corner is visible from samplePosition. A null callback preserves the
// lookup/interpolation behavior while treating every encoded corner as valid.
using WebRendererLightGridVisibilityCallback = bool (*)(
    const float samplePosition[3], const float gridPosition[3],
    void *context) noexcept;

// The callback mirrors Com_CanPrimaryLightAffectPoint for the alpha channel
// of the native model-lighting volume. RGB model-lighting lookup is independent
// of this callback, but retaining the value preserves the canonical payload.
using WebRendererPrimaryLightInfluenceCallback = bool (*)(
    std::uint32_t primaryLightIndex, const float position[3],
    void *context) noexcept;

struct WebRendererModelLightingCallbacks
{
    WebRendererLightGridVisibilityCallback sampleVisible = nullptr;
    WebRendererPrimaryLightInfluenceCallback primaryLightInfluences = nullptr;
    void *context = nullptr;
};

struct WebRendererModelLightingSample
{
    WebRendererLightGridColors colors{};
    std::uint8_t primaryLightWeight = 0u;
    std::uint8_t primaryLightIndex = 0u;
    bool extrapolated = false;
};

// Bounds-checked portable form of R_GetLightingAtPoint. It consumes the
// canonical GfxLightGrid directly, including its native row RLE encoding.
bool WebRenderer_EvaluateModelLighting(
    const GfxLightGrid &lightGrid,
    const float samplePosition[3],
    std::uint32_t nonSunPrimaryLightIndex,
    const WebRendererModelLightingCallbacks *callbacks,
    WebRendererModelLightingSample &sample) noexcept;

// CPU-owned representation of native modelLightGlob.image. Each entry owns a
// 4x4x4 RGBA8 block; WebGL uploads the bytes as a 3D texture without changing
// the native sample layout or color space.
struct WebRendererModelLightingAtlas
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t depth = 0u;
    std::uint32_t entryCount = 0u;
    std::vector<std::uint8_t> pixels;
};

bool WebRenderer_InitializeModelLightingAtlas(
    std::uint32_t entryCount,
    WebRendererModelLightingAtlas &atlas);

bool WebRenderer_SetModelLightingAtlasEntry(
    WebRendererModelLightingAtlas &atlas,
    std::uint32_t entryIndex,
    const WebRendererLightGridColors &colors,
    std::uint8_t primaryLightWeight) noexcept;

bool WebRenderer_SetModelGroundLightingAtlasEntry(
    WebRendererModelLightingAtlas &atlas,
    std::uint32_t entryIndex,
    std::uint32_t groundLighting) noexcept;

void WebRenderer_GetModelLightingCoordinates(
    const WebRendererModelLightingAtlas &atlas,
    std::uint32_t entryIndex,
    float coordinates[3]) noexcept;

std::array<float, 3> WebRenderer_EvaluateModelLightingShader(
    const std::array<float, 4> &base,
    const std::array<float, 4> &vertexColor,
    const std::array<float, 3> &sampledLighting) noexcept;
