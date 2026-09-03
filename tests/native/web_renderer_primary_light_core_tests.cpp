#include <universal/q_shared.h>
#include <gfx_d3d/r_primarylights_core.h>
#include <qcommon/com_world_types.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

namespace
{
struct TestAxis
{
    float dir[3]{};
    float midPoint = 0.0f;
    float halfSize = 0.0f;
};

struct TestHull
{
    float kdopMidPoint[9]{};
    float kdopHalfSize[9]{};
    std::uint32_t axisCount = 0u;
    TestAxis *axis = nullptr;
};

struct TestRegion
{
    std::uint32_t hullCount = 0u;
    TestHull *hulls = nullptr;
};

struct TestWorld
{
    std::uint32_t sunPrimaryLightIndex = 1u;
    std::uint32_t primaryLightCount = 5u;
    std::uint32_t *primaryLightEntityShadowVis = nullptr;
    std::uint32_t *primaryLightDynEntShadowVis[2]{};
    std::uint8_t *nonSunPrimaryLightForModelDynEnt = nullptr;
    TestRegion *lightRegion = nullptr;
};

void TestCanonicalLinkageAndRegions()
{
    constexpr std::uint32_t ENTITY_COUNT = 2208u;
    std::array<std::uint32_t, 256> entityBits{};
    std::array<std::uint32_t, 2> modelBits{};
    std::array<std::uint32_t, 2> brushBits{};
    std::array<std::uint8_t, 8> modelLights{};
    std::array<TestRegion, 5> regions{};
    TestHull rejectingHull{};
    std::fill_n(rejectingHull.kdopMidPoint, 9u, 100.0f);
    std::fill_n(rejectingHull.kdopHalfSize, 9u, 1.0f);
    regions[4] = {1u, &rejectingHull};

    TestWorld world{};
    world.primaryLightEntityShadowVis = entityBits.data();
    world.primaryLightDynEntShadowVis[0] = modelBits.data();
    world.primaryLightDynEntShadowVis[1] = brushBits.data();
    world.nonSunPrimaryLightForModelDynEnt = modelLights.data();
    world.lightRegion = regions.data();

    std::array<ComPrimaryLight, 5> lights{};
    lights[2].type = 2u;
    lights[2].radius = 64.0f;
    lights[2].cosHalfFovExpanded = -1.0f;
    lights[3].type = 3u;
    lights[3].origin[0] = 1000.0f;
    lights[3].radius = 8.0f;
    lights[4].type = 2u;
    lights[4].radius = 64.0f;
    lights[4].cosHalfFovExpanded = -1.0f;
    ComWorld common{nullptr, 1, 5u, lights.data()};

    const float origin[3]{0.0f, 0.0f, 0.0f};
    kisak::primary_lights::LinkSphereEntity(
        world, common, ENTITY_COUNT, 0u, 17u, origin, 4.0f);
    assert(kisak::primary_lights::IsEntityVisible(
        world, ENTITY_COUNT, 0u, 17u, 2u));
    assert(!kisak::primary_lights::IsEntityVisible(
        world, ENTITY_COUNT, 0u, 17u, 3u));
    assert(kisak::primary_lights::IsEntityVisible(
        world, ENTITY_COUNT, 0u, 17u, 4u));
    assert(kisak::primary_lights::EntityBit(
        world, ENTITY_COUNT, 0u, 17u, 2u) == 51u);
    kisak::primary_lights::UnlinkEntity(
        world, ENTITY_COUNT, 0u, 17u);
    assert(!kisak::primary_lights::IsEntityVisible(
        world, ENTITY_COUNT, 0u, 17u, 2u));

    const float mins[3]{-2.0f, -2.0f, -2.0f};
    const float maxs[3]{2.0f, 2.0f, 2.0f};
    kisak::primary_lights::LinkBoxEntity(
        world, common, ENTITY_COUNT, 0u, 18u, mins, maxs);
    assert(kisak::primary_lights::IsEntityVisible(
        world, ENTITY_COUNT, 0u, 18u, 2u));
    assert(!kisak::primary_lights::IsEntityVisible(
        world, ENTITY_COUNT, 0u, 18u, 4u));

    kisak::primary_lights::LinkDynEnt(
        world, common, 8u, 3u, 0u, mins, maxs);
    assert(kisak::primary_lights::IsDynEntVisible(
        world, 8u, 3u, 0u, 2u));
    assert(!kisak::primary_lights::IsDynEntVisible(
        world, 8u, 3u, 0u, 4u));
    assert(modelLights[3] == 2u);
    kisak::primary_lights::UnlinkDynEnt(world, 8u, 3u, 0u);
    assert(!kisak::primary_lights::IsDynEntVisible(
        world, 8u, 3u, 0u, 2u));
}
} // namespace

float __cdecl PointToBoxDistSq(
    const float *point, const float *mins, const float *maxs)
{
    float result = 0.0f;
    for (std::size_t component = 0u; component < 3u; ++component)
    {
        const float delta = point[component] < mins[component]
            ? mins[component] - point[component]
            : point[component] > maxs[component]
                ? point[component] - maxs[component] : 0.0f;
        result += delta * delta;
    }
    return result;
}

bool __cdecl CullBoxFromCone(const float *, const float *, float,
    const float *, const float *)
{
    return false;
}

bool __cdecl CullSphereFromCone(const float *, const float *, float,
    const float *, float)
{
    return false;
}

bool __cdecl CullBoxFromSphere(const float *origin, float radius,
    const float *center, const float *halfSize)
{
    const float mins[3]{center[0] - halfSize[0],
        center[1] - halfSize[1], center[2] - halfSize[2]};
    const float maxs[3]{center[0] + halfSize[0],
        center[1] + halfSize[1], center[2] + halfSize[2]};
    return PointToBoxDistSq(origin, mins, maxs) >= radius * radius;
}

bool __cdecl CullBoxFromConicSectionOfSphere(const float *origin,
    const float *, float, float radius, const float *center,
    const float *halfSize)
{
    return CullBoxFromSphere(origin, radius, center, halfSize);
}

int main()
{
    TestCanonicalLinkageAndRegions();
    return 0;
}
