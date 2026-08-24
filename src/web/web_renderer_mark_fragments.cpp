// Browser renderer boundary for canonical EffectsCore impact-mark fragments.
// This is the world-brush, entity-brush, static-XModel, and rigid animated
// DObj subset of gfx_d3d/r_marks.cpp adapted to consume the published scene
// directly. EffectsCore still owns mark allocation, lifetime, material choice,
// vertex expansion, and draw generation.

#include <EffectsCore/fx_system.h>
#include <bgame/bg_local.h>
#include <cgame/cg_pose.h>
#include <gfx_d3d/material_types.h>
#include <gfx_d3d/r_bsp.h>
#include <universal/com_math.h>
#include <web/web_renderer_mark_fragments.h>
#include <xanim/dobj_utils.h>
#include <xanim/xmodel_types.h>
#include <xanim/xsurface_types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

extern GfxWorld s_world;

void __cdecl DObjSkelMatToMatrix43(
    const DObjSkelMat *input, float (*output)[3])
{
    iassert(input && output);
    for (std::size_t row = 0u; row < 3u; ++row)
        std::copy_n(input->axis[row], 3u, output[row]);
    std::copy_n(input->origin, 3u, output[3]);
}

void __cdecl R_MarkUtil_GetDObjAnimMatAndHideParts(
    const DObj_s *object,
    const cpose_t *pose,
    const DObjAnimMat **boneMatrices,
    std::uint32_t *hidePartBits)
{
    iassert(object && pose && boneMatrices && hidePartBits);
    int partBits[4] = {-1, -1, -1, -1};
    *boneMatrices = CG_DObjCalcPose(pose, object, partBits);
    iassert(*boneMatrices);
    DObjGetHidePartBits(object, hidePartBits);
}

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

bool BoundsOverlapRanges(const float lhsMins[3], const float lhsMaxs[3],
    const float rhsMins[3], const float rhsMaxs[3]) noexcept
{
    for (std::size_t axis = 0u; axis < 3u; ++axis)
        if (lhsMaxs[axis] < rhsMins[axis] ||
            lhsMins[axis] > rhsMaxs[axis])
            return false;
    return true;
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
    const float clipPlanes[MARK_CLIP_PLANE_COUNT][4],
    const float markDirection[3], const GfxMarkContext &context,
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

    GfxMarkContext surfaceContext = context;
    surfaceContext.lmapIndex = surface.lightmapIndex;
    if (info.markHasLightmap != (surfaceContext.lmapIndex != 31u))
        return true;
    surfaceContext.reflectionProbeIndex = surface.reflectionProbeIndex;
    if (info.markHasReflection !=
        (surfaceContext.reflectionProbeIndex != 0u))
        return true;
    surfaceContext.primaryLightIndex = surface.primaryLightIndex;

    const GfxWorldVertex *vertices = s_world.vd.vertices + firstVertex;
    const std::uint16_t *indices = s_world.indices + firstIndex;
    for (std::uint32_t triangle = 0u;
         triangle < surface.tris.triCount; ++triangle, indices += 3u)
    {
        if (indices[0] >= surface.tris.vertexCount ||
            indices[1] >= surface.tris.vertexCount ||
            indices[2] >= surface.tris.vertexCount)
            continue;
        if (TriangleRejected(markDirection, vertices[indices[0]].xyz,
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
                clipPoints[pingPong ^ 1], clipPlanes[plane]);
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
            triangleOut.context = surfaceContext;
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
    std::uint32_t surfaceBegin = 0u;
    std::uint32_t surfaceEnd = static_cast<std::uint32_t>(
        s_world.surfaceCount);
    if (s_world.models && s_world.modelCount > 0)
    {
        surfaceBegin = s_world.models[0].startSurfIndex;
        surfaceEnd = surfaceBegin + s_world.models[0].surfaceCount;
        if (surfaceBegin > static_cast<std::uint32_t>(s_world.surfaceCount) ||
            surfaceEnd > static_cast<std::uint32_t>(s_world.surfaceCount))
            return true;
    }
    bool anyMarks = false;
    GfxMarkContext context{};
    for (std::uint32_t surfaceIndex = surfaceBegin;
         surfaceIndex < surfaceEnd;
         ++surfaceIndex)
    {
        const GfxSurface &surface = s_world.dpvs.surfaces[surfaceIndex];
        if (!BoundsOverlap(surface.bounds, info.mins, info.maxs) ||
            !MaterialAllowsMark(surface.material, info.material))
            continue;
        if (!AppendBrushSurface(info, surface, info.planes, info.axis[0],
                context, anyMarks))
            return false;
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

void TransformPlanesToPoseLocal(const float worldPlanes[6][4],
    const float poseAxis[3][3], const float poseOrigin[3],
    float localPlanes[6][4]) noexcept
{
    for (std::size_t plane = 0u; plane < 6u; ++plane)
    {
        for (std::size_t axis = 0u; axis < 3u; ++axis)
            localPlanes[plane][axis] =
                Vec3Dot(worldPlanes[plane], poseAxis[axis]);
        localPlanes[plane][3] = worldPlanes[plane][3] -
            Vec3Dot(worldPlanes[plane], poseOrigin);
    }
}

void TransformDirectionToPoseLocal(const float worldDirection[3],
    const float poseAxis[3][3], float localDirection[3]) noexcept
{
    for (std::size_t axis = 0u; axis < 3u; ++axis)
        localDirection[axis] = Vec3Dot(worldDirection, poseAxis[axis]);
}

void TransformPointToPoseLocal(const float worldPoint[3],
    const float poseAxis[3][3], const float poseOrigin[3],
    float localPoint[3]) noexcept
{
    float offset[3]{};
    Vec3Sub(worldPoint, poseOrigin, offset);
    TransformDirectionToPoseLocal(offset, poseAxis, localPoint);
}

bool GenerateEntityBrushFragments(MarkInfo &info) noexcept
{
    if (!s_world.dpvs.surfaces || s_world.surfaceCount <= 0)
        return true;
    for (int collision = 0;
         collision < info.sceneBModelCollidedCount; ++collision)
    {
        const MarkInfoCollidedBModel &collided =
            info.sceneBModelsCollided[collision];
        if (!collided.brushModel || !collided.pose) continue;
        const std::uint32_t surfaceBegin =
            collided.brushModel->startSurfIndex;
        const std::uint32_t surfaceEnd = surfaceBegin +
            collided.brushModel->surfaceCount;
        if (surfaceBegin > static_cast<std::uint32_t>(s_world.surfaceCount) ||
            surfaceEnd > static_cast<std::uint32_t>(s_world.surfaceCount))
            continue;

        float poseAxis[3][3]{};
        AnglesToAxis(collided.pose->angles, poseAxis);
        float localPlanes[6][4]{};
        TransformPlanesToPoseLocal(info.planes, poseAxis,
            collided.pose->origin, localPlanes);
        float localMarkDirection[3]{};
        TransformDirectionToPoseLocal(
            info.axis[0], poseAxis, localMarkDirection);
        GfxMarkContext context{};
        context.modelTypeAndSurf = 0x80u;
        context.modelIndex = collided.entnum;
        bool anyMarks = false;
        for (std::uint32_t surfaceIndex = surfaceBegin;
             surfaceIndex < surfaceEnd; ++surfaceIndex)
        {
            const GfxSurface &surface =
                s_world.dpvs.surfaces[surfaceIndex];
            if (!MaterialAllowsMark(surface.material, info.material))
                continue;
            if (!AppendBrushSurface(info, surface, localPlanes,
                    localMarkDirection, context, anyMarks))
                return false;
        }
        if (!anyMarks) continue;
        float localOrigin[3]{};
        float localTexCoordAxis[3]{};
        TransformPointToPoseLocal(info.origin, poseAxis,
            collided.pose->origin, localOrigin);
        TransformDirectionToPoseLocal(
            info.axis[1], poseAxis, localTexCoordAxis);
        info.callback(info.callbackContext, info.usedTriCount, info.tris,
            info.usedPointCount, info.points, localOrigin,
            localTexCoordAxis);
        info.usedTriCount = 0;
        info.usedPointCount = 0;
    }
    return true;
}

void TransformStaticModelPosition(const GfxPackedPlacement &placement,
    const float local[3], float world[3]) noexcept
{
    for (std::size_t component = 0u; component < 3u; ++component)
    {
        world[component] = placement.origin[component] + placement.scale *
            (local[0] * placement.axis[0][component] +
             local[1] * placement.axis[1][component] +
             local[2] * placement.axis[2][component]);
    }
}

void TransformStaticModelNormal(const GfxPackedPlacement &placement,
    PackedUnitVec packed, float world[3]) noexcept
{
    float local[3]{};
    Vec3UnpackUnitVec(packed, local);
    for (std::size_t component = 0u; component < 3u; ++component)
    {
        world[component] =
            local[0] * placement.axis[0][component] +
            local[1] * placement.axis[1][component] +
            local[2] * placement.axis[2][component];
    }
}

bool AppendStaticModelSurface(MarkInfo &info, const XSurface &surface,
    const GfxPackedPlacement &placement,
    const GfxMarkContext &context) noexcept
{
    if (!surface.verts0 || !surface.triIndices || surface.deformed ||
        surface.vertCount == 0u || surface.triCount == 0u)
        return true;
    for (std::uint32_t triangle = 0u;
         triangle < surface.triCount; ++triangle)
    {
        const std::uint16_t *indices =
            surface.triIndices + triangle * 3u;
        if (indices[0] >= surface.vertCount ||
            indices[1] >= surface.vertCount ||
            indices[2] >= surface.vertCount)
            continue;
        WebWorldMarkPoint clipPoints[2][MARK_CLIP_POINT_CAPACITY]{};
        float normals[3][3]{};
        for (std::size_t vertex = 0u; vertex < 3u; ++vertex)
        {
            const GfxPackedVertex &source = surface.verts0[indices[vertex]];
            TransformStaticModelPosition(
                placement, source.xyz, clipPoints[0][vertex].xyz);
            clipPoints[0][vertex].vertWeights[vertex] = 1.0f;
            TransformStaticModelNormal(
                placement, source.normal, normals[vertex]);
        }
        if (TriangleRejected(info.axis[0], clipPoints[0][0].xyz,
                clipPoints[0][1].xyz, clipPoints[0][2].xyz))
            continue;
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
        for (int point = 0; point < fragmentPointCount; ++point)
        {
            const WebWorldMarkPoint &clip = clipPoints[pingPong][point];
            FxMarkPoint &destination =
                info.points[info.usedPointCount + point];
            std::copy_n(clip.xyz, 3u, destination.xyz);
            destination.lmapCoord[0] = 0.0f;
            destination.lmapCoord[1] = 0.0f;
            for (std::size_t component = 0u; component < 3u; ++component)
            {
                destination.normal[component] =
                    clip.vertWeights[0] * normals[0][component] +
                    clip.vertWeights[1] * normals[1][component] +
                    clip.vertWeights[2] * normals[2][component];
            }
        }
        info.usedPointCount += fragmentPointCount;
    }
    return true;
}

void CollectStaticModelCollisions(MarkInfo &info) noexcept
{
    info.smodelCollidedCount = 0;
    if (!s_world.dpvs.smodelInsts || !s_world.dpvs.smodelDrawInsts)
        return;
    for (std::uint32_t index = 0u;
         index < s_world.dpvs.smodelCount &&
         info.smodelCollidedCount < 32; ++index)
    {
        if (index > (std::numeric_limits<std::uint16_t>::max)()) break;
        const GfxStaticModelInst &instance =
            s_world.dpvs.smodelInsts[index];
        if (!BoundsOverlapRanges(instance.mins, instance.maxs,
                info.mins, info.maxs))
            continue;
        info.smodelsCollided[info.smodelCollidedCount++] =
            static_cast<std::uint16_t>(index);
    }
}

bool GenerateStaticModelFragments(MarkInfo &info) noexcept
{
    if (!s_world.dpvs.smodelDrawInsts) return true;
    for (int collision = 0; collision < info.smodelCollidedCount;
         ++collision)
    {
        const std::uint32_t instanceIndex =
            info.smodelsCollided[collision];
        if (instanceIndex >= s_world.dpvs.smodelCount) continue;
        const GfxStaticModelDrawInst &instance =
            s_world.dpvs.smodelDrawInsts[instanceIndex];
        const XModel *model = instance.model;
        if (!model || !model->surfs || !model->materialHandles ||
            model->numLods <= 0)
            continue;
        const XModelLodInfo &lod = model->lodInfo[0];
        if (lod.surfIndex > model->numsurfs ||
            lod.numsurfs > model->numsurfs - lod.surfIndex)
            continue;
        GfxMarkContext context{};
        context.lmapIndex = 31u;
        context.reflectionProbeIndex = instance.reflectionProbeIndex;
        context.primaryLightIndex = instance.primaryLightIndex;
        context.modelIndex = static_cast<std::uint16_t>(instanceIndex);
        for (std::uint32_t surfaceIndex = 0u;
             surfaceIndex < lod.numsurfs && surfaceIndex <= 63u;
             ++surfaceIndex)
        {
            Material *material =
                model->materialHandles[lod.surfIndex + surfaceIndex];
            if (!MaterialAllowsMark(material, info.material)) continue;
            context.modelTypeAndSurf = static_cast<std::uint8_t>(
                0x40u | surfaceIndex);
            if (!AppendStaticModelSurface(info,
                    model->surfs[lod.surfIndex + surfaceIndex],
                    instance.placement, context))
                return false;
        }
        if (info.usedTriCount != 0 && info.usedPointCount != 0)
        {
            info.callback(info.callbackContext, info.usedTriCount, info.tris,
                info.usedPointCount, info.points, info.origin, info.axis[1]);
        }
        info.usedTriCount = 0;
        info.usedPointCount = 0;
    }
    return true;
}

void MultiplySkelMat(const DObjSkelMat &left, const DObjSkelMat &right,
    DObjSkelMat &output) noexcept
{
    for (std::size_t row = 0u; row < 3u; ++row)
    {
        for (std::size_t column = 0u; column < 3u; ++column)
        {
            output.axis[row][column] =
                left.axis[row][0] * right.axis[0][column] +
                left.axis[row][1] * right.axis[1][column] +
                left.axis[row][2] * right.axis[2][column];
        }
        output.axis[row][3] = 0.0f;
    }
    for (std::size_t column = 0u; column < 3u; ++column)
    {
        output.origin[column] =
            left.origin[0] * right.axis[0][column] +
            left.origin[1] * right.axis[1][column] +
            left.origin[2] * right.axis[2][column] +
            right.origin[column];
    }
    output.origin[3] = 1.0f;
}

void TransformPlanesToSkelLocal(const float worldPlanes[6][4],
    const DObjSkelMat &matrix, float localPlanes[6][4]) noexcept
{
    for (std::size_t plane = 0u; plane < 6u; ++plane)
    {
        for (std::size_t axis = 0u; axis < 3u; ++axis)
        {
            localPlanes[plane][axis] =
                worldPlanes[plane][0] * matrix.axis[axis][0] +
                worldPlanes[plane][1] * matrix.axis[axis][1] +
                worldPlanes[plane][2] * matrix.axis[axis][2];
        }
        localPlanes[plane][3] = worldPlanes[plane][3] -
            Vec3Dot(worldPlanes[plane], matrix.origin);
    }
}

void TransformDirectionToSkelLocal(const float worldDirection[3],
    const DObjSkelMat &matrix, float localDirection[3]) noexcept
{
    for (std::size_t axis = 0u; axis < 3u; ++axis)
    {
        localDirection[axis] =
            worldDirection[0] * matrix.axis[axis][0] +
            worldDirection[1] * matrix.axis[axis][1] +
            worldDirection[2] * matrix.axis[axis][2];
    }
}

void TransformPointToSkelLocal(const float worldPoint[3],
    const DObjSkelMat &matrix, float localPoint[3]) noexcept
{
    float offset[3]{};
    Vec3Sub(worldPoint, matrix.origin, offset);
    TransformDirectionToSkelLocal(offset, matrix, localPoint);
}

bool AppendDObjVertList(MarkInfo &info, const XSurface &surface,
    const XRigidVertList &vertList, const DObjSkelMat &skinMatrix,
    const GfxMarkContext &context) noexcept
{
    const std::uint32_t triangleBegin = vertList.triOffset;
    const std::uint32_t triangleEnd = triangleBegin + vertList.triCount;
    if (!surface.verts0 || !surface.triIndices || surface.deformed ||
        triangleBegin > surface.triCount || triangleEnd > surface.triCount)
        return true;

    float localPlanes[6][4]{};
    TransformPlanesToSkelLocal(info.planes, skinMatrix, localPlanes);
    float localMarkDirection[3]{};
    TransformDirectionToSkelLocal(
        info.axis[0], skinMatrix, localMarkDirection);
    for (std::uint32_t triangle = triangleBegin;
         triangle < triangleEnd; ++triangle)
    {
        const std::uint16_t *indices =
            surface.triIndices + triangle * 3u;
        if (indices[0] >= surface.vertCount ||
            indices[1] >= surface.vertCount ||
            indices[2] >= surface.vertCount)
            continue;
        WebWorldMarkPoint clipPoints[2][MARK_CLIP_POINT_CAPACITY]{};
        float normals[3][3]{};
        for (std::size_t vertex = 0u; vertex < 3u; ++vertex)
        {
            const GfxPackedVertex &source = surface.verts0[indices[vertex]];
            std::copy_n(source.xyz, 3u, clipPoints[0][vertex].xyz);
            clipPoints[0][vertex].vertWeights[vertex] = 1.0f;
            Vec3UnpackUnitVec(source.normal, normals[vertex]);
        }
        if (TriangleRejected(localMarkDirection, clipPoints[0][0].xyz,
                clipPoints[0][1].xyz, clipPoints[0][2].xyz))
            continue;
        int pingPong = 0;
        int fragmentPointCount = 3;
        for (int plane = 0; plane < MARK_CLIP_PLANE_COUNT; ++plane)
        {
            fragmentPointCount = ChopWorldPolyBehindPlane(
                fragmentPointCount, clipPoints[pingPong],
                clipPoints[pingPong ^ 1], localPlanes[plane]);
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
        for (int point = 0; point < fragmentPointCount; ++point)
        {
            const WebWorldMarkPoint &clip = clipPoints[pingPong][point];
            FxMarkPoint &destination =
                info.points[info.usedPointCount + point];
            std::copy_n(clip.xyz, 3u, destination.xyz);
            destination.lmapCoord[0] = 0.0f;
            destination.lmapCoord[1] = 0.0f;
            for (std::size_t component = 0u; component < 3u; ++component)
            {
                destination.normal[component] =
                    clip.vertWeights[0] * normals[0][component] +
                    clip.vertWeights[1] * normals[1][component] +
                    clip.vertWeights[2] * normals[2][component];
            }
        }
        info.usedPointCount += fragmentPointCount;
    }
    return true;
}

bool GenerateDObjFragments(MarkInfo &info) noexcept
{
    for (int collision = 0;
         collision < info.sceneDObjCollidedCount; ++collision)
    {
        const MarkInfoCollidedDObj &collided =
            info.sceneDObjsCollided[collision];
        DObj_s *object = collided.dObj;
        if (!object || !collided.pose || object->numModels == 0u ||
            object->numModels > DOBJ_MAX_SUBMODELS ||
            object->numBones > DOBJ_MAX_PARTS || !object->models)
            continue;

        char zeroLods[DOBJ_MAX_SUBMODELS]{};
        int posePartBits[DOBJ_MAX_PART_BITS]{};
        if (DObjGetSurfaces(object, posePartBits, zeroLods) <= 0)
            continue;
        DObjLock(object);
        DObjAnimMat *posedMats =
            CG_DObjCalcPose(collided.pose, object, posePartBits);
        DObjUnlock(object);
        if (!posedMats ||
            !DObjSkelAreBonesUpToDate(object, posePartBits))
            continue;
        std::uint32_t hideBits[DOBJ_MAX_PART_BITS]{};
        DObjGetHidePartBits(object, hideBits);

        std::uint32_t globalBoneOffset = 0u;
        for (std::uint32_t modelIndex = 0u;
             modelIndex < object->numModels && modelIndex <= 63u;
             ++modelIndex)
        {
            const XModel *model = object->models[modelIndex];
            if (!model || !model->surfs || !model->materialHandles ||
                !model->baseMat || model->numLods <= 0 ||
                globalBoneOffset + model->numBones > object->numBones)
            {
                break;
            }
            const XModelLodInfo &lod = model->lodInfo[0];
            if (lod.surfIndex > model->numsurfs ||
                lod.numsurfs > model->numsurfs - lod.surfIndex)
            {
                globalBoneOffset += model->numBones;
                continue;
            }
            GfxMarkContext context{};
            context.modelIndex = collided.entnum;
            context.modelTypeAndSurf = static_cast<std::uint8_t>(
                0xc0u | modelIndex);
            for (std::uint32_t localSurface = 0u;
                 localSurface < lod.numsurfs; ++localSurface)
            {
                const std::uint32_t surfaceIndex =
                    lod.surfIndex + localSurface;
                const XSurface &surface = model->surfs[surfaceIndex];
                if (surface.deformed || !surface.vertList ||
                    surface.vertListCount == 0u ||
                    !MaterialAllowsMark(
                        model->materialHandles[surfaceIndex], info.material))
                    continue;
                for (std::uint32_t listIndex = 0u;
                     listIndex < surface.vertListCount; ++listIndex)
                {
                    const XRigidVertList &vertList =
                        surface.vertList[listIndex];
                    if ((vertList.boneOffset % sizeof(DObjSkelMat)) != 0u)
                        continue;
                    const std::uint32_t boneIndex =
                        vertList.boneOffset / sizeof(DObjSkelMat);
                    if (boneIndex >= model->numBones ||
                        globalBoneOffset + boneIndex >= object->numBones)
                        continue;
                    const std::uint32_t hideBit = globalBoneOffset + boneIndex;
                    if ((hideBits[hideBit >> 5u] &
                            (0x80000000u >> (hideBit & 31u))) != 0u)
                        continue;
                    DObjSkelMat inverseBase{};
                    DObjSkelMat current{};
                    DObjAnimMat posedWithViewOffset =
                        posedMats[globalBoneOffset + boneIndex];
                    for (std::size_t axis = 0u; axis < 3u; ++axis)
                        posedWithViewOffset.trans[axis] += info.viewOffset[axis];
                    ConvertQuatToInverseSkelMat(
                        &model->baseMat[boneIndex], &inverseBase);
                    ConvertQuatToSkelMat(&posedWithViewOffset, &current);
                    DObjSkelMat skinMatrix{};
                    MultiplySkelMat(inverseBase, current, skinMatrix);
                    context.lmapIndex = static_cast<std::uint8_t>(boneIndex);
                    if (!AppendDObjVertList(info, surface, vertList,
                            skinMatrix, context))
                        return false;
                    if (info.usedTriCount == 0 || info.usedPointCount == 0)
                        continue;
                    float localOrigin[3]{};
                    float localTexCoordAxis[3]{};
                    TransformPointToSkelLocal(
                        info.origin, skinMatrix, localOrigin);
                    TransformDirectionToSkelLocal(
                        info.axis[1], skinMatrix, localTexCoordAxis);
                    info.callback(info.callbackContext, info.usedTriCount,
                        info.tris, info.usedPointCount, info.points,
                        localOrigin, localTexCoordAxis);
                    info.usedTriCount = 0;
                    info.usedPointCount = 0;
                }
            }
            globalBoneOffset += model->numBones;
        }
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
    if (markAgainst == MARK_FRAGMENTS_AGAINST_MODELS)
        CollectStaticModelCollisions(*info);
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
    {
        if (!GenerateWorldBrushFragments(*info) ||
            !GenerateEntityBrushFragments(*info))
            return;
    }
    else if (info->markAgainst == MARK_FRAGMENTS_AGAINST_MODELS)
    {
        if (!GenerateStaticModelFragments(*info) ||
            !GenerateDObjFragments(*info))
            return;
    }
}
