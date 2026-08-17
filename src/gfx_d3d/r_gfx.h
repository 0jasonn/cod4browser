#pragma once

#include <d3d9.h>

#include <gfx_d3d/gfx_draw_surf_types.h>
#include <gfx_d3d/gfx_image_types.h>
#include <gfx_d3d/gfx_light_types.h>
#include <universal/com_math.h>
#include <qcommon/com_pack.h>
#include <cstdint>

struct Material;
struct XModel;

#define NULL_VERTEX_BUFFER 0

struct srfTriangles_t // sizeof=0x10
{                                       // ...
    int vertexLayerData;
    int firstVertex;
    uint16_t vertexCount;
    uint16_t triCount;
    int baseIndex;
};

union GfxColor // sizeof=0x4
{                                       // ...
    operator uint32_t()
    {
        return packed;
    }
    GfxColor()
    {
        packed = 0;
    }
    GfxColor(int i)
    {
        packed = i;
    }
    GfxColor(uint32_t i)
    {
        packed = i;
    }
    uint32_t packed;
    uint8_t array[4];
};

struct GfxPackedVertex // sizeof=0x20
{                                       // ...
    float xyz[3];
    float binormalSign;
    GfxColor color;
    PackedTexCoords texCoord;
    PackedUnitVec normal;
    PackedUnitVec tangent;
};
static_assert(sizeof(GfxPackedVertex) == 32);

struct GfxPackedVertexNormal // sizeof=0x8
{                                       // ...
    PackedUnitVec normal;
    PackedUnitVec tangent;
};

struct GfxDynamicIndices // sizeof=0xC
{                                       // ...
    volatile int used;
    int total;
    uint16_t* indices;          // ...
};

struct GfxVertexBufferState // sizeof=0x10
{                                       // ...
    volatile uint32_t used;
    int total;
    IDirect3DVertexBuffer9* buffer;     // ...
    uint8_t* verts;
};

struct GfxMeshData // sizeof=0x20
{                                       // ...
    uint32_t indexCount;
    uint32_t totalIndexCount;
    uint16_t* indices;
    GfxVertexBufferState vb;
    uint32_t vertSize;
};


struct GfxMatrix // sizeof=0x40
{                                       // ...
    float m[4][4];                      // ...
};

struct GfxPlacement // sizeof=0x1C
{                                       // ...
    float quat[4];                      // ...
    float origin[3];                    // ...
};

struct GfxScaledPlacement // sizeof=0x20
{                                       // ...
    GfxPlacement base;                  // ...
    float scale;                        // ...
};

struct GfxViewParms // sizeof=0x140
{                                       // ...
    GfxMatrix viewMatrix;
    GfxMatrix projectionMatrix;         // ...
    GfxMatrix viewProjectionMatrix;     // ...
    GfxMatrix inverseViewProjectionMatrix; // ...
    float origin[4];                    // ...
    float axis[3][3];                   // ...
    float depthHackNearClip;
    float zNear;
    int pad;
};

struct GfxIndexBufferState // sizeof=0x10
{                                       // ...
    volatile int used;
    int total;
    IDirect3DIndexBuffer9* buffer;      // ...
    uint16_t* indices;
};

enum GfxPrimStatsTarget : __int32
{                                       // ...
    GFX_PRIM_STATS_WORLD = 0x0,
    GFX_PRIM_STATS_SMODELCACHED = 0x1,
    GFX_PRIM_STATS_SMODELRIGID = 0x2,
    GFX_PRIM_STATS_XMODELRIGID = 0x3,
    GFX_PRIM_STATS_XMODELSKINNED = 0x4,
    GFX_PRIM_STATS_BMODEL = 0x5,
    GFX_PRIM_STATS_FX = 0x6,
    GFX_PRIM_STATS_HUD = 0x7,
    GFX_PRIM_STATS_DEBUG = 0x8,
    GFX_PRIM_STATS_CODE = 0x9,
    GFX_PRIM_STATS_COUNT = 0xA,
};

enum GfxViewMode : __int32
{                                       // ...
    VIEW_MODE_NONE = 0x0,
    VIEW_MODE_3D = 0x1,
    VIEW_MODE_2D = 0x2,
    VIEW_MODE_IDENTITY = 0x3,
};

enum GfxViewportBehavior : __int32
{                                       // ...
    GFX_USE_VIEWPORT_FOR_VIEW = 0x0,    // ...
    GFX_USE_VIEWPORT_FULL = 0x1,    // ...
};

enum GfxDepthRangeType : __int32
{                                       // ...
    GFX_DEPTH_RANGE_SCENE = 0x0,
    GFX_DEPTH_RANGE_VIEWMODEL = 0x2,
    GFX_DEPTH_RANGE_FULL = -0x1,
};

struct GfxCodeMatrices // sizeof=0x800
{                                       // ...
    GfxMatrix matrix[32];
};

struct GfxBuffers // sizeof=0x2400A0
{                                       // ...
    GfxDynamicIndices smodelCache;      // ...
    IDirect3DVertexBuffer9* smodelCacheVb; // ...
    GfxIndexBufferState preTessIndexBufferPool[2]; // ...
    GfxIndexBufferState* preTessIndexBuffer; // ...
    int preTessBufferFrame;             // ...
    GfxIndexBufferState dynamicIndexBufferPool[1]; // ...
    GfxIndexBufferState* dynamicIndexBuffer; // ...
    GfxVertexBufferState skinnedCacheVbPool[2]; // ...
    uint8_t* skinnedCacheLockAddr; // ...
    GfxVertexBufferState dynamicVertexBufferPool[1]; // ...
    GfxVertexBufferState* dynamicVertexBuffer; // ...
    IDirect3DVertexBuffer9* particleCloudVertexBuffer; // ...
    IDirect3DIndexBuffer9* particleCloudIndexBuffer; // ...
    int dynamicBufferFrame;             // ...
    GfxPackedVertexNormal skinnedCacheNormals[2][147456]; // ...
    GfxPackedVertexNormal* skinnedCacheNormalsAddr; // ...
    GfxPackedVertexNormal* oldSkinnedCacheNormalsAddr; // ...
    uint32_t skinnedCacheNormalsFrameCount; // ...
    bool fastSkin;                      // ...
    bool skinCache;                     // ...
    // padding byte
    // padding byte
};

struct GfxPointVertex // sizeof=0x10
{                                       // ...
    float xyz[3];
    uint8_t color[4];           // ...
};


struct GfxStaticModelInst // sizeof=0x1C
{                                       // ...
    float mins[3];
    float maxs[3];
    GfxColor groundLighting;
};

struct GfxSurface // sizeof=0x30
{                                       // ...
    srfTriangles_t tris;
    Material* material;
    uint8_t lightmapIndex;
    uint8_t reflectionProbeIndex;
    uint8_t primaryLightIndex;
    uint8_t flags;
    float bounds[2][3];
};

struct GfxCullGroup // sizeof=0x20
{
    float mins[3];
    float maxs[3];
    int surfaceCount;
    int startSurfIndex;
};

struct GfxPackedPlacement // sizeof=0x34
{                                       // ...
    float origin[3];
    float axis[3][3];
    float scale;
};

struct GfxStaticModelDrawInst // sizeof=0x4C
{                                       // ...
    float cullDist;
    GfxPackedPlacement placement;
    XModel* model;
    uint16_t smodelCacheIndex[4];
    uint8_t reflectionProbeIndex;
    uint8_t primaryLightIndex;
    uint16_t lightingHandle;
    uint8_t flags;
    // padding byte
    // padding byte
    // padding byte
};

struct GfxDrawSurfList // sizeof=0x8
{                                       // ...
    GfxDrawSurf *current;               // ...
    GfxDrawSurf *end;                   // ...
};
struct GfxDelayedCmdBuf // sizeof=0x10
{                                       // ...
    int primDrawSurfPos;
    uint32_t primDrawSurfSize;
    GfxDrawSurf drawSurfKey;
};
struct GfxBspDrawSurfData // sizeof=0x18
{                                       // ...
    GfxDelayedCmdBuf delayedCmdBuf;
    GfxDrawSurfList drawSurfList;       // ...
};

struct GfxWorldDpvsStatic // sizeof=0x68
{                                       // ...
    uint32_t smodelCount;           // ...
    uint32_t staticSurfaceCount;    // ...
    uint32_t staticSurfaceCountNoDecal; // ...
    uint32_t litSurfsBegin;         // ...
    uint32_t litSurfsEnd;           // ...
    uint32_t decalSurfsBegin;       // ...
    uint32_t decalSurfsEnd;         // ...
    uint32_t emissiveSurfsBegin;    // ...
    uint32_t emissiveSurfsEnd;      // ...
    uint32_t smodelVisDataCount;    // ...
    uint32_t surfaceVisDataCount;   // ...
    uint8_t* smodelVisData[3];  // ...
    uint8_t* surfaceVisData[3]; // ... [1] = CSM Parition Near, [2] = CSM Parition Far
    uint32_t* lodData;              // ...
    uint16_t* sortedSurfIndex;  // ...
    GfxStaticModelInst* smodelInsts;    // ...
    GfxSurface* surfaces;               // ...
    GfxCullGroup* cullGroups;           // ...
    GfxStaticModelDrawInst* smodelDrawInsts; // ...
    GfxDrawSurf* surfaceMaterials;      // ...
    uint32_t* surfaceCastsSunShadow; // ...
    volatile int usageCount;
};
static_assert(sizeof(GfxWorldDpvsStatic) == 0x68);

using EntVisData = byte *[3];

struct GfxWorldDpvsDynamic // sizeof=0x30
{                                       // ...
    uint32_t dynEntClientWordCount[2]; // ...
    uint32_t dynEntClientCount[2];  // ...
    uint32_t* dynEntCellBits[2];    // ...
    EntVisData dynEntVisData[2]; // ...
};

struct GfxWorldStreamInfo // sizeof=0x0
{                                       // ...
};

struct GfxWorldVertex // sizeof=0x2C
{                                       // ...
    float xyz[3];
    float binormalSign;
    GfxColor color;
    float texCoord[2];
    float lmapCoord[2];
    PackedUnitVec normal;
    PackedUnitVec tangent;
};

struct GfxWorldVertexData // sizeof=0x8
{                                       // ...
    GfxWorldVertex* vertices;           // ...
    IDirect3DVertexBuffer9* worldVb;    // ...
};

struct GfxWorldVertexLayerData // sizeof=0x8
{                                       // ...
    uint8_t* data;              // ...
    IDirect3DVertexBuffer9* layerVb;    // ...
};

struct SunLightParseParams // sizeof=0x80
{                                       // ...
    char name[64];
    float ambientScale;
    float ambientColor[3];
    float diffuseFraction;
    float sunLight;
    float sunColor[3];
    float diffuseColor[3];
    bool diffuseColorHasBeenSet;
    // padding byte
    // padding byte
    // padding byte
    float angles[3];
};

struct GfxReflectionProbe // sizeof=0x10
{
    float origin[3];
    GfxImage* reflectionImage;
};

struct GfxWorldDpvsPlanes // sizeof=0x10
{                                       // ...
    int cellCount;                      // ...
    cplane_s* planes;                   // ...
    uint16_t* nodes;            // ...
    uint32_t* sceneEntCellBits;     // ...
};

struct GfxAabbTree // sizeof=0x2C
{
    float mins[3];
    float maxs[3];
    uint16_t childCount;
    uint16_t surfaceCount;
    uint16_t startSurfIndex;
    uint16_t surfaceCountNoDecal;
    uint16_t startSurfIndexNoDecal;
    uint16_t smodelIndexCount;
    uint16_t* smodelIndexes;
    int childrenOffset;
};

struct GfxPortal;

struct GfxPortalWritable // sizeof=0xC
{
    bool isQueued;
    bool isAncestor;
    uint8_t recursionDepth;
    uint8_t hullPointCount;
    float (*hullPoints)[2];
    GfxPortal* queuedParent;
};

struct DpvsPlane // sizeof=0x14
{                                       // ...
    float coeffs[4];                    // ...
    uint8_t side[3];            // ...
    uint8_t pad;
};

struct GfxCell;

struct GfxPortal // sizeof=0x44
{
    GfxPortalWritable writable;
    DpvsPlane plane;
    GfxCell* cell;
    float (*vertices)[3];
    uint8_t vertexCount;
    // padding byte
    // padding byte
    // padding byte
    float hullAxis[2][3];
};

struct GfxCell // sizeof=0x38
{
    float mins[3];
    float maxs[3];
    int aabbTreeCount;
    GfxAabbTree* aabbTree;
    int portalCount;
    GfxPortal* portals;
    int cullGroupCount;
    int* cullGroups;
    uint8_t reflectionProbeCount;
    // padding byte
    // padding byte
    // padding byte
    uint8_t* reflectionProbes;
};

struct GfxLightmapArray // sizeof=0x8
{
    GfxImage* primary;
    GfxImage* secondary;
};

struct GfxLightGridEntry // sizeof=0x4
{                                       // ...
    uint16_t colorsIndex;
    uint8_t primaryLightIndex;  // ...
    uint8_t needsTrace;
};

struct GfxLightGridRow // sizeof=0xC
{                                       // ...
    uint16_t colStart;          // ...
    uint16_t colCount;          // ...
    uint16_t zStart;            // ...
    uint16_t zCount;            // ...
    uint32_t firstEntry;            // ...
};

struct GfxLightGridColors // sizeof=0xA8
{                                       // ...
    uint8_t rgb[56][3];
};

struct GfxLightGrid // sizeof=0x38
{                                       // ...
    bool hasLightRegions;               // ...
    // padding byte
    // padding byte
    // padding byte
    uint32_t sunPrimaryLightIndex;  // ...
    uint16_t mins[3];           // ...
    uint16_t maxs[3];           // ...
    uint32_t rowAxis;               // ...
    uint32_t colAxis;               // ...
    uint16_t* rowDataStart;     // ...
    uint32_t rawRowDataSize;        // ...
    uint8_t* rawRowData;        // ...
    uint32_t entryCount;            // ...
    GfxLightGridEntry* entries;         // ...
    uint32_t colorCount;            // ...
    GfxLightGridColors* colors;         // ...
};

struct GfxBrushModelWritable // sizeof=0x18
{                                       // ...
    float mins[3];
    float maxs[3];
};

struct GfxBrushModel // sizeof=0x38
{
    GfxBrushModelWritable writable;
    float bounds[2][3];
    uint16_t surfaceCount;
    uint16_t startSurfIndex;
    uint16_t surfaceCountNoDecal;
    // padding byte
    // padding byte
};

struct DiskBrushModel // sizeof=0x30
{
    float mins[3];
    float maxs[3];
    uint16_t firstTriSoup[2];
    uint16_t triSoupCount[2];
    int firstSurface;
    int numSurfaces;
    int firstBrush;
    int numBrushes;
};

struct GfxFog // sizeof=0x14
{                                       // ...
    int startTime;                      // ...
    int finishTime;                     // ...
    GfxColor color;                     // ...
    float fogStart;                     // ...
    float density;                      // ...
};

struct GfxVertex // sizeof=0x20
{                                       // ...
    float xyzw[4];
    GfxColor color;                     // ...
    float texCoord[2];                  // ...
    PackedUnitVec normal;               // ...
};

struct GfxDepthOfField // sizeof=0x20
{                                       // ...
    float viewModelStart;
    float viewModelEnd;
    float nearStart;
    float nearEnd;
    float farStart;
    float farEnd;
    float nearBlur;
    float farBlur;
};
struct GfxFilm // sizeof=0x2C
{                                       // ...
    bool enabled;
    // padding byte
    // padding byte
    // padding byte
    float brightness;
    float contrast;
    float desaturation;
    bool invert;
    // padding byte
    // padding byte
    // padding byte
    float tintDark[3];
    float tintLight[3];
};
struct GfxGlow // sizeof=0x14
{                                       // ...
    bool enabled;
    // padding byte
    // padding byte
    // padding byte
    float bloomCutoff;
    float bloomDesaturation;
    float bloomIntensity;
    float radius;
};

struct GfxQuadMeshData // sizeof=0x30
{                                       // ...
    float x;
    float y;
    float width;
    float height;
    GfxMeshData meshData;               // ...
};

struct GfxEntity // sizeof=0x8
{                                       // ...
    uint32_t renderFxFlags;
    float materialTime;
};

struct GfxSceneDef // sizeof=0x14
{                                       // ...
    int time;                           // ...
    float floatTime;                    // ...
    float viewOffset[3];                // ...
};

struct GfxViewport // sizeof=0x10
{                                       // ...
    int x;                              // ...
    int y;                              // ...
    int width;                          // ...
    int height;                         // ...
};

struct refdef_s // sizeof=0x4098
{                                       // ...
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    float tanHalfFovX;
    float tanHalfFovY;
    float vieworg[3];
    float viewaxis[3][3];
    float viewOffset[3];
    int time;
    float zNear;
    float blurRadius;
    GfxDepthOfField dof;
    GfxFilm film;
    GfxGlow glow;
    GfxLight primaryLights[255];
    GfxViewport scissorViewport;
    bool useScissorViewport;
    // padding byte
    // padding byte
    // padding byte
    int localClientNum;
};

struct GfxPosTexVertex // sizeof=0x14
{                                       // ...
    float xyz[3];
    float texCoord[2];
};
