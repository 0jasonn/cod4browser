#pragma once

#include <gfx_d3d/gfx_light_types.h>
#include <algorithm>
#include <cmath>

// Shared native scene-light construction and selection. GfxLight remains the
// canonical frame payload; the platform backend owns only its GPU resources.
namespace kisak::dynamic_lights
{
inline void SetOmni(GfxLight &light, GfxLightDef *definition,
    const float origin[3], float radius, const float color[3])
{
    light = {};
    light.def = definition;
    light.type = 3;
    std::copy_n(origin, 3, light.origin);
    std::copy_n(color, 3, light.color);
    light.radius = radius;
    light.spotShadowIndex = UINT32_MAX;
}

inline float SetSpot(GfxLight &light, GfxLightDef *definition,
    const float origin[3], const float direction[3], float radius,
    const float color[3], float startRadius, float endRadius,
    float innerFraction, float brightness, bool shadows)
{
    const float outer = std::atan((endRadius - startRadius) / radius);
    const float inner = outer * innerFraction;
    const float offset = startRadius / std::tan(outer);
    SetOmni(light, definition, origin, radius + offset, color);
    light.type = 2;
    for (int axis = 0; axis < 3; ++axis)
    {
        light.dir[axis] = -direction[axis];
        light.origin[axis] += offset * light.dir[axis];
        light.color[axis] *= brightness;
    }
    light.exponent = 1;
    light.cosHalfFovInner = std::cos(inner);
    light.cosHalfFovOuter = std::cos(outer);
    light.canUseShadowMap = shadows;
    return offset;
}

inline bool ImportanceGreaterEqual(const GfxLight *a, const GfxLight *b,
    const float viewOrigin[3])
{
    if (a->type != b->type) return a->type == 2;
    float distanceA = 0, distanceB = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float da = viewOrigin[axis] - a->origin[axis];
        const float db = viewOrigin[axis] - b->origin[axis];
        distanceA += da * da;
        distanceB += db * db;
    }
    return b->radius * b->radius * distanceA <= a->radius * a->radius * distanceB;
}

// Preserve native partition order, including ties; sorting all lights would
// change the order of additive material passes unnecessarily.
inline void MostImportant(const GfxLight **lights, int count, int keep,
    const float viewOrigin[3])
{
    if (keep <= 0 || count <= keep) return;
    for (;;)
    {
        int bottom = 0, top = count;
        const GfxLight *pivot = lights[0];
        for (;;)
        {
            do { ++bottom; }
            while (bottom < top && ImportanceGreaterEqual(lights[bottom], pivot, viewOrigin));
            if (bottom > top) break;
            do { --top; }
            while (top >= bottom && ImportanceGreaterEqual(pivot, lights[top], viewOrigin));
            if (bottom > top) break;
            std::swap(lights[bottom], lights[top]);
        }
        if (bottom == count)
        {
            std::swap(lights[0], lights[top]);
            --bottom;
        }
        if (bottom == keep) return;
        if (bottom >= keep) count = bottom;
        else
        {
            lights += bottom;
            count -= bottom;
            keep -= bottom;
        }
    }
}
}
