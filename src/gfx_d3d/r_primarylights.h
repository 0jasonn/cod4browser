#pragma once
#include "gfx_world_types.h"
#include "r_rendercmds.h"
#include "r_shadowed_light_history.h"

enum GfxLightType : __int32
{
    GFX_LIGHT_TYPE_NONE = 0x0,
    GFX_LIGHT_TYPE_DIR = 0x1,
    GFX_LIGHT_TYPE_SPOT = 0x2,
    GFX_LIGHT_TYPE_OMNI = 0x3,
    GFX_LIGHT_TYPE_COUNT = 0x4,
    GFX_LIGHT_TYPE_DIR_SHADOWMAP = 0x4,
    GFX_LIGHT_TYPE_SPOT_SHADOWMAP = 0x5,
    GFX_LIGHT_TYPE_OMNI_SHADOWMAP = 0x6,
    GFX_LIGHT_TYPE_COUNT_WITH_SHADOWMAP_VERSIONS = 0x7,
};

struct GfxCandidateShadowedLight // sizeof=0x8
{                                       // ...
    uint32_t shadowableLightIndex;  // ...
    float score;
};

void __cdecl R_ClearShadowedPrimaryLightHistory(int localClientNum);
void __cdecl R_AddDynamicShadowableLight(GfxViewInfo *viewInfo, const GfxLight *visibleLight);
bool __cdecl R_IsDynamicShadowedLight(uint32_t shadowableLightIndex);
bool __cdecl R_IsPrimaryLight(uint32_t shadowableLightIndex);
void __cdecl R_ChooseShadowedLights(GfxViewInfo *viewInfo);
uint32_t __cdecl R_AddPotentiallyShadowedLight(
    const GfxViewInfo *viewInfo,
    uint32_t shadowableLightIndex,
    GfxCandidateShadowedLight *candidateLights,
    uint32_t candidateLightCount);
double __cdecl R_ShadowedSpotLightScore(const GfxViewParms *viewParms, const GfxLight *light);
void __cdecl R_AddShadowsForLight(GfxViewInfo *viewInfo, uint32_t shadowableLightIndex, float spotShadowFade);
void __cdecl R_AddShadowedLightToShadowHistory(
    GfxShadowedLightHistory *shadowHistory,
    uint32_t shadowableLightIndex,
    float fadeDelta);
void __cdecl R_FadeOutShadowHistoryEntries(GfxShadowedLightHistory *shadowHistory, float fadeDelta);
void __cdecl R_LinkSphereEntityToPrimaryLights(
    uint32_t localClientNum,
    uint32_t entityNum,
    const float *origin,
    float radius);
uint32_t __cdecl R_GetPrimaryLightEntityShadowBit(
    uint32_t localClientNum,
    uint32_t entnum,
    uint32_t primaryLightIndex);
void __cdecl R_LinkBoxEntityToPrimaryLights(
    uint32_t localClientNum,
    uint32_t entityNum,
    const float *mins,
    const float *maxs);
char __cdecl R_CullBoxFromLightRegionHull(
    const GfxLightRegionHull *hull,
    const float *boxMidPoint,
    const float *boxHalfSize);
void __cdecl R_LinkDynEntToPrimaryLights(
    uint32_t dynEntId,
    DynEntityDrawType drawType,
    const float *mins,
    const float *maxs);
bool __cdecl Com_CullBoxFromPrimaryLight(
    const struct ComPrimaryLight *light,
    const float *boxMidPoint,
    const float *boxHalfSize);
uint32_t __cdecl R_GetPrimaryLightDynEntShadowBit(uint32_t entnum, uint32_t primaryLightIndex);
void __cdecl R_UnlinkEntityFromPrimaryLights(uint32_t localClientNum, uint32_t entityNum);
void __cdecl R_UnlinkDynEntFromPrimaryLights(uint32_t dynEntId, DynEntityDrawType drawType);
bool __cdecl R_IsEntityVisibleToPrimaryLight(
    uint32_t localClientNum,
    uint32_t entityNum,
    uint32_t primaryLightIndex);
bool __cdecl R_IsDynEntVisibleToPrimaryLight(
    uint32_t dynEntId,
    DynEntityDrawType drawType,
    uint32_t primaryLightIndex);
int __cdecl R_IsEntityVisibleToAnyShadowedPrimaryLight(const GfxViewInfo *viewInfo, uint32_t entityNum);
bool __cdecl R_IsEntityVisibleToShadowedPrimaryLight(uint32_t baseBitIndex, uint32_t shadowableLightIndex);
int __cdecl R_IsDynEntVisibleToAnyShadowedPrimaryLight(
    const GfxViewInfo *viewInfo,
    uint32_t dynEntId,
    DynEntityDrawType drawType);
bool __cdecl R_IsDynEntVisibleToShadowedPrimaryLight(
    uint32_t baseBitIndex,
    DynEntityDrawType drawType,
    uint32_t shadowableLightIndex);
uint32_t __cdecl R_GetNonSunPrimaryLightForBox(
    const GfxViewInfo *viewInfo,
    const float *boxMidPoint,
    const float *boxHalfSize);
uint32_t __cdecl R_GetNonSunPrimaryLightForSphere(const GfxViewInfo *viewInfo, const float *origin, float radius);
char __cdecl R_CullSphereFromLightRegionHull(const GfxLightRegionHull *hull, const float *origin, float radius);
bool __cdecl Com_CullSphereFromPrimaryLight(const struct ComPrimaryLight *light, const float *origin, float radius);
