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

void __cdecl Vec3UnpackUnitVec(PackedUnitVec, float *out)
{
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 1.0f;
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
Material *g_resolvedMaterial = nullptr;

Material *ResolveMaterial(Material *) noexcept
{
    return g_resolvedMaterial;
}

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
    assert(command.vertices[0].color[0] == 0x40 / 255.0f);
    assert(command.vertices[0].color[3] == 0x80 / 255.0f);
    assert(std::fabs(command.vertices[0].textureCoordinate[0] -
        0x40 / 255.0f) < 0.00001f);
    assert(command.batches[0].firstSurfaceIndex == 1u);
    assert(command.batches[0].materialIdentity == &fixture.material);
    assert(command.batches[0].baseImage == &fixture.image);
    assert(command.batches[0].samplerState == 0x42u);
    assert(command.batches[0].stateBits[0] == 0x18008800u);
    assert(command.batches[0].sourceKind == WebRendererSceneBatchKind::FxXModel);
    assert(!command.batches[0].castsSunShadow);
    assert(command.batches[0].technique == WebRendererWorldTechnique::BaseTexture);
    assert(command.vertices[0].normal[2] == 1.0f);

    WebRendererFxModelSubmission dynamicSubmission = submission;
    dynamicSubmission.sourceKind =
        WebRendererSceneBatchKind::DynamicXModel;
    fixture.material.info.gameFlags = 0x40u;
    dynamicSubmission.modelLightingEnabled = true;
    dynamicSubmission.modelLightingCoordinates[0] = 0.25f;
    dynamicSubmission.modelLightingCoordinates[1] = 0.5f;
    dynamicSubmission.modelLightingCoordinates[2] = 0.75f;
    command = {};
    assert(WebRenderer_BuildFxModelSceneCommand(
        &dynamicSubmission, 1u, command) ==
        WebRendererFxModelSceneResult::Success);
    assert(command.batches[0].sourceKind ==
        WebRendererSceneBatchKind::DynamicXModel);
    assert(command.batches[0].castsSunShadow);
    assert(command.batches[0].lightingMode ==
        WebRendererWorldLightingMode::ModelLightGrid);
    assert(command.batches[0].modelLightingCoordinates[0] == 0.25f);
    assert(command.batches[0].modelLightingCoordinates[1] == 0.5f);
    assert(command.batches[0].modelLightingCoordinates[2] == 0.75f);

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

void TestCanonicalMaterialResolutionAndTechniqueRemap()
{
    Fixture source;
    Fixture canonical;
    canonical.material.info.name = "canonical/dynent-material";
    canonical.image.name = "canonical/dynent-image";
    const WebRendererFxModelSubmission submission = Submission(source);
    WebRendererFxModelSceneCommand command;

    g_resolvedMaterial = &canonical.material;
    assert(WebRenderer_BuildFxModelSceneCommand(
        &submission, 1u, command, nullptr, ResolveMaterial) ==
        WebRendererFxModelSceneResult::Success);
    assert(command.batches[0].materialIdentity == &canonical.material);
    assert(command.batches[0].baseImage == &canonical.image);
    assert(command.batches[0].technique ==
        WebRendererWorldTechnique::BaseTexture);
    g_resolvedMaterial = nullptr;

    source.techniqueSet.techniques[TECHNIQUE_LIT_INDEX] = nullptr;
    source.techniqueSet.remappedTechniqueSet = &canonical.techniqueSet;
    command = {};
    assert(WebRenderer_BuildFxModelSceneCommand(
        &submission, 1u, command) ==
        WebRendererFxModelSceneResult::Success);
    assert(command.batches[0].technique ==
        WebRendererWorldTechnique::BaseTexture);
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
    viewOrigin[0] = 19.0f;
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
    std::uint32_t dropped = 0u;
    assert(WebRenderer_BuildFxModelSceneCommand(&invalid, 1u, command,
        &dropped) ==
        WebRendererFxModelSceneResult::NoFxModel);
    assert(dropped == 1u);
    assert(command.vertices.size() == originalVertices.size());
    assert(std::memcmp(command.vertices.data(), originalVertices.data(),
        command.vertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);
    assert(command.indices == originalIndices);
    assert(command.batches.size() == originalBatches.size());

    invalid = valid;
    fixture.indices[1] = 99u;
    dropped = 0u;
    assert(WebRenderer_BuildFxModelSceneCommand(&invalid, 1u, command,
        &dropped) ==
        WebRendererFxModelSceneResult::NoFxModel);
    assert(command.vertices.size() == originalVertices.size());
    assert(std::memcmp(command.vertices.data(), originalVertices.data(),
        command.vertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);
    assert(command.indices == originalIndices);

    fixture.indices[1] = 2u;
    fixture.surfaces[0].deformed = true;
    dropped = 0u;
    assert(WebRenderer_BuildFxModelSceneCommand(&valid, 1u, command,
        &dropped) ==
        WebRendererFxModelSceneResult::NoFxModel);
    assert(dropped == 1u);
    assert(command.vertices.size() == originalVertices.size());
    assert(std::memcmp(command.vertices.data(), originalVertices.data(),
        command.vertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);

    Fixture twoSurface;
    Fixture validLater;
    twoSurface.model.lodInfo[0].numsurfs = 2u;
    twoSurface.model.lodInfo[0].surfIndex = 0u;
    twoSurface.surfaces[1].deformed = true;
    const WebRendererFxModelSubmission mixed[2] = {
        Submission(twoSurface), Submission(validLater)};
    dropped = 0u;
    assert(WebRenderer_BuildFxModelSceneCommand(mixed, 2u, command,
        &dropped) == WebRendererFxModelSceneResult::Success);
    assert(dropped == 1u);
    assert(command.modelCount == 1u);
    assert(command.surfaceCount == 1u);
    assert(command.batches.size() == 1u);
    assert(command.batches[0].modelIdentity == &validLater.model);
}

void TestRetainCopyOverflowAndClear()
{
    Fixture fixture;
    std::array<WebRendererFxModelSubmission,
        WEB_RENDERER_MAX_FX_MODEL_SUBMISSIONS> storage{};
    std::uint32_t count = 0u;
    GfxScaledPlacement placement{};
    placement.base.quat[3] = 1.0f;
    placement.base.origin[0] = 7.0f;
    placement.scale = 1.0f;
    assert(WebRenderer_RetainFxModelSubmission(storage.data(), &count,
        &fixture.model, &placement, 1u) ==
        WebRendererFxModelRetainResult::Accepted);
    placement.base.origin[0] = 99.0f;
    assert(count == 1u);
    assert(storage[0].placement.base.origin[0] == 7.0f);
    placement.base.quat[3] = 0.0f;
    assert(!WebRenderer_FxModelPlacementIsValid(placement));
    assert(WebRenderer_RetainFxModelSubmission(storage.data(), &count,
        &fixture.model, &placement, 0u) ==
        WebRendererFxModelRetainResult::InvalidSubmission);
    assert(count == 1u);
    placement.base.quat[3] = 1.0f;
    count = WEB_RENDERER_MAX_FX_MODEL_SUBMISSIONS;
    assert(WebRenderer_RetainFxModelSubmission(storage.data(), &count,
        &fixture.model, &placement, 0u) ==
        WebRendererFxModelRetainResult::LimitReached);
    assert(count == WEB_RENDERER_MAX_FX_MODEL_SUBMISSIONS);
    WebRenderer_ClearFxModelSubmissions(&count);
    assert(count == 0u);
}

void TestAtomicCompositionAndOutputLimits()
{
    Fixture fixture;
    const WebRendererFxModelSubmission submissions[2] = {
        Submission(fixture), Submission(fixture, 1u)};
    WebRendererFxModelSceneCommand fx;
    const auto buildResult = WebRenderer_BuildFxModelSceneCommand(
        submissions, 2u, fx);
    assert(buildResult == WebRendererFxModelSceneResult::Success);

    std::vector<WebRendererSurfaceVertex> vertices(1u);
    std::vector<std::uint32_t> indices{0u};
    WebRendererWorldBatchDesc dobjBatch{};
    dobjBatch.firstIndex = 0u;
    dobjBatch.indexCount = 1u;
    dobjBatch.sourceKind = WebRendererSceneBatchKind::DynamicDObj;
    std::vector<WebRendererWorldBatchDesc> batches{dobjBatch};
    std::uint32_t surfaceCount = 1u;
    const auto appendResult = WebRenderer_AppendFxModelSceneCommand(
        fx, vertices, indices, batches, surfaceCount);
    assert(appendResult == WebRendererFxModelAppendResult::Success);
    assert(vertices.size() == 7u);
    assert(indices.front() == 0u);
    assert(indices[1] == 1u);
    assert(batches.front().sourceKind == WebRendererSceneBatchKind::DynamicDObj);
    assert(batches[1].firstIndex == 1u);
    assert(batches[2].firstIndex == 4u);
    assert(batches[1].sourceKind == WebRendererSceneBatchKind::FxXModel);
    assert(batches[2].sourceKind == WebRendererSceneBatchKind::FxXModel);
    assert(surfaceCount == 3u);
    assert(WebRenderer_ValidateFxModelAdmissionCounts(
        1u, 1u, 1u, 1u, 6u, 6u, 2u, 2u, 3u, 3u, 1u, 1u) ==
        WebRendererFxModelAppendResult::Success);
    vertices.resize(10u);
    const std::size_t codeMeshIndexBase = indices.size();
    indices.insert(indices.end(), {7u, 8u, 9u});
    WebRendererWorldBatchDesc codeMeshBatch{};
    codeMeshBatch.firstIndex = static_cast<std::uint32_t>(codeMeshIndexBase);
    codeMeshBatch.indexCount = 3u;
    codeMeshBatch.sourceKind = WebRendererSceneBatchKind::FxCodeMesh;
    batches.push_back(codeMeshBatch);
    ++surfaceCount;
    assert(codeMeshIndexBase == 7u);
    assert(batches.size() == 4u);
    assert(batches[3].firstIndex == 7u);
    assert(batches[3].sourceKind == WebRendererSceneBatchKind::FxCodeMesh);
    assert(indices[7] == 7u && indices[8] == 8u && indices[9] == 9u);
    assert(surfaceCount == 4u);

    assert(WebRenderer_ValidateFxModelAdmissionCounts(
        WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES - 3u, 0u, 0u, 0u,
        2u, 0u, 0u, 0u, 2u, 0u, 0u, 0u) ==
        WebRendererFxModelAppendResult::OutputTooLarge);

    const auto originalVertices = vertices;
    const auto originalIndices = indices;
    const auto originalBatches = batches;
    const std::uint32_t originalSurfaceCount = surfaceCount;
    WebRendererFxModelSceneCommand invalid = fx;
    invalid.indices[0] = 999u;
    assert(WebRenderer_AppendFxModelSceneCommand(invalid, vertices, indices,
        batches, surfaceCount) == WebRendererFxModelAppendResult::InvalidCommand);
    assert(vertices.size() == originalVertices.size());
    assert(indices == originalIndices);
    assert(batches.size() == originalBatches.size());
    assert(surfaceCount == originalSurfaceCount);

    assert(WebRenderer_ValidateFxModelAppendCounts(
        WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES + 1u, 0u, 0u, 0u,
        vertices.size(), indices.size(), batches.size(), surfaceCount) ==
        WebRendererFxModelAppendResult::OutputTooLarge);
    assert(WebRenderer_ValidateFxModelAppendCounts(
        0u, 0u, 0u, 1u, WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES,
        indices.size(), batches.size(), UINT32_MAX) ==
        WebRendererFxModelAppendResult::OutputTooLarge);
    assert(vertices.size() == originalVertices.size());
    assert(indices == originalIndices);
    assert(batches.size() == originalBatches.size());
    assert(surfaceCount == originalSurfaceCount);
}
}

int main()
{
    assert(WebRenderer_IsFxVertexColorBatch(
        WebRendererSceneBatchKind::FxCodeMesh));
    assert(WebRenderer_IsFxVertexColorBatch(
        WebRendererSceneBatchKind::FxXModel));
    assert(WebRenderer_IsFxVertexColorBatch(
        WebRendererSceneBatchKind::FxParticleCloud));
    assert(!WebRenderer_IsFxVertexColorBatch(
        WebRendererSceneBatchKind::DynamicDObj));
    assert(!WebRenderer_IsFxVertexColorBatch(
        WebRendererSceneBatchKind::WorldSurface));
    TestIdentityAndCanonicalSurfaceData();
    TestCanonicalMaterialResolutionAndTechniqueRemap();
    TestDeterministicDistanceLodSelection();
    TestQuaternionScaleOriginAndOrder();
    TestFailureLeavesDestinationUntouched();
    TestRetainCopyOverflowAndClear();
    TestAtomicCompositionAndOutputLimits();
    return 0;
}
