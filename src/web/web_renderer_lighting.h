#pragma once

#include <array>
#include <cstdint>
#include <vector>

struct GfxLightGrid;

// Canonical 20-byte GfxFog payload retained without importing the native
// backend command graph. The browser frontend receives the same server fog
// commands and advances this state once per rendered scene.
struct WebRendererFog
{
    std::int32_t startTime = 0;
    std::int32_t finishTime = 0;
    std::uint32_t color = 0u;
    float fogStart = 0.0f;
    float density = 0.0f;
};
static_assert(sizeof(WebRendererFog) == 20u);

// Mirrors R_UpdateFrameFog, including byte-wise color interpolation and the
// native one-millisecond minimum transition span. Returns false for FOG_NONE
// and writes a disabled payload to frameFog in that case.
bool WebRenderer_UpdateFrameFog(
    std::array<WebRendererFog, 5> &fogStates,
    std::uint32_t fogIndex,
    std::int32_t sceneTime,
    WebRendererFog &frameFog) noexcept;

// CPU reference for the native exponential-fog visibility used by the WebGL
// fragment shader. One means fully scene-colored; zero means fully fog-colored.
float WebRenderer_EvaluateFogVisibility(
    float distance,
    const WebRendererFog &fog) noexcept;

// Portable form of the canonical GfxFilm payload after R_SetFilmInfo has
// applied the renderer adjustment dvars. Keeping this at the frontend/backend
// seam preserves the campaign vision-set values without exposing GfxViewInfo
// or native command-buffer constants to WebGL.
struct WebRendererFilmSettings
{
    bool enabled = false;
    float brightness = 0.0f;
    float contrast = 1.0f;
    float desaturation = 0.0f;
    bool invert = false;
    float tintDark[3]{1.0f, 1.0f, 1.0f};
    float tintLight[3]{1.0f, 1.0f, 1.0f};
};

// Exact CONST_SRC_CODE_COLOR_* values emitted by
// R_UpdateColorManipulation. The eventual WebGL post-effect pass consumes
// these normalized constants rather than independently interpreting vision
// settings in the platform backend.
struct WebRendererColorManipulationConstants
{
    float colorBias[4]{};
    float colorTintBase[4]{};
    float colorTintDelta[4]{};
    bool enabled = false;
};

bool WebRenderer_CalculateColorManipulationConstants(
    const WebRendererFilmSettings &film,
    WebRendererColorManipulationConstants &constants) noexcept;

struct WebRendererGlowSettings
{
    bool enabled = false;
    float bloomCutoff = 0.0f;
    float bloomDesaturation = 0.0f;
    float bloomIntensity = 0.0f;
    float radius = 0.0f;
};

// Exact CONST_SRC_CODE_GLOW_SETUP/APPLY values emitted by R_SetGlowInfo.
struct WebRendererGlowConstants
{
    float bloomCutoff = 0.0f;
    float bloomCutoffRescale = 0.0f;
    float bloomDesaturation = 0.0f;
    float bloomIntensity = 0.0f;
    float radius = 0.0f;
    bool enabled = false;
};

bool WebRenderer_CalculateGlowConstants(
    const WebRendererGlowSettings &glow,
    WebRendererGlowConstants &constants) noexcept;

struct WebRendererDepthOfFieldSettings
{
    float viewModelStart = 0.0f;
    float viewModelEnd = 0.0f;
    float nearStart = 0.0f;
    float nearEnd = 0.0f;
    float farStart = 0.0f;
    float farEnd = 0.0f;
    float nearBlur = 0.0f;
    float farBlur = 0.0f;
    bool enabled = false;
};

bool WebRenderer_ValidateDepthOfFieldSettings(
    const WebRendererDepthOfFieldSettings &dof) noexcept;

// CPU oracle for the scene/viewmodel CoC radius consumed by the WebGL pass.
float WebRenderer_EvaluateDepthOfFieldBlur(
    const WebRendererDepthOfFieldSettings &dof,
    float viewDistance,
    bool viewModel) noexcept;

// CPU oracle for the native hardware gamma ramp. D3D9 applies
// pow(displayValue, 1 / r_gamma) after rendering; WebGL must reproduce that
// at the final framebuffer boundary rather than altering lightmap samples.
float WebRenderer_EvaluateDisplayGamma(
    float displayValue, float gamma) noexcept;

// CPU oracle for the DXT5nm channel convention used by IW3 n0 material
// programs: tangent X is alpha, tangent Y is green, and positive Z is
// reconstructed after expanding the stored channels from [0,1] to [-1,1].
std::array<float, 3> WebRenderer_DecodeDxt5Normal(
    const std::array<float, 4> &sample) noexcept;

// CPU oracle for the fractional 2x2 depth-comparison reconstruction used by
// IW3 sun- and spot-shadow receivers. Samples are ordered top-left,
// top-right, bottom-left, bottom-right and contain binary comparison results.
float WebRenderer_EvaluateBilinearShadowVisibility(
    const std::array<float, 4> &comparisons,
    float fractionX,
    float fractionY) noexcept;

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

// CPU reference for native lm_[rt]0c0n0_sm2. Both the light direction and
// DXT5nm surface normal are decoded as slopes before the two lightmap lobes
// are combined. The browser fragment shader mirrors this helper.
std::array<float, 3> WebRenderer_EvaluateSecondaryDirectionalNormalLighting(
    const std::array<float, 4> &base,
    const std::array<float, 4> &vertexColor,
    const std::array<float, 4> &secondaryLobe0,
    const std::array<float, 4> &secondaryLobe1,
    const std::array<float, 4> &normalSample) noexcept;

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

// Copies complete 4x4x4 native lighting entries between atlases whose heights
// may differ. Used when per-frame canonical entity families share one backend
// volume texture.
bool WebRenderer_CopyModelLightingAtlasEntries(
    const WebRendererModelLightingAtlas &source,
    WebRendererModelLightingAtlas &destination,
    std::uint32_t destinationEntryOffset) noexcept;

void WebRenderer_GetModelLightingCoordinates(
    const WebRendererModelLightingAtlas &atlas,
    std::uint32_t entryIndex,
    float coordinates[3]) noexcept;

std::array<float, 3> WebRenderer_EvaluateModelLightingShader(
    const std::array<float, 4> &base,
    const std::array<float, 4> &vertexColor,
    const std::array<float, 3> &sampledLighting) noexcept;

std::array<float, 3> WebRenderer_EvaluateAmbientProbeLightingShader(
    const std::array<float, 4> &base,
    const std::array<float, 4> &vertexColor,
    const std::array<float, 4> &sampledLighting,
    const std::array<float, 3> &sunDiffuse) noexcept;
