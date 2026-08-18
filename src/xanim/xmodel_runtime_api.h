#pragma once

#include <xanim/xmodel_types.h>

int __cdecl XModelNumBones(const XModel *model);
XModel *__cdecl XModelPrecache(
    char *name,
    void *(__cdecl *Alloc)(int),
    void *(__cdecl *AllocColl)(int));
