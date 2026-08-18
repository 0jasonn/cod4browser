#pragma once

#include <qcommon/packed_tex_coords.h>
#include <universal/packed_unit_vec.h>

#include <cstdint>

union GfxColor
{
    operator std::uint32_t() const { return packed; }
    GfxColor() : packed(0) {}
    GfxColor(int value) : packed(static_cast<std::uint32_t>(value)) {}
    GfxColor(std::uint32_t value) : packed(value) {}
    std::uint32_t packed;
    std::uint8_t array[4];
};

struct GfxPackedVertex
{
    float xyz[3];
    float binormalSign;
    GfxColor color;
    PackedTexCoords texCoord;
    PackedUnitVec normal;
    PackedUnitVec tangent;
};

struct GfxPackedVertexNormal
{
    PackedUnitVec normal;
    PackedUnitVec tangent;
};

static_assert(sizeof(GfxColor) == 4u);
static_assert(sizeof(GfxPackedVertex) == 32u);
static_assert(sizeof(GfxPackedVertexNormal) == 8u);
