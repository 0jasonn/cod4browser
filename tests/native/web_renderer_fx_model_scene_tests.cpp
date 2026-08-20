#include <gfx_d3d/material_types.h>
#include <gfx_d3d/gfx_image_types.h>
#include <ui/keycodes.h>
#include <web/web_renderer_fx_model_scene.h>
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

int __cdecl XModelGetLodForDist(const XModel *model, float distance)
{
    for (int lod = 0; lod < model->numLods; ++lod)
    {
        if (model->lodInfo[lod].dist == 0.0f ||
            model->lodInfo[lod].dist > distance)
        {
            return lod;
        }
    }
    return -1;
}

namespace
{
constexpr std::uint32_t TECHNIQUE_LIT_INDEX = 7u;

struct Fixture
{
    std::array<GfxPackedVertex, 3> vertices{};
    std::array<std::uint16_t, 3> indices{0u, 2u, 1u};
    std::array<XSurface, 2> surfaces{};
    GfxImage image{};
    MaterialTextureDef texture{};
    MaterialTechnique technique{};
    MaterialTechniqueSet techniqueSet{};
    GfxStateBits stateBits[1]{};
    Material material{};
    Material *materials[2]{&material, &material};
    XModel model{};

    Fixture()
    {
        vertices[0].xyz[0] = 1.0f;
        vertices[1].xyz[1] = 1.0f;
        vertices[2].xyz[2] = 1.0f;
        vertices[0].color.packed = 0x80402010u;
        vertices[1].color.packed = 0xffaabbccu;
        vertices[2].color.packed = 0xffffffffu;
        vertices[0].texCoord.packed = 0x00008040u;
        for (XSurface &surface : surfaces)
        {
            surface.vertCount = static_cast<std::uint16_t>(vertices.size());
            surface.triCount = 1u;
            surface.verts0 = vertices.data();
            surface.triIndices = indices.data();
        }

        image.name = "fx-model-image";
        texture.semantic = 2u;
        texture.samplerState = 0x42u;
        texture.u.image = &image;
        technique.passCount = 1u;
        techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = &technique;
        stateBits[0].loadBits[0] = 0x18008800u;
        stateBits[0].loadBits[1] = 0x0000000du;
        material.info.name = "fx/model-material";
        material.textureCount = 1u;
        material.textureTable = &texture;
        material.techniqueSet = &techniqueSet;
        material.stateBitsEntry[TECHNIQUE_LIT_INDEX] = 0u;
        material.stateBitsCount = 1u;
        material.stateBitsTable = stateBits;

        model.name = "fx/model";
        model.numsurfs = static_cast<std::uint8_t>(surfaces.size());
        model.numLods = 2;
        model.surfs = surfaces.data();
        model.materialHandles = materials;
        model.lodInfo[0].numsurfs = 1u;
        model.lodInfo[0].surfIndex = 0u;
        model.lodInfo[1].numsurfs = 1u;
        model.lodInfo[1].surfIndex = 1u;
    }
};

WebRendererFxModelSubmission Submission(
    const Fixture &fixture, std::uint16_t lod = 0u)
{
    WebRendererFxModelSubmission submission;
    submission.model = &fixture.model;
    submission.lod = lod;
    submission.placement.base.quat[3] = 1.0f;
    submission.placement.scale = 1.0f;
    return submission;
}

void TestIdentityAndCanonicalSurfaceData()
{
    Fixture fixture;
    const WebRendererFxModelSubmission submission = Submission(fixture, 1u);
    WebRendererFxModelSceneCommand command;
    assert(WebRenderer_BuildFxModelSceneCommand(&submission, 1u, command) ==
        WebRendererFxModelSceneResult::Success);
    assert(command.modelCount == 1u);
    assert(command.surfaceCount == 1u);
    assert(command.vertices.size() == 3u);
    assert(command.indices == std::vector<std::uint32_t>({0u, 2u, 1u}));
    assert(command.vertices[0].position[0] == 1.0f);
    assert(command.vertices[0].color[0] == 0x20 / 255.0f);
    assert(command.vertices[0].color[3] == 0x80 / 255.0f);
    assert(std::fabs(command.vertices[0].textureCoordinate[0] -
        0x40 / 255.0f) < 0.00001f);
    assert(command.batches[0].firstSurfaceIndex == 1u);
    assert(command.batches[0].materialIdentity == &fixture.material);
    assert(command.batches[0].baseImage == &fixture.image);
    assert(command.batches[0].samplerState == 0x42u);
    assert(command.batches[0].stateBits[0] == 0x18008800u);
    assert(command.batches[0].sourceKind == WebRendererSceneBatchKind::FxXModel);
    assert(command.batches[0].technique == WebRendererWorldTechnique::BaseTexture);

    fixture.material.textureTable = nullptr;
    fixture.material.textureCount = 0u;
    command = {};
    assert(WebRenderer_BuildFxModelSceneCommand(&submission, 1u, command) ==
        WebRendererFxModelSceneResult::Success);
    assert(command.batches[0].baseImage == nullptr);
    assert(command.batches[0].sourceKind == WebRendererSceneBatchKind::FxXModel);
    assert(command.batches[0].technique ==
        WebRendererWorldTechnique::BackendFallback);
}

void TestDeterministicDistanceLodSelection()
{
    Fixture fixture;
    fixture.model.lodInfo[0].dist = 10.0f;
    fixture.model.lodInfo[1].dist = 0.0f;
    GfxScaledPlacement placement{};
    placement.base.quat[3] = 1.0f;
    placement.scale = 1.0f;
    float viewOrigin[3] = {5.0f, 0.0f, 0.0f};
    assert(WebRenderer_SelectFxModelLod(&fixture.model, placement,
        viewOrigin) == 0);
    viewOrigin[0] = 20.0f;
    assert(WebRenderer_SelectFxModelLod(&fixture.model, placement,
        viewOrigin) == 1);
    placement.scale = 2.0f;
    assert(WebRenderer_SelectFxModelLod(&fixture.model, placement,
        viewOrigin) == 0);
}

void TestQuaternionScaleOriginAndOrder()
{
    Fixture fixture;
    WebRendererFxModelSubmission submissions[2] = {
        Submission(fixture), Submission(fixture)};
    submissions[0].placement.base.origin[0] = 10.0f;
    submissions[0].placement.base.origin[1] = 20.0f;
    submissions[0].placement.base.origin[2] = 30.0f;
    submissions[0].placement.scale = 2.0f;
    const float halfRoot = std::sqrt(0.5f);
    submissions[0].placement.base.quat[2] = halfRoot;
    submissions[0].placement.base.quat[3] = halfRoot;
    submissions[1].placement.base.origin[0] = 50.0f;

    WebRendererFxModelSceneCommand command;
    assert(WebRenderer_BuildFxModelSceneCommand(submissions, 2u, command) ==
        WebRendererFxModelSceneResult::Success);
    assert(command.modelCount == 2u);
    assert(command.batches.size() == 2u);
    assert(command.batches[0].firstIndex == 0u);
    assert(command.batches[1].firstIndex == 3u);
    // Kisak's row-major UnitQuatToAxis maps local +X to +Y for this rotation.
    assert(std::fabs(command.vertices[0].position[0] - 10.0f) < 0.0001f);
    assert(std::fabs(command.vertices[0].position[1] - 22.0f) < 0.0001f);
    assert(std::fabs(command.vertices[0].position[2] - 30.0f) < 0.0001f);
    assert(std::fabs(command.vertices[3].position[0] - 51.0f) < 0.0001f);
}

void TestFailureLeavesDestinationUntouched()
{
    Fixture fixture;
    WebRendererFxModelSceneCommand command;
    const WebRendererFxModelSubmission valid = Submission(fixture);
    assert(WebRenderer_BuildFxModelSceneCommand(&valid, 1u, command) ==
        WebRendererFxModelSceneResult::Success);
    const auto originalVertices = command.vertices;
    const auto originalIndices = command.indices;
    const auto originalBatches = command.batches;

    WebRendererFxModelSubmission invalid = valid;
    invalid.placement.scale = 0.0f;
    assert(WebRenderer_BuildFxModelSceneCommand(&invalid, 1u, command) ==
        WebRendererFxModelSceneResult::InvalidPlacement);
    assert(command.vertices.size() == originalVertices.size());
    assert(std::memcmp(command.vertices.data(), originalVertices.data(),
        command.vertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);
    assert(command.indices == originalIndices);
    assert(command.batches.size() == originalBatches.size());

    invalid = valid;
    fixture.indices[1] = 99u;
    assert(WebRenderer_BuildFxModelSceneCommand(&invalid, 1u, command) ==
        WebRendererFxModelSceneResult::IndexOutOfRange);
    assert(command.vertices.size() == originalVertices.size());
    assert(std::memcmp(command.vertices.data(), originalVertices.data(),
        command.vertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);
    assert(command.indices == originalIndices);

    fixture.surfaces[0].deformed = true;
    assert(WebRenderer_BuildFxModelSceneCommand(&valid, 1u, command) ==
        WebRendererFxModelSceneResult::UnsupportedSurface);
    assert(command.vertices.size() == originalVertices.size());
    assert(std::memcmp(command.vertices.data(), originalVertices.data(),
        command.vertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);
}
}

int main()
{
    TestIdentityAndCanonicalSurfaceData();
    TestDeterministicDistanceLodSelection();
    TestQuaternionScaleOriginAndOrder();
    TestFailureLeavesDestinationUntouched();
    return 0;
}
