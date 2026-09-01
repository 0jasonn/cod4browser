#include <web/web_renderer_dobj_scene.h>
#include <web/web_renderer_draw_state.h>
#include <gfx_d3d/material_types.h>
#include <universal/q_shared.h>
#include <xanim/xmodel.h>
#include <cgame/cg_pose.h>
#include <xanim/xsurface_types.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace
{
float g_lodDistance = -1.0f;
int g_canonicalLod = 2;
Material *g_resolvedMaterial = nullptr;

// Synthetic, repository-authored geometry. Runtime pose and packing are
// stubbed below; the actual builder, validation and publication run here.
void TestDObjEmissionAndAtomicFailure()
{
    std::array<GfxPackedVertex, 3> vertices{};
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        vertices[i].xyz[0] = static_cast<float>(i);
        vertices[i].color.packed = 0x80402010u;
        vertices[i].texCoord.packed = 0x0201u;
        vertices[i].binormalSign = -1.0f;
    }
    std::uint16_t indices[]{0, 2, 1};
    XRigidVertList rigid{};
    rigid.vertCount = 3;
    std::array<XSurface, 2> surfaces{};
    for (XSurface &surface : surfaces)
    {
        surface.vertCount = 3;
        surface.triCount = 1;
        surface.verts0 = vertices.data();
        surface.triIndices = indices;
        surface.vertList = &rigid;
        surface.vertListCount = 1;
        surface.partBits[0] = static_cast<int>(0x80000000u);
    }
    Material *materials[]{nullptr, nullptr};
    DObjAnimMat base{};
    XModel model{};
    model.numBones = 1;
    model.numLods = 1;
    model.numsurfs = 2;
    model.lodInfo[0].numsurfs = 2;
    model.surfs = surfaces.data();
    model.materialHandles = materials;
    model.baseMat = &base;
    XModel *models[]{&model};
    DObj_s obj{};
    obj.numModels = 1;
    obj.numBones = 1;
    obj.models = models;
    obj.skel.mat = &base;
    cpose_t pose{};
    WebRendererDObjSubmission submission{&obj, &pose, 17u, 2u};
    WebRendererDObjSceneCommand command;
    WebRendererLodParms lodParms;
    assert(WebRenderer_BuildLodParms(pose.origin, 1.0f, 1.0f, 0.0f,
        1.0f, 0.0f, lodParms));
    g_canonicalLod = 0;
    assert(WebRenderer_BuildDObjSceneCommand(&submission, 1, command, &lodParms) ==
        WebRendererDObjSceneResult::Success);
    assert(command.vertices.size() == 6 && command.indices.size() == 6);
    assert(command.batches.size() == 2 && command.surfaceCount == 2);
    assert(command.dobjCount == 1 && command.modelCount == 1);
    for (std::size_t i = 0; i < command.vertices.size(); ++i)
    {
        const auto &v = command.vertices[i];
        assert(v.position[0] == static_cast<float>(i % 3));
        assert(v.position[1] == 0.0f && v.position[2] == 0.0f);
        assert(v.normal[2] == 1.0f && v.tangent[2] == 1.0f);
        assert(v.normal[0] == 0.0f && v.normal[1] == 0.0f);
        assert(v.tangent[0] == 0.0f && v.tangent[1] == 0.0f);
        assert(v.binormalSign == -1.0f);
        assert(v.lightmapCoordinate[0] == 0.0f && v.lightmapCoordinate[1] == 0.0f);
        assert(v.textureCoordinate[0] == 1.0f && v.textureCoordinate[1] == 2.0f);
        assert(std::fabs(v.color[0] - 64.0f / 255.0f) < 0.000001f);
        assert(std::fabs(v.color[1] - 32.0f / 255.0f) < 0.000001f);
        assert(std::fabs(v.color[2] - 16.0f / 255.0f) < 0.000001f);
        assert(std::fabs(v.color[3] - 128.0f / 255.0f) < 0.000001f);
        assert(command.indices[i] == indices[i % 3] + (i / 3) * 3);
    }
    assert(command.batches[0].firstIndex == 0 && command.batches[1].firstIndex == 3);
    assert(command.batches[0].depthHack && command.batches[1].depthHack);
    assert(command.batches[0].modelIdentity == &model);
    const auto savedVertices = command.vertices;
    const auto savedIndices = command.indices;
    const auto unchanged = [&]() {
        assert(command.vertices.size() == savedVertices.size());
        assert(std::memcmp(command.vertices.data(), savedVertices.data(),
            savedVertices.size() * sizeof(WebRendererSurfaceVertex)) == 0);
        assert(command.indices == savedIndices && command.batches.size() == 2);
        assert(command.surfaceCount == 2 && command.dobjCount == 1 && command.modelCount == 1);
    };
    auto badVertices = vertices;
    surfaces[1].verts0 = badVertices.data();
    for (float *value : {&badVertices[2].xyz[0], &badVertices[2].binormalSign})
    {
        const float saved = *value;
        *value = std::numeric_limits<float>::quiet_NaN();
        assert(WebRenderer_BuildDObjSceneCommand(&submission, 1, command, &lodParms) ==
            WebRendererDObjSceneResult::InvalidModel);
        unchanged();
        *value = saved;
    }
    badVertices[2].texCoord.packed = UINT32_MAX;
    assert(WebRenderer_BuildDObjSceneCommand(&submission, 1, command, &lodParms) ==
        WebRendererDObjSceneResult::InvalidModel);
    unchanged();
    surfaces[1].verts0 = vertices.data();
    std::uint16_t badIndices[]{0, 2, 3};
    surfaces[1].triIndices = badIndices;
    assert(WebRenderer_BuildDObjSceneCommand(&submission, 1, command, &lodParms) ==
        WebRendererDObjSceneResult::IndexOutOfRange);
    unchanged();
    surfaces[1].triIndices = indices;
    obj.hidePartBits[0] = 0x80000000u;
    assert(WebRenderer_BuildDObjSceneCommand(&submission, 1, command, &lodParms) ==
        WebRendererDObjSceneResult::NoDObj);
    unchanged();
    obj.hidePartBits[0] = 0;
    // The same emission loop also receives weighted vertices.
    std::uint16_t blend[]{0, 0, 0};
    for (XSurface &surface : surfaces)
    {
        surface.deformed = true;
        surface.vertInfo.vertCount[0] = 3;
        surface.vertInfo.vertsBlend = blend;
    }
    assert(WebRenderer_BuildDObjSceneCommand(&submission, 1, command, &lodParms) ==
        WebRendererDObjSceneResult::Success);
    unchanged();
}

Material *ResolveMaterial(Material *) noexcept
{
    return g_resolvedMaterial;
}

void TestFusedSkinningUsesCurrentPoseAndRecyclesOnlyGeometry()
{
    // Synthetic four-bone fixture: each weighted block has one vertex, and
    // the rigid version addresses the same bones through four rigid lists.
    std::array<DObjAnimMat, 4> base{}, posed{};
    std::array<GfxPackedVertex, 4> vertices{};
    std::array<XRigidVertList, 4> rigid{};
    std::uint16_t blend[]{0, 0, 64, 32768,
        0, 64, 16384, 128, 32768,
        0, 64, 16384, 128, 16384, 192, 16384};
    std::uint16_t indices[]{0, 1, 2, 2, 3, 0};
    for (int i = 0; i < 4; ++i)
    {
        base[i].quat[3] = 1.0f;
        base[i].trans[0] = static_cast<float>(i);
        vertices[i].xyz[0] = 2.0f;
        vertices[i].xyz[1] = 3.0f;
        vertices[i].xyz[2] = 5.0f;
        vertices[i].normal.packed = 1;
        vertices[i].tangent.packed = 2;
        vertices[i].color.packed = 0x80402010u;
        vertices[i].texCoord.packed = 0x0201u;
        vertices[i].binormalSign = -1.0f;
        rigid[i].boneOffset = static_cast<std::uint16_t>(i * sizeof(DObjSkelMat));
        rigid[i].vertCount = 1;
    }
    XSurface surface{};
    surface.vertCount = 4;
    surface.triCount = 2;
    surface.verts0 = vertices.data();
    surface.triIndices = indices;
    surface.vertList = rigid.data();
    surface.vertListCount = 4;
    std::fill_n(surface.vertInfo.vertCount, 4, 1);
    surface.vertInfo.vertsBlend = blend;
    MaterialTechnique shadow{};
    MaterialTechniqueSet techniques{};
    techniques.techniques[2] = &shadow;
    Material material{};
    material.techniqueSet = &techniques;
    Material *materials[]{&material};
    XModel model{};
    model.numBones = 4;
    model.numLods = 1;
    model.numsurfs = 1;
    model.lodInfo[0].numsurfs = 1;
    model.surfs = &surface;
    model.materialHandles = materials;
    model.baseMat = base.data();
    XModel *models[]{&model};
    DObj_s obj{};
    obj.numModels = 1;
    obj.numBones = 4;
    obj.models = models;
    obj.skel.mat = posed.data();
    cpose_t pose{};
    WebRendererDObjSubmission submission{&obj, &pose, 17u, 2u};
    WebRendererLodParms lod{};
    assert(WebRenderer_BuildLodParms(pose.origin, 1, 1, 0, 1, 0, lod));
    g_canonicalLod = 0;
    WebRenderer_ReleaseDObjSceneScratch();
    WebRendererDObjSceneCommand command;
    const WebRendererSurfaceVertex *recycledVertices = nullptr;
    const std::uint32_t *recycledIndices = nullptr;
    for (int frame = 0; frame < 4; ++frame)
    {
        surface.deformed = frame < 2;
        float angles[4]{};
        for (int bone = 0; bone < 4; ++bone)
        {
            angles[bone] = 0.25f * bone + 0.1f * frame;
            posed[bone].quat[2] = std::sin(angles[bone] / 2.0f);
            posed[bone].quat[3] = std::cos(angles[bone] / 2.0f);
            posed[bone].trans[0] = 10.0f * bone + frame;
            posed[bone].trans[1] = -2.0f * frame;
        }
        assert(WebRenderer_BuildDObjSceneCommand(&submission, 1, command, &lod) ==
            WebRendererDObjSceneResult::Success);
        if (recycledVertices) assert(command.vertices.data() == recycledVertices);
        if (recycledIndices) assert(command.indices.data() == recycledIndices);
        assert(command.batches.size() == 1 && command.batches[0].castsSunShadow);
        assert(command.batches[0].depthHack && command.batches[0].modelIdentity == &model);
        for (int vertex = 0; vertex < 4; ++vertex)
        {
            float weights[4]{};
            if (!surface.deformed) weights[vertex] = 1;
            else if (vertex == 0) weights[0] = 1;
            else if (vertex == 1) weights[0] = weights[1] = 0.5f;
            else if (vertex == 2) { weights[0] = weights[1] = 0.25f; weights[2] = 0.5f; }
            else std::fill_n(weights, 4, 0.25f);
            // Independent planar rotation oracle, including inverse bind translation.
            float x = 0, y = 0, nx = 0, ny = 0;
            for (int bone = 0; bone < 4; ++bone)
            {
                const float c = std::cos(angles[bone]), s = std::sin(angles[bone]);
                x += weights[bone] * ((2.0f - bone) * c - 3.0f * s + posed[bone].trans[0]);
                y += weights[bone] * ((2.0f - bone) * s + 3.0f * c + posed[bone].trans[1]);
                nx += weights[bone] * c;
                ny += weights[bone] * s;
            }
            const float length = std::sqrt(nx * nx + ny * ny);
            const auto &v = command.vertices[vertex];
            assert(std::fabs(v.position[0] - x) < 0.00001f);
            assert(std::fabs(v.position[1] - y) < 0.00001f && v.position[2] == 5.0f);
            assert(std::fabs(v.normal[0] - nx / length) < 0.00001f);
            assert(std::fabs(v.normal[1] - ny / length) < 0.00001f && v.normal[2] == 0);
            assert(std::fabs(v.tangent[0] + ny / length) < 0.00001f);
            assert(std::fabs(v.tangent[1] - nx / length) < 0.00001f && v.tangent[2] == 0);
            assert(v.binormalSign == -1 && v.textureCoordinate[0] == 1 && v.textureCoordinate[1] == 2);
            assert(v.lightmapCoordinate[0] == 0 && v.lightmapCoordinate[1] == 0);
        }
        for (int i = 0; i < 6; ++i) assert(command.indices[i] == indices[i]);
        const auto saved = command.vertices;
        // Malformed weighted offsets and rigid counts must not publish partial output.
        if (surface.deformed) blend[9] = 1;
        else rigid[3].vertCount = 2;
        assert(WebRenderer_BuildDObjSceneCommand(&submission, 1, command, &lod) ==
            WebRendererDObjSceneResult::InvalidModel);
        assert(command.vertices.size() == saved.size());
        assert(std::memcmp(command.vertices.data(), saved.data(), saved.size() * sizeof(saved[0])) == 0);
        blend[9] = 0;
        rigid[3].vertCount = 1;
        // Clear failed scratch first so the exact submitted allocation is recycled.
        WebRenderer_ReleaseDObjSceneScratch();
        recycledVertices = command.vertices.data();
        recycledIndices = command.indices.data();
        WebRenderer_RecycleDObjSceneGeometry(command);
        assert(command.vertices.empty() && command.indices.empty());
    }
    WebRenderer_ReleaseDObjSceneScratch();
}

void TestDynamicShadowRangesPreserveGeometryAndPlacement()
{
    struct Batch { std::uint32_t firstIndex, indexCount; bool castsSunShadow, opaque; };
    struct Draw
    {
        std::uint32_t batchIndex, instance;
        bool partitionVisible = true;
    };
    const std::vector<Batch> batches{
        {0, 3, true, true}, {3, 3, true, true}, {6, 3, true, false},
        {9, 3, true, true}, {12, 3, false, true}, {15, 3, true, true},
        {18, 3, true, true}, {21, 3, true, true}, {24, 0, true, true},
        {24, 3, true, true}, {30, 3, true, true},
        {UINT32_MAX - 2u, 3, true, true}, {0, 3, true, true},
    };
    const std::vector<Draw> draws{
        {0, UINT32_MAX}, {1, UINT32_MAX}, {2, UINT32_MAX},
        {3, UINT32_MAX}, {4, UINT32_MAX}, {5, 0}, {6, 1, false}, {7, 1},
        {8, 1}, {9, 1}, {10, 1}, {11, 1}, {12, 1},
    };
    // Compare every index, placement and cutout identity with the original
    // unmerged stream; a skipped caster, gap or overflow cannot be bridged.
    using TriangleInput = std::array<std::uint32_t, 3>;
    std::vector<TriangleInput> expected, actual;
    const auto append = [](auto &out, const Draw &draw, const Batch &batch,
        std::uint32_t first, std::uint32_t count) {
        for (std::uint32_t i = 0; i < count; ++i)
            out.push_back({first + i, draw.instance, batch.opaque ? UINT32_MAX : draw.batchIndex});
    };
    for (const auto &draw : draws)
    {
        const auto &batch = batches[draw.batchIndex];
        if (draw.partitionVisible && batch.castsSunShadow)
            append(expected, draw, batch, batch.firstIndex, batch.indexCount);
    }
    const auto merged = WebRenderer_ForEachShadowRange(draws,
        [&](const Draw &draw) -> const Batch & { return batches[draw.batchIndex]; },
        [](const Draw &draw, const Batch &batch) {
            return draw.partitionVisible && batch.castsSunShadow;
        },
        [&](const Draw &a, const Draw &b) {
            return a.instance == b.instance && batches[a.batchIndex].opaque && batches[b.batchIndex].opaque;
        },
        [&](const Draw &draw, const Batch &batch, std::uint32_t first, std::uint32_t count) {
            append(actual, draw, batch, first, count);
        });
    assert(merged == 1 && actual == expected);
    // Exercise the existing world adapter as well as the dynamic reference path.
    actual.clear();
    expected.clear();
    for (const auto &batch : batches)
        if (batch.castsSunShadow) append(expected, {0, 0}, batch, batch.firstIndex, batch.indexCount);
    WebRenderer_ForEachSunShadowRange(batches,
        [](const Batch &batch) { return batch.opaque; },
        [&](const Batch &batch, std::uint32_t first, std::uint32_t count) {
            append(actual, {0, 0}, batch, first, count);
        });
    assert(actual == expected);
}

void TestStableDrawOrderPreservesUnsafeAnchors()
{
    struct Batch { std::uint32_t key; bool reorderable; };
    struct Draw { std::uint32_t batchIndex; };
    const std::vector<Batch> batches{
        {30u, true}, {10u, true}, {10u, true},
        {0u, false},
        {40u, true}, {20u, true},
        {1u, false},
        {5u, true},
    };
    const std::vector<Draw> draws{
        {0u}, {1u}, {2u}, {3u}, {4u}, {5u}, {6u}, {7u},
    };
    std::vector<std::uint32_t> order;
    WebRenderer_BuildStableDrawOrder(draws,
        [&](const Draw &draw) -> const Batch & {
            return batches[draw.batchIndex];
        },
        [](const Batch &batch) { return batch.reorderable; },
        [](const Batch &batch) { return batch.key; }, order);
    assert((order == std::vector<std::uint32_t>{
        1u, 2u, 0u, 3u, 5u, 4u, 6u, 7u}));
}

void TestLodDelegatesToCanonicalXModelPolicy()
{
    XModel model{};
    model.numLods = 3;
    const float poseOrigin[3] = {3.0f, 4.0f, 0.0f};
    const float viewOrigin[3] = {0.0f, 0.0f, 0.0f};
    WebRendererLodParms parms{};
    constexpr float NATIVE_FOV_BASIS = 2.118673086166382f;
    assert(WebRenderer_BuildLodParms(viewOrigin, 1.0f / NATIVE_FOV_BASIS,
        1.0f, 0.0f, 1.0f, 0.0f, parms));

    g_lodDistance = -1.0f;
    g_canonicalLod = 2;
    assert(WebRenderer_SelectDObjLod(
        &model, poseOrigin, &parms) == 2);
    assert(g_lodDistance == 5.0f);

    g_canonicalLod = -1;
    assert(WebRenderer_SelectDObjLod(
        &model, poseOrigin, &parms) == -1);
    assert(WebRenderer_SelectDObjLod(&model, poseOrigin, nullptr) == -1);
    assert(WebRenderer_SelectDObjLod(nullptr, poseOrigin, &parms) == -1);

    g_canonicalLod = 1;
    assert(WebRenderer_BuildLodParms(viewOrigin, 1.0f / NATIVE_FOV_BASIS,
        2.0f, 3.0f, 4.0f, 5.0f, parms));
    model.lodRampType = 0u;
    assert(WebRenderer_SelectDObjLod(&model, poseOrigin, &parms) == 1);
    assert(std::fabs(g_lodDistance - 13.0f) < 0.0001f);
    model.lodRampType = 1u;
    assert(WebRenderer_SelectDObjLod(&model, poseOrigin, &parms) == 1);
    assert(std::fabs(g_lodDistance - 25.0f) < 0.0001f);

    model.lodRampType = 0u;
    assert(WebRenderer_SelectModelLod(
        &model, poseOrigin, 2.0f, parms) == 1);
    assert(std::fabs(g_lodDistance - 8.0f) < 0.0001f);
    assert(WebRenderer_SelectStaticModelLod(
        &model, poseOrigin, 2.0f, 20.0f, parms) == 1);
    assert(std::fabs(g_lodDistance - 6.5f) < 0.0001f);
    assert(WebRenderer_SelectStaticModelLod(
        &model, poseOrigin, 2.0f, 12.0f, parms) == -1);

    WebRendererLodParms invalid{};
    assert(!WebRenderer_BuildLodParms(
        nullptr, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, invalid));
    assert(WebRenderer_SelectDObjLod(&model, poseOrigin, &invalid) == -1);
}

void TestOrdinaryAndViewmodelFlagsShareAdmission()
{
    const DObj_s *object = reinterpret_cast<const DObj_s *>(0x1u);
    const cpose_t *pose = reinterpret_cast<const cpose_t *>(0x1u);

    for (const std::uint32_t renderFlags : {0u, 3u, 4u, 7u})
    {
        const WebRendererDObjSubmission submission{
            object, pose, 17u, renderFlags};
        assert(WebRenderer_ValidateDObjSubmission(submission, 0u) ==
            WebRendererDObjAdmissionResult::Accepted);
    }
}

void TestNativeDepthHackBitIsPreservedAtPortableBoundary()
{
    assert(!WebRenderer_DObjUsesDepthHack(0u));
    assert(!WebRenderer_DObjUsesDepthHack(1u));
    assert(WebRenderer_DObjUsesDepthHack(2u));
    assert(WebRenderer_DObjUsesDepthHack(3u));
    assert(!WebRenderer_DObjUsesDepthHack(4u));
    assert(WebRenderer_DObjUsesDepthHack(7u));
}

void TestNativeSunShadowCullBitIsPreservedAtPortableBoundary()
{
    assert(WebRenderer_DObjIsSunShadowCandidate(0u));
    assert(!WebRenderer_DObjIsSunShadowCandidate(1u));
    assert(WebRenderer_DObjIsSunShadowCandidate(2u));
    assert(!WebRenderer_DObjIsSunShadowCandidate(3u));
}

void TestInvalidAndCapacityAdmissionIsDeterministic()
{
    const DObj_s *object = reinterpret_cast<const DObj_s *>(0x1u);
    const cpose_t *pose = reinterpret_cast<const cpose_t *>(0x1u);
    const WebRendererDObjSubmission valid{object, pose, 1u, 0u};
    const WebRendererDObjSubmission invalid{nullptr, pose, 1u, 0u};

    assert(WebRenderer_ValidateDObjSubmission(invalid, 0u) ==
        WebRendererDObjAdmissionResult::InvalidSubmission);
    assert(WebRenderer_ValidateDObjSubmission(valid, 511u, 512u) ==
        WebRendererDObjAdmissionResult::Accepted);
    assert(WebRenderer_ValidateDObjSubmission(valid, 512u, 512u) ==
        WebRendererDObjAdmissionResult::LimitReached);
}

void TestDObjMaterialResolutionPreservesCanonicalFallback()
{
    Material alias{};
    Material canonical{};
    g_resolvedMaterial = &canonical;
    assert(WebRenderer_ResolveDObjMaterial(&alias, ResolveMaterial) ==
        &canonical);
    g_resolvedMaterial = nullptr;
    assert(WebRenderer_ResolveDObjMaterial(&alias, ResolveMaterial) == &alias);
    assert(WebRenderer_ResolveDObjMaterial(&alias, nullptr) == &alias);
}

void TestReflexSightTechniqueSelectsIntensityOpacitySubset()
{
    assert(WebRenderer_IsReflexSightTechnique("reflexsight_dtex"));
    assert(WebRenderer_IsReflexSightTechnique("reflexsight"));
    assert(!WebRenderer_IsReflexSightTechnique("lp_t0c0_sm2"));
    assert(!WebRenderer_IsReflexSightTechnique(nullptr));
    assert(WebRenderer_UsesColorIntensityOpacity(
        WebRendererWorldTechnique::ReflexSight));
    assert(!WebRenderer_UsesColorIntensityOpacity(
        WebRendererWorldTechnique::BaseTexture));
    assert(WebRenderer_UsesModelEnvironmentSpecular(
        WebRendererWorldTechnique::BaseTextureSpecular));
    assert(WebRenderer_UsesModelEnvironmentSpecular(
        WebRendererWorldTechnique::BaseTextureNormalSpecular));
    assert(WebRenderer_UsesWorldNormalMap(
        WebRendererWorldTechnique::BaseTextureNormalSpecular));
    assert(!WebRenderer_UsesSecondaryDirectionalLightmap(
        WebRendererWorldTechnique::BaseTextureNormalSpecular));
}
} // namespace

int main()
{
    TestLodDelegatesToCanonicalXModelPolicy();
    TestOrdinaryAndViewmodelFlagsShareAdmission();
    TestNativeDepthHackBitIsPreservedAtPortableBoundary();
    TestNativeSunShadowCullBitIsPreservedAtPortableBoundary();
    TestInvalidAndCapacityAdmissionIsDeterministic();
    TestDObjMaterialResolutionPreservesCanonicalFallback();
    TestReflexSightTechniqueSelectsIntensityOpacitySubset();
    TestDObjEmissionAndAtomicFailure();
    TestFusedSkinningUsesCurrentPoseAndRecyclesOnlyGeometry();
    TestDynamicShadowRangesPreserveGeometryAndPlacement();
    TestStableDrawOrderPreservesUnsafeAnchors();
    return 0;
}

int __cdecl XModelGetLodForDist(const XModel *, float distance)
{
    g_lodDistance = distance;
    return g_canonicalLod;
}

DObjAnimMat *__cdecl CG_DObjCalcPose(const cpose_t *, const DObj_s *obj, int *)
{
    return obj->skel.mat;
}
XModel *__cdecl DObjGetModel(const DObj_s *obj, int index) { return obj->models[index]; }
void __cdecl DObjGetHidePartBits(const DObj_s *obj, std::uint32_t *bits)
{
    std::copy_n(obj->hidePartBits, 4, bits);
}
void __cdecl ConvertQuatToSkelMat(const DObjAnimMat *mat, DObjSkelMat *matrix)
{
    *matrix = {};
    for (int i = 0; i < 3; ++i) matrix->axis[i][i] = 1.0f;
    const float z = mat->quat[2], w = mat->quat[3];
    matrix->axis[0][0] = matrix->axis[1][1] = 1.0f - 2.0f * z * z;
    matrix->axis[0][1] = 2.0f * z * w;
    matrix->axis[1][0] = -2.0f * z * w;
    std::copy_n(mat->trans, 3, matrix->origin);
    matrix->origin[3] = 1.0f;
}
void __cdecl ConvertQuatToInverseSkelMat(const DObjAnimMat *mat, DObjSkelMat *matrix)
{
    ConvertQuatToSkelMat(mat, matrix);
    // Fixtures use translation-only inverse bind matrices.
    for (int i = 0; i < 3; ++i) matrix->origin[i] = -mat->trans[i];
}
void __cdecl Vec2UnpackTexCoords(PackedTexCoords in, float *out)
{
    out[0] = in.packed == UINT32_MAX ? std::numeric_limits<float>::quiet_NaN()
        : static_cast<float>(in.packed & 0xffu);
    out[1] = static_cast<float>((in.packed >> 8) & 0xffu);
}
void __cdecl Vec3UnpackUnitVec(PackedUnitVec in, float *out)
{
    out[0] = out[1] = out[2] = 0.0f;
    out[in.packed == 1 ? 0 : in.packed == 2 ? 1 : 2] = 1.0f;
}
