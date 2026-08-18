#pragma once

#include <cstdint>
#include "gfx_color_types.h"
#include "gfx_placement_types.h"

struct GfxParticleCloud
{
    GfxScaledPlacement placement;
    float endpos[3];
    GfxColor color;
    float radius[2];
    std::uint32_t pad[2];
};
