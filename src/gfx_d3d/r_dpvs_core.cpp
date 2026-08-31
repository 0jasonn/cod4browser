// Device-independent algorithms extracted from KisakCOD r_dpvs.cpp and
// r_dpvs_static.cpp. Keep portal clipping, queueing and AABB policy shared.
#include <universal/q_shared.h>
#include "gfx_world_types.h"
#include "r_dpvs_core.h"
#include "r_dvars.h"
#include <universal/com_math.h>
#include <qcommon/qcommon.h>
#include <qcommon/threads.h>
#include <universal/com_convexhull.h>
#include <universal/com_memory.h>
#include <universal/profile.h>

const float standardFrustumSidePlanes[4][4] =
{
  { -1.0, 0.0, 0.0, 1.0 },
  { 1.0, 0.0, 0.0, 1.0 },
  { 0.0, -1.0, 0.0, 1.0 },
  { 0.0, 1.0, 0.0, 1.0 }
}; // idb
const float color[4] = { 0.0f, 1.0f, 1.0f, 0.25f };

char *__cdecl DpvsContext::R_PortalAssertMsg()
{
    const char *v0; // eax
    const char *v1; // eax
    const char *v2; // eax
    const char *v3; // eax
    const char *v4; // eax

    v0 = va("\torg %.8gf, %.8gf, %.8gf\n", dpvsGlob.viewOrg[0], dpvsGlob.viewOrg[1], dpvsGlob.viewOrg[2]);
    v1 = va(
        "%s\tplane %.8gf, %.8gf, %.8gf, %.8gf\n",
        v0,
        dpvsGlob.viewPlane.coeffs[0],
        dpvsGlob.viewPlane.coeffs[1],
        dpvsGlob.viewPlane.coeffs[2],
        dpvsGlob.viewPlane.coeffs[3]);
    v2 = va(
        "%s\t%.8gf, %.8gf, %.8gf, %.8gf\n",
        v1,
        dpvsGlob.viewProjMtx->m[0][0],
        dpvsGlob.viewProjMtx->m[0][1],
        dpvsGlob.viewProjMtx->m[0][2],
        dpvsGlob.viewProjMtx->m[0][3]);
    v3 = va(
        "%s\t%.8gf, %.8gf, %.8gf, %.8gf\n",
        v2,
        dpvsGlob.viewProjMtx->m[1][0],
        dpvsGlob.viewProjMtx->m[1][1],
        dpvsGlob.viewProjMtx->m[1][2],
        dpvsGlob.viewProjMtx->m[1][3]);
    v4 = va(
        "%s\t%.8gf, %.8gf, %.8gf, %.8gf\n",
        v3,
        dpvsGlob.viewProjMtx->m[2][0],
        dpvsGlob.viewProjMtx->m[2][1],
        dpvsGlob.viewProjMtx->m[2][2],
        dpvsGlob.viewProjMtx->m[2][3]);
    return va(
        "%s\t%.8gf, %.8gf, %.8gf, %.8gf\n",
        v4,
        dpvsGlob.viewProjMtx->m[3][0],
        dpvsGlob.viewProjMtx->m[3][1],
        dpvsGlob.viewProjMtx->m[3][2],
        dpvsGlob.viewProjMtx->m[3][3]);
}

void __cdecl DpvsContext::R_GetSidePlaneNormals(const float (*winding)[3], uint32_t vertexCount, float (*normals)[3])
{
    float *v3; // [esp+18h] [ebp-61Ch]
    float delta[388]; // [esp+1Ch] [ebp-618h] BYREF
    uint32_t vertexIndex; // [esp+62Ch] [ebp-8h]
    uint32_t vertexIndexNext; // [esp+630h] [ebp-4h]

    iassert( vertexCount < ARRAY_COUNT( delta ) );
    if (dpvsGlob.viewOrgIsDir)
    {
        vertexIndex = vertexCount - 1;
        for (vertexIndexNext = 0; vertexIndexNext < vertexCount; ++vertexIndexNext)
        {
            Vec3Sub(&(*winding)[3 * vertexIndexNext], &(*winding)[3 * vertexIndex], delta);
            Vec3Cross(dpvsGlob.viewOrg, delta, &(*normals)[3 * vertexIndex]);
            Vec3Normalize(&(*normals)[3 * vertexIndex]);
            vertexIndex = vertexIndexNext;
        }
    }
    else
    {
        for (vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
            Vec3Sub(&(*winding)[3 * vertexIndex], dpvsGlob.viewOrg, &delta[3 * vertexIndex]);
        v3 = &delta[3 * vertexCount];
        *v3 = delta[0];
        v3[1] = delta[1];
        v3[2] = delta[2];
        for (vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
        {
            Vec3Cross(&delta[3 * vertexIndex + 3], &delta[3 * vertexIndex], &(*normals)[3 * vertexIndex]);
            Vec3Normalize(&(*normals)[3 * vertexIndex]);
        }
    }
}

GfxPortal *__cdecl DpvsContext::R_NextQueuedPortal()
{
    float dist; // eax
    PortalHeapNode *portalQueue; // esi
    float v2; // eax
    PortalHeapNode *v3; // esi
    int heapIndex; // [esp+4h] [ebp-Ch]
    int chosenChildIndex; // [esp+8h] [ebp-8h]
    GfxPortal *portal; // [esp+Ch] [ebp-4h]

    if (dpvsGlob.queuedCount <= 0)
        MyAssertHandler(
            ".\\r_dpvs.cpp",
            2394,
            0,
            "%s\n\t(dpvsGlob.queuedCount) = %i",
            "(dpvsGlob.queuedCount > 0)",
            dpvsGlob.queuedCount);
    portal = dpvsGlob.portalQueue->portal;
    dpvsGlob.portalQueue->portal->writable.isQueued = 0;
    --dpvsGlob.queuedCount;
    for (heapIndex = 0; ; heapIndex = chosenChildIndex)
    {
        chosenChildIndex = 2 * heapIndex + 1;
        if (chosenChildIndex > dpvsGlob.queuedCount)
            break;
        if (chosenChildIndex < dpvsGlob.queuedCount
            && dpvsGlob.portalQueue[chosenChildIndex].dist >(double)dpvsGlob.portalQueue[chosenChildIndex + 1].dist)
        {
            chosenChildIndex = 2 * heapIndex + 2;
        }
        if (dpvsGlob.portalQueue[chosenChildIndex].dist >= (double)dpvsGlob.portalQueue[dpvsGlob.queuedCount].dist)
            break;
        dist = dpvsGlob.portalQueue[chosenChildIndex].dist;
        portalQueue = dpvsGlob.portalQueue;
        dpvsGlob.portalQueue[heapIndex].portal = dpvsGlob.portalQueue[chosenChildIndex].portal;
        portalQueue[heapIndex].dist = dist;
    }
    v2 = dpvsGlob.portalQueue[dpvsGlob.queuedCount].dist;
    v3 = dpvsGlob.portalQueue;
    dpvsGlob.portalQueue[heapIndex].portal = dpvsGlob.portalQueue[dpvsGlob.queuedCount].portal;
    v3[heapIndex].dist = v2;
    R_AssertValidQueue();
    return portal;
}

int DpvsContext::R_AssertValidQueue()
{
    int result = 0; // eax
    int queueIndex; // [esp+4h] [ebp-4h]

    for (queueIndex = 1; queueIndex < dpvsGlob.queuedCount; ++queueIndex)
    {
        if (dpvsGlob.portalQueue[queueIndex].dist < (double)dpvsGlob.portalQueue[(queueIndex - 1) >> 1].dist)
            MyAssertHandler(
                ".\\r_dpvs.cpp",
                2347,
                0,
                "%s",
                "dpvsGlob.portalQueue[parentIndex].dist <= dpvsGlob.portalQueue[queueIndex].dist");
        result = queueIndex + 1;
    }
    return result;
}

void __cdecl DpvsContext::R_EnqueuePortal(GfxPortal *portal)
{
    float v1; // edx
    PortalHeapNode *portalQueue; // esi
    int heapIndex; // [esp+4h] [ebp-Ch]
    float dist; // [esp+8h] [ebp-8h]
    int parentIndex; // [esp+Ch] [ebp-4h]

    iassert( portal );
    iassert( !portal->writable.isQueued );
    iassert( portal->writable.hullPoints );
    iassert( portal->writable.hullPointCount >= 3 );
    if (dpvsGlob.queuedCount >= 256)
        Com_Error(ERR_DROP, "More than %i queued portals", 256);
    portal->writable.isQueued = 1;
    dist = R_FurthestPointOnWinding(portal->vertices, portal->vertexCount, &dpvsGlob.viewPlane);
    for (heapIndex = dpvsGlob.queuedCount; ; heapIndex = (heapIndex - 1) >> 1)
    {
        parentIndex = (heapIndex - 1) >> 1;
        if (parentIndex < 0 || dist >= (double)dpvsGlob.portalQueue[parentIndex].dist)
            break;
        v1 = dpvsGlob.portalQueue[parentIndex].dist;
        portalQueue = dpvsGlob.portalQueue;
        dpvsGlob.portalQueue[heapIndex].portal = dpvsGlob.portalQueue[parentIndex].portal;
        portalQueue[heapIndex].dist = v1;
    }
    if (heapIndex < 0 || heapIndex > dpvsGlob.queuedCount)
        MyAssertHandler(
            ".\\r_dpvs.cpp",
            2379,
            1,
            "%s\n\t(heapIndex) = %i",
            "(heapIndex >= 0 && heapIndex <= dpvsGlob.queuedCount)",
            heapIndex);
    dpvsGlob.portalQueue[heapIndex].portal = portal;
    dpvsGlob.portalQueue[heapIndex].dist = dist;
    ++dpvsGlob.queuedCount;
    R_AssertValidQueue();
}

bool __cdecl DpvsContext::R_ShouldSkipPortal(const GfxPortal *portal, const DpvsPlane *planes, int planeCount)
{
    float v4; // [esp+0h] [ebp-4h]

    if (portal->writable.isAncestor)
        return 1;
    v4 = Vec4Dot(portal->plane.coeffs, dpvsGlob.viewOrg);
    return v4 > 0.0 || R_PortalBehindAnyPlane(portal, planes, planeCount) != 0;
}

void __cdecl DpvsContext::R_AddVertToPortalHullPoints(GfxPortal *portal, const float *v)
{
    float hull[64][2]; // [esp+14h] [ebp-208h] BYREF
    int hullPointCount; // [esp+218h] [ebp-4h]

    if (portal->writable.hullPoints)
    {
        if (portal->writable.hullPointCount == 64)
        {
            {
                PROF_SCOPED("R_ConvexHull");
                hullPointCount = Com_ConvexHull(portal->writable.hullPoints, portal->writable.hullPointCount, hull);
            }
            if (hullPointCount == 64)
                Com_Error(ERR_DROP, "More than %i points on a clipped portal's convex hull\n", 64);
            portal->writable.hullPointCount = hullPointCount;
            memcpy(
                (uint8_t *)portal->writable.hullPoints,
                (uint8_t *)hull,
                8 * portal->writable.hullPointCount);
        }
    }
    else
    {
        portal->writable.hullPoints = (float (*)[2])R_AllocHullPoints();
        portal->writable.hullPointCount = 0;
    }
    portal->writable.hullPoints[portal->writable.hullPointCount][0] = Vec3Dot(v, portal->hullAxis[0]);
    portal->writable.hullPoints[portal->writable.hullPointCount++][1] = Vec3Dot(v, portal->hullAxis[1]);
}

GfxHullPointsPool *__cdecl DpvsContext::R_AllocHullPoints()
{
    GfxHullPointsPool *hullPointsPool; // [esp+0h] [ebp-4h]

    hullPointsPool = dpvsGlob.nextFreeHullPoints;
    if (!dpvsGlob.nextFreeHullPoints)
        Com_Error(ERR_DROP, "more than %i queued portals", 256);
    dpvsGlob.nextFreeHullPoints = hullPointsPool->nextFree;
    return hullPointsPool;
}

int __cdecl DpvsContext::R_ChopPortal(
    const GfxPortal *portal,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount,
    float (*v)[128][3],
    const float (**finalVerts)[3])
{
    int vertCount; // [esp+0h] [ebp-Ch] BYREF
    int planeIndex; // [esp+4h] [ebp-8h]
    const float (*w)[3]; // [esp+8h] [ebp-4h]

    iassert( Sys_IsMainThread() );
    iassert( portal );
    iassert( parentPlane );
    iassert( planes || planeCount == 0 );
    vertCount = portal->vertexCount;
    w = portal->vertices;
    w = R_ChopPortalWinding(w, &vertCount, parentPlane, (float (*)[3])v);
    if (!vertCount)
        return 0;
    if (dpvsGlob.farPlane)
    {
        w = R_ChopPortalWinding(w, &vertCount, dpvsGlob.farPlane, (float (*)[3])(*v)[128 * (w == (const float (*)[3])v)]);
        if (!vertCount)
            return 0;
    }
    for (planeIndex = 0; planeIndex < planeCount; ++planeIndex)
    {
        w = R_ChopPortalWinding(w, &vertCount, &planes[planeIndex], (float (*)[3])(*v)[128 * (w == (const float (*)[3])v)]);
        if (!vertCount)
            return 0;
    }
    if (finalVerts)
        *finalVerts = w;
    return vertCount;
}

void __cdecl DpvsContext::R_SetCellVisible(const GfxCell *cell)
{
    iassert( dpvsGlob.cellBits );
    dpvsGlob.cellBits[(uint32_t)(cell - world->cells) >> 5] |= 1 << ((cell - world->cells) & 0x1F);
}

void __cdecl DpvsContext::R_SetupWorldSurfacesDpvs(const GfxViewParms *viewParms)
{
    DpvsView *dpvsView; // [esp+0h] [ebp-4h]

    iassert( Sys_IsMainThread() );
    iassert( world );
    iassert( viewParms );
    dpvsView = dpvsGlob.views[localClientNum];
    dpvsView->renderFxFlagsCull = 0;
    dpvsGlob.viewProjMtx = &viewParms->viewProjectionMatrix;
    dpvsGlob.invViewProjMtx = &viewParms->inverseViewProjectionMatrix;
    R_FrustumClipPlanes(&viewParms->viewProjectionMatrix, standardFrustumSidePlanes, 4, dpvsView->frustumPlanes);
    iassert( viewParms->projectionMatrix.m[3][3] == 0 );
    R_SetupDpvsForPoint(viewParms);
    dpvsGlob.sideFrustumPlanes = dpvsView->frustumPlanes;
    dpvsView->frustumPlaneCount = R_AddNearAndFarClipPlanes(dpvsView->frustumPlanes, 4);
}

int __cdecl DpvsContext::R_AddNearAndFarClipPlanes(DpvsPlane *planes, int planeCount)
{
    int planeCounta; // [esp+Ch] [ebp+Ch]

    iassert( Sys_IsMainThread() );
    iassert( dpvsGlob.nearPlane );
    planes[planeCount] = *dpvsGlob.nearPlane;
    planeCounta = planeCount + 1;
    if (dpvsGlob.farPlane)
        planes[planeCounta++] = *dpvsGlob.farPlane;
    return planeCounta;
}

void __cdecl DpvsContext::R_SetupDpvsForPoint(const GfxViewParms *viewParms)
{
    float zfar; // [esp+14h] [ebp-4h]

    iassert( Sys_IsMainThread() );
    dpvsGlob.viewOrg[0] = viewParms->origin[0];
    dpvsGlob.viewOrg[1] = viewParms->origin[1];
    dpvsGlob.viewOrg[2] = viewParms->origin[2];
    dpvsGlob.viewOrg[3] = 1.0;
    dpvsGlob.viewOrgIsDir = 0;
    dpvsGlob.viewPlane.coeffs[0] = viewParms->axis[0][0];
    dpvsGlob.viewPlane.coeffs[1] = viewParms->axis[0][1];
    dpvsGlob.viewPlane.coeffs[2] = viewParms->axis[0][2];
    dpvsGlob.viewPlane.coeffs[3] = 0.1 - Vec3Dot(dpvsGlob.viewPlane.coeffs, dpvsGlob.viewOrg);
    dpvsGlob.viewPlane.side[0] = dpvsGlob.viewPlane.coeffs[0] <= 0 ? 0 : 0xC;
    dpvsGlob.viewPlane.side[1] = dpvsGlob.viewPlane.coeffs[1] <= 0 ? 4 : 16;
    dpvsGlob.viewPlane.side[2] = dpvsGlob.viewPlane.coeffs[2] <= 0 ? 8 : 20;
    dpvsGlob.nearPlane = (DpvsPlane *)&dpvsGlob;
    zfar = farPlaneDist;
    if (zfar > 0.0)
    {
        dpvsGlob.fogPlane.coeffs[0] = -viewParms->axis[0][0];
        dpvsGlob.fogPlane.coeffs[1] = -viewParms->axis[0][1];
        dpvsGlob.fogPlane.coeffs[2] = -viewParms->axis[0][2];
        dpvsGlob.fogPlane.coeffs[3] = zfar - Vec3Dot(dpvsGlob.fogPlane.coeffs, dpvsGlob.viewOrg);
        dpvsGlob.fogPlane.side[0] = dpvsGlob.fogPlane.coeffs[0] <= 0 ? 0 : 0xC;
        dpvsGlob.fogPlane.side[1] = dpvsGlob.fogPlane.coeffs[1] <= 0 ? 4 : 16;
        dpvsGlob.fogPlane.side[2] = dpvsGlob.fogPlane.coeffs[2] <= 0 ? 8 : 20;
        dpvsGlob.farPlane = &dpvsGlob.fogPlane;
    }
    else
    {
        dpvsGlob.farPlane = 0;
    }
}

void __cdecl DpvsContext::R_AddWorldSurfacesPortalWalk(int cameraCellIndex)
{
    GfxCell *cell; // [esp+0h] [ebp-Ch]
    int cellIndex; // [esp+4h] [ebp-8h]
    DpvsView *dpvsView; // [esp+8h] [ebp-4h]

    iassert( Sys_IsMainThread() );
    iassert( world->dpvsPlanes.cellCount );
    memset((uint8_t *)dpvsGlob.cellVisibleBits, 0, 4 * ((world->dpvsPlanes.cellCount + 31) >> 5));
    dpvsGlob.cellBits = dpvsGlob.cellVisibleBits;
    if (!r_skipPvs->current.enabled)
    {
        dpvsView = dpvsGlob.views[localClientNum];

        // *(DpvsView **)(*((uint32_t *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 8)
        iassert(dpvsView == &dpvsGlob.views[localClientNum][0]);
        if (cameraCellIndex < 0)
        {
            for (cellIndex = 0; cellIndex < world->dpvsPlanes.cellCount; ++cellIndex)
            {
                DispatchCell(
                    &world->cells[cellIndex],
                    dpvsView->frustumPlanes,
                    dpvsView->frustumPlaneCount,
                    dpvsView->frustumPlaneCount);
                R_SetCellVisible(&world->cells[cellIndex]);
            }
        }
        else
        {
            cell = &world->cells[cameraCellIndex];
            if (r_singleCell->current.enabled)
            {
                dpvsGlob.farPlane = 0;
                DispatchCell(
                    cell,
                    dpvsView->frustumPlanes,
                    dpvsView->frustumPlaneCount,
                    dpvsView->frustumPlaneCount);
                R_SetCellVisible(cell);
            }
            else
            {
                R_VisitPortals(cell, &dpvsGlob.viewPlane, dpvsView->frustumPlanes, dpvsView->frustumPlaneCount);
            }
        }
    }
}

void __cdecl DpvsContext::R_VisitPortals(const GfxCell *cell, const DpvsPlane *parentPlane, const DpvsPlane *planes, int planeCount)
{
    float scale; // [esp+4h] [ebp-D98h]
    float v5; // [esp+28h] [ebp-D74h]
    float v6; // [esp+2Ch] [ebp-D70h]
    DpvsPlane *childPlanes; // [esp+58h] [ebp-D44h]
    GfxHullPointsPool(*hullPointsPoolArray)[256]; // [esp+64h] [ebp-D38h]
    uint32_t childPlanesCount; // [esp+68h] [ebp-D34h]
    int childPlaneCount; // [esp+6Ch] [ebp-D30h]
    int iteration; // [esp+70h] [ebp-D2Ch]
    GfxPortal *portal; // [esp+74h] [ebp-D28h]
    int queueIndex; // [esp+78h] [ebp-D24h]
    float portalVerts[64][3]; // [esp+7Ch] [ebp-D20h] BYREF
    float hullOrigin[3]; // [esp+380h] [ebp-A1Ch] BYREF
    DpvsClipChildren clipChildren; // [esp+38Ch] [ebp-A10h] BYREF
    uint32_t vertIndex; // [esp+390h] [ebp-A0Ch]
    PortalHeapNode portalQueue[256]; // [esp+394h] [ebp-A08h] BYREF
    float hull[64][2]; // [esp+B94h] [ebp-208h] BYREF
    uint32_t hullPointCount; // [esp+D98h] [ebp-4h]

    LargeLocal hullPointsPoolArray_large_local(0x20000);
    //LargeLocal::LargeLocal(&hullPointsPoolArray_large_local, 0x20000);
    //hullPointsPoolArray = (GfxHullPointsPool(*)[256])LargeLocal::GetBuf(&hullPointsPoolArray_large_local);
    hullPointsPoolArray = (GfxHullPointsPool(*)[256])hullPointsPoolArray_large_local.GetBuf();

    PROF_SCOPED("R_VisitPortals");

    iassert( Sys_IsMainThread() );
    childPlanesCount = 0;
    for (queueIndex = 0; queueIndex < 255; ++queueIndex)
        (*hullPointsPoolArray)[queueIndex].nextFree = &(*hullPointsPoolArray)[queueIndex + 1];
    (*hullPointsPoolArray)[queueIndex].nextFree = 0;
    dpvsGlob.nextFreeHullPoints = (GfxHullPointsPool *)hullPointsPoolArray;
    dpvsGlob.portalQueue = portalQueue;
    dpvsGlob.queuedCount = 0;
    R_VisitPortalsForCell(cell, 0, parentPlane, planes, planeCount, planeCount, 0, DPVS_CLIP_CHILDREN);
    iteration = 0;
    while (dpvsGlob.queuedCount)
    {
        portal = R_NextQueuedPortal();
        iassert( portal );
        {
            PROF_SCOPED("R_ConvexHull");
            hullPointCount = Com_ConvexHull(portal->writable.hullPoints, portal->writable.hullPointCount, hull);
        }
        R_FreeHullPoints((GfxHullPointsPool *)portal->writable.hullPoints);
        portal->writable.hullPoints = 0;
        if (hullPointCount)
        {
            if (++iteration == r_portalWalkLimit->current.integer)
            {
                while (dpvsGlob.queuedCount)
                {
                    portal = R_NextQueuedPortal();
                    R_FreeHullPoints((GfxHullPointsPool *)portal->writable.hullPoints);
                    portal->writable.hullPoints = 0;
                }
                break;
            }
            v6 = Vec4Dot(portal->plane.coeffs, dpvsGlob.viewOrg);
            if (v6 > 0.1248750016093254)
            {
                v5 = Vec4Dot(portal->plane.coeffs, dpvsGlob.viewOrg);
                MyAssertHandler(
                    ".\\r_dpvs.cpp",
                    2817,
                    0,
                    "%s\n\t(R_DpvsPlaneDistToEye( &portal->plane )) = %g",
                    "(R_DpvsPlaneDistToEye( &portal->plane ) <= 0.125f * 0.999f)",
                    v5);
            }
            scale = -portal->plane.coeffs[3];
            Vec3Scale(portal->plane.coeffs, scale, hullOrigin);
            for (vertIndex = 0; vertIndex < hullPointCount; ++vertIndex)
            {
                Vec3Mad(hullOrigin, hull[vertIndex][0], portal->hullAxis[0], portalVerts[vertIndex]);
                Vec3Mad(portalVerts[vertIndex], hull[vertIndex][1], portal->hullAxis[1], portalVerts[vertIndex]);
            }
            if (debugPolygon && r_showPortals->current.integer && !r_portalBevelsOnly->current.enabled)
                debugPolygon( color, hullPointCount, portalVerts);
            if (childPlanesCount + 16 > 0x800)
            {
                if (flushCells) flushCells();
                childPlanesCount = 0;
            }
            childPlanes = &dpvsGlob.childPlanes[childPlanesCount];
            childPlaneCount = R_PortalClipPlanes(childPlanes, hullPointCount, portalVerts, portal->cell, &clipChildren);
            iassert( childPlaneCount <= DPVS_PORTAL_MAX_PLANES );
            childPlanesCount += childPlaneCount;
            if (portal->writable.recursionDepth < r_portalMinRecurseDepth->current.integer)
                clipChildren = DPVS_CLIP_CHILDREN;
            R_VisitPortalsForCell(
                portal->cell,
                portal,
                &portal->plane,
                childPlanes,
                childPlaneCount,
                0,
                portal->writable.recursionDepth + 1,
                clipChildren);
        }
    }
    //LargeLocal::~LargeLocal(&hullPointsPoolArray_large_local);
}

uint32_t __cdecl DpvsContext::R_PortalClipPlanes(
    DpvsPlane *planes,
    uint32_t vertexCount,
    const float (*winding)[3],
    GfxCell *cell,
    DpvsClipChildren *clipChildren)
{
    bool v6; // [esp+0h] [ebp-640h]
    DpvsForceBevels v7; // [esp+4h] [ebp-63Ch]
    DpvsPlane *a; // [esp+8h] [ebp-638h]
    DpvsPlane *v9; // [esp+10h] [ebp-630h]
    float *v10; // [esp+14h] [ebp-62Ch]
    float normals[128][3]; // [esp+18h] [ebp-628h] BYREF
    uint32_t windingVertIndex; // [esp+618h] [ebp-28h]
    uint32_t planeCount; // [esp+61Ch] [ebp-24h]
    float clipSpaceMins[2]; // [esp+620h] [ebp-20h] BYREF
    float clipSpaceMaxs[2]; // [esp+628h] [ebp-18h] BYREF
    bool useNormalPlanes; // [esp+633h] [ebp-Dh]
    float distMin; // [esp+634h] [ebp-Ch]
    DpvsForceBevels forceBevels; // [esp+638h] [ebp-8h]
    bool useBevelPlanes; // [esp+63Fh] [ebp-1h]

    iassert( Sys_IsMainThread() );
    iassert( (vertexCount >= 3) );
    useNormalPlanes = vertexCount <= 0xA;
    v7 = (DpvsForceBevels)(vertexCount > 0xA || r_portalBevelsOnly->current.enabled);
    forceBevels = v7;
    v6 = v7 == DPVS_FORCE_BEVELS || r_portalBevels->current.value > 0.0;
    useBevelPlanes = v6;
    R_GetSidePlaneNormals(winding, vertexCount, normals);
    planeCount = 0;
    if (useBevelPlanes || r_portalMinClipArea->current.value > 0.0)
    {
        R_ProjectPortal(vertexCount, winding, clipSpaceMins, clipSpaceMaxs, clipChildren);
        if (useBevelPlanes)
            planeCount = R_AddBevelPlanes(planes, vertexCount, winding, normals, clipSpaceMins, clipSpaceMaxs, forceBevels);
    }
    else
    {
        *clipChildren = DPVS_CLIP_CHILDREN;
    }
    if (useNormalPlanes)
    {
        for (windingVertIndex = 0; windingVertIndex < vertexCount; ++windingVertIndex)
        {
            if (Vec3LengthSq(normals[windingVertIndex]) != 0.0)
            {
                v9 = &planes[planeCount];
                v10 = normals[windingVertIndex];
                v9->coeffs[0] = *v10;
                v9->coeffs[1] = v10[1];
                v9->coeffs[2] = v10[2];
                a = &planes[planeCount];
                a->coeffs[3] = 0.001 - Vec3Dot(a->coeffs, &(*winding)[3 * windingVertIndex]);
                a->side[0] = COERCE_INT(a->coeffs[0]) <= 0 ? 0 : 0xC;
                a->side[1] = COERCE_INT(a->coeffs[1]) <= 0 ? 4 : 16;
                a->side[2] = COERCE_INT(a->coeffs[2]) <= 0 ? 8 : 20;
                ++planeCount;
            }
        }
    }
    iassert( dpvsGlob.nearPlane );
    planes[planeCount] = *dpvsGlob.nearPlane;
    distMin = R_NearestPointOnWinding(winding, vertexCount, &planes[planeCount]);
    if (distMin > 0.0)
        planes[planeCount].coeffs[3] = planes[planeCount].coeffs[3] - distMin;
    ++planeCount;
    if (dpvsGlob.farPlane)
        planes[planeCount++] = *dpvsGlob.farPlane;
    return planeCount;
}

void __cdecl DpvsContext::R_ProjectPortal(
    int vertexCount,
    const float (*winding)[3],
    float *mins,
    float *maxs,
    DpvsClipChildren *clipChildren)
{
    int windingVertIndex; // [esp+40h] [ebp-440h]
    float area; // [esp+44h] [ebp-43Ch]
    const float *xyz; // [esp+48h] [ebp-438h]
    float x; // [esp+50h] [ebp-430h]
    float y; // [esp+54h] [ebp-42Ch]
    float screenSpaceWinding[132][2]; // [esp+58h] [ebp-428h] BYREF

    iassert(vertexCount >= 3);

    mins[0] = 1.0f;
    mins[1] = 1.0f;
    maxs[0] = -1.0f;
    maxs[1] = -1.0f;

    for (windingVertIndex = 0; windingVertIndex < vertexCount; ++windingVertIndex)
    {
        xyz = &(*winding)[3 * windingVertIndex];

        float w = xyz[0] * dpvsGlob.viewProjMtx->m[0][3]
            + xyz[1] * dpvsGlob.viewProjMtx->m[1][3]
            + xyz[2] * dpvsGlob.viewProjMtx->m[2][3]
            + dpvsGlob.viewProjMtx->m[3][3];

        if (w < 0.125)
        {
            mins[0] = -1.0f;
            mins[1] = -1.0f;
            maxs[0] = 1.0f;
            maxs[1] = 1.0f;
            *clipChildren = DPVS_CLIP_CHILDREN;
            return;
        }
        x = (((*xyz * dpvsGlob.viewProjMtx->m[0][0]) + (xyz[1] * dpvsGlob.viewProjMtx->m[1][0])) + (xyz[2] * dpvsGlob.viewProjMtx->m[2][0])) + dpvsGlob.viewProjMtx->m[3][0];
        y = (((*xyz * dpvsGlob.viewProjMtx->m[0][1]) + (xyz[1] * dpvsGlob.viewProjMtx->m[1][1])) + (xyz[2] * dpvsGlob.viewProjMtx->m[2][1])) + dpvsGlob.viewProjMtx->m[3][1];

        float invW = 1.0 / w;
        screenSpaceWinding[windingVertIndex][0] = x * (1.0 / w);
        screenSpaceWinding[windingVertIndex][1] = y * invW;

        // Update min/max values
        if (mins[0] > screenSpaceWinding[windingVertIndex][0]) { mins[0] = screenSpaceWinding[windingVertIndex][0]; }
        if (maxs[0] < screenSpaceWinding[windingVertIndex][0]) { maxs[0] = screenSpaceWinding[windingVertIndex][0]; }
        if (mins[1] > screenSpaceWinding[windingVertIndex][1]) { mins[1] = screenSpaceWinding[windingVertIndex][1]; }
        if (maxs[1] < screenSpaceWinding[windingVertIndex][1]) { maxs[1] = screenSpaceWinding[windingVertIndex][1]; }
    }

    area = ((maxs[0] - mins[0]) * (maxs[1] - mins[1])) * 0.25;
    iassert(area >= 0);

    // Portal is too small, who cares
    if (area < r_portalMinClipArea->current.value)
    {
        *clipChildren = DPVS_DONT_CLIP_CHILDREN;
        return;
    }

    screenSpaceWinding[vertexCount][0] = screenSpaceWinding[0][0];
    screenSpaceWinding[vertexCount][1] = screenSpaceWinding[0][1];
    screenSpaceWinding[vertexCount + 1][0] = screenSpaceWinding[1][0];
    screenSpaceWinding[vertexCount + 1][1] = screenSpaceWinding[1][1];

    area = 0.0f;
    for (int i = 1; i <= vertexCount; ++i)
        area += (screenSpaceWinding[i + 1][1] - screenSpaceWinding[i - 1][1]) * screenSpaceWinding[i][0];

    //for ( windingVertIndexa = 1; windingVertIndexa <= vertexCount; ++windingVertIndexa )
    //    area = ((screenSpaceWinding[windingVertIndexa + 1][1] - *(&y + 2 * windingVertIndexa)) * screenSpaceWinding[windingVertIndexa][0]) + area;

    area *= 0.125;
    iassert(area >= 0);

    *clipChildren = (DpvsClipChildren)(r_portalMinClipArea->current.value <= area);
}

uint32_t __cdecl DpvsContext::R_AddBevelPlanes(
    DpvsPlane *planes,
    uint32_t vertexCount,
    const float (*winding)[3],
    const float (*windingNormals)[3],
    float *mins,
    float *maxs,
    DpvsForceBevels forceBevels)
{
    float v8; // [esp+0h] [ebp-B0h]
    float v9; // [esp+4h] [ebp-ACh]
    DpvsPlane *a; // [esp+8h] [ebp-A8h]
    DpvsPlane *v11; // [esp+10h] [ebp-A0h]
    float *v12; // [esp+14h] [ebp-9Ch]
    uint32_t windingVertIndex; // [esp+1Ch] [ebp-94h]
    float projected[2]; // [esp+20h] [ebp-90h] BYREF
    float bevelVerts[5][3]; // [esp+28h] [ebp-88h] BYREF
    uint32_t planeCount; // [esp+64h] [ebp-4Ch]
    float bevelNormals[4][3]; // [esp+68h] [ebp-48h] BYREF
    float invW; // [esp+98h] [ebp-18h]
    uint32_t bevelVertIndex; // [esp+9Ch] [ebp-14h]
    float unprojected[4]; // [esp+A0h] [ebp-10h] BYREF

    for (bevelVertIndex = 0; bevelVertIndex < 4; ++bevelVertIndex)
    {
        if (bevelVertIndex >= 2)
            v9 = *maxs;
        else
            v9 = *mins;
        projected[0] = v9;
        if (bevelVertIndex == 1 || bevelVertIndex == 2)
            v8 = mins[1];
        else
            v8 = maxs[1];
        projected[1] = v8;
        R_UnprojectPoint(projected, unprojected);
        invW = 1.0 / unprojected[3];
        bevelVerts[bevelVertIndex][0] = unprojected[0] * invW;
        bevelVerts[bevelVertIndex][1] = unprojected[1] * invW;
        bevelVerts[bevelVertIndex][2] = unprojected[2] * invW;
    }
    bevelVerts[4][0] = bevelVerts[0][0];
    bevelVerts[4][1] = bevelVerts[0][1];
    bevelVerts[4][2] = bevelVerts[0][2];
    R_GetSidePlaneNormals(bevelVerts, 4u, bevelNormals);
    planeCount = 0;
    for (bevelVertIndex = 0; bevelVertIndex < 4; ++bevelVertIndex)
    {
        v11 = &planes[planeCount];
        v12 = bevelNormals[bevelVertIndex];
        v11->coeffs[0] = *v12;
        v11->coeffs[1] = v12[1];
        v11->coeffs[2] = v12[2];
        if (forceBevels == DPVS_DONT_FORCE_BEVELS)
        {
            for (windingVertIndex = 0; windingVertIndex < vertexCount; ++windingVertIndex)
            {
                if (r_portalBevels->current.value < Vec3Dot(
                    &(*windingNormals)[3 * windingVertIndex],
                    planes[planeCount].coeffs))
                {
                    if (debugLine && (r_showPortals->current.integer & 2) != 0)
                        debugLine(
                            bevelVerts[bevelVertIndex],
                            bevelVerts[bevelVertIndex + 1],
                            colorMdCyan);
                    goto LABEL_12;
                }
            }
        }
        if (debugLine && r_showPortals->current.integer)
            debugLine(
                bevelVerts[bevelVertIndex],
                bevelVerts[bevelVertIndex + 1],
                colorLtCyan);
        a = &planes[planeCount];
        a->coeffs[3] = 0.001 - Vec3Dot(a->coeffs, bevelVerts[bevelVertIndex]);
        a->side[0] = COERCE_INT(a->coeffs[0]) <= 0 ? 0 : 0xC;
        a->side[1] = COERCE_INT(a->coeffs[1]) <= 0 ? 4 : 16;
        a->side[2] = COERCE_INT(a->coeffs[2]) <= 0 ? 8 : 20;
        ++planeCount;
    LABEL_12:
        ;
    }
    return planeCount;
}

void __cdecl DpvsContext::R_UnprojectPoint(const float *projected, float *unprojected)
{
    *unprojected = dpvsGlob.invViewProjMtx->m[0][0] * *projected
        + dpvsGlob.invViewProjMtx->m[1][0] * projected[1]
        + dpvsGlob.invViewProjMtx->m[3][0];
    unprojected[1] = dpvsGlob.invViewProjMtx->m[0][1] * *projected
        + dpvsGlob.invViewProjMtx->m[1][1] * projected[1]
        + dpvsGlob.invViewProjMtx->m[3][1];
    unprojected[2] = dpvsGlob.invViewProjMtx->m[0][2] * *projected
        + dpvsGlob.invViewProjMtx->m[1][2] * projected[1]
        + dpvsGlob.invViewProjMtx->m[3][2];
    unprojected[3] = dpvsGlob.invViewProjMtx->m[0][3] * *projected
        + dpvsGlob.invViewProjMtx->m[1][3] * projected[1]
        + dpvsGlob.invViewProjMtx->m[3][3];
}

void __cdecl DpvsContext::R_VisitPortalsForCell(
    const GfxCell *cell,
    GfxPortal *parentPortal,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount,
    int frustumPlaneCount,
    signed int recursionDepth,
    DpvsClipChildren clipChildren)
{
    uint8_t v8; // [esp+0h] [ebp-24h]
    int vertCount; // [esp+8h] [ebp-1Ch]
    const float *verts; // [esp+Ch] [ebp-18h]
    int xCoord; // [esp+10h] [ebp-14h] BYREF
    int yCoord; // [esp+14h] [ebp-10h] BYREF
    float v13; // [esp+18h] [ebp-Ch]
    GfxPortal *portal; // [esp+1Ch] [ebp-8h]
    int portalIndex; // [esp+20h] [ebp-4h]

    DispatchCell(cell, planes, planeCount, frustumPlaneCount);
    R_SetCellVisible(cell);
    R_SetAncestorListStatus(parentPortal, 1);
    if (clipChildren)
    {
        if (clipChildren != DPVS_CLIP_CHILDREN)
            MyAssertHandler(
                ".\\r_dpvs.cpp",
                2679,
                0,
                "%s\n\t(clipChildren) = %i",
                "(clipChildren == DPVS_CLIP_CHILDREN)",
                clipChildren);
        for (portalIndex = 0; portalIndex < cell->portalCount; ++portalIndex)
        {
            portal = &cell->portals[portalIndex];
            if (!R_ShouldSkipPortal(portal, planes, planeCount))
            {
                iassert( !dpvsGlob.viewOrgIsDir );
                v13 = Vec4Dot(portal->plane.coeffs, dpvsGlob.viewOrg);
                if (v13 <= -0.125)
                {
                    if (R_ChopPortalAndAddHullPoints(portal, parentPlane, planes, planeCount))
                    {
                        if (portal->writable.isQueued)
                        {
                            if (portal->writable.recursionDepth < recursionDepth)
                                v8 = portal->writable.recursionDepth;
                            else
                                v8 = recursionDepth;
                            portal->writable.recursionDepth = v8;
                            if (portal->writable.queuedParent != parentPortal)
                                portal->writable.queuedParent = 0;
                        }
                        else
                        {
                            portal->writable.recursionDepth = recursionDepth;
                            portal->writable.queuedParent = parentPortal;
                            R_EnqueuePortal(portal);
                        }
                    }
                }
                else
                {
                    iassert( !portal->writable.isAncestor );
                    iassert( !portal->writable.isQueued );
                    vertCount = portal->vertexCount;
                    verts = (const float *)portal->vertices;
                    Vec3ProjectionCoords(portal->plane.coeffs, &xCoord, &yCoord);
                    if (ProjectedWindingContainsCoplanarPoint(
                        (const float (*)[3])verts,
                        vertCount,
                        xCoord,
                        yCoord,
                        dpvsGlob.viewOrg))
                    {
                        portal->writable.queuedParent = 0;
                        R_VisitPortalsForCell(
                            portal->cell,
                            portal,
                            &portal->plane,
                            planes,
                            planeCount,
                            frustumPlaneCount,
                            portal->writable.recursionDepth + 1,
                            clipChildren);
                    }
                    iassert( !portal->writable.isAncestor );
                    if (parentPortal)
                    {
                        if (!parentPortal->writable.isAncestor)
                            MyAssertHandler(
                                ".\\r_dpvs.cpp",
                                2700,
                                1,
                                "%s",
                                "parentPortal == NULL || parentPortal->writable.isAncestor");
                    }
                }
            }
        }
        R_SetAncestorListStatus(parentPortal, 0);
    }
    else
    {
        R_VisitAllFurtherCells(cell, parentPlane, planes, planeCount, frustumPlaneCount);
        R_SetAncestorListStatus(parentPortal, 0);
    }
}

char __cdecl DpvsContext::R_ChopPortalAndAddHullPoints(
    GfxPortal *portal,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount)
{
    int vertCount; // [esp+0h] [ebp-C10h]
    int vertIndex; // [esp+4h] [ebp-C0Ch]
    float v[2][128][3]; // [esp+8h] [ebp-C08h] BYREF
    const float (*w)[3]; // [esp+C0Ch] [ebp-4h] BYREF

    vertCount = R_ChopPortal(portal, parentPlane, planes, planeCount, v, &w);
    if (!vertCount)
        return 0;
    for (vertIndex = 0; vertIndex < vertCount; ++vertIndex)
        R_AddVertToPortalHullPoints(portal, w[vertIndex]);
    return 1;
}

void __cdecl DpvsContext::R_VisitAllFurtherCells(
    const GfxCell *cell,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount,
    uint8_t frustumPlaneCount)
{
    int i; // [esp+0h] [ebp-1C0Ch]
    GfxCell *list[1025]; // [esp+4h] [ebp-1C08h] BYREF
    int FurtherCellList_r; // [esp+1008h] [ebp-C04h]
    float v[256][3]; // [esp+100Ch] [ebp-C00h] BYREF

    FurtherCellList_r = R_GetFurtherCellList_r(
        cell,
        parentPlane,
        planes,
        planeCount,
        (float (*)[128][3])v,
        (const GfxCell **)list,
        0);
    for (i = 0; i < FurtherCellList_r; ++i)
    {
        DispatchCell(list[i], planes, planeCount, frustumPlaneCount);
        R_SetCellVisible(list[i]);
    }
}

int __cdecl DpvsContext::R_GetFurtherCellList_r(
    const GfxCell *cell,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount,
    float (*v)[128][3],
    const GfxCell **list,
    int count)
{
    int v7; // eax
    const GfxPortal *portal; // [esp+0h] [ebp-8h]
    int portalIndex; // [esp+4h] [ebp-4h]

    for (portalIndex = 0; portalIndex < cell->portalCount; ++portalIndex)
    {
        portal = &cell->portals[portalIndex];
        if (!R_IsCellInList(portal->cell, list, count) && !R_ShouldSkipPortal(portal, planes, planeCount))
        {
            if (R_ChopPortal(portal, parentPlane, planes, planeCount, v, 0))
            {
                v7 = R_AddCellToList(portal->cell, list, count);
                count = R_GetFurtherCellList_r(portal->cell, parentPlane, planes, planeCount, v, list, v7);
            }
        }
    }
    return count;
}

void __cdecl DpvsContext::R_FreeHullPoints(GfxHullPointsPool *hullPoints)
{
    hullPoints->nextFree = dpvsGlob.nextFreeHullPoints;
    dpvsGlob.nextFreeHullPoints = hullPoints;
}

void __cdecl R_FrustumClipPlanes(
    const GfxMatrix *viewProjMtx,
    const float (*sidePlanes)[4],
    int sidePlaneCount,
    DpvsPlane *frustumPlanes)
{
    int term; // [esp+14h] [ebp-10h]
    float scale; // [esp+18h] [ebp-Ch]
    float length; // [esp+1Ch] [ebp-8h]
    int planeIndex; // [esp+20h] [ebp-4h]

    for (planeIndex = 0; planeIndex < sidePlaneCount; ++planeIndex)
    {
        for (term = 0; term < 4; ++term)
        {
            frustumPlanes[planeIndex].coeffs[term] = Vec4Dot(&(*sidePlanes)[4 * planeIndex], viewProjMtx->m[term]);
        }
        length = Vec3Length(frustumPlanes[planeIndex].coeffs);
        iassert( length > 0 );
        scale = 1.0 / length;
        Vec4Scale(frustumPlanes[planeIndex].coeffs, scale, frustumPlanes[planeIndex].coeffs);

        R_SetDpvsPlaneSides(&frustumPlanes[planeIndex]);
    }
}

double __cdecl R_FurthestPointOnWinding(const float (*points)[3], int pointCount, const DpvsPlane *plane)
{
    float v4; // [esp+0h] [ebp-24h]
    float v5; // [esp+4h] [ebp-20h]
    float v6; // [esp+8h] [ebp-1Ch]
    float v7; // [esp+Ch] [ebp-18h]
    int pointIndex; // [esp+18h] [ebp-Ch]
    int pointIndexa; // [esp+18h] [ebp-Ch]
    float distMax; // [esp+20h] [ebp-4h]

    v7 = Vec3Dot(plane->coeffs, (const float *)points) + plane->coeffs[3];
    v6 = Vec3Dot(plane->coeffs, &(*points)[3 * pointCount - 3]) + plane->coeffs[3];
    if (v6 >= (double)v7)
    {
        distMax = v6;
        for (pointIndexa = pointCount - 2; pointIndexa > 0; --pointIndexa)
        {
            v4 = Vec3Dot(plane->coeffs, &(*points)[3 * pointIndexa]) + plane->coeffs[3];
            if (v4 < (double)distMax)
                break;
            distMax = v4;
        }
    }
    else
    {
        distMax = v7;
        for (pointIndex = 1; pointIndex < pointCount - 1; ++pointIndex)
        {
            v5 = Vec3Dot(plane->coeffs, &(*points)[3 * pointIndex]) + plane->coeffs[3];
            if (v5 < (double)distMax)
                break;
            distMax = v5;
        }
    }
    return distMax;
}

char __cdecl R_PortalBehindAnyPlane(const GfxPortal *portal, const DpvsPlane *planes, int planeCount)
{
    while (planeCount)
    {
        if (R_PortalBehindPlane(portal, planes))
            return 1;
        --planeCount;
        ++planes;
    }
    return 0;
}

char __cdecl R_PortalBehindPlane(const GfxPortal *portal, const DpvsPlane *plane)
{
    float v3; // [esp+0h] [ebp-Ch]
    int c; // [esp+4h] [ebp-8h]
    float *v; // [esp+8h] [ebp-4h]

    v = (float *)portal->vertices;
    for (c = portal->vertexCount; c; --c)
    {
        v3 = Vec3Dot(plane->coeffs, v) + plane->coeffs[3];
        if (v3 > 0.0)
            return 0;
        v += 3;
    }
    return 1;
}

const float (*__cdecl R_ChopPortalWinding(
    const float (*vertsIn)[3],
    int *vertexCount,
    const DpvsPlane *plane,
    float (*vertsOut)[3]))[3]
{
    float v5; // [esp+0h] [ebp-2D0h]
    float v6; // [esp+4h] [ebp-2CCh]
    float v7; // [esp+8h] [ebp-2C8h]
    float *v8; // [esp+Ch] [ebp-2C4h]
    float *v9; // [esp+10h] [ebp-2C0h]
    float *v10; // [esp+14h] [ebp-2BCh]
    float *v11; // [esp+18h] [ebp-2B8h]
    float v12; // [esp+1Ch] [ebp-2B4h]
    uint8_t sideForVert[136]; // [esp+20h] [ebp-2B0h]
    float lerpFactor; // [esp+ACh] [ebp-224h]
    int backCount; // [esp+B0h] [ebp-220h]
    int vertexIndex; // [esp+B4h] [ebp-21Ch]
    float distForVert[131]; // [esp+B8h] [ebp-218h]
    int newVertCount; // [esp+2C4h] [ebp-Ch]
    int frontCount; // [esp+2C8h] [ebp-8h]
    const float *v; // [esp+2CCh] [ebp-4h]

    if (*vertexCount < 0 || *vertexCount > 128)
        Com_Error(ERR_DROP, "DPVS portal winding exceeds 128 vertices");
    frontCount = 0;
    backCount = 0;
    for (vertexIndex = 0; vertexIndex < *vertexCount; ++vertexIndex)
    {
        v12 = Vec3Dot(plane->coeffs, &(*vertsIn)[3 * vertexIndex]) + plane->coeffs[3];
        distForVert[vertexIndex] = v12 - EQUAL_EPSILON;
        sideForVert[vertexIndex] = 2;
        if (distForVert[vertexIndex] >= -EQUAL_EPSILON)
        {
            if (distForVert[vertexIndex] > EQUAL_EPSILON)
            {
                sideForVert[vertexIndex] = 0;
                ++frontCount;
            }
        }
        else
        {
            sideForVert[vertexIndex] = 1;
            ++backCount;
        }
    }
    if (frontCount)
    {
        if (backCount)
        {
            sideForVert[vertexIndex] = sideForVert[0];
            distForVert[vertexIndex] = distForVert[0];
            newVertCount = 0;
            for (vertexIndex = 0; vertexIndex < *vertexCount && newVertCount < 128; ++vertexIndex)
            {
                if (sideForVert[vertexIndex] == 2)
                {
                    v10 = &(*vertsOut)[3 * newVertCount];
                    v11 = (float *)&(*vertsIn)[3 * vertexIndex];
                    *v10 = *v11;
                    v10[1] = v11[1];
                    v10[2] = v11[2];
                    ++newVertCount;
                }
                else
                {
                    if (!sideForVert[vertexIndex])
                    {
                        v8 = &(*vertsOut)[3 * newVertCount];
                        v9 = (float *)&(*vertsIn)[3 * vertexIndex];
                        *v8 = *v9;
                        v8[1] = v9[1];
                        v8[2] = v9[2];
                        ++newVertCount;
                    }
                    if (sideForVert[vertexIndex + 1] != 2 && sideForVert[vertexIndex + 1] != sideForVert[vertexIndex])
                    {
                        lerpFactor = distForVert[vertexIndex] / (distForVert[vertexIndex] - distForVert[vertexIndex + 1]);
                        v = &(*vertsIn)[3 * ((vertexIndex + 1) % *vertexCount)];
                        v7 = (*v - (float)(*vertsIn)[3 * vertexIndex]) * lerpFactor + (float)(*vertsIn)[3 * vertexIndex];
                        (*vertsOut)[3 * newVertCount] = v7;
                        v6 = (v[1] - (float)(*vertsIn)[3 * vertexIndex + 1]) * lerpFactor + (float)(*vertsIn)[3 * vertexIndex + 1];
                        (*vertsOut)[3 * newVertCount + 1] = v6;
                        v5 = (v[2] - (float)(*vertsIn)[3 * vertexIndex + 2]) * lerpFactor + (float)(*vertsIn)[3 * vertexIndex + 2];
                        (*vertsOut)[3 * newVertCount++ + 2] = v5;
                    }
                }
            }
            iassert( newVertCount >= 3 );
            *vertexCount = newVertCount;
            return vertsOut;
        }
        else
        {
            return vertsIn;
        }
    }
    else
    {
        *vertexCount = 0;
        return 0;
    }
}

void __cdecl R_SetAncestorListStatus(GfxPortal *portal, bool isAncestor)
{
    while (portal)
    {
        iassert( portal->writable.isAncestor != isAncestor );
        portal->writable.isAncestor = isAncestor;
        portal = portal->writable.queuedParent;
    }
}

void __cdecl R_CopyClipPlane(const DpvsPlane *in, DpvsPlane *out)
{
    *out = *in;
}

double __cdecl R_NearestPointOnWinding(const float (*points)[3], int pointCount, const DpvsPlane *plane)
{
    float v4; // [esp+0h] [ebp-24h]
    float v5; // [esp+4h] [ebp-20h]
    float v6; // [esp+8h] [ebp-1Ch]
    float v7; // [esp+Ch] [ebp-18h]
    float distMin; // [esp+18h] [ebp-Ch]
    int pointIndex; // [esp+1Ch] [ebp-8h]
    int pointIndexa; // [esp+1Ch] [ebp-8h]

    v7 = Vec3Dot(plane->coeffs, (const float *)points) + plane->coeffs[3];
    v6 = Vec3Dot(plane->coeffs, &(*points)[3 * pointCount - 3]) + plane->coeffs[3];
    if (v6 <= (double)v7)
    {
        distMin = v6;
        for (pointIndexa = pointCount - 2; pointIndexa > 0; --pointIndexa)
        {
            v4 = Vec3Dot(plane->coeffs, &(*points)[3 * pointIndexa]) + plane->coeffs[3];
            if (v4 > (double)distMin)
                break;
            distMin = v4;
        }
    }
    else
    {
        distMin = v7;
        for (pointIndex = 1; pointIndex < pointCount - 1; ++pointIndex)
        {
            v5 = Vec3Dot(plane->coeffs, &(*points)[3 * pointIndex]) + plane->coeffs[3];
            if (v5 > (double)distMin)
                break;
            distMin = v5;
        }
    }
    return distMin;
}

char __cdecl R_IsCellInList(const GfxCell *cell, const GfxCell **list, int count)
{
    int index; // [esp+0h] [ebp-4h]

    for (index = 0; index < count; ++index)
    {
        if (cell == list[index])
            return 1;
    }
    return 0;
}

int __cdecl R_AddCellToList(const GfxCell *cell, const GfxCell **list, int count)
{
    if (count >= 1024)
        Com_Error(ERR_DROP, "DPVS further-cell list exceeds 1024 cells");
    list[count] = cell;
    return count + 1;
}

int __cdecl R_CellForPoint(const GfxWorld *world, const float *origin)
{
    const uint16_t *node; // [esp+4h] [ebp-1Ch]
    int cellIndex; // [esp+14h] [ebp-Ch]
    int cellCount; // [esp+18h] [ebp-8h]

    iassert( world );
    node = world->dpvsPlanes.nodes;
    cellCount = world->dpvsPlanes.cellCount + 1;
    while (1)
    {
        //cellIndex = node[0];
        //if (cellIndex - cellCount < 0)
        //    break;
        //plane = &world->dpvsPlanes.planes[cellIndex - cellCount];
        //d = Vec3Dot(origin, plane->normal) - plane->dist;
        //int side = (d <= 0.0);
        //unsigned short offset = (node[1] - 2);
        //offset *= side;
        //
        //node = (const uint16_t *)((const char *)node + (offset * 2) + 4);
        //
        //mnode_t *nodemethod2 = (const uint16_t *)((const char *)node + 2 * side * (node[1] - 2) + 4);
        //iassert(node == nodemethod2);

        cellIndex = node[0];
        if (cellIndex - cellCount < 0)
            break;
        if (!world->dpvsPlanes.planes)
            Com_Error(ERR_DROP, "DPVS BSP plane array missing");
        cplane_s *v2 = &world->dpvsPlanes.planes[cellIndex - cellCount];
        node = (const uint16_t *)((const char *)node
            + 2
            * ((float)((float)((float)((float)(*origin * v2->normal[0]) + (float)(origin[1] * v2->normal[1]))
                + (float)(origin[2] * v2->normal[2]))
                - v2->dist) <= 0.0)
            * (node[1] - 2)
            + 4);
    }
    return cellIndex - 1;
}

float __cdecl R_DpvsPlaneMaxSignedDistToBox(const DpvsPlane *plane, const float *minmax)
{
    return (float)((float)(*(const float *)((char *)minmax + plane->side[2]) * plane->coeffs[2])
        + (float)((float)(*(const float *)((char *)minmax + plane->side[1]) * plane->coeffs[1])
            + (float)((float)(*(const float *)((char *)minmax + plane->side[0]) * plane->coeffs[0])
                + plane->coeffs[3])));
}

float __cdecl R_DpvsPlaneMinSignedDistToBox(const DpvsPlane *plane, const float *minmax)
{
    return (float)((float)(*(const float *)((char *)minmax - plane->side[2] + 28) * plane->coeffs[2])
        + (float)((float)(*(const float *)((char *)minmax - plane->side[1] + 20) * plane->coeffs[1])
            + (float)((float)(*(const float *)((char *)minmax - plane->side[0] + 12) * plane->coeffs[0])
                + plane->coeffs[3])));
}

void __cdecl R_SetDpvsPlaneSides(DpvsPlane *plane)
{
    plane->side[0] = (plane->coeffs[0] <= 0.0f) ? 0 : 12;
    plane->side[1] = (plane->coeffs[1] <= 0.0f) ? 4 : 16;
    plane->side[2] = (plane->coeffs[2] <= 0.0f) ? 8 : 20;
}

static uint16_t *R_CheckedSortedSurfaceRange(GfxWorld &world, unsigned start, unsigned count)
{
    const uint64_t sortedCount = static_cast<uint64_t>(world.dpvs.staticSurfaceCount) +
        world.dpvs.staticSurfaceCountNoDecal;
    if (!world.dpvs.sortedSurfIndex || start > sortedCount || count > sortedCount - start)
        Com_Error(ERR_DROP, "DPVS sorted surface range out of bounds");
    uint16_t *indices = world.dpvs.sortedSurfIndex + start;
    for (unsigned i = 0; i < count; ++i)
        if (indices[i] >= world.dpvs.staticSurfaceCount || indices[i] >= world.surfaceCount)
            Com_Error(ERR_DROP, "DPVS world surface index out of bounds");
    return indices;
}

void __cdecl DpvsContext::R_AddAabbTreeSurfacesInFrustum_r(const GfxAabbTree *tree, const DpvsClipPlaneSet *clipSet)
{
    int v2; // [esp+10h] [ebp-D0h]
    int v3; // [esp+14h] [ebp-CCh]
    const DpvsPlane *v4; // [esp+18h] [ebp-C8h]
    uint32_t m; // [esp+1Ch] [ebp-C4h]
    GfxSurface *v6; // [esp+24h] [ebp-BCh]
    int v7; // [esp+28h] [ebp-B8h]
    int v8; // [esp+2Ch] [ebp-B4h]
    const DpvsPlane *v9; // [esp+30h] [ebp-B0h]
    uint32_t j; // [esp+34h] [ebp-ACh]
    GfxStaticModelInst *v11; // [esp+38h] [ebp-A8h]
    uint32_t surfaceCountNoDecal; // [esp+3Ch] [ebp-A4h]
    uint16_t *v13; // [esp+40h] [ebp-A0h]
    int startSurfIndexNoDecal; // [esp+44h] [ebp-9Ch]
    uint16_t *smodelIndexes; // [esp+48h] [ebp-98h]
    uint32_t v16; // [esp+4Ch] [ebp-94h]
    uint32_t k; // [esp+50h] [ebp-90h]
    uint32_t i; // [esp+54h] [ebp-8Ch]
    uint32_t surfaceCount; // [esp+58h] [ebp-88h]
    uint16_t *v20; // [esp+5Ch] [ebp-84h]
    uint32_t startSurfIndex; // [esp+60h] [ebp-80h]
    uint16_t *indices; // [esp+64h] [ebp-7Ch]
    uint32_t smodelIndexCount; // [esp+68h] [ebp-78h]
    const DpvsPlane *plane; // [esp+6Ch] [ebp-74h]
    uint32_t planeCount; // [esp+70h] [ebp-70h]
    const GfxAabbTree *children; // [esp+74h] [ebp-6Ch]
    uint32_t smodelIndexIter; // [esp+78h] [ebp-68h]
    DpvsClipPlaneSet clipSetChild; // [esp+80h] [ebp-60h] BYREF
    uint32_t childIndex; // [esp+CCh] [ebp-14h]
    uint32_t surfNodeIndex; // [esp+D0h] [ebp-10h]
    uint32_t planeIndex; // [esp+D4h] [ebp-Ch]
    uint32_t childCount; // [esp+D8h] [ebp-8h]
    uint32_t smodelIndex; // [esp+DCh] [ebp-4h]

    clipSetChild.count = 0;
    planeCount = clipSet->count;
    for (planeIndex = 0; planeIndex < planeCount; ++planeIndex)
    {
        plane = clipSet->planes[planeIndex];
        if (*(float *)((char *)tree->mins + plane->side[0]) * plane->coeffs[0]
            + plane->coeffs[3]
            + *(float *)((char *)tree->mins + plane->side[1]) * plane->coeffs[1]
            + *(float *)((char *)tree->mins + plane->side[2]) * plane->coeffs[2] <= 0.0)
            return;
        if (*(float *)((char *)tree->maxs - plane->side[0]) * plane->coeffs[0]
            + plane->coeffs[3]
            + *(float *)((char *)&tree->maxs[2] - plane->side[1]) * plane->coeffs[1]
            + *(float *)((char *)&tree->startSurfIndex - plane->side[2]) * plane->coeffs[2] < 0.0)
            clipSetChild.planes[clipSetChild.count++] = plane;
    }
    if (clipSetChild.count)
    {
        if (tree->childCount)
        {
            children = (const GfxAabbTree *)((char *)tree + tree->childrenOffset);
            childCount = tree->childCount;
            for (childIndex = 0; childIndex < childCount; ++childIndex)
                R_AddAabbTreeSurfacesInFrustum_r(&children[childIndex], &clipSetChild);
        }
        else
        {
            if (debugBox && (r_showAabbTrees->current.integer))
                debugBox( tree->mins, tree->maxs, colorOrange);
            if (drawSModels)
            {
                v16 = tree->smodelIndexCount;
                if (tree->smodelIndexCount)
                {
                    smodelIndexes = tree->smodelIndexes;
                    for (i = 0; i < v16; ++i)
                    {
                        v7 = smodelIndexes[i];
                        if (static_cast<uint32_t>(v7) >= world->dpvs.smodelCount)
                            Com_Error(ERR_DROP, "DPVS static model index out of range");
                        //if (!*(_BYTE *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 24) + v7))
                        if (!g_smodelVisData[v7])
                        {
                            for (j = 0; j < clipSetChild.count; ++j)
                            {
                                v9 = clipSetChild.planes[j];
                                v11 = &world->dpvs.smodelInsts[v7];
                                if (*(float *)((char *)v11->mins + v9->side[0]) * v9->coeffs[0]
                                    + v9->coeffs[3]
                                    + *(float *)((char *)v11->mins + v9->side[1]) * v9->coeffs[1]
                                    + *(float *)((char *)v11->mins + v9->side[2]) * v9->coeffs[2] <= 0.0)
                                {
                                    v8 = 1;
                                    goto LABEL_42;
                                }
                            }
                            v8 = 0;
                        LABEL_42:
                            if (!v8)
                            {
                                //*(_BYTE *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 24) + v7) = 1;
                                g_smodelVisData[v7] = 1;
                            }
                        }
                    }
                }
            }
            if (drawWorld)
            {
                if (r_drawDecals->current.enabled)
                {
                    startSurfIndexNoDecal = tree->startSurfIndex;
                    surfaceCountNoDecal = tree->surfaceCount;
                }
                else
                {
                    startSurfIndexNoDecal = tree->startSurfIndexNoDecal;
                    surfaceCountNoDecal = tree->surfaceCountNoDecal;
                }
                if (surfaceCountNoDecal)
                {
                    v13 = R_CheckedSortedSurfaceRange(*world, startSurfIndexNoDecal, surfaceCountNoDecal);
                    for (k = 0; k < surfaceCountNoDecal; ++k)
                    {
                        v2 = v13[k];
                        //if (!*(_BYTE *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 28) + v2))
                        if (!g_surfaceVisData[v2])
                        {
                            v6 = &world->dpvs.surfaces[v2];
                            for (m = 0; m < clipSetChild.count; ++m)
                            {
                                v4 = clipSetChild.planes[m];
                                if (*(float *)((char *)v6->bounds[0] + v4->side[0]) * v4->coeffs[0]
                                    + v4->coeffs[3]
                                    + *(float *)((char *)v6->bounds[0] + v4->side[1]) * v4->coeffs[1]
                                    + *(float *)((char *)v6->bounds[0] + v4->side[2]) * v4->coeffs[2] <= 0.0)
                                {
                                    v3 = 1;
                                    goto LABEL_59;
                                }
                            }
                            v3 = 0;
                        LABEL_59:
                            if (!v3)
                            {
                                if (debugBox && (r_showAabbTrees->current.integer & 2) != 0)
                                    debugBox( v6->bounds[0], v6->bounds[1], colorGreen);
                                //*(_BYTE *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 28) + v2) = 1;
                                g_surfaceVisData[v2] = 1;
                            }
                        }
                    }
                }
            }
        }
    }
    else
    {
        if (debugBox && (r_showAabbTrees->current.integer))
            debugBox( tree->mins, tree->maxs, colorYellow);
        if (drawSModels)
        {
            smodelIndexCount = tree->smodelIndexCount;
            if (tree->smodelIndexCount)
            {
                indices = tree->smodelIndexes;
                for (smodelIndexIter = 0; smodelIndexIter < smodelIndexCount; ++smodelIndexIter)
                {
                    smodelIndex = indices[smodelIndexIter];
                    if (smodelIndex >= world->dpvs.smodelCount)
                        Com_Error(ERR_DROP, "DPVS static model index out of range");
                    //*(_BYTE *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 24) + smodelIndex) = 1;
                    g_smodelVisData[smodelIndex] = 1;
                }
            }
        }
        if (drawWorld)
        {
            if (r_drawDecals->current.enabled)
            {
                startSurfIndex = tree->startSurfIndex;
                surfaceCount = tree->surfaceCount;
            }
            else
            {
                startSurfIndex = tree->startSurfIndexNoDecal;
                surfaceCount = tree->surfaceCountNoDecal;
            }
            if (surfaceCount)
            {
                v20 = R_CheckedSortedSurfaceRange(*world, startSurfIndex, surfaceCount);
                for (surfNodeIndex = 0; surfNodeIndex < surfaceCount; ++surfNodeIndex)
                {
                    g_surfaceVisData[v20[surfNodeIndex]] = 1;
                }
            }
        }
    }
}

void __cdecl DpvsContext::R_AddCellStaticSurfacesInFrustum(DpvsStaticCellCmd *dpvsCell)
{
    DpvsPlane clipPlanePool[16]; // [esp+CCh] [ebp-1A0h] BYREF
    const GfxAabbTree *tree; // [esp+210h] [ebp-5Ch]
    DpvsClipPlaneSet clipSet; // [esp+214h] [ebp-58h] BYREF
    DpvsPlanes planes; // [esp+260h] [ebp-Ch]
    uint32_t planeIndex; // [esp+268h] [ebp-4h]

    tree = dpvsCell->cell->aabbTree;
    if (tree)
    {
        planes.count = dpvsCell->planeCount;
        iassert( planes.count <= (10 + 4 + 2) );
        iassert( planes.count );
        planes.planes = dpvsCell->planes;
        for (planeIndex = 0; planeIndex < planes.count; ++planeIndex)
        {
            R_CopyClipPlane(&planes.planes[planeIndex], &clipPlanePool[planeIndex]);
            clipSet.planes[planeIndex] = &clipPlanePool[planeIndex];
        }
        clipSet.count = planes.count;
        R_AddAabbTreeSurfacesInFrustum_r(tree, &clipSet);
    }
}

void __cdecl DpvsContext::R_AddCullGroupSurfacesInFrustum(int cullGroupIndex, const DpvsPlane *planes, int planeCount)
{
    int v3; // [esp+4h] [ebp-20h]
    int i; // [esp+Ch] [ebp-18h]
    const uint16_t *indices; // [esp+18h] [ebp-Ch]
    GfxCullGroup *group; // [esp+1Ch] [ebp-8h]
    int count; // [esp+20h] [ebp-4h]

    if (cullGroupIndex < 0 || cullGroupIndex >= world->cullGroupCount || !world->dpvs.cullGroups)
        Com_Error(ERR_DROP, "DPVS cull group index out of bounds");
    group = &world->dpvs.cullGroups[cullGroupIndex];
    for (i = 0; i < planeCount; ++i)
    {
        if (*(float *)((char *)group->mins + planes->side[0]) * planes->coeffs[0]
            + planes->coeffs[3]
            + *(float *)((char *)group->mins + planes->side[1]) * planes->coeffs[1]
            + *(float *)((char *)group->mins + planes->side[2]) * planes->coeffs[2] <= 0.0)
        {
            v3 = 1;
            goto LABEL_7;
        }
        ++planes;
    }
    v3 = 0;
LABEL_7:
    if (!v3)
    {
        if (debugBox && (r_showPortals->current.integer & 1) != 0)
            debugBox( group->mins, group->maxs, colorLtYellow);
        if (group->surfaceCount)
        {
            if (group->surfaceCount < 0 || group->startSurfIndex < 0)
                Com_Error(ERR_DROP, "DPVS cull group surface range out of bounds");
            indices = R_CheckedSortedSurfaceRange(*world, group->startSurfIndex, group->surfaceCount);
            for (count = 0; count < group->surfaceCount; ++count)
            {
                //*(_BYTE *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 28) + indices[count]) = 1;
                g_surfaceVisData[indices[count]] = 1;
            }
        }
    }
}

void __cdecl DpvsContext::R_AddCellCullGroupsInFrustum(DpvsStaticCellCmd *dpvsCell)
{
    const GfxCell *cell; // [esp+0h] [ebp-10h]
    int *cullGroup; // [esp+8h] [ebp-8h]
    int count; // [esp+Ch] [ebp-4h]

    cell = dpvsCell->cell;
    if (cell->cullGroupCount < 0 || (cell->cullGroupCount && !cell->cullGroups))
        Com_Error(ERR_DROP, "DPVS cell cull group list unavailable");
    count = cell->cullGroupCount;
    cullGroup = cell->cullGroups;
    while (count)
    {
        R_AddCullGroupSurfacesInFrustum(*cullGroup, dpvsCell->planes, dpvsCell->planeCount);
        --count;
        ++cullGroup;
    }
}

void DpvsContext::DispatchCell(const GfxCell *cell, const DpvsPlane *planes,
    unsigned char planeCount, unsigned char frustumPlaneCount)
{
    if (dispatchCell)
        dispatchCell(cell, planes, planeCount, frustumPlaneCount);
    else
    {
        DpvsStaticCellCmd command{planes, cell, planeCount, frustumPlaneCount, 0};
        R_AddCellStaticSurfacesInFrustum(&command);
        if (drawWorld)
            R_AddCellCullGroupsInFrustum(&command);
    }
}

void R_ClearStaticDpvsView(GfxWorld &world, unsigned viewIndex, bool clearSurfaces)
{
    if (world.dpvs.smodelCount)
        memset(world.dpvs.smodelVisData[viewIndex], 0, world.dpvs.smodelCount);
    if (clearSurfaces && world.models->surfaceCount)
        memset(world.dpvs.surfaceVisData[viewIndex], 0, world.models->surfaceCount);
}

bool R_ComputeStaticCameraVisibility(GfxWorld &world, DpvsGlobals &dpvs,
    const GfxViewParms &viewParms, unsigned localClientNum, float farPlaneDist,
    bool includeWorldSurfaces)
{
    if (localClientNum >= 4 || world.dpvsPlanes.cellCount <= 0 ||
        world.dpvsPlanes.cellCount > 1024 || !world.cells ||
        !world.dpvsPlanes.nodes ||
        (world.dpvs.smodelCount && (!world.dpvs.smodelInsts || !world.dpvs.smodelVisData[0])))
        return false;
    if (includeWorldSurfaces &&
        (world.surfaceCount < 0 || world.dpvs.staticSurfaceCount > static_cast<unsigned>(world.surfaceCount) ||
         (world.dpvs.staticSurfaceCount &&
          (!world.dpvs.surfaceVisData[0] || !world.dpvs.surfaces || !world.dpvs.sortedSurfIndex))))
        return false;
    for (int cellIndex = 0; cellIndex < world.dpvsPlanes.cellCount; ++cellIndex)
    {
        GfxCell &cell = world.cells[cellIndex];
        if (cell.portalCount < 0 || (cell.portalCount && !cell.portals)) return false;
        for (int portalIndex = 0; portalIndex < cell.portalCount; ++portalIndex)
        {
            GfxPortal &portal = cell.portals[portalIndex];
            const uintptr_t target = reinterpret_cast<uintptr_t>(portal.cell);
            const uintptr_t begin = reinterpret_cast<uintptr_t>(world.cells);
            if (target < begin || (target - begin) % sizeof(GfxCell) ||
                (target - begin) / sizeof(GfxCell) >= static_cast<unsigned>(world.dpvsPlanes.cellCount) ||
                !portal.vertices || portal.vertexCount < 3 || portal.vertexCount > 128)
                return false;
            portal.writable = {};
        }
    }
    R_ClearStaticDpvsView(world, 0, false);
    if (includeWorldSurfaces && world.dpvs.staticSurfaceCount)
        memset(world.dpvs.surfaceVisData[0], 0, world.dpvs.staticSurfaceCount);
    DpvsContext context{&world, dpvs, localClientNum, farPlaneDist};
    context.g_smodelVisData = world.dpvs.smodelVisData[0];
    context.drawWorld = includeWorldSurfaces;
    context.g_surfaceVisData = world.dpvs.surfaceVisData[0];
    context.R_SetupWorldSurfacesDpvs(&viewParms);
    const int cameraCell = R_CellForPoint(&world, viewParms.origin);
    if (cameraCell >= world.dpvsPlanes.cellCount) return false;
    dpvs.cameraCellIndex = static_cast<unsigned>(cameraCell);
    context.R_AddWorldSurfacesPortalWalk(cameraCell);
    // Transient scratch and matrices belong to this completed synchronous call.
    dpvs.portalQueue = nullptr;
    dpvs.nextFreeHullPoints = nullptr;
    dpvs.viewProjMtx = nullptr;
    dpvs.invViewProjMtx = nullptr;
    return true;
}
