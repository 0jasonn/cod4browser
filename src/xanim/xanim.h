#pragma once

// LWSS: This file has way too many structs. KISAKTODO: move out later.
#include <database/db_asset_types.h>
#include <database/db_file_types.h>
#include <database/db_registry_types.h>
#include <physics/phys_preset.h>
#include <qcommon/cm_types.h>
#include <xanim/xanim_types.h>
#include "xanim_public.h"
#include <script/scr_stringlist.h>
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/r_bsp.h>
#include <universal/com_math.h>
#include <bgame/bg_weapons.h>
#include <bgame/weapon_types.h>
#include <xanim/xanim.h>
#include <sound/snd_public.h>
#include <qcommon/com_world_types.h>
#include "dobj.h"
#include "xmodel.h"
#include <gfx_d3d/r_material.h>
#include <gfx_d3d/r_gfx.h>
#include <xanim/xsurface_types.h>

#ifndef KISAK_RADIANT
#include <game/pathnode.h>
#endif

#include <ui/ui_shared.h>

#define ANIM_FLAG_COMPLETE 1

struct XModelNameMap // sizeof=0x4
{                                       // ...
    uint16_t name;              // ...
    uint16_t index;
};

struct XAnimParent // sizeof=0x4
{                                       // ...
    uint16_t flags;
    uint16_t children;
};
struct XAnimEntry // sizeof=0x8
{                                       // ...
    uint16_t numAnims;
    uint16_t parent;
    union //$7F333398CC08E12E110886895274CBFC
    {
        XAnimParts *parts;
        XAnimParent animParent;
    };
};
struct XAnim_s // sizeof=0x14
{
    const char *debugName;
    uint32_t size;
    const char **debugAnimNames;
    XAnimEntry entries[1];
};

struct XAnimTree_s // sizeof=0x14
{
    XAnim_s *anims;
    int info_usage;
    volatile long calcRefCount;
    volatile long modifyRefCount;
    uint16_t children;
    // padding byte
    // padding byte
};
struct mnode_t // sizeof=0x4
{
    uint16_t cellIndex;
    uint16_t rightChildOffset;
};

struct XAnimState // sizeof=0x20
{                                       // ...
    float currentAnimTime;              // ...
    float oldTime;                      // ...
    __int16 cycleCount;                 // ...
    __int16 oldCycleCount;              // ...
    float goalTime;                     // ...
    float goalWeight;                   // ...
    float weight;                       // ...
    float rate;                         // ...
    bool instantWeightChange;           // ...
    // padding byte
    // padding byte
    // padding byte
};

struct XAnimInfo // sizeof=0x40
{                                       // ...
    uint16_t notifyChild;
    __int16 notifyIndex;
    uint16_t notifyName;
    uint16_t notifyType;
    uint16_t prev;              // ...
    uint16_t next;              // ...
    uint16_t children;          // ...
    uint16_t parent;            // ...
    uint16_t animIndex;         // ...
    uint16_t animToModel;
    bool inuse;                         // ...
    // padding byte
    // padding byte
    // padding byte
    XAnimTree_s* tree;
    //$7F333398CC08E12E110886895274CBFC ___u12;
    union
    {                                       // ...
        XAnimParts* parts;
        XAnimParent animParent;
    };
    XAnimState state;                   // ...
};

struct XAnimSimpleRotPos // sizeof=0x18
{                                       // ...
    float rot[2];                       // ...
    float posWeight;                    // ...
    float pos[3];                       // ...
};

struct XAnimDeltaInfo // sizeof=0x4
{                                       // ...
    bool bClear;                        // ...
    bool bNormQuat;                     // ...
    bool bAbs;                          // ...
    bool bUseGoalWeight;                // ...
};

struct XAnimNotify_s // sizeof=0xC
{                                       // ...
    const char* name;
    uint32_t type;
    float timeFrac;
};

struct XModelDrawInfo // sizeof=0x4
{                                       // ...
    uint16_t lod;
    uint16_t surfId;
};
struct GfxSceneDynModel // sizeof=0x6
{
    XModelDrawInfo info;
    uint16_t dynEntId;
};
struct BModelDrawInfo // sizeof=0x2
{                                       // ...
    uint16_t surfId;
};
struct GfxSceneDynBrush // sizeof=0x4
{
    BModelDrawInfo info;
    uint16_t dynEntId;
};

struct SndDriverGlobals // sizeof=0x4
{                                       // ...
    const char* name;
};

extern "C" {
    // win32
    struct _OVERLAPPED;
}

union XAssetSize // sizeof=0x878
{                                       // ...
    XAssetSize()
    {
        fx = NULL;
    }
    XAnimParts parts;
    XModel model;
    Material material;
    MaterialPixelShader pixelShader;
    MaterialVertexShader vertexShader;
    MaterialTechniqueSet techniqueSet;
    GfxImage image;
    snd_alias_list_t sound;
    SndCurve sndCurve;
    clipMap_t clipMap;
    ComWorld comWorld;
    MapEnts mapEnts;
    GfxWorld gfxWorld;
    GfxLightDef lightDef;
    Font_s font;
    MenuList menuList;
    menuDef_t menu;
    LocalizeEntry localize;
    WeaponDef weapon;
    SndDriverGlobals sndDriverGlobals;
    const FxEffectDef *fx;
    FxImpactTable impactFx;
    RawFile rawfile;
    StringTable stringTable;
};

template <typename T>
union XAssetPoolEntry // sizeof=0x10
{                                       // ...
    XAssetPoolEntry()
    {
        next = NULL;
    }
    T entry;
    XAssetPoolEntry<T> *next;
};

template <typename T, int LEN>
struct XAssetPool
{
    XAssetPoolEntry<T> *freeHead;
    XAssetPoolEntry<T> entries[LEN];
};

struct DObj_s;

struct gentity_s;

int __cdecl XAnimGetTreeHighMemUsage();
int __cdecl XAnimGetTreeMemUsage();
void __cdecl TRACK_xanim();
int __cdecl XAnimGetTreeMaxMemUsage();
XAnimInfo *XAnimAllocInfo(DObj_s *obj, uint32_t animIndex, int after);
void __cdecl XAnimInit();
void __cdecl XAnimShutdown();
XAnimParts* __cdecl XAnimFindData_LoadObj(const char* name);
XAnimParts* __cdecl XAnimFindData_FastFile(const char* name);
void __cdecl XAnimCreate(XAnim_s* anims, uint32_t animIndex, const char* name);
XAnimParts *__cdecl XAnimPrecache(const char *name, void *(__cdecl *Alloc)(int));
void __cdecl XAnimBlend(
    XAnim_s* anims,
    uint32_t animIndex,
    const char* name,
    uint32_t children,
    uint32_t num,
    uint32_t flags);
bool __cdecl IsNodeAdditive(const XAnimEntry* node);
bool __cdecl IsLeafNode(const XAnimEntry* anim);
XAnim_s* __cdecl XAnimCreateAnims(const char* debugName, uint32_t size, void* (__cdecl* Alloc)(int));
void __cdecl XAnimFreeList(XAnim_s* anims);
void __cdecl XAnimFree(XAnimParts *parts);
XAnimTree_s* __cdecl XAnimCreateTree(XAnim_s* anims, void* (__cdecl* Alloc)(int));
void __cdecl XAnimFreeTree(XAnimTree_s* tree, void(__cdecl* Free)(void*, int));
void XAnimCheckTreeLeak();
int XAnimGetAssetType(XAnimTree_s *tree, uint32_t index);
XAnim_s* __cdecl XAnimGetAnims(const XAnimTree_s* tree);
bool XAnimIsLeafNode(const XAnim_s *anims, uint32_t animIndex);
void XAnimResetAnimMap(const DObj_s* obj, uint32_t infoIndex);
void __cdecl XAnimInitModelMap(XModel* const* models, uint32_t numModels, XModelNameMap* modelMap);
void __cdecl XAnimResetAnimMap_r(XModelNameMap* modelMap, uint32_t infoIndex);
void __cdecl XAnimResetAnimMapLeaf(const XModelNameMap* modelMap, uint32_t infoIndex);
uint32_t __cdecl XAnimGetAnimMap(const XAnimParts* parts, const XModelNameMap* modelMap);
double __cdecl XAnimGetLength(const XAnim_s* anims, uint32_t animIndex);
int __cdecl XAnimGetLengthMsec(const XAnim_s* anims, uint32_t anim);
double __cdecl XAnimGetTime(const XAnimTree_s* tree, uint32_t animIndex);
uint32_t __cdecl XAnimGetInfoIndex(const XAnimTree_s* tree, uint32_t animIndex);
uint32_t __cdecl XAnimGetInfoIndex_r(const XAnimTree_s* tree, uint32_t animIndex, uint32_t infoIndex);
double __cdecl XAnimGetWeight(const XAnimTree_s* tree, uint32_t animIndex);
bool __cdecl XAnimHasFinished(const XAnimTree_s* tree, uint32_t animIndex);
int __cdecl XAnimGetNumChildren(const XAnim_s* anims, uint32_t animIndex);
uint32_t __cdecl XAnimGetChildAt(const XAnim_s* anims, uint32_t animIndex, uint32_t childIndex);
const char* __cdecl XAnimGetAnimName(const XAnim_s* anims, uint32_t animIndex);
char* __cdecl XAnimGetAnimDebugName(const XAnim_s* anims, uint32_t animIndex);
const char* __cdecl XAnimGetAnimTreeDebugName(const XAnim_s* anims);
uint32_t __cdecl XAnimGetAnimTreeSize(const XAnim_s* anims);
void __cdecl XAnimInitInfo(XAnimInfo* info);
void __cdecl XAnimUpdateOldTime(
    DObj_s* obj,
    uint32_t infoIndex,
    XAnimState* syncState,
    float dtime,
    bool parentHasWeight,
    bool* childHasTimeForParent);
uint32_t __cdecl XAnimInitTime(XAnimTree_s* tree, uint32_t infoIndex, float goalTime);
void __cdecl XAnimResetTime(uint32_t infoIndex);
void __cdecl XAnimResetTimeInternal(uint32_t infoIndex);
uint32_t __cdecl XAnimCloneInitTime(XAnimTree_s* tree, uint32_t infoIndex, uint32_t parentIndex);
void __cdecl DObjInitServerTime(DObj_s* obj, float dtime);
void __cdecl DObjUpdateClientInfo(DObj_s* obj, float dtime, bool notify);
void __cdecl XAnimUpdateTimeAndNotetrack(const DObj_s* obj, uint32_t infoIndex, float dtime, bool bNotify);
void __cdecl XAnimCheckFreeInfo(XAnimTree_s* tree, uint32_t infoIndex, int hasWeight);
void __cdecl XAnimFreeInfo(XAnimTree_s* tree, uint32_t infoIndex);
void __cdecl XAnimClearServerNotify(XAnimInfo* info);
double __cdecl XAnimGetAverageRateFrequency(const XAnimTree_s *tree, uint32_t infoIndex);
void __cdecl XAnimUpdateTimeAndNotetrackLeaf(
    const DObj_s* obj,
    const XAnimParts* parts,
    uint32_t infoIndex,
    float dtime,
    bool bNotify);
void __cdecl XAnimProcessClientNotify(XAnimInfo* info, float dtime);
uint16_t __cdecl XAnimGetNextNotifyIndex(const XAnimParts* parts, float time);
double __cdecl XAnimGetNotifyFracLeaf(const XAnimState* state, const XAnimState* nextState, float time, float dtime);
void __cdecl XAnimAddClientNotify(uint32_t notetrackName, float frac, uint32_t notifyType);
void __cdecl XAnimUpdateTimeAndNotetrackSyncSubTree(
    const DObj_s* obj,
    uint32_t infoIndex,
    float dtime,
    bool bNotify);
void __cdecl XAnimUpdateInfoSync(
    const DObj_s* obj,
    uint32_t infoIndex,
    bool bNotify,
    XAnimState* syncState,
    float dtime);
void __cdecl XAnimProcessServerNotify(const DObj_s* obj, XAnimInfo* info, float time);
XAnimParts* __cdecl XAnimGetParts(const XAnimTree_s* tree, XAnimInfo* info);
void __cdecl NotifyServerNotetrack(const DObj_s* obj, uint32_t notifyName, uint32_t notetrackName);
int __cdecl DObjUpdateServerInfo(DObj_s* obj, float dtime, int bNotify);
double __cdecl XAnimFindServerNoteTrack(const DObj_s* obj, uint32_t infoIndex, float dtime);
double __cdecl XAnimFindServerNoteTrackLeafNode(const DObj_s* obj, XAnimInfo* info, float dtime);
double __cdecl XAnimGetNextServerNotifyFrac(
    const DObj_s* obj,
    XAnimInfo* info,
    const XAnimState* syncState,
    const XAnimState* nextSyncState,
    float dtime);
double __cdecl XAnimFindServerNoteTrackSyncSubTree(const DObj_s* obj, XAnimInfo* info, float dtime);
double __cdecl XAnimGetServerNotifyFracSyncTotal(
    const DObj_s* obj,
    XAnimInfo* info,
    const XAnimState* syncState,
    const XAnimState* nextSyncState,
    float dtime);
int __cdecl DObjGetClientNotifyList(XAnimNotify_s** notifyList);
void __cdecl DObjDisplayAnimToBuffer(const DObj_s* obj, const char* header, char* buffer, int bufferSize);
void __cdecl XAnimDisplay(
    const XAnimTree_s *tree,
    uint32_t infoIndex,
    int depth,
    char *buffer,
    int bufferSize,
    int *bufferPos);
void __cdecl DObjDisplayAnim(const DObj_s* obj, const char* header);
void __cdecl XAnimCalcDelta(DObj_s* obj, uint32_t animIndex, float* rot, float* trans, bool bUseGoalWeight);
void __cdecl XAnimCalcDeltaTree(
    const DObj_s* obj,
    uint32_t infoIndex,
    float weightScale,
    XAnimDeltaInfo deltaInfo,
    XAnimSimpleRotPos* rotPos);
void __cdecl XAnimCalcRelDeltaParts(
    const XAnimParts* parts,
    float weightScale,
    float time1,
    float time2,
    XAnimSimpleRotPos* rotPos,
    int quatIndex);
void __cdecl TransformToQuatRefFrame(const float* rot, float* trans);
void __cdecl XAnimCalcAbsDeltaParts(const XAnimParts* parts, float weightScale, float time, XAnimSimpleRotPos* rotPos);
void __cdecl XAnimCalcAbsDelta(DObj_s* obj, uint32_t animIndex, float* rot, float* trans);
void __cdecl XAnimGetRelDelta(
    const XAnim_s* anims,
    uint32_t animIndex,
    float* rot,
    float* trans,
    float time1,
    float time2);
void __cdecl XAnimGetAbsDelta(const XAnim_s* anims, uint32_t animIndex, float* rot, float* trans, float time);
uint32_t __cdecl XAnimAllocInfoWithParent(
    XAnimTree_s* tree,
    uint16_t animToModel,
    uint32_t animIndex,
    uint32_t parentInfoIndex,
    int after);
uint32_t XAnimAllocInfoIndex(DObj_s *obj, uint32_t animIndex, int after);
uint32_t __cdecl XAnimEnsureGoalWeightParent(DObj_s* obj, uint32_t animIndex);
void __cdecl XAnimClearGoalWeightInternal(
    XAnimTree_s* tree,
    uint32_t infoIndex,
    float blendTime,
    int forceBlendTime);
void __cdecl XAnimClearTreeGoalWeightsInternal(
    XAnimTree_s* tree,
    uint32_t infoIndex,
    float blendTime,
    int forceBlendTime);
void __cdecl XAnimClearTreeGoalWeights(XAnimTree_s* tree, uint32_t animIndex, float blendTime);
void __cdecl XAnimClearTreeGoalWeightsStrict(XAnimTree_s* tree, uint32_t animIndex, float blendTime);
void __cdecl XAnimClearGoalWeightKnobInternal(
    XAnimTree_s* tree,
    uint32_t infoIndex,
    float goalWeight,
    float goalTime);
int __cdecl XAnimSetCompleteGoalWeightNode(
    XAnimTree_s* tree,
    uint32_t infoIndex,
    float goalWeight,
    float goalTime,
    float rate,
    uint32_t notifyName,
    uint32_t notifyType);
int XAnimSetCompleteGoalWeightKnobAll(
    DObj_s *obj,
    uint32_t animIndex,
    uint32_t rootIndex,
    float goalWeight,
    float goalTime,
    float rate,
    int notifyName,
    int notifyType,
    int bRestart);
int __cdecl XAnimSetGoalWeightKnobAll(
    DObj_s* obj,
    uint32_t animIndex,
    uint32_t rootIndex,
    float goalWeight,
    float goalTime,
    float rate,
    uint32_t notifyName,
    uint32_t notifyType,
    int bRestart);
int XAnimSetCompleteGoalWeightKnob(
    DObj_s *obj,
    uint32_t animIndex,
    double goalWeight,
    double goalTime,
    double rate,
    uint32_t notifyName,
    uint32_t notifyType,
    int bRestart);
int __cdecl XAnimSetGoalWeightKnob(
    DObj_s* obj,
    uint32_t animIndex,
    float goalWeight,
    float goalTime,
    float rate,
    uint32_t notifyName,
    uint32_t notifyType,
    int bRestart);
void __cdecl XAnimClearTree(XAnimTree_s* tree);
int __cdecl XAnimSetGoalWeightNode(
    XAnimTree_s* tree,
    uint32_t infoIndex,
    float goalWeight,
    float goalTime,
    float rate,
    uint32_t notifyName,
    uint32_t notifyType);
uint32_t __cdecl XAnimGetDescendantWithGreatestWeight(const XAnimTree_s* tree, uint32_t infoIndex);
void __cdecl XAnimSetupSyncNodes(XAnim_s* anims);
void __cdecl XAnimSetupSyncNodes_r(XAnim_s* anims, uint32_t animIndex);
void __cdecl XAnimFillInSyncNodes_r(XAnim_s* anims, uint32_t animIndex, bool bLoop);
bool __cdecl XAnimHasTime(const XAnim_s* anims, uint32_t animIndex);
BOOL __cdecl XAnimIsPrimitive(XAnim_s* anims, uint32_t animIndex);
void __cdecl XAnimSetTime(XAnimTree_s *tree, uint32_t animIndex, float time);
void __cdecl XAnimUpdateServerNotifyIndex(XAnimInfo* info, const XAnimParts* parts);
uint32_t __cdecl XAnimRestart(XAnimTree_s* tree, uint32_t infoIndex, float goalTime);
int __cdecl XAnimSetGoalWeight(
    DObj_s* obj,
    uint32_t animIndex,
    float goalWeight,
    float goalTime,
    float rate,
    uint32_t notifyName,
    uint32_t notifyType,
    int bRestart);
void __cdecl XAnimSetAnimRate(XAnimTree_s* tree, uint32_t animIndex, float rate);
bool __cdecl XAnimIsLooped(const XAnim_s* anims, uint32_t animIndex);
char __cdecl XAnimNotetrackExists(const XAnim_s* anims, uint32_t animIndex, uint32_t name);
void __cdecl XAnimAddNotetrackTimesToScriptArray(const XAnim_s* anims, uint32_t animIndex, uint32_t name);
int __cdecl XAnimSetCompleteGoalWeight(
    DObj_s* obj,
    uint32_t animIndex,
    float goalWeight,
    float goalTime,
    float rate,
    uint32_t notifyName,
    uint32_t notifyType,
    int bRestart);
void __cdecl XAnimCloneAnimInfo(const XAnimInfo* from, XAnimInfo* to);
void __cdecl XAnimCloneAnimTree(const XAnimTree_s* from, XAnimTree_s* to);
void __cdecl XAnimCloneAnimTree_r(
    const XAnimTree_s* from,
    XAnimTree_s* to,
    uint32_t fromInfoIndex,
    uint32_t toInfoParentIndex);
XAnimInfo* __cdecl GetAnimInfo(int infoIndex);
void XAnimDisableLeakCheck();
void XAnimFreeAnims(XAnim_s *anims, void(*Free)(void *, int));
void XAnimCloneClientAnimTree(const XAnimTree_s *from, XAnimTree_s *to);
void DObjTransfer(const DObj_s *fromObj, DObj_s *toObj, double dtime);


// xanim_load_obj
XModelPieces *__cdecl XModelPiecesPrecache(const char *name, void *(__cdecl *Alloc)(int));
XAnimParts *__cdecl XAnimLoadFile(char *name, void *(__cdecl *Alloc)(int));


// KISAK HACK: These are for gfx_d3d/r_material.h
struct TechniqueSetList
{
    MaterialTechniqueSet *hashTable[1024];
    int count;
};
void __cdecl R_GetMaterialList(XAssetHeader header, char *data);
void __cdecl Material_CollateTechniqueSets(XAssetHeader header, TechniqueSetList *techSetList);
void __cdecl Material_ReleaseTechniqueSet(XAssetHeader header, void *crap);
