// Browser renderer boundary for canonical EffectsCore impact-mark fragments.
// This is the world-brush subset of gfx_d3d/r_marks.cpp adapted to consume the
// published GfxWorld directly. EffectsCore still owns mark allocation,
// lifetime, material choice, vertex expansion, and draw generation.

#include <EffectsCore/fx_system.h>
#include <gfx_d3d/material_types.h>
#include <gfx_d3d/r_bsp.h>
#include <universal/com_math.h>
#include <web/web_renderer_mark_fragments.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

extern GfxWorld s_world;

namespace
{
constexpr int MARK_CLIP_PLANE_COUNT = 6;
constexpr int MARK_CLIP_POINT_CAPACITY = 9;
constexpr float MARK_CLIP_EPSILON = 0.5f;

enum class ClipSide : std::uint8_t
{
    Front,
    Back,
    On,
};

struct WebWorldMarkPoint
{
    float xyz[3];
    float vertWeights[3];
};

bool BoundsOverlap(const float lhs[2][3],
    const float mins[3], const float maxs[3]) noexcept
{
    for (std::size_t axis = 0u; axis < 3u; ++axis)
    {
        if (lhs[1][axis] < mins[axis] || lhs[0][axis] > maxs[axis])
            return false;
    }
    return true;
}

bool MaterialAllowsMark(const Material *receiver,
    const Material *mark) noexcept
{
    if (!receiver || !mark || (receiver->stateFlags & 4u) != 0u ||
        (receiver->info.gameFlags & 4u) != 0u)
        return false;
    return (receiver->info.surfaceTypeBits & mark->info.surfaceTypeBits) ==
        mark->info.surfaceTypeBits;
}

void GetFragmentBounds(const float origin[3], const float axis[3][3],
    float radius, float mins[3], float maxs[3]) noexcept
{
    for (std::size_t coordinate = 0u; coordinate < 3u; ++coordinate)
    {
        const float extent = (std::fabs(axis[0][coordinate]) +
            std::fabs(axis[1][coordinate]) +
            std::fabs(axis[2][coordinate])) * radius;
        mins[coordinate] = origin[coordinate] - extent;
        maxs[coordinate] = origin[coordinate] + extent;
    }
}

void GetFragmentClipPlanes(const float origin[3], const float axis[3][3],
    float radius, float planes[MARK_CLIP_PLANE_COUNT][4]) noexcept
{
    std::size_t planeIndex = 0u;
    for (std::size_t axisIndex = 0u; axisIndex < 3u; ++axisIndex)
    {
        for (std::size_t component = 0u; component < 3u; ++component)
        {
            planes[planeIndex][component] = axis[axisIndex][component];
            planes[planeIndex + 1u][component] = -axis[axisIndex][component];
        }
        planes[planeIndex][3] =
            Vec3Dot(planes[planeIndex], origin) - radius;
        planes[planeIndex + 1u][3] =
            Vec3Dot(planes[planeIndex + 1u], origin) - radius;
        planeIndex += 2u;
    }
}

bool TriangleRejected(const float markNormal[3],
    const float xyz0[3], const float xyz1[3], const float xyz2[3]) noexcept
{
    float edge01[3];
    float edge21[3];
    float scaledNormal[3];
    Vec3Sub(xyz0, xyz1, edge01);
    Vec3Sub(xyz2, xyz1, edge21);
    Vec3Cross(edge01, edge21, scaledNormal);
    const float scaledDot = Vec3Dot(scaledNormal, markNormal);
    return scaledDot < 0.0f ||
        Vec3LengthSq(scaledNormal) * 0.25f > scaledDot * scaledDot;
}

void SetupWorldClipPoints(const GfxWorldVertex *vertices,
    const std::uint16_t indices[3],
    WebWorldMarkPoint points[MARK_CLIP_POINT_CAPACITY]) noexcept
{
    for (std::size_t pointIndex = 0u; pointIndex < 3u; ++pointIndex)
    {
        const GfxWorldVertex &vertex = vertices[indices[pointIndex]];
        std::copy_n(vertex.xyz, 3u, points[pointIndex].xyz);
        points[pointIndex].vertWeights[0] = 0.0f;
        points[pointIndex].vertWeights[1] = 0.0f;
        points[pointIndex].vertWeights[2] = 0.0f;
        points[pointIndex].vertWeights[pointIndex] = 1.0f;
    }
}

void LerpClipPoint(const WebWorldMarkPoint &from,
    const WebWorldMarkPoint &to, float fraction,
    WebWorldMarkPoint &output) noexcept
{
    Vec3Lerp(from.xyz, to.xyz, fraction, output.xyz);
    Vec3Lerp(from.vertWeights, to.vertWeights, fraction,
        output.vertWeights);
}

int ChopWorldPolyBehindPlane(int inputCount,
    const WebWorldMarkPoint *input, WebWorldMarkPoint *output,
    const float plane[4]) noexcept
{
    if (inputCount < 3 || inputCount > MARK_CLIP_POINT_CAPACITY)
        return 0;
    float distances[MARK_CLIP_POINT_CAPACITY]{};
    ClipSide sides[MARK_CLIP_POINT_CAPACITY]{};
    int frontCount = 0;
    int backCount = 0;
    for (int index = 0; index < inputCount; ++index)
    {
        distances[index] = Vec3Dot(input[index].xyz, plane) - plane[3];
        if (distances[index] > MARK_CLIP_EPSILON)
        {
            sides[index] = ClipSide::Front;
            ++frontCount;
        }
        else if (distances[index] < -MARK_CLIP_EPSILON)
        {
            sides[index] = ClipSide::Back;
            ++backCount;
        }
        else
        {
            sides[index] = ClipSide::On;
        }
    }
    if (frontCount == 0) return 0;
    if (backCount == 0)
    {
        std::copy_n(input, inputCount, output);
        return inputCount;
    }

    int outputCount = 0;
    for (int index = 0; index < inputCount; ++index)
    {
        const int next = (index + 1) % inputCount;
        if (sides[index] != ClipSide::Back)
        {
            if (outputCount >= MARK_CLIP_POINT_CAPACITY) return 0;
            output[outputCount++] = input[index];
        }
        if (sides[index] == ClipSide::On || sides[next] == ClipSide::On ||
            sides[index] == sides[next])
            continue;
        if (outputCount >= MARK_CLIP_POINT_CAPACITY) return 0;
        const float denominator = distances[index] - distances[next];
        if (denominator == 0.0f) return 0;
        LerpClipPoint(input[index], input[next],
            distances[index] / denominator, output[outputCount++]);
    }
    return outputCount;
}

bool AppendBrushSurface(MarkInfo &info, const GfxSurface &surface,
    bool &anyMarks) noexcept
{
    if (!s_world.vd.vertices || !s_world.indices || !surface.material ||
        surface.tris.firstVertex < 0 || surface.tris.baseIndex < 0 ||
        surface.tris.triCount == 0u || surface.tris.vertexCount == 0u)
        return true;
    const std::uint32_t firstVertex =
        static_cast<std::uint32_t>(surface.tris.firstVertex);
    const std::uint32_t firstIndex =
        static_cast<std::uint32_t>(surface.tris.baseIndex);
    const std::uint32_t indexCount =
        static_cast<std::uint32_t>(surface.tris.triCount) * 3u;
    if (firstVertex > s_world.vertexCount ||
        surface.tris.vertexCount > s_world.vertexCount - firstVertex ||
        firstIndex > static_cast<std::uint32_t>(s_world.indexCount) ||
        indexCount > static_cast<std::uint32_t>(s_world.indexCount) -
            firstIndex)
        return true;

    GfxMarkContext context{};
    context.lmapIndex = surface.lightmapIndex;
    if (info.markHasLightmap != (context.lmapIndex != 31u)) return true;
    context.reflectionProbeIndex = surface.reflectionProbeIndex;
    if (info.markHasReflection != (context.reflectionProbeIndex != 0u))
        return true;
    context.primaryLightIndex = surface.primaryLightIndex;
    context.modelTypeAndSurf = 0u;
    context.modelIndex = 0u;

    const GfxWorldVertex *vertices = s_world.vd.vertices + firstVertex;
    const std::uint16_t *indices = s_world.indices + firstIndex;
    for (std::uint32_t triangle = 0u;
         triangle < surface.tris.triCount; ++triangle, indices += 3u)
    {
        if (indices[0] >= surface.tris.vertexCount ||
            indices[1] >= surface.tris.vertexCount ||
            indices[2] >= surface.tris.vertexCount)
            continue;
        if (TriangleRejected(info.axis[0], vertices[indices[0]].xyz,
                vertices[indices[1]].xyz, vertices[indices[2]].xyz))
            continue;

        WebWorldMarkPoint clipPoints[2][MARK_CLIP_POINT_CAPACITY]{};
        SetupWorldClipPoints(vertices, indices, clipPoints[0]);
        int pingPong = 0;
        int fragmentPointCount = 3;
        for (int plane = 0; plane < MARK_CLIP_PLANE_COUNT; ++plane)
        {
            fragmentPointCount = ChopWorldPolyBehindPlane(
                fragmentPointCount, clipPoints[pingPong],
                clipPoints[pingPong ^ 1], info.planes[plane]);
            if (fragmentPointCount == 0) break;
            pingPong ^= 1;
        }
        if (fragmentPointCount == 0) continue;
        const int fragmentTriCount = fragmentPointCount - 2;
        if (fragmentPointCount > info.maxPoints - info.usedPointCount ||
            fragmentTriCount > info.maxTris - info.usedTriCount)
            return false;

        const int basePoint = info.usedPointCount;
        for (int point = 2; point < fragmentPointCount; ++point)
        {
            FxMarkTri &triangleOut = info.tris[info.usedTriCount++];
            triangleOut.indices[0] = static_cast<std::uint16_t>(
                basePoint + point - 1);
            triangleOut.indices[1] = static_cast<std::uint16_t>(
                basePoint + point);
            triangleOut.indices[2] = static_cast<std::uint16_t>(basePoint);
            triangleOut.context = context;
        }

        float lightmapCoordinates[3][2]{};
        float normals[3][3]{};
        for (std::size_t vertex = 0u; vertex < 3u; ++vertex)
        {
            const GfxWorldVertex &source = vertices[indices[vertex]];
            std::copy_n(source.lmapCoord, 2u, lightmapCoordinates[vertex]);
            Vec3UnpackUnitVec(source.normal, normals[vertex]);
        }
        for (int point = 0; point < fragmentPointCount; ++point)
        {
            const WebWorldMarkPoint &clip = clipPoints[pingPong][point];
            FxMarkPoint &destination = info.points[info.usedPointCount + point];
            std::copy_n(clip.xyz, 3u, destination.xyz);
            for (std::size_t coordinate = 0u; coordinate < 2u; ++coordinate)
            {
                destination.lmapCoord[coordinate] =
                    clip.vertWeights[0] * lightmapCoordinates[0][coordinate] +
                    clip.vertWeights[1] * lightmapCoordinates[1][coordinate] +
                    clip.vertWeights[2] * lightmapCoordinates[2][coordinate];
            }
            for (std::size_t coordinate = 0u; coordinate < 3u; ++coordinate)
            {
                destination.normal[coordinate] =
                    clip.vertWeights[0] * normals[0][coordinate] +
                    clip.vertWeights[1] * normals[1][coordinate] +
                    clip.vertWeights[2] * normals[2][coordinate];
            }
        }
        info.usedPointCount += fragmentPointCount;
        anyMarks = true;
    }
    return true;
}

bool GenerateWorldBrushFragments(MarkInfo &info) noexcept
{
    if (!s_world.dpvs.surfaces || s_world.surfaceCount <= 0)
        return true;
    bool anyMarks = false;
    for (int surfaceIndex = 0; surfaceIndex < s_world.surfaceCount;
         ++surfaceIndex)
    {
        const GfxSurface &surface = s_world.dpvs.surfaces[surfaceIndex];
        if (!BoundsOverlap(surface.bounds, info.mins, info.maxs) ||
            !MaterialAllowsMark(surface.material, info.material))
            continue;
        if (!AppendBrushSurface(info, surface, anyMarks)) return false;
    }
    if (anyMarks)
    {
        info.callback(info.callbackContext, info.usedTriCount, info.tris,
            info.usedPointCount, info.points, info.origin, info.axis[1]);
        info.usedTriCount = 0;
        info.usedPointCount = 0;
    }
    return true;
}
} // namespace

void __cdecl R_MarkFragments_Begin(MarkInfo *info,
    MarkFragmentsAgainstEnum markAgainst, const float *origin,
    const float (*axis)[3], float radius, const float *viewOffset,
    Material *material)
{
    if (!info) return;
    std::memset(info, 0, sizeof(*info));
    if (!origin || !axis || !viewOffset || !material ||
        !std::isfinite(radius) || radius <= 0.0f)
        return;
    std::copy_n(origin, 3u, info->origin);
    std::copy_n(&axis[0][0], 9u, &info->axis[0][0]);
    std::copy_n(viewOffset, 3u, info->viewOffset);
    info->radius = radius;
    info->material = material;
    info->markHasLightmap = (material->info.gameFlags & 2u) != 0u;
    info->markHasReflection = (material->info.gameFlags & 0x10u) != 0u;
    info->markAgainst = markAgainst;
    GetFragmentBounds(info->origin, info->axis, radius,
        info->mins, info->maxs);
    GetFragmentClipPlanes(info->origin, info->axis, radius, info->planes);
}

char __cdecl R_MarkFragments_AddDObj(MarkInfo *info,
    DObj_s *object, cpose_t *pose, std::uint16_t entityIndex)
{
    if (!info || info->sceneDObjCollidedCount >= 32) return 0;
    info->sceneDObjsCollided[info->sceneDObjCollidedCount++] =
        {object, pose, entityIndex};
    return 1;
}

char __cdecl R_MarkFragments_AddBModel(MarkInfo *info,
    GfxBrushModel *brushModel, cpose_t *pose, std::uint16_t entityIndex)
{
    if (!info || info->sceneBModelCollidedCount >= 32) return 0;
    info->sceneBModelsCollided[info->sceneBModelCollidedCount++] =
        {brushModel, pose, entityIndex};
    return 1;
}

void __cdecl R_MarkFragments_Go(MarkInfo *info,
    void(__cdecl *callback)(void *, int, FxMarkTri *, int, FxMarkPoint *,
        const float *, const float *),
    void *callbackContext, int maxTris, FxMarkTri *tris,
    int maxPoints, FxMarkPoint *points)
{
    if (!info || !info->material || !callback || !tris || !points ||
        maxTris <= 0 || maxPoints <= 0)
        return;
    info->maxTris = maxTris;
    info->tris = tris;
    info->maxPoints = maxPoints;
    info->points = points;
    info->usedTriCount = 0;
    info->usedPointCount = 0;
    info->callback = callback;
    info->callbackContext = callbackContext;
    if (info->markAgainst == MARK_FRAGMENTS_AGAINST_BRUSHES)
        GenerateWorldBrushFragments(*info);
    // Attached DObj/static-model fragment clipping remains native-only in
    // this slice. EffectsCore still receives and owns every model impact FX;
    // only the persistent decal polygon is omitted for those receivers.
}
