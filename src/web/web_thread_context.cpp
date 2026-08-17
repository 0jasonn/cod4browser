#include <qcommon/system.h>
#include <qcommon/thread_context.h>

#include <array>
#include <cstdint>
#include <cstdlib>

namespace
{
constexpr int VALUE_SLOT_COUNT = 4;

std::array<std::array<void *, VALUE_SLOT_COUNT>, THREAD_CONTEXT_COUNT> g_contextValues{};
std::array<std::uint32_t, CRITSECT_COUNT> g_criticalSectionDepth{};
bool g_mainThreadInitialized = false;
ThreadContext_t g_currentContext = THREAD_CONTEXT_MAIN;
void(KISAK_CDECL *g_databaseFunction)(std::uint32_t) = nullptr;
bool g_databaseCompleted = true;
bool g_databaseCompleted2 = true;

void RunDatabaseFunction()
{
    if (!g_databaseFunction) std::abort();
    const ThreadContext_t previous = g_currentContext;
    g_currentContext = THREAD_CONTEXT_DATABASE;
    g_databaseFunction(THREAD_CONTEXT_DATABASE);
    g_currentContext = previous;
}
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
    return g_mainThreadInitialized && g_currentContext == THREAD_CONTEXT_MAIN;
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
        g_contextValues[static_cast<std::size_t>(g_currentContext)]
            [static_cast<std::size_t>(valueIndex)] = data;
    }
}

void *Sys_GetValue(int valueIndex)
{
    if (valueIndex < 0 || valueIndex >= VALUE_SLOT_COUNT)
    {
        return nullptr;
    }
    return g_contextValues[static_cast<std::size_t>(g_currentContext)]
        [static_cast<std::size_t>(valueIndex)];
}

char Sys_SpawnDatabaseThread(void(KISAK_CDECL *function)(std::uint32_t))
{
    if (!function || g_databaseFunction) return 0;
    g_databaseFunction = function;
    g_contextValues[THREAD_CONTEXT_DATABASE] = g_contextValues[THREAD_CONTEXT_MAIN];
    RunDatabaseFunction();
    return 1;
}

bool Sys_IsDatabaseThread()
{
    return g_currentContext == THREAD_CONTEXT_DATABASE;
}

void Sys_WakeDatabase() { g_databaseCompleted = false; }
void Sys_WakeDatabase2() { g_databaseCompleted2 = false; }
void Sys_DatabaseCompleted() { g_databaseCompleted = true; }
void Sys_DatabaseCompleted2() { g_databaseCompleted2 = true; }
bool Sys_IsDatabaseReady() { return g_databaseCompleted; }
bool Sys_IsDatabaseReady2() { return g_databaseCompleted2; }
void Sys_SyncDatabase() { if (!g_databaseCompleted) std::abort(); }
void Sys_NotifyDatabase()
{
    if (!g_databaseFunction) std::abort();
    RunDatabaseFunction();
}
void Sys_WaitStartDatabase() {}
void Sys_WaitDatabaseThread() {}

void NET_Sleep(int)
{
    // The Web engine prefix never waits. A future Worker-owned synchronous
    // runtime may provide a real cooperative/yielding implementation.
}
