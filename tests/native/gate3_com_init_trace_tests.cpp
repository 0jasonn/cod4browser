#include <qcommon/com_init_trace.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <universal/dvar.h>
#include <universal/physicalmemory.h>
#include <universal/q_shared.h>

#include <array>
#include <cassert>
#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace
{
std::array<void *, 4> g_values{};
bool g_mainThreadInitialized = false;
int g_probeCount = 0;
int g_databaseThreadInitCount = 0;
bool g_nestedTryExecuteDeferred = false;
std::array<std::array<char, 32>, 2> g_probeArguments{};

void __cdecl ProbeCommand()
{
    assert(Cmd_Argc() == 2);
    assert(I_stricmp(Cmd_Argv(0), "gate3_probe") == 0);
    assert(g_probeCount < static_cast<int>(g_probeArguments.size()));
    std::snprintf(
        g_probeArguments[static_cast<std::size_t>(g_probeCount)].data(),
        g_probeArguments[static_cast<std::size_t>(g_probeCount)].size(),
        "%s",
        Cmd_Argv(1));
    ++g_probeCount;
}

void __cdecl TryExecuteCommand()
{
    g_nestedTryExecuteDeferred = !Cbuf_TryExecute(0, 0);
}
} // namespace

void DB_InitThread() { ++g_databaseThreadInitCount; }
int com_inServerFrame = 0;
void __cdecl SV_WaitServer() { assert(!com_inServerFrame); }
void __cdecl G_AddCommandNotify(volatile std::uint16_t) {}
void Scr_Error(const char *) { std::abort(); }
void __cdecl MemFile_WriteData(MemoryFile *, int, const void *) {}
void __cdecl MemFile_WriteCString(MemoryFile *, const char *) {}
const char *__cdecl MemFile_ReadCString(MemoryFile *) { return ""; }
void __cdecl MemFile_ReadData(MemoryFile *, int, std::uint8_t *) {}

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
double __cdecl ConvertToMB(int bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }
void Sys_OutOfMemErrorInternal(const char *filename, int line)
{
    Com_Error(ERR_DROP, "Out of memory at %s:%d", filename, line);
}
void *Sys_AllocatePhysicalMemory(std::size_t size, std::size_t alignment)
{
#ifdef _WIN32
    return _aligned_malloc(size, alignment);
#else
    return std::aligned_alloc(alignment, size);
#endif
}
void Sys_FreePhysicalMemory(void *memory)
{
#ifdef _WIN32
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}
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
    constexpr std::array<const char *, 15> expectedStages{{
        "Com_Init entered",
        "Com_ParseCommandLine",
        "SL_Init",
        "Swap_Init",
        "Cbuf_Init",
        "Cmd_Init",
        "Com_StartupVariable",
        "Com_InitDvars",
        "CCS_InitConstantConfigStrings",
        "PMem_Init",
        "DB_SetInitializing",
        "PMem_BeginAlloc",
        "Com_InitXAssets",
        "stop",
        nullptr,
    }};
    assert(trace.stageCount == expectedStages.size() - 1);
    for (std::size_t index = 0; index < trace.stageCount; ++index)
    {
        assert(std::strcmp(trace.stages[index], expectedStages[index]) == 0);
    }
    assert(std::strcmp(trace.stopStage, "DB_LoadXAssets/engine-filesystem-mount") == 0);
    assert(g_databaseThreadInitCount == 1);
    assert(trace.startupVariableCount == 3);
    constexpr std::array<const char *, 6> expectedCommands{{
        "wait", "vstr", "exec", "cmdlist", "seta", "set",
    }};
    assert(trace.commandCount == expectedCommands.size());
    for (std::size_t index = 0; index < expectedCommands.size(); ++index)
    {
        assert(std::strcmp(trace.commandNames[index], expectedCommands[index]) == 0);
    }
    assert(trace.dvarCount == 22);
    assert(trace.physicalMemorySize == 0x8000000u);
    assert(trace.pmemLowPosition == 0);
    assert(trace.pmemHighPosition == 0x8000000u);
    assert(trace.pmemHighAllocationCount == 1);
    assert(trace.databaseInitializing);
    assert(std::strcmp(Dvar_GetString("gate3_startup"), "portable") == 0);
    const dvar_t *archive = Dvar_FindVar("gate3_archive");
    assert(archive && archive->type == DVAR_TYPE_STRING);
    assert((archive->flags & DVAR_ARCHIVE) != 0);
    const dvar_t *smp = Dvar_FindVar("sys_smp_allowed");
    assert(smp && !smp->current.enabled && (smp->flags & DVAR_INIT));

    // Exercise the real command implementation's registration order, exact
    // lookup, case-insensitive execution, token/argument access, buffered wait
    // semantics, and explicit registration lifetime.
    assert(Cmd_FindCommand("wait") != nullptr);
    assert(Cmd_FindCommand("WAIT") == nullptr);
    static cmd_function_s probeCommand{};
    Cmd_AddCommandInternal("gate3_probe", ProbeCommand, &probeCommand);
    assert(Cmd_FindCommand("gate3_probe") == &probeCommand);
    Cbuf_AddText(0, "GATE3_PROBE first;wait;gate3_probe second\n");
    Cbuf_Execute(0, 0);
    assert(g_probeCount == 1);
    assert(std::strcmp(g_probeArguments[0].data(), "first") == 0);
    Cbuf_Execute(0, 0);
    assert(g_probeCount == 2);
    assert(std::strcmp(g_probeArguments[1].data(), "second") == 0);
    Cmd_RemoveCommand("gate3_probe");
    assert(Cmd_FindCommand("gate3_probe") == nullptr);

    static cmd_function_s tryExecuteCommand{};
    Cmd_AddCommandInternal(
        "gate3_try_execute", TryExecuteCommand, &tryExecuteCommand);
    Cbuf_AddText(0, "gate3_try_execute\n");
    assert(Cbuf_TryExecute(0, 0));
    assert(g_nestedTryExecuteDeferred);
    Cmd_RemoveCommand("gate3_try_execute");

    char limitedTokens[] = "first second token with spaces";
    Cmd_TokenizeStringWithLimit(limitedTokens, 2);
    assert(Cmd_Argc() == 2);
    assert(std::strcmp(Cmd_Argv(0), "first") == 0);
    assert(std::strcmp(Cmd_Argv(1), "second token with spaces") == 0);
    Cmd_EndTokenizedString();

    static bool caughtPreFilesystemExec = false;
    if (!setjmp(errorBoundary))
    {
        Cbuf_ExecuteBuffer(0, 0, "exec gate3-test.cfg");
    }
    else
    {
        caughtPreFilesystemExec = true;
    }
    assert(caughtPreFilesystemExec);

    // The canonical arena remains two-ended. Check alignment, collision-free
    // low/high movement, and named-scope reset against the real global PMem.
    constexpr const char *lowScope = "gate3-low-test";
    const char *highScope = PMem_GetState()->prim[1].allocName;
    assert(highScope != nullptr);
    PMem_BeginAlloc(lowScope, 0);
    std::uint8_t *lowAllocation = PMem_Alloc(100, 64, 0, 0);
    std::uint8_t *highAllocation = PMem_Alloc(100, 64, 0, 1);
    const PhysicalMemory *memory = PMem_GetState();
    assert(reinterpret_cast<std::uintptr_t>(lowAllocation) % 64 == 0);
    assert(reinterpret_cast<std::uintptr_t>(highAllocation) % 64 == 0);
    assert(memory->prim[0].pos == 100);
    assert(memory->prim[1].pos == 0x7FFFF80u);
    assert(PMem_GetFreeAmount() == memory->prim[1].pos - memory->prim[0].pos);
    PMem_EndAlloc(lowScope, 0);
    PMem_EndAlloc(highScope, 1);
    PMem_Free(lowScope, 0);
    PMem_Free(highScope, 1);
    assert(memory->prim[0].pos == 0);
    assert(memory->prim[1].pos == 0x8000000u);
    assert(memory->prim[0].allocListCount == 0);
    assert(memory->prim[1].allocListCount == 0);

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

    static bool caughtArenaOverflow = false;
    PMem_BeginAlloc("overflow-test", 0);
    if (!setjmp(errorBoundary))
    {
        (void)PMem_Alloc(0x8000001u, 4096, 0, 0);
    }
    else
    {
        caughtArenaOverflow = true;
    }
    assert(caughtArenaOverflow);

    std::printf(
        "canonical-runtime-trace stages=%zu startup=%zu commands=%zu dvars=%zu pmem=%zu stop=%s\n",
        trace.stageCount,
        trace.startupVariableCount,
        trace.commandCount,
        trace.dvarCount,
        trace.physicalMemorySize,
        trace.stopStage);
    return 0;
}
