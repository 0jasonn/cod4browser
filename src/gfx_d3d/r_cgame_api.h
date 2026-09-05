#pragma once

#include <cstdint>
#include <gfx_d3d/gfx_placement_types.h>
#include <gfx_d3d/r_scene_api.h>

struct DObj_s;
struct FxCmd;
struct GfxBrushModel;
struct GfxLight;
struct Material;
struct SunLightParseParams;
struct XModel;
struct cpose_t;
struct refdef_s;
struct Font_s;

struct GfxSkinnedXModelSurfs
{
    void *firstSurf;
};

struct GfxSceneEntityCull
{
    volatile std::uint32_t state;
    float mins[3];
    float maxs[3];
    char lods[32];
    GfxSkinnedXModelSurfs skinnedSurfs;
};

union GfxSceneEntityInfo
{
    cpose_t *pose;
    std::uint16_t *cachedLightingHandle;
};

struct GfxSceneEntity
{
    float lightingOrigin[3];
    GfxScaledPlacement placement;
    GfxSceneEntityCull cull;
    std::uint16_t gfxEntIndex;
    std::uint16_t entnum;
    const DObj_s *obj;
    GfxSceneEntityInfo info;
    std::uint8_t reflectionProbeIndex;
};

GfxBrushModel *__cdecl R_GetBrushModel(std::uint32_t modelIndex);
void __cdecl R_AddBrushModelToSceneFromAngles(const GfxBrushModel *bmodel,
    const float *origin, const float *angles, std::uint16_t entnum);
void __cdecl R_AddDObjToScene(const DObj_s *obj, const cpose_t *pose,
    std::uint32_t entnum, std::uint32_t renderFxFlags, float *lightingOrigin,
    float materialTime);
void __cdecl R_LinkDObjEntity(std::uint32_t localClientNum,
    std::uint32_t entnum, float *origin, float radius);
void __cdecl R_LinkBModelEntity(std::uint32_t localClientNum,
    std::uint32_t entnum, GfxBrushModel *bmodel);
void __cdecl R_UpdateXModelBoundsDelayed(GfxSceneEntity *sceneEnt);
void __cdecl R_SkinGfxEntityDelayed(GfxSceneEntity *sceneEnt);
XModel *__cdecl R_RegisterModel(const char *name);
void __cdecl R_ClearScene(std::uint32_t localClientNum);
void __cdecl R_SetLodOrigin(const refdef_s *refdef);
double __cdecl R_GetFarPlaneDist();
void __cdecl R_UpdateSpotLightEffect(FxCmd *cmd);
void __cdecl R_UpdateNonDependentEffects(FxCmd *cmd);
void __cdecl R_UpdateRemainingEffects(FxCmd *cmd);
void R_DObjReplaceMaterial(DObj_s *obj, int lod, int surfaceIndex,
    Material *material);
void R_DObjGetSurfMaterials(DObj_s *obj, int lod,
    Material **matHandleArray);
void R_SetIgnorePrecacheErrors(std::uint32_t ignore);
const char *__cdecl Material_GetName(Material *handle);
Material *__cdecl Material_RegisterHandle(const char *name, int imageTrack);
void __cdecl R_InterpretSunLightParseParams(SunLightParseParams *sunParse);
void R_ResetSunLightParseParams();
void R_SetSunLightOverride(float *sunColor);
void R_ResetSunLightOverride();
void R_SetSunDirectionOverride(float *sunDir);
void R_LerpSunDirectionOverride(float *sunDirBegin, float *sunDirEnd,
    int lerpBeginTime, int lerpEndTime);
void R_ResetSunDirectionOverride();
void __cdecl R_InitPrimaryLights(GfxLight *primaryLights);
void __cdecl R_ClearShadowedPrimaryLightHistory(int localClientNum);
void R_SetCullDist(float dist);
void __cdecl R_SetFogFromServer(float start, std::uint8_t r,
    std::uint8_t g, std::uint8_t b, float density);
void __cdecl R_SwitchFog(std::uint32_t fogvar, int startTime,
    int transitionTime);
void __cdecl R_InitSceneData(int localClientNum);
void __cdecl R_UnlinkEntity(std::uint32_t localClientNum,
    std::uint32_t entnum);
int __cdecl R_PickMaterial(int traceMask, const float *org,
    const float *dir, char *name, char *surfaceFlags, char *contents,
    std::uint32_t charLimit);
int __cdecl R_TextWidth(const char *text, int maxChars, Font_s *font);
