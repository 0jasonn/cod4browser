#pragma once

#include <cstdint>

#if defined(KISAK_GATE3_COM_INIT_PREFIX) || !defined(_WIN32)
using LONG = std::int32_t;

inline LONG InterlockedIncrement(volatile LONG *value)
{
    const LONG updated = *value + 1;
    *value = updated;
    return updated;
}

inline std::uint32_t InterlockedIncrement(volatile std::uint32_t *value)
{
    const std::uint32_t updated = *value + 1;
    *value = updated;
    return updated;
}

inline LONG InterlockedDecrement(volatile LONG *value)
{
    const LONG updated = *value - 1;
    *value = updated;
    return updated;
}

inline std::uint32_t InterlockedDecrement(volatile std::uint32_t *value)
{
    const std::uint32_t updated = *value - 1;
    *value = updated;
    return updated;
}

inline LONG InterlockedCompareExchange(volatile LONG *destination, LONG exchange, LONG compare)
{
    const LONG original = *destination;
    if (original == compare)
    {
        *destination = exchange;
    }
    return original;
}

inline std::uint32_t InterlockedCompareExchange(
    volatile std::uint32_t *destination,
    std::uint32_t exchange,
    std::uint32_t compare)
{
    const std::uint32_t original = *destination;
    if (original == compare)
    {
        *destination = exchange;
    }
    return original;
}
#else
#include <Windows.h>
#endif

// Engine-visible synchronization identities. The Win32 implementation and
// the single-threaded Web implementation provide the same API; OS handles and
// browser lifecycle details stay outside qcommon.
#if defined(KISAK_MP)
enum CriticalSection : int
{
    CRITSECT_CONSOLE = 0,
    CRITSECT_DEBUG_SOCKET,
    CRITSECT_COM_ERROR,
    CRITSECT_STATMON,
    CRITSECT_DEBUG_LINE,
    CRITSECT_ALLOC_MARK,
    CRITSECT_SCRIPT_STRING,
    CRITSECT_MEMORY_TREE,
    CRITSECT_ASSERT,
    CRITSECT_RD_BUFFER,
    CRITSECT_SYS_EVENT_QUEUE,
    CRITSECT_GPU_FENCE,
    CRITSECT_FATAL_ERROR,
    CRITSECT_SCRIPT_DEBUGGER_ALLOC,
    CRITSECT_MISSING_ASSET,
    CRITSECT_PHYSICS,
    CRITSECT_LIVE,
    CRITSECT_AUDIO_PHYSICS,
    CRITSECT_CINEMATIC,
    CRITSECT_CINEMATIC_TARGET_CHANGE,
    CRITSECT_FX_ALLOC,
    CRITSECT_CBUF,
    CRITSECT_COUNT,
};
#else
enum CriticalSection : int
{
    CRITSECT_CONSOLE = 0,
    CRITSECT_DEBUG_SOCKET,
    CRITSECT_COM_ERROR,
    CRITSECT_STATMON,
    CRITSECT_SOUND_ALLOC,
    CRITSECT_MEM_ALLOC0,
    CRITSECT_MEM_ALLOC1,
    CRITSECT_DEBUG_LINE,
    CRITSECT_ALLOC_MARK,
    CRITSECT_STREAMED_SOUND,
    CRITSECT_FAKELAG,
    CRITSECT_CLIENT_MESSAGE,
    CRITSECT_CLIENT_CMD,
    CRITSECT_DOBJ_ALLOC,
    CRITSECT_START_SERVER,
    CRITSECT_XANIM_ALLOC,
    CRITSECT_KEY_BINDINGS,
    CRITSECT_FX_VIS,
    CRITSECT_SERVER_MESSAGE,
    CRITSECT_SCRIPT_STRING,
    CRITSECT_MEMORY_TREE,
    CRITSECT_ASSERT,
    CRITSECT_SCRIPT_DEBUGGER_ALLOC,
    CRITSECT_MISSING_ASSET,
    CRITSECT_PHYSICS,
    CRITSECT_LIVE,
    CRITSECT_AUDIO_PHYSICS,
    CRITSECT_CINEMATIC,
    CRITSECT_CINEMATIC_TARGET_CHANGE,
    CRITSECT_FX_ALLOC,
    CRITSECT_NETTHREAD_OVERRIDE,
    CRITSECT_CBUF,
    CRITSECT_SYS_EVENT_QUEUE,
    CRITSECT_FATAL_ERROR,
    CRITSECT_GPU_FENCE,
    CRITSECT_COUNT,
};
#endif

struct FastCriticalSection
{
    volatile std::uint32_t readCount;
    volatile std::uint32_t writeCount;
};

void Sys_InitializeCriticalSections();
void Sys_EnterCriticalSection(int criticalSection);
void Sys_LeaveCriticalSection(int criticalSection);
void Sys_LockWrite(FastCriticalSection *criticalSection);
void Sys_UnlockWrite(FastCriticalSection *criticalSection);

std::uint32_t Sys_GetCpuCount();
void Sys_InitMainThread();
bool Sys_IsMainThread();
bool Sys_IsRenderThread();
bool Sys_IsServerThread();
void Sys_SetValue(int valueIndex, void *data);
void *Sys_GetValue(int valueIndex);
void NET_Sleep(int milliseconds);
void Sys_Error(const char *format, ...);
