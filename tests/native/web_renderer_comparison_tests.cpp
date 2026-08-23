#include <gfx_d3d/gfx_image_types.h>
#include <web/web_renderer_comparison.h>

#include <array>
#include <cassert>

namespace
{
WebRendererWorldBatchDesc MakeBatch()
{
    static GfxImage base{};
    static GfxImage normal{};
    static GfxImage secondaryLightmap{};
    base.name = "images/concrete";
    normal.name = "images/concrete_nml";
    secondaryLightmap.name = "*lightmap0_secondary";
    WebRendererWorldBatchDesc batch{};
    batch.firstIndex = 0u;
    batch.indexCount = 6u;
    batch.surfaceCount = 2u;
    batch.firstSurfaceIndex = 14u;
    batch.lastSurfaceIndex = 15u;
    batch.materialName = "mc/killhouse/concrete";
    batch.techniqueName = "lm_l_sm_r0c0n0s0";
    batch.techniqueType = 7u;
    batch.baseImage = &base;
    batch.normalImage = &normal;
    batch.secondaryLightmapImage = &secondaryLightmap;
    batch.stateBits[0] = 0x18008800u;
    batch.stateBits[1] = 0x0000000du;
    batch.samplerState = 0x52u;
    batch.lightmapIndex = 0u;
    batch.technique = WebRendererWorldTechnique::BaseTextureLightmapNormal;
    batch.lightingMode =
        WebRendererWorldLightingMode::SecondaryDirectional;
    batch.customSamplerFlags = 4u;
    batch.techniqueFlags = 0x1234u;
    batch.pixelShaderName = "lm_r0c0_sm2.hlsl";
    batch.pixelShaderProgramHash = 0x89abcdefu;
    return batch;
}

void TestCaptureContainsOnlyStableNormalizedIdentityAndState()
{
    const WebRendererWorldBatchDesc batch = MakeBatch();
    WebRendererWorldSurfaceDesc surface{};
    surface.batches = &batch;
    surface.batchCount = 1u;
    WebRendererComparisonCapture capture;
    assert(WebRenderer_CaptureComparison(surface, capture));
    assert(capture.records.size() == 1u);
    assert(capture.surfaceCount == 2u);
    assert(capture.lightmappedDrawCount == 1u);
    const WebRendererComparisonRecord &record = capture.records[0];
    assert(record.drawOrder == 0u);
    assert(record.materialName == "mc/killhouse/concrete");
    assert(record.techniqueName == "lm_l_sm_r0c0n0s0");
    assert(record.baseImageName == "images/concrete");
    assert(record.normalImageName == "images/concrete_nml");
    assert(record.normalImageUsed);
    assert(record.lightmapImageName.empty());
    assert(record.secondaryLightmapImageName == "*lightmap0_secondary");
    assert(record.lightingMode ==
        WebRendererWorldLightingMode::SecondaryDirectional);
    assert(record.customSamplerFlags == 4u);
    assert(record.techniqueFlags == 0x1234u);
    assert(record.pixelShaderName == "lm_r0c0_sm2.hlsl");
    assert(record.pixelShaderProgramHash == 0x89abcdefu);
    assert(record.cull == WebRendererComparisonCull::Back);
    assert(record.alphaTest == WebRendererComparisonAlphaTest::Disabled);
    assert(record.depthTestEnabled && record.depthWriteEnabled);
    assert(record.depthFunction ==
        WebRendererComparisonDepthFunction::LessEqual);
    assert(record.samplerFilter == 2u);
    assert(record.samplerMipmap == 2u);
    assert(!record.samplerClampU && record.samplerClampV);
}

void TestComparisonPinpointsBackendFallbackWithoutAddresses()
{
    WebRendererWorldBatchDesc intendedBatch = MakeBatch();
    WebRendererWorldBatchDesc actualBatch = intendedBatch;
    actualBatch.technique = WebRendererWorldTechnique::BackendFallback;
    actualBatch.lightmapImage = nullptr;
    actualBatch.secondaryLightmapImage = nullptr;
    actualBatch.normalImage = nullptr;
    WebRendererWorldSurfaceDesc intendedSurface{};
    intendedSurface.batches = &intendedBatch;
    intendedSurface.batchCount = 1u;
    WebRendererWorldSurfaceDesc actualSurface{};
    actualSurface.batches = &actualBatch;
    actualSurface.batchCount = 1u;
    WebRendererComparisonCapture intended;
    WebRendererComparisonCapture actual;
    assert(WebRenderer_CaptureComparison(intendedSurface, intended));
    assert(WebRenderer_CaptureComparison(actualSurface, actual));
    const std::vector<WebRendererComparisonDelta> deltas =
        WebRenderer_CompareCaptures(intended, actual);
    assert(deltas.size() == 1u);
    assert((deltas[0].fields & WEB_RENDERER_COMPARISON_TECHNIQUE) != 0u);
    assert((deltas[0].fields & WEB_RENDERER_COMPARISON_BASE_IMAGE) != 0u);
    assert((deltas[0].fields & WEB_RENDERER_COMPARISON_LIGHTMAP) != 0u);
    assert((deltas[0].fields & WEB_RENDERER_COMPARISON_NORMAL_IMAGE) != 0u);
    assert((deltas[0].fields & WEB_RENDERER_COMPARISON_RENDER_STATE) == 0u);
}
} // namespace

int main()
{
    TestCaptureContainsOnlyStableNormalizedIdentityAndState();
    TestComparisonPinpointsBackendFallbackWithoutAddresses();
    return 0;
}
