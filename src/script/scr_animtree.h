#pragma once
#include "scr_animtree_runtime.h"
#include <xanim/xanim.h>
#include <bgame/bg_local.h>

struct scrAnimGlob_t // sizeof=0x20C
{                                       // ...
    const char *start;                  // ...
    const char *pos;                    // ...
    uint16_t using_xanim_lookup[2][MAX_XANIMTREE_NUM]; // ...
    int bAnimCheck;                     // ...
};
static_assert(sizeof(scrAnimGlob_t) == 0x20C);

void __cdecl TRACK_scr_animtree();
void __cdecl SetAnimCheck(int bAnimCheck);
void __cdecl Scr_EmitAnimation(char *pos, uint32_t animName, uint32_t sourcePos);
void __cdecl Scr_EmitAnimationInternal(char *pos, uint32_t animName, uint32_t names);
int __cdecl Scr_GetAnimsIndex(const XAnim_s *anims);
XAnim_s *__cdecl Scr_GetAnims(uint32_t index);
void __cdecl Scr_UsingTree(const char *filename, uint32_t sourcePos);
uint32_t __cdecl Scr_UsingTreeInternal(const char *filename, uint32_t *index, int user);
void __cdecl Scr_LoadAnimTreeAtIndex(uint32_t index, void *(__cdecl *Alloc)(int), int user);
int __cdecl Scr_GetAnimTreeSize(uint32_t parentNode);
void __cdecl ConnectScriptToAnim(
    uint32_t names,
    uint16_t index,
    uint32_t filename,
    uint32_t name,
    uint16_t treeIndex);
int __cdecl Scr_CreateAnimationTree(
    uint32_t parentNode,
    uint32_t names,
    XAnim_s *anims,
    uint32_t childIndex,
    const char *parentName,
    uint32_t parentIndex,
    uint32_t filename,
    int treeIndex,
    uint16_t flags);
void __cdecl Scr_CheckAnimsDefined(uint32_t names, uint32_t filename);
bool __cdecl Scr_LoadAnimTreeInternal(const char *filename, uint32_t parentNode, uint32_t names);
void __cdecl Scr_AnimTreeParse(const char *pos, uint32_t parentNode, uint32_t names);
void __cdecl AnimTreeCompileError(const char *msg);
bool __cdecl AnimTreeParseInternal(
    uint32_t parentNode,
    uint32_t names,
    bool bIncludeParent,
    bool bLoop,
    bool bComplete);
int __cdecl GetAnimTreeParseProperties();
scr_animtree_t __cdecl Scr_FindAnimTree(const char *filename);
void __cdecl Scr_FindAnim(const char *filename, const char *animName, scr_anim_s *anim, int user);
