#include <gfx_d3d/gfx_world_types.h>
#include <web/web_renderer_world_scene.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace
{
constexpr std::uint32_t TECHNIQUE_LIT_INDEX = 7u;
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

void TestCanonicalMaterialAndLightmapIdentitySplitBatches()
{
    Fixture fixture;
    MaterialTechnique litTechnique{};
    litTechnique.passCount = 1u;
    litTechnique.passArray[0].customSamplerFlags = 2u;
    MaterialTechniqueSet techniqueSet{};
    techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &litTechnique;
    GfxStateBits stateBits[2]{{{0x18008800u, 0x0000000du}},
                              {{0x18004800u, 0x00000002u}}};
    GfxImage baseImageA{};
    GfxImage baseImageB{};
    GfxImage lightmapImage{};
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
    GfxLightmapArray lightmap{&lightmapImage, nullptr};
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
    assert(first.lightmapImage == &lightmapImage);
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

void TestCanonicalLmTechniqueNameSelectsPrimaryLightmap()
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
    assert(command.batches[0].lightmapImage == &lightmapImage);
    assert(command.batches[0].technique ==
        WebRendererWorldTechnique::BaseTextureLightmap);
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
} // namespace

int main()
{
    TestCanonicalOpaqueSurfacesAreBatchedInWorldOrder();
    TestCanonicalMaterialAndLightmapIdentitySplitBatches();
    TestCanonicalLmTechniqueNameSelectsPrimaryLightmap();
    TestMalformedLocalIndexIsRejectedAtomically();
    TestSkyPassIsNotFoldedIntoOpaqueWorldBatch();
    TestConservativeVisibilityIsDisabledForMovingCanonicalView();
    return 0;
}
