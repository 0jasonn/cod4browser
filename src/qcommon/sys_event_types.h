#pragma once

#include <qcommon/qcommon.h>

// Portable shape of the canonical platform event record. The Windows owner
// historically declared this beside DirectInput state; browser event pumping
// needs the record without importing Win32 headers.
struct sysEvent_t
{
    int evTime;
    sysEventType_t evType;
    int evValue;
    int evValue2;
    int evPtrLength;
    void *evPtr;
};

static_assert(sizeof(void *) != 4 || sizeof(sysEvent_t) == 24);

sysEvent_t *__cdecl Sys_GetEvent(sysEvent_t *result);
