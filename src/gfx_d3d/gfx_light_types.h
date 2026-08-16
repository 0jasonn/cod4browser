#pragma once

#include <gfx_d3d/gfx_image_types.h>

#include <cstdint>

struct GfxLightImage
{
    GfxImage *image;
    std::uint8_t samplerState;
    std::uint8_t padding[3];
};

struct GfxLightDef
{
    const char *name;
    GfxLightImage attenuation;
    int lmapLookupStart;
};

struct GfxLight
{
    std::uint8_t type;
    std::uint8_t canUseShadowMap;
    std::uint8_t unused[2];
    float color[3];
    float dir[3];
    float origin[3];
    float radius;
    float cosHalfFovOuter;
    float cosHalfFovInner;
    int exponent;
    std::uint32_t spotShadowIndex;
    GfxLightDef *def;
};

static_assert(sizeof(void *) != 4u || sizeof(GfxLightImage) == 8u);
static_assert(sizeof(void *) != 4u || sizeof(GfxLightDef) == 16u);
static_assert(sizeof(void *) != 4u || sizeof(GfxLight) == 64u);
