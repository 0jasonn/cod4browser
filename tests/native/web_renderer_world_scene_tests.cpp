#include <gfx_d3d/gfx_world_types.h>
#include <gfx_d3d/r_material_override_core.h>
#include <gfx_d3d/r_dynamiclights_core.h>
#include <web/web_renderer_world_scene.h>
#include <web/web_renderer_image_reference.h>
#include <web/web_renderer_material_lookup.h>
#include <web/web_renderer_draw_state.h>
#include <web/web_renderer_dynamic_textures.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace
{
void TestWorldLightReceiversPreserveNativeBoundsAndCameraSelection()
{
    // Synthetic bounds deliberately split one material batch at the light edge.
    std::vector<WebRendererWorldSurfaceRange> surfaces{
        {0, 0, 0, 3, {-0.25f,-0.25f,2}, {0.25f,0.25f,3}},
        {1, 0, 3, 3, {20,20,2}, {21,21,3}},
        {2, 0, 6, 3, {-0.25f,-0.25f,4}, {0.25f,0.25f,5}},
        {3, 0, 9, 3, {-0.25f,-0.25f,5}, {0.25f,0.25f,6}},
        {4, 1, 12, 3, {-0.25f,-0.25f,7}, {0.25f,0.25f,8}}};
    std::uint8_t visibility[]{1,1,1,0,1};
    GfxLight light{};
    light.type = 3;
    light.radius = 10;
    std::vector<WebRendererWorldCameraRange> ranges;
    const auto build = [&](float nearOffset = 0.0f) {
        return WebRenderer_BuildWorldCameraRanges(surfaces, visibility, 5, true,
            ranges, &light, nearOffset);
    };
    assert(build() && ranges.size() == 3);
    assert(ranges[0].firstIndex == 0 && ranges[0].indexCount == 3);
    assert(ranges[1].firstIndex == 6 && ranges[1].indexCount == 3);
    assert(ranges[2].firstIndex == 12 && ranges[2].batchIndex == 1);
    visibility[3] = 1;
    assert(build() && ranges[1].indexCount == 6 && ranges[1].surfaceCount == 2);
    light.type = 2;
    light.dir[2] = -1;
    light.cosHalfFovOuter = std::sqrt(0.5f);
    assert(build(3) && ranges.size() == 2 && ranges[0].firstIndex == 6);
    visibility[3] = 0;
    assert(build(3) && ranges.size() == 2 && ranges[0].indexCount == 3);
    std::vector<WebRendererWorldShadowRange> shadowRanges;
    assert(WebRenderer_BuildWorldTransientSpotShadowRanges(
        surfaces, light, 3, shadowRanges));
    assert(shadowRanges.size() == 2 && shadowRanges[0].firstIndex == 6 &&
        shadowRanges[0].indexCount == 6 &&
        shadowRanges[0].surfaceCount == 2);
    // Native caster selection ignores camera DPVS but still uses the shifted
    // light planes, unlike a conservative shadow-projection box.
    assert(shadowRanges[1].firstIndex == 12);
    light.type = 3;
    assert(!WebRenderer_BuildWorldTransientSpotShadowRanges(
        surfaces, light, 3, shadowRanges) && shadowRanges.empty());
    light.type = 2;
    light.radius = std::numeric_limits<float>::quiet_NaN();
    assert(!WebRenderer_BuildWorldTransientSpotShadowRanges(
        surfaces, light, 3, shadowRanges) && shadowRanges.empty());
    light.radius = 10;
    visibility[3] = 1;
    // Strict near/far plane contact and an omni's inclusive tangency differ.
    surfaces[0].mins[2] = surfaces[0].maxs[2] = 10;
    assert(build() && ranges[0].firstIndex == 6);
    light.type = 3;
    assert(build() && ranges[0].firstIndex == 0);
    light.radius = 1;
    assert(build() && ranges.empty());
    light.radius = std::numeric_limits<float>::quiet_NaN();
    assert(!build() && ranges.empty());
}

struct MaterialFeature
{
    const char *name;
    std::uint32_t mask;
    std::uint32_t value;
};

void TestTechniqueSetFeatureNameRemap()
{
    constexpr MaterialFeature features[]{
        {"s0", 4u, 0u}, {"d0", 8u, 0u}, {"n0", 16u, 0u},
        {"zfeather", 1u, 0u}, {"outdoor", 2u, 0u},
        {"sm", 384u, 128u}, {"hsm", 384u, 256u},
        {"twk", 32u, 0u},
    };
    char remapped[64]{};
    assert(Material_RemapTechniqueSetNameCore(
        "wc_l_sm_r0c0n0d0s0", remapped, sizeof(remapped),
        0x19cu, 0x100u, features, std::size(features), false));
    assert(std::strcmp(remapped, "wc_l_hsm_r0c0") == 0);
    assert(Material_RemapTechniqueSetNameCore(
        "particle_cloud_outdoor_zfeather", remapped, sizeof(remapped),
        3u, 0u, features, std::size(features), false));
    assert(std::strcmp(remapped, "particle_cloud") == 0);
    assert(Material_RemapTechniqueSetNameCore(
        "sm2/wc_l_hsm_twk", remapped, sizeof(remapped),
        0x1a0u, 0x80u, features, std::size(features), true));
    assert(std::strcmp(remapped, "sm2/wc_l_sm") == 0);
    char tooSmall[8]{};
    assert(!Material_RemapTechniqueSetNameCore(
        "particle_cloud", tooSmall, sizeof(tooSmall),
        0u, 0u, features, std::size(features), false));

    MaterialTechniqueSet source{}, target{}, reference{}, missing{};
    source.name = "wc_l_sm_r0c0n0d0s0";
    target.name = "wc_l_hsm_r0c0";
    reference.name = ",wc_l_sm_r0c0n0d0s0";
    missing.name = "wc_l_sm_d0";
    MaterialTechniqueSet *sets[]{&source, &target, &reference, &missing};
    const auto stats = Material_ResolveTechniqueSetRemapsCore(
        sets, std::size(sets), 0x19cu, 0x100u,
        features, std::size(features), [&](const char *name) {
            for (MaterialTechniqueSet *set : sets)
                if (std::strcmp(set->name, name) == 0) return set;
            return static_cast<MaterialTechniqueSet *>(nullptr);
        });
    assert(source.remappedTechniqueSet == &target);
    assert(target.remappedTechniqueSet == &target);
    assert(reference.remappedTechniqueSet == &target);
    assert(missing.remappedTechniqueSet == &missing);
    assert(stats.shaderModel3 == 3u && stats.featureRemaps == 1u &&
        stats.references == 1u);
}

GfxImage g_resolvedImage{};

const GfxImage *LookupResolvedImage(const char *name) noexcept
{
    return name && std::strcmp(name, "crater_blacktop") == 0
        ? &g_resolvedImage : nullptr;
}

constexpr std::uint32_t TECHNIQUE_LIT_INDEX = 7u;
constexpr std::uint32_t TECHNIQUE_LIT_SUN_SHADOW_INDEX = 9u;
constexpr std::uint32_t TECHNIQUE_LIT_SPOT_INDEX = 10u;
constexpr std::uint32_t TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX = 2u;
WebRendererSceneViewDesc MakeView()
{
    WebRendererSceneViewDesc view{};
    view.width = 960u;
    view.height = 540u;
    view.tanHalfFovX = 1.0f;
    view.tanHalfFovY = 1.0f;
    view.viewAxis[0][0] = 1.0f;
    view.viewAxis[1][1] = 1.0f;
    view.viewAxis[2][2] = 1.0f;
    view.zNear = 1.0f;
    view.localClientNum = 0;
    view.worldName = "maps/test.d3dbsp";
    return view;
}

GfxWorldVertex MakeVertex(float x, float y, float z, std::uint32_t color)
{
    GfxWorldVertex vertex{};
    vertex.xyz[0] = x;
    vertex.xyz[1] = y;
    vertex.xyz[2] = z;
    vertex.color.packed = color;
    vertex.texCoord[0] = y;
    vertex.texCoord[1] = z;
    return vertex;
}

GfxSurface MakeSurface(
    int firstVertex, int baseIndex, float minimumX, float maximumX)
{
    GfxSurface surface{};
    surface.tris.firstVertex = firstVertex;
    surface.tris.vertexCount = 3u;
    surface.tris.triCount = 1u;
    surface.tris.baseIndex = baseIndex;
    surface.bounds[0][0] = minimumX;
    surface.bounds[0][1] = -1.0f;
    surface.bounds[0][2] = -1.0f;
    surface.bounds[1][0] = maximumX;
    surface.bounds[1][1] = 1.0f;
    surface.bounds[1][2] = 1.0f;
    return surface;
}

struct Fixture
{
    std::array<GfxWorldVertex, 9> vertices{
        MakeVertex(10.0f, -1.0f, -1.0f, 0xff804020u),
        MakeVertex(10.0f, 1.0f, -1.0f, 0xff804020u),
        MakeVertex(10.0f, 0.0f, 1.0f, 0xff804020u),
        MakeVertex(-10.0f, -1.0f, -1.0f, 0xffffffffu),
        MakeVertex(-10.0f, 1.0f, -1.0f, 0xffffffffu),
        MakeVertex(-10.0f, 0.0f, 1.0f, 0xffffffffu),
        MakeVertex(12.0f, -1.0f, -1.0f, 0xff102030u),
        MakeVertex(12.0f, 1.0f, -1.0f, 0xff102030u),
        MakeVertex(12.0f, 0.0f, 1.0f, 0xff102030u),
    };
    std::array<std::uint16_t, 9> indices{
        0u, 1u, 2u,
        0u, 1u, 2u,
        2u, 1u, 0u,
    };
    std::array<GfxSurface, 3> surfaces{
        MakeSurface(0, 0, 10.0f, 10.0f),
        MakeSurface(3, 3, -10.0f, -10.0f),
        MakeSurface(6, 6, 12.0f, 12.0f),
    };
    GfxBrushModel model{};
    GfxWorld world{};

    Fixture()
    {
        model.startSurfIndex = 0u;
        model.surfaceCount = static_cast<std::uint16_t>(surfaces.size());
        world.name = "maps/test.d3dbsp";
        world.indexCount = static_cast<int>(indices.size());
        world.indices = indices.data();
        world.surfaceCount = static_cast<int>(surfaces.size());
        world.vertexCount = static_cast<std::uint32_t>(vertices.size());
        world.vd.vertices = vertices.data();
        world.modelCount = 1;
        world.models = &model;
        world.dpvs.surfaces = surfaces.data();
    }
};

void TestCanonicalOpaqueSurfacesAreBatchedInWorldOrder()
{
    Fixture fixture;
    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.surfaceCount == 3u);
    assert(command.firstSurfaceIndex == 0u);
    assert(command.lastSurfaceIndex == 2u);
    assert(command.vertices.size() == 9u);
    assert(command.indices ==
        std::vector<std::uint32_t>({
            0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u}));
    assert(command.batches.size() == 1u);
    assert(command.batches[0].firstIndex == 0u);
    assert(command.batches[0].indexCount == 9u);
    assert(command.batches[0].surfaceCount == 3u);
    assert(command.batches[0].technique ==
        WebRendererWorldTechnique::BackendFallback);
    assert(command.vertices[0].position[0] == 10.0f);
    assert(command.vertices[6].position[0] == 12.0f);
    assert(command.vertices[0].color[0] > command.vertices[0].color[1]);
    assert(command.vertices[0].color[1] > command.vertices[0].color[2]);
}

void TestPrimaryLightIdentitySplitsNativeLitBatches()
{
    Fixture fixture;
    fixture.surfaces[0].primaryLightIndex = 2u;
    fixture.surfaces[1].primaryLightIndex = 2u;
    fixture.surfaces[2].primaryLightIndex = 3u;
    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches.size() == 2u);
    assert(command.batches[0].primaryLightIndex == 2u);
    assert(command.batches[0].surfaceCount == 2u);
    assert(command.batches[1].primaryLightIndex == 3u);
    assert(command.batches[1].surfaceCount == 1u);

    fixture.surfaces[2].primaryLightIndex = 2u;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].primaryLightIndex == 2u);
}

void TestSpotPrimaryLightSelectsNativeMaterialTechnique()
{
    Fixture fixture;
    MaterialTechnique litTechnique{};
    litTechnique.passCount = 1u;
    litTechnique.name = "lit";
    MaterialTechnique spotTechnique{};
    spotTechnique.passCount = 1u;
    spotTechnique.name = "lit_spot";
    spotTechnique.passArray[0u].customSamplerFlags = 6u;
    MaterialPixelShader spotPixelShader{};
    spotPixelShader.name = "lm_spot_r0c0n0_sm2.hlsl";
    spotTechnique.passArray[0u].pixelShader = &spotPixelShader;
    MaterialTechniqueSet techniqueSet{};
    techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &litTechnique;
    techniqueSet.techniques[TECHNIQUE_LIT_SPOT_INDEX] = &spotTechnique;
    GfxStateBits stateBits[2]{};
    GfxImage baseImage{};
    GfxImage normalImage{};
    GfxImage primaryLightmap{};
    GfxImage secondaryLightmap{};
    GfxLightmapArray lightmap{&primaryLightmap, &secondaryLightmap};
    fixture.world.lightmapCount = 1u;
    fixture.world.lightmaps = &lightmap;
    std::array<MaterialTextureDef, 2u> textures{};
    textures[0u].semantic = 2u;
    textures[0u].u.image = &baseImage;
    textures[1u].semantic = 5u;
    textures[1u].u.image = &normalImage;
    Material material{};
    material.info.name = "spot/material";
    material.techniqueSet = &techniqueSet;
    material.textureCount = static_cast<std::uint8_t>(textures.size());
    material.textureTable = textures.data();
    material.stateBitsCount = 2u;
    material.stateBitsTable = stateBits;
    std::fill(std::begin(material.stateBitsEntry),
        std::end(material.stateBitsEntry), 0xffu);
    material.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 0u;
    material.stateBitsEntry[TECHNIQUE_LIT_SPOT_INDEX] = 1u;
    for (GfxSurface &surface : fixture.surfaces)
    {
        surface.material = &material;
        surface.primaryLightIndex = 2u;
        surface.lightmapIndex = 0u;
    }
    std::array<WebRendererPrimaryLightDesc, 3u> lights{};
    lights[2].type = 2u;
    const WebRendererWorldLightTechniqueContext lightContext{
        lights.data(), static_cast<std::uint32_t>(lights.size()), 1u, false};
    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command, &lightContext) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].primaryLightIndex == 2u);
    assert(command.batches[0].techniqueType == TECHNIQUE_LIT_SPOT_INDEX);
    assert(std::strcmp(command.batches[0].techniqueName, "lit_spot") == 0);
    assert(command.batches[0].lightmapImage == &primaryLightmap);
    assert(command.batches[0].secondaryLightmapImage == &secondaryLightmap);
    assert(command.batches[0].normalImage == &normalImage);
    assert(command.batches[0].technique ==
        WebRendererWorldTechnique::BaseTextureLightmapNormal);
}

void TestPrimaryLightFrameAcceptsScriptedLightState()
{
    std::array<WebRendererPrimaryLightDesc, 3u> lights{};
    lights[1u].type = 1u;
    lights[1u].direction[2] = -1.0f;
    lights[2u].type = 2u;
    lights[2u].radius = 256.0f;
    lights[2u].cosHalfFovOuter = 0.7f;
    lights[2u].cosHalfFovInner = 0.8f;
    lights[2u].falloffScale = 32.0f / 512.0f;
    lights[2u].falloffShift = 1.0f / 512.0f;
    // A scripted primary light is disabled by fading its frame color to zero;
    // it remains a valid indexed spotlight with retained attenuation data.
    assert(WebRenderer_ValidatePrimaryLightFrame(
        lights.data(), static_cast<std::uint32_t>(lights.size())));

    lights[2u].radius = 0.0f;
    assert(!WebRenderer_ValidatePrimaryLightFrame(
        lights.data(), static_cast<std::uint32_t>(lights.size())));
    lights[2u].radius = 256.0f;
    lights[2u].cosHalfFovInner = lights[2u].cosHalfFovOuter;
    assert(!WebRenderer_ValidatePrimaryLightFrame(
        lights.data(), static_cast<std::uint32_t>(lights.size())));
    assert(!WebRenderer_ValidatePrimaryLightFrame(nullptr, 1u));
}

void TestMissingSpotTechniqueRetainsNativeSkipIntent()
{
    Fixture fixture;
    std::array<WebRendererPrimaryLightDesc, 3u> lights{};
    lights[2u].type = 2u;
    for (GfxSurface &surface : fixture.surfaces)
        surface.primaryLightIndex = 2u;
    const WebRendererWorldLightTechniqueContext lightContext{
        lights.data(), static_cast<std::uint32_t>(lights.size()), 1u, false};
    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command, &lightContext) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0u].technique ==
        WebRendererWorldTechnique::NativeTechniqueUnavailable);
    assert(command.batches[0u].techniqueType == 36u);
}

void TestCommaPrefixedImageReferenceResolvesAtRendererBoundary()
{
    GfxImage reference{};
    reference.name = ",crater_blacktop";
    GfxImage ordinary{};
    ordinary.name = "ordinary";
    assert(WebRenderer_ResolveImageReference(
        &reference, LookupResolvedImage) == &g_resolvedImage);
    assert(WebRenderer_ResolveImageReference(
        &ordinary, LookupResolvedImage) == &ordinary);
    assert(WebRenderer_ResolveImageReference(
        &reference, nullptr) == &reference);
}

void TestCanonicalMaterialAndLightmapIdentitySplitBatches()
{
    Fixture fixture;
    MaterialTechnique litTechnique{};
    litTechnique.passCount = 1u;
    litTechnique.name = "lm_r0c0_sm2";
    litTechnique.passArray[0].customSamplerFlags = 4u;
    MaterialTechniqueSet techniqueSet{};
    techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &litTechnique;
    GfxStateBits stateBits[2]{{{0x18008800u, 0x0000000du}},
                              {{0x18004800u, 0x00000002u}}};
    GfxImage baseImageA{};
    GfxImage baseImageB{};
    GfxImage lightmapImage{};
    GfxImage secondaryLightmapImage{};
    MaterialTextureDef textureA{};
    textureA.semantic = 2u;
    textureA.samplerState = 0x22u;
    textureA.u.image = &baseImageA;
    MaterialTextureDef textureB = textureA;
    textureB.samplerState = 0x42u;
    textureB.u.image = &baseImageB;
    Material materialA{};
    materialA.info.name = "material/a";
    materialA.textureCount = 1u;
    materialA.textureTable = &textureA;
    materialA.techniqueSet = &techniqueSet;
    materialA.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 0u;
    materialA.stateBitsCount = 2u;
    materialA.stateBitsTable = stateBits;
    Material materialB = materialA;
    materialB.info.name = "material/b";
    materialB.textureTable = &textureB;
    materialB.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 1u;
    GfxLightmapArray lightmap{&lightmapImage, &secondaryLightmapImage};
    fixture.world.lightmapCount = 1;
    fixture.world.lightmaps = &lightmap;
    fixture.surfaces[0].material = &materialA;
    fixture.surfaces[0].lightmapIndex = 0u;
    fixture.surfaces[1].material = &materialA;
    fixture.surfaces[1].lightmapIndex = 0u;
    fixture.surfaces[2].material = &materialB;
    fixture.surfaces[2].lightmapIndex = 31u;

    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches.size() == 2u);
    const WebRendererWorldBatchDesc &first = command.batches[0];
    assert(first.materialIdentity == &materialA);
    assert(std::strcmp(first.materialName, "material/a") == 0);
    assert(first.baseImage == &baseImageA);
    assert(first.lightmapImage == nullptr);
    assert(first.secondaryLightmapImage == &secondaryLightmapImage);
    assert(first.lightingMode ==
        WebRendererWorldLightingMode::SecondaryDirectional);
    assert(first.customSamplerFlags == 4u);
    assert(first.samplerState == 0x22u);
    assert(first.stateBits[0] == stateBits[0].loadBits[0]);
    assert(first.stateBits[1] == stateBits[0].loadBits[1]);
    assert(first.technique == WebRendererWorldTechnique::BaseTextureLightmap);
    assert(first.firstIndex == 0u && first.indexCount == 6u &&
        first.surfaceCount == 2u && first.firstSurfaceIndex == 0u &&
        first.lastSurfaceIndex == 1u);
    const WebRendererWorldBatchDesc &second = command.batches[1];
    assert(second.materialIdentity == &materialB);
    assert(second.baseImage == &baseImageB);
    assert(second.lightmapImage == nullptr);
    assert(second.technique == WebRendererWorldTechnique::BaseTexture);
    assert(second.firstIndex == 6u && second.indexCount == 3u);
    assert(command.vertices[0].lightmapCoordinate[0] ==
        fixture.vertices[0].lmapCoord[0]);
}

void TestCanonicalLmTechniqueNameDoesNotInventLightmapSamplers()
{
    Fixture fixture;
    MaterialTechnique litTechnique{};
    litTechnique.name = "lm_world_test";
    litTechnique.passCount = 1u;
    MaterialTechniqueSet techniqueSet{};
    techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &litTechnique;
    GfxStateBits stateBits[1]{};
    GfxImage baseImage{};
    GfxImage lightmapImage{};
    MaterialTextureDef texture{};
    texture.semantic = 2u;
    texture.u.image = &baseImage;
    Material material{};
    material.info.name = "material/lm-name";
    material.textureCount = 1u;
    material.textureTable = &texture;
    material.techniqueSet = &techniqueSet;
    material.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 0u;
    material.stateBitsCount = 1u;
    material.stateBitsTable = stateBits;
    GfxLightmapArray lightmap{&lightmapImage, nullptr};
    fixture.world.lightmapCount = 1;
    fixture.world.lightmaps = &lightmap;
    for (GfxSurface &surface : fixture.surfaces)
    {
        surface.material = &material;
        surface.lightmapIndex = 0u;
    }

    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].lightmapImage == nullptr);
    assert(command.batches[0].secondaryLightmapImage == nullptr);
    assert(command.batches[0].lightingMode ==
        WebRendererWorldLightingMode::None);
    assert(command.batches[0].technique ==
        WebRendererWorldTechnique::BaseTexture);
}

void TestRemappedTechniqueSetDrivesPortableSelection()
{
    Fixture fixture;
    MaterialTechnique litTechnique{};
    litTechnique.name = "lm_remapped_world";
    litTechnique.passCount = 1u;
    litTechnique.passArray[0].customSamplerFlags = 4u;
    MaterialTechniqueSet directTechniqueSet{};
    MaterialTechniqueSet remappedTechniqueSet{};
    remappedTechniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &litTechnique;
    directTechniqueSet.remappedTechniqueSet = &remappedTechniqueSet;
    GfxStateBits stateBits[1]{{{0x18008800u, 0x0000000du}}};
    GfxImage baseImage{};
    GfxImage lightmapImage{};
    GfxImage secondaryLightmapImage{};
    MaterialTextureDef texture{};
    texture.semantic = 2u;
    texture.u.image = &baseImage;
    Material material{};
    material.info.name = "material/remapped";
    material.textureCount = 1u;
    material.textureTable = &texture;
    material.techniqueSet = &directTechniqueSet;
    material.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 0u;
    material.stateBitsCount = 1u;
    material.stateBitsTable = stateBits;
    GfxLightmapArray lightmap{&lightmapImage, &secondaryLightmapImage};
    fixture.world.lightmapCount = 1;
    fixture.world.lightmaps = &lightmap;
    for (GfxSurface &surface : fixture.surfaces)
    {
        surface.material = &material;
        surface.lightmapIndex = 0u;
    }

    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].technique ==
        WebRendererWorldTechnique::BaseTextureLightmap);
    assert(std::strcmp(command.batches[0].techniqueName,
        "lm_remapped_world") == 0);
    assert(command.batches[0].secondaryLightmapImage ==
        &secondaryLightmapImage);
    assert(command.batches[0].lightmapImage == nullptr);
}

void TestCinematicCodeImagesSelectCanonicalMaterialWithoutTextureTable()
{
    Fixture fixture;
    MaterialShaderArgument args[4]{};
    for (unsigned i = 0; i < 4; ++i)
    {
        args[i].type = 4;
        args[i].dest = 4 + i;
        args[i].u.codeSampler = static_cast<MaterialTextureSource>(22 + i);
    }
    MaterialPixelShader shader{};
    shader.name = "cinematic.hlsl";
    MaterialTechnique technique{};
    technique.name = "cinematic";
    technique.passCount = 1;
    technique.passArray[0].pixelShader = &shader;
    technique.passArray[0].args = args;
    technique.passArray[0].stableArgCount = 4;
    MaterialTechniqueSet direct{}, remapped{};
    direct.remappedTechniqueSet = &remapped;
    remapped.techniques[4] = &technique;
    GfxStateBits state{{0x18008800u, 2u}};
    Material material{};
    material.info.name = "synthetic/cinematic";
    material.techniqueSet = &direct;
    material.stateBitsTable = &state;
    material.stateBitsCount = 1;
    std::fill_n(material.stateBitsEntry, 34, 255);
    material.stateBitsEntry[4] = 0;
    for (auto &surface : fixture.surfaces) surface.material = &material;
    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches.size() == 1);
    const auto &batch = command.batches[0];
    assert(batch.technique == WebRendererWorldTechnique::Cinematic);
    assert(batch.materialIdentity == &material && batch.techniqueType == 4);
    assert(batch.baseImage == nullptr && batch.lightmapImage == nullptr);
    assert(batch.stateBits[0] == state.loadBits[0] && batch.stateBits[1] == state.loadBits[1]);
    assert(batch.samplerState == 0x62 && batch.normalSamplerState == 0x62 &&
        batch.detailSamplerState == 0x62 && batch.specularSamplerState == 0x62);
    assert(WebRenderer_IsCinematicMaterial(&material, 4));
    assert(!WebRenderer_IsCinematicMaterial(&material, 7));
    assert(!WebRenderer_IsCinematicMaterial(&material, 34));
    args[3].u.codeSampler = static_cast<MaterialTextureSource>(24); // A duplicate cannot stand in for alpha.
    assert(!WebRenderer_IsCinematicMaterial(&material, 4));
    assert(WebRenderer_BuildWorldSceneCommand(fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches[0].technique == WebRendererWorldTechnique::BackendFallback);
    args[3].u.codeSampler = static_cast<MaterialTextureSource>(25);
    args[3].dest = 4;
    assert(!WebRenderer_IsCinematicMaterial(&material, 4));
    args[3].dest = 7;
    shader.name = "unknown.hlsl";
    assert(!WebRenderer_IsCinematicMaterial(&material, 4));
    shader.name = "cinematic.hlsl";
    technique.passCount = 2;
    assert(!WebRenderer_IsCinematicMaterial(&material, 4));
}

void TestCanonicalWorldColorAliasUsesLitStateAndLightmaps()
{
    Fixture fixture;
    MaterialTechniqueSet techniqueSet{};
    techniqueSet.name = ",wc_l_sm_b0c0n0s0";
    GfxStateBits stateBits[2]{{{0u, 0u}},
                              {{0x18008812u, 0x0000000du}}};
    GfxImage baseImage{};
    GfxImage lightmapImage{};
    GfxImage secondaryLightmapImage{};
    MaterialTextureDef texture{};
    texture.semantic = 2u;
    texture.u.image = &baseImage;
    Material material{};
    material.info.name = "wc/material";
    material.textureCount = 1u;
    material.textureTable = &texture;
    material.techniqueSet = &techniqueSet;
    std::fill(std::begin(material.stateBitsEntry),
        std::end(material.stateBitsEntry), 0xffu);
    material.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 1u;
    material.stateBitsCount = 2u;
    material.stateBitsTable = stateBits;
    GfxLightmapArray lightmap{&lightmapImage, &secondaryLightmapImage};
    fixture.world.lightmapCount = 1;
    fixture.world.lightmaps = &lightmap;
    for (GfxSurface &surface : fixture.surfaces)
    {
        surface.material = &material;
        surface.lightmapIndex = 0u;
    }

    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches.size() == 1u);
    const WebRendererWorldBatchDesc &batch = command.batches[0];
    assert(batch.technique ==
        WebRendererWorldTechnique::BaseTextureLightmap);
    assert(std::strcmp(batch.techniqueName, ",wc_l_sm_b0c0n0s0") == 0);
    assert(batch.stateBits[0] == 0x18008812u);
    assert(batch.stateBits[1] == 0x0000000du);
    assert(batch.lightmapImage == nullptr);
    assert(batch.secondaryLightmapImage == &secondaryLightmapImage);
    assert(batch.customSamplerFlags == 4u);
    assert(batch.lightingMode ==
        WebRendererWorldLightingMode::SecondaryDirectional);
}

void TestNativePixelShaderFamiliesSelectPortableMaterialTechniques()
{
    Fixture fixture;
    MaterialPixelShader pixelShader{};
    pixelShader.name = "lm_r0c0n0_sm2.hlsl";
    MaterialTechnique litTechnique{};
    litTechnique.name = "lm_r0c0n0_sm2";
    litTechnique.passCount = 1u;
    litTechnique.passArray[0].customSamplerFlags = 4u;
    litTechnique.passArray[0].pixelShader = &pixelShader;
    MaterialTechniqueSet techniqueSet{};
    techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &litTechnique;
    GfxStateBits stateBits[1]{{{0x18008800u, 0x0000000du}}};
    GfxImage baseImage{};
    GfxImage normalImage{};
    GfxImage secondaryLightmapImage{};
    std::array<MaterialTextureDef, 2> textures{};
    textures[0].semantic = 2u;
    textures[0].u.image = &baseImage;
    textures[1].semantic = 5u;
    textures[1].samplerState = 0x22u;
    textures[1].u.image = &normalImage;
    Material material{};
    material.info.name = "material/native-families";
    material.textureCount = static_cast<std::uint8_t>(textures.size());
    material.textureTable = textures.data();
    material.techniqueSet = &techniqueSet;
    material.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 0u;
    material.stateBitsCount = 1u;
    material.stateBitsTable = stateBits;
    GfxLightmapArray lightmap{nullptr, &secondaryLightmapImage};
    fixture.world.lightmapCount = 1;
    fixture.world.lightmaps = &lightmap;
    for (GfxSurface &surface : fixture.surfaces)
    {
        surface.material = &material;
        surface.lightmapIndex = 0u;
    }

    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].technique ==
        WebRendererWorldTechnique::BaseTextureLightmapNormal);
    assert(command.batches[0].normalImage == &normalImage);
    assert(command.batches[0].normalSamplerState == 0x22u);

    pixelShader.name = "mul.hlsl";
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches[0].technique ==
        WebRendererWorldTechnique::VertexColorMultiply);

    pixelShader.name = "vertcol_simple_add_fog.hlsl";
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches[0].technique ==
        WebRendererWorldTechnique::VertexColorAdditive);
}

void TestDistanceFalloffVertexShaderCarriesCanonicalMaterialConstants()
{
    Fixture fixture;
    MaterialVertexShader vertexShader{};
    vertexShader.name = "vertcol_simple_fog_df.hlsl";
    const std::uint32_t vertexProgram[]{0xfffe0300u, 0x0000ffffu};
    vertexShader.prog.loadDef.program =
        const_cast<std::uint32_t *>(vertexProgram);
    vertexShader.prog.loadDef.programSize =
        static_cast<std::uint16_t>(std::size(vertexProgram));
    MaterialPixelShader pixelShader{};
    pixelShader.name = "vertcol_simple_fog.hlsl";
    MaterialTechnique technique{};
    technique.name = "vertcol_simple_dfalloff_fog";
    technique.passCount = 1u;
    technique.passArray[0u].vertexShader = &vertexShader;
    technique.passArray[0u].pixelShader = &pixelShader;
    MaterialTechniqueSet techniqueSet{};
    techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &technique;
    GfxStateBits stateBits[1]{{{0x19288939u, 0x0000000cu}}};
    GfxImage baseImage{};
    MaterialTextureDef texture{};
    texture.semantic = 2u;
    texture.u.image = &baseImage;
    std::array<MaterialConstantDef, 3u> constants{};
    constants[0u].nameHash = 0xbdde5cf5u;
    constants[1u].nameHash = 0x3d05a1f2u;
    constants[2u].nameHash = 0x6b1da6fau;
    const float falloffParms[4]{-0.01f, 1.0f, 0.0f, 0.0f};
    const float beginColor[4]{0.8f, 0.7f, 0.6f, 1.0f};
    const float endColor[4]{0.2f, 0.3f, 0.4f, 1.0f};
    std::copy_n(falloffParms, 4u, constants[0u].literal);
    std::copy_n(beginColor, 4u, constants[1u].literal);
    std::copy_n(endColor, 4u, constants[2u].literal);
    Material material{};
    material.info.name = "wc/hdrportal_darken";
    material.textureCount = 1u;
    material.textureTable = &texture;
    material.constantCount = static_cast<std::uint8_t>(constants.size());
    material.constantTable = constants.data();
    material.techniqueSet = &techniqueSet;
    material.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 0u;
    material.stateBitsCount = 1u;
    material.stateBitsTable = stateBits;
    for (GfxSurface &surface : fixture.surfaces)
        surface.material = &material;

    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches.size() == 1u);
    const WebRendererWorldBatchDesc &batch = command.batches[0u];
    assert(batch.technique ==
        WebRendererWorldTechnique::VertexColorDistanceFalloff);
    assert(std::strcmp(batch.vertexShaderName,
        "vertcol_simple_fog_df.hlsl") == 0);
    assert(batch.vertexShaderProgramHash != 0u);
    assert(std::strcmp(batch.pixelShaderName,
        "vertcol_simple_fog.hlsl") == 0);
    assert(std::memcmp(batch.falloffParms, falloffParms,
        sizeof(falloffParms)) == 0);
    assert(std::memcmp(batch.falloffBeginColor, beginColor,
        sizeof(beginColor)) == 0);
    assert(std::memcmp(batch.falloffEndColor, endColor,
        sizeof(endColor)) == 0);
}

void TestSunShadowTechniqueAndCanonicalCasterBitsSplitBatches()
{
    Fixture fixture;
    MaterialPixelShader pixelShader{};
    pixelShader.name = "lm_sm_sun_r0c0n0_sm2.hlsl";
    MaterialTechnique shadowTechnique{};
    shadowTechnique.name = "lm_sm_sun_r0c0n0_sm2";
    shadowTechnique.passCount = 1u;
    shadowTechnique.passArray[0].customSamplerFlags = 6u;
    shadowTechnique.passArray[0].pixelShader = &pixelShader;
    MaterialTechniqueSet techniqueSet{};
    techniqueSet.techniques[TECHNIQUE_LIT_SUN_SHADOW_INDEX] =
        &shadowTechnique;
    techniqueSet.techniques[TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX] =
        &shadowTechnique;
    GfxStateBits stateBits[1]{{{0x18008812u, 0x0000000du}}};
    GfxImage baseImage{};
    GfxImage normalImage{};
    GfxImage primaryLightmap{};
    GfxImage secondaryLightmap{};
    std::array<MaterialTextureDef, 2u> textures{};
    textures[0].semantic = 2u;
    textures[0].u.image = &baseImage;
    textures[1].semantic = 5u;
    textures[1].u.image = &normalImage;
    Material material{};
    material.info.name = "material/sun-shadow";
    material.textureCount = static_cast<std::uint8_t>(textures.size());
    material.textureTable = textures.data();
    material.techniqueSet = &techniqueSet;
    std::fill(std::begin(material.stateBitsEntry),
        std::end(material.stateBitsEntry), 0xffu);
    material.stateBitsEntry[TECHNIQUE_LIT_SUN_SHADOW_INDEX] = 0u;
    material.stateBitsEntry[TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX] = 0u;
    material.stateBitsCount = 1u;
    material.stateBitsTable = stateBits;
    GfxLightmapArray lightmap{&primaryLightmap, &secondaryLightmap};
    fixture.world.lightmapCount = 1;
    fixture.world.lightmaps = &lightmap;
    std::uint32_t casterBits = 0x5u;
    fixture.world.dpvs.surfaceCastsSunShadow = &casterBits;
    for (GfxSurface &surface : fixture.surfaces)
    {
        surface.material = &material;
        surface.lightmapIndex = 0u;
    }
    WebRendererSceneViewDesc view = MakeView();
    view.sunShadowEnabled = true;

    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, view, command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches.size() == 3u);
    assert(command.batches[0].castsSunShadow);
    assert(!command.batches[1].castsSunShadow);
    assert(command.batches[2].castsSunShadow);
    for (const WebRendererWorldBatchDesc &batch : command.batches)
    {
        assert(batch.castsSpotShadow);
        assert(batch.techniqueType == TECHNIQUE_LIT_SUN_SHADOW_INDEX);
        assert(batch.technique ==
            WebRendererWorldTechnique::BaseTextureLightmapNormal);
        assert(batch.lightmapImage == &primaryLightmap);
        assert(batch.secondaryLightmapImage == &secondaryLightmap);
        assert(batch.normalImage == &normalImage);
    }
}

void TestShaderModel3SpecularPassCarriesCanonicalInputs()
{
    Fixture fixture;
    MaterialPixelShader pixelShader{};
    pixelShader.name = "lm_r0c0n0s0_sm3.hlsl";
    MaterialTechnique technique{};
    technique.name = "lm_r0c0n0s0_sm3";
    technique.passCount = 1u;
    technique.passArray[0].customSamplerFlags = 5u;
    technique.passArray[0].pixelShader = &pixelShader;
    MaterialTechniqueSet techniqueSet{};
    techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &technique;
    GfxStateBits stateBits[1]{{{0x18008800u, 0x0000000du}}};
    GfxImage baseImage{};
    GfxImage normalImage{};
    GfxImage specularImage{};
    GfxImage secondaryLightmap{};
    GfxImage reflectionImage{};
    std::array<MaterialTextureDef, 3u> textures{};
    textures[0].semantic = 2u;
    textures[0].u.image = &baseImage;
    textures[1].semantic = 5u;
    textures[1].samplerState = 0x22u;
    textures[1].u.image = &normalImage;
    textures[2].semantic = 8u;
    textures[2].samplerState = 0x32u;
    textures[2].u.image = &specularImage;
    MaterialConstantDef envMapParms{};
    envMapParms.nameHash = 0x3d9994dcu;
    const float expectedEnvMapParms[4]{0.0f, 4.0f, 1.0f, 0.625f};
    std::copy_n(expectedEnvMapParms, 4u, envMapParms.literal);
    Material material{};
    material.info.name = "wc/com_plastic_wall";
    material.textureCount = static_cast<std::uint8_t>(textures.size());
    material.textureTable = textures.data();
    material.constantCount = 1u;
    material.constantTable = &envMapParms;
    material.techniqueSet = &techniqueSet;
    material.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 0u;
    material.stateBitsCount = 1u;
    material.stateBitsTable = stateBits;
    GfxLightmapArray lightmap{nullptr, &secondaryLightmap};
    fixture.world.lightmapCount = 1u;
    fixture.world.lightmaps = &lightmap;
    GfxReflectionProbe reflectionProbe{};
    reflectionProbe.reflectionImage = &reflectionImage;
    fixture.world.reflectionProbeCount = 1u;
    fixture.world.reflectionProbes = &reflectionProbe;
    for (GfxSurface &surface : fixture.surfaces)
    {
        surface.material = &material;
        surface.lightmapIndex = 0u;
        surface.reflectionProbeIndex = 0u;
    }

    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches.size() == 1u);
    const WebRendererWorldBatchDesc &batch = command.batches[0];
    assert(batch.technique ==
        WebRendererWorldTechnique::BaseTextureLightmapNormalSpecular);
    assert(batch.baseImage == &baseImage);
    assert(batch.normalImage == &normalImage);
    assert(batch.specularImage == &specularImage);
    assert(batch.specularSamplerState == 0x32u);
    assert(batch.secondaryLightmapImage == &secondaryLightmap);
    assert(batch.reflectionProbeImage == &reflectionImage);
    assert(batch.reflectionProbeIndex == 0u);
    assert(std::memcmp(batch.envMapParms, expectedEnvMapParms,
        sizeof(expectedEnvMapParms)) == 0);
    assert(WebRenderer_UsesWorldSpecularMap(batch.technique));
}

void TestMalformedLocalIndexIsRejectedAtomically()
{
    Fixture fixture;
    fixture.indices[1] = 3u;
    WebRendererWorldSceneCommand command;
    command.surfaceCount = 99u;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::IndexOutOfRange);
    assert(command.surfaceCount == 99u);
}

void TestNonFiniteWorldVertexIsRejectedAtomically()
{
    Fixture fixture;
    GfxWorldVertex &vertex = fixture.vertices[0];
    for (float *component : {&vertex.xyz[0], &vertex.texCoord[0],
            &vertex.texCoord[1], &vertex.lmapCoord[0], &vertex.lmapCoord[1],
            &vertex.binormalSign})
    {
        const float saved = *component;
        for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity()})
        {
            *component = invalid;
            WebRendererWorldSceneCommand command;
            command.surfaceCount = 99u;
            assert(WebRenderer_BuildWorldSceneCommand(
                fixture.world, MakeView(), command) ==
                WebRendererWorldSceneResult::InvalidSurfaceBounds);
            assert(command.surfaceCount == 99u);
            assert(command.vertices.empty() && command.indices.empty());
        }
        *component = saved;
    }
}

void TestSharedMaterialLookupPreservesPrecedenceAndDefaults()
{
    GfxImage semanticImage{}, namedImage{};
    MaterialTextureDef textures[2]{};
    textures[0].semantic = 2u;
    textures[0].samplerState = 3u;
    textures[0].u.image = &semanticImage;
    textures[1].nameHash = 0xa0ab1041u;
    textures[1].samplerState = 7u;
    textures[1].u.image = &namedImage;
    Material material{};
    material.textureCount = 2u;
    material.textureTable = textures;
    std::uint8_t sampler = 11u;
    assert(WebRenderer_FindBaseImage(&material, sampler) == &namedImage);
    assert(sampler == 7u);
    textures[1].u.image = nullptr;
    assert(WebRenderer_FindBaseImage(&material, sampler) == &semanticImage);
    assert(sampler == 3u);
    assert(WebRenderer_FindBaseImage(nullptr, sampler) == nullptr);
    assert(WebRenderer_FindDetailImage(&material, sampler) == nullptr);
    assert(sampler == 3u);
    float constant[4]{1.0f, 2.0f, 3.0f, 4.0f};
    assert(!WebRenderer_CopyMaterialConstant(&material, 123u, constant));
    assert(constant[0] == 1.0f && constant[3] == 4.0f);
}

void TestSkyPassIsNotFoldedIntoOpaqueWorldBatch()
{
    Fixture fixture;
    Material sky{};
    sky.info.gameFlags = 8u;
    fixture.surfaces[0].material = &sky;
    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.surfaceCount == 2u);
    assert(command.firstSurfaceIndex == 1u);
    assert(command.vertices.size() == 6u);
}

void TestSpecialSurfaceInventoryUsesCanonicalMaterialData()
{
    Fixture fixture;
    MaterialTechnique technique{};
    technique.name = "special";
    technique.flags = 3u;
    technique.passCount = 1u;
    MaterialTechniqueSet techniqueSet{};
    techniqueSet.techniques[4] = &technique;
    GfxStateBits stateBits{};
    GfxImage baseImage{};
    water_t water{};
    MaterialTextureDef textures[2]{};
    textures[0].semantic = 2u;
    textures[0].u.image = &baseImage;
    textures[1].semantic = 11u;
    textures[1].u.water = &water;
    Material material{};
    material.textureCount = 2u;
    material.textureTable = textures;
    material.techniqueSet = &techniqueSet;
    material.stateBitsEntry[4] = 0u;
    material.stateBitsCount = 1u;
    material.stateBitsTable = &stateBits;
    fixture.surfaces[0].material = &material;
    fixture.surfaces[1].material = &material;

    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.waterSurfaceCount == 2u);
    assert(command.waterMaterialCount == 1u);
    assert(command.resolvedSceneSurfaceCount == 2u);
    assert(command.resolvedPostSunSurfaceCount == 2u);
}

void TestCanonicalWaterPassCarriesSimulationAndReflectionInputs()
{
    Fixture fixture;
    MaterialPixelShader pixelShader{};
    pixelShader.name = "water_l_sun.hlsl";
    MaterialTechnique technique{};
    technique.name = "water_l_sun";
    technique.passCount = 1u;
    technique.passArray[0].pixelShader = &pixelShader;
    technique.passArray[0].customSamplerFlags = 1u;
    MaterialTechniqueSet techniqueSet{};
    techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &technique;
    GfxStateBits stateBits{{0x19288065u, 12u}};
    std::array<complex_s, 16> h0{};
    std::array<float, 16> wTerm{};
    water_t water{};
    water.H0 = h0.data();
    water.wTerm = wTerm.data();
    water.M = 4;
    water.N = 4;
    MaterialTextureDef waterTexture{};
    waterTexture.semantic = 11u;
    waterTexture.samplerState = 2u;
    waterTexture.u.water = &water;
    MaterialConstantDef constants[2]{};
    constants[0].nameHash = 0x3d9994dcu;
    const float envMapParms[4]{0.0f, 0.5f, 4.25f, 2.5f};
    std::copy_n(envMapParms, 4u, constants[0].literal);
    constants[1].nameHash = 0xb82a51e8u;
    const float waterColor[4]{0.53f, 0.47f, 0.40f, 1.0f};
    std::copy_n(waterColor, 4u, constants[1].literal);
    Material material{};
    material.info.name = "wc/kh_water_mud";
    material.textureCount = 1u;
    material.textureTable = &waterTexture;
    material.constantCount = 2u;
    material.constantTable = constants;
    material.techniqueSet = &techniqueSet;
    material.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 0u;
    material.stateBitsCount = 1u;
    material.stateBitsTable = &stateBits;
    GfxImage reflectionImage{};
    GfxReflectionProbe reflectionProbe{};
    reflectionProbe.reflectionImage = &reflectionImage;
    fixture.world.reflectionProbeCount = 1u;
    fixture.world.reflectionProbes = &reflectionProbe;
    fixture.surfaces[0].material = &material;
    fixture.surfaces[0].reflectionProbeIndex = 0u;

    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    const WebRendererWorldBatchDesc &batch = command.batches[0];
    assert(batch.technique == WebRendererWorldTechnique::WaterLitSun);
    assert(batch.water == &water);
    assert(batch.waterSamplerState == 2u);
    assert(batch.reflectionProbeImage == &reflectionImage);
    assert(batch.reflectionProbeIndex == 0u);
    assert(std::memcmp(batch.envMapParms, envMapParms,
        sizeof(envMapParms)) == 0);
    assert(std::memcmp(batch.waterColor, waterColor,
        sizeof(waterColor)) == 0);
}

void TestConservativeVisibilityIsDisabledForMovingCanonicalView()
{
    Fixture fixture;
    WebRendererSceneViewDesc view = MakeView();
    view.viewAxis[0][0] = -1.0f;
    view.viewAxis[1][1] = -1.0f;
    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, view, command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.surfaceCount == 3u);
    assert(command.firstSurfaceIndex == 0u);

    view.viewOrigin[0] = -1000.0f;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, view, command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.surfaceCount == 3u);
}

void TestCanonicalDpvsRangesOverrideNonContiguousModelCount()
{
    Fixture fixture;
    fixture.model.surfaceCountNoDecal = 1u;
    fixture.world.dpvs.litSurfsBegin = 0u;
    fixture.world.dpvs.litSurfsEnd = 2u;
    fixture.world.dpvs.decalSurfsBegin = 2u;
    fixture.world.dpvs.decalSurfsEnd = 3u;
    fixture.world.dpvs.emissiveSurfsBegin = 3u;
    fixture.world.dpvs.emissiveSurfsEnd = 3u;
    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.surfaceCount == 3u);
    assert(command.firstSurfaceIndex == 0u);
    assert(command.lastSurfaceIndex == 2u);
}

void TestSpotShadowCommandPreservesAuthoredCasterMembership()
{
    Fixture fixture;
    MaterialTechnique shadowTechnique{};
    shadowTechnique.passCount = 1u;
    MaterialTechniqueSet techniqueSet{};
    techniqueSet.techniques[TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX] =
        &shadowTechnique;
    GfxStateBits shadowState{};
    shadowState.loadBits[0] = 0x1234c000u;
    Material material{};
    material.techniqueSet = &techniqueSet;
    material.stateBitsCount = 1u;
    material.stateBitsTable = &shadowState;
    std::fill(std::begin(material.stateBitsEntry),
        std::end(material.stateBitsEntry), 0xffu);
    material.stateBitsEntry[TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX] = 0u;
    for (GfxSurface &surface : fixture.surfaces)
        surface.material = &material;

    std::array<std::uint16_t, 2u> casterSurfaces{2u, 0u};
    std::array<std::uint16_t, 2u> casterModels{4u, 1u};
    std::array<GfxShadowGeometry, 3u> shadowGeometry{};
    shadowGeometry[2u].surfaceCount =
        static_cast<std::uint16_t>(casterSurfaces.size());
    shadowGeometry[2u].sortedSurfIndex = casterSurfaces.data();
    shadowGeometry[2u].smodelCount =
        static_cast<std::uint16_t>(casterModels.size());
    shadowGeometry[2u].smodelIndex = casterModels.data();
    fixture.world.primaryLightCount =
        static_cast<std::uint32_t>(shadowGeometry.size());
    fixture.world.shadowGeom = shadowGeometry.data();
    fixture.world.dpvs.smodelCount = 5u;

    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.spotShadowCasters.size() == 2u);
    assert(command.spotShadowCasters[0u].primaryLightIndex == 2u);
    assert(command.spotShadowCasters[0u].firstIndex == 6u);
    assert(command.spotShadowCasters[0u].indexCount == 3u);
    assert(command.spotShadowCasters[0u].batchIndex == 0u);
    assert(command.spotShadowCasters[0u].stateBits0 == 0x1234c000u);
    assert(command.spotShadowCasters[1u].firstIndex == 0u);
    assert(command.spotShadowStaticModels.size() == 2u);
    assert(command.spotShadowStaticModels[0u].primaryLightIndex == 2u);
    assert(command.spotShadowStaticModels[0u].canonicalInstanceIndex == 4u);
    assert(command.spotShadowStaticModels[1u].canonicalInstanceIndex == 1u);
}

void TestDynamicBrushModelUsesCanonicalSurfaceRangeAndPlacement()
{
    Fixture fixture;
    MaterialTechnique shadowTechnique{};
    MaterialTechniqueSet shadowTechniqueSet{};
    shadowTechniqueSet.techniques[
        TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX] = &shadowTechnique;
    Material brushMaterial{};
    brushMaterial.techniqueSet = &shadowTechniqueSet;
    fixture.surfaces[2].material = &brushMaterial;
    std::array<GfxBrushModel, 2> models{};
    models[0] = fixture.model;
    models[1].startSurfIndex = 2u;
    models[1].surfaceCount = 1u;
    fixture.world.models = models.data();
    fixture.world.modelCount = static_cast<int>(models.size());

    WebRendererBrushModelSubmission submission{};
    submission.model = &models[1];
    submission.origin[0] = 100.0f;
    submission.origin[1] = 200.0f;
    submission.origin[2] = 300.0f;
    submission.axis[0][0] = 1.0f;
    submission.axis[1][1] = 1.0f;
    submission.axis[2][2] = 1.0f;
    submission.entityNumber = 42u;

    WebRendererBrushModelSceneCommand command;
    assert(WebRenderer_BuildBrushModelSceneCommand(
        fixture.world, &submission, 1u, command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.modelCount == 1u);
    assert(command.surfaceCount == 1u);
    assert(command.vertices.size() == 3u);
    assert(command.indices == std::vector<std::uint32_t>({0u, 1u, 2u}));
    assert(command.vertices[0].position[0] == 112.0f);
    // Surface 2's canonical index order starts at local vertex 2.
    assert(command.vertices[0].position[1] == 200.0f);
    assert(command.vertices[0].position[2] == 301.0f);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].sourceKind ==
        WebRendererSceneBatchKind::DynamicBModel);
    assert(command.batches[0].dynamicLightSurfType == 6u);
    assert(command.batches[0].firstSurfaceIndex == 2u);
    assert(command.batches[0].lastSurfaceIndex == 2u);
    assert(command.batches[0].castsSunShadow);

    fixture.surfaces[2].primaryLightIndex = 2u;
    std::array<WebRendererPrimaryLightDesc, 3u> lights{};
    lights[2u].type = 2u;
    const WebRendererWorldLightTechniqueContext lightContext{
        lights.data(), static_cast<std::uint32_t>(lights.size()), 1u, false};
    assert(WebRenderer_BuildBrushModelSceneCommand(
        fixture.world, &submission, 1u, command, &lightContext) ==
        WebRendererWorldSceneResult::Success);
    assert(command.batches[0].primaryLightIndex == 2u);
    assert(command.batches[0].techniqueType == 36u);
    assert(command.batches[0].technique ==
        WebRendererWorldTechnique::NativeTechniqueUnavailable);
}

void TestTextureParameterMemoPreservesAliasedObjectState()
{
    struct Bind { std::uint32_t texture; std::uint8_t sampler; bool mipmaps; };
    const std::array<Bind, 9> bindings{{
        {1, 0x62, true}, {2, 0x62, true}, {1, 0x62, true},
        {1, 0x23, true}, {1, 0x23, false}, {257, 0x62, true},
        {1, 0x23, false}, {2, 0x62, true}, {1, 0x62, true}}};
    WebRendererTextureParameters memo;
    std::array<std::pair<std::uint8_t, bool>, 258> original{}, cached{};
    std::uint32_t writes = 0;
    for (const auto &bind : bindings)
    {
        original[bind.texture] = {bind.sampler, bind.mipmaps};
        if (memo.NeedsUpdate(bind.texture, bind.sampler, bind.mipmaps))
        {
            cached[bind.texture] = {bind.sampler, bind.mipmaps};
            ++writes;
        }
        // Every draw sees the same last-write state, including a texture
        // shared by units with different samplers and a table collision.
        assert(original == cached);
    }
    assert(writes == 7u);
    memo.Reset();
    assert(memo.NeedsUpdate(1, 0x62, true)); // name reused after upload/recovery

    WebRendererDynamicTextures dynamic;
    std::array<std::uint32_t, 10> expectedUnits{}, actualUnits{};
    const std::array<WebRendererDynamicTextureSet, 4> sets{{
        {{1, 2, 3, 4, 5, 6}, {1, 2, 3, 4}},
        {{1, 2, 3, 4, 5, 7}, {1, 2, 3, 4}},
        {{1, 1, 1, 1, 1, 1}, {1, 2, 3, 4}},
        {{1, 2, 3, 4, 5, 6}, {1, 2, 3, 4}}}};
    for (const auto &set : sets)
    {
        constexpr std::array<unsigned, 6> units{0, 1, 4, 5, 2, 9};
        for (unsigned i = 0; i < units.size(); ++i)
        {
            expectedUnits[units[i]] = set.textures[i];
            original[set.textures[i]] = {i < 4 ? set.samplers[i] : 0x62, true};
        }
        dynamic.Apply(set, [&](auto unit, auto texture, auto sampler, bool unchanged) {
            if (unchanged) assert(actualUnits[unit] == texture);
            else actualUnits[unit] = texture;
            if (memo.NeedsUpdate(texture, sampler, true))
                cached[texture] = {sampler, true};
        });
        assert(expectedUnits == actualUnits && original == cached);
    }
}

void TestSunShadowRangesPreserveTriangleOrderAndCutouts()
{
    // Each triple names one triangle. The ordinary per-batch loop is the
    // independent coverage oracle, including off-camera/non-caster gaps.
    std::array<WebRendererWorldBatchDesc, 8> batches{};
    for (std::uint32_t index = 0u; index < batches.size(); ++index)
    {
        batches[index].firstIndex = index * 3u;
        batches[index].indexCount = 3u;
        batches[index].castsSunShadow = true;
    }
    batches[2].castsSunShadow = false;
    batches[4].stateBits[0] = 0x1000u; // cutout stays a separate draw
    batches[7].firstIndex = 30u; // non-contiguous geometry cannot be bridged
    std::vector<std::uint32_t> expected, actual;
    for (const auto &batch : batches)
        if (batch.castsSunShadow)
            for (std::uint32_t index = 0u; index < batch.indexCount; ++index)
                expected.push_back(batch.firstIndex + index);
    std::vector<std::array<std::uint32_t, 3>> draws;
    const auto merged = WebRenderer_ForEachSunShadowRange(batches,
        [](const auto &batch) { return batch.stateBits[0] == 0u; },
        [&](const auto &batch, std::uint32_t first, std::uint32_t count) {
            draws.push_back({first, count, batch.stateBits[0]});
            for (std::uint32_t index = 0u; index < count; ++index)
                actual.push_back(first + index);
        });
    assert(actual == expected);
    assert(merged == 2u);
    const std::vector<std::array<std::uint32_t, 3>> expectedDraws{
        {0u, 6u, 0u}, {9u, 3u, 0u}, {12u, 3u, 0x1000u},
        {15u, 6u, 0u}, {30u, 3u, 0u}};
    assert(draws == expectedDraws);
    for (auto &batch : batches) batch.castsSunShadow = false;
    assert(WebRenderer_ForEachSunShadowRange(batches,
        [](const auto &) { return true; },
        [](const auto &, auto, auto) { assert(false); }) == 0u);
}

void TestWorldSunShadowRangesCullPartitionsIndependently()
{
    const auto makeRange = [](std::uint32_t surface, std::uint32_t batch,
                               std::uint32_t first, float minimumX,
                               float maximumX) {
        WebRendererWorldSurfaceRange range{surface, batch, first, 3u};
        range.mins[0] = minimumX;
        range.mins[1] = range.mins[2] = -0.25f;
        range.maxs[0] = maximumX;
        range.maxs[1] = range.maxs[2] = 0.25f;
        return range;
    };
    const std::vector<WebRendererWorldSurfaceRange> surfaces{
        makeRange(0u, 0u, 0u, -0.5f, 0.5f),
        makeRange(1u, 0u, 3u, 2.0f, 2.5f),
        makeRange(2u, 0u, 6u, 0.5f, 1.0f),
        makeRange(3u, 1u, 9u, -0.5f, 0.5f),
    };
    const std::array<float, 16> nearMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    auto farMatrix = nearMatrix;
    farMatrix[0] = 0.25f;

    std::vector<WebRendererWorldShadowRange> nearRanges, farRanges;
    assert(WebRenderer_BuildWorldShadowRanges(
        surfaces, nearMatrix, nearRanges));
    assert(WebRenderer_BuildWorldShadowRanges(
        surfaces, farMatrix, farRanges));
    assert(nearRanges.size() == 3u);
    assert(nearRanges[0].firstIndex == 0u && nearRanges[0].indexCount == 3u);
    assert(nearRanges[1].firstIndex == 6u && nearRanges[1].indexCount == 3u);
    assert(nearRanges[2].firstIndex == 9u && nearRanges[2].indexCount == 3u);
    assert(farRanges.size() == 2u);
    assert(farRanges[0].firstIndex == 0u && farRanges[0].indexCount == 9u);
    assert(farRanges[0].surfaceCount == 3u);
    assert(farRanges[1].firstIndex == 9u && farRanges[1].indexCount == 3u);

    std::array<WebRendererWorldBatchDesc, 2> batches{};
    for (auto &batch : batches) batch.castsSunShadow = true;
    std::vector<std::array<std::uint32_t, 2>> draws;
    assert(WebRenderer_ForEachWorldSunShadowRange(nearRanges, batches,
        [](const auto &batch) { return batch.castsSunShadow; },
        [](const auto &) { return true; },
        [&](const auto &, std::uint32_t first, std::uint32_t count) {
            draws.push_back({first, count});
        }) == 1u);
    assert((draws == std::vector<std::array<std::uint32_t, 2>>{
        {0u, 3u}, {6u, 6u}}));

    auto malformed = surfaces;
    malformed[1].mins[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!WebRenderer_BuildWorldShadowRanges(
        malformed, nearMatrix, nearRanges));
    assert(nearRanges.empty());
}

void TestBrushReceiverUsesCanonicalWritableBounds()
{
    GfxBrushModel model{};
    model.writable.mins[0] = 10.0f;
    model.writable.maxs[0] = 11.0f;
    WebRendererBrushModelInstanceDesc instance{};
    // Writable bounds are already world-space. Do not transform them again.
    instance.origin[0] = 100.0f;
    instance.axis[0][1] = 1.0f;
    instance.axis[1][0] = -1.0f;
    instance.axis[2][2] = 1.0f;
    assert(WebRenderer_CopyBrushReceiverBounds(model, instance));
    assert(instance.receiverMins[0] == 10.0f && instance.receiverMaxs[0] == 11.0f);
    GfxLight light{};
    light.type = 3;
    light.radius = 10.0f;
    float planes[6][4];
    assert(kisak::dynamic_lights::ReceiverPlanes(light, 0.0f, planes));
    assert(WebRenderer_BrushReceivesLight(instance, light, planes));
    model.writable.mins[0] = 10.01f;
    assert(WebRenderer_CopyBrushReceiverBounds(model, instance));
    assert(!WebRenderer_BrushReceivesLight(instance, light, planes));
    light.type = 2;
    light.dir[0] = -1.0f;
    light.cosHalfFovOuter = 0.9f;
    assert(kisak::dynamic_lights::ReceiverPlanes(light, 3.0f, planes));
    model.writable.mins[0] = 1.0f;
    model.writable.maxs[0] = 3.0f;
    assert(WebRenderer_CopyBrushReceiverBounds(model, instance));
    assert(!WebRenderer_BrushReceivesLight(instance, light, planes));
    model.writable.maxs[0] = 3.01f;
    assert(WebRenderer_CopyBrushReceiverBounds(model, instance));
    assert(WebRenderer_BrushReceivesLight(instance, light, planes));
    for (const float invalid : {0.0f, std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity()})
    {
        model.writable.maxs[0] = invalid;
        assert(!WebRenderer_CopyBrushReceiverBounds(model, instance));
        assert(instance.receiverMaxs[0] == 3.01f); // failed copy is atomic
        auto invalidInstance = instance;
        invalidInstance.receiverMaxs[0] = invalid;
        assert(!WebRenderer_BrushPlacementIsFinite(invalidInstance, {}, 0.0f));
    }
}

void TestRetainedBrushPlacementMatchesExpandedGeometry()
{
    Fixture fixture;
    WebRendererBrushModelSubmission submission{};
    submission.model = &fixture.model;
    for (std::size_t axis = 0u; axis < 3u; ++axis) submission.axis[axis][axis] = 1.0f;
    WebRendererBrushModelSceneCommand retained;
    assert(WebRenderer_BuildBrushModelSceneCommand(fixture.world, &submission, 1u, retained) ==
        WebRendererWorldSceneResult::Success);
    float maximumCoordinate = 0.0f;
    for (const auto &vertex : retained.vertices)
        for (std::size_t component = 0u; component < 3u; ++component)
            maximumCoordinate = std::max({maximumCoordinate, std::fabs(vertex.position[component]),
                std::fabs(vertex.normal[component]), std::fabs(vertex.tangent[component])});

    WebRendererBrushModelInstanceDesc instance{};
    instance.axis[0][1] = 1.0f;
    instance.axis[1][0] = -1.0f;
    instance.axis[2][2] = 1.0f;
    instance.origin[0] = 100.0f;
    instance.origin[1] = 200.0f;
    instance.origin[2] = 300.0f;
    for (const float offset : {100.0f, -27.0f})
    {
        instance.origin[0] = offset;
        assert(WebRenderer_BrushPlacementIsFinite(instance, retained.vertices, maximumCoordinate));
        std::memcpy(submission.axis, instance.axis, sizeof(instance.axis));
        std::memcpy(submission.origin, instance.origin, sizeof(instance.origin));
        WebRendererBrushModelSceneCommand expanded;
        assert(WebRenderer_BuildBrushModelSceneCommand(fixture.world, &submission, 1u, expanded) ==
            WebRendererWorldSceneResult::Success);
        assert(expanded.indices == retained.indices && expanded.batches.size() == retained.batches.size());
        for (std::size_t index = 0u; index < retained.vertices.size(); ++index)
        {
            const auto &source = retained.vertices[index];
            const auto &expected = expanded.vertices[index];
            // Explicit quarter-turn placement, independent of the builder's matrix loop.
            assert(expected.position[0] == offset - source.position[1]);
            assert(expected.position[1] == 200.0f + source.position[0]);
            assert(expected.position[2] == 300.0f + source.position[2]);
            assert(expected.normal[0] == -source.normal[1] && expected.normal[1] == source.normal[0]);
            assert(expected.tangent[0] == -source.tangent[1] && expected.tangent[1] == source.tangent[0]);
        }
    }
    instance.origin[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!WebRenderer_BrushPlacementIsFinite(instance, retained.vertices, maximumCoordinate));
    instance.origin[0] = 0.0f;
    instance.axis[0][0] = std::numeric_limits<float>::max();
    assert(!WebRenderer_BrushPlacementIsFinite(instance, retained.vertices, maximumCoordinate));
    instance.axis[0][0] = std::numeric_limits<float>::infinity();
    assert(!WebRenderer_BrushPlacementIsFinite(instance, retained.vertices, maximumCoordinate));

    // The shader adds origin first; cancellation in the old CPU expansion
    // must not conceal overflow in that ordering at the GPU boundary.
    std::vector<WebRendererSurfaceVertex> large(1);
    large[0].position[0] = std::numeric_limits<float>::max();
    large[0].position[1] = -std::numeric_limits<float>::max();
    instance = {};
    instance.axis[0][0] = instance.axis[1][0] = 1.0f;
    instance.origin[0] = std::numeric_limits<float>::max();
    assert(!WebRenderer_BrushPlacementIsFinite(instance, large, std::numeric_limits<float>::max()));
}

void TestMalformedDynamicBrushRangeIsRejectedAtomically()
{
    Fixture fixture;
    GfxBrushModel model{};
    model.startSurfIndex = 2u;
    model.surfaceCount = 2u;
    WebRendererBrushModelSubmission submission{};
    submission.model = &model;
    submission.axis[0][0] = 1.0f;
    submission.axis[1][1] = 1.0f;
    submission.axis[2][2] = 1.0f;
    WebRendererBrushModelSceneCommand command;
    command.surfaceCount = 99u;
    assert(WebRenderer_BuildBrushModelSceneCommand(
        fixture.world, &submission, 1u, command) ==
        WebRendererWorldSceneResult::InvalidSurfaceRange);
    assert(command.surfaceCount == 99u);
}

void TestBrushMatchesWorldSelectionAndRejectsAtomically()
{
    // Repository-authored synthetic data, under the repository license.
    // The uncached world path supplies the technique/hash and vertex oracle.
    Fixture fixture;
    std::uint32_t vertexProgram[]{0x12345678u, 0x90abcdefu};
    std::uint32_t pixelProgram[]{0x76543210u, 0xfedcba98u};
    MaterialVertexShader vertexShader{};
    vertexShader.name = "synthetic_vertex";
    vertexShader.prog.loadDef.program = vertexProgram;
    vertexShader.prog.loadDef.programSize = 2;
    MaterialPixelShader pixelShader{};
    pixelShader.name = "synthetic_pixel";
    pixelShader.prog.loadDef.program = pixelProgram;
    pixelShader.prog.loadDef.programSize = 2;
    MaterialTechnique lit{};
    lit.name = "synthetic_lit";
    lit.passCount = 1;
    lit.flags = 17;
    lit.passArray[0].vertexShader = &vertexShader;
    lit.passArray[0].pixelShader = &pixelShader;
    lit.passArray[0].customSamplerFlags = 6;
    MaterialTechnique spot = lit;
    spot.name = "synthetic_spot";
    spot.flags = 23;
    std::uint32_t spotProgram[]{0x11223344u};
    MaterialPixelShader spotShader = pixelShader;
    spotShader.name = "synthetic_spot_pixel";
    spotShader.prog.loadDef.program = spotProgram;
    spotShader.prog.loadDef.programSize = 1;
    spot.passArray[0].pixelShader = &spotShader;
    MaterialTechniqueSet techniqueSet{};
    techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &lit;
    techniqueSet.techniques[TECHNIQUE_LIT_SPOT_INDEX] = &spot;
    techniqueSet.techniques[TECHNIQUE_BUILD_SHADOWMAP_DEPTH_INDEX] = &lit;
    GfxStateBits states[2]{};
    states[0].loadBits[0] = 0x1234;
    states[1].loadBits[1] = 0x5678;
    Material material{};
    material.techniqueSet = &techniqueSet;
    material.stateBitsTable = states;
    material.stateBitsCount = 2;
    std::fill_n(material.stateBitsEntry, 34, std::uint8_t{0xff});
    material.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 0;
    material.stateBitsEntry[TECHNIQUE_LIT_SPOT_INDEX] = 1;
    Material otherMaterial = material;
    GfxStateBits otherStates[2]{};
    otherStates[0].loadBits[0] = 0xabcd;
    otherMaterial.stateBitsTable = otherStates;
    for (std::size_t i = 0; i < fixture.surfaces.size(); ++i)
    {
        fixture.surfaces[i].material = &material;
        // Split batches without changing their requested technique.
        fixture.surfaces[i].lightmapIndex = static_cast<std::uint8_t>(i);
    }
    std::array<WebRendererPrimaryLightDesc, 3> lights{};
    lights[2].type = 2;
    const WebRendererWorldLightTechniqueContext context{lights.data(), 3, 1, false};
    WebRendererBrushModelSubmission submission{};
    submission.model = &fixture.model;
    for (std::size_t i = 0; i < 3; ++i) submission.axis[i][i] = 1.0f;
    WebRendererBrushModelSceneCommand actual;
    const auto check = [&]() {
        WebRendererWorldSceneCommand expected;
        assert(WebRenderer_BuildWorldSceneCommand(fixture.world, MakeView(), expected, &context) ==
            WebRendererWorldSceneResult::Success);
        assert(WebRenderer_BuildBrushModelSceneCommand(fixture.world, &submission, 1, actual, &context) ==
            WebRendererWorldSceneResult::Success);
        assert(actual.indices == expected.indices && actual.vertices.size() == expected.vertices.size());
        assert(std::memcmp(actual.vertices.data(), expected.vertices.data(),
            actual.vertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);
        assert(actual.batches.size() == 3 && expected.batches.size() == 3);
        for (std::size_t i = 0; i < actual.batches.size(); ++i)
        {
            const auto &a = actual.batches[i];
            const auto &e = expected.batches[i];
            assert(a.materialIdentity == e.materialIdentity && a.techniqueType == e.techniqueType);
            assert(std::strcmp(a.techniqueName, e.techniqueName) == 0 && a.technique == e.technique);
            assert(a.techniqueFlags == e.techniqueFlags && a.customSamplerFlags == e.customSamplerFlags);
            assert(std::strcmp(a.vertexShaderName, e.vertexShaderName) == 0);
            assert(std::strcmp(a.pixelShaderName, e.pixelShaderName) == 0);
            assert(a.vertexShaderProgramHash == e.vertexShaderProgramHash);
            assert(a.pixelShaderProgramHash == e.pixelShaderProgramHash);
            assert(a.stateBits[0] == e.stateBits[0] && a.stateBits[1] == e.stateBits[1]);
            assert(a.firstIndex == e.firstIndex && a.indexCount == e.indexCount);
            assert(a.primaryLightIndex == e.primaryLightIndex && a.lightmapIndex == e.lightmapIndex);
            assert(a.sourceKind == WebRendererSceneBatchKind::DynamicBModel);
            assert(a.castsSunShadow == (a.materialIdentity != nullptr));
        }
    };
    check(); // Consecutive identical keys still emit distinct lightmap batches.
    fixture.surfaces[1].material = &otherMaterial;
    check(); // A -> B -> A must not reuse a different material's state.
    fixture.surfaces[1].material = &material;
    fixture.surfaces[1].primaryLightIndex = 2;
    check(); // Same material, lit -> spot -> lit.
    lit.passArray[0].vertexShader = nullptr;
    check(); // Null -> vertex shader -> null, independently of pixel changes.
    lit.passArray[0].vertexShader = &vertexShader;
    vertexProgram[0] ^= 0xffu;
    pixelProgram[1] ^= 0xffu;
    states[0].loadBits[0] ^= 0xffu;
    check(); // New builds must observe changed shader bytes and state.
    techniqueSet.techniques[TECHNIQUE_LIT_SPOT_INDEX] = nullptr;
    check(); // Missing requested technique must not reuse the lit result.
    for (auto &surface : fixture.surfaces) surface.material = nullptr;
    check();
    const auto savedVertices = actual.vertices;
    const auto savedIndices = actual.indices;
    fixture.vertices[8].binormalSign = std::numeric_limits<float>::quiet_NaN();
    assert(WebRenderer_BuildBrushModelSceneCommand(fixture.world, &submission, 1, actual, &context) ==
        WebRendererWorldSceneResult::InvalidSurfaceBounds);
    assert(actual.vertices.size() == savedVertices.size());
    assert(std::memcmp(actual.vertices.data(), savedVertices.data(),
        savedVertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);
    assert(actual.indices == savedIndices && actual.batches.size() == 3 && actual.surfaceCount == 3);
}
} // namespace

void TestAuthoredSoftParticleBindings()
{
    Material material{}; MaterialTechniqueSet set{}, remapped{}; MaterialTechnique tech{};
    MaterialVertexShader vs{}; MaterialPixelShader ps{}; MaterialShaderArgument args[9]{};
    float feather[4]{0.05f,20,0,0}, falloff[4]{0,0,2,-1}, begin[4]{1,0,0,1}, end[4]{0,1,0,1};
    float eye[4]{2,0,0,0}, fog[4]{0.1f,0.2f,0.3f,1};
    const auto literal=[&](unsigned i,unsigned type,unsigned dest,const float *value) {
        args[i].type=type; args[i].dest=dest; args[i].u.literalConst=value;
    };
    literal(0,1,12,feather); literal(1,7,5,feather);
    args[2].type=4; args[2].dest=4; args[2].u.codeSampler=static_cast<MaterialTextureSource>(18);
    args[3].type=2; args[3].dest=0; args[3].u.nameHash=0xa0ab1041u;
    literal(4,1,13,falloff); literal(5,1,14,begin); literal(6,1,15,end);
    literal(7,1,16,eye); literal(8,7,0,fog);
    tech.passCount=1; tech.passArray[0].vertexShader=&vs; tech.passArray[0].pixelShader=&ps;
    tech.passArray[0].args=args; tech.passArray[0].stableArgCount=9;
    material.techniqueSet=&set; set.remappedTechniqueSet=&remapped; remapped.techniques[5]=&tech;
    WebRendererSoftParticle soft{};
    vs.name="zfeather_foa_nf_eo_dtex.hlsl"; ps.name="zfeather_add_nf.hlsl";
    assert(WebRenderer_GetSoftParticle(&material,5,soft));
    assert(soft.flags==14 && soft.feather[0]==0.05f && soft.feather[1]==0.05f && soft.eyeOffset==2);
    assert(soft.falloff[2]==2 && soft.falloff[3]==-1 && soft.beginColor[0]==1 && soft.endColor[1]==1);
    vs.name="zfeather_foa_dtex.hlsl"; ps.name="zfeather.hlsl";
    assert(WebRenderer_GetSoftParticle(&material,5,soft));
    assert(soft.flags==5 && !soft.sceneFog && soft.fogColor[1]==0.2f);
    args[8].type=5; args[8].u.codeConst.index=42;
    assert(WebRenderer_GetSoftParticle(&material,5,soft) && soft.sceneFog);
    args[2].u.codeSampler=static_cast<MaterialTextureSource>(10);
    assert(!WebRenderer_GetSoftParticle(&material,5,soft));
    args[2].u.codeSampler=static_cast<MaterialTextureSource>(18);
    feather[0]=std::numeric_limits<float>::quiet_NaN();
    assert(!WebRenderer_GetSoftParticle(&material,5,soft));
    feather[0]=0.05f; args[4].dest=99;
    assert(!WebRenderer_GetSoftParticle(&material,5,soft));
    args[4].dest=13; ps.name="distortion_zfeather.hlsl";
    assert(!WebRenderer_GetSoftParticle(&material,5,soft));
    ps.name="zfeather.hlsl"; vs.name="unknown_zfeather.hlsl";
    assert(!WebRenderer_GetSoftParticle(&material,5,soft));
    vs.name="zfeather_dtex.hlsl"; tech.passCount=2;
    assert(!WebRenderer_GetSoftParticle(&material,5,soft));
    tech.passCount=1;
    assert(!WebRenderer_GetSoftParticle(nullptr,5,soft));
    assert(!WebRenderer_GetSoftParticle(&material,34,soft));

    GfxStateBits state{{0x18000800u,0xdu}}; material.stateBitsTable=&state; material.stateBitsCount=1;
    material.stateBitsEntry[1]=0; remapped.techniques[1]=&tech;
    vs.name="floatz_build_atest_dtex.hlsl"; ps.name="floatz_build_atest.hlsl";
    args[0].type=3; args[0].dest=20; args[0].u.codeConst.index=54;
    std::uint32_t actual[2]{}; bool alpha=false;
    assert(WebRenderer_GetFloatZ(&material,actual,alpha) && alpha);
    assert(actual[0]==state.loadBits[0] && actual[1]==state.loadBits[1]);
    vs.name=ps.name="floatz_build.hlsl";
    assert(WebRenderer_GetFloatZ(&material,actual,alpha) && !alpha);
    args[0].u.codeConst.index=53;
    assert(!WebRenderer_GetFloatZ(&material,actual,alpha));
    args[0].u.codeConst.index=54; material.stateBitsEntry[1]=255;
    assert(!WebRenderer_GetFloatZ(&material,actual,alpha));
}

void TestCanonicalOutdoorLookupIsCarriedAtomically()
{
    Fixture fixture;
    GfxImage outdoor{};
    outdoor.name = "$outdoor";
    fixture.world.outdoorImage = &outdoor;
    for (unsigned component = 0u; component < 4u; ++component)
        fixture.world.outdoorLookupMatrix[component][component] = 1.0f;
    fixture.world.outdoorLookupMatrix[0][0] = 0.25f;
    fixture.world.outdoorLookupMatrix[3][0] = 0.5f;
    fixture.world.outdoorLookupMatrix[3][2] = -0.125f;

    WebRendererWorldSceneCommand command;
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::Success);
    assert(command.outdoorImage == &outdoor);
    assert(std::memcmp(command.outdoorLookupMatrix,
        fixture.world.outdoorLookupMatrix,
        sizeof(command.outdoorLookupMatrix)) == 0);

    fixture.world.outdoorLookupMatrix[1][2] =
        std::numeric_limits<float>::quiet_NaN();
    const auto before = command.outdoorImage;
    const float beforeScale = command.outdoorLookupMatrix[0][0];
    assert(WebRenderer_BuildWorldSceneCommand(
        fixture.world, MakeView(), command) ==
        WebRendererWorldSceneResult::InvalidWorld);
    assert(command.outdoorImage == before);
    assert(command.outdoorLookupMatrix[0][0] == beforeScale);
}

void TestAuthoredDistortionBindings()
{
    Material material{}; MaterialTechniqueSet set{}, remapped{};
    MaterialTechnique technique{}; MaterialVertexShader vs{}; MaterialPixelShader ps{};
    MaterialShaderArgument args[7]{}; float scale[4]{10, 20, 0, 0}, out[4]{};
    material.techniqueSet = &set; set.remappedTechniqueSet = &remapped;
    remapped.techniques[5] = &technique; technique.passCount = 1; technique.flags = 33;
    vs.name = "distortion_scale_zfeather_dtex.hlsl"; ps.name = "distortion_zfeather.hlsl";
    auto &pass = technique.passArray[0]; pass.vertexShader = &vs; pass.pixelShader = &ps;
    pass.args = args; pass.stableArgCount = 7;
    args[0].type = 2; args[0].dest = 4; args[0].u.nameHash = 0xa0ab1041;
    args[1].type = 4; args[1].dest = 0; args[1].u.codeSampler = static_cast<MaterialTextureSource>(10);
    args[2].type = 4; args[2].dest = 5; args[2].u.codeSampler = static_cast<MaterialTextureSource>(18);
    args[3].type = 3; args[3].dest = 0; args[3].u.codeConst.index = 80;
    args[4].type = 3; args[4].dest = 17; args[4].u.codeConst.index = 51;
    args[5].type = 3; args[5].dest = 18; args[5].u.codeConst.index = 52;
    args[6].type = 1; args[6].dest = 12; args[6].u.literalConst = scale;
    assert(WebRenderer_GetDistortion(&material, 5, out) && out[0] == 10 && out[1] == 20);
    MaterialConstantDef constant{};
    constant.nameHash = 0xf37b6913u; std::copy_n(scale, 4, constant.literal);
    material.constantTable = &constant; material.constantCount = 1;
    args[6].type = 0; args[6].u.nameHash = constant.nameHash;
    assert(WebRenderer_GetDistortion(&material, 5, out) && out[0] == 10 && out[1] == 20);
    args[6].type = 1; args[6].u.literalConst = scale;
    assert(WebRenderer_SkipsDistortion(&material, 5, false));
    assert(!WebRenderer_SkipsDistortion(&material, 5, true));
    for (unsigned i = 0; i < 6; ++i)
    {
        const auto dest = args[i].dest; args[i].dest = 31;
        assert(!WebRenderer_GetDistortion(&material, 5, out)); args[i].dest = dest;
    }
    scale[1] = std::nanf("");
    assert(!WebRenderer_GetDistortion(&material, 5, out) && out[1] == 20);
    scale[1] = -20; // Authored negative scaling is meaningful.
    assert(WebRenderer_GetDistortion(&material, 5, out) && out[1] == -20);
    technique.flags = 32;
    assert(!WebRenderer_GetDistortion(&material, 5, out));
    assert(!WebRenderer_SkipsDistortion(&material, 5, false));
    technique.flags = 33; technique.passCount = 2;
    assert(!WebRenderer_GetDistortion(&material, 5, out));
    technique.passCount = 1; ps.name = "unknown.hlsl";
    assert(!WebRenderer_GetDistortion(&material, 5, out));
    assert(WebRenderer_SkipsDistortion(&material, 5, false));
    assert(!WebRenderer_GetDistortion(nullptr, 5, out));
    assert(!WebRenderer_GetDistortion(&material, 34, out));
}

int main()
{
    TestWorldLightReceiversPreserveNativeBoundsAndCameraSelection();
    TestTechniqueSetFeatureNameRemap();
    TestAuthoredDistortionBindings();
    TestAuthoredSoftParticleBindings();
    TestCommaPrefixedImageReferenceResolvesAtRendererBoundary();
    TestCanonicalOpaqueSurfacesAreBatchedInWorldOrder();
    TestCanonicalOutdoorLookupIsCarriedAtomically();
    TestPrimaryLightIdentitySplitsNativeLitBatches();
    TestSpotPrimaryLightSelectsNativeMaterialTechnique();
    TestPrimaryLightFrameAcceptsScriptedLightState();
    TestMissingSpotTechniqueRetainsNativeSkipIntent();
    TestCanonicalMaterialAndLightmapIdentitySplitBatches();
    TestCanonicalLmTechniqueNameDoesNotInventLightmapSamplers();
    TestRemappedTechniqueSetDrivesPortableSelection();
    TestCinematicCodeImagesSelectCanonicalMaterialWithoutTextureTable();
    TestCanonicalWorldColorAliasUsesLitStateAndLightmaps();
    TestNativePixelShaderFamiliesSelectPortableMaterialTechniques();
    TestDistanceFalloffVertexShaderCarriesCanonicalMaterialConstants();
    TestSunShadowTechniqueAndCanonicalCasterBitsSplitBatches();
    TestShaderModel3SpecularPassCarriesCanonicalInputs();
    TestMalformedLocalIndexIsRejectedAtomically();
    TestNonFiniteWorldVertexIsRejectedAtomically();
    TestSharedMaterialLookupPreservesPrecedenceAndDefaults();
    TestSkyPassIsNotFoldedIntoOpaqueWorldBatch();
    TestSpecialSurfaceInventoryUsesCanonicalMaterialData();
    TestCanonicalWaterPassCarriesSimulationAndReflectionInputs();
    TestConservativeVisibilityIsDisabledForMovingCanonicalView();
    TestCanonicalDpvsRangesOverrideNonContiguousModelCount();
    TestSpotShadowCommandPreservesAuthoredCasterMembership();
    TestDynamicBrushModelUsesCanonicalSurfaceRangeAndPlacement();
    TestRetainedBrushPlacementMatchesExpandedGeometry();
    TestBrushReceiverUsesCanonicalWritableBounds();
    TestSunShadowRangesPreserveTriangleOrderAndCutouts();
    TestWorldSunShadowRangesCullPartitionsIndependently();
    TestTextureParameterMemoPreservesAliasedObjectState();
    TestMalformedDynamicBrushRangeIsRejectedAtomically();
    TestBrushMatchesWorldSelectionAndRejectsAtomically();
    return 0;
}
