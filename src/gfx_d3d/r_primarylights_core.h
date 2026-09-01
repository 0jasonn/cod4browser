#pragma once

#include <universal/com_math.h>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>

// Renderer-independent form of Kisak's native primary-light linkage for the
// browser frontend. World and light-region records are templates because the
// native renderer and portable DB declarations intentionally keep separate
// ABI-facing type headers.
namespace kisak::primary_lights
{
template <typename World>
constexpr std::uint32_t RelevantLightCount(const World &world) noexcept
{
    const std::uint32_t first = world.sunPrimaryLightIndex + 1u;
    return world.primaryLightCount > first
        ? world.primaryLightCount - first : 0u;
}

template <typename World>
constexpr std::uint32_t EntityBit(const World &world,
    std::uint32_t entityCount, std::uint32_t localClientNum,
    std::uint32_t entityNum, std::uint32_t primaryLightIndex) noexcept
{
    return primaryLightIndex - (world.sunPrimaryLightIndex + 1u) +
        RelevantLightCount(world) *
            (entityNum + entityCount * localClientNum);
}

template <typename World>
constexpr std::uint32_t DynEntBit(const World &world,
    std::uint32_t dynEntId, std::uint32_t primaryLightIndex) noexcept
{
    return primaryLightIndex - (world.sunPrimaryLightIndex + 1u) +
        RelevantLightCount(world) * dynEntId;
}

inline void SetBit(std::uint32_t *bits, std::uint32_t bit) noexcept
{
    bits[bit >> 5u] |= 1u << (bit & 31u);
}

inline void ClearBit(std::uint32_t *bits, std::uint32_t bit) noexcept
{
    bits[bit >> 5u] &= ~(1u << (bit & 31u));
}

inline bool TestBit(const std::uint32_t *bits, std::uint32_t bit) noexcept
{
    return (bits[bit >> 5u] & (1u << (bit & 31u))) != 0u;
}

template <typename Hull>
bool CullBoxFromLightRegionHull(const Hull &hull,
    const float boxMidPoint[3], const float boxHalfSize[3]) noexcept
{
    const float kdopMidPoint[9]{
        boxMidPoint[0], boxMidPoint[1], boxMidPoint[2],
        boxMidPoint[0] + boxMidPoint[1],
        boxMidPoint[0] - boxMidPoint[1],
        boxMidPoint[0] + boxMidPoint[2],
        boxMidPoint[0] - boxMidPoint[2],
        boxMidPoint[1] + boxMidPoint[2],
        boxMidPoint[1] - boxMidPoint[2]};
    const float kdopHalfSize[9]{
        boxHalfSize[0], boxHalfSize[1], boxHalfSize[2],
        boxHalfSize[0] + boxHalfSize[1],
        boxHalfSize[0] + boxHalfSize[1],
        boxHalfSize[0] + boxHalfSize[2],
        boxHalfSize[0] + boxHalfSize[2],
        boxHalfSize[1] + boxHalfSize[2],
        boxHalfSize[1] + boxHalfSize[2]};
    for (std::size_t axis = 0u; axis < 9u; ++axis)
        if (std::fabs(kdopMidPoint[axis] - hull.kdopMidPoint[axis]) >=
            kdopHalfSize[axis] + hull.kdopHalfSize[axis])
            return true;
    for (std::uint32_t axis = 0u; axis < hull.axisCount; ++axis)
    {
        const auto &candidate = hull.axis[axis];
        const float extent =
            boxHalfSize[0] * std::fabs(candidate.dir[0]) +
            boxHalfSize[1] * std::fabs(candidate.dir[1]) +
            boxHalfSize[2] * std::fabs(candidate.dir[2]);
        const float projected =
            boxMidPoint[0] * candidate.dir[0] +
            boxMidPoint[1] * candidate.dir[1] +
            boxMidPoint[2] * candidate.dir[2];
        if (std::fabs(projected - candidate.midPoint) >=
            extent + candidate.halfSize)
            return true;
    }
    return false;
}

template <typename Region>
bool CullBoxFromLightRegion(const Region *regions,
    std::uint32_t primaryLightIndex, const float lightOrigin[3],
    const float boxMidPoint[3], const float boxHalfSize[3]) noexcept
{
    if (!regions) return false;
    const Region &region = regions[primaryLightIndex];
    if (!region.hulls || region.hullCount == 0u) return false;
    const float relativeMidPoint[3]{
        boxMidPoint[0] - lightOrigin[0],
        boxMidPoint[1] - lightOrigin[1],
        boxMidPoint[2] - lightOrigin[2]};
    for (std::uint32_t hull = 0u; hull < region.hullCount; ++hull)
        if (!CullBoxFromLightRegionHull(
                region.hulls[hull], relativeMidPoint, boxHalfSize))
            return false;
    return true;
}

template <typename Light>
bool CullBoxFromPrimaryLight(const Light &light,
    const float boxMidPoint[3], const float boxHalfSize[3]) noexcept
{
    return light.type == 2u && light.cosHalfFovExpanded >= 0.0f
        ? CullBoxFromConicSectionOfSphere(light.origin, light.dir,
            light.cosHalfFovExpanded, light.radius,
            boxMidPoint, boxHalfSize)
        : CullBoxFromSphere(light.origin, light.radius,
            boxMidPoint, boxHalfSize);
}

template <typename World, typename CommonWorld>
void LinkSphereEntity(World &world, const CommonWorld &commonWorld,
    std::uint32_t entityCount, std::uint32_t localClientNum,
    std::uint32_t entityNum, const float origin[3], float radius) noexcept
{
    if (!world.primaryLightEntityShadowVis || !commonWorld.primaryLights ||
        !origin || entityCount == 0u || entityNum >= entityCount ||
        radius < 0.0f) return;
    const std::uint32_t first = world.sunPrimaryLightIndex + 1u;
    const std::uint32_t end = std::min(
        world.primaryLightCount, commonWorld.primaryLightCount);
    for (std::uint32_t lightIndex = first; lightIndex < end; ++lightIndex)
    {
        const auto &light = commonWorld.primaryLights[lightIndex];
        const float dx = origin[0] - light.origin[0];
        const float dy = origin[1] - light.origin[1];
        const float dz = origin[2] - light.origin[2];
        const float combinedRadius = light.radius + radius;
        if (dx * dx + dy * dy + dz * dz >=
                combinedRadius * combinedRadius ||
            (light.type == 2u && light.cosHalfFovExpanded >= 0.0f &&
                CullSphereFromCone(light.origin, light.dir,
                    light.cosHalfFovExpanded, origin, radius)))
            continue;
        SetBit(world.primaryLightEntityShadowVis,
            EntityBit(world, entityCount, localClientNum,
                entityNum, lightIndex));
    }
}

template <typename World, typename CommonWorld>
void LinkBoxEntity(World &world, const CommonWorld &commonWorld,
    std::uint32_t entityCount, std::uint32_t localClientNum,
    std::uint32_t entityNum, const float mins[3],
    const float maxs[3]) noexcept
{
    if (!world.primaryLightEntityShadowVis || !commonWorld.primaryLights ||
        !mins || !maxs || entityCount == 0u || entityNum >= entityCount)
        return;
    const float center[3]{
        (mins[0] + maxs[0]) * 0.5f,
        (mins[1] + maxs[1]) * 0.5f,
        (mins[2] + maxs[2]) * 0.5f};
    const float halfSize[3]{
        center[0] - mins[0], center[1] - mins[1],
        center[2] - mins[2]};
    const std::uint32_t first = world.sunPrimaryLightIndex + 1u;
    const std::uint32_t end = std::min(
        world.primaryLightCount, commonWorld.primaryLightCount);
    for (std::uint32_t lightIndex = first; lightIndex < end; ++lightIndex)
    {
        const auto &light = commonWorld.primaryLights[lightIndex];
        if (PointToBoxDistSq(light.origin, mins, maxs) >=
                light.radius * light.radius ||
            (light.type == 2u && light.cosHalfFovExpanded >= 0.0f &&
                CullBoxFromCone(light.origin, light.dir,
                    light.cosHalfFovExpanded, center, halfSize)) ||
            CullBoxFromLightRegion(world.lightRegion, lightIndex,
                light.origin, center, halfSize))
            continue;
        SetBit(world.primaryLightEntityShadowVis,
            EntityBit(world, entityCount, localClientNum,
                entityNum, lightIndex));
    }
}

template <typename World, typename CommonWorld>
void LinkDynEnt(World &world, const CommonWorld &commonWorld,
    std::uint32_t dynEntCount, std::uint32_t dynEntId,
    std::uint32_t drawType, const float mins[3],
    const float maxs[3]) noexcept
{
    if (drawType >= 2u || !world.primaryLightDynEntShadowVis[drawType] ||
        !commonWorld.primaryLights || !mins || !maxs ||
        dynEntId >= dynEntCount) return;
    const float center[3]{
        (mins[0] + maxs[0]) * 0.5f,
        (mins[1] + maxs[1]) * 0.5f,
        (mins[2] + maxs[2]) * 0.5f};
    const float halfSize[3]{
        center[0] - mins[0], center[1] - mins[1],
        center[2] - mins[2]};
    std::uint32_t bestLight = 0u;
    float bestDistance = (std::numeric_limits<float>::max)();
    const std::uint32_t first = world.sunPrimaryLightIndex + 1u;
    const std::uint32_t end = std::min(
        world.primaryLightCount, commonWorld.primaryLightCount);
    for (std::uint32_t lightIndex = first; lightIndex < end; ++lightIndex)
    {
        const auto &light = commonWorld.primaryLights[lightIndex];
        if (CullBoxFromPrimaryLight(light, center, halfSize) ||
            CullBoxFromLightRegion(world.lightRegion, lightIndex,
                light.origin, center, halfSize))
            continue;
        SetBit(world.primaryLightDynEntShadowVis[drawType],
            DynEntBit(world, dynEntId, lightIndex));
        const float dx = center[0] - light.origin[0];
        const float dy = center[1] - light.origin[1];
        const float dz = center[2] - light.origin[2];
        const float distance = dx * dx + dy * dy + dz * dz;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestLight = lightIndex;
        }
    }
    if (drawType == 0u && world.nonSunPrimaryLightForModelDynEnt)
        world.nonSunPrimaryLightForModelDynEnt[dynEntId] =
            static_cast<std::uint8_t>(bestLight);
}

template <typename World>
void UnlinkEntity(World &world, std::uint32_t entityCount,
    std::uint32_t localClientNum, std::uint32_t entityNum) noexcept
{
    if (!world.primaryLightEntityShadowVis || entityCount == 0u ||
        entityNum >= entityCount) return;
    for (std::uint32_t lightIndex = world.sunPrimaryLightIndex + 1u;
         lightIndex < world.primaryLightCount; ++lightIndex)
        ClearBit(world.primaryLightEntityShadowVis,
            EntityBit(world, entityCount, localClientNum,
                entityNum, lightIndex));
}

template <typename World>
void UnlinkDynEnt(World &world, std::uint32_t dynEntCount,
    std::uint32_t dynEntId, std::uint32_t drawType) noexcept
{
    if (drawType >= 2u || !world.primaryLightDynEntShadowVis[drawType] ||
        dynEntId >= dynEntCount) return;
    for (std::uint32_t lightIndex = world.sunPrimaryLightIndex + 1u;
         lightIndex < world.primaryLightCount; ++lightIndex)
        ClearBit(world.primaryLightDynEntShadowVis[drawType],
            DynEntBit(world, dynEntId, lightIndex));
}

template <typename World>
bool EntityVisibilityAvailable(const World &world,
    std::uint32_t primaryLightIndex) noexcept
{
    return world.primaryLightEntityShadowVis &&
        primaryLightIndex > world.sunPrimaryLightIndex &&
        primaryLightIndex < world.primaryLightCount;
}

template <typename World>
bool DynEntVisibilityAvailable(const World &world, std::uint32_t drawType,
    std::uint32_t primaryLightIndex) noexcept
{
    return drawType < 2u && world.primaryLightDynEntShadowVis[drawType] &&
        primaryLightIndex > world.sunPrimaryLightIndex &&
        primaryLightIndex < world.primaryLightCount;
}

template <typename World>
bool IsEntityVisible(const World &world, std::uint32_t entityCount,
    std::uint32_t localClientNum, std::uint32_t entityNum,
    std::uint32_t primaryLightIndex) noexcept
{
    return EntityVisibilityAvailable(world, primaryLightIndex) &&
        entityCount != 0u && entityNum < entityCount &&
        TestBit(world.primaryLightEntityShadowVis,
            EntityBit(world, entityCount, localClientNum,
                entityNum, primaryLightIndex));
}

template <typename World>
bool IsDynEntVisible(const World &world, std::uint32_t dynEntCount,
    std::uint32_t dynEntId, std::uint32_t drawType,
    std::uint32_t primaryLightIndex) noexcept
{
    return DynEntVisibilityAvailable(world, drawType, primaryLightIndex) &&
        dynEntId < dynEntCount &&
        TestBit(world.primaryLightDynEntShadowVis[drawType],
            DynEntBit(world, dynEntId, primaryLightIndex));
}
} // namespace kisak::primary_lights
