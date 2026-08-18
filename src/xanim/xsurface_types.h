#pragma once

#include <gfx_d3d/gfx_packed_vertex_types.h>

#include <cstdint>

struct XSurfaceCollisionAabb { std::uint16_t mins[3]; std::uint16_t maxs[3]; };
struct XSurfaceCollisionNode
{
    XSurfaceCollisionAabb aabb;
    std::uint16_t childBeginIndex;
    std::uint16_t childCount;
};
struct XSurfaceCollisionLeaf { std::uint16_t triangleBeginIndex; };
struct XSurfaceCollisionTree
{
    float trans[3];
    float scale[3];
    std::uint32_t nodeCount;
    XSurfaceCollisionNode *nodes;
    std::uint32_t leafCount;
    XSurfaceCollisionLeaf *leafs;
};
struct XRigidVertList
{
    std::uint16_t boneOffset;
    std::uint16_t vertCount;
    std::uint16_t triOffset;
    std::uint16_t triCount;
    XSurfaceCollisionTree *collisionTree;
};
struct XSurfaceVertexInfo
{
    std::int16_t vertCount[4];
    std::uint16_t *vertsBlend;
};
struct XSurface
{
    std::uint8_t tileMode;
    bool deformed;
    std::uint16_t vertCount;
    std::uint16_t triCount;
    std::uint8_t zoneHandle;
    std::uint8_t pad;
    std::uint16_t baseTriIndex;
    std::uint16_t baseVertIndex;
    std::uint16_t *triIndices;
    XSurfaceVertexInfo vertInfo;
    GfxPackedVertex *verts0;
    std::uint32_t vertListCount;
    XRigidVertList *vertList;
    int partBits[4];
};

static_assert(sizeof(XSurfaceCollisionAabb) == 12u);
static_assert(sizeof(void *) != 4u || sizeof(XSurfaceCollisionNode) == 16u);
static_assert(sizeof(XSurfaceCollisionLeaf) == 2u);
static_assert(sizeof(void *) != 4u || sizeof(XSurfaceCollisionTree) == 40u);
static_assert(sizeof(void *) != 4u || sizeof(XRigidVertList) == 12u);
static_assert(sizeof(void *) != 4u || sizeof(XSurfaceVertexInfo) == 12u);
static_assert(sizeof(void *) != 4u || sizeof(XSurface) == 56u);
