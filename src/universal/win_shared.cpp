#include <universal/q_shared.h>
#include "q_shared.h"

#include <cmath>
#include <intrin.h>

#include <Windows.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>

int initialized_1 = 0;
int sys_timeBase;

uint32_t __cdecl Sys_Milliseconds()
{
    if (!initialized_1)
    {
        sys_timeBase = timeGetTime();
        initialized_1 = 1;
    }
    return timeGetTime() - sys_timeBase;
}

uint32_t __cdecl Sys_MillisecondsRaw()
{
    return timeGetTime();
}

uint32_t __cdecl Sys_RawTimerTicks()
{
    return static_cast<uint32_t>(__rdtsc());
}

void *Sys_VirtualReserve(std::size_t size)
{
    return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);
}

bool Sys_VirtualCommit(void *memory, std::size_t size)
{
    return VirtualAlloc(memory, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

void Sys_VirtualDecommit(void *memory, std::size_t size)
{
    VirtualFree(memory, size, MEM_DECOMMIT);
}

void Sys_VirtualRelease(void *memory)
{
    VirtualFree(memory, 0, MEM_RELEASE);
}

void __cdecl Sys_SnapVector(float *v)
{
    v[0] = SnapFloat(v[0]);
    v[1] = SnapFloat(v[1]);
    v[2] = SnapFloat(v[2]);
}

