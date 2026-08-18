#pragma once

#include <cstdint>

#include <xanim/dobj_types.h>
struct XSurface;
struct Material;
struct PhysPreset;
struct PhysGeomList;

#ifndef MAX_LODS
#define MAX_LODS 4
#endif

enum XModelLodRampType : std::int32_t
{
    XMODEL_LOD_RAMP_RIGID = 0x0,
    XMODEL_LOD_RAMP_SKINNED = 0x1,
    XMODEL_LOD_RAMP_COUNT = 0x2,
};

struct XModelLodInfo // sizeof=0x1C
{
    float dist;
    std::uint16_t numsurfs;
    std::uint16_t surfIndex;
    int partBits[4];
    std::uint8_t lod;
    std::uint8_t smcIndexPlusOne;
    std::uint8_t smcAllocBits;
    std::uint8_t unused;
};

struct XModelCollTri_s // sizeof=0x30
{
    float plane[4];
    float svec[4];
    float tvec[4];
};

struct XBoneInfo // sizeof=0x28
{
    float bounds[2][3];
    float offset[3];
    float radiusSquared;
};

struct XModelCollSurf_s // sizeof=0x2C on IW3
{
    XModelCollTri_s *collTris;
    int numCollTris;
    float mins[3];
    float maxs[3];
    int boneIdx;
    int contents;
    int surfFlags;
};

struct XModelStreamInfo
{
};

struct XModel // IW3 size: 0xDC
{
    const char *name;
    std::uint8_t numBones;
    std::uint8_t numRootBones;
    std::uint8_t numsurfs;
    std::uint8_t lodRampType;
    std::uint16_t *boneNames;
    std::uint8_t *parentList;
    std::int16_t *quats;
    float *trans;
    std::uint8_t *partClassification;
    DObjAnimMat *baseMat;
    XSurface *surfs;
    Material **materialHandles;
    XModelLodInfo lodInfo[MAX_LODS];
    XModelCollSurf_s *collSurfs;
    int numCollSurfs;
    int contents;
    XBoneInfo *boneInfo;
    float radius;
    float mins[3];
    float maxs[3];
    std::int16_t numLods;
    std::int16_t collLod;
    XModelStreamInfo streamInfo;
    int memUsage;
    std::uint8_t flags;
    bool bad;
    std::uint8_t padding[2];
    PhysPreset *physPreset;
    PhysGeomList *physGeoms;
};

static_assert(sizeof(XModelLodInfo) == 28u);
static_assert(sizeof(XModelCollTri_s) == 48u);
static_assert(sizeof(XBoneInfo) == 40u);
static_assert(sizeof(void *) != 4u || sizeof(XModelCollSurf_s) == 44u);
static_assert(sizeof(void *) != 4u || sizeof(XModel) == 220u);
