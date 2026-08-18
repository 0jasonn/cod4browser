// Canonical first-map Hunk owner split from com_memory.cpp while the rest of
// that translation unit's XAnim/model cleanup closure is not yet linked. The
// allocation direction, alignment, zero-fill, and low/high collision checks
// match Kisak. Reservation/freeing remains a platform-system responsibility.

#include <universal/com_memory.h>

#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <universal/q_shared.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
struct HunkEndState
{
    std::uint32_t permanent = 0;
    std::uint32_t temp = 0;
};

std::uint8_t *g_hunkData = nullptr;
std::uint32_t g_hunkSize = 0;
HunkEndState g_hunkLow{};
HunkEndState g_hunkHigh{};

std::uint32_t AlignUp(std::uint64_t value, std::uint32_t alignment)
{
    const std::uint64_t aligned =
        (value + alignment - 1u) & ~std::uint64_t{alignment - 1u};
    if (aligned > UINT32_MAX)
        Com_Error(ERR_DROP, "Hunk allocation size overflow");
    return static_cast<std::uint32_t>(aligned);
}

void ValidateAlignment(std::int32_t alignment)
{
    if (alignment <= 0 || (alignment & (alignment - 1)) != 0 || alignment > 4096)
        Com_Error(ERR_DROP, "Hunk allocation alignment %i is invalid", alignment);
}

void EnsureInitialized()
{
    if (!g_hunkData)
        Com_Error(ERR_DROP, "Hunk allocation used before Com_InitHunkMemory");
}

void EnsureAvailable(std::uint32_t low, std::uint32_t high, std::uint32_t size)
{
    if (low > g_hunkSize || high > g_hunkSize ||
        low > g_hunkSize - high || size > g_hunkSize - low - high)
    {
        Com_Error(
            ERR_DROP,
            "Hunk allocation failed on %u bytes (total %u MB, low %u MB, high %u MB)",
            size,
            g_hunkSize / 0x100000u,
            low / 0x100000u,
            high / 0x100000u);
    }
}
} // namespace

void Com_InitHunkMemory()
{
    iassert(Sys_IsMainThread());
    iassert(!g_hunkData);
    if (g_hunkData)
        Com_Error(ERR_DROP, "Com_InitHunkMemory called twice");

    // Native fastfile SP selects ten MiB here. The platform allocator reserves
    // the contiguous range; browser code never owns objects placed within it.
    constexpr std::uint32_t kFastfileHunkSize = 0x00a00000u;
    g_hunkData = static_cast<std::uint8_t *>(
        Sys_AllocatePhysicalMemory(kFastfileHunkSize, 4096u));
    if (!g_hunkData)
    {
        Sys_OutOfMemErrorInternal(".\\universal\\com_memory_hunk.cpp", 72);
        return;
    }
    g_hunkSize = kFastfileHunkSize;
    g_hunkLow = {};
    g_hunkHigh = {};
}

void Hunk_Clear()
{
    iassert(Sys_IsMainThread());
    EnsureInitialized();
    g_hunkLow = {};
    g_hunkHigh = {};
}

std::int32_t __cdecl Hunk_Used()
{
    EnsureInitialized();
    return static_cast<std::int32_t>(
        g_hunkLow.permanent + g_hunkHigh.permanent);
}

std::uint8_t *__cdecl Hunk_Alloc(
    std::uint32_t size, const char *name, std::int32_t type)
{
    return Hunk_AllocAlign(size, 32, name, type);
}

std::uint8_t *__cdecl Hunk_AllocAlign(
    std::uint32_t size,
    std::int32_t alignment,
    const char *,
    std::int32_t)
{
    EnsureInitialized();
    ValidateAlignment(alignment);
    iassert(g_hunkHigh.temp == g_hunkHigh.permanent);

    const std::uint32_t oldPermanent = g_hunkHigh.permanent;
    const std::uint32_t alignedPermanent = AlignUp(
        static_cast<std::uint64_t>(oldPermanent) + size,
        static_cast<std::uint32_t>(alignment));
    EnsureAvailable(g_hunkLow.temp, oldPermanent, alignedPermanent - oldPermanent);
    g_hunkHigh.permanent = alignedPermanent;
    g_hunkHigh.temp = alignedPermanent;
    std::uint8_t *result = g_hunkData + g_hunkSize - alignedPermanent;
    iassert((reinterpret_cast<std::uintptr_t>(result) &
        static_cast<std::uintptr_t>(alignment - 1)) == 0);
    std::memset(result, 0, size);
    return result;
}

std::uint8_t *__cdecl Hunk_AllocLow(
    std::uint32_t size, const char *name, std::int32_t type)
{
    return Hunk_AllocLowAlign(size, 32, name, type);
}

std::uint8_t *__cdecl Hunk_AllocLowAlign(
    std::uint32_t size,
    std::int32_t alignment,
    const char *,
    std::int32_t)
{
    EnsureInitialized();
    ValidateAlignment(alignment);
    iassert(g_hunkLow.temp == g_hunkLow.permanent);

    const std::uint32_t alignedStart = AlignUp(
        g_hunkLow.permanent, static_cast<std::uint32_t>(alignment));
    const std::uint32_t newPermanent = AlignUp(
        static_cast<std::uint64_t>(alignedStart) + size, 1u);
    EnsureAvailable(g_hunkLow.permanent, g_hunkHigh.temp,
        newPermanent - g_hunkLow.permanent);
    g_hunkLow.permanent = newPermanent;
    g_hunkLow.temp = newPermanent;
    std::uint8_t *result = g_hunkData + alignedStart;
    iassert((reinterpret_cast<std::uintptr_t>(result) &
        static_cast<std::uintptr_t>(alignment - 1)) == 0);
    std::memset(result, 0, size);
    return result;
}
