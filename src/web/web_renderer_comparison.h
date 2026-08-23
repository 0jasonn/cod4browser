#pragma once

#include <web/web_renderer.h>

#include <cstdint>
#include <string>
#include <vector>

enum class WebRendererComparisonAlphaTest : std::uint8_t
{
    Disabled = 0,
    GreaterThanZero,
    LessThan128,
    GreaterEqual128,
    Unknown,
};

enum class WebRendererComparisonCull : std::uint8_t
{
    None = 0,
    Back,
    Front,
    Unknown,
};

enum class WebRendererComparisonDepthFunction : std::uint8_t
{
    Always = 0,
    Less,
    Equal,
    LessEqual,
    Unknown,
};

// Pointer-free, GPU-handle-free record for comparing canonical frontend intent
// with the portable backend command actually retained for one draw.
struct WebRendererComparisonRecord
{
    std::uint32_t drawOrder = 0u;
    WebRendererSceneBatchKind sourceKind =
        WebRendererSceneBatchKind::WorldSurface;
    std::string materialName;
    std::string techniqueName;
    std::uint8_t techniqueType = 0xffu;
    WebRendererWorldTechnique portableTechnique =
        WebRendererWorldTechnique::BackendFallback;
    WebRendererWorldLightingMode lightingMode =
        WebRendererWorldLightingMode::None;
    std::uint8_t customSamplerFlags = 0u;
    std::uint16_t techniqueFlags = 0u;
    std::string pixelShaderName;
    std::uint32_t pixelShaderProgramHash = 0u;
    std::uint32_t surfaceCount = 0u;
    std::uint32_t firstSurfaceIndex = 0u;
    std::uint32_t lastSurfaceIndex = 0u;
    std::uint32_t firstIndex = 0u;
    std::uint32_t indexCount = 0u;
    std::uint32_t stateBits[2]{};
    std::string baseImageName;
    std::string normalImageName;
    std::string lightmapImageName;
    std::string secondaryLightmapImageName;
    bool baseImageUsed = false;
    bool normalImageUsed = false;
    bool lightmapUsed = false;
    bool secondaryLightmapUsed = false;
    std::uint8_t lightmapIndex = 31u;
    bool blendEnabled = false;
    std::uint8_t sourceBlendRgb = 0u;
    std::uint8_t destinationBlendRgb = 0u;
    std::uint8_t blendOperationRgb = 0u;
    std::uint8_t sourceBlendAlpha = 0u;
    std::uint8_t destinationBlendAlpha = 0u;
    std::uint8_t blendOperationAlpha = 0u;
    WebRendererComparisonAlphaTest alphaTest =
        WebRendererComparisonAlphaTest::Disabled;
    WebRendererComparisonCull cull = WebRendererComparisonCull::None;
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    WebRendererComparisonDepthFunction depthFunction =
        WebRendererComparisonDepthFunction::LessEqual;
    bool colorWriteRgb = true;
    bool colorWriteAlpha = true;
    std::uint8_t samplerState = 0u;
    std::uint8_t samplerFilter = 0u;
    std::uint8_t samplerMipmap = 0u;
    bool samplerClampU = false;
    bool samplerClampV = false;
};

struct WebRendererComparisonCapture
{
    std::vector<WebRendererComparisonRecord> records;
    std::uint32_t surfaceCount = 0u;
    std::uint32_t lightmappedDrawCount = 0u;
    std::uint32_t fallbackDrawCount = 0u;
    std::uint32_t alphaTestedDrawCount = 0u;
    std::uint32_t blendedDrawCount = 0u;
};

enum WebRendererComparisonDifference : std::uint32_t
{
    WEB_RENDERER_COMPARISON_MATERIAL = 1u << 0u,
    WEB_RENDERER_COMPARISON_TECHNIQUE = 1u << 1u,
    WEB_RENDERER_COMPARISON_ORDER_OR_RANGE = 1u << 2u,
    WEB_RENDERER_COMPARISON_RENDER_STATE = 1u << 3u,
    WEB_RENDERER_COMPARISON_BASE_IMAGE = 1u << 4u,
    WEB_RENDERER_COMPARISON_LIGHTMAP = 1u << 5u,
    WEB_RENDERER_COMPARISON_SAMPLER = 1u << 6u,
    WEB_RENDERER_COMPARISON_DRAW_COUNT = 1u << 7u,
    WEB_RENDERER_COMPARISON_NORMAL_IMAGE = 1u << 8u,
};

struct WebRendererComparisonDelta
{
    std::uint32_t drawOrder = 0u;
    std::uint32_t fields = 0u;
};

// Captures canonical names and normalized render-state values from a portable
// command. Canonical object addresses and GPU handles are intentionally absent.
bool WebRenderer_CaptureComparison(
    const WebRendererWorldSurfaceDesc &surface,
    WebRendererComparisonCapture &destination);

std::vector<WebRendererComparisonDelta> WebRenderer_CompareCaptures(
    const WebRendererComparisonCapture &intended,
    const WebRendererComparisonCapture &actual);

const char *WebRenderer_ComparisonAlphaTestString(
    WebRendererComparisonAlphaTest value) noexcept;
const char *WebRenderer_ComparisonCullString(
    WebRendererComparisonCull value) noexcept;
const char *WebRenderer_ComparisonDepthFunctionString(
    WebRendererComparisonDepthFunction value) noexcept;
