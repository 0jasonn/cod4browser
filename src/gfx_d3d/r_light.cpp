#include <universal/q_shared.h>
#include "r_light.h"
#include "r_dynamiclights_core.h"
#include <qcommon/qcommon.h>
#include <universal/com_files.h>
#include <universal/com_memory.h>
#include "r_image.h"
#include "r_init.h"
#include "r_scene.h"
#include "r_dvars.h"
#include <database/database.h>
#include "r_bsp.h"
#include "r_marks.h"

#include <algorithm>
#include "r_staticmodelcache.h"
#include "r_add_staticmodel.h"
#include <DynEntity/DynEntity_client.h>
#include "fxprimitives.h"
#include <EffectsCore/fx_system.h>
#include "r_pretess.h"
#include <qcommon/com_bsp.h>
#include <universal/profile.h>

struct StaticModelLightCallback // sizeof=0x74
{                                       // ...
    uint8_t *smodelVisData;     // ...
    float position[3];                  // ...
    float radiusSq;                     // ...
    float planes[6][4];                 // ...
};

LightGlobals lightGlob;
StaticModelLightCallback g_staticModelLightCallback;

int(__cdecl *allowSurf_0[1])(int, void *) = { R_AllowBspOmniLight };
int(__cdecl *allowSurf_1[2])(int, void *) = { R_AllowBspSpotLight, R_AllowBspSpotLightShadows };

void __cdecl R_EnumLightDefs(void(__cdecl *func)(GfxLightDef *, void *), void *data)
{
    GfxLightDef *def; // [esp+0h] [ebp-8h]
    int defIndex; // [esp+4h] [ebp-4h]

    for (defIndex = 0; defIndex < lightGlob.defCount; ++defIndex)
    {
        def = lightGlob.defs[defIndex];
        iassert( def );
        func(def, data);
    }
}

GfxLightDef *__cdecl R_RegisterLightDef_LoadObj(const char *name)
{
    GfxLightDef *defa; // [esp+0h] [ebp-8h]
    int defIndex; // [esp+4h] [ebp-4h]
    int defIndexa; // [esp+4h] [ebp-4h]

    iassert( name );
    for (defIndex = 0; defIndex < lightGlob.defCount; ++defIndex)
    {
        iassert( lightGlob.defs[defIndex] );
        iassert( lightGlob.defs[defIndex]->name );
        if (!I_stricmp(name, lightGlob.defs[defIndex]->name))
            return lightGlob.defs[defIndex];
    }
    if (lightGlob.defCount == 64)
    {
        Com_Printf(8, "Loaded light defs:\n");
        for (defIndexa = 0; defIndexa < lightGlob.defCount; ++defIndexa)
            Com_Printf(8, "  %s\n", lightGlob.defs[defIndexa]->name);
        Com_Error(
            ERR_DROP,
            "Can't load light def %s; %i unique light defs already loaded",
            name,
            lightGlob.defCount
            );
    }
    defa = R_LoadLightDef(name);
    if (defa)
    {
        lightGlob.defs[lightGlob.defCount++] = defa;
        return defa;
    }
    else
    {
        if (!I_stricmp(name, "light_dynamic"))
            Com_Error(ERR_DROP, "Can't load the default light def '%s'", name);
        return R_RegisterLightDef("light_dynamic");
    }
}

GfxLightDef *__cdecl R_RegisterLightDef(const char *name)
{
    if (IsFastFileLoad())
        return R_RegisterLightDef_FastFile(name);
    else
        return R_RegisterLightDef_LoadObj(name);
}

GfxLightDef *__cdecl R_RegisterLightDef_FastFile(const char *name)
{
    return DB_FindXAssetHeader(ASSET_TYPE_LIGHT_DEF, name).lightDef;
}

void __cdecl R_InitLightDefs()
{
    iassert( lightGlob.defCount == 0 );
    rgp.dlightDef = R_RegisterLightDef("light_dynamic");
}

void __cdecl R_ShutdownLightDefs()
{
    lightGlob.defCount = 0;
}

int __cdecl R_GetPointLightPartitions(const GfxLight **visibleLights)
{
    const GfxLight *addedLights[32]; // [esp+0h] [ebp-90h] BYREF
    int visibleCount; // [esp+84h] [ebp-Ch]
    int visibleLimit; // [esp+88h] [ebp-8h]
    int lightIndex; // [esp+8Ch] [ebp-4h]

    if (scene.addedLightCount > 32)
        MyAssertHandler(
            ".\\r_light.cpp",
            229,
            1,
            "%s\n\t(scene.addedLightCount) = %i",
            "(scene.addedLightCount <= 32)",
            scene.addedLightCount);
    visibleCount = 0;
    for (lightIndex = 0; lightIndex < scene.addedLightCount; ++lightIndex)
    {
        if (!scene.isAddedLightCulled[lightIndex])
        {
            iassert( (visibleCount <= 32) );
            addedLights[visibleCount++] = &scene.addedLight[lightIndex];
        }
    }
    visibleLimit = r_dlightLimit->current.integer;
    iassert( (visibleLimit <= 4) );
    if (visibleCount > visibleLimit)
    {
        R_MostImportantLights(addedLights, visibleCount, visibleLimit);
        visibleCount = visibleLimit;
    }
    for (lightIndex = 0; lightIndex < visibleCount; ++lightIndex)
    {
        visibleLights[lightIndex] = addedLights[lightIndex];
        scene.visLightShadow[lightIndex - 4].drawSurfCount = 0;
    }
    return visibleCount;
}

void __cdecl R_MostImportantLights(const GfxLight **lights, int lightCount, int keepCount)
{
    iassert(lightCount > keepCount);
    iassert(keepCount >= 1);
    kisak::dynamic_lights::MostImportant(lights, lightCount, keepCount, rg.viewOrg);
}

bool __cdecl R_LightImportanceGreaterEqual(const GfxLight *light0, const GfxLight *light1)
{
    iassert(light0->type == 2 || light0->type == 3);
    iassert(light1->type == 2 || light1->type == 3);
    return kisak::dynamic_lights::ImportanceGreaterEqual(light0, light1, rg.viewOrg);
}

void __cdecl R_GetBspLightSurfs(const GfxLight **visibleLights, int visibleCount)
{
    GfxBspDrawSurfData surfData[2];

    iassert(visibleCount);
    iassert(rgp.world);

    R_InitBspDrawSurf(&surfData[0]);
    R_InitBspDrawSurf(&surfData[1]);

    for (int lightIndex = 0; lightIndex < visibleCount; ++lightIndex)
    {
        const GfxLight *light = visibleLights[lightIndex];
        iassert(light->type == GFX_LIGHT_TYPE_OMNI || light->type == GFX_LIGHT_TYPE_SPOT);

        if (light->type == GFX_LIGHT_TYPE_OMNI)
            R_GetBspOmniLightSurfs(light, lightIndex, surfData);
        else
            R_GetBspSpotLightSurfs(light, lightIndex, surfData);
    }
}

BOOL __cdecl R_SortBspShadowReceiverSurfaces(GfxSurface *surface0, GfxSurface *surface1)
{
    return surface0 < surface1;
}

void __cdecl R_GetBspOmniLightSurfs(const GfxLight *light, int lightIndex, GfxBspDrawSurfData *surfData)
{
    uint16_t triSurfList[2]; // [esp+F0h] [ebp-4Ch] BYREF
    uint32_t surfIndex; // [esp+F4h] [ebp-48h]
    float mins[3]; // [esp+F8h] [ebp-44h] BYREF
    BspOmniLightCallback bspLightCallback; // [esp+104h] [ebp-38h] BYREF
    uint32_t visLightDrawSurfCount; // [esp+118h] [ebp-24h] BYREF
    uint8_t *surfaceVisData; // [esp+11Ch] [ebp-20h]
    GfxSurface **surfaces[1]; // [esp+120h] [ebp-1Ch] BYREF
    float maxs[3]; // [esp+124h] [ebp-18h] BYREF
    GfxDrawSurf *drawSurfs; // [esp+130h] [ebp-Ch]
    GfxDrawSurf *surfaceMaterials; // [esp+134h] [ebp-8h]
    uint32_t listSurfIndex; // [esp+138h] [ebp-4h]
    //int savedregs; // [esp+13Ch] [ebp+0h] BYREF

    surfaceVisData = rgp.world->dpvs.surfaceVisData[0];
    surfaceMaterials = rgp.world->dpvs.surfaceMaterials;
    mins[0] = light->origin[0] - light->radius;
    mins[1] = light->origin[1] - light->radius;
    mins[2] = light->origin[2] - light->radius;
    maxs[0] = light->origin[0] + light->radius;
    maxs[1] = light->origin[1] + light->radius;
    maxs[2] = light->origin[2] + light->radius;
    drawSurfs = scene.visLight[lightIndex].drawSurfs;
    surfaces[0] = (GfxSurface **)&drawSurfs[512];
    bspLightCallback.surfaceVisData = surfaceVisData;
    bspLightCallback.position[0] = light->origin[0];
    bspLightCallback.position[1] = light->origin[1];
    bspLightCallback.position[2] = light->origin[2];
    bspLightCallback.radiusSq = light->radius * light->radius;
    R_BoxSurfaces(
        mins,
        maxs,
        allowSurf_0,
        &bspLightCallback,
        surfaces,
        0x400u,
        &visLightDrawSurfCount,
        1u);
    if (visLightDrawSurfCount)
    {
        surfData->drawSurfList.current = drawSurfs;
        surfData->drawSurfList.end = (GfxDrawSurf *)&scene.visLightShadow[lightIndex - 3];
        //std::_Sort<int *, int, bool(__cdecl *)(int, int)>(
        //    (const GfxStaticModelDrawInst **)surfaces[0],
        //    (const GfxStaticModelDrawInst **)&surfaces[0][visLightDrawSurfCount],
        //    (int)(4 * visLightDrawSurfCount) >> 2,
        //    (bool(__cdecl *)(const GfxStaticModelDrawInst *, const GfxStaticModelDrawInst *))R_SortBspShadowReceiverSurfaces);
        std::sort(&surfaces[0][0], &surfaces[0][visLightDrawSurfCount], R_SortBspShadowReceiverSurfaces);
        for (listSurfIndex = 0; listSurfIndex < visLightDrawSurfCount; ++listSurfIndex)
        {
            if (listSurfIndex >= rgp.world->surfaceCount)
                MyAssertHandler(
                    ".\\r_light.cpp",
                    491,
                    0,
                    "listSurfIndex doesn't index rgp.world->surfaceCount\n\t%i not in [0, %i)",
                    listSurfIndex,
                    rgp.world->surfaceCount);
            surfIndex = surfaces[0][listSurfIndex] - rgp.world->dpvs.surfaces;
            triSurfList[0] = surfIndex;
            R_AddBspDrawSurfs(surfaceMaterials[surfIndex], (uint8_t *)triSurfList, 1u, surfData);
        }
        R_EndCmdBuf(&surfData->delayedCmdBuf);
        scene.visLightShadow[lightIndex - 4].drawSurfCount = surfData->drawSurfList.current
            - scene.visLight[lightIndex].drawSurfs;
    }
    else
    {
        scene.visLightShadow[lightIndex - 4].drawSurfCount = 0;
    }
}

int __cdecl R_AllowBspOmniLight(int surfIndex, void *bspLightCallbackAsVoid)
{
    const auto *callback = static_cast<const BspOmniLightCallback *>(bspLightCallbackAsVoid);
    return callback->surfaceVisData[surfIndex] &&
        kisak::dynamic_lights::BoxInSphere(callback->position, callback->radiusSq,
            rgp.world->dpvs.surfaces[surfIndex].bounds[0],
            rgp.world->dpvs.surfaces[surfIndex].bounds[1]);
}

void __cdecl R_GetBspSpotLightSurfs(const GfxLight *light, int lightIndex, GfxBspDrawSurfData *surfData)
{
    uint16_t triSurfList[2]; // [esp+1B4h] [ebp-ACh] BYREF
    uint32_t surfIndex; // [esp+1B8h] [ebp-A8h]
    float mins[3]; // [esp+1BCh] [ebp-A4h] BYREF
    BspSpotLightCallback bspLightCallback; // [esp+1C8h] [ebp-98h] BYREF
    uint8_t *surfaceVisData; // [esp+230h] [ebp-30h]
    GfxSurface **surfaces[2]; // [esp+234h] [ebp-2Ch] BYREF
    float maxs[3]; // [esp+23Ch] [ebp-24h] BYREF
    GfxDrawSurf *drawSurfs[2]; // [esp+248h] [ebp-18h]
    GfxDrawSurf *surfaceMaterials; // [esp+250h] [ebp-10h]
    uint32_t listSurfIndex; // [esp+254h] [ebp-Ch]
    uint32_t surfCounts[2]; // [esp+258h] [ebp-8h] BYREF

    iassert(lightIndex < MAX_VISIBLE_SHADOWABLE_DLIGHTS);

    surfaceVisData = rgp.world->dpvs.surfaceVisData[0];
    surfaceMaterials = rgp.world->dpvs.surfaceMaterials;

    mins[0] = light->origin[0] - light->radius;
    mins[1] = light->origin[1] - light->radius;
    mins[2] = light->origin[2] - light->radius;

    maxs[0] = light->origin[0] + light->radius;
    maxs[1] = light->origin[1] + light->radius;
    maxs[2] = light->origin[2] + light->radius;

    drawSurfs[0] = scene.visLight[lightIndex].drawSurfs;
    surfaces[0] = (GfxSurface **)&drawSurfs[0][512];
    drawSurfs[1] = scene.visLightShadow[lightIndex].drawSurfs;
    surfaces[1] = (GfxSurface **)&drawSurfs[1][512];
    bspLightCallback.surfaceVisData = surfaceVisData;
    R_CalcSpotLightPlanes(light, bspLightCallback.planes);
    R_BoxSurfaces(mins, maxs, allowSurf_1, &bspLightCallback, surfaces, 0x400u, surfCounts, 2u);
    if (surfCounts[0])
    {
        scene.visLightShadow[lightIndex - 4].drawSurfCount = surfCounts[0];
        surfData->drawSurfList.current = drawSurfs[0];
        surfData->drawSurfList.end = (GfxDrawSurf *)&scene.visLightShadow[lightIndex - 3];
        //std::_Sort<int *, int, bool(__cdecl *)(int, int)>(
        //    (const GfxStaticModelDrawInst **)surfaces[0],
        //    (const GfxStaticModelDrawInst **)&surfaces[0][surfCounts[0]],
        //    (signed int)(4 * surfCounts[0]) >> 2,
        //    (bool(__cdecl *)(const GfxStaticModelDrawInst *, const GfxStaticModelDrawInst *))R_SortBspShadowReceiverSurfaces);
        std::sort(&surfaces[0][0], &surfaces[0][surfCounts[0]], R_SortBspShadowReceiverSurfaces);
        for (listSurfIndex = 0; listSurfIndex < surfCounts[0]; ++listSurfIndex)
        {
            if (listSurfIndex >= rgp.world->surfaceCount)
                MyAssertHandler(
                    ".\\r_light.cpp",
                    557,
                    0,
                    "listSurfIndex doesn't index rgp.world->surfaceCount\n\t%i not in [0, %i)",
                    listSurfIndex,
                    rgp.world->surfaceCount);
            surfIndex = surfaces[0][listSurfIndex] - rgp.world->dpvs.surfaces;
            triSurfList[0] = surfIndex;
            R_AddBspDrawSurfs(surfaceMaterials[surfIndex], (uint8_t *)triSurfList, 1u, surfData);
        }
        R_EndCmdBuf(&surfData->delayedCmdBuf);
        scene.visLightShadow[lightIndex - 4].drawSurfCount = surfData->drawSurfList.current
            - scene.visLight[lightIndex].drawSurfs;
    }
    else
    {
        scene.visLightShadow[lightIndex - 4].drawSurfCount = 0;
    }
    if (surfCounts[1])
    {
        scene.visLightShadow[lightIndex].drawSurfCount = surfCounts[1];
        surfData[1].drawSurfList.current = drawSurfs[1];
        surfData[1].drawSurfList.end = (GfxDrawSurf *)((char *)scene.cookie + 8200 * lightIndex);
        //std::_Sort<int *, int, bool(__cdecl *)(int, int)>(
        //    (const GfxStaticModelDrawInst **)surfaces[1],
        //    (const GfxStaticModelDrawInst **)&surfaces[1][surfCounts[1]],
        //    (signed int)(4 * surfCounts[1]) >> 2,
        //    (bool(__cdecl *)(const GfxStaticModelDrawInst *, const GfxStaticModelDrawInst *))R_SortBspShadowReceiverSurfaces);
        std::sort(&surfaces[1][0], &surfaces[1][surfCounts[1]], R_SortBspShadowReceiverSurfaces);
        for (listSurfIndex = 0; listSurfIndex < surfCounts[1]; ++listSurfIndex)
        {
            if (listSurfIndex >= rgp.world->surfaceCount)
                MyAssertHandler(
                    ".\\r_light.cpp",
                    585,
                    0,
                    "listSurfIndex doesn't index rgp.world->surfaceCount\n\t%i not in [0, %i)",
                    listSurfIndex,
                    rgp.world->surfaceCount);
            surfIndex = surfaces[1][listSurfIndex] - rgp.world->dpvs.surfaces;
            triSurfList[0] = surfIndex;
            R_AddBspDrawSurfs(surfaceMaterials[surfIndex], (uint8_t *)triSurfList, 1u, surfData + 1);
        }
        R_EndCmdBuf(&surfData[1].delayedCmdBuf);
        scene.visLightShadow[lightIndex].drawSurfCount = surfData[1].drawSurfList.current
            - scene.visLightShadow[lightIndex].drawSurfs;
    }
    else
    {
        scene.visLightShadow[lightIndex].drawSurfCount = 0;
    }
}

int __cdecl R_AllowBspSpotLightShadows(int surfIndex, void *bspLightCallbackAsVoid)
{
    if (r_spotLightShadows->current.enabled)
        return R_BoxInPlanes(
            (const float (*)[4])((uint32_t)bspLightCallbackAsVoid + 4),
            rgp.world->dpvs.surfaces[surfIndex].bounds[0],
            rgp.world->dpvs.surfaces[surfIndex].bounds[1]);
    else
        return 0;
}

int __cdecl R_BoxInPlanes(const float (*planes)[4], const float *mins, const float *maxs)
{
    return kisak::dynamic_lights::BoxInPlanes(planes, mins, maxs);
}

int __cdecl R_AllowBspSpotLight(int surfIndex, void *bspLightCallbackAsVoid)
{
    if (*(_BYTE *)(*(uint32_t *)bspLightCallbackAsVoid + surfIndex))
        return R_BoxInPlanes(
            (const float (*)[4])((uint32_t)bspLightCallbackAsVoid + 4),
            rgp.world->dpvs.surfaces[surfIndex].bounds[0],
            rgp.world->dpvs.surfaces[surfIndex].bounds[1]);
    else
        return 0;
}

void __cdecl R_CalcSpotLightPlanes(const GfxLight *light, float (*planes)[4])
{
    kisak::dynamic_lights::SpotPlanes(*light, scene.dynamicSpotLightNearPlaneOffset, planes);
}

void __cdecl R_CalcPlaneFromPointDir(float *plane, const float *origin, const float *dir)
{
    *plane = *dir;
    plane[1] = dir[1];
    plane[2] = dir[2];
    plane[3] = -Vec3Dot(origin, dir);
}

void __cdecl R_ComputeSpotLightCrossDirs(const GfxLight *light, float (*crossDirs)[3])
{
    kisak::dynamic_lights::SpotCrossDirs(*light, crossDirs);
}

void __cdecl R_CalcPlaneFromCosSinPointDirs(
    float *plane,
    float fCos,
    float fSin,
    const float *origin,
    const float *forward,
    const float *lateral)
{
    Vec3ScaleMad(fCos, lateral, fSin, forward, plane);
    plane[3] = -Vec3Dot(plane, origin);
}

void __cdecl R_GetStaticModelLightSurfs(const GfxLight **visibleLights, int visibleCount)
{
    const GfxStaticModelDrawInst* smodelDrawInst; // [esp+18h] [ebp-89Ch]
    GfxDrawSurf drawSurf; // [esp+1Ch] [ebp-898h]
    float mins[3]; // [esp+28h] [ebp-88Ch] BYREF
    uint32_t surfaceIndex; // [esp+34h] [ebp-880h]
    const Material* material; // [esp+38h] [ebp-87Ch]
    GfxBspDrawSurfData shadowSurfData; // [esp+3Ch] [ebp-878h] BYREF
    uint32_t surfaceCount; // [esp+58h] [ebp-85Ch]
    const GfxLight* light; // [esp+5Ch] [ebp-858h]
    GfxStaticModelId staticModelId; // [esp+60h] [ebp-854h]
    Material* const* pMaterial; // [esp+64h] [ebp-850h]
    uint16_t list[2]; // [esp+68h] [ebp-84Ch] BYREF
    uint32_t* lodData; // [esp+6Ch] [ebp-848h]
    int lod; // [esp+70h] [ebp-844h]
    uint16_t smodels[1024]; // [esp+74h] [ebp-840h] BYREF
    float maxs[3]; // [esp+878h] [ebp-3Ch] BYREF
    int smodelCount; // [esp+884h] [ebp-30h]
    XSurface* surfaces; // [esp+888h] [ebp-2Ch] BYREF
    int index; // [esp+88Ch] [ebp-28h]
    int lightIndex; // [esp+890h] [ebp-24h]
    GfxBspDrawSurfData surfData; // [esp+894h] [ebp-20h] BYREF
    int smodelIndex; // [esp+8B0h] [ebp-4h]
    //int savedregs; // [esp+8B4h] [ebp+0h] BYREF

    iassert( visibleCount );
    R_InitBspDrawSurf(&surfData);
    R_InitBspDrawSurf(&shadowSurfData);
    g_staticModelLightCallback.smodelVisData = rgp.world->dpvs.smodelVisData[0];
    lodData = rgp.world->dpvs.lodData;
    for (lightIndex = 0; lightIndex < visibleCount; ++lightIndex)
    {
        light = visibleLights[lightIndex];

        iassert(light->type == GFX_LIGHT_TYPE_OMNI || light->type == GFX_LIGHT_TYPE_SPOT);

        mins[0] = light->origin[0] - light->radius;
        mins[1] = light->origin[1] - light->radius;
        mins[2] = light->origin[2] - light->radius;

        maxs[0] = light->origin[0] + light->radius;
        maxs[1] = light->origin[1] + light->radius;
        maxs[2] = light->origin[2] + light->radius;

        surfData.drawSurfList.current = &scene.visLight[lightIndex].drawSurfs[scene.visLightShadow[lightIndex - 4].drawSurfCount];
        surfData.drawSurfList.end = (GfxDrawSurf*)&scene.visLightShadow[lightIndex - 3];

        if (light->type == GFX_LIGHT_TYPE_OMNI)
        {
            g_staticModelLightCallback.position[0] = light->origin[0];
            g_staticModelLightCallback.position[1] = light->origin[1];
            g_staticModelLightCallback.position[2] = light->origin[2];
            g_staticModelLightCallback.radiusSq = light->radius * light->radius;
            smodelCount = R_BoxStaticModels(mins, maxs, R_AllowStaticModelOmniLight, smodels, 1024);
        }
        else
        {
            shadowSurfData.drawSurfList.current = &scene.visLightShadow[lightIndex].drawSurfs[scene.visLightShadow[lightIndex].drawSurfCount];
            shadowSurfData.drawSurfList.end = (GfxDrawSurf*)((char*)scene.cookie + 8200 * lightIndex);
            R_CalcSpotLightPlanes(light, g_staticModelLightCallback.planes);
            smodelCount = R_BoxStaticModels(mins, maxs, R_AllowStaticModelSpotLight, smodels, 1024);
        }

        for (index = 0; index < smodelCount; ++index)
        {
            smodelIndex = smodels[index];
            smodelDrawInst = &rgp.world->dpvs.smodelDrawInsts[smodelIndex];
            lod = (lodData[smodelIndex >> 4] >> (2 * (smodelIndex & 0xF))) & 3;
            surfaceCount = XModelGetSurfaces(smodelDrawInst->model, &surfaces, lod);
            iassert( surfaceCount );
            staticModelId = R_GetStaticModelId(smodelIndex, lod);
            pMaterial = XModelGetSkins(smodelDrawInst->model, lod);
            iassert( pMaterial );
            list[0] = staticModelId.objectId;
            surfaceIndex = 0;
            while (surfaceIndex < surfaceCount)
            {
                material = *pMaterial;
                iassert( material );
                iassert(rgp.sortedMaterials[material->info.drawSurf.fields.materialSortedIndex] == material);

                if (Material_GetTechnique(material, TECHNIQUE_LIGHT_OMNI))
                {
                    drawSurf = kisak::dynamic_lights::ReceiverDrawSurf(
                        material->info.drawSurf, staticModelId.surfType,
                        material->info.drawSurf.fields.objectId);
                    if (!R_AllocDrawSurf(&surfData.delayedCmdBuf, drawSurf, &surfData.drawSurfList, 3u))
                        break;
                    R_AddDelayedStaticModelDrawSurf(&surfData.delayedCmdBuf, &surfaces[surfaceIndex], (uint8_t*)list, 1u);
                    if (light->type == 2 && r_spotLightShadows->current.enabled && r_spotLightSModelShadows->current.enabled)
                    {
                        if (!R_AllocDrawSurf(&shadowSurfData.delayedCmdBuf, drawSurf, &shadowSurfData.drawSurfList, 3u))
                            break;
                        R_AddDelayedStaticModelDrawSurf(
                            &shadowSurfData.delayedCmdBuf,
                            &surfaces[surfaceIndex],
                            (uint8_t*)list,
                            1u);
                    }
                }
                ++surfaceIndex;
                ++pMaterial;
            }
        }
        R_EndCmdBuf(&surfData.delayedCmdBuf);
        scene.visLightShadow[lightIndex - 4].drawSurfCount = surfData.drawSurfList.current
            - scene.visLight[lightIndex].drawSurfs;
        R_EndCmdBuf(&shadowSurfData.delayedCmdBuf);
        scene.visLightShadow[lightIndex].drawSurfCount = shadowSurfData.drawSurfList.current
            - scene.visLightShadow[lightIndex].drawSurfs;
    }
}

int __cdecl R_AllowStaticModelOmniLight(int smodelIndex)
{
    return g_staticModelLightCallback.smodelVisData[smodelIndex]
        && kisak::dynamic_lights::BoxInSphere(
            g_staticModelLightCallback.position,
            g_staticModelLightCallback.radiusSq,
            rgp.world->dpvs.smodelInsts[smodelIndex].mins,
            rgp.world->dpvs.smodelInsts[smodelIndex].maxs);
}

int __cdecl R_AllowStaticModelSpotLight(int smodelIndex)
{
    if (g_staticModelLightCallback.smodelVisData[smodelIndex])
        return R_BoxInPlanes(
            g_staticModelLightCallback.planes,
            rgp.world->dpvs.smodelInsts[smodelIndex].mins,
            rgp.world->dpvs.smodelInsts[smodelIndex].maxs);
    else
        return 0;
}

void __cdecl R_GetSceneEntLightSurfs(const GfxLight **visibleLights, int visibleCount)
{
    float v2; // [esp+4h] [ebp-208h]
    float v3; // [esp+8h] [ebp-204h]
    float v4; // [esp+Ch] [ebp-200h]
    float v[3]; // [esp+10h] [ebp-1FCh] BYREF
    float diff[3]; // [esp+1Ch] [ebp-1F0h] BYREF
    GfxSceneDynBrush *sceneDynBrush; // [esp+28h] [ebp-1E4h]
    DynEntityPose *dynEntPose; // [esp+2Ch] [ebp-1E0h]
    GfxVisibleLight *visLightShadow; // [esp+30h] [ebp-1DCh]
    GfxSceneModel *sceneModel; // [esp+34h] [ebp-1D8h]
    GfxDrawSurf *drawSurf; // [esp+38h] [ebp-1D4h]
    uint32_t dynEntIndex; // [esp+3Ch] [ebp-1D0h]
    GfxDrawSurf *newDrawSurf; // [esp+40h] [ebp-1CCh]
    float planes[4][6][4]; // [esp+44h] [ebp-1C8h] BYREF
    uint32_t sceneEntCount; // [esp+1C8h] [ebp-44h]
    const GfxLight *light; // [esp+1CCh] [ebp-40h]
    uint32_t visLightDrawSurfCount; // [esp+1D0h] [ebp-3Ch]
    float radius; // [esp+1D4h] [ebp-38h]
    GfxVisibleLight *visLight; // [esp+1D8h] [ebp-34h]
    const DynEntityDef *dynEntDef; // [esp+1DCh] [ebp-30h]
    const float *bounds; // [esp+1E0h] [ebp-2Ch]
    uint32_t visLightShadowDrawSurfCount; // [esp+1E4h] [ebp-28h]
    uint32_t sceneEntIndex; // [esp+1E8h] [ebp-24h]
    GfxSceneEntity *sceneEnt; // [esp+1ECh] [ebp-20h]
    GfxSceneBrush *sceneBrush; // [esp+1F0h] [ebp-1Ch]
    uint8_t *sceneEntVisData; // [esp+1F4h] [ebp-18h]
    int lightIndex; // [esp+1F8h] [ebp-14h]
    GfxSceneDynModel *sceneDynModel; // [esp+1FCh] [ebp-10h]
    float distSq; // [esp+200h] [ebp-Ch]
    const GfxBrushModel *bmodel; // [esp+204h] [ebp-8h]
    GfxDrawSurf *lastDrawSurf; // [esp+208h] [ebp-4h]

    iassert( (visibleCount <= 4) );
    for (lightIndex = 0; lightIndex < visibleCount; ++lightIndex)
    {
        light = visibleLights[lightIndex];
        if (light->type == 2)
            R_CalcSpotLightPlanes(light, planes[lightIndex]);
    }
    sceneEntCount = scene.sceneDObjCount;
    sceneEntVisData = scene.sceneDObjVisData[0];
    for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
    {
        if (sceneEntVisData[sceneEntIndex] == 1)
        {
            sceneEnt = &scene.sceneDObj[sceneEntIndex];
            if (sceneEnt->cull.state < 2)
                MyAssertHandler(
                    ".\\r_light.cpp",
                    895,
                    0,
                    "sceneEnt->cull.state >= CULL_STATE_BOUNDED\n\t%i, %i",
                    sceneEnt->cull.state,
                    2);
            bounds = sceneEnt->cull.mins;
            lightIndex = 0;
        LABEL_16:
            if (lightIndex >= visibleCount)
                continue;
            light = visibleLights[lightIndex];
            if (light->type != 3 && light->type != 2)
                MyAssertHandler(
                    ".\\r_light.cpp",
                    902,
                    1,
                    "%s",
                    "light->type == GFX_LIGHT_TYPE_OMNI || light->type == GFX_LIGHT_TYPE_SPOT");
            if (light->type == 3)
            {
                distSq = PointToBoxDistSq(light->origin, bounds, bounds + 3);
                v4 = light->radius * light->radius;
                if (distSq <= (double)v4)
                    goto LABEL_29;
            }
            else if (!R_SpotLightIsAttachedToDobj(sceneEnt->obj))
            {
                iassert( light->type == GFX_LIGHT_TYPE_SPOT );
                if (R_BoxInPlanes(planes[lightIndex], bounds, bounds + 3)
                    && (frontEndDataOut->gfxEnts[sceneEnt->gfxEntIndex].renderFxFlags & 8) == 0)
                {
                LABEL_29:
                    visLight = &scene.visLightShadow[lightIndex - 4];
                    lastDrawSurf = (GfxDrawSurf *)&visLight[1];
                    visLightDrawSurfCount = visLight->drawSurfCount;
                    drawSurf = &visLight->drawSurfs[visLightDrawSurfCount];
                    newDrawSurf = R_AddDObjSurfaces(sceneEnt, TECHNIQUE_LIGHT_OMNI, drawSurf, (GfxDrawSurf *)&visLight[1]);
                    visLight->drawSurfCount += newDrawSurf - drawSurf;
                    if (light->type == 2
                        && r_spotLightShadows->current.enabled
                        && r_spotLightEntityShadows->current.enabled
                        && (frontEndDataOut->gfxEnts[sceneEnt->gfxEntIndex].renderFxFlags & 1) == 0)
                    {
                        if (lightIndex)
                            MyAssertHandler(
                                ".\\r_light.cpp",
                                938,
                                0,
                                "lightIndex doesn't index MAX_VISIBLE_SHADOWABLE_DLIGHTS\n\t%i not in [0, %i)",
                                lightIndex,
                                1);
                        visLightShadow = &scene.visLightShadow[lightIndex];
                        lastDrawSurf = (GfxDrawSurf *)&visLightShadow[1];
                        visLightShadowDrawSurfCount = visLightShadow->drawSurfCount;
                        drawSurf = &visLightShadow->drawSurfs[visLightShadowDrawSurfCount];
                        newDrawSurf = R_AddDObjSurfaces(sceneEnt, TECHNIQUE_LIGHT_OMNI, drawSurf, (GfxDrawSurf *)&visLightShadow[1]);
                        visLightShadow->drawSurfCount += newDrawSurf - drawSurf;
                    }
                }
            }
            ++lightIndex;
            goto LABEL_16;
        }
    }
    sceneEntCount = scene.sceneModelCount;
    sceneEntVisData = scene.sceneModelVisData[0];
    for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
    {
        if (sceneEntVisData[sceneEntIndex] == 1)
        {
            sceneModel = &scene.sceneModel[sceneEntIndex];
            for (lightIndex = 0; ; ++lightIndex)
            {
                if (lightIndex >= visibleCount)
                    goto LABEL_39;
                light = visibleLights[lightIndex];
                if (light->type != 3 && light->type != 2)
                    MyAssertHandler(
                        ".\\r_light.cpp",
                        963,
                        1,
                        "%s",
                        "light->type == GFX_LIGHT_TYPE_OMNI || light->type == GFX_LIGHT_TYPE_SPOT");
                if (light->type == 3)
                    break;
                iassert( light->type == GFX_LIGHT_TYPE_SPOT );
                if (R_SphereInPlanes(planes[lightIndex], sceneModel->placement.base.origin, sceneModel->radius))
                    goto LABEL_55;
            LABEL_43:
                ;
            }
            Vec3Sub(sceneModel->placement.base.origin, light->origin, diff);
            distSq = Vec3LengthSq(diff);
            radius = light->radius + sceneModel->radius;
            if (distSq > radius * radius)
                goto LABEL_43;
        LABEL_55:
            visLight = &scene.visLightShadow[lightIndex - 4];
            lastDrawSurf = (GfxDrawSurf *)&visLight[1];
            visLightDrawSurfCount = visLight->drawSurfCount;
            drawSurf = &visLight->drawSurfs[visLightDrawSurfCount];
            newDrawSurf = R_AddXModelSurfaces(
                &sceneModel->info,
                sceneModel->model,
                TECHNIQUE_LIGHT_OMNI,
                drawSurf,
                (GfxDrawSurf *)&visLight[1]);
            visLight->drawSurfCount += newDrawSurf - drawSurf;
            if (light->type == 2 && r_spotLightShadows->current.enabled && r_spotLightEntityShadows->current.enabled)
            {
                if (lightIndex)
                    MyAssertHandler(
                        ".\\r_light.cpp",
                        989,
                        0,
                        "lightIndex doesn't index MAX_VISIBLE_SHADOWABLE_DLIGHTS\n\t%i not in [0, %i)",
                        lightIndex,
                        1);
                visLightShadow = &scene.visLightShadow[lightIndex];
                lastDrawSurf = (GfxDrawSurf *)&visLightShadow[1];
                visLightShadowDrawSurfCount = visLightShadow->drawSurfCount;
                drawSurf = &visLightShadow->drawSurfs[visLightShadowDrawSurfCount];
                newDrawSurf = R_AddXModelSurfaces(
                    &sceneModel->info,
                    sceneModel->model,
                    TECHNIQUE_LIGHT_OMNI,
                    drawSurf,
                    (GfxDrawSurf *)&visLightShadow[1]);
                visLightShadow->drawSurfCount += newDrawSurf - drawSurf;
            }
            goto LABEL_43;
        }
    LABEL_39:
        ;
    }
    sceneEntCount = scene.sceneDynModelCount;
    sceneEntVisData = rgp.world->dpvsDyn.dynEntVisData[0][0];
    for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
    {
        sceneDynModel = &rgp.world->sceneDynModel[sceneEntIndex];
        dynEntIndex = sceneDynModel->dynEntId;
        if (sceneEntVisData[dynEntIndex] == 1)
        {
            dynEntPose = DynEnt_GetClientPose(dynEntIndex, DYNENT_DRAW_MODEL);
            for (lightIndex = 0; ; ++lightIndex)
            {
                if (lightIndex >= visibleCount)
                    goto LABEL_64;
                light = visibleLights[lightIndex];
                if (light->type != 3 && light->type != 2)
                    MyAssertHandler(
                        ".\\r_light.cpp",
                        1018,
                        1,
                        "%s",
                        "light->type == GFX_LIGHT_TYPE_OMNI || light->type == GFX_LIGHT_TYPE_SPOT");
                if (light->type == 3)
                    break;
                iassert( light->type == GFX_LIGHT_TYPE_SPOT );
                if (R_SphereInPlanes(planes[lightIndex], dynEntPose->pose.origin, dynEntPose->radius))
                    goto LABEL_80;
            LABEL_68:
                ;
            }
            Vec3Sub(dynEntPose->pose.origin, light->origin, v);
            distSq = Vec3LengthSq(v);
            radius = light->radius + dynEntPose->radius;
            if (distSq > radius * radius)
                goto LABEL_68;
        LABEL_80:
            visLight = &scene.visLightShadow[lightIndex - 4];
            lastDrawSurf = (GfxDrawSurf *)&visLight[1];
            visLightDrawSurfCount = visLight->drawSurfCount;
            drawSurf = &visLight->drawSurfs[visLightDrawSurfCount];
            dynEntDef = DynEnt_GetEntityDef(dynEntIndex, DYNENT_DRAW_MODEL);
            newDrawSurf = R_AddXModelSurfaces(
                &sceneDynModel->info,
                dynEntDef->xModel,
                TECHNIQUE_LIGHT_OMNI,
                drawSurf,
                lastDrawSurf);
            visLight->drawSurfCount += newDrawSurf - drawSurf;
            if (light->type == 2 && r_spotLightShadows->current.enabled && r_spotLightEntityShadows->current.enabled)
            {
                if (lightIndex)
                    MyAssertHandler(
                        ".\\r_light.cpp",
                        1045,
                        0,
                        "lightIndex doesn't index MAX_VISIBLE_SHADOWABLE_DLIGHTS\n\t%i not in [0, %i)",
                        lightIndex,
                        1);
                visLightShadow = &scene.visLightShadow[lightIndex];
                lastDrawSurf = (GfxDrawSurf *)&visLightShadow[1];
                visLightShadowDrawSurfCount = visLightShadow->drawSurfCount;
                drawSurf = &visLightShadow->drawSurfs[visLightShadowDrawSurfCount];
                dynEntDef = DynEnt_GetEntityDef(dynEntIndex, DYNENT_DRAW_MODEL);
                newDrawSurf = R_AddXModelSurfaces(
                    &sceneDynModel->info,
                    dynEntDef->xModel,
                    TECHNIQUE_LIGHT_OMNI,
                    drawSurf,
                    lastDrawSurf);
                visLightShadow->drawSurfCount += newDrawSurf - drawSurf;
            }
            goto LABEL_68;
        }
    LABEL_64:
        ;
    }
    sceneEntCount = scene.sceneBrushCount;
    sceneEntVisData = scene.sceneBrushVisData[0];
    for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
    {
        if (sceneEntVisData[sceneEntIndex] == 1)
        {
            sceneBrush = &scene.sceneBrush[sceneEntIndex];
            bmodel = sceneBrush->bmodel;
            for (lightIndex = 0; ; ++lightIndex)
            {
                if (lightIndex >= visibleCount)
                    goto LABEL_89;
                light = visibleLights[lightIndex];
                if (light->type != 3 && light->type != 2)
                    MyAssertHandler(
                        ".\\r_light.cpp",
                        1074,
                        1,
                        "%s",
                        "light->type == GFX_LIGHT_TYPE_OMNI || light->type == GFX_LIGHT_TYPE_SPOT");
                if (light->type == 3)
                    break;
                iassert( light->type == GFX_LIGHT_TYPE_SPOT );
                if (R_BoxInPlanes(planes[lightIndex], bmodel->writable.mins, bmodel->writable.maxs))
                    goto LABEL_105;
            LABEL_93:
                ;
            }
            distSq = PointToBoxDistSq(light->origin, bmodel->writable.mins, bmodel->writable.maxs);
            v3 = light->radius * light->radius;
            if (distSq > (double)v3)
                goto LABEL_93;
        LABEL_105:
            visLight = &scene.visLightShadow[lightIndex - 4];
            lastDrawSurf = (GfxDrawSurf *)&visLight[1];
            visLightDrawSurfCount = visLight->drawSurfCount;
            drawSurf = &visLight->drawSurfs[visLightDrawSurfCount];
            newDrawSurf = R_AddBModelSurfaces(
                &sceneBrush->info,
                sceneBrush->bmodel,
                TECHNIQUE_LIGHT_OMNI,
                drawSurf,
                (GfxDrawSurf *)&visLight[1]);
            visLight->drawSurfCount += newDrawSurf - drawSurf;
            if (light->type == 2 && r_spotLightShadows->current.enabled && r_spotLightEntityShadows->current.enabled)
            {
                if (lightIndex)
                    MyAssertHandler(
                        ".\\r_light.cpp",
                        1099,
                        0,
                        "lightIndex doesn't index MAX_VISIBLE_SHADOWABLE_DLIGHTS\n\t%i not in [0, %i)",
                        lightIndex,
                        1);
                visLightShadow = &scene.visLightShadow[lightIndex];
                lastDrawSurf = (GfxDrawSurf *)&visLightShadow[1];
                visLightShadowDrawSurfCount = visLightShadow->drawSurfCount;
                drawSurf = &visLightShadow->drawSurfs[visLightShadowDrawSurfCount];
                newDrawSurf = R_AddBModelSurfaces(
                    &sceneBrush->info,
                    sceneBrush->bmodel,
                    TECHNIQUE_LIGHT_OMNI,
                    drawSurf,
                    (GfxDrawSurf *)&visLightShadow[1]);
                visLightShadow->drawSurfCount += newDrawSurf - drawSurf;
            }
            goto LABEL_93;
        }
    LABEL_89:
        ;
    }
    sceneEntCount = scene.sceneDynBrushCount;
    sceneEntVisData = rgp.world->dpvsDyn.dynEntVisData[1][0];
    for (sceneEntIndex = 0; sceneEntIndex < sceneEntCount; ++sceneEntIndex)
    {
        sceneDynBrush = &rgp.world->sceneDynBrush[sceneEntIndex];
        dynEntIndex = sceneDynBrush->dynEntId;
        if (sceneEntVisData[dynEntIndex] == 1)
        {
            dynEntDef = DynEnt_GetEntityDef(dynEntIndex, DYNENT_DRAW_BRUSH);
            bmodel = R_GetBrushModel(dynEntDef->brushModel);
            for (lightIndex = 0; ; ++lightIndex)
            {
                if (lightIndex >= visibleCount)
                    goto LABEL_114;
                light = visibleLights[lightIndex];
                if (light->type != 3 && light->type != 2)
                    MyAssertHandler(
                        ".\\r_light.cpp",
                        1129,
                        1,
                        "%s",
                        "light->type == GFX_LIGHT_TYPE_OMNI || light->type == GFX_LIGHT_TYPE_SPOT");
                if (light->type == 3)
                    break;
                iassert( light->type == GFX_LIGHT_TYPE_SPOT );
                if (R_BoxInPlanes(planes[lightIndex], bmodel->writable.mins, bmodel->writable.maxs))
                    goto LABEL_130;
            LABEL_118:
                ;
            }
            distSq = PointToBoxDistSq(light->origin, bmodel->writable.mins, bmodel->writable.maxs);
            v2 = light->radius * light->radius;
            if (distSq > (double)v2)
                goto LABEL_118;
        LABEL_130:
            visLight = &scene.visLightShadow[lightIndex - 4];
            lastDrawSurf = (GfxDrawSurf *)&visLight[1];
            visLightDrawSurfCount = visLight->drawSurfCount;
            drawSurf = &visLight->drawSurfs[visLightDrawSurfCount];
            newDrawSurf = R_AddBModelSurfaces(
                (BModelDrawInfo *)sceneDynBrush,
                bmodel,
                TECHNIQUE_LIGHT_OMNI,
                drawSurf,
                (GfxDrawSurf *)&visLight[1]);
            visLight->drawSurfCount += newDrawSurf - drawSurf;
            if (light->type == 2 && r_spotLightShadows->current.enabled && r_spotLightEntityShadows->current.enabled)
            {
                if (lightIndex)
                    MyAssertHandler(
                        ".\\r_light.cpp",
                        1155,
                        0,
                        "lightIndex doesn't index MAX_VISIBLE_SHADOWABLE_DLIGHTS\n\t%i not in [0, %i)",
                        lightIndex,
                        1);
                visLightShadow = &scene.visLightShadow[lightIndex];
                lastDrawSurf = (GfxDrawSurf *)&visLightShadow[1];
                visLightShadowDrawSurfCount = visLightShadow->drawSurfCount;
                drawSurf = &visLightShadow->drawSurfs[visLightShadowDrawSurfCount];
                newDrawSurf = R_AddBModelSurfaces(
                    (BModelDrawInfo *)sceneDynBrush,
                    bmodel,
                    TECHNIQUE_LIGHT_OMNI,
                    drawSurf,
                    (GfxDrawSurf *)&visLightShadow[1]);
                visLightShadow->drawSurfCount += newDrawSurf - drawSurf;
            }
            goto LABEL_118;
        }
    LABEL_114:
        ;
    }
}

int __cdecl R_SphereInPlanes(const float (*planes)[4], const float *center, float radius)
{
    return kisak::dynamic_lights::SphereInPlanes(planes, center, radius);
}

// KISAKTODO: unfk
void __cdecl ShortSortArray_GfxReverseSortDrawSurfsInterface_GfxDrawSurf_(GfxDrawSurf *lo, GfxDrawSurf *hi)
{
    int packed_high; // edx
    unsigned __int64 v3; // [esp+4h] [ebp-34h]
    unsigned __int64 packed; // [esp+Ch] [ebp-2Ch]
    GfxDrawSurf *max; // [esp+1Ch] [ebp-1Ch]
    unsigned __int64 maxKey; // [esp+20h] [ebp-18h]
    GfxDrawSurf *walk; // [esp+34h] [ebp-4h]

    while (hi > lo)
    {
        max = lo;
        LODWORD(maxKey) = LODWORD(lo->fields);
        HIDWORD(maxKey) = ((~((lo->packed >> 54) & 0x3F) & 0x3F) << 22) | HIDWORD(lo->packed) & 0xF03FFFFF;
        for (walk = lo + 1; walk <= hi; ++walk)
        {
            packed = walk->packed;
            HIDWORD(packed) = ((~((walk->packed >> 54) & 0x3F) & 0x3F) << 22) | HIDWORD(walk->packed) & 0xF03FFFFF;
            if (maxKey < packed)
            {
                LODWORD(maxKey) = LODWORD(walk->fields);
                HIDWORD(maxKey) = ((~((walk->packed >> 54) & 0x3F) & 0x3F) << 22) | HIDWORD(walk->packed) & 0xF03FFFFF;
                max = walk;
            }
        }
        v3 = max->packed;
        packed_high = HIDWORD(hi->packed);
        //*&max->fields = hi->fields;
        LODWORD(max->packed) = LODWORD(hi->packed);
        HIDWORD(max->packed) = packed_high;
        hi->packed = v3;
        --hi;
    }
}

// KISAKTODO: unfk
void __cdecl qsortArray_GfxReverseSortDrawSurfsInterface_GfxDrawSurf_(GfxDrawSurf *elems, int count)
{
    int packed_high; // edx
    GfxDrawSurf *v3; // eax
    int v4; // eax
    int v5; // ecx
    GfxDrawSurf *v6; // edx
    GfxDrawSurf v7; // [esp+4h] [ebp-180h]
    unsigned __int64 fields; // [esp+Ch] [ebp-178h]
    int v9; // [esp+10h] [ebp-174h]
    GfxDrawSurf v10; // [esp+14h] [ebp-170h]
    GfxDrawSurf v11; // [esp+1Ch] [ebp-168h]
    GfxDrawSurf v12; // [esp+2Ch] [ebp-158h]
    unsigned __int64 pivotKey; // [esp+64h] [ebp-120h]
    GfxDrawSurf *loWalk; // [esp+74h] [ebp-110h]
    int sortCount; // [esp+78h] [ebp-10Ch]
    GfxDrawSurf *hiEnd; // [esp+7Ch] [ebp-108h]
    GfxDrawSurf *hiWalk; // [esp+80h] [ebp-104h]
    GfxDrawSurf *loStack[30]; // [esp+84h] [ebp-100h]
    GfxDrawSurf *hiStack[30]; // [esp+FCh] [ebp-88h]
    int stackPos; // [esp+178h] [ebp-Ch]
    GfxDrawSurf *loEnd; // [esp+17Ch] [ebp-8h]
    GfxDrawSurf *mid; // [esp+180h] [ebp-4h]

    if (count >= 2)
    {
        stackPos = 0;
        loEnd = elems;
        hiEnd = &elems[count - 1];
        while (1)
        {
            while (1)
            {
                sortCount = hiEnd - loEnd + 1;
                if (sortCount <= 8)
                {
//                    ShortSortArray<GfxReverseSortDrawSurfsInterface, GfxDrawSurf>(loEnd, hiEnd);
                    ShortSortArray_GfxReverseSortDrawSurfsInterface_GfxDrawSurf_(loEnd, hiEnd);
                    goto LABEL_22;
                }
                mid = &loEnd[sortCount / 2];
                v12.fields = mid->fields;
                packed_high = HIDWORD(loEnd->packed);
                v3 = mid;
                *&mid->fields = loEnd->fields;
                HIDWORD(v3->packed) = packed_high;
                loEnd->fields = v12.fields;
                loWalk = loEnd;
                hiWalk = hiEnd + 1;
                LODWORD(pivotKey) = loEnd->packed;
                HIDWORD(pivotKey) = ((~((loEnd->packed >> 54) & 0x3F) & 0x3F) << 22) | HIDWORD(loEnd->packed) & 0xF03FFFFF;
                while (1)
                {
                    do
                    {
                        if (++loWalk > hiEnd)
                            break;
                        v11.fields = loWalk->fields;
                        HIDWORD(v11.packed) = ((~((loWalk->packed >> 54) & 0x3F) & 0x3F) << 22)
                            | HIDWORD(loWalk->packed) & 0xF03FFFFF;
                    } while (v11.packed <= pivotKey);
                    do
                    {
                        if (loEnd >= --hiWalk)
                            break;
                        v10.fields = hiWalk->fields;
                        HIDWORD(v10.packed) = ((~((hiWalk->packed >> 54) & 0x3F) & 0x3F) << 22)
                            | HIDWORD(hiWalk->packed) & 0xF03FFFFF;
                    } while (pivotKey <= v10.packed);
                    if (hiWalk < loWalk)
                        break;
                    fields = loWalk->packed;
                    v9 = HIDWORD(loWalk->packed);
                    v4 = HIDWORD(hiWalk->packed);
                    *&loWalk->fields = hiWalk->fields;
                    HIDWORD(loWalk->packed) = v4;
                    *&hiWalk->packed = fields;
                    HIDWORD(hiWalk->packed) = v9;
                }
                v7.fields = loEnd->fields;
                v5 = HIDWORD(hiWalk->packed);
                v6 = loEnd;
                *&loEnd->fields = hiWalk->fields;
                HIDWORD(v6->packed) = v5;
                hiWalk->fields = v7.fields;
                if (&hiWalk[-1] - loEnd >= hiEnd - loWalk)
                    break;
                if (loWalk < hiEnd)
                {
                    loStack[stackPos] = loWalk;
                    hiStack[stackPos++] = hiEnd;
                }
                if (loEnd >= &hiWalk[-1])
                {
                LABEL_22:
                    if (--stackPos < 0)
                        return;
                    loEnd = loStack[stackPos];
                    hiEnd = hiStack[stackPos];
                }
                else
                {
                    hiEnd = hiWalk - 1;
                }
            }
            if (loEnd <= hiWalk)
            {
                loStack[stackPos] = loEnd;
                hiStack[stackPos++] = hiWalk - 1;
            }
            if (loWalk >= hiEnd)
                goto LABEL_22;
            loEnd = loWalk;
        }
    }
}


void __cdecl R_ReverseSortDrawSurfs(GfxDrawSurf *drawSurfList, int surfCount)
{
    PROF_SCOPED("R_SortDrawSurfs");
    //qsortArray<GfxReverseSortDrawSurfsInterface, GfxDrawSurf>(drawSurfList, surfCount);
    qsortArray_GfxReverseSortDrawSurfsInterface_GfxDrawSurf_(drawSurfList, surfCount);
}

int __cdecl R_EmitPointLightPartitionSurfs(
    GfxViewInfo *viewInfo,
    const GfxLight **visibleLights,
    int visibleCount,
    const float *viewOrigin)
{
    int firstDrawSurf; // [esp+Ch] [ebp-28h]
    PointLightPartition *partitions; // [esp+10h] [ebp-24h]
    const GfxLight *light; // [esp+18h] [ebp-1Ch]
    int partitionCount; // [esp+20h] [ebp-14h]
    uint32_t lightDrawSurfCount; // [esp+24h] [ebp-10h]
    int lightIndex; // [esp+28h] [ebp-Ch]
    PointLightPartition *partition; // [esp+2Ch] [ebp-8h]
    int drawSurfCount; // [esp+30h] [ebp-4h]

    partitions = viewInfo->pointLightPartitions;
    partitionCount = 0;
    for (lightIndex = 0; lightIndex < visibleCount; ++lightIndex)
    {
        light = visibleLights[lightIndex];
        lightDrawSurfCount = scene.visLightShadow[lightIndex - 4].drawSurfCount;
        R_ReverseSortDrawSurfs(scene.visLight[lightIndex].drawSurfs, lightDrawSurfCount);
        partition = &partitions[partitionCount];
        R_InitDrawSurfListInfo(&partition->info);
        partition->info.baseTechType = (MaterialTechniqueType)R_GetTechniqueForLightType(light, viewInfo);
        partition->info.viewInfo = viewInfo;
        partition->info.viewOrigin[0] = *viewOrigin;
        partition->info.viewOrigin[1] = viewOrigin[1];
        partition->info.viewOrigin[2] = viewOrigin[2];
        partition->info.viewOrigin[3] = viewOrigin[3];
        partition->info.light = &partition->light;
        partition->info.cameraView = 1;
        firstDrawSurf = frontEndDataOut->drawSurfCount;
        R_EmitDrawSurfList(scene.visLight[lightIndex].drawSurfs, lightDrawSurfCount);
        drawSurfCount = frontEndDataOut->drawSurfCount - firstDrawSurf;
        if (drawSurfCount)
        {
            memcpy(partition, light, 0x40u);
            partition->info.drawSurfs = &frontEndDataOut->drawSurfs[firstDrawSurf];
            partitions[partitionCount++].info.drawSurfCount = drawSurfCount;
        }
    }
    return partitionCount;
}

int __cdecl R_GetTechniqueForLightType(const GfxLight *light, const GfxViewInfo *viewInfo)
{
    const char *v3; // eax

    iassert( viewInfo );
    if (light->type == 2)
    {
        if (!r_spotLightShadows->current.enabled)
            return 21;
        iassert( comWorld.isInUse );
        if (Com_BitCheckAssert(frontEndDataOut->shadowableLightHasShadowMap, comWorld.primaryLightCount, 32))
        {
            iassert( viewInfo->emissiveSpotLightCount == 1 );
            iassert( comWorld.isInUse );
            if (comWorld.primaryLightCount + 1 != viewInfo->shadowableLightCount)
                MyAssertHandler(
                    ".\\r_light.cpp",
                    1182,
                    0,
                    "%s",
                    "Com_GetPrimaryLightCount() + GFX_MAX_EMISSIVE_SPOT_LIGHTS == viewInfo->shadowableLightCount");
            return 23;
        }
        else
        {
            return 21;
        }
    }
    else if (light->type == 3)
    {
        return 22;
    }
    else
    {
        if (!alwaysfails)
        {
            v3 = va("Dynamic light type %d isn't supported.", light->type);
            MyAssertHandler(".\\r_light.cpp", 1195, 0, v3);
        }
        return 22;
    }
}

void __cdecl R_EmitShadowedLightPartitionSurfs(
    GfxViewInfo *viewInfo,
    uint32_t lightDrawSurfCount,
    GfxDrawSurf *lightDrawSurfs,
    GfxDrawSurfListInfo *info)
{
    int firstDrawSurf; // [esp+0h] [ebp-8h]
    uint32_t drawSurfCount; // [esp+4h] [ebp-4h]

    firstDrawSurf = frontEndDataOut->drawSurfCount;
    R_EmitDrawSurfList(lightDrawSurfs, lightDrawSurfCount);
    drawSurfCount = frontEndDataOut->drawSurfCount - firstDrawSurf;
    info->drawSurfs = &frontEndDataOut->drawSurfs[firstDrawSurf];
    info->drawSurfCount = drawSurfCount;
}

