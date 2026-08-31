#pragma once
#include "gfx_dpvs_types.h"

#include <gfx_d3d/gfx_draw_surf_types.h>
#include <gfx_d3d/gfx_color_types.h>
#include <gfx_d3d/gfx_image_types.h>
#include <gfx_d3d/gfx_light_types.h>
#include <gfx_d3d/material_types.h>
#include <universal/cplane_types.h>
#include <universal/packed_unit_vec.h>

#include <cstddef>
#include <cstdint>

// Renderer-independent canonical KisakCOD/IW3 world database declarations.
// The serialized ABI is the original 32-bit layout.  GPU fields remain typed
// as opaque D3D interface pointers for native source compatibility, but the
// browser database loader always publishes them as null; WebGL resources are
// created later by the renderer adapter.
struct IDirect3DVertexBuffer9;
struct XModel;

struct srfTriangles_t
{
    int vertexLayerData;
    int firstVertex;
    std::uint16_t vertexCount;
    std::uint16_t triCount;
    int baseIndex;
};

struct GfxStaticModelInst
{
    float mins[3];
    float maxs[3];
    GfxColor groundLighting;
};

struct GfxSurface
{
    srfTriangles_t tris;
    Material *material;
    std::uint8_t lightmapIndex;
    std::uint8_t reflectionProbeIndex;
    std::uint8_t primaryLightIndex;
    std::uint8_t flags;
    float bounds[2][3];
};

struct GfxCullGroup
{
    float mins[3];
    float maxs[3];
    int surfaceCount;
    int startSurfIndex;
};

struct GfxPackedPlacement
{
    float origin[3];
    float axis[3][3];
    float scale;
};

struct GfxStaticModelDrawInst
{
    float cullDist;
    GfxPackedPlacement placement;
    XModel *model;
    std::uint16_t smodelCacheIndex[4];
    std::uint8_t reflectionProbeIndex;
    std::uint8_t primaryLightIndex;
    std::uint16_t lightingHandle;
    std::uint8_t flags;
    std::uint8_t padding[3];
};

struct GfxWorldDpvsStatic
{
    std::uint32_t smodelCount;
    std::uint32_t staticSurfaceCount;
    std::uint32_t staticSurfaceCountNoDecal;
    std::uint32_t litSurfsBegin;
    std::uint32_t litSurfsEnd;
    std::uint32_t decalSurfsBegin;
    std::uint32_t decalSurfsEnd;
    std::uint32_t emissiveSurfsBegin;
    std::uint32_t emissiveSurfsEnd;
    std::uint32_t smodelVisDataCount;
    std::uint32_t surfaceVisDataCount;
    std::uint8_t *smodelVisData[3];
    std::uint8_t *surfaceVisData[3];
    std::uint32_t *lodData;
    std::uint16_t *sortedSurfIndex;
    GfxStaticModelInst *smodelInsts;
    GfxSurface *surfaces;
    GfxCullGroup *cullGroups;
    GfxStaticModelDrawInst *smodelDrawInsts;
    GfxDrawSurf *surfaceMaterials;
    std::uint32_t *surfaceCastsSunShadow;
    volatile int usageCount;
};

using EntVisData = std::uint8_t *[3];

struct GfxWorldDpvsDynamic
{
    std::uint32_t dynEntClientWordCount[2];
    std::uint32_t dynEntClientCount[2];
    std::uint32_t *dynEntCellBits[2];
    EntVisData dynEntVisData[2];
};

struct GfxWorldStreamInfo
{
};

struct GfxWorldVertex
{
    float xyz[3];
    float binormalSign;
    GfxColor color;
    float texCoord[2];
    float lmapCoord[2];
    PackedUnitVec normal;
    PackedUnitVec tangent;
};

struct GfxWorldVertexData
{
    GfxWorldVertex *vertices;
    IDirect3DVertexBuffer9 *worldVb;
};

struct GfxWorldVertexLayerData
{
    std::uint8_t *data;
    IDirect3DVertexBuffer9 *layerVb;
};

struct SunLightParseParams
{
    char name[64];
    float ambientScale;
    float ambientColor[3];
    float diffuseFraction;
    float sunLight;
    float sunColor[3];
    float diffuseColor[3];
    bool diffuseColorHasBeenSet;
    std::uint8_t padding[3];
    float angles[3];
};

struct GfxReflectionProbe
{
    float origin[3];
    GfxImage *reflectionImage;
};

struct GfxWorldDpvsPlanes
{
    int cellCount;
    cplane_s *planes;
    std::uint16_t *nodes;
    std::uint32_t *sceneEntCellBits;
};

struct GfxAabbTree
{
    float mins[3];
    float maxs[3];
    std::uint16_t childCount;
    std::uint16_t surfaceCount;
    std::uint16_t startSurfIndex;
    std::uint16_t surfaceCountNoDecal;
    std::uint16_t startSurfIndexNoDecal;
    std::uint16_t smodelIndexCount;
    std::uint16_t *smodelIndexes;
    int childrenOffset;
};

struct GfxPortal;

struct GfxPortalWritable
{
    bool isQueued;
    bool isAncestor;
    std::uint8_t recursionDepth;
    std::uint8_t hullPointCount;
    float (*hullPoints)[2];
    GfxPortal *queuedParent;
};



struct GfxCell;

struct GfxPortal
{
    GfxPortalWritable writable;
    DpvsPlane plane;
    GfxCell *cell;
    float (*vertices)[3];
    std::uint8_t vertexCount;
    std::uint8_t padding[3];
    float hullAxis[2][3];
};

struct GfxCell
{
    float mins[3];
    float maxs[3];
    int aabbTreeCount;
    GfxAabbTree *aabbTree;
    int portalCount;
    GfxPortal *portals;
    int cullGroupCount;
    int *cullGroups;
    std::uint8_t reflectionProbeCount;
    std::uint8_t padding[3];
    std::uint8_t *reflectionProbes;
};

struct GfxLightmapArray
{
    GfxImage *primary;
    GfxImage *secondary;
};

struct GfxLightGridEntry
{
    std::uint16_t colorsIndex;
    std::uint8_t primaryLightIndex;
    std::uint8_t needsTrace;
};

struct GfxLightGridColors
{
    std::uint8_t rgb[56][3];
};

struct GfxLightGrid
{
    bool hasLightRegions;
    std::uint8_t padding[3];
    std::uint32_t sunPrimaryLightIndex;
    std::uint16_t mins[3];
    std::uint16_t maxs[3];
    std::uint32_t rowAxis;
    std::uint32_t colAxis;
    std::uint16_t *rowDataStart;
    std::uint32_t rawRowDataSize;
    std::uint8_t *rawRowData;
    std::uint32_t entryCount;
    GfxLightGridEntry *entries;
    std::uint32_t colorCount;
    GfxLightGridColors *colors;
};

struct GfxBrushModelWritable
{
    float mins[3];
    float maxs[3];
};

struct GfxBrushModel
{
    GfxBrushModelWritable writable;
    float bounds[2][3];
    std::uint16_t surfaceCount;
    std::uint16_t startSurfIndex;
    std::uint16_t surfaceCountNoDecal;
    std::uint8_t padding[2];
};

struct MaterialMemory
{
    Material *material;
    int memory;
};

struct sunflare_t
{
    bool hasValidData;
    std::uint8_t padding[3];
    Material *spriteMaterial;
    Material *flareMaterial;
    float spriteSize;
    float flareMinSize;
    float flareMinDot;
    float flareMaxSize;
    float flareMaxDot;
    float flareMaxAlpha;
    int flareFadeInTime;
    int flareFadeOutTime;
    float blindMinDot;
    float blindMaxDot;
    float blindMaxDarken;
    int blindFadeInTime;
    int blindFadeOutTime;
    float glareMinDot;
    float glareMaxDot;
    float glareMaxLighten;
    int glareFadeInTime;
    int glareFadeOutTime;
    float sunFxPosition[3];
};

struct GfxShadowGeometry
{
    std::uint16_t surfaceCount;
    std::uint16_t smodelCount;
    std::uint16_t *sortedSurfIndex;
    std::uint16_t *smodelIndex;
};

struct GfxLightRegionAxis
{
    float dir[3];
    float midPoint;
    float halfSize;
};

struct GfxLightRegionHull
{
    float kdopMidPoint[9];
    float kdopHalfSize[9];
    std::uint32_t axisCount;
    GfxLightRegionAxis *axis;
};

struct GfxLightRegion
{
    std::uint32_t hullCount;
    GfxLightRegionHull *hulls;
};

struct XModelDrawInfo
{
    std::uint16_t lod;
    std::uint16_t surfId;
};

struct GfxSceneDynModel
{
    XModelDrawInfo info;
    std::uint16_t dynEntId;
};

struct BModelDrawInfo
{
    std::uint16_t surfId;
};

struct GfxSceneDynBrush
{
    BModelDrawInfo info;
    std::uint16_t dynEntId;
};

struct GfxWorld
{
    const char *name;
    const char *baseName;
    int planeCount;
    int nodeCount;
    int indexCount;
    std::uint16_t *indices;
    int surfaceCount;
    GfxWorldStreamInfo streamInfo;
    std::uint8_t streamInfoPadding[3];
    int skySurfCount;
    int *skyStartSurfs;
    GfxImage *skyImage;
    std::uint8_t skySamplerState;
    std::uint8_t skyPadding[3];
    std::uint32_t vertexCount;
    GfxWorldVertexData vd;
    std::uint32_t vertexLayerDataSize;
    GfxWorldVertexLayerData vld;
    SunLightParseParams sunParse;
    GfxLight *sunLight;
    float sunColorFromBsp[3];
    std::uint32_t sunPrimaryLightIndex;
    std::uint32_t primaryLightCount;
    int cullGroupCount;
    std::uint32_t reflectionProbeCount;
    GfxReflectionProbe *reflectionProbes;
    GfxTexture *reflectionProbeTextures;
    GfxWorldDpvsPlanes dpvsPlanes;
    int cellBitsCount;
    GfxCell *cells;
    int lightmapCount;
    GfxLightmapArray *lightmaps;
    GfxLightGrid lightGrid;
    GfxTexture *lightmapPrimaryTextures;
    GfxTexture *lightmapSecondaryTextures;
    int modelCount;
    GfxBrushModel *models;
    float mins[3];
    float maxs[3];
    std::uint32_t checksum;
    int materialMemoryCount;
    MaterialMemory *materialMemory;
    sunflare_t sun;
    float outdoorLookupMatrix[4][4];
    GfxImage *outdoorImage;
    std::uint32_t *cellCasterBits;
    GfxSceneDynModel *sceneDynModel;
    GfxSceneDynBrush *sceneDynBrush;
    std::uint32_t *primaryLightEntityShadowVis;
    std::uint32_t *primaryLightDynEntShadowVis[2];
    std::uint8_t *nonSunPrimaryLightForModelDynEnt;
    GfxShadowGeometry *shadowGeom;
    GfxLightRegion *lightRegion;
    GfxWorldDpvsStatic dpvs;
    GfxWorldDpvsDynamic dpvsDyn;
};

static_assert(sizeof(void *) != 4u || sizeof(srfTriangles_t) == 16u);
static_assert(sizeof(void *) != 4u || sizeof(GfxStaticModelInst) == 28u);
static_assert(sizeof(void *) != 4u || sizeof(GfxSurface) == 48u);
static_assert(sizeof(void *) != 4u || sizeof(GfxCullGroup) == 32u);
static_assert(sizeof(void *) != 4u || sizeof(GfxStaticModelDrawInst) == 76u);
static_assert(sizeof(void *) != 4u || sizeof(GfxWorldDpvsStatic) == 104u);
static_assert(sizeof(void *) != 4u || sizeof(GfxWorldDpvsDynamic) == 48u);
static_assert(sizeof(GfxWorldVertex) == 44u);
static_assert(sizeof(void *) != 4u || sizeof(GfxWorldVertexData) == 8u);
static_assert(sizeof(void *) != 4u || sizeof(GfxWorldVertexLayerData) == 8u);
static_assert(sizeof(SunLightParseParams) == 128u);
static_assert(sizeof(void *) != 4u || sizeof(GfxReflectionProbe) == 16u);
static_assert(sizeof(void *) != 4u || sizeof(GfxWorldDpvsPlanes) == 16u);
static_assert(sizeof(void *) != 4u || sizeof(GfxAabbTree) == 44u);
static_assert(sizeof(void *) != 4u || sizeof(GfxPortal) == 68u);
static_assert(sizeof(void *) != 4u || sizeof(GfxCell) == 56u);
static_assert(sizeof(void *) != 4u || sizeof(GfxLightmapArray) == 8u);
static_assert(sizeof(GfxLightGridEntry) == 4u);
static_assert(sizeof(GfxLightGridColors) == 168u);
static_assert(sizeof(void *) != 4u || sizeof(GfxLightGrid) == 56u);
static_assert(sizeof(GfxBrushModel) == 56u);
static_assert(sizeof(void *) != 4u || sizeof(MaterialMemory) == 8u);
static_assert(sizeof(void *) != 4u || sizeof(sunflare_t) == 96u);
static_assert(sizeof(void *) != 4u || sizeof(GfxShadowGeometry) == 12u);
static_assert(sizeof(GfxLightRegionAxis) == 20u);
static_assert(sizeof(void *) != 4u || sizeof(GfxLightRegionHull) == 80u);
static_assert(sizeof(void *) != 4u || sizeof(GfxLightRegion) == 8u);
static_assert(sizeof(GfxSceneDynModel) == 6u);
static_assert(sizeof(GfxSceneDynBrush) == 4u);
static_assert(sizeof(void *) != 4u || sizeof(GfxWorld) == 732u);

static_assert(sizeof(void *) != 4u || offsetof(GfxWorld, indices) == 20u);
static_assert(sizeof(void *) != 4u || offsetof(GfxWorld, vd) == 52u);
static_assert(sizeof(void *) != 4u || offsetof(GfxWorld, dpvsPlanes) == 240u);
static_assert(sizeof(void *) != 4u || offsetof(GfxWorld, lightGrid) == 272u);
static_assert(sizeof(void *) != 4u || offsetof(GfxWorld, materialMemory) == 376u);
static_assert(sizeof(void *) != 4u || offsetof(GfxWorld, dpvs) == 580u);
static_assert(sizeof(void *) != 4u || offsetof(GfxWorld, dpvsDyn) == 684u);
