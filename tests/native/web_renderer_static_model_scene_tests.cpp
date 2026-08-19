#include <gfx_d3d/gfx_world_types.h>
#include <web/web_renderer_static_model_scene.h>
#include <xanim/xmodel_types.h>
#include <xanim/xsurface_types.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>

void __cdecl Vec2UnpackTexCoords(PackedTexCoords in, float *out)
{
    out[0] = static_cast<float>(in.packed & 0xffu) / 255.0f;
    out[1] = static_cast<float>((in.packed >> 8u) & 0xffu) / 255.0f;
}

namespace
{
constexpr std::uint32_t TECHNIQUE_LIT_INDEX = 7u;

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
    std::array<GfxStaticModelDrawInst, 2> instances{};
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
            instances[instance].placement.scale = instance == 0u ? 1.0f : 2.0f;
            instances[instance].placement.origin[0] =
                static_cast<float>(instance) * 10.0f;
            for (std::size_t axis = 0u; axis < 3u; ++axis)
                instances[instance].placement.axis[axis][axis] = 1.0f;
        }
        world.dpvs.smodelCount = static_cast<std::uint32_t>(instances.size());
        world.dpvs.smodelDrawInsts = instances.data();
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
    assert(batch.draw.sourceKind == WebRendererSceneBatchKind::StaticXModel);
    assert(batch.draw.materialIdentity == &fixture.material);
    assert(batch.draw.modelIdentity == &fixture.model);
    assert(std::strcmp(batch.draw.materialName, "model/material") == 0);
    assert(std::strcmp(batch.draw.modelName, "model/test") == 0);
    assert(batch.draw.baseImage == &fixture.image);
    assert(batch.draw.samplerState == 0x42u);
    assert(batch.draw.technique == WebRendererWorldTechnique::BaseTexture);
    assert(batch.draw.firstInstanceIndex == 0u);
    assert(batch.draw.lastInstanceIndex == 1u);
    assert(command.instances[1].origin[0] == 10.0f);
    assert(command.instances[1].axis[0][0] == 2.0f);
    assert(command.instances[1].canonicalInstanceIndex == 1u);
    assert(std::fabs(command.vertices[0].textureCoordinate[0] -
        64.0f / 255.0f) < 0.0001f);
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
    TestMalformedIndexAndPlacementFailAtomically();
    return 0;
}
