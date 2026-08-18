#pragma once

#include <cstdint>

struct AimTarget
{
    std::int32_t entIndex;
    float worldDistSqr;
    float mins[3];
    float maxs[3];
    float velocity[3];
};
static_assert(sizeof(AimTarget) == 0x2C);

struct AimTargetGlob
{
    AimTarget targets[64];
    std::int32_t targetCount;
    AimTarget clientTargets[64];
    std::int32_t clientTargetCount;
};
static_assert(sizeof(AimTargetGlob) == 0x1608);
