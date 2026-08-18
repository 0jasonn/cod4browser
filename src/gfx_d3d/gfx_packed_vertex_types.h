#pragma once

#include <qcommon/packed_tex_coords.h>
#include <gfx_d3d/gfx_color_types.h>
#include <universal/packed_unit_vec.h>

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

static_assert(sizeof(GfxPackedVertex) == 32u);
static_assert(sizeof(GfxPackedVertexNormal) == 8u);
