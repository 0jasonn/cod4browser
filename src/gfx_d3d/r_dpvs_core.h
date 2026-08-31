#pragma once
#include "gfx_dpvs_types.h"

struct GfxWorld;
struct GfxAabbTree;

// Canonical DPVS state plus the native frontend dispatch/debug boundary.
// No device objects or scene-entity buffers are required by static visibility.
struct DpvsContext
{
    void DispatchCell(const GfxCell *, const DpvsPlane *, unsigned char, unsigned char);
    GfxWorld *world;
    DpvsGlobals &dpvsGlob;
    unsigned localClientNum;
    float farPlaneDist;
    bool drawSModels = true;
    bool drawWorld = false;
    unsigned char *g_smodelVisData = nullptr;
    unsigned char *g_surfaceVisData = nullptr;
    void (*dispatchCell)(const GfxCell *, const DpvsPlane *, unsigned char, unsigned char) = nullptr;
    void (*flushCells)() = nullptr;
    void (*debugBox)(const float *, const float *, const float *) = nullptr;
    void (*debugLine)(const float *, const float *, const float *) = nullptr;
    void (*debugPolygon)(const float *, int, float (*)[3]) = nullptr;

    char *__cdecl R_PortalAssertMsg();
    void __cdecl R_GetSidePlaneNormals(const float (*winding)[3], uint32_t vertexCount, float (*normals)[3]);
    GfxPortal *__cdecl R_NextQueuedPortal();
    int R_AssertValidQueue();
    void __cdecl R_EnqueuePortal(GfxPortal *portal);
    bool __cdecl R_ShouldSkipPortal(const GfxPortal *portal, const DpvsPlane *planes, int planeCount);
    void __cdecl R_AddVertToPortalHullPoints(GfxPortal *portal, const float *v);
    GfxHullPointsPool *__cdecl R_AllocHullPoints();
    int __cdecl R_ChopPortal(
    const GfxPortal *portal,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount,
    float (*v)[128][3],
    const float (**finalVerts)[3]);
    void __cdecl R_SetCellVisible(const GfxCell *cell);
    void __cdecl R_SetupWorldSurfacesDpvs(const GfxViewParms *viewParms);
    int __cdecl R_AddNearAndFarClipPlanes(DpvsPlane *planes, int planeCount);
    void __cdecl R_SetupDpvsForPoint(const GfxViewParms *viewParms);
    void __cdecl R_AddWorldSurfacesPortalWalk(int cameraCellIndex);
    void __cdecl R_VisitPortals(const GfxCell *cell, const DpvsPlane *parentPlane, const DpvsPlane *planes, int planeCount);
    uint32_t __cdecl R_PortalClipPlanes(
    DpvsPlane *planes,
    uint32_t vertexCount,
    const float (*winding)[3],
    GfxCell *cell,
    DpvsClipChildren *clipChildren);
    void __cdecl R_ProjectPortal(
    int vertexCount,
    const float (*winding)[3],
    float *mins,
    float *maxs,
    DpvsClipChildren *clipChildren);
    uint32_t __cdecl R_AddBevelPlanes(
    DpvsPlane *planes,
    uint32_t vertexCount,
    const float (*winding)[3],
    const float (*windingNormals)[3],
    float *mins,
    float *maxs,
    DpvsForceBevels forceBevels);
    void __cdecl R_UnprojectPoint(const float *projected, float *unprojected);
    void __cdecl R_VisitPortalsForCell(
    const GfxCell *cell,
    GfxPortal *parentPortal,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount,
    int frustumPlaneCount,
    signed int recursionDepth,
    DpvsClipChildren clipChildren);
    char __cdecl R_ChopPortalAndAddHullPoints(
    GfxPortal *portal,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount);
    void __cdecl R_VisitAllFurtherCells(
    const GfxCell *cell,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount,
    uint8_t frustumPlaneCount);
    int __cdecl R_GetFurtherCellList_r(
    const GfxCell *cell,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount,
    float (*v)[128][3],
    const GfxCell **list,
    int count);
    void __cdecl R_FreeHullPoints(GfxHullPointsPool *hullPoints);
    void __cdecl R_AddAabbTreeSurfacesInFrustum_r(const GfxAabbTree *tree, const DpvsClipPlaneSet *clipSet);
    void __cdecl R_AddCellStaticSurfacesInFrustum(DpvsStaticCellCmd *dpvsCell);
    void __cdecl R_AddCullGroupSurfacesInFrustum(int cullGroupIndex, const DpvsPlane *planes, int planeCount);
    void __cdecl R_AddCellCullGroupsInFrustum(DpvsStaticCellCmd *dpvsCell);
};

void __cdecl R_FrustumClipPlanes(
    const GfxMatrix *viewProjMtx,
    const float (*sidePlanes)[4],
    int sidePlaneCount,
    DpvsPlane *frustumPlanes);
double __cdecl R_FurthestPointOnWinding(const float (*points)[3], int pointCount, const DpvsPlane *plane);
char __cdecl R_PortalBehindAnyPlane(const GfxPortal *portal, const DpvsPlane *planes, int planeCount);
char __cdecl R_PortalBehindPlane(const GfxPortal *portal, const DpvsPlane *plane);
const float (*__cdecl R_ChopPortalWinding(
    const float (*vertsIn)[3],
    int *vertexCount,
    const DpvsPlane *plane,
    float (*vertsOut)[3]))[3];
void __cdecl R_SetAncestorListStatus(GfxPortal *portal, bool isAncestor);
void __cdecl R_CopyClipPlane(const DpvsPlane *in, DpvsPlane *out);
double __cdecl R_NearestPointOnWinding(const float (*points)[3], int pointCount, const DpvsPlane *plane);
char __cdecl R_IsCellInList(const GfxCell *cell, const GfxCell **list, int count);
int __cdecl R_AddCellToList(const GfxCell *cell, const GfxCell **list, int count);
int __cdecl R_CellForPoint(const GfxWorld *world, const float *origin);
float __cdecl R_DpvsPlaneMaxSignedDistToBox(const DpvsPlane *plane, const float *minmax);
float __cdecl R_DpvsPlaneMinSignedDistToBox(const DpvsPlane *plane, const float *minmax);
void __cdecl R_SetDpvsPlaneSides(DpvsPlane *plane);

// Synchronous static-camera producer. False means unavailable, not an empty mask.
// Resets and writes camera slot 0 only; other views/caster data remain untouched.
bool R_ComputeStaticCameraVisibility(GfxWorld &world, DpvsGlobals &dpvs,
    const GfxViewParms &viewParms, unsigned localClientNum, float farPlaneDist);
void R_ClearStaticDpvsView(GfxWorld &world, unsigned viewIndex, bool clearSurfaces);
