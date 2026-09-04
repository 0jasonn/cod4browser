#include <universal/q_shared.h>
#include "r_dpvs.h"
#include "r_dpvs_core.h"
#include <qcommon/mem_track.h>
#include "r_model_lighting.h"
#include "r_dvars.h"
#include <DynEntity/DynEntity_client.h>
#include "r_drawsurf.h"
#include "r_primarylights.h"
#include "r_model.h"
#include <qcommon/threads.h>
#include <universal/com_convexhull.h>
#include "r_workercmds.h"
#include "r_utils.h"
#include "r_light.h"
#include <devgui/devgui.h>
#include <cgame/cg_local.h>
#include "rb_light.h"
#include "r_sunshadow.h" // SCENE_VIEW_CAMERA
#include <universal/profile.h>

#ifdef KISAK_MP
#include <cgame_mp/cg_local_mp.h>
#elif KISAK_SP
#include <cgame/cg_ents.h>
#endif

DpvsGlobals dpvsGlob;

thread_local DpvsView *g_dpvsView;
thread_local int g_viewIndex;
thread_local EntVisData g_dynEntVisData;
thread_local byte *g_smodelVisData;
thread_local byte *g_surfaceVisData;

DpvsContext R_NativeDpvsContext()
{
    DpvsContext context{rgp.world, dpvsGlob,
        static_cast<unsigned>(scene.dpvs.localClientNum), static_cast<float>(R_GetFarPlaneDist())};
    context.drawSModels = rg.drawSModels;
    context.drawWorld = rg.drawWorld;
    context.g_smodelVisData = g_smodelVisData;
    context.g_surfaceVisData = g_surfaceVisData;
    context.dispatchCell = R_AddCellSurfacesAndCullGroupsInFrustumDelayed;
    context.flushCells = [] {
        R_WarnOncePerFrame(R_WARN_PORTAL_PLANES);
        R_WaitWorkerCmdsOfType(WRKCMD_DPVS_CELL_STATIC);
        R_WaitWorkerCmdsOfType(WRKCMD_DPVS_CELL_DYN_MODEL);
        R_WaitWorkerCmdsOfType(WRKCMD_DPVS_CELL_SCENE_ENT);
        R_WaitWorkerCmdsOfType(WRKCMD_DPVS_ENTITY);
        R_WaitWorkerCmdsOfType(WRKCMD_DPVS_CELL_DYN_BRUSH);
    };
    context.debugBox = [](const float *mins, const float *maxs, const float *color) {
        R_AddDebugBox(&frontEndDataOut->debugGlobals, mins, maxs, color);
    };
    context.debugLine = [](const float *start, const float *end, const float *color) {
        R_AddDebugLine(&frontEndDataOut->debugGlobals, start, end, color);
    };
    context.debugPolygon = [](const float *color, int count, float (*verts)[3]) {
        R_AddDebugPolygon(&frontEndDataOut->debugGlobals, color, count, verts);
    };
    return context;
}

void __cdecl TRACK_r_dpvs()
{
    track_static_alloc_internal(&dpvsGlob, 44664, "dpvsGlob", 18);
}

char *__cdecl R_PortalAssertMsg()
{
    return R_NativeDpvsContext().R_PortalAssertMsg();
}

uint32_t __cdecl R_FindNearestReflectionProbeInCell(
    const GfxWorld *world,
    const GfxCell *cell,
    const float *origin)
{
    float diff[3]; // [esp+4h] [ebp-1Ch] BYREF
    float bestProbeDist; // [esp+10h] [ebp-10h]
    uint8_t bestProbe; // [esp+16h] [ebp-Ah]
    uint8_t probeIndex; // [esp+17h] [ebp-9h]
    float testProbeDist; // [esp+18h] [ebp-8h]
    uint32_t cellProbeIndex; // [esp+1Ch] [ebp-4h]

    iassert( world->reflectionProbeCount < 0xff );
    bestProbe = 0;
    bestProbeDist = FLT_MAX;
    testProbeDist = FLT_MAX;
    iassert( cell->reflectionProbeCount > 0 );
    for (cellProbeIndex = 0; cellProbeIndex < cell->reflectionProbeCount; ++cellProbeIndex)
    {
        probeIndex = cell->reflectionProbes[cellProbeIndex];
        if (probeIndex >= world->reflectionProbeCount)
            MyAssertHandler(
                ".\\r_dpvs.cpp",
                714,
                0,
                "probeIndex doesn't index world->reflectionProbeCount\n\t%i not in [0, %i)",
                probeIndex,
                world->reflectionProbeCount);
        Vec3Sub(origin, world->reflectionProbes[probeIndex].origin, diff);
        testProbeDist = Vec3LengthSq(diff);
        if (bestProbeDist > (double)testProbeDist)
        {
            bestProbeDist = testProbeDist;
            bestProbe = probeIndex;
        }
    }
    return bestProbe;
}

uint32_t __cdecl R_FindNearestReflectionProbe(const GfxWorld *world, const float *origin)
{
    float diff[3]; // [esp+4h] [ebp-18h] BYREF
    float bestProbeDist; // [esp+10h] [ebp-Ch]
    uint8_t bestProbe; // [esp+16h] [ebp-6h]
    uint8_t probeIndex; // [esp+17h] [ebp-5h]
    float testProbeDist; // [esp+18h] [ebp-4h]

    iassert(world->reflectionProbeCount < 0xff);

    bestProbe = 0;
    bestProbeDist = FLT_MAX;
    testProbeDist = FLT_MAX;
    for (probeIndex = 1; probeIndex < world->reflectionProbeCount; ++probeIndex)
    {
        Vec3Sub(origin, world->reflectionProbes[probeIndex].origin, diff);
        testProbeDist = Vec3LengthSq(diff);
        if (bestProbeDist > (double)testProbeDist)
        {
            bestProbeDist = testProbeDist;
            bestProbe = probeIndex;
        }
    }
    return bestProbe;
}

uint32_t __cdecl R_CalcReflectionProbeIndex(const float *origin)
{
    uint32_t cellIndex; // [esp+0h] [ebp-4h]

    cellIndex = R_CellForPoint(rgp.world, origin);

    if (cellIndex == -1)
        return R_FindNearestReflectionProbe(rgp.world, origin);

    bcassert(cellIndex, rgp.world->dpvsPlanes.cellCount);
    return R_FindNearestReflectionProbeInCell(rgp.world, &rgp.world->cells[cellIndex], origin);
}

void __cdecl R_AddAllSceneEntSurfacesCamera(const GfxViewInfo *viewInfo)
{
    GfxSceneDynBrush *sceneDynBrush; // [esp+70h] [ebp-70h]
    DynEntityPose *dynEntPose; // [esp+74h] [ebp-6Ch]
    DynEntityPose *dynEntPosea; // [esp+74h] [ebp-6Ch]
    GfxSceneModel *sceneModel; // [esp+78h] [ebp-68h]
    GfxLightingInfo lightingInfo; // [esp+7Ch] [ebp-64h] BYREF
    GfxDrawSurf *lastDrawSurfs[3]; // [esp+80h] [ebp-60h] BYREF
    uint32_t sceneEntCount; // [esp+8Ch] [ebp-54h]
    DynEntityClient *dynEntClient; // [esp+90h] [ebp-50h]
    uint32_t reflectionProbeIndex; // [esp+94h] [ebp-4Ch]
    GfxEntity *gfxEnt; // [esp+98h] [ebp-48h]
    const DynEntityDef *dynEntDef; // [esp+9Ch] [ebp-44h]
    int depthHack; // [esp+A0h] [ebp-40h]
    uint32_t sceneEntIndex; // [esp+A4h] [ebp-3Ch]
    GfxSceneEntity *sceneEnt; // [esp+A8h] [ebp-38h]
    uint16_t *cachedLightingHandle; // [esp+ACh] [ebp-34h]
    int isShadowReceiver; // [esp+B0h] [ebp-30h]
    GfxSceneBrush *sceneBrush; // [esp+B4h] [ebp-2Ch]
    GfxDrawSurf *drawSurfs[3]; // [esp+B8h] [ebp-28h] BYREF
    uint16_t dynEntId; // [esp+C4h] [ebp-1Ch]
    uint32_t gfxEntIndex; // [esp+C8h] [ebp-18h]
    uint8_t *sceneEntVisData; // [esp+CCh] [ebp-14h]
    GfxSceneDynModel *sceneDynModel; // [esp+D0h] [ebp-10h]
    uint32_t lightingHandle; // [esp+D4h] [ebp-Ch]
    const GfxBrushModel *bmodel; // [esp+D8h] [ebp-8h]
    int drawSurfCount; // [esp+DCh] [ebp-4h]

    PROF_SCOPED("SceneEntSurfaces");
    {
        drawSurfs[0] = scene.drawSurfs[2];
        lastDrawSurfs[0] = &scene.drawSurfs[2][scene.maxDrawSurfCount[2]];
        drawSurfs[1] = scene.drawSurfs[5];
        lastDrawSurfs[1] = &scene.drawSurfs[5][scene.maxDrawSurfCount[5]];
        drawSurfs[2] = scene.drawSurfs[11];
        lastDrawSurfs[2] = &scene.drawSurfs[11][scene.maxDrawSurfCount[11]];
        sceneEntCount = scene.sceneDObjCount;
        sceneEntVisData = scene.sceneDObjVisData[0];
        for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
        {
            if (sceneEntVisData[sceneEntIndex] == 1)
            {
                sceneEnt = &scene.sceneDObj[sceneEntIndex];
                iassert(sceneEnt->cull.state >= CULL_STATE_BOUNDED);
                cachedLightingHandle = (uint16_t *)LongNoSwap((uint32_t)sceneEnt->info.cachedLightingHandle);
                lightingHandle = R_AllocModelLighting_Box(
                    viewInfo,
                    sceneEnt->lightingOrigin,
                    sceneEnt->cull.mins,
                    sceneEnt->cull.maxs,
                    cachedLightingHandle,
                    &lightingInfo);
                if (lightingHandle)
                {
                    sceneEnt->reflectionProbeIndex = lightingInfo.reflectionProbeIndex;
                    R_AddDObjSurfacesCamera(sceneEnt, lightingHandle, lightingInfo.primaryLightIndex, drawSurfs, lastDrawSurfs);
                }
                else
                {
                    sceneEntVisData[sceneEntIndex] = 0;
                }
            }
        }
        sceneEntCount = scene.sceneModelCount;
        sceneEntVisData = scene.sceneModelVisData[0];
        for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
        {
            if (sceneEntVisData[sceneEntIndex] == 1)
            {
                sceneModel = &scene.sceneModel[sceneEntIndex];
                lightingHandle = R_AllocModelLighting_Sphere(
                    viewInfo,
                    sceneModel->lightingOrigin,
                    sceneModel->placement.base.origin,
                    sceneModel->radius,
                    sceneModel->cachedLightingHandle,
                    &lightingInfo);
                if (lightingHandle)
                {
                    gfxEntIndex = sceneModel->gfxEntIndex;
                    if (gfxEntIndex)
                    {
                        gfxEnt = &frontEndDataOut->gfxEnts[gfxEntIndex];
                        isShadowReceiver = sc_enable->current.enabled && (gfxEnt->renderFxFlags & 0x100) != 0;
                        depthHack = (gfxEnt->renderFxFlags & 2) != 0;
                    }
                    else
                    {
                        isShadowReceiver = 0;
                        depthHack = 0;
                    }
                    sceneModel->reflectionProbeIndex = lightingInfo.reflectionProbeIndex;
                    R_AddXModelSurfacesCamera(
                        &sceneModel->info,
                        sceneModel->model,
                        sceneModel->placement.base.origin,
                        sceneModel->gfxEntIndex,
                        lightingHandle,
                        lightingInfo.primaryLightIndex,
                        isShadowReceiver,
                        depthHack,
                        drawSurfs,
                        lastDrawSurfs,
                        lightingInfo.reflectionProbeIndex);
                }
                else
                {
                    sceneEntVisData[sceneEntIndex] = 0;
                }
            }
        }
        sceneEntCount = scene.sceneDynModelCount;
        sceneEntVisData = rgp.world->dpvsDyn.dynEntVisData[0][0];
        for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
        {
            sceneDynModel = &rgp.world->sceneDynModel[sceneEntIndex];
            dynEntId = sceneDynModel->dynEntId;
            if (sceneEntVisData[dynEntId] == 1)
            {
                dynEntPose = DynEnt_GetClientPose(dynEntId, DYNENT_DRAW_MODEL);
                dynEntClient = DynEnt_GetClientEntity(dynEntId, DYNENT_DRAW_MODEL);
                lightingHandle = R_AllocModelLighting_PrimaryLight(
                    dynEntPose->pose.origin,
                    dynEntId,
                    &dynEntClient->lightingHandle,
                    &lightingInfo);
                if (lightingHandle)
                {
                    dynEntDef = DynEnt_GetEntityDef(dynEntId, DYNENT_DRAW_MODEL);
                    R_AddXModelSurfacesCamera(
                        &sceneDynModel->info,
                        dynEntDef->xModel,
                        dynEntPose->pose.origin,
                        0,
                        lightingHandle,
                        lightingInfo.primaryLightIndex,
                        0,
                        0,
                        drawSurfs,
                        lastDrawSurfs,
                        lightingInfo.reflectionProbeIndex);
                }
                else
                {
                    sceneEntVisData[sceneEntIndex] = 0;
                }
            }
        }
        sceneEntCount = scene.sceneBrushCount;
        sceneEntVisData = scene.sceneBrushVisData[0];
        for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
        {
            if (sceneEntVisData[sceneEntIndex] == 1)
            {
                sceneBrush = &scene.sceneBrush[sceneEntIndex];
                reflectionProbeIndex = R_CalcReflectionProbeIndex(sceneBrush->placement.origin);
                sceneBrush->reflectionProbeIndex = reflectionProbeIndex;
                iassert(sceneBrush->reflectionProbeIndex == reflectionProbeIndex);
                R_AddBModelSurfacesCamera(&sceneBrush->info, sceneBrush->bmodel, drawSurfs, lastDrawSurfs, reflectionProbeIndex);
            }
        }
        sceneEntCount = scene.sceneDynBrushCount;
        sceneEntVisData = rgp.world->dpvsDyn.dynEntVisData[1][0];
        for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
        {
            sceneDynBrush = &rgp.world->sceneDynBrush[sceneEntIndex];
            dynEntId = sceneDynBrush->dynEntId;
            if (sceneEntVisData[dynEntId] == 1)
            {
                dynEntPosea = DynEnt_GetClientPose(dynEntId, DYNENT_DRAW_BRUSH);
                dynEntDef = DynEnt_GetEntityDef(dynEntId, DYNENT_DRAW_BRUSH);
                bmodel = R_GetBrushModel(dynEntDef->brushModel);
                reflectionProbeIndex = R_CalcReflectionProbeIndex(dynEntPosea->pose.origin);
                R_AddBModelSurfacesCamera((BModelDrawInfo *)sceneDynBrush, bmodel, drawSurfs, lastDrawSurfs, reflectionProbeIndex);
            }
        }
    }

    {
        PROF_SCOPED("SortSceneEntSurfaces");
        drawSurfCount = drawSurfs[0] - scene.drawSurfs[2];
        scene.drawSurfCount[2] = drawSurfCount;
        KISAK_NULLSUB();
        R_SortDrawSurfs(scene.drawSurfs[2], drawSurfCount);
        drawSurfCount = drawSurfs[1] - scene.drawSurfs[5];
        scene.drawSurfCount[5] = drawSurfCount;
        KISAK_NULLSUB();
        R_SortDrawSurfs(scene.drawSurfs[5], drawSurfCount);
        drawSurfCount = drawSurfs[2] - scene.drawSurfs[11];
        scene.drawSurfCount[11] = drawSurfCount;
        KISAK_NULLSUB();
        R_SortDrawSurfs(scene.drawSurfs[11], drawSurfCount);
    }
}

void __cdecl R_AddAllSceneEntSurfacesSunShadow()
{
    uint32_t partitionIndex; // [esp+0h] [ebp-4h]

    for (partitionIndex = 0; partitionIndex < 2; ++partitionIndex)
        R_AddAllSceneEntSurfacesRangeSunShadow(partitionIndex);
}

void __cdecl R_AddAllSceneEntSurfacesRangeSunShadow(uint32_t partitionIndex)
{
    GfxSceneDynBrush *sceneDynBrush; // [esp+30h] [ebp-40h]
    GfxDrawSurf *drawSurf; // [esp+38h] [ebp-38h]
    MaterialTechniqueType shadowmapBuildTechType; // [esp+3Ch] [ebp-34h]
    uint32_t stage; // [esp+44h] [ebp-2Ch]
    const DynEntityDef *dynEntDef; // [esp+48h] [ebp-28h]
    uint8_t *sceneEntVisData; // [esp+5Ch] [ebp-14h]
    GfxSceneDynModel *sceneDynModel; // [esp+60h] [ebp-10h]
    GfxBrushModel *bmodel; // [esp+64h] [ebp-Ch]
    signed int drawSurfCount; // [esp+68h] [ebp-8h]
    GfxDrawSurf *lastDrawSurf; // [esp+6Ch] [ebp-4h]

    PROF_SCOPED("SceneEntSurfacesShadow");

    stage = 3 * partitionIndex + 17;
    drawSurf = scene.drawSurfs[stage];
    lastDrawSurf = &drawSurf[scene.maxDrawSurfCount[stage]];
    shadowmapBuildTechType = gfxMetrics.shadowmapBuildTechType;

    for (int sceneEntIndex = 0; sceneEntIndex < scene.sceneDObjCount; ++sceneEntIndex)
    {
        if (scene.sceneDObjVisData[partitionIndex + 1][sceneEntIndex] == 1)
            drawSurf = R_AddDObjSurfaces(&scene.sceneDObj[sceneEntIndex], shadowmapBuildTechType, drawSurf, lastDrawSurf);
    }

    for (int sceneEntIndex = 0; sceneEntIndex < scene.sceneModelCount; ++sceneEntIndex)
    {
        if (scene.sceneModelVisData[partitionIndex + 1][sceneEntIndex] == 1)
            drawSurf = R_AddXModelSurfaces(
                &scene.sceneModel[sceneEntIndex].info,
                scene.sceneModel[sceneEntIndex].model,
                shadowmapBuildTechType,
                drawSurf,
                lastDrawSurf);
    }

    sceneEntVisData = rgp.world->dpvsDyn.dynEntVisData[0][partitionIndex + 1];
    for (int sceneEntIndex = 0; sceneEntIndex < scene.sceneDynModelCount; ++sceneEntIndex)
    {
        sceneDynModel = &rgp.world->sceneDynModel[sceneEntIndex];
        if (sceneEntVisData[sceneDynModel->dynEntId] == 1)
        {
            dynEntDef = DynEnt_GetEntityDef(sceneDynModel->dynEntId, DYNENT_DRAW_MODEL);
            drawSurf = R_AddXModelSurfaces(
                &sceneDynModel->info,
                dynEntDef->xModel,
                shadowmapBuildTechType,
                drawSurf,
                lastDrawSurf);
        }
    }

    for (int sceneEntIndex = 0; sceneEntIndex < scene.sceneBrushCount; ++sceneEntIndex)
    {
        if (scene.sceneBrushVisData[partitionIndex + 1][sceneEntIndex] == 1)
            drawSurf = R_AddBModelSurfaces(
                &scene.sceneBrush[sceneEntIndex].info,
                scene.sceneBrush[sceneEntIndex].bmodel,
                shadowmapBuildTechType,
                drawSurf,
                lastDrawSurf);
    }

    sceneEntVisData = rgp.world->dpvsDyn.dynEntVisData[1][partitionIndex + 1];
    for (int sceneEntIndex = 0; sceneEntIndex < scene.sceneDynBrushCount; ++sceneEntIndex)
    {
        sceneDynBrush = &rgp.world->sceneDynBrush[sceneEntIndex];
        if (sceneEntVisData[sceneDynBrush->dynEntId] == 1)
        {
            dynEntDef = DynEnt_GetEntityDef(sceneDynBrush->dynEntId, DYNENT_DRAW_BRUSH);
            bmodel = R_GetBrushModel(dynEntDef->brushModel);
            drawSurf = R_AddBModelSurfaces((BModelDrawInfo *)sceneDynBrush, bmodel, shadowmapBuildTechType, drawSurf, lastDrawSurf); // KISAK: value assignment here is a later bugfix
        }
    }

    drawSurfCount = drawSurf - scene.drawSurfs[stage];
    scene.drawSurfCount[stage] = drawSurfCount;
    KISAK_NULLSUB();
    R_SortDrawSurfs(scene.drawSurfs[stage], drawSurfCount);
}

void __cdecl R_AddAllSceneEntSurfacesSpotShadow(
    const GfxViewInfo *viewInfo,
    uint32_t spotShadowIndex,
    uint32_t primaryLightIndex)
{
    GfxSceneDynBrush *sceneDynBrush; // [esp+0h] [ebp-44h]
    GfxDrawSurf *drawSurf; // [esp+8h] [ebp-3Ch]
    MaterialTechniqueType shadowmapBuildTechType; // [esp+Ch] [ebp-38h]
    volatile uint32_t sceneEntCount; // [esp+10h] [ebp-34h]
    volatile uint32_t sceneEntCounta; // [esp+10h] [ebp-34h]
    uint32_t sceneEntCountb; // [esp+10h] [ebp-34h]
    volatile uint32_t sceneEntCountc; // [esp+10h] [ebp-34h]
    uint32_t sceneEntCountd; // [esp+10h] [ebp-34h]
    uint32_t stage; // [esp+18h] [ebp-2Ch]
    const DynEntityDef *dynEntDef; // [esp+1Ch] [ebp-28h]
    const DynEntityDef *dynEntDefa; // [esp+1Ch] [ebp-28h]
    uint32_t sceneEntIndex; // [esp+20h] [ebp-24h]
    uint32_t sceneEntIndexa; // [esp+20h] [ebp-24h]
    uint32_t sceneEntIndexb; // [esp+20h] [ebp-24h]
    uint32_t sceneEntIndexc; // [esp+20h] [ebp-24h]
    uint32_t sceneEntIndexd; // [esp+20h] [ebp-24h]
    GfxSceneBrush *sceneBrush; // [esp+28h] [ebp-1Ch]
    uint16_t dynEntId; // [esp+2Ch] [ebp-18h]
    uint16_t dynEntIda; // [esp+2Ch] [ebp-18h]
    GfxSceneDynModel *sceneDynModel; // [esp+34h] [ebp-10h]
    GfxBrushModel *bmodel; // [esp+38h] [ebp-Ch]
    signed int drawSurfCount; // [esp+3Ch] [ebp-8h]
    GfxDrawSurf *lastDrawSurf; // [esp+40h] [ebp-4h]

    iassert( R_IsPrimaryLight( primaryLightIndex ) );
    stage = 3 * spotShadowIndex + 23;
    drawSurf = scene.drawSurfs[stage];
    lastDrawSurf = &drawSurf[scene.maxDrawSurfCount[stage]];
    shadowmapBuildTechType = gfxMetrics.shadowmapBuildTechType;
    sceneEntCount = scene.sceneDObjCount;
    for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
    {
        if (scene.sceneDObjVisData[spotShadowIndex + 3][sceneEntIndex] == 1)
            drawSurf = R_AddDObjSurfaces(&scene.sceneDObj[sceneEntIndex], shadowmapBuildTechType, drawSurf, lastDrawSurf);
    }
    sceneEntCounta = scene.sceneModelCount;
    for (sceneEntIndexa = 0; sceneEntIndexa < sceneEntCounta; ++sceneEntIndexa)
    {
        if (scene.sceneModelVisData[spotShadowIndex + 3][sceneEntIndexa] == 1)
            drawSurf = R_AddXModelSurfaces(
                &scene.sceneModel[sceneEntIndexa].info,
                scene.sceneModel[sceneEntIndexa].model,
                shadowmapBuildTechType,
                drawSurf,
                lastDrawSurf);
    }
    sceneEntCountb = scene.sceneDynModelCount;
    for (sceneEntIndexb = 0; sceneEntIndexb < sceneEntCountb; ++sceneEntIndexb)
    {
        sceneDynModel = &rgp.world->sceneDynModel[sceneEntIndexb];
        dynEntId = sceneDynModel->dynEntId;
        if (R_IsDynEntVisibleToPrimaryLight(dynEntId, DYNENT_DRAW_MODEL, primaryLightIndex))
        {
            dynEntDef = DynEnt_GetEntityDef(dynEntId, DYNENT_DRAW_MODEL);
            drawSurf = R_AddXModelSurfaces(
                &sceneDynModel->info,
                dynEntDef->xModel,
                shadowmapBuildTechType,
                drawSurf,
                lastDrawSurf);
        }
    }
    sceneEntCountc = scene.sceneBrushCount;
    for (sceneEntIndexc = 0; sceneEntIndexc < sceneEntCountc; ++sceneEntIndexc)
    {
        sceneBrush = &scene.sceneBrush[sceneEntIndexc];
        if (R_IsEntityVisibleToPrimaryLight(viewInfo->localClientNum, sceneBrush->entnum, primaryLightIndex)
            && !Com_BitCheckAssert(scene.entOverflowedDrawBuf, sceneBrush->entnum, 0xFFFFFFF))
        {
            drawSurf = R_AddBModelSurfaces(
                &sceneBrush->info,
                sceneBrush->bmodel,
                shadowmapBuildTechType,
                drawSurf,
                lastDrawSurf);
        }
    }
    sceneEntCountd = scene.sceneDynBrushCount;
    for (sceneEntIndexd = 0; sceneEntIndexd < sceneEntCountd; ++sceneEntIndexd)
    {
        sceneDynBrush = &rgp.world->sceneDynBrush[sceneEntIndexd];
        dynEntIda = sceneDynBrush->dynEntId;
        if (R_IsDynEntVisibleToPrimaryLight(dynEntIda, DYNENT_DRAW_BRUSH, primaryLightIndex))
        {
            dynEntDefa = DynEnt_GetEntityDef(dynEntIda, DYNENT_DRAW_BRUSH);
            bmodel = R_GetBrushModel(dynEntDefa->brushModel);
            drawSurf = R_AddBModelSurfaces((BModelDrawInfo *)sceneDynBrush, bmodel, shadowmapBuildTechType, drawSurf, lastDrawSurf);// KISAK: value assignment here is a later bugfix
        }
    }
    drawSurfCount = drawSurf - scene.drawSurfs[stage];
    scene.drawSurfCount[stage] = drawSurfCount;
    KISAK_NULLSUB();
    R_SortDrawSurfs(scene.drawSurfs[stage], drawSurfCount);
}

void __cdecl R_AddSceneDObj(uint32_t entnum, uint32_t viewIndex)
{
    iassert( entnum != gfxCfg.entnumNone );
    scene.dpvs.entVisData[viewIndex][entnum] = 1;
}

void __cdecl R_DrawAllSceneEnt(const GfxViewInfo *viewInfo)
{
    uint8_t viewVisData; // [esp+14h] [ebp-50h]
    uint32_t *entVisBits; // [esp+18h] [ebp-4Ch]
    GfxSceneModel *sceneModel; // [esp+1Ch] [ebp-48h]
    volatile uint32_t sceneEntCount; // [esp+20h] [ebp-44h]
    const DpvsView *view; // [esp+24h] [ebp-40h]
    GfxEntity *gfxEnt; // [esp+28h] [ebp-3Ch]
    GfxEntity *gfxEnta; // [esp+28h] [ebp-3Ch]
    uint32_t sceneEntIndex; // [esp+2Ch] [ebp-38h]
    GfxSceneEntity *sceneEnt; // [esp+30h] [ebp-34h]
    GfxSceneBrush *sceneBrush; // [esp+34h] [ebp-30h]
    uint32_t entnum; // [esp+3Ch] [ebp-28h]
    uint8_t *sceneEntVisData[7]; // [esp+40h] [ebp-24h]
    uint32_t viewIndex; // [esp+5Ch] [ebp-8h]
    uint32_t visData; // [esp+60h] [ebp-4h]

    entVisBits = dpvsGlob.entVisBits[scene.dpvs.localClientNum];
    for (viewIndex = 0; viewIndex < 7; ++viewIndex)
        sceneEntVisData[viewIndex] = scene.sceneDObjVisData[viewIndex];
    sceneEntCount = scene.sceneDObjCount;
    iassert( scene.dpvs.localClientNum == (uint)viewInfo->localClientNum );
    view = dpvsGlob.views[scene.dpvs.localClientNum];
    for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
    {
        sceneEnt = &scene.sceneDObj[sceneEntIndex];
        entnum = sceneEnt->entnum;
        if (sceneEnt->gfxEntIndex)
        {
            gfxEnt = &frontEndDataOut->gfxEnts[scene.sceneDObj[sceneEntIndex].gfxEntIndex];
            if (entnum == gfxCfg.entnumNone)
            {
                visData = 0;
                for (viewIndex = 0; viewIndex < 3; ++viewIndex)
                {
                    sceneEntVisData[viewIndex][sceneEntIndex] = (view[viewIndex].renderFxFlagsCull & gfxEnt->renderFxFlags) == 0;
                    visData |= sceneEntVisData[viewIndex][sceneEntIndex];
                }
                while (viewIndex < 7)
                {
                    sceneEntVisData[viewIndex][sceneEntIndex] = scene.dpvs.entVisData[viewIndex][entnum];
                    visData |= sceneEntVisData[viewIndex++][sceneEntIndex];
                }
            }
            else
            {
                if ((entVisBits[entnum >> 5] & (0x80000000 >> (entnum & 0x1F))) != 0)
                {
                    visData = 0;
                    for (viewIndex = 0; viewIndex < 3; ++viewIndex)
                    {
                        if ((view[viewIndex].renderFxFlagsCull & gfxEnt->renderFxFlags) != 0)
                            sceneEntVisData[viewIndex][sceneEntIndex] = 0;
                        else
                            sceneEntVisData[viewIndex][sceneEntIndex] = scene.dpvs.entVisData[viewIndex][entnum];
                        visData |= sceneEntVisData[viewIndex][sceneEntIndex];
                    }
                    while (viewIndex < 7)
                    {
                        sceneEntVisData[viewIndex][sceneEntIndex] = scene.dpvs.entVisData[viewIndex][entnum];
                        visData |= sceneEntVisData[viewIndex++][sceneEntIndex];
                    }
                    if ((visData & 1) != 0 && sceneEnt->cull.state < 2)
                        MyAssertHandler(
                            ".\\r_dpvs.cpp",
                            1393,
                            0,
                            "sceneEnt->cull.state >= CULL_STATE_BOUNDED\n\t%i, %i",
                            sceneEnt->cull.state,
                            2);
                    continue;
                }
                visData = 0;
                for (viewIndex = 0; viewIndex < 3; ++viewIndex)
                {
                    viewVisData = scene.dpvs.entVisData[viewIndex][entnum];
                    if (!viewVisData)
                        viewVisData = 1;
                    sceneEntVisData[viewIndex][sceneEntIndex] = (view[viewIndex].renderFxFlagsCull & gfxEnt->renderFxFlags) == 0
                        ? viewVisData
                        : 0;
                    visData |= sceneEntVisData[viewIndex][sceneEntIndex];
                }
                while (viewIndex < 7)
                {
                    sceneEntVisData[viewIndex][sceneEntIndex] = scene.dpvs.entVisData[viewIndex][entnum];
                    visData |= sceneEntVisData[viewIndex++][sceneEntIndex];
                }
            }
            goto LABEL_62;
        }
        if (entnum == gfxCfg.entnumNone)
        {
            visData = 0;
            for (viewIndex = 0; viewIndex < 3; ++viewIndex)
            {
                sceneEntVisData[viewIndex][sceneEntIndex] = 1;
                visData |= sceneEntVisData[viewIndex][sceneEntIndex];
            }
            while (viewIndex < 7)
            {
                sceneEntVisData[viewIndex][sceneEntIndex] = scene.dpvs.entVisData[viewIndex][entnum];
                visData |= sceneEntVisData[viewIndex++][sceneEntIndex];
            }
        LABEL_62:
            if ((visData & 1) != 0)
            {
                if (R_SkinAndBoundSceneEnt(sceneEnt))
                {
                    if (sceneEnt->cull.state < 2)
                        MyAssertHandler(
                            ".\\r_dpvs.cpp",
                            1505,
                            0,
                            "sceneEnt->cull.state >= CULL_STATE_BOUNDED\n\t%i, %i",
                            sceneEnt->cull.state,
                            2);
                }
                else
                {
                    for (viewIndex = 0; viewIndex < 7; ++viewIndex)
                        sceneEntVisData[viewIndex][sceneEntIndex] = 0;
                }
            }
            continue;
        }
        if ((entVisBits[entnum >> 5] & (0x80000000 >> (entnum & 0x1F))) == 0)
        {
            visData = 0;
            for (viewIndex = 0; viewIndex < 3; ++viewIndex)
            {
                viewVisData = scene.dpvs.entVisData[viewIndex][entnum];
                if (!viewVisData)
                    viewVisData = 1;
                sceneEntVisData[viewIndex][sceneEntIndex] = viewVisData;
                visData |= sceneEntVisData[viewIndex][sceneEntIndex];
            }
            while (viewIndex < 7)
            {
                sceneEntVisData[viewIndex][sceneEntIndex] = scene.dpvs.entVisData[viewIndex][entnum];
                visData |= sceneEntVisData[viewIndex++][sceneEntIndex];
            }
            goto LABEL_62;
        }
        visData = 0;
        for (viewIndex = 0; viewIndex < 3; ++viewIndex)
        {
            sceneEntVisData[viewIndex][sceneEntIndex] = scene.dpvs.entVisData[viewIndex][entnum];
            visData |= sceneEntVisData[viewIndex][sceneEntIndex];
        }
        while (viewIndex < 7)
        {
            sceneEntVisData[viewIndex][sceneEntIndex] = scene.dpvs.entVisData[viewIndex][entnum];
            visData |= sceneEntVisData[viewIndex++][sceneEntIndex];
        }
        if ((visData & 1) != 0 && sceneEnt->cull.state < 2)
            MyAssertHandler(
                ".\\r_dpvs.cpp",
                1460,
                0,
                "sceneEnt->cull.state >= CULL_STATE_BOUNDED\n\t%i, %i",
                sceneEnt->cull.state,
                2);
    }
    for (viewIndex = 0; viewIndex < 7; ++viewIndex)
        sceneEntVisData[viewIndex] = scene.sceneModelVisData[viewIndex];

    for (sceneEntIndex = 0; sceneEntIndex < scene.sceneModelCount; ++sceneEntIndex)
    {
        entnum = scene.sceneModel[sceneEntIndex].entnum;
        visData = 0;
        if (scene.sceneModel[sceneEntIndex].gfxEntIndex)
        {
            gfxEnta = &frontEndDataOut->gfxEnts[scene.sceneModel[sceneEntIndex].gfxEntIndex];
            if (entnum == gfxCfg.entnumNone)
            {
                for (viewIndex = 0; viewIndex < 3; ++viewIndex)
                {
                    if ((view[viewIndex].renderFxFlagsCull & gfxEnta->renderFxFlags) != 0)
                        sceneEntVisData[viewIndex][sceneEntIndex] = 0;
                    else
                        visData |= sceneEntVisData[viewIndex][sceneEntIndex];
                }
            }
            else if ((entVisBits[entnum >> 5] & (0x80000000 >> (entnum & 0x1F))) != 0)
            {
                for (viewIndex = 0; viewIndex < 3; ++viewIndex)
                {
                    if ((view[viewIndex].renderFxFlagsCull & gfxEnta->renderFxFlags) != 0)
                        sceneEntVisData[viewIndex][sceneEntIndex] = 0;
                    else
                        sceneEntVisData[viewIndex][sceneEntIndex] = scene.dpvs.entVisData[viewIndex][entnum];
                    visData |= sceneEntVisData[viewIndex][sceneEntIndex];
                }
            }
            else
            {
                for (viewIndex = 0; viewIndex < 3; ++viewIndex)
                {
                    viewVisData = scene.dpvs.entVisData[viewIndex][entnum];
                    if (!viewVisData)
                        viewVisData = 1;
                    sceneEntVisData[viewIndex][sceneEntIndex] = (view[viewIndex].renderFxFlagsCull & gfxEnta->renderFxFlags) == 0
                        ? viewVisData
                        : 0;
                    visData |= sceneEntVisData[viewIndex][sceneEntIndex];
                }
            }
            while (viewIndex < 7)
            {
                sceneEntVisData[viewIndex][sceneEntIndex] = scene.dpvs.entVisData[viewIndex][entnum];
                visData |= sceneEntVisData[viewIndex++][sceneEntIndex];
            }
        }
        else
        {
            if (entnum == gfxCfg.entnumNone)
            {
                for (viewIndex = 0; viewIndex < 3; ++viewIndex)
                    visData |= sceneEntVisData[viewIndex][sceneEntIndex];
            }
            else if ((entVisBits[entnum >> 5] & (0x80000000 >> (entnum & 0x1F))) != 0)
            {
                for (viewIndex = 0; viewIndex < 3; ++viewIndex)
                {
                    sceneEntVisData[viewIndex][sceneEntIndex] = scene.dpvs.entVisData[viewIndex][entnum];
                    visData |= sceneEntVisData[viewIndex][sceneEntIndex];
                }
            }
            else
            {
                for (viewIndex = 0; viewIndex < 3; ++viewIndex)
                {
                    viewVisData = scene.dpvs.entVisData[viewIndex][entnum];
                    if (!viewVisData)
                        viewVisData = 1;
                    sceneEntVisData[viewIndex][sceneEntIndex] = viewVisData;
                    visData |= sceneEntVisData[viewIndex][sceneEntIndex];
                }
            }
            while (viewIndex < 7)
            {
                sceneEntVisData[viewIndex][sceneEntIndex] = scene.dpvs.entVisData[viewIndex][entnum];
                visData |= sceneEntVisData[viewIndex++][sceneEntIndex];
            }
        }
        if ((visData & 1) != 0)
        {
            sceneModel = &scene.sceneModel[sceneEntIndex];
            if (!R_SkinXModel(
                &sceneModel->info,
                sceneModel->model,
                sceneModel->obj,
                &sceneModel->placement.base,
                sceneModel->placement.scale,
                sceneModel->gfxEntIndex))
            {
                for (viewIndex = 0; viewIndex < 7; ++viewIndex)
                    sceneEntVisData[viewIndex][sceneEntIndex] = 0;
            }
        }
    }

    for (viewIndex = 0; viewIndex < 3; ++viewIndex)
        sceneEntVisData[viewIndex] = scene.sceneBrushVisData[viewIndex];

    for (sceneEntIndex = 0; sceneEntIndex < scene.sceneBrushCount; ++sceneEntIndex)
    {
        sceneBrush = &scene.sceneBrush[sceneEntIndex];
        entnum = sceneBrush->entnum;
        iassert( entnum != gfxCfg.entnumNone );
        visData = 0;
        if ((entVisBits[entnum >> 5] & (0x80000000 >> (entnum & 0x1F))) != 0)
        {
            for (viewIndex = 0; viewIndex < 3; ++viewIndex)
            {
                sceneEntVisData[viewIndex][sceneEntIndex] = scene.dpvs.entVisData[viewIndex][entnum];
                visData |= sceneEntVisData[viewIndex][sceneEntIndex];
            }
        }
        else
        {
            for (viewIndex = 0; viewIndex < 3; ++viewIndex)
            {
                viewVisData = scene.dpvs.entVisData[viewIndex][entnum];
                if (!viewVisData)
                    viewVisData = 1;
                sceneEntVisData[viewIndex][sceneEntIndex] = viewVisData;
                visData |= sceneEntVisData[viewIndex][sceneEntIndex];
            }
        }
        if (((visData & 1) != 0 || R_IsEntityVisibleToAnyShadowedPrimaryLight(viewInfo, entnum))
            && !R_DrawBModel(&sceneBrush->info, sceneBrush->bmodel, &sceneBrush->placement))
        {
            Com_BitSetAssert(scene.entOverflowedDrawBuf, sceneBrush->entnum, 0xFFFFFFF);
            for (viewIndex = 0; viewIndex < 3; ++viewIndex)
                sceneEntVisData[viewIndex][sceneEntIndex] = 0;
        }
    }
}

int __cdecl R_DrawBModel(BModelDrawInfo *bmodelInfo, const GfxBrushModel *bmodel, const GfxPlacement *placement)
{
    uint16_t visibleSurfaceCount; // [esp+Ah] [ebp-26h]
    uint32_t surfId; // [esp+10h] [ebp-20h]
    int startSurfPos; // [esp+14h] [ebp-1Ch]
    GfxScaledPlacement *newPlacement; // [esp+18h] [ebp-18h]
    uint32_t surfIndex; // [esp+1Ch] [ebp-14h]
    uint32_t surfIndexa; // [esp+1Ch] [ebp-14h]
    const GfxSurface *surf; // [esp+24h] [ebp-Ch]
    const GfxSurface *surfa; // [esp+24h] [ebp-Ch]
    BModelSurface *bmodelSurf; // [esp+28h] [ebp-8h]

    if (r_drawDecals->current.enabled)
        visibleSurfaceCount = bmodel->surfaceCount;
    else
        visibleSurfaceCount = bmodel->surfaceCountNoDecal;
    iassert( visibleSurfaceCount );
    startSurfPos = InterlockedExchangeAdd(&frontEndDataOut->surfPos, 8 * visibleSurfaceCount + 32);
    if (8 * (uint32_t)visibleSurfaceCount + 32 + startSurfPos <= 0x20000)
    {
        iassert( !(startSurfPos & 3) );
        newPlacement = (GfxScaledPlacement *)&frontEndDataOut->surfsBuffer[startSurfPos];
        memcpy(&frontEndDataOut->surfsBuffer[startSurfPos], placement, 0x1Cu);
        newPlacement->scale = 1.0;
        bmodelSurf = (BModelSurface *)&newPlacement[1];
        surfId = (char *)&newPlacement[1] - (char *)frontEndDataOut;
        iassert( !(surfId & 3) );
        bmodelInfo->surfId = surfId >> 2;
        if (r_drawDecals->current.enabled)
        {
            surfIndexa = 0;
            surfa = &rgp.world->dpvs.surfaces[bmodel->startSurfIndex];
            while (surfIndexa < bmodel->surfaceCount)
            {
                bmodelSurf->placement = newPlacement;
                bmodelSurf->surf = surfa;
                ++bmodelSurf;
                ++surfIndexa;
                ++surfa;
            }
        }
        else
        {
            surfIndex = 0;
            surf = &rgp.world->dpvs.surfaces[bmodel->startSurfIndex];
            while (surfIndex < bmodel->surfaceCount)
            {
                if ((surf->flags & 2) == 0)
                {
                    bmodelSurf->placement = newPlacement;
                    bmodelSurf->surf = surf;
                    ++bmodelSurf;
                }
                ++surfIndex;
                ++surf;
            }
        }
        return 1;
    }
    else
    {
        R_WarnOncePerFrame(R_WARN_MAX_SCENE_SURFS_SIZE);
        return 0;
    }
}

void __cdecl R_DrawAllDynEnt(const GfxViewInfo *viewInfo)
{
    DynEntityPose *dynEntPose; // [esp+38h] [ebp-38h]
    GfxSceneDynBrush *sceneDynBrush; // [esp+3Ch] [ebp-34h]
    uint32_t dynEntIndex; // [esp+40h] [ebp-30h]
    uint32_t dynEntCount; // [esp+44h] [ebp-2Ch]
    const DynEntityDef *dynEntDef; // [esp+48h] [ebp-28h]
    uint8_t *dynEntVisData[3]; // [esp+54h] [ebp-1Ch]
    GfxSceneDynModel *sceneDynModel; // [esp+60h] [ebp-10h]
    uint32_t viewIndex; // [esp+64h] [ebp-Ch]
    uint32_t visData; // [esp+68h] [ebp-8h]
    GfxBrushModel *bmodel; // [esp+6Ch] [ebp-4h]

    PROF_SCOPED("DrawDynEnt");

    for (viewIndex = 0; viewIndex < 3; ++viewIndex)
        dynEntVisData[viewIndex] = rgp.world->dpvsDyn.dynEntVisData[0][viewIndex];
    dynEntCount = rgp.world->dpvsDyn.dynEntClientCount[0];
    for (dynEntIndex = 0; dynEntIndex < dynEntCount; ++dynEntIndex)
    {
        visData = dynEntVisData[2][dynEntIndex] | dynEntVisData[1][dynEntIndex] | dynEntVisData[0][dynEntIndex];
        if ((visData & 1) != 0 || R_IsDynEntVisibleToAnyShadowedPrimaryLight(viewInfo, dynEntIndex, DYNENT_DRAW_MODEL))
        {
            dynEntPose = DynEnt_GetClientPose(dynEntIndex, DYNENT_DRAW_MODEL);
            dynEntDef = DynEnt_GetEntityDef(dynEntIndex, DYNENT_DRAW_MODEL);
            iassert( dynEntDef->xModel );
            sceneDynModel = &rgp.world->sceneDynModel[scene.sceneDynModelCount];
            if (R_SkinXModel(&sceneDynModel->info, dynEntDef->xModel, 0, &dynEntPose->pose, 1.0, 0))
            {
                sceneDynModel->dynEntId = dynEntIndex;
                ++scene.sceneDynModelCount;
            }
            else
            {
                dynEntVisData[0][dynEntIndex] = 0;
                dynEntVisData[1][dynEntIndex] = 0;
                dynEntVisData[2][dynEntIndex] = 0;
            }
        }
    }
    if (!rg.drawXModels)
        scene.sceneDynModelCount = 0;
    for (viewIndex = 0; viewIndex < 3; ++viewIndex)
        dynEntVisData[viewIndex] = rgp.world->dpvsDyn.dynEntVisData[1][viewIndex];
    dynEntCount = rgp.world->dpvsDyn.dynEntClientCount[1];
    for (dynEntIndex = 0; dynEntIndex < dynEntCount; ++dynEntIndex)
    {
        visData = dynEntVisData[2][dynEntIndex] | dynEntVisData[1][dynEntIndex] | dynEntVisData[0][dynEntIndex];
        if ((visData & 1) != 0)
        {
            dynEntPose = DynEnt_GetClientPose(dynEntIndex, DYNENT_DRAW_BRUSH);
            dynEntDef = DynEnt_GetEntityDef(dynEntIndex, DYNENT_DRAW_BRUSH);
            iassert( !dynEntDef->xModel );
            iassert( dynEntDef->brushModel );
            bmodel = R_GetBrushModel(dynEntDef->brushModel);
            if (bmodel->surfaceCount)
            {
                sceneDynBrush = &rgp.world->sceneDynBrush[scene.sceneDynBrushCount];
                if (R_DrawBModel((BModelDrawInfo *)sceneDynBrush, bmodel, &dynEntPose->pose))
                {
                    sceneDynBrush->dynEntId = dynEntIndex;
                    ++scene.sceneDynBrushCount;
                }
                else
                {
                    dynEntVisData[0][dynEntIndex] = 0;
                    dynEntVisData[1][dynEntIndex] = 0;
                    dynEntVisData[2][dynEntIndex] = 0;
                }
            }
        }
    }
    if (!rg.drawBModels)
        scene.sceneDynBrushCount = 0;
}

void __cdecl R_UnfilterEntFromCells(uint32_t localClientNum, uint32_t entnum)
{
    uint32_t cellIndex; // [esp+0h] [ebp-18h]
    uint32_t invBit; // [esp+4h] [ebp-14h]
    uint32_t offset; // [esp+8h] [ebp-10h]
    uint32_t *entCellBits; // [esp+Ch] [ebp-Ch]
    uint32_t cellCount; // [esp+10h] [ebp-8h]
    uint32_t cellCounta; // [esp+10h] [ebp-8h]
    uint32_t wordIndex; // [esp+14h] [ebp-4h]

    iassert( Sys_IsMainThread() );
    iassert( rgp.world );
    iassert( entnum != gfxCfg.entnumNone );
    iassert( gfxCfg.maxClientViews * gfxCfg.entCount <= MAX_TOTAL_ENT_COUNT );
    cellCount = rgp.world->dpvsPlanes.cellCount;
    if ((gfxCfg.entCount & 0x1F) != 0)
        MyAssertHandler(
            ".\\r_dpvs.cpp",
            1778,
            0,
            "%s\n\t(gfxCfg.entCount) = %i",
            "(!(gfxCfg.entCount & 31))",
            gfxCfg.entCount);
    offset = localClientNum * (gfxCfg.entCount >> 5);
    if (offset >= 0x80)
        MyAssertHandler(
            ".\\r_dpvs.cpp",
            1781,
            0,
            "offset doesn't index MAX_TOTAL_ENT_COUNT >> 5\n\t%i not in [0, %i)",
            offset,
            128);
    entCellBits = &rgp.world->dpvsPlanes.sceneEntCellBits[offset];
    wordIndex = entnum >> 5;
    invBit = ~(0x80000000 >> (entnum & 0x1F));
    dpvsGlob.entVisBits[localClientNum][entnum >> 5] &= invBit;
    cellCounta = 2 * cellCount;
    for (cellIndex = 0; cellIndex < cellCounta; ++cellIndex)
    {
        entCellBits[wordIndex] &= invBit;
        wordIndex += 128;
    }
}

void __cdecl R_UnfilterDynEntFromCells(uint32_t dynEntId, DynEntityDrawType drawType)
{
    iassert(Sys_IsMainThread());
    if (!R_UnlinkDynEntityFromCells(*rgp.world, drawType, dynEntId))
        Com_Error(ERR_DROP, "R_UnfilterDynEntFromCells: invalid canonical cell storage");
}

void __cdecl R_FilterXModelIntoScene(
    const XModel *model,
    const GfxScaledPlacement *placement,
    uint16_t renderFxFlags,
    uint16_t *cachedLightingHandle)
{
    const char *v4; // eax
    int v5; // [esp+38h] [ebp-3Ch]
    int frustumPlaneCount; // [esp+3Ch] [ebp-38h]
    const float *a; // [esp+40h] [ebp-34h]
    int v8; // [esp+44h] [ebp-30h]
    GfxSceneModel *sceneModel; // [esp+4Ch] [ebp-28h]
    float radius; // [esp+54h] [ebp-20h]
    const DpvsView *view; // [esp+58h] [ebp-1Ch]
    GfxEntity *gfxEnt; // [esp+5Ch] [ebp-18h]
    uint32_t sceneEntIndex; // [esp+60h] [ebp-14h]
    //uint32_t gfxEntIndex; // [esp+64h] [ebp-10h]
    ushort gfxEntIndex; // [esp+64h] [ebp-10h]
    uint32_t cullCount; // [esp+68h] [ebp-Ch]
    uint8_t sceneEntVisData[4]; // [esp+6Ch] [ebp-8h]
    uint32_t viewIndex; // [esp+70h] [ebp-4h]

    iassert( model );
    iassert( placement->scale > 0 );
    if (!Vec4IsNormalized(placement->base.quat))
    {
        v4 = va(
            "%g %g %g %g",
            placement->base.quat[0],
            placement->base.quat[1],
            placement->base.quat[2],
            placement->base.quat[3]);
        MyAssertHandler(".\\r_dpvs.cpp", 2061, 0, "%s\n\t%s", "Vec4IsNormalized( placement->base.quat )", v4);
    }
    radius = XModelGetRadius(model) * placement->scale;
    cullCount = 0;
    view = dpvsGlob.views[scene.dpvs.localClientNum];
    for (viewIndex = 0; viewIndex < 3; ++viewIndex)
    {
        frustumPlaneCount = view[viewIndex].frustumPlaneCount;
        v8 = 0;
        a = view[viewIndex].frustumPlanes[0].coeffs;
        while (v8 < frustumPlaneCount)
        {
            if (Vec3Dot(a, placement->base.origin) + a[3] + radius <= 0.0)
            {
                v5 = 1;
                goto LABEL_16;
            }
            ++v8;
            a += 5;
        }
        v5 = 0;
    LABEL_16:
        if (v5)
        {
            ++cullCount;
            sceneEntVisData[viewIndex] = 2;
        }
        else
        {
            sceneEntVisData[viewIndex] = 1;
        }
    }
    if (cullCount != 3 && r_drawXModels->current.enabled)
    {
        if (renderFxFlags)
        {
            gfxEntIndex = InterlockedExchangeAdd(&frontEndDataOut->gfxEntCount, 1);
            if (gfxEntIndex >= 0x80)
            {
                frontEndDataOut->gfxEntCount = 128;
                R_WarnOncePerFrame(R_WARN_KNOWN_SPECIAL_MODELS, 128);
                return;
            }
            gfxEnt = &frontEndDataOut->gfxEnts[gfxEntIndex];
            frontEndDataOut->gfxEnts[gfxEntIndex].materialTime = 0.0;
            gfxEnt->renderFxFlags = renderFxFlags;
        }
        else
        {
            gfxEntIndex = 0;
        }
        sceneEntIndex = R_AllocSceneModel();
        if (sceneEntIndex < 0x400)
        {
            sceneModel = &scene.sceneModel[sceneEntIndex];
            sceneModel->model = model;
            iassert( !sceneModel->obj );
            sceneModel->gfxEntIndex = gfxEntIndex;
            memcpy(&sceneModel->placement, placement, sizeof(sceneModel->placement));
            sceneModel->entnum = gfxCfg.entnumNone;
            sceneModel->cachedLightingHandle = cachedLightingHandle;
            sceneModel->radius = radius;
            sceneModel->lightingOrigin[0] = sceneModel->placement.base.origin[0];
            sceneModel->lightingOrigin[1] = sceneModel->placement.base.origin[1];
            sceneModel->lightingOrigin[2] = sceneModel->placement.base.origin[2];
            sceneModel->lightingOrigin[2] = sceneModel->lightingOrigin[2] + 4.0;
            for (viewIndex = 0; viewIndex < 3; ++viewIndex)
                scene.sceneModelVisData[viewIndex][sceneEntIndex] = sceneEntVisData[viewIndex];
        }
    }
}

void __cdecl R_FilterDObjIntoCells(uint32_t localClientNum, uint32_t entnum, float *origin, float radius)
{
    float s; // [esp+0h] [ebp-30h]
    float mins[3]; // [esp+8h] [ebp-28h] BYREF
    FilterEntInfo entInfo; // [esp+14h] [ebp-1Ch] BYREF
    float maxs[3]; // [esp+24h] [ebp-Ch] BYREF

    iassert( entnum != gfxCfg.entnumNone );
    if (localClientNum >= gfxCfg.maxClientViews)
        MyAssertHandler(
            ".\\r_dpvs.cpp",
            2137,
            0,
            "localClientNum doesn't index gfxCfg.maxClientViews\n\t%i not in [0, %i)",
            localClientNum,
            gfxCfg.maxClientViews);
    R_UnfilterEntFromCells(localClientNum, entnum);
    s = -radius;
    Vec3AddScalar(origin, s, mins);
    Vec3AddScalar(origin, radius, maxs);
    entInfo.localClientNum = localClientNum;
    entInfo.entnum = entnum;
    entInfo.info.radius = radius;
    entInfo.cellOffset = 0;
    R_FilterEntIntoCells_r(&entInfo, (mnode_t *)rgp.world->dpvsPlanes.nodes, mins, maxs);
}

void __cdecl R_FilterEntIntoCells_r(FilterEntInfo *entInfo, mnode_t *node, const float *mins, const float *maxs)
{
    float localmaxs[3]; // [esp+0h] [ebp-50h]
    float dist; // [esp+Ch] [ebp-44h]
    float localmins[3]; // [esp+10h] [ebp-40h] BYREF
    uint32_t type; // [esp+1Ch] [ebp-34h]
    int side; // [esp+20h] [ebp-30h]
    cplane_s *plane; // [esp+24h] [ebp-2Ch]
    int cellIndex; // [esp+28h] [ebp-28h]
    float mins2[3]; // [esp+2Ch] [ebp-24h] BYREF
    int cellCount; // [esp+38h] [ebp-18h]
    float maxs2[3]; // [esp+3Ch] [ebp-14h] BYREF
    mnode_t *rightNode; // [esp+48h] [ebp-8h]
    int planeIndex; // [esp+4Ch] [ebp-4h]

    cellCount = rgp.world->dpvsPlanes.cellCount + 1;
    mins2[0] = *mins;
    mins2[1] = mins[1];
    mins2[2] = mins[2];
    maxs2[0] = *maxs;
    maxs2[1] = maxs[1];
    maxs2[2] = maxs[2];
    while (1)
    {
        cellIndex = node->cellIndex;
        planeIndex = cellIndex - cellCount;
        if (cellIndex - cellCount < 0)
            break;
        plane = &rgp.world->dpvsPlanes.planes[planeIndex];
        side = BoxOnPlaneSide(mins2, maxs2, plane);
        if (side == 3)
        {
            type = plane->type;
            rightNode = (mnode_t *)((char *)node + 2 * node->rightChildOffset);
            if (type >= 3)
            {
                R_FilterEntIntoCells_r(entInfo, node + 1, mins2, maxs2);
            }
            else
            {
                dist = plane->dist;
                localmins[0] = mins2[0];
                localmins[1] = mins2[1];
                localmins[2] = mins2[2];
                localmins[type] = dist;
                localmaxs[0] = maxs2[0];
                localmaxs[1] = maxs2[1];
                localmaxs[2] = maxs2[2];
                localmaxs[type] = dist;
                iassert(BoxOnPlaneSide(localmins, maxs2, plane) == BOXSIDE_FRONT);
                if (maxs2[type] > (double)dist)
                    R_FilterEntIntoCells_r(entInfo, node + 1, localmins, maxs2);
                maxs2[0] = localmaxs[0];
                maxs2[1] = localmaxs[1];
                maxs2[2] = localmaxs[2];
            }
            node = rightNode;
        }
        else
        {
            iassert( (side == BOXSIDE_FRONT) || (side == BOXSIDE_BACK) );

            if (!side) // blops add
            {
                side = BOXSIDE_FRONT;
            }

            node = (mnode_t *)((char *)node + ((side - 1) * (node->rightChildOffset - 2)) * 2 + 4);
        }
    }
    if (cellIndex)
        R_AddEntToCell(entInfo, cellIndex - 1);
}

void __cdecl R_AddEntToCell(FilterEntInfo *entInfo, uint32_t cellIndex)
{
    uint32_t bit; // [esp+0h] [ebp-1Ch]
    uint32_t localClientNum; // [esp+4h] [ebp-18h]
    uint32_t offset; // [esp+8h] [ebp-14h]
    uint32_t *entCellBits; // [esp+Ch] [ebp-10h]
    uint32_t entnum; // [esp+14h] [ebp-8h]

    iassert( Sys_IsMainThread() );
    localClientNum = entInfo->localClientNum;
    if (entInfo->localClientNum >= gfxCfg.maxClientViews)
        MyAssertHandler(
            ".\\r_dpvs.cpp",
            1840,
            0,
            "localClientNum doesn't index gfxCfg.maxClientViews\n\t%i not in [0, %i)",
            localClientNum,
            gfxCfg.maxClientViews);
    entnum = entInfo->entnum;
    iassert( gfxCfg.maxClientViews * gfxCfg.entCount <= MAX_TOTAL_ENT_COUNT );
    if ((gfxCfg.entCount & 7) != 0)
        MyAssertHandler(
            ".\\r_dpvs.cpp",
            1846,
            0,
            "%s\n\t(gfxCfg.entCount) = %i",
            "(!(gfxCfg.entCount & 7))",
            gfxCfg.entCount);
    offset = localClientNum * (gfxCfg.entCount >> 5);
    if (offset >= 0x80)
        MyAssertHandler(
            ".\\r_dpvs.cpp",
            1849,
            0,
            "offset doesn't index MAX_TOTAL_ENT_COUNT >> 5\n\t%i not in [0, %i)",
            offset,
            128);
    entCellBits = &rgp.world->dpvsPlanes.sceneEntCellBits[128 * entInfo->cellOffset + 128 * cellIndex + offset];
    bit = 0x80000000 >> (entnum & 0x1F);
    entCellBits[entnum >> 5] |= bit;
    dpvsGlob.entVisBits[localClientNum][entnum >> 5] |= bit;
    scene.dpvs.entInfo[localClientNum][entnum].bmodel = entInfo->info.bmodel;
}

void __cdecl R_FilterBModelIntoCells(uint32_t localClientNum, uint32_t entnum, GfxBrushModel *bmodel)
{
    FilterEntInfo entInfo; // [esp+0h] [ebp-10h] BYREF

    iassert( entnum != gfxCfg.entnumNone );
    if (localClientNum >= gfxCfg.maxClientViews)
        MyAssertHandler(
            ".\\r_dpvs.cpp",
            2158,
            0,
            "localClientNum doesn't index gfxCfg.maxClientViews\n\t%i not in [0, %i)",
            localClientNum,
            gfxCfg.maxClientViews);
    R_UnfilterEntFromCells(localClientNum, entnum);
    entInfo.localClientNum = localClientNum;
    entInfo.entnum = entnum;
    entInfo.info.bmodel = bmodel;
    entInfo.cellOffset = rgp.world->dpvsPlanes.cellCount;
    R_FilterEntIntoCells_r(&entInfo, (mnode_t *)rgp.world->dpvsPlanes.nodes, bmodel->writable.mins, bmodel->writable.maxs);
}

void __cdecl R_FilterDynEntIntoCells(uint32_t dynEntId, DynEntityDrawType drawType, float *mins, float *maxs)
{
    iassert(Sys_IsMainThread());
    if (!R_LinkDynEntityBoundsToCells(*rgp.world, drawType, dynEntId, mins, maxs))
        Com_Error(ERR_DROP, "R_FilterDynEntIntoCells: invalid canonical bounds or cell storage");
}

void __cdecl R_FilterEntitiesIntoCells(int cameraCellIndex)
{
    float s; // [esp+0h] [ebp-84h]
    int v3; // [esp+18h] [ebp-6Ch]
    const DpvsPlane *v4; // [esp+1Ch] [ebp-68h]
    int v5; // [esp+20h] [ebp-64h]
    int v6; // [esp+24h] [ebp-60h]
    int frustumPlaneCount; // [esp+28h] [ebp-5Ch]
    float radius; // [esp+2Ch] [ebp-58h]
    const DpvsPlane *a; // [esp+30h] [ebp-54h]
    int v10; // [esp+34h] [ebp-50h]
    int v11; // [esp+38h] [ebp-4Ch]
    const DpvsPlane *frustumPlanes; // [esp+40h] [ebp-44h]
    int v13; // [esp+44h] [ebp-40h]
    float mins[3]; // [esp+48h] [ebp-3Ch] BYREF
    float maxs[3]; // [esp+54h] [ebp-30h] BYREF
    GfxSceneModel *sceneModel; // [esp+60h] [ebp-24h]
    const DpvsView *dpvsView; // [esp+64h] [ebp-20h]
    const DpvsView *view; // [esp+68h] [ebp-1Ch]
    int sceneEntIndex; // [esp+6Ch] [ebp-18h]
    GfxSceneEntity *sceneEnt; // [esp+70h] [ebp-14h]
    GfxSceneBrush *sceneBrush; // [esp+74h] [ebp-10h]
    uint32_t entnum; // [esp+78h] [ebp-Ch]
    uint32_t viewIndex; // [esp+7Ch] [ebp-8h]
    const GfxBrushModel *bmodel; // [esp+80h] [ebp-4h]

    iassert( Sys_IsMainThread() );
    if (cameraCellIndex < 0)
        dpvsGlob.cameraCellIndex = 0;
    else
        dpvsGlob.cameraCellIndex = cameraCellIndex;

    view = dpvsGlob.views[scene.dpvs.localClientNum];

    // DObj 
    for (sceneEntIndex = 0; sceneEntIndex < scene.sceneDObjCount; ++sceneEntIndex)
    {
        sceneEnt = &scene.sceneDObj[sceneEntIndex];
        if (sceneEnt->cull.state != 4)
        {
            entnum = sceneEnt->entnum;
            for (viewIndex = 0; viewIndex < 3; ++viewIndex)
            {
                dpvsView = &view[viewIndex];
                v13 = 0;
                frustumPlanes = dpvsView->frustumPlanes;
                while (v13 < dpvsView->frustumPlaneCount)
                {
                    if (*(float *)((char *)sceneEnt->cull.mins + frustumPlanes->side[0]) * frustumPlanes->coeffs[0]
                        + frustumPlanes->coeffs[3]
                        + *(float *)((char *)sceneEnt->cull.mins + frustumPlanes->side[1]) * frustumPlanes->coeffs[1]
                        + *(float *)((char *)sceneEnt->cull.mins + frustumPlanes->side[2]) * frustumPlanes->coeffs[2] <= 0.0)
                    {
                        v11 = 1;
                        goto LABEL_19;
                    }
                    ++v13;
                    ++frustumPlanes;
                }
                v11 = 0;
            LABEL_19:
                if (v11)
                    scene.dpvs.entVisData[viewIndex][entnum] = 2;
            }
            if (r_showCullXModels->current.enabled)
                R_AddDebugBox(&frontEndDataOut->debugGlobals, sceneEnt->cull.mins, sceneEnt->cull.maxs, colorCyan);
        }
    }

    // XModels
    for (sceneEntIndex = 0; sceneEntIndex < scene.sceneModelCount; ++sceneEntIndex)
    {
        sceneModel = &scene.sceneModel[sceneEntIndex];
        entnum = sceneModel->entnum;
        for (viewIndex = 0; viewIndex < 3; ++viewIndex)
        {
            dpvsView = &view[viewIndex];
            frustumPlaneCount = dpvsView->frustumPlaneCount;
            radius = sceneModel->radius;
            v10 = 0;
            a = dpvsView->frustumPlanes;
            while (v10 < frustumPlaneCount)
            {
                if (Vec3Dot(a->coeffs, sceneModel->placement.base.origin) + a->coeffs[3] + radius <= 0.0)
                {
                    v6 = 1;
                    goto LABEL_35;
                }
                ++v10;
                ++a;
            }
            v6 = 0;
        LABEL_35:
            if (v6)
                scene.dpvs.entVisData[viewIndex][entnum] = 2;
        }
        if (r_showCullXModels->current.enabled)
        {
            s = -sceneModel->radius;
            Vec3AddScalar(sceneModel->placement.base.origin, s, mins);
            Vec3AddScalar(sceneModel->placement.base.origin, sceneModel->radius, maxs);
            R_AddDebugBox(&frontEndDataOut->debugGlobals, mins, maxs, colorCyan);
        }
    }

    // Brushes
    for (sceneEntIndex = 0; sceneEntIndex < scene.sceneBrushCount; ++sceneEntIndex)
    {
        sceneBrush = &scene.sceneBrush[sceneEntIndex];
        entnum = sceneBrush->entnum;
        bmodel = sceneBrush->bmodel;
        for (viewIndex = 0; viewIndex < 3; ++viewIndex)
        {
            dpvsView = &view[viewIndex];
            v5 = 0;
            v4 = dpvsView->frustumPlanes;
            while (v5 < dpvsView->frustumPlaneCount)
            {
                if (*(float *)((char *)bmodel->writable.mins + v4->side[0]) * v4->coeffs[0]
                    + v4->coeffs[3]
                    + *(float *)((char *)bmodel->writable.mins + v4->side[1]) * v4->coeffs[1]
                    + *(float *)((char *)bmodel->writable.mins + v4->side[2]) * v4->coeffs[2] <= 0.0)
                {
                    v3 = 1;
                    goto LABEL_51;
                }
                ++v5;
                ++v4;
            }
            v3 = 0;
        LABEL_51:
            if (v3)
                scene.dpvs.entVisData[viewIndex][entnum] = 2;
        }
    }
}

// [ viewIndex ]
// SCENE_VIEW_CAMERA = 0x0,
// SCENE_VIEW_SUNSHADOW_0 = 0x1, (CSM Near)
// SCENE_VIEW_SUNSHADOW_1 = 0x2, (CSM Far)
uint32_t __cdecl R_SetVisData(uint32_t viewIndex)
{
    uint32_t oldViewIndex; // [esp+4h] [ebp-8h]
    uint32_t drawType; // [esp+8h] [ebp-4h]

    oldViewIndex = g_viewIndex;
    //g_viewIndex = oldViewIndex; // (Fuck you whoever did this typo!)
    g_viewIndex = viewIndex;
    for (drawType = 0; drawType < 2; ++drawType)
        g_dynEntVisData[drawType] = rgp.world->dpvsDyn.dynEntVisData[drawType][viewIndex];
    g_dpvsView = &dpvsGlob.views[scene.dpvs.localClientNum][viewIndex];
    return oldViewIndex;
}

void __cdecl R_AddCellDynBrushSurfacesInFrustumCmd(const DpvsDynamicCellCmd *data)
{
    uint32_t oldViewIndex; // [esp+0h] [ebp-8h]

    oldViewIndex = R_SetVisData(data->viewIndex);
    if (r_drawDynEnts->current.enabled)
        R_CullDynBrushInCell(data->cellIndex, data->planes, data->planeCount);
    R_SetVisData(oldViewIndex);
}

void __cdecl R_CullDynBrushInCell(uint32_t cellIndex, const DpvsPlane *planes, int planeCount)
{
    const unsigned count = DynEnt_GetEntityCount(DYNENT_COLL_CLIENT_BRUSH);
    if (count != rgp.world->dpvsDyn.dynEntClientCount[1] ||
        !R_CullDynEntityCell(*rgp.world, 1u, cellIndex, nullptr,
            count ? DynEnt_GetEntityDef(0u, DYNENT_DRAW_BRUSH) : nullptr,
            planes, planeCount, g_dynEntVisData[1]))
        Com_Error(ERR_DROP, "R_CullDynBrushInCell: invalid canonical cell data");
}

void __cdecl R_GenerateShadowMapCasterCells()
{
    GfxLight *sunLight; // edx
    GfxCell *cell; // [esp+4h] [ebp-Ch]
    int cellIndex; // [esp+8h] [ebp-8h]
    uint32_t cellCasterBitsCount; // [esp+Ch] [ebp-4h]

    iassert( rgp.world->sunLight );
    cellCasterBitsCount = (rgp.world->dpvsPlanes.cellCount + 31) >> 5;
    memset((uint8_t *)rgp.world->cellCasterBits, 0, 4 * cellCasterBitsCount * rgp.world->dpvsPlanes.cellCount);
    if (rgp.world->sunPrimaryLightIndex)
    {
        iassert( Vec3LengthSq( rgp.world->sunLight->dir ) );
        sunLight = rgp.world->sunLight;
        dpvsGlob.viewOrg[0] = -sunLight->dir[0];
        dpvsGlob.viewOrg[1] = -sunLight->dir[1];
        dpvsGlob.viewOrg[2] = -sunLight->dir[2];
        dpvsGlob.viewOrg[3] = 0.0;
        dpvsGlob.viewOrgIsDir = 1;
        dpvsGlob.farPlane = 0;
        dpvsGlob.nearPlane = 0;
        for (cellIndex = 0; cellIndex < rgp.world->dpvsPlanes.cellCount; ++cellIndex)
        {
            cell = &rgp.world->cells[cellIndex];
            dpvsGlob.cellBits = &rgp.world->cellCasterBits[cellCasterBitsCount * cellIndex];
            R_VisitPortalsNoFrustum(cell);
        }
    }
}

void __cdecl R_VisitPortalsNoFrustum(const GfxCell *cell)
{
    float scale; // [esp+4h] [ebp-D80h]
    GfxHullPointsPool(*hullPointsPoolArray)[256]; // [esp+5Ch] [ebp-D28h]
    int childPlaneCount; // [esp+60h] [ebp-D24h]
    GfxPortal *portal; // [esp+64h] [ebp-D20h]
    int queueIndex; // [esp+68h] [ebp-D1Ch]
    float portalVerts[64][3]; // [esp+6Ch] [ebp-D18h] BYREF
    float hullOrigin[3]; // [esp+36Ch] [ebp-A18h] BYREF
    uint32_t vertIndex; // [esp+378h] [ebp-A0Ch]
    PortalHeapNode portalQueue[256]; // [esp+37Ch] [ebp-A08h] BYREF
    float hull[64][2]; // [esp+B7Ch] [ebp-208h] BYREF
    uint32_t hullPointCount; // [esp+D80h] [ebp-4h]

    LargeLocal hullPointsPoolArray_large_local(0x20000);
    //LargeLocal::LargeLocal(&hullPointsPoolArray_large_local, 0x20000);
    //hullPointsPoolArray = (GfxHullPointsPool(*)[256])LargeLocal::GetBuf(&hullPointsPoolArray_large_local);
    hullPointsPoolArray = (GfxHullPointsPool(*)[256])hullPointsPoolArray_large_local.GetBuf();

    PROF_SCOPED("R_VisitPortals");

    iassert( Sys_IsMainThread() );
    for (queueIndex = 0; queueIndex < 255; ++queueIndex)
        (*hullPointsPoolArray)[queueIndex].nextFree = &(*hullPointsPoolArray)[queueIndex + 1];
    (*hullPointsPoolArray)[queueIndex].nextFree = 0;
    dpvsGlob.nextFreeHullPoints = (GfxHullPointsPool *)hullPointsPoolArray;
    dpvsGlob.portalQueue = portalQueue;
    dpvsGlob.queuedCount = 0;
    R_VisitPortalsForCellNoFrustum(cell, 0, 0, 0, 0, 0, 0);
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
            scale = -portal->plane.coeffs[3];
            Vec3Scale(portal->plane.coeffs, scale, hullOrigin);
            for (vertIndex = 0; vertIndex < hullPointCount; ++vertIndex)
            {
                Vec3Mad(hullOrigin, hull[vertIndex][0], portal->hullAxis[0], portalVerts[vertIndex]);
                Vec3Mad(portalVerts[vertIndex], hull[vertIndex][1], portal->hullAxis[1], portalVerts[vertIndex]);
            }
            childPlaneCount = R_PortalClipPlanesNoFrustum(dpvsGlob.childPlanes, hullPointCount, portalVerts);
            iassert( childPlaneCount <= DPVS_PORTAL_MAX_PLANES );
            R_VisitPortalsForCellNoFrustum(
                portal->cell,
                portal,
                &portal->plane,
                dpvsGlob.childPlanes,
                childPlaneCount,
                0,
                portal->writable.recursionDepth + 1);
        }
    }
    //LargeLocal::~LargeLocal(&hullPointsPoolArray_large_local);
}

uint32_t __cdecl R_PortalClipPlanesNoFrustum(
    DpvsPlane *planes,
    uint32_t vertexCount,
    const float (*winding)[3])
{
    DpvsPlane *a; // [esp+0h] [ebp-620h]
    DpvsPlane *v5; // [esp+8h] [ebp-618h]
    float *v6; // [esp+Ch] [ebp-614h]
    float normals[128][3]; // [esp+10h] [ebp-610h] BYREF
    uint32_t windingVertIndex; // [esp+614h] [ebp-Ch]
    uint32_t planeCount; // [esp+618h] [ebp-8h]
    bool useNormalPlanes; // [esp+61Fh] [ebp-1h]

    iassert( Sys_IsMainThread() );
    iassert( (vertexCount >= 3) );
    useNormalPlanes = vertexCount <= 0xA;
    R_GetSidePlaneNormals(winding, vertexCount, normals);
    planeCount = 0;
    if (useNormalPlanes)
    {
        for (windingVertIndex = 0; windingVertIndex < vertexCount; ++windingVertIndex)
        {
            if (Vec3LengthSq(normals[windingVertIndex]) != 0.0)
            {
                v5 = &planes[planeCount];
                v6 = normals[windingVertIndex];
                v5->coeffs[0] = *v6;
                v5->coeffs[1] = v6[1];
                v5->coeffs[2] = v6[2];
                a = &planes[planeCount];
                a->coeffs[3] = 0.001 - Vec3Dot(a->coeffs, &(*winding)[3 * windingVertIndex]);
                a->side[0] = COERCE_INT(a->coeffs[0]) <= 0 ? 0 : 0xC;
                a->side[1] = COERCE_INT(a->coeffs[1]) <= 0 ? 4 : 16;
                a->side[2] = COERCE_INT(a->coeffs[2]) <= 0 ? 8 : 20;
                ++planeCount;
            }
        }
    }
    return planeCount;
}

void __cdecl R_GetSidePlaneNormals(const float (*winding)[3], uint32_t vertexCount, float (*normals)[3])
{
    return R_NativeDpvsContext().R_GetSidePlaneNormals(winding, vertexCount, normals);
}

GfxPortal *__cdecl R_NextQueuedPortal()
{
    return R_NativeDpvsContext().R_NextQueuedPortal();
}

int R_AssertValidQueue()
{
    return R_NativeDpvsContext().R_AssertValidQueue();
}

void __cdecl R_VisitPortalsForCellNoFrustum(
    const GfxCell *cell,
    GfxPortal *parentPortal,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount,
    int frustumPlaneCount,
    signed int recursionDepth)
{
    uint8_t v7; // [esp+0h] [ebp-10h]
    GfxPortal *portal; // [esp+8h] [ebp-8h]
    int portalIndex; // [esp+Ch] [ebp-4h]

    R_SetCellVisible(cell);
    R_SetAncestorListStatus(parentPortal, 1);
    for (portalIndex = 0; portalIndex < cell->portalCount; ++portalIndex)
    {
        portal = &cell->portals[portalIndex];
        if (!R_ShouldSkipPortal(portal, planes, planeCount)
            && R_ChopPortalAndAddHullPointsNoFrustum(portal, parentPlane, planes, planeCount))
        {
            if (portal->writable.isQueued)
            {
                if (portal->writable.recursionDepth < recursionDepth)
                    v7 = portal->writable.recursionDepth;
                else
                    v7 = recursionDepth;
                portal->writable.recursionDepth = v7;
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
    R_SetAncestorListStatus(parentPortal, 0);
}

void __cdecl R_EnqueuePortal(GfxPortal *portal)
{
    return R_NativeDpvsContext().R_EnqueuePortal(portal);
}

bool __cdecl R_ShouldSkipPortal(const GfxPortal *portal, const DpvsPlane *planes, int planeCount)
{
    return R_NativeDpvsContext().R_ShouldSkipPortal(portal, planes, planeCount);
}

char __cdecl R_ChopPortalAndAddHullPointsNoFrustum(
    GfxPortal *portal,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount)
{
    int vertCount; // [esp+0h] [ebp-C10h]
    int vertIndex; // [esp+4h] [ebp-C0Ch]
    float v[2][128][3]; // [esp+8h] [ebp-C08h] BYREF
    const float (*w)[3]; // [esp+C0Ch] [ebp-4h] BYREF

    if (parentPlane)
    {
        vertCount = R_ChopPortal(portal, parentPlane, planes, planeCount, v, &w);
        if (!vertCount)
            return 0;
    }
    else
    {
        vertCount = portal->vertexCount;
        iassert( vertCount );
        w = portal->vertices;
    }
    for (vertIndex = 0; vertIndex < vertCount; ++vertIndex)
        R_AddVertToPortalHullPoints(portal, w[vertIndex]);
    return 1;
}

void __cdecl R_AddVertToPortalHullPoints(GfxPortal *portal, const float *v)
{
    return R_NativeDpvsContext().R_AddVertToPortalHullPoints(portal, v);
}

GfxHullPointsPool *__cdecl R_AllocHullPoints()
{
    return R_NativeDpvsContext().R_AllocHullPoints();
}

int __cdecl R_ChopPortal(
    const GfxPortal *portal,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount,
    float (*v)[128][3],
    const float (**finalVerts)[3])
{
    return R_NativeDpvsContext().R_ChopPortal(portal, parentPlane, planes, planeCount, v, finalVerts);
}

void __cdecl R_SetCellVisible(const GfxCell *cell)
{
    return R_NativeDpvsContext().R_SetCellVisible(cell);
}

void __cdecl R_AddWorldSurfacesFrustumOnly()
{
    int v0; // [esp+8h] [ebp-B4h]
    DpvsPlane *v1; // [esp+Ch] [ebp-B0h]
    int v2; // [esp+10h] [ebp-ACh]
    int v3; // [esp+14h] [ebp-A8h]
    DpvsPlane *v4; // [esp+18h] [ebp-A4h]
    int v5; // [esp+1Ch] [ebp-A0h]
    GfxCell *cell; // [esp+20h] [ebp-9Ch]
    GfxCell *cella; // [esp+20h] [ebp-9Ch]
    uint32_t cellIndex; // [esp+24h] [ebp-98h]
    uint32_t cellIndexa; // [esp+24h] [ebp-98h]
    uint32_t casterIndex; // [esp+28h] [ebp-94h]
    uint32_t cellDrawBits[32]; // [esp+2Ch] [ebp-90h] BYREF
    uint32_t cellCount; // [esp+ACh] [ebp-10h]
    uint32_t visibleCellIndex; // [esp+B0h] [ebp-Ch]
    uint32_t *cellCasterBits; // [esp+B4h] [ebp-8h]
    uint32_t cellBitsCount; // [esp+B8h] [ebp-4h]

    cellCount = rgp.world->dpvsPlanes.cellCount;
    iassert( cellCount );
    if (sm_strictCull->current.enabled)
    {
        cellBitsCount = (cellCount + 31) >> 5;
        memset((uint8_t *)cellDrawBits, 0, 4 * cellBitsCount);
        for (visibleCellIndex = 0; visibleCellIndex < cellCount; ++visibleCellIndex)
        {
            if ((dpvsGlob.cellVisibleBits[visibleCellIndex >> 5] & (1 << (visibleCellIndex & 0x1F))) != 0)
            {
                cellCasterBits = &rgp.world->cellCasterBits[cellBitsCount * visibleCellIndex];
                for (casterIndex = 0; casterIndex < cellBitsCount; ++casterIndex)
                    cellDrawBits[casterIndex] |= cellCasterBits[casterIndex];
            }
        }
        for (cellIndex = 0; cellIndex < cellCount; ++cellIndex)
        {
            if ((cellDrawBits[cellIndex >> 5] & (1 << (cellIndex & 0x1F))) != 0)
            {
                cell = &rgp.world->cells[cellIndex];
                v5 = 0;
                v4 = g_dpvsView->frustumPlanes;
                while (v5 < g_dpvsView->frustumPlaneCount)
                {
                    if (*(float *)((char *)cell->mins + v4->side[0]) * v4->coeffs[0]
                        + v4->coeffs[3]
                        + *(float *)((char *)cell->mins + v4->side[1]) * v4->coeffs[1]
                        + *(float *)((char *)cell->mins + v4->side[2]) * v4->coeffs[2] <= 0.0)
                    {
                        v3 = 1;
                        goto LABEL_22;
                    }
                    ++v5;
                    ++v4;
                }
                v3 = 0;
            LABEL_22:
                if (!v3)
                    R_AddCellSurfacesAndCullGroupsInFrustumDelayed(
                        cell,
                        g_dpvsView->frustumPlanes,
                        g_dpvsView->frustumPlaneCount, g_dpvsView->frustumPlaneCount);
            }
        }
    }
    else
    {
        for (cellIndexa = 0; cellIndexa < cellCount; ++cellIndexa)
        {
            cella = &rgp.world->cells[cellIndexa];
            v2 = 0;
            v1 = g_dpvsView->frustumPlanes;
            while (v2 < g_dpvsView->frustumPlaneCount)
            {
                if (*(float *)((char *)cella->mins + v1->side[0]) * v1->coeffs[0]
                    + v1->coeffs[3]
                    + *(float *)((char *)cella->mins + v1->side[1]) * v1->coeffs[1]
                    + *(float *)((char *)cella->mins + v1->side[2]) * v1->coeffs[2] <= 0.0)
                {
                    v0 = 1;
                    goto LABEL_34;
                }
                ++v2;
                ++v1;
            }
            v0 = 0;
        LABEL_34:
            if (!v0)
                R_AddCellSurfacesAndCullGroupsInFrustumDelayed(
                    cella,
                    g_dpvsView->frustumPlanes,
                    g_dpvsView->frustumPlaneCount,
                    g_dpvsView->frustumPlaneCount);
        }
    }
}

void __cdecl R_AddCellSurfacesAndCullGroupsInFrustumDelayed(
    const GfxCell *cell,
    const DpvsPlane *planes,
    uint8_t planeCount,
    uint8_t frustumPlaneCount)
{
    DpvsDynamicCellCmd dpvsDynamicCell; // [esp+0h] [ebp-18h] BYREF
    DpvsStaticCellCmd dpvsStaticCell; // [esp+Ch] [ebp-Ch] BYREF

    dpvsStaticCell.cell = cell;
    dpvsStaticCell.planes = planes;
    dpvsStaticCell.planeCount = planeCount;
    dpvsStaticCell.frustumPlaneCount = frustumPlaneCount;
    dpvsStaticCell.viewIndex = g_viewIndex; // *(_WORD *)(*((uint32_t *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 12);
    R_AddWorkerCmd(WRKCMD_DPVS_CELL_STATIC, (uint8_t *)&dpvsStaticCell);

    dpvsDynamicCell.cellIndex = cell - rgp.world->cells;
    dpvsDynamicCell.planes = planes;
    dpvsDynamicCell.planeCount = planeCount;
    dpvsDynamicCell.frustumPlaneCount = frustumPlaneCount;
    dpvsDynamicCell.viewIndex = g_viewIndex; //*(_WORD *)(*((uint32_t *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 12);
    R_AddWorkerCmd(WRKCMD_DPVS_CELL_DYN_MODEL, (uint8_t *)&dpvsDynamicCell);
    R_AddWorkerCmd(WRKCMD_DPVS_CELL_SCENE_ENT, (uint8_t *)&dpvsDynamicCell);
    R_AddWorkerCmd(WRKCMD_DPVS_CELL_DYN_BRUSH, (uint8_t *)&dpvsDynamicCell);
}

void __cdecl R_ShowCull()
{
    float s; // [esp+0h] [ebp-50h]
    float *origin; // [esp+14h] [ebp-3Ch]
    GfxSceneModel *sceneModel; // [esp+18h] [ebp-38h]
    float mins[3]; // [esp+1Ch] [ebp-34h] BYREF
    float radius; // [esp+28h] [ebp-28h]
    uint32_t sceneEntCount; // [esp+2Ch] [ebp-24h]
    float maxs[3]; // [esp+30h] [ebp-20h] BYREF
    uint32_t sceneEntIndex; // [esp+3Ch] [ebp-14h]
    GfxSceneEntity *sceneEnt; // [esp+40h] [ebp-10h]
    GfxSceneBrush *sceneBrush; // [esp+44h] [ebp-Ch]
    uint8_t *sceneEntVisData; // [esp+48h] [ebp-8h]
    const GfxBrushModel *bmodel; // [esp+4Ch] [ebp-4h]

    if (r_showCullXModels->current.enabled)
    {
        sceneEntCount = scene.sceneDObjCount;
        sceneEntVisData = scene.sceneDObjVisData[0];
        for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
        {
            sceneEnt = &scene.sceneDObj[sceneEntIndex];
            if (sceneEntVisData[sceneEntIndex] == 1)
                R_AddDebugBox(&frontEndDataOut->debugGlobals, sceneEnt->cull.mins, sceneEnt->cull.maxs, colorGreen);
            else
                R_AddDebugBox(&frontEndDataOut->debugGlobals, sceneEnt->cull.mins, sceneEnt->cull.maxs, colorRed);
        }
        sceneEntCount = scene.sceneModelCount;
        sceneEntVisData = scene.sceneModelVisData[0];
        for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
        {
            sceneModel = &scene.sceneModel[sceneEntIndex];
#ifndef KISAK_RADIANT
            origin = CG_GetEntityOrigin(scene.dpvs.localClientNum, sceneModel->entnum);
#else
            origin = sceneModel->placement.base.origin;
#endif
            radius = XModelGetRadius(sceneModel->model);
            s = -radius;
            Vec3AddScalar(origin, s, mins);
            Vec3AddScalar(origin, radius, maxs);
            if (sceneEntVisData[sceneEntIndex] == 1)
                R_AddDebugBox(&frontEndDataOut->debugGlobals, mins, maxs, colorGreen);
            else
                R_AddDebugBox(&frontEndDataOut->debugGlobals, mins, maxs, colorRed);
        }
    }
    if (r_showCullBModels->current.enabled)
    {
        sceneEntCount = scene.sceneBrushCount;
        sceneEntVisData = scene.sceneBrushVisData[0];
        for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
        {
            sceneBrush = &scene.sceneBrush[sceneEntIndex];
            bmodel = sceneBrush->bmodel;
            if (sceneEntVisData[sceneEntIndex] == 1)
                R_AddDebugBox(&frontEndDataOut->debugGlobals, bmodel->writable.mins, bmodel->writable.maxs, colorGreen);
            else
                R_AddDebugBox(&frontEndDataOut->debugGlobals, bmodel->writable.mins, bmodel->writable.maxs, colorRed);
        }
    }
}

void __cdecl R_InitSceneData(int localClientNum)
{
    uint32_t cellIndex; // [esp+0h] [ebp-10h]
    uint32_t offset; // [esp+4h] [ebp-Ch]
    uint32_t cellCount; // [esp+Ch] [ebp-4h]

    iassert( Sys_IsMainThread() );
    iassert( rgp.world );
    iassert( gfxCfg.maxClientViews * gfxCfg.entCount <= MAX_TOTAL_ENT_COUNT );
    cellCount = rgp.world->dpvsPlanes.cellCount;
    if ((gfxCfg.entCount & 0x1F) != 0)
        MyAssertHandler(
            ".\\r_dpvs.cpp",
            3153,
            0,
            "%s\n\t(gfxCfg.entCount) = %i",
            "(!(gfxCfg.entCount & 31))",
            gfxCfg.entCount);
    offset = localClientNum * (gfxCfg.entCount >> 5);
    if (offset >= 0x80)
        MyAssertHandler(
            ".\\r_dpvs.cpp",
            3156,
            0,
            "offset doesn't index MAX_TOTAL_ENT_COUNT >> 5\n\t%i not in [0, %i)",
            offset,
            128);
    for (cellIndex = 0; cellIndex < 2 * cellCount; ++cellIndex)
        Com_Memset(&rgp.world->dpvsPlanes.sceneEntCellBits[128 * cellIndex + offset], 0, 4 * (gfxCfg.entCount >> 5));
    memset((uint8_t *)dpvsGlob.entVisBits[localClientNum], 0, 4 * (gfxCfg.entCount >> 5));
    memset((uint8_t *)scene.dpvs.entInfo[localClientNum], 0, 4 * gfxCfg.entCount);
}

void __cdecl DynEntCl_InitFilter()
{
    uint32_t drawType; // [esp+0h] [ebp-4h]

    iassert( Sys_IsMainThread() );
    iassert( rgp.world );
    scene.sceneDynModelCount = 0;
    scene.sceneDynBrushCount = 0;
    for (drawType = 0; drawType < 2; ++drawType)
        if (!R_ClearDynEntityCellLinks(*rgp.world, drawType))
            Com_Error(ERR_DROP, "DynEntCl_InitFilter: invalid canonical cell storage");
}

void __cdecl R_InitSceneBuffers()
{
    uint32_t localClientNum; // [esp+0h] [ebp-8h]
    uint32_t viewIndex; // [esp+4h] [ebp-4h]

    iassert( (gfxCfg.entCount & 31) == 0 );
    scene.entOverflowedDrawBuf = (uint32_t *)R_AllocGlobalVariable(gfxCfg.entCount >> 3, "R_InitSceneBuffers");
    for (viewIndex = 0; viewIndex < 7; ++viewIndex)
        scene.dpvs.entVisData[viewIndex] = (uint8_t *)R_AllocGlobalVariable(gfxCfg.entCount, "R_InitSceneBuffers");
    scene.dpvs.sceneXModelIndex = (uint16_t *)R_AllocGlobalVariable(2 * gfxCfg.entCount, "R_InitSceneBuffers");
    scene.dpvs.sceneDObjIndex = (uint16_t *)R_AllocGlobalVariable(2 * gfxCfg.entCount, "R_InitSceneBuffers");
    for (localClientNum = 0; localClientNum < gfxCfg.maxClientViews; ++localClientNum)
    {
        dpvsGlob.entVisBits[localClientNum] = (uint32_t *)R_AllocGlobalVariable(
            4 * (gfxCfg.entCount >> 5),
            "R_InitSceneBuffers");
        scene.dpvs.entInfo[localClientNum] = (GfxEntCellRefInfo *)R_AllocGlobalVariable(
            4 * gfxCfg.entCount,
            "R_InitSceneBuffers");
    }
}

void __cdecl R_ClearDpvsScene()
{
    uint32_t drawType; // [esp+0h] [ebp-8h]
    int i; // [esp+4h] [ebp-4h]
    int ia; // [esp+4h] [ebp-4h]

    iassert( rgp.world );
    iassert( rgp.world->cells );
    Com_Memset((uint32_t *)scene.dpvs.sceneXModelIndex, 255, 2 * gfxCfg.entCount);
    if (*scene.dpvs.sceneXModelIndex != 0xFFFF)
        MyAssertHandler(
            ".\\r_dpvs.cpp",
            3236,
            0,
            "%s\n\t(scene.dpvs.sceneXModelIndex[0]) = %i",
            "(scene.dpvs.sceneXModelIndex[0] == (65535))",
            *scene.dpvs.sceneXModelIndex);
    Com_Memset((uint32_t *)scene.dpvs.sceneDObjIndex, 255, 2 * gfxCfg.entCount);
    if (*scene.dpvs.sceneDObjIndex != 0xFFFF)
        MyAssertHandler(
            ".\\r_dpvs.cpp",
            3239,
            0,
            "%s\n\t(scene.dpvs.sceneDObjIndex[0]) = %i",
            "(scene.dpvs.sceneDObjIndex[0] == (65535))",
            *scene.dpvs.sceneDObjIndex);
    iassert( (gfxCfg.entCount & 31) == 0 );
    Com_Memset(scene.entOverflowedDrawBuf, 0, gfxCfg.entCount >> 3);
    for (i = 0; i < 7; ++i)
        Com_Memset((uint32_t *)scene.dpvs.entVisData[i], 0, gfxCfg.entCount);
    for (ia = 0; ia < 3; ++ia)
    {
        R_ClearStaticDpvsView(*rgp.world, ia, true);
        for (drawType = 0; drawType < 2; ++drawType)
            Com_Memset(
                (uint32_t *)rgp.world->dpvsDyn.dynEntVisData[drawType][ia],
                0,
                rgp.world->dpvsDyn.dynEntClientCount[drawType]);
    }
    Com_Memset((uint32_t *)&rgp.world->sceneDynModel->info, 0, 6 * scene.sceneDynModelCount);
    scene.sceneDynModelCount = 0;
    Com_Memset((uint32_t *)rgp.world->sceneDynBrush, 0, 4 * scene.sceneDynBrushCount);
    scene.sceneDynBrushCount = 0;
    R_SetVisData(0);
}

bool __cdecl R_CullDynamicSpotLightInCameraView()
{
    if (!scene.addedLightCount)
        return 1;
    if (scene.addedLight[0].type != 2)
        return 1;
    scene.isAddedLightCulled[0] = R_CullPointAndRadius(
        scene.addedLight[0].origin,
        scene.addedLight[0].radius,
        dpvsGlob.views[scene.dpvs.localClientNum][SCENE_VIEW_CAMERA].frustumPlanes,
        dpvsGlob.views[scene.dpvs.localClientNum][SCENE_VIEW_CAMERA].frustumPlaneCount);
    return scene.isAddedLightCulled[0];
}

void __cdecl R_CullDynamicPointLightsInCameraView()
{
    int planeCount; // [esp+Ch] [ebp-10h]
    DpvsPlane *planes; // [esp+10h] [ebp-Ch]
    GfxLight *dl; // [esp+14h] [ebp-8h]
    int lightIndex; // [esp+18h] [ebp-4h]

    planes = dpvsGlob.views[scene.dpvs.localClientNum][SCENE_VIEW_CAMERA].frustumPlanes;
    planeCount = dpvsGlob.views[scene.dpvs.localClientNum][SCENE_VIEW_CAMERA].frustumPlaneCount;
    for (lightIndex = 0; lightIndex < scene.addedLightCount; ++lightIndex)
    {
        dl = &scene.addedLight[lightIndex];
        if (dl->type != 2 || lightIndex)
        {
            iassert( dl->type == GFX_LIGHT_TYPE_OMNI );
            scene.isAddedLightCulled[lightIndex] = R_CullPointAndRadius(dl->origin, dl->radius, planes, planeCount);
        }
    }
}

const float standardFrustumSidePlanes[4][4] =
{
  { -1.0, 0.0, 0.0, 1.0 },
  { 1.0, 0.0, 0.0, 1.0 },
  { 0.0, -1.0, 0.0, 1.0 },
  { 0.0, 1.0, 0.0, 1.0 }
}; // idb

void __cdecl R_SetupWorldSurfacesDpvs(const GfxViewParms *viewParms)
{
    return R_NativeDpvsContext().R_SetupWorldSurfacesDpvs(viewParms);
}

int __cdecl R_AddNearAndFarClipPlanes(DpvsPlane *planes, int planeCount)
{
    return R_NativeDpvsContext().R_AddNearAndFarClipPlanes(planes, planeCount);
}

void __cdecl R_SetupDpvsForPoint(const GfxViewParms *viewParms)
{
    return R_NativeDpvsContext().R_SetupDpvsForPoint(viewParms);
}

void __cdecl R_SetViewFrustumPlanes(GfxViewInfo *viewInfo)
{
    iassert(dpvsGlob.views[scene.dpvs.localClientNum][SCENE_VIEW_CAMERA].frustumPlaneCount >= 4);

    for (int i = 0; i < 4; ++i)
    {
        viewInfo->frustumPlanes[i][0] = dpvsGlob.views[scene.dpvs.localClientNum][SCENE_VIEW_CAMERA].frustumPlanes[i].coeffs[0];
        viewInfo->frustumPlanes[i][1] = dpvsGlob.views[scene.dpvs.localClientNum][SCENE_VIEW_CAMERA].frustumPlanes[i].coeffs[1];
        viewInfo->frustumPlanes[i][2] = dpvsGlob.views[scene.dpvs.localClientNum][SCENE_VIEW_CAMERA].frustumPlanes[i].coeffs[2];
        viewInfo->frustumPlanes[i][3] = dpvsGlob.views[scene.dpvs.localClientNum][SCENE_VIEW_CAMERA].frustumPlanes[i].coeffs[3];
    }
}

// lol.

void __cdecl R_AddSkySurfacesDpvs(const DpvsPlane *planes, int planeCount)
{
    int v2; // [esp+4h] [ebp-1B0h]
    int v3; // [esp+8h] [ebp-1ACh]
    const DpvsPlane *v4; // [esp+Ch] [ebp-1A8h]
    uint32_t i; // [esp+10h] [ebp-1A4h]
    GfxSurface *v6; // [esp+18h] [ebp-19Ch]
    DpvsPlane clipPlanePool[16]; // [esp+1Ch] [ebp-198h] BYREF
    int surfIndex; // [esp+160h] [ebp-54h]
    DpvsClipPlaneSet clipSet; // [esp+164h] [ebp-50h]
    int planeIndex; // [esp+1B0h] [ebp-4h]

    iassert( Sys_IsMainThread() );
    g_smodelVisData = rgp.world->dpvs.smodelVisData[0];
    g_surfaceVisData = rgp.world->dpvs.surfaceVisData[0];
    for (planeIndex = 0; planeIndex < planeCount; ++planeIndex)
    {
        R_CopyClipPlane(&planes[planeIndex], &clipPlanePool[planeIndex]);
        clipSet.planes[planeIndex] = &clipPlanePool[planeIndex];
    }
    clipSet.count = planeCount;
    for (surfIndex = 0; surfIndex < rgp.world->skySurfCount; ++surfIndex)
    {
        v2 = rgp.world->skyStartSurfs[surfIndex];
        if (!*(_BYTE *)(g_surfaceVisData + v2))
        {
            v6 = &rgp.world->dpvs.surfaces[v2];
            for (i = 0; i < clipSet.count; ++i)
            {
                v4 = clipSet.planes[i];
                if (*(float *)((char *)v6->bounds[0] + v4->side[0]) * v4->coeffs[0]
                    + v4->coeffs[3]
                    + *(float *)((char *)v6->bounds[0] + v4->side[1]) * v4->coeffs[1]
                    + *(float *)((char *)v6->bounds[0] + v4->side[2]) * v4->coeffs[2] <= 0.0)
                {
                    v3 = 1;
                    goto LABEL_15;
                }
            }
            v3 = 0;
        LABEL_15:
            if (!v3)
            {
                if ((r_showAabbTrees->current.integer & 2) != 0)
                    R_AddDebugBox(&frontEndDataOut->debugGlobals, v6->bounds[0], v6->bounds[1], colorGreen);
                *(_BYTE *)(g_surfaceVisData + v2) = 1;
            }
        }
    }
}

void __cdecl R_AddWorldSurfacesDpvs(const GfxViewParms *viewParms, int cameraCellIndex)
{
    DpvsView *dpvsView; // [esp+0h] [ebp-4h]

    iassert( Sys_IsMainThread() );
    iassert( rgp.world );
    iassert( viewParms );
    rg.debugViewParms = viewParms;
    R_AddWorldSurfacesPortalWalk(cameraCellIndex);
    dpvsView = dpvsGlob.views[scene.dpvs.localClientNum];
    if (dpvsGlob.farPlane)
    {
        if (dpvsView->frustumPlaneCount <= 0)
            MyAssertHandler(
                ".\\r_dpvs.cpp",
                3369,
                0,
                "%s\n\t(dpvsView->frustumPlaneCount) = %i",
                "(dpvsView->frustumPlaneCount > 0)",
                dpvsView->frustumPlaneCount);
        if (!Vec4Compare(dpvsView->frustumPlanes[dpvsView->frustumPlaneCount - 1].coeffs, dpvsGlob.farPlane->coeffs))
            MyAssertHandler(
                ".\\r_dpvs.cpp",
                3370,
                0,
                "%s",
                "Vec4Compare( dpvsView->frustumPlanes[dpvsView->frustumPlaneCount - 1].coeffs, dpvsGlob.farPlane->coeffs )");
        R_AddSkySurfacesDpvs(dpvsView->frustumPlanes, dpvsView->frustumPlaneCount - 1);
    }
    if (r_vc_makelog->current.integer)
        R_ShowLightVisCachePoints(viewParms->origin, dpvsView->frustumPlanes, dpvsView->frustumPlaneCount);
}

void __cdecl R_AddWorldSurfacesPortalWalk(int cameraCellIndex)
{
    vassert(dpvsGlob.views[scene.dpvs.localClientNum] == g_dpvsView,
        "dpsView == %p, g_dpsView == %p", dpvsGlob.views[scene.dpvs.localClientNum], g_dpvsView);
    return R_NativeDpvsContext().R_AddWorldSurfacesPortalWalk(cameraCellIndex);
}

const float color[4] = { 0.0f, 1.0f, 1.0f, 0.25f };

void __cdecl R_VisitPortals(const GfxCell *cell, const DpvsPlane *parentPlane, const DpvsPlane *planes, int planeCount)
{
    return R_NativeDpvsContext().R_VisitPortals(cell, parentPlane, planes, planeCount);
}

uint32_t __cdecl R_PortalClipPlanes(
    DpvsPlane *planes,
    uint32_t vertexCount,
    const float (*winding)[3],
    GfxCell *cell,
    DpvsClipChildren *clipChildren)
{
    return R_NativeDpvsContext().R_PortalClipPlanes(planes, vertexCount, winding, cell, clipChildren);
}

void __cdecl R_ProjectPortal(
    int vertexCount,
    const float (*winding)[3],
    float *mins,
    float *maxs,
    DpvsClipChildren *clipChildren)
{
    return R_NativeDpvsContext().R_ProjectPortal(vertexCount, winding, mins, maxs, clipChildren);
}

uint32_t __cdecl R_AddBevelPlanes(
    DpvsPlane *planes,
    uint32_t vertexCount,
    const float (*winding)[3],
    const float (*windingNormals)[3],
    float *mins,
    float *maxs,
    DpvsForceBevels forceBevels)
{
    return R_NativeDpvsContext().R_AddBevelPlanes(planes, vertexCount, winding, windingNormals, mins, maxs, forceBevels);
}

void __cdecl R_UnprojectPoint(const float *projected, float *unprojected)
{
    return R_NativeDpvsContext().R_UnprojectPoint(projected, unprojected);
}

void __cdecl R_VisitPortalsForCell(
    const GfxCell *cell,
    GfxPortal *parentPortal,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount,
    int frustumPlaneCount,
    signed int recursionDepth,
    DpvsClipChildren clipChildren)
{
    return R_NativeDpvsContext().R_VisitPortalsForCell(cell, parentPortal, parentPlane, planes, planeCount, frustumPlaneCount, recursionDepth, clipChildren);
}

char __cdecl R_ChopPortalAndAddHullPoints(
    GfxPortal *portal,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount)
{
    return R_NativeDpvsContext().R_ChopPortalAndAddHullPoints(portal, parentPlane, planes, planeCount);
}

void __cdecl R_VisitAllFurtherCells(
    const GfxCell *cell,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount,
    uint8_t frustumPlaneCount)
{
    return R_NativeDpvsContext().R_VisitAllFurtherCells(cell, parentPlane, planes, planeCount, frustumPlaneCount);
}

int __cdecl R_GetFurtherCellList_r(
    const GfxCell *cell,
    const DpvsPlane *parentPlane,
    const DpvsPlane *planes,
    int planeCount,
    float (*v)[128][3],
    const GfxCell **list,
    int count)
{
    return R_NativeDpvsContext().R_GetFurtherCellList_r(cell, parentPlane, planes, planeCount, v, list, count);
}

void __cdecl R_SetupShadowSurfacesDpvs(
    const GfxViewParms *viewParms,
    const float (*sidePlanes)[4],
    uint32_t sidePlaneCount,
    int partitionIndex)
{
    DpvsView *dpvsView; // [esp+0h] [ebp-8h]

    iassert(Sys_IsMainThread());
    iassert(rgp.world);
    iassert(viewParms);

    dpvsView = &dpvsGlob.views[scene.dpvs.localClientNum][SCENE_VIEW_SUNSHADOW_0 + partitionIndex];
    dpvsView->renderFxFlagsCull = 1;

    iassert(sidePlaneCount <= ARRAY_COUNT(dpvsView->frustumPlanes));

    R_FrustumClipPlanes(&viewParms->viewProjectionMatrix, sidePlanes, sidePlaneCount, dpvsView->frustumPlanes);

    // Add Near plane only if facing the same way as the view direction (Which at this point should be sunAxis[0] - sun forward)
    if (Vec3Dot(viewParms->axis[0], scene.shadowNearPlane[partitionIndex].coeffs) < 0.0f)
    {
        iassert(sidePlaneCount < ARRAY_COUNT(dpvsView->frustumPlanes));
        memcpy(&dpvsView->frustumPlanes[sidePlaneCount], &scene.shadowNearPlane[partitionIndex], sizeof(DpvsPlane));
        sidePlaneCount++;
    }

    // Add far plane if doing the Near Partition AND it's facing the same way 
    if ((!partitionIndex || !rg.sunShadowFull) && Vec3Dot(viewParms->axis[0], scene.shadowFarPlane[partitionIndex].coeffs) < 0.0)
    {
        iassert(sidePlaneCount < ARRAY_COUNT(dpvsView->frustumPlanes));
        memcpy(&dpvsView->frustumPlanes[sidePlaneCount], &scene.shadowFarPlane[partitionIndex], sizeof(DpvsPlane));
        sidePlaneCount++;
    }

    // Add the side planes
    for (int planeIndex = 0; planeIndex < 4; ++planeIndex)
    {
        if (Vec3Dot(viewParms->axis[0], dpvsGlob.sideFrustumPlanes[planeIndex].coeffs) < 0.0)
        {
            iassert(sidePlaneCount < ARRAY_COUNT(dpvsView->frustumPlanes));
            memcpy(&dpvsView->frustumPlanes[sidePlaneCount], &dpvsGlob.sideFrustumPlanes[planeIndex], sizeof(DpvsPlane));
            sidePlaneCount++;
        }
    }

    dpvsView->frustumPlaneCount = sidePlaneCount;
}

double __cdecl R_GetFarPlaneDist()
{
    if (r_zfar->current.value == 0.0)
        return dpvsGlob.cullDist;
    else
        return r_zfar->current.value;
}

uint32_t __cdecl R_CalcReflectionProbeIndex(const GfxWorld *world, const float *origin)
{
    uint32_t cellIndex; // [esp+0h] [ebp-4h]

    cellIndex = R_CellForPoint(world, origin);
    if (cellIndex == -1)
        return R_FindNearestReflectionProbe(world, origin);
    if (cellIndex >= world->dpvsPlanes.cellCount)
        MyAssertHandler(
            ".\\r_staticmodel_load_obj.cpp",
            552,
            0,
            "cellIndex doesn't index world->dpvsPlanes.cellCount\n\t%i not in [0, %i)",
            cellIndex,
            world->dpvsPlanes.cellCount);
    return R_FindNearestReflectionProbeInCell(world, &world->cells[cellIndex], origin);
}

void __cdecl R_FreeHullPoints(GfxHullPointsPool *hullPoints)
{
    return R_NativeDpvsContext().R_FreeHullPoints(hullPoints);
}

void R_SetCullDist(float dist)
{
    if (dist > 0.0)
        dpvsGlob.cullDist = dist;
    else
        dpvsGlob.cullDist = 0.0;
}