#include <qcommon/system.h>

#include <array>
#include <cstdint>

namespace
{
constexpr int VALUE_SLOT_COUNT = 4;

std::array<void *, VALUE_SLOT_COUNT> g_mainThreadValues{};
std::array<std::uint32_t, CRITSECT_COUNT> g_criticalSectionDepth{};
bool g_mainThreadInitialized = false;
} // namespace

void Sys_InitializeCriticalSections()
{
    g_criticalSectionDepth.fill(0);
}

void Sys_EnterCriticalSection(int criticalSection)
{
    if (criticalSection >= 0 && criticalSection < CRITSECT_COUNT)
    {
        ++g_criticalSectionDepth[static_cast<std::size_t>(criticalSection)];
    }
}

void Sys_LeaveCriticalSection(int criticalSection)
{
    if (criticalSection >= 0 && criticalSection < CRITSECT_COUNT)
    {
        std::uint32_t &depth = g_criticalSectionDepth[static_cast<std::size_t>(criticalSection)];
        if (depth)
        {
            --depth;
        }
    }
}

void Sys_LockWrite(FastCriticalSection *criticalSection)
{
    if (criticalSection)
    {
        criticalSection->writeCount = criticalSection->writeCount + 1;
    }
}

void Sys_UnlockWrite(FastCriticalSection *criticalSection)
{
    if (criticalSection && criticalSection->writeCount)
    {
        criticalSection->writeCount = criticalSection->writeCount - 1;
    }
}

std::uint32_t Sys_GetCpuCount()
{
    // The first Web target is deliberately single-threaded. This must not
    // imply pthread, Worker, renderer-thread, or server-thread availability.
    return 1;
}

void Sys_InitMainThread()
{
    g_mainThreadInitialized = true;
}

bool Sys_IsMainThread()
{
    return g_mainThreadInitialized;
}

bool Sys_IsRenderThread()
{
    return false;
}

bool Sys_IsServerThread()
{
    return false;
}

void Sys_SetValue(int valueIndex, void *data)
{
    if (valueIndex >= 0 && valueIndex < VALUE_SLOT_COUNT)
    {
        g_mainThreadValues[static_cast<std::size_t>(valueIndex)] = data;
    }
}

void *Sys_GetValue(int valueIndex)
{
    if (valueIndex < 0 || valueIndex >= VALUE_SLOT_COUNT)
    {
        return nullptr;
    }
    return g_mainThreadValues[static_cast<std::size_t>(valueIndex)];
}

void NET_Sleep(int)
{
    // The Web engine prefix never waits. A future Worker-owned synchronous
    // runtime may provide a real cooperative/yielding implementation.
}
