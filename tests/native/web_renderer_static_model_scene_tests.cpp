#include <gfx_d3d/gfx_world_types.h>
#include <web/web_renderer_static_model_scene.h>
#include <xanim/xmodel_types.h>
#include <xanim/xsurface_types.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

void __cdecl Vec2UnpackTexCoords(PackedTexCoords in, float *out)
{
    out[0] = static_cast<float>(in.packed & 0xffu) / 255.0f;
    out[1] = static_cast<float>((in.packed >> 8u) & 0xffu) / 255.0f;
}

void __cdecl Vec3UnpackUnitVec(PackedUnitVec, float *out)
{
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 1.0f;
}

namespace
{
constexpr std::uint32_t TECHNIQUE_LIT_INDEX = 7u;
constexpr std::uint32_t TECHNIQUE_LIT_INSTANCED_INDEX = 14u;
constexpr std::uint32_t TECHNIQUE_LIT_INSTANCED_SUN_INDEX = 15u;
constexpr std::uint32_t TECHNIQUE_LIT_INSTANCED_SUN_SHADOW_INDEX = 16u;
Material *g_resolvedMaterial = nullptr;

Material *ResolveMaterial(Material *) noexcept
{
    return g_resolvedMaterial;
}

struct Fixture
{
    std::array<GfxPackedVertex, 3> vertices{};
    std::array<std::uint16_t, 3> indices{0u, 1u, 2u};
    XSurface surface{};
    GfxImage image{};
    MaterialTextureDef texture{};
    MaterialTechnique technique{};
    MaterialTechniqueSet techniqueSet{};
    GfxStateBits stateBits[1]{};
    Material material{};
    Material *materials[1]{&material};
    XModel model{};
    std::array<GfxStaticModelDrawInst, 3> instances{};
    std::array<GfxStaticModelInst, 3> lightingInstances{};
    GfxWorld world{};

    Fixture()
    {
        vertices[0].xyz[0] = 1.0f;
        vertices[1].xyz[1] = 1.0f;
        vertices[2].xyz[2] = 1.0f;
        vertices[0].color.packed = 0xff804020u;
        vertices[1].color.packed = 0xffffffffu;
        vertices[2].color.packed = 0xffffffffu;
        vertices[0].texCoord.packed = 0x00008040u;
        surface.vertCount = static_cast<std::uint16_t>(vertices.size());
        surface.triCount = 1u;
        surface.verts0 = vertices.data();
        surface.triIndices = indices.data();

        image.name = "model-color";
        texture.semantic = 2u;
        texture.samplerState = 0x42u;
        texture.u.image = &image;
        technique.passCount = 1u;
        techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &technique;
        stateBits[0].loadBits[0] = 0x18008800u;
        stateBits[0].loadBits[1] = 0x0000000du;
        material.info.name = "model/material";
        material.info.gameFlags = 0x40u;
        material.textureCount = 1u;
        material.textureTable = &texture;
        material.techniqueSet = &techniqueSet;
        material.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 0u;
        material.stateBitsCount = 1u;
        material.stateBitsTable = stateBits;

        model.name = "model/test";
        model.numsurfs = 1u;
        model.numLods = 1;
        model.surfs = &surface;
        model.materialHandles = materials;
        model.lodInfo[0].numsurfs = 1u;
        model.lodInfo[0].surfIndex = 0u;

        for (std::size_t instance = 0u; instance < instances.size(); ++instance)
        {
            instances[instance].model = &model;
            instances[instance].cullDist = 1000.0f;
            instances[instance].placement.scale = instance == 0u ? 1.0f : 2.0f;
            instances[instance].placement.origin[0] =
                static_cast<float>(instance) * 10.0f;
            for (std::size_t axis = 0u; axis < 3u; ++axis)
                instances[instance].placement.axis[axis][axis] = 1.0f;
            lightingInstances[instance].groundLighting.packed =
                0xff204080u;
        }
        world.dpvs.smodelCount = 2u;
        world.dpvs.smodelDrawInsts = instances.data();
        world.dpvs.smodelInsts = lightingInstances.data();
    }
};

void TestCanonicalInstancesShareOneMaterialSurfaceBatch()
{
    Fixture fixture;
    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.modelCount == 1u);
    assert(command.surfaceCount == 1u);
    assert(command.canonicalInstanceCount == 2u);
    assert(command.vertices.size() == 3u);
    assert(command.indices == std::vector<std::uint32_t>({0u, 1u, 2u}));
    assert(command.instances.size() == 2u);
    assert(command.batches.size() == 1u);

    const WebRendererStaticModelBatchDesc &batch = command.batches[0];
    assert(batch.instanceOffset == 0u && batch.instanceCount == 2u);
    assert(batch.lodIndex == 0u);
    assert(batch.draw.sourceKind == WebRendererSceneBatchKind::StaticXModel);
    assert(batch.draw.materialIdentity == &fixture.material);
    assert(batch.draw.modelIdentity == &fixture.model);
    assert(std::strcmp(batch.draw.materialName, "model/material") == 0);
    assert(std::strcmp(batch.draw.modelName, "model/test") == 0);
    assert(batch.draw.baseImage == &fixture.image);
    assert(batch.draw.samplerState == 0x42u);
    assert(batch.draw.technique == WebRendererWorldTechnique::BaseTexture);
    assert(batch.draw.castsSunShadow);
    assert(batch.draw.lightingMode ==
        WebRendererWorldLightingMode::ModelLightGrid);
    assert(batch.draw.firstInstanceIndex == 0u);
    assert(batch.draw.lastInstanceIndex == 1u);
    assert(command.instances[1].origin[0] == 10.0f);
    assert(command.instances[1].axis[0][0] == 2.0f);
    assert(command.instances[1].modelScale == 2.0f);
    assert(command.instances[1].modelCullDistance == 1000.0f);
    assert(command.instances[1].canonicalInstanceIndex == 1u);
    assert(command.modelLightingAtlas.entryCount == 2u);
    assert(command.instances[0].modelLightingCoordinates[0] !=
        command.instances[1].modelLightingCoordinates[0]);
    assert(command.modelLightingAtlas.pixels[0] == 0x20u);
    assert(command.modelLightingAtlas.pixels[1] == 0x40u);
    assert(command.modelLightingAtlas.pixels[2] == 0x80u);
    assert(std::fabs(command.vertices[0].textureCoordinate[0] -
        64.0f / 255.0f) < 0.0001f);
    assert(command.vertices[0].normal[2] == 1.0f);
}

void TestEveryAuthoredLodIsRetainedForRuntimeSelection()
{
    Fixture fixture;
    std::array<XSurface, 2> surfaces{fixture.surface, fixture.surface};
    Material *materials[2]{&fixture.material, &fixture.material};
    fixture.model.numsurfs = 2u;
    fixture.model.numLods = 2;
    fixture.model.surfs = surfaces.data();
    fixture.model.materialHandles = materials;
    fixture.model.lodInfo[0].surfIndex = 0u;
    fixture.model.lodInfo[0].numsurfs = 1u;
    fixture.model.lodInfo[1].surfIndex = 1u;
    fixture.model.lodInfo[1].numsurfs = 1u;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.modelCount == 1u);
    assert(command.surfaceCount == 2u);
    assert(command.vertices.size() == 6u);
    assert(command.indices.size() == 6u);
    assert(command.instances.size() == 2u);
    assert(command.batches.size() == 2u);
    assert(command.batches[0].lodIndex == 0u);
    assert(command.batches[1].lodIndex == 1u);
    assert(command.batches[0].instanceOffset == 0u);
    assert(command.batches[1].instanceOffset == 0u);
    assert(command.batches[0].instanceCount == 2u);
    assert(command.batches[1].instanceCount == 2u);
}

void TestCanonicalPrimaryLightSplitsStaticInstanceGroups()
{
    Fixture fixture;
    fixture.instances[0].primaryLightIndex = 1u;
    fixture.instances[1].primaryLightIndex = 2u;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 2u);
    assert(command.instances.size() == 2u);
    assert(command.batches[0].instanceCount == 1u);
    assert(command.batches[1].instanceCount == 1u);
    assert(command.batches[0].draw.primaryLightIndex == 1u);
    assert(command.batches[1].draw.primaryLightIndex == 2u);
}

void TestCanonicalInstanceIndicesSurviveRegroupingAndLods()
{
    Fixture fixture;
    fixture.world.dpvs.smodelCount = 3u;
    fixture.instances[0].primaryLightIndex = 1u;
    fixture.instances[1].primaryLightIndex = 2u;
    fixture.instances[2].primaryLightIndex = 1u;
    std::array<XSurface, 2> surfaces{fixture.surface, fixture.surface};
    Material *materials[2]{&fixture.material, &fixture.material};
    fixture.model.numsurfs = 2u;
    fixture.model.numLods = 2;
    fixture.model.surfs = surfaces.data();
    fixture.model.materialHandles = materials;
    fixture.model.lodInfo[1].surfIndex = 1u;
    fixture.model.lodInfo[1].numsurfs = 1u;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.canonicalInstanceCount == 3u);
    assert(command.instances.size() == 3u);
    assert(command.batches.size() == 4u);
    for (const auto &batch : command.batches)
    {
        assert(batch.lodIndex < 2u);
        for (std::uint32_t i = 0u; i < batch.instanceCount; ++i)
        {
            const auto &instance = command.instances[batch.instanceOffset + i];
            const std::uint32_t canonical = instance.canonicalInstanceIndex;
            assert(canonical < fixture.world.dpvs.smodelCount);
            assert(fixture.instances[canonical].primaryLightIndex ==
                batch.draw.primaryLightIndex);
            assert(instance.origin[0] == fixture.instances[canonical].placement.origin[0]);
        }
    }
    // The noncontiguous light-1 group must still address DPVS slots 0 and 2.
    assert(command.instances[0].canonicalInstanceIndex == 0u);
    assert(command.instances[1].canonicalInstanceIndex == 2u);
    assert(command.instances[2].canonicalInstanceIndex == 1u);
}

void TestAmbientProbeShaderIdentitySurvivesPortableBoundary()
{
    Fixture fixture;
    MaterialPixelShader pixelShader{};
    pixelShader.name = "lp_amb_t0c0_sm3.hlsl";
    fixture.technique.passArray[0].pixelShader = &pixelShader;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].draw.ambientProbeLighting);
    assert(std::strcmp(command.batches[0].draw.pixelShaderName,
        "lp_amb_t0c0_sm3.hlsl") == 0);
}

void TestCanonicalStaticModelInstancedTechniqueSelection()
{
    Fixture fixture;
    MaterialTechnique instanced{};
    MaterialTechnique instancedSun{};
    MaterialTechnique instancedSunShadow{};
    MaterialPixelShader ambientPixel{};
    MaterialPixelShader sunPixel{};
    ambientPixel.name = "lp_i_amb_t0c0_sm3.hlsl";
    sunPixel.name = "lp_i_amb_sun_t0c0_sm3.hlsl";
    instanced.name = "lprobe_i_amb_t0c0_dtex_sm3";
    instanced.passCount = 1u;
    instanced.passArray[0].pixelShader = &ambientPixel;
    instancedSun.name = "lprobe_i_amb_sun_t0c0_dtex_sm3";
    instancedSun.passCount = 1u;
    instancedSun.passArray[0].pixelShader = &sunPixel;
    instancedSunShadow = instancedSun;
    fixture.techniqueSet.techniques[TECHNIQUE_LIT_INSTANCED_INDEX] =
        &instanced;
    fixture.techniqueSet.techniques[TECHNIQUE_LIT_INSTANCED_SUN_INDEX] =
        &instancedSun;
    fixture.techniqueSet.techniques[
        TECHNIQUE_LIT_INSTANCED_SUN_SHADOW_INDEX] = &instancedSunShadow;
    fixture.material.stateBitsEntry[TECHNIQUE_LIT_INSTANCED_INDEX] = 0u;
    fixture.material.stateBitsEntry[TECHNIQUE_LIT_INSTANCED_SUN_INDEX] = 0u;
    fixture.material.stateBitsEntry[
        TECHNIQUE_LIT_INSTANCED_SUN_SHADOW_INDEX] = 0u;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].draw.techniqueType ==
        TECHNIQUE_LIT_INSTANCED_INDEX);
    assert(command.batches[0].draw.ambientProbeLighting);

    fixture.world.sunPrimaryLightIndex = 1u;
    fixture.instances[0].primaryLightIndex = 1u;
    fixture.instances[1].primaryLightIndex = 1u;
    command = {};
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].draw.techniqueType ==
        TECHNIQUE_LIT_INSTANCED_SUN_SHADOW_INDEX);
    assert(std::strcmp(command.batches[0].draw.pixelShaderName,
        "lp_i_amb_sun_t0c0_sm3.hlsl") == 0);
    assert(command.batches[0].draw.ambientProbeLighting);
}

void TestNamedDetailMapAndScaleSurvivePortableBoundary()
{
    Fixture fixture;
    GfxImage detailImage{};
    std::array<MaterialTextureDef, 2> textures{};
    textures[0].nameHash = 0xeb529b4du;
    textures[0].semantic = 2u;
    textures[0].samplerState = 0x13u;
    textures[0].u.image = &detailImage;
    textures[1] = fixture.texture;
    textures[1].nameHash = 0xa0ab1041u;
    fixture.material.textureCount = 2u;
    fixture.material.textureTable = textures.data();
    MaterialConstantDef detailScale{};
    detailScale.nameHash = 0x08d36a09u;
    detailScale.literal[0] = 8.0f;
    detailScale.literal[1] = 16.0f;
    fixture.material.constantCount = 1u;
    fixture.material.constantTable = &detailScale;
    MaterialPixelShader pixelShader{};
    pixelShader.name = "lp_r0c0d0_sm3.hlsl";
    fixture.technique.passArray[0].pixelShader = &pixelShader;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    const WebRendererWorldBatchDesc &draw = command.batches[0].draw;
    assert(draw.baseImage == &fixture.image);
    assert(draw.detailImage == &detailImage);
    assert(draw.detailSamplerState == 0x13u);
    assert(draw.detailScale[0] == 8.0f);
    assert(draw.detailScale[1] == 16.0f);
}

void TestLightingAtlasCountsOnlySubmittedCanonicalPlacements()
{
    Fixture fixture;
    fixture.world.dpvs.smodelCount = 3u;
    fixture.instances[2].model = nullptr;
    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.canonicalInstanceCount == 3u);
    assert(command.instances.size() == 2u);
    assert(command.modelLightingFailureCount == 0u);
    assert(command.modelLightingAtlas.entryCount == 2u);
    assert(command.batches[0].draw.lightingMode ==
        WebRendererWorldLightingMode::ModelLightGrid);
}

void TestNativeStaticModelCardinalityIsAccepted()
{
    Fixture fixture;
    std::vector<GfxStaticModelDrawInst> instances(
        WEB_RENDERER_MAX_STATIC_MODEL_INSTANCES, fixture.instances[0]);
    fixture.world.dpvs.smodelCount = static_cast<std::uint32_t>(
        instances.size());
    fixture.world.dpvs.smodelDrawInsts = instances.data();
    fixture.world.dpvs.smodelInsts = nullptr;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.canonicalInstanceCount ==
        WEB_RENDERER_MAX_STATIC_MODEL_INSTANCES);
    assert(command.instances.size() ==
        WEB_RENDERER_MAX_STATIC_MODEL_INSTANCES);
    assert(command.instances.back().canonicalInstanceIndex == 65'535u);
}

void TestMaterialReferencesResolveAtRendererEvaluation()
{
    Fixture fixture;
    Material reference{};
    reference.info.name = ",model/material";
    fixture.materials[0] = &reference;
    g_resolvedMaterial = &fixture.material;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(
        fixture.world, command, nullptr, ResolveMaterial) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].draw.materialIdentity == &fixture.material);
    assert(command.batches[0].draw.baseImage == &fixture.image);
    assert(command.batches[0].draw.technique ==
        WebRendererWorldTechnique::BaseTexture);

    g_resolvedMaterial = nullptr;
}

void TestCameraRegionThreeRemainsShadowOnly()
{
    Fixture fixture;
    fixture.material.cameraRegion = 3u;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 1u);
    const WebRendererWorldBatchDesc &draw = command.batches[0].draw;
    assert(draw.cameraRegion == 3u);
    assert(!WebRenderer_IsCameraVisibleXModelSurface(
        draw.sourceKind, draw.cameraRegion));
    assert(WebRenderer_IsCameraVisibleXModelSurface(
        WebRendererSceneBatchKind::StaticXModel, 2u));
    assert(WebRenderer_IsCameraVisibleXModelSurface(
        WebRendererSceneBatchKind::FxXModel, 3u));
    // The batch is deliberately retained so the backend's shadow partition
    // can still submit its canonical build-shadowmap technique.
    assert(draw.castsSunShadow);
}

void TestSm3SpecularRetainsCanonicalProbeAndSplitsProbeGroups()
{
    Fixture fixture;
    GfxImage normalImage{};
    GfxImage specularImage{};
    GfxImage probeImages[2]{};
    std::array<MaterialTextureDef, 3> textures{};
    textures[0] = fixture.texture;
    textures[1].semantic = 5u;
    textures[1].samplerState = 0x0bu;
    textures[1].u.image = &normalImage;
    textures[2].semantic = 8u;
    textures[2].samplerState = 0x2bu;
    textures[2].u.image = &specularImage;
    fixture.material.textureCount =
        static_cast<std::uint8_t>(textures.size());
    fixture.material.textureTable = textures.data();

    MaterialConstantDef envMapParms{};
    envMapParms.nameHash = 0x3d9994dcu;
    envMapParms.literal[0] = 0.4f;
    envMapParms.literal[1] = 10.0f;
    envMapParms.literal[2] = 2.5f;
    envMapParms.literal[3] = 0.625f;
    fixture.material.constantCount = 1u;
    fixture.material.constantTable = &envMapParms;

    std::uint32_t shaderWords[2]{0xffff0300u, 0x0000ffffu};
    MaterialPixelShader pixelShader{};
    pixelShader.name = "lp_r0c0n0s0_sm3.hlsl";
    pixelShader.prog.loadDef.program = shaderWords;
    pixelShader.prog.loadDef.programSize = 2u;
    fixture.technique.name = "lp_r0c0n0s0_dtex_sm3";
    fixture.technique.flags = 0x1234u;
    fixture.technique.passArray[0].pixelShader = &pixelShader;
    fixture.technique.passArray[0].customSamplerFlags = 1u;

    GfxReflectionProbe probes[2]{};
    probes[0].reflectionImage = &probeImages[0];
    probes[1].reflectionImage = &probeImages[1];
    fixture.world.reflectionProbeCount = 2u;
    fixture.world.reflectionProbes = probes;
    fixture.instances[0].reflectionProbeIndex = 0u;
    fixture.instances[1].reflectionProbeIndex = 1u;

    WebRendererStaticModelSceneCommand command;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::Success);
    assert(command.batches.size() == 2u);
    assert(command.instances.size() == 2u);
    assert(command.vertices.size() == 3u);
    assert(command.indices.size() == 3u);
    assert(command.batches[0].draw.firstIndex == 0u);
    assert(command.batches[1].draw.firstIndex == 0u);
    for (std::size_t index = 0u; index < command.batches.size(); ++index)
    {
        const WebRendererWorldBatchDesc &draw = command.batches[index].draw;
        assert(draw.technique ==
            WebRendererWorldTechnique::BaseTextureNormalSpecular);
        assert(draw.normalImage == &normalImage);
        assert(draw.specularImage == &specularImage);
        assert(draw.specularSamplerState == 0x2bu);
        assert(draw.reflectionProbeIndex == index);
        assert(draw.reflectionProbeImage == &probeImages[index]);
        assert(draw.envMapParms[0] == 0.4f);
        assert(draw.envMapParms[2] == 2.5f);
        assert(draw.techniqueFlags == 0x1234u);
        assert(draw.customSamplerFlags == 1u);
        assert(std::strcmp(draw.pixelShaderName,
            "lp_r0c0n0s0_sm3.hlsl") == 0);
        assert(draw.pixelShaderProgramHash != 0u);
        assert(WebRenderer_UsesModelEnvironmentSpecular(draw.technique));
    }
}

void TestMalformedIndexAndPlacementFailAtomically()
{
    Fixture fixture;
    WebRendererStaticModelSceneCommand command;
    command.modelCount = 99u;
    fixture.indices[2] = 3u;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::IndexOutOfRange);
    assert(command.modelCount == 99u);

    fixture.indices[2] = 2u;
    fixture.instances[0].placement.scale = 0.0f;
    assert(WebRenderer_BuildStaticModelSceneCommand(fixture.world, command) ==
        WebRendererStaticModelSceneResult::InvalidPlacement);
    assert(command.modelCount == 99u);
}
} // namespace

int main()
{
    TestCanonicalInstancesShareOneMaterialSurfaceBatch();
    TestEveryAuthoredLodIsRetainedForRuntimeSelection();
    TestCanonicalPrimaryLightSplitsStaticInstanceGroups();
    TestCanonicalInstanceIndicesSurviveRegroupingAndLods();
    TestAmbientProbeShaderIdentitySurvivesPortableBoundary();
    TestCanonicalStaticModelInstancedTechniqueSelection();
    TestNamedDetailMapAndScaleSurvivePortableBoundary();
    TestLightingAtlasCountsOnlySubmittedCanonicalPlacements();
    TestNativeStaticModelCardinalityIsAccepted();
    TestMaterialReferencesResolveAtRendererEvaluation();
    TestCameraRegionThreeRemainsShadowOnly();
    TestSm3SpecularRetainsCanonicalProbeAndSplitsProbeGroups();
    TestMalformedIndexAndPlacementFailAtomically();
    return 0;
}
