#pragma once

#include <cstddef>
#include <cstdint>
#include <bit>

inline bool Sys_BitScanReverse(std::uint32_t *index, std::uint32_t mask)
{
    if (!mask)
    {
        return false;
    }
    *index = 31u - static_cast<std::uint32_t>(std::countl_zero(mask));
    return true;
}

#if defined(_MSC_VER)
#define KISAK_CDECL __cdecl
#else
#define KISAK_CDECL
#endif

#if defined(KISAK_GATE3_COM_INIT_PREFIX) || !defined(_WIN32)
#ifndef KISAK_LONG_DEFINED
using LONG = std::int32_t;
#define KISAK_LONG_DEFINED 1
#endif

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

inline LONG InterlockedExchangeAdd(volatile LONG *value, LONG amount)
{
    const LONG original = *value;
    *value = original + amount;
    return original;
}

inline std::uint32_t InterlockedExchangeAdd(
    volatile std::uint32_t *value,
    std::uint32_t amount)
{
    const std::uint32_t original = *value;
    *value = original + amount;
    return original;
}

inline LONG InterlockedExchange(volatile LONG *value, LONG exchange)
{
    const LONG original = *value;
    *value = exchange;
    return original;
}

inline std::uint32_t InterlockedExchange(
    volatile std::uint32_t *value,
    std::uint32_t exchange)
{
    const std::uint32_t original = *value;
    *value = exchange;
    return original;
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

#if defined(KISAK_WEB)
inline long InterlockedIncrement(volatile long *value)
{
    const long updated = *value + 1;
    *value = updated;
    return updated;
}

inline long InterlockedDecrement(volatile long *value)
{
    const long updated = *value - 1;
    *value = updated;
    return updated;
}

inline long InterlockedExchangeAdd(volatile long *value, long amount)
{
    const long original = *value;
    *value = original + amount;
    return original;
}

inline long InterlockedExchange(volatile long *value, long exchange)
{
    const long original = *value;
    *value = exchange;
    return original;
}

inline long InterlockedCompareExchange(
    volatile long *destination,
    long exchange,
    long compare)
{
    const long original = *destination;
    if (original == compare)
    {
        *destination = exchange;
    }
    return original;
}
#endif
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
char *Sys_GetClipboardData();
int KISAK_CDECL Sys_SetClipboardData(const char *text);
void IN_Frame();
void KISAK_CDECL IN_ShowSystemCursor(int show);
void KISAK_CDECL Sys_OpenURL(const char *url, int doexit);
void Sys_GetHardwareDescription(char *gpu, std::size_t gpuSize,
    char *cpuVendor, std::size_t cpuVendorSize,
    char *cpuName, std::size_t cpuNameSize);
bool Sys_IsRenderThread();
bool Sys_IsServerThread();
void Sys_SetValue(int valueIndex, void *data);
void *Sys_GetValue(int valueIndex);
void NET_Sleep(int milliseconds);
void Sys_Error(const char *format, ...);
void Sys_OutOfMemErrorInternal(const char *filename, int line);
void *Sys_AllocatePhysicalMemory(std::size_t size, std::size_t alignment);
void Sys_FreePhysicalMemory(void *memory);
void *Sys_VirtualReserve(std::size_t size);
bool Sys_VirtualCommit(void *memory, std::size_t size);
void Sys_VirtualDecommit(void *memory, std::size_t size);
void Sys_VirtualRelease(void *memory);
