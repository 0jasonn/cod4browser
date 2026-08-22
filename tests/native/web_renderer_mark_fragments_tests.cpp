#include <EffectsCore/fx_system.h>
#include <gfx_d3d/material_types.h>
#include <gfx_d3d/r_bsp.h>
#include <universal/com_math.h>
#include <web/web_renderer_mark_fragments.h>

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

namespace
{
int callbackCount;
int callbackTriCount;
int callbackPointCount;
GfxMarkContext callbackContext{};

void __cdecl CaptureFragments(void *, int triCount, FxMarkTri *triangles,
    int pointCount, FxMarkPoint *, const float *, const float *)
{
    ++callbackCount;
    callbackTriCount = triCount;
    callbackPointCount = pointCount;
    callbackContext = triangles[0].context;
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
} // namespace

int main()
{
    TestCanonicalWorldTriangleIsClippedAndPublished();
    TestBoundsAndMaterialFiltersSuppressMarks();
    return 0;
}
