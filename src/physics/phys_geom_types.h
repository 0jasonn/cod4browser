#pragma once

#include <qcommon/cm_types.h>

#include <cstdint>

struct BrushWrapper
{
    float mins[3];
    int contents;
    float maxs[3];
    std::uint32_t numsides;
    cbrushside_t *sides;
    std::int16_t axialMaterialNum[2][3];
    std::uint8_t *baseAdjacentSide;
    std::int16_t firstAdjacentSideOffsets[2][3];
    std::uint8_t edgeCount[2][3];
    std::uint8_t pad[2];
    int totalEdgeCount;
    cplane_s *planes;
};

struct PhysMass
{
    float centerOfMass[3];
    float momentsOfInertia[3];
    float productsOfInertia[3];
};

struct PhysGeomInfo
{
    BrushWrapper *brush;
    int type;
    float orientation[3][3];
    float offset[3];
    float halfLengths[3];
};

struct PhysGeomList
{
    std::uint32_t count;
    PhysGeomInfo *geoms;
    PhysMass mass;
};

static_assert(sizeof(void *) != 4u || sizeof(BrushWrapper) == 80u);
static_assert(sizeof(PhysMass) == 36u);
static_assert(sizeof(void *) != 4u || sizeof(PhysGeomInfo) == 68u);
static_assert(sizeof(void *) != 4u || sizeof(PhysGeomList) == 44u);
