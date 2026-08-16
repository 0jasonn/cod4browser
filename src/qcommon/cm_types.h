#pragma once

#include <cstdint>

#include <universal/cplane_types.h>

struct XModel;
struct DynEntityDef;
struct DynEntityPose;
struct DynEntityClient;
struct DynEntityColl;

struct cStaticModelWritable { std::uint16_t nextModelInWorldSector; };
struct cStaticModel_s
{
    cStaticModelWritable writable;
    XModel *xmodel;
    float origin[3];
    float invScaledAxis[3][3];
    float absmin[3];
    float absmax[3];
};
struct dmaterial_t { char material[64]; int surfaceFlags; int contentFlags; };
struct cNode_t { cplane_s *plane; std::int16_t children[2]; };
struct cLeafBrushNodeLeaf_t { std::uint16_t *brushes; };
struct cLeafBrushNodeChildren_t
{
    float dist;
    float range;
    std::uint16_t childOffset[2];
};
union cLeafBrushNodeData_t
{
    cLeafBrushNodeLeaf_t leaf;
    cLeafBrushNodeChildren_t children;
};
struct cLeafBrushNode_s
{
    std::uint8_t axis;
    std::int16_t leafBrushCount;
    int contents;
    cLeafBrushNodeData_t data;
};
struct CollisionBorder
{
    float distEq[3];
    float zBase;
    float zSlope;
    float start;
    float length;
};
struct CollisionPartition
{
    std::uint8_t triCount;
    std::uint8_t borderCount;
    int firstTri;
    CollisionBorder *borders;
};
union CollisionAabbTreeIndex { int firstChildIndex; int partitionIndex; };
struct CollisionAabbTree
{
    float origin[3];
    float halfSize[3];
    std::uint16_t materialIndex;
    std::uint16_t childCount;
    CollisionAabbTreeIndex u;
};
struct cbrushside_t
{
    cplane_s *plane;
    std::uint32_t materialNum;
    std::int16_t firstAdjacentSideOffset;
    std::uint8_t edgeCount;
};
struct alignas(16) cbrush_t
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
};
struct MapEnts { const char *name; char *entityString; int numEntityChars; };
struct cLeaf_t
{
    std::uint16_t firstCollAabbIndex;
    std::uint16_t collAabbCount;
    int brushContents;
    int terrainContents;
    float mins[3];
    float maxs[3];
    int leafBrushNode;
    std::int16_t cluster;
};
struct cmodel_t
{
    float mins[3];
    float maxs[3];
    float radius;
    cLeaf_t leaf;
};
struct clipMap_t
{
    const char *name;
    int isInUse;
    int planeCount;
    cplane_s *planes;
    std::uint32_t numStaticModels;
    cStaticModel_s *staticModelList;
    std::uint32_t numMaterials;
    dmaterial_t *materials;
    std::uint32_t numBrushSides;
    cbrushside_t *brushsides;
    std::uint32_t numBrushEdges;
    std::uint8_t *brushEdges;
    std::uint32_t numNodes;
    cNode_t *nodes;
    std::uint32_t numLeafs;
    cLeaf_t *leafs;
    std::uint32_t leafbrushNodesCount;
    cLeafBrushNode_s *leafbrushNodes;
    std::uint32_t numLeafBrushes;
    std::uint16_t *leafbrushes;
    std::uint32_t numLeafSurfaces;
    std::uint32_t *leafsurfaces;
    std::uint32_t vertCount;
    float (*verts)[3];
    int triCount;
    std::uint16_t *triIndices;
    std::uint8_t *triEdgeIsWalkable;
    int borderCount;
    CollisionBorder *borders;
    int partitionCount;
    CollisionPartition *partitions;
    int aabbTreeCount;
    CollisionAabbTree *aabbTrees;
    std::uint32_t numSubModels;
    cmodel_t *cmodels;
    std::uint16_t numBrushes;
    cbrush_t *brushes;
    int numClusters;
    int clusterBytes;
    std::uint8_t *visibility;
    int vised;
    MapEnts *mapEnts;
    cbrush_t *box_brush;
    cmodel_t box_model;
    std::uint16_t dynEntCount[2];
    DynEntityDef *dynEntDefList[2];
    DynEntityPose *dynEntPoseList[2];
    DynEntityClient *dynEntClientList[2];
    DynEntityColl *dynEntCollList[2];
    std::uint32_t checksum;
};

#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(cStaticModel_s) == 0x50);
static_assert(sizeof(dmaterial_t) == 0x48);
static_assert(sizeof(cNode_t) == 0x8);
static_assert(sizeof(cLeafBrushNode_s) == 0x14);
static_assert(sizeof(CollisionBorder) == 0x1c);
static_assert(sizeof(CollisionPartition) == 0xc);
static_assert(sizeof(CollisionAabbTree) == 0x20);
static_assert(sizeof(cbrushside_t) == 0xc);
static_assert(sizeof(cbrush_t) == 0x50);
static_assert(sizeof(MapEnts) == 0xc);
static_assert(sizeof(cLeaf_t) == 0x2c);
static_assert(sizeof(cmodel_t) == 0x48);
static_assert(sizeof(clipMap_t) == 0x11c);
#endif
