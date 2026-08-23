#include <EffectsCore/fx_system.h>
#include <bgame/bg_local.h>
#include <gfx_d3d/material_types.h>
#include <gfx_d3d/r_bsp.h>
#include <universal/com_math.h>
#include <web/web_renderer_mark_fragments.h>
#include <xanim/dobj.h>
#include <xanim/xmodel_types.h>
#include <xanim/xsurface_types.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

GfxWorld s_world{};

void __cdecl Vec3Sub(const float *a, const float *b, float *out)
{
    for (int i = 0; i < 3; ++i) out[i] = a[i] - b[i];
}

float __cdecl Vec3Dot(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void __cdecl Vec3Cross(const float *a, const float *b, float *out)
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

float __cdecl Vec3LengthSq(const float *value)
{
    return Vec3Dot(value, value);
}

void __cdecl Vec3Lerp(const float *start, const float *end,
    float fraction, float *out)
{
    for (int i = 0; i < 3; ++i)
        out[i] = start[i] + (end[i] - start[i]) * fraction;
}

void __cdecl Vec3UnpackUnitVec(PackedUnitVec, float *out)
{
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 1.0f;
}

void __cdecl AnglesToAxis(const float *, float axis[3][3])
{
    axis[0][0] = 1.0f;
    axis[0][1] = 0.0f;
    axis[0][2] = 0.0f;
    axis[1][0] = 0.0f;
    axis[1][1] = 1.0f;
    axis[1][2] = 0.0f;
    axis[2][0] = 0.0f;
    axis[2][1] = 0.0f;
    axis[2][2] = 1.0f;
}

DObjAnimMat testPosedMats[1]{};

int __cdecl DObjGetSurfaces(const DObj_s *, int *partBits, const char *)
{
    std::fill_n(partBits, 4u, -1);
    return 1;
}

void __cdecl DObjLock(DObj_s *) {}
void __cdecl DObjUnlock(DObj_s *) {}

DObjAnimMat *__cdecl CG_DObjCalcPose(
    const cpose_t *, const DObj_s *, int *)
{
    return testPosedMats;
}

void __cdecl DObjGetHidePartBits(
    const DObj_s *object, std::uint32_t *partBits)
{
    std::copy_n(object->hidePartBits, 4u, partBits);
}

int __cdecl DObjSkelAreBonesUpToDate(const DObj_s *, int *)
{
    return 1;
}

void __cdecl ConvertQuatToSkelMat(
    const DObjAnimMat *mat, DObjSkelMat *skelMat)
{
    *skelMat = {};
    skelMat->axis[0][0] = 1.0f;
    skelMat->axis[1][1] = 1.0f;
    skelMat->axis[2][2] = 1.0f;
    skelMat->origin[3] = 1.0f;
    std::copy_n(mat->trans, 3u, skelMat->origin);
}

void __cdecl ConvertQuatToInverseSkelMat(
    const DObjAnimMat *mat, DObjSkelMat *skelMat)
{
    ConvertQuatToSkelMat(mat, skelMat);
    for (std::size_t axis = 0u; axis < 3u; ++axis)
        skelMat->origin[axis] = -skelMat->origin[axis];
}

namespace
{
int callbackCount;
int callbackTriCount;
int callbackPointCount;
GfxMarkContext callbackContext{};
float callbackOrigin[3]{};
float callbackTexCoordAxis[3]{};
float callbackFirstPoint[3]{};

void __cdecl CaptureFragments(void *, int triCount, FxMarkTri *triangles,
    int pointCount, FxMarkPoint *points, const float *origin,
    const float *texCoordAxis)
{
    ++callbackCount;
    callbackTriCount = triCount;
    callbackPointCount = pointCount;
    callbackContext = triangles[0].context;
    std::copy_n(origin, 3u, callbackOrigin);
    std::copy_n(texCoordAxis, 3u, callbackTexCoordAxis);
    std::copy_n(points[0].xyz, 3u, callbackFirstPoint);
}

void BuildSingleSurfaceWorld(Material &receiver)
{
    static GfxWorldVertex vertices[3]{};
    static std::uint16_t indices[3]{0u, 1u, 2u};
    static GfxSurface surface{};
    vertices[0].xyz[0] = -2.0f;
    vertices[0].xyz[1] = -2.0f;
    vertices[1].xyz[0] = 2.0f;
    vertices[1].xyz[1] = -2.0f;
    vertices[2].xyz[0] = 0.0f;
    vertices[2].xyz[1] = 2.0f;
    surface.tris.firstVertex = 0;
    surface.tris.vertexCount = 3u;
    surface.tris.triCount = 1u;
    surface.tris.baseIndex = 0;
    surface.material = &receiver;
    surface.lightmapIndex = 31u;
    surface.reflectionProbeIndex = 0u;
    surface.primaryLightIndex = 2u;
    for (int axis = 0; axis < 3; ++axis)
    {
        surface.bounds[0][axis] = -2.0f;
        surface.bounds[1][axis] = 2.0f;
    }
    s_world = {};
    s_world.indexCount = 3;
    s_world.indices = indices;
    s_world.surfaceCount = 1;
    s_world.vertexCount = 3u;
    s_world.vd.vertices = vertices;
    s_world.dpvs.surfaces = &surface;
}

void RunMark(const float origin[3], Material &mark)
{
    const float axis[3][3]{
        {0.0f, 0.0f, -1.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    const float viewOffset[3]{};
    MarkInfo info{};
    FxMarkTri triangles[32]{};
    FxMarkPoint points[64]{};
    R_MarkFragments_Begin(&info, MARK_FRAGMENTS_AGAINST_BRUSHES,
        origin, axis, 1.0f, viewOffset, &mark);
    R_MarkFragments_Go(&info, CaptureFragments, nullptr,
        32, triangles, 64, points);
}

void TestCanonicalWorldTriangleIsClippedAndPublished()
{
    Material receiver{};
    Material mark{};
    receiver.info.surfaceTypeBits = 1u;
    mark.info.surfaceTypeBits = 1u;
    BuildSingleSurfaceWorld(receiver);
    callbackCount = 0;
    const float origin[3]{0.0f, 0.0f, 0.0f};
    RunMark(origin, mark);
    assert(callbackCount == 1);
    assert(callbackTriCount >= 1);
    assert(callbackPointCount >= 3);
    assert(callbackContext.lmapIndex == 31u);
    assert(callbackContext.primaryLightIndex == 2u);
}

void TestBoundsAndMaterialFiltersSuppressMarks()
{
    Material receiver{};
    Material mark{};
    receiver.info.surfaceTypeBits = 1u;
    mark.info.surfaceTypeBits = 1u;
    BuildSingleSurfaceWorld(receiver);
    callbackCount = 0;
    const float outside[3]{100.0f, 100.0f, 100.0f};
    RunMark(outside, mark);
    assert(callbackCount == 0);
    receiver.stateFlags = 4u;
    const float origin[3]{};
    RunMark(origin, mark);
    assert(callbackCount == 0);
}

void TestMovingBrushFragmentsUsePoseLocalSpaceAndContext()
{
    Material receiver{};
    Material mark{};
    receiver.info.surfaceTypeBits = 1u;
    mark.info.surfaceTypeBits = 1u;
    BuildSingleSurfaceWorld(receiver);
    static GfxBrushModel models[2]{};
    models[0].startSurfIndex = 0u;
    models[0].surfaceCount = 0u;
    models[1].startSurfIndex = 0u;
    models[1].surfaceCount = 1u;
    s_world.models = models;
    s_world.modelCount = 2;

    const float origin[3]{10.0f, 20.0f, 30.0f};
    const float axis[3][3]{
        {0.0f, 0.0f, -1.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    const float viewOffset[3]{};
    MarkInfo info{};
    FxMarkTri triangles[32]{};
    FxMarkPoint points[64]{};
    cpose_t pose{};
    std::copy_n(origin, 3u, pose.origin);
    callbackCount = 0;
    R_MarkFragments_Begin(&info, MARK_FRAGMENTS_AGAINST_BRUSHES,
        origin, axis, 1.0f, viewOffset, &mark);
    assert(R_MarkFragments_AddBModel(
        &info, &models[1], &pose, 77u) == 1);
    R_MarkFragments_Go(&info, CaptureFragments, nullptr,
        32, triangles, 64, points);
    assert(callbackCount == 1);
    assert(callbackTriCount >= 1);
    assert(callbackPointCount >= 3);
    assert(callbackContext.modelTypeAndSurf == 0x80u);
    assert(callbackContext.modelIndex == 77u);
    assert(std::fabs(callbackOrigin[0]) < 0.0001f);
    assert(std::fabs(callbackOrigin[1]) < 0.0001f);
    assert(std::fabs(callbackOrigin[2]) < 0.0001f);
    assert(std::fabs(callbackFirstPoint[0]) < 2.1f);
    assert(std::fabs(callbackFirstPoint[1]) < 2.1f);
    assert(std::fabs(callbackFirstPoint[2]) < 0.0001f);
    assert(callbackTexCoordAxis[0] == 1.0f);
    assert(callbackTexCoordAxis[1] == 0.0f);
    assert(callbackTexCoordAxis[2] == 0.0f);
}

void TestStaticModelFragmentsUseCanonicalInstanceContext()
{
    Material receiver{};
    Material mark{};
    receiver.info.surfaceTypeBits = 1u;
    mark.info.surfaceTypeBits = 1u;
    BuildSingleSurfaceWorld(receiver);

    GfxPackedVertex vertices[3]{};
    vertices[0].xyz[0] = -2.0f;
    vertices[0].xyz[1] = -2.0f;
    vertices[1].xyz[0] = 2.0f;
    vertices[1].xyz[1] = -2.0f;
    vertices[2].xyz[1] = 2.0f;
    std::uint16_t indices[3]{0u, 1u, 2u};
    XSurface surface{};
    surface.vertCount = 3u;
    surface.triCount = 1u;
    surface.verts0 = vertices;
    surface.triIndices = indices;
    Material *materials[1]{&receiver};
    XModel model{};
    model.numsurfs = 1u;
    model.numLods = 1;
    model.surfs = &surface;
    model.materialHandles = materials;
    model.lodInfo[0].surfIndex = 0u;
    model.lodInfo[0].numsurfs = 1u;
    GfxStaticModelInst modelBounds{};
    GfxStaticModelDrawInst draw{};
    draw.model = &model;
    draw.placement.origin[0] = 10.0f;
    draw.placement.origin[1] = 20.0f;
    draw.placement.origin[2] = 30.0f;
    draw.placement.axis[0][0] = 1.0f;
    draw.placement.axis[1][1] = 1.0f;
    draw.placement.axis[2][2] = 1.0f;
    draw.placement.scale = 1.0f;
    draw.reflectionProbeIndex = 3u;
    draw.primaryLightIndex = 4u;
    for (int component = 0; component < 3; ++component)
    {
        modelBounds.mins[component] = draw.placement.origin[component] - 2.0f;
        modelBounds.maxs[component] = draw.placement.origin[component] + 2.0f;
    }
    s_world.dpvs.smodelCount = 1u;
    s_world.dpvs.smodelInsts = &modelBounds;
    s_world.dpvs.smodelDrawInsts = &draw;

    const float origin[3]{10.0f, 20.0f, 30.0f};
    const float axis[3][3]{
        {0.0f, 0.0f, -1.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    const float viewOffset[3]{};
    MarkInfo info{};
    FxMarkTri triangles[32]{};
    FxMarkPoint points[64]{};
    callbackCount = 0;
    R_MarkFragments_Begin(&info, MARK_FRAGMENTS_AGAINST_MODELS,
        origin, axis, 1.0f, viewOffset, &mark);
    assert(info.smodelCollidedCount == 1);
    R_MarkFragments_Go(&info, CaptureFragments, nullptr,
        32, triangles, 64, points);
    assert(callbackCount == 1);
    assert(callbackTriCount >= 1);
    assert(callbackPointCount >= 3);
    assert(callbackContext.modelTypeAndSurf == 0x40u);
    assert(callbackContext.modelIndex == 0u);
    assert(callbackContext.lmapIndex == 31u);
    assert(callbackContext.reflectionProbeIndex == 3u);
    assert(callbackContext.primaryLightIndex == 4u);
}

void TestAnimatedDObjFragmentsRemainBoneLocal()
{
    Material receiver{};
    Material mark{};
    receiver.info.surfaceTypeBits = 1u;
    mark.info.surfaceTypeBits = 1u;
    BuildSingleSurfaceWorld(receiver);

    GfxPackedVertex vertices[3]{};
    vertices[0].xyz[0] = -2.0f;
    vertices[0].xyz[1] = -2.0f;
    vertices[1].xyz[0] = 2.0f;
    vertices[1].xyz[1] = -2.0f;
    vertices[2].xyz[1] = 2.0f;
    std::uint16_t indices[3]{0u, 1u, 2u};
    XRigidVertList vertList{};
    vertList.vertCount = 3u;
    vertList.triCount = 1u;
    XSurface surface{};
    surface.vertCount = 3u;
    surface.triCount = 1u;
    surface.verts0 = vertices;
    surface.triIndices = indices;
    surface.vertListCount = 1u;
    surface.vertList = &vertList;
    Material *materials[1]{&receiver};
    DObjAnimMat baseMat{};
    baseMat.quat[3] = 1.0f;
    baseMat.transWeight = 2.0f;
    XModel model{};
    model.numsurfs = 1u;
    model.numLods = 1;
    model.numBones = 1u;
    model.surfs = &surface;
    model.materialHandles = materials;
    model.baseMat = &baseMat;
    model.lodInfo[0].surfIndex = 0u;
    model.lodInfo[0].numsurfs = 1u;
    XModel *models[1]{&model};
    DObj_s object{};
    object.numModels = 1u;
    object.numBones = 1u;
    object.models = models;
    testPosedMats[0] = {};
    testPosedMats[0].quat[3] = 1.0f;
    testPosedMats[0].transWeight = 2.0f;
    testPosedMats[0].trans[0] = 10.0f;
    testPosedMats[0].trans[1] = 20.0f;
    testPosedMats[0].trans[2] = 30.0f;

    const float origin[3]{10.0f, 20.0f, 30.0f};
    const float axis[3][3]{
        {0.0f, 0.0f, -1.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    const float viewOffset[3]{};
    MarkInfo info{};
    FxMarkTri triangles[32]{};
    FxMarkPoint points[64]{};
    cpose_t pose{};
    callbackCount = 0;
    R_MarkFragments_Begin(&info, MARK_FRAGMENTS_AGAINST_MODELS,
        origin, axis, 1.0f, viewOffset, &mark);
    assert(R_MarkFragments_AddDObj(&info, &object, &pose, 55u) == 1);
    R_MarkFragments_Go(&info, CaptureFragments, nullptr,
        32, triangles, 64, points);
    assert(callbackCount == 1);
    assert(callbackTriCount >= 1);
    assert(callbackPointCount >= 3);
    assert(callbackContext.modelTypeAndSurf == 0xc0u);
    assert(callbackContext.modelIndex == 55u);
    assert(callbackContext.lmapIndex == 0u);
    assert(std::fabs(callbackOrigin[0]) < 0.0001f);
    assert(std::fabs(callbackOrigin[1]) < 0.0001f);
    assert(std::fabs(callbackOrigin[2]) < 0.0001f);
    assert(std::fabs(callbackFirstPoint[0]) < 2.1f);
    assert(std::fabs(callbackFirstPoint[1]) < 2.1f);
    assert(std::fabs(callbackFirstPoint[2]) < 0.0001f);
    assert(callbackTexCoordAxis[0] == 1.0f);
    assert(callbackTexCoordAxis[1] == 0.0f);
    assert(callbackTexCoordAxis[2] == 0.0f);
}
} // namespace

int main()
{
    TestCanonicalWorldTriangleIsClippedAndPublished();
    TestBoundsAndMaterialFiltersSuppressMarks();
    TestMovingBrushFragmentsUsePoseLocalSpaceAndContext();
    TestStaticModelFragmentsUseCanonicalInstanceContext();
    TestAnimatedDObjFragmentsRemainBoneLocal();
    return 0;
}
