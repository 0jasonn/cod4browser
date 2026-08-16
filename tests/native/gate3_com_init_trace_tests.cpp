#include <qcommon/com_init_trace.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>

#include <array>
#include <cassert>
#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
std::array<void *, 4> g_values{};
bool g_mainThreadInitialized = false;
} // namespace

void Sys_InitializeCriticalSections() {}
void Sys_EnterCriticalSection(int) {}
void Sys_LeaveCriticalSection(int) {}
void Sys_LockWrite(FastCriticalSection *section) { section->writeCount = section->writeCount + 1; }
void Sys_UnlockWrite(FastCriticalSection *section) { section->writeCount = section->writeCount - 1; }
std::uint32_t Sys_GetCpuCount() { return 1; }
void Sys_InitMainThread() { g_mainThreadInitialized = true; }
bool Sys_IsMainThread() { return g_mainThreadInitialized; }
bool Sys_IsRenderThread() { return false; }
bool Sys_IsServerThread() { return false; }
void Sys_SetValue(int index, void *value) { g_values[static_cast<std::size_t>(index)] = value; }
void *Sys_GetValue(int index) { return g_values[static_cast<std::size_t>(index)]; }
void NET_Sleep(int) {}
std::uint32_t __cdecl Sys_Milliseconds() { return 0; }
void __cdecl Sys_Print(const char *text) { std::fputs(text, stdout); }

void Sys_Error(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
    std::abort();
}

void MyAssertHandler(const char *filename, int line, int type, const char *format, ...)
{
    std::fprintf(stderr, "assert:%s:%d:%d:", filename, line, type);
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::abort();
}

int main()
{
    Sys_InitializeCriticalSections();
    Sys_InitMainThread();
    static jmp_buf errorBoundary;
    static va_info_t formattedText;
    Sys_SetValue(1, &formattedText);
    Sys_SetValue(2, &errorBoundary);

    Dvar_Init();
    char commandLine[] = "+set gate3_startup portable +seta gate3_archive 1";
    Com_Init(commandLine);

    const ComInitTraceSnapshot &trace = Com_GetInitTrace();
    constexpr std::array<const char *, 11> expectedStages{{
        "Com_Init entered",
        "Com_ParseCommandLine",
        "SL_Init",
        "Swap_Init",
        "Cbuf_Init",
        "Cmd_Init",
        "Com_StartupVariable",
        "Com_InitDvars",
        "CCS_InitConstantConfigStrings",
        "stop",
        nullptr,
    }};
    assert(trace.stageCount == expectedStages.size() - 1);
    for (std::size_t index = 0; index < trace.stageCount; ++index)
    {
        assert(std::strcmp(trace.stages[index], expectedStages[index]) == 0);
    }
    assert(std::strcmp(trace.stopStage, "PMem_Init/DB_SetInitializing") == 0);
    assert(trace.startupVariableCount == 3);
    assert(trace.commandCount == 4);
    assert(trace.dvarCount == 22);
    assert(std::strcmp(Dvar_GetString("gate3_startup"), "portable") == 0);
    const dvar_t *archive = Dvar_FindVar("gate3_archive");
    assert(archive && archive->type == DVAR_TYPE_STRING);
    assert((archive->flags & DVAR_ARCHIVE) != 0);
    const dvar_t *smp = Dvar_FindVar("sys_smp_allowed");
    assert(smp && !smp->current.enabled && (smp->flags & DVAR_INIT));

    static bool caughtEngineError = false;
    if (!setjmp(errorBoundary))
    {
        Com_Error(ERR_DROP, "gate3 error-boundary probe");
    }
    else
    {
        caughtEngineError = true;
    }
    assert(caughtEngineError);

    std::printf(
        "gate3-trace stages=%zu startup=%zu commands=%zu dvars=%zu stop=%s\n",
        trace.stageCount,
        trace.startupVariableCount,
        trace.commandCount,
        trace.dvarCount,
        trace.stopStage);
    return 0;
}
