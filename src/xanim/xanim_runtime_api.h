#pragma once

#include <universal/q_shared.h>

struct XAnim_s;
struct XAnimTree_s;

XAnimTree_s *__cdecl XAnimCreateTree(
    XAnim_s *anims, void *(__cdecl *Alloc)(int));
void __cdecl XAnimFreeTree(
    XAnimTree_s *tree, void (__cdecl *Free)(void *, int));
void __cdecl XAnimClearTree(XAnimTree_s *tree);
void XAnimDisableLeakCheck();
int __cdecl DObjUpdateServerInfo(
    DObj_s *obj, float dtime, int notify);
void __cdecl DObjInitServerTime(DObj_s *obj, float dtime);
void __cdecl DObjDisplayAnim(const DObj_s *obj, const char *header);
