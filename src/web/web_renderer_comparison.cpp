#include <web/web_renderer_comparison.h>

#include <gfx_d3d/gfx_image_types.h>

#include <algorithm>
#include <new>

namespace
{
constexpr std::uint32_t STATE0_BLEND_RGB_MASK = 0x700u;
constexpr std::uint32_t STATE0_ALPHA_TEST_DISABLE = 0x800u;
constexpr std::uint32_t STATE0_ALPHA_TEST_MASK = 0x3000u;
constexpr std::uint32_t STATE0_CULL_MASK = 0xc000u;
constexpr std::uint32_t STATE0_COLOR_WRITE_RGB = 0x08000000u;
constexpr std::uint32_t STATE0_COLOR_WRITE_ALPHA = 0x10000000u;
constexpr std::uint32_t STATE1_DEPTH_WRITE = 0x1u;
constexpr std::uint32_t STATE1_DEPTH_TEST_DISABLE = 0x2u;
constexpr std::uint32_t STATE1_DEPTH_FUNCTION_MASK = 0xcu;

const char *NameOr(const char *name, const char *fallback) noexcept
{
    return name && name[0] != '\0' ? name : fallback;
}

WebRendererComparisonAlphaTest DecodeAlphaTest(std::uint32_t state) noexcept
{
    if ((state & STATE0_ALPHA_TEST_DISABLE) != 0u)
        return WebRendererComparisonAlphaTest::Disabled;
    switch (state & STATE0_ALPHA_TEST_MASK)
    {
    case 0x1000u: return WebRendererComparisonAlphaTest::GreaterThanZero;
    case 0x2000u: return WebRendererComparisonAlphaTest::LessThan128;
    case 0x3000u: return WebRendererComparisonAlphaTest::GreaterEqual128;
    default: return WebRendererComparisonAlphaTest::Unknown;
    }
}

WebRendererComparisonCull DecodeCull(std::uint32_t state) noexcept
{
    switch (state & STATE0_CULL_MASK)
    {
    case 0x4000u: return WebRendererComparisonCull::None;
    case 0x8000u: return WebRendererComparisonCull::Back;
    case 0xc000u: return WebRendererComparisonCull::Front;
    default: return WebRendererComparisonCull::Unknown;
    }
}

WebRendererComparisonDepthFunction DecodeDepthFunction(
    std::uint32_t state) noexcept
{
    switch (state & STATE1_DEPTH_FUNCTION_MASK)
    {
    case 0u: return WebRendererComparisonDepthFunction::Always;
    case 4u: return WebRendererComparisonDepthFunction::Less;
    case 8u: return WebRendererComparisonDepthFunction::Equal;
    case 12u: return WebRendererComparisonDepthFunction::LessEqual;
    default: return WebRendererComparisonDepthFunction::Unknown;
    }
}

bool SameRenderState(
    const WebRendererComparisonRecord &left,
    const WebRendererComparisonRecord &right) noexcept
{
    return left.stateBits[0] == right.stateBits[0] &&
        left.stateBits[1] == right.stateBits[1] &&
        left.blendEnabled == right.blendEnabled &&
        left.alphaTest == right.alphaTest && left.cull == right.cull &&
        left.depthTestEnabled == right.depthTestEnabled &&
        left.depthWriteEnabled == right.depthWriteEnabled &&
        left.depthFunction == right.depthFunction &&
        left.colorWriteRgb == right.colorWriteRgb &&
        left.colorWriteAlpha == right.colorWriteAlpha;
}
} // namespace

bool WebRenderer_CaptureComparison(
    const WebRendererWorldSurfaceDesc &surface,
    WebRendererComparisonCapture &destination)
{
    if (!surface.batches || surface.batchCount == 0u)
        return false;
    WebRendererComparisonCapture replacement;
    try
    {
        replacement.records.reserve(surface.batchCount);
        for (std::uint32_t index = 0u; index < surface.batchCount; ++index)
        {
            const WebRendererWorldBatchDesc &batch = surface.batches[index];
            WebRendererComparisonRecord record;
            record.drawOrder = index;
            record.sourceKind = batch.sourceKind;
            record.materialName = NameOr(batch.materialName, "<null-material>");
            record.techniqueName = NameOr(
                batch.techniqueName, "<unsupported-technique>");
            record.techniqueType = batch.techniqueType;
            record.portableTechnique = batch.technique;
            record.lightingMode = batch.lightingMode;
            record.customSamplerFlags = batch.customSamplerFlags;
            record.techniqueFlags = batch.techniqueFlags;
            record.vertexShaderName = NameOr(
                batch.vertexShaderName, "<unavailable-vertex-shader>");
            record.vertexShaderProgramHash =
                batch.vertexShaderProgramHash;
            record.pixelShaderName = NameOr(
                batch.pixelShaderName, "<unavailable-pixel-shader>");
            record.pixelShaderProgramHash = batch.pixelShaderProgramHash;
            record.surfaceCount = batch.surfaceCount;
            record.firstSurfaceIndex = batch.firstSurfaceIndex;
            record.lastSurfaceIndex = batch.lastSurfaceIndex;
            record.firstIndex = batch.firstIndex;
            record.indexCount = batch.indexCount;
            record.stateBits[0] = batch.stateBits[0];
            record.stateBits[1] = batch.stateBits[1];
            record.baseImageName = batch.baseImage
                ? NameOr(batch.baseImage->name, "<unnamed-image>") : "";
            record.normalImageUsed = batch.normalImage != nullptr &&
                WebRenderer_UsesWorldNormalMap(batch.technique);
            record.normalImageName = record.normalImageUsed
                ? NameOr(batch.normalImage->name, "<unnamed-normal-map>") : "";
            record.lightmapImageName = batch.lightmapImage
                ? NameOr(batch.lightmapImage->name, "<unnamed-lightmap>") : "";
            record.secondaryLightmapImageName = batch.secondaryLightmapImage
                ? NameOr(batch.secondaryLightmapImage->name,
                    "<unnamed-secondary-lightmap>") : "";
            record.baseImageUsed = batch.baseImage != nullptr &&
                batch.technique != WebRendererWorldTechnique::BackendFallback &&
                !WebRenderer_SkipsNativeDraw(batch.technique);
            record.lightmapUsed = batch.lightmapImage != nullptr &&
                WebRenderer_UsesSecondaryDirectionalLightmap(batch.technique);
            record.secondaryLightmapUsed =
                batch.secondaryLightmapImage != nullptr &&
                WebRenderer_UsesSecondaryDirectionalLightmap(batch.technique);
            record.lightmapIndex = batch.lightmapIndex;
            const std::uint32_t state0 = batch.stateBits[0];
            const std::uint32_t state1 = batch.stateBits[1];
            record.blendEnabled = (state0 & STATE0_BLEND_RGB_MASK) != 0u;
            record.sourceBlendRgb = static_cast<std::uint8_t>(state0 & 0xfu);
            record.destinationBlendRgb =
                static_cast<std::uint8_t>((state0 >> 4u) & 0xfu);
            record.blendOperationRgb =
                static_cast<std::uint8_t>((state0 >> 8u) & 0x7u);
            record.sourceBlendAlpha =
                static_cast<std::uint8_t>((state0 >> 16u) & 0xfu);
            record.destinationBlendAlpha =
                static_cast<std::uint8_t>((state0 >> 20u) & 0xfu);
            record.blendOperationAlpha =
                static_cast<std::uint8_t>((state0 >> 24u) & 0x7u);
            record.alphaTest = DecodeAlphaTest(state0);
            record.cull = DecodeCull(state0);
            record.depthTestEnabled =
                (state1 & STATE1_DEPTH_TEST_DISABLE) == 0u;
            record.depthWriteEnabled = (state1 & STATE1_DEPTH_WRITE) != 0u;
            record.depthFunction = DecodeDepthFunction(state1);
            record.colorWriteRgb = (state0 & STATE0_COLOR_WRITE_RGB) != 0u;
            record.colorWriteAlpha =
                (state0 & STATE0_COLOR_WRITE_ALPHA) != 0u;
            record.samplerState = batch.samplerState;
            record.samplerFilter = batch.samplerState & 0x7u;
            record.samplerMipmap =
                static_cast<std::uint8_t>((batch.samplerState >> 3u) & 0x3u);
            record.samplerClampU = (batch.samplerState & 0x20u) != 0u;
            record.samplerClampV = (batch.samplerState & 0x40u) != 0u;
            replacement.surfaceCount += batch.surfaceCount;
            if (record.secondaryLightmapUsed && record.lightingMode ==
                    WebRendererWorldLightingMode::SecondaryDirectional)
                ++replacement.lightmappedDrawCount;
            if (record.portableTechnique ==
                WebRendererWorldTechnique::BackendFallback)
                ++replacement.fallbackDrawCount;
            if (record.alphaTest != WebRendererComparisonAlphaTest::Disabled &&
                record.alphaTest != WebRendererComparisonAlphaTest::Unknown)
                ++replacement.alphaTestedDrawCount;
            if (record.blendEnabled) ++replacement.blendedDrawCount;
            replacement.records.push_back(std::move(record));
        }
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    destination = std::move(replacement);
    return true;
}

std::vector<WebRendererComparisonDelta> WebRenderer_CompareCaptures(
    const WebRendererComparisonCapture &intended,
    const WebRendererComparisonCapture &actual)
{
    std::vector<WebRendererComparisonDelta> deltas;
    const std::size_t shared = std::min(
        intended.records.size(), actual.records.size());
    deltas.reserve(shared + (intended.records.size() != actual.records.size()));
    for (std::size_t index = 0u; index < shared; ++index)
    {
        const WebRendererComparisonRecord &left = intended.records[index];
        const WebRendererComparisonRecord &right = actual.records[index];
        std::uint32_t fields = 0u;
        if (left.materialName != right.materialName)
            fields |= WEB_RENDERER_COMPARISON_MATERIAL;
        if (left.techniqueName != right.techniqueName ||
            left.techniqueType != right.techniqueType ||
            left.portableTechnique != right.portableTechnique ||
            left.techniqueFlags != right.techniqueFlags ||
            left.vertexShaderName != right.vertexShaderName ||
            left.vertexShaderProgramHash !=
                right.vertexShaderProgramHash ||
            left.pixelShaderName != right.pixelShaderName ||
            left.pixelShaderProgramHash != right.pixelShaderProgramHash)
            fields |= WEB_RENDERER_COMPARISON_TECHNIQUE;
        if (left.drawOrder != right.drawOrder ||
            left.surfaceCount != right.surfaceCount ||
            left.firstSurfaceIndex != right.firstSurfaceIndex ||
            left.lastSurfaceIndex != right.lastSurfaceIndex ||
            left.firstIndex != right.firstIndex ||
            left.indexCount != right.indexCount)
            fields |= WEB_RENDERER_COMPARISON_ORDER_OR_RANGE;
        if (!SameRenderState(left, right))
            fields |= WEB_RENDERER_COMPARISON_RENDER_STATE;
        if (left.baseImageName != right.baseImageName ||
            left.baseImageUsed != right.baseImageUsed)
            fields |= WEB_RENDERER_COMPARISON_BASE_IMAGE;
        if (left.normalImageName != right.normalImageName ||
            left.normalImageUsed != right.normalImageUsed)
            fields |= WEB_RENDERER_COMPARISON_NORMAL_IMAGE;
        if (left.lightmapImageName != right.lightmapImageName ||
            left.lightmapUsed != right.lightmapUsed ||
            left.secondaryLightmapImageName !=
                right.secondaryLightmapImageName ||
            left.secondaryLightmapUsed != right.secondaryLightmapUsed ||
            left.lightmapIndex != right.lightmapIndex ||
            left.lightingMode != right.lightingMode ||
            left.customSamplerFlags != right.customSamplerFlags)
            fields |= WEB_RENDERER_COMPARISON_LIGHTMAP;
        if (left.samplerFilter != right.samplerFilter ||
            left.samplerMipmap != right.samplerMipmap ||
            left.samplerClampU != right.samplerClampU ||
            left.samplerClampV != right.samplerClampV)
            fields |= WEB_RENDERER_COMPARISON_SAMPLER;
        if (fields != 0u)
            deltas.push_back({static_cast<std::uint32_t>(index), fields});
    }
    if (intended.records.size() != actual.records.size())
        deltas.push_back({static_cast<std::uint32_t>(shared),
            WEB_RENDERER_COMPARISON_DRAW_COUNT});
    return deltas;
}

const char *WebRenderer_ComparisonAlphaTestString(
    WebRendererComparisonAlphaTest value) noexcept
{
    switch (value)
    {
    case WebRendererComparisonAlphaTest::Disabled: return "disabled";
    case WebRendererComparisonAlphaTest::GreaterThanZero: return "gt-zero";
    case WebRendererComparisonAlphaTest::LessThan128: return "lt-128";
    case WebRendererComparisonAlphaTest::GreaterEqual128: return "ge-128";
    case WebRendererComparisonAlphaTest::Unknown: return "unknown";
    }
    return "unknown";
}

const char *WebRenderer_ComparisonCullString(
    WebRendererComparisonCull value) noexcept
{
    switch (value)
    {
    case WebRendererComparisonCull::None: return "none";
    case WebRendererComparisonCull::Back: return "back";
    case WebRendererComparisonCull::Front: return "front";
    case WebRendererComparisonCull::Unknown: return "unknown";
    }
    return "unknown";
}

const char *WebRenderer_ComparisonDepthFunctionString(
    WebRendererComparisonDepthFunction value) noexcept
{
    switch (value)
    {
    case WebRendererComparisonDepthFunction::Always: return "always";
    case WebRendererComparisonDepthFunction::Less: return "less";
    case WebRendererComparisonDepthFunction::Equal: return "equal";
    case WebRendererComparisonDepthFunction::LessEqual: return "less-equal";
    case WebRendererComparisonDepthFunction::Unknown: return "unknown";
    }
    return "unknown";
}
