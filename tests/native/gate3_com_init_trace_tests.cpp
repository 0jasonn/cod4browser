#include <qcommon/com_init_trace.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <database/db_string_ownership.h>
#include <script/scr_stringlist.h>
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
int g_domainCallCount = 0;
int g_domainValue = 0;
std::array<std::array<char, 32>, 2> g_probeArguments{};

bool __cdecl ValidateDomainValue(dvar_s *, DvarValue value)
{
    ++g_domainCallCount;
    g_domainValue = value.integer;
    return value.integer >= 0;
}

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
float com_timescaleValue = 1.0f;
float com_codeTimeScale = 1.0f;
void __cdecl SV_WaitServer() { assert(!com_inServerFrame); }
void __cdecl G_AddCommandNotify(volatile std::uint16_t) {}
void Scr_Error(const char *) { std::abort(); }
void __cdecl MemFile_WriteData(MemoryFile *, int, const void *) {}
void __cdecl MemFile_WriteCString(MemoryFile *, const char *) {}
const char *__cdecl MemFile_ReadCString(MemoryFile *) { return ""; }
void __cdecl MemFile_ReadData(MemoryFile *, int, std::uint8_t *) {}
#if KISAK_RUNTIME_COMMANDS_TEST
void __cdecl Com_Quit_f() {}
int __cdecl FS_FOpenFileWrite(const char *) { return 0; }
int __cdecl FS_FOpenFileWriteToDir(const char *, const char *) { return 0; }
void FS_Printf(int, const char *, ...) {}
void __cdecl FS_FCloseFile(int) {}
void __cdecl Key_WriteBindings(int32_t, int32_t) {}
void __cdecl Con_WriteFilterConfigString(int32_t) {}
void __cdecl Dvar_WriteVariables(int) {}
void __cdecl Dvar_WriteDefaults(int) {}
#endif

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

    const std::uint32_t sharedString = SL_GetString("gate3-zone-shared", 4u);
    DB_RegisterStringZoneOwnership(sharedString, 1u);
    assert(SL_GetString("gate3-zone-shared", 4u) == sharedString);
    DB_RegisterStringZoneOwnership(sharedString, 2u);
    assert(DB_HasRegisteredStringOwnership() &&
        (SL_GetUser(sharedString) & 4u));
    DB_ReleaseStringZoneOwnership(std::uint64_t{1} << 1u);
    assert(SL_FindString("gate3-zone-shared") == sharedString &&
        (SL_GetUser(sharedString) & 4u));
    DB_ReleaseStringZoneOwnership(std::uint64_t{1} << 2u);
    assert(!SL_FindString("gate3-zone-shared") &&
        !DB_HasRegisteredStringOwnership());

    const std::uint32_t defaultString = SL_GetString("gate3-default", 4u);
    DB_RegisterStringZoneOwnership(defaultString, 0u);
    DB_RegisterStringZoneOwnership(defaultString, 0u);
    const std::uint32_t temporaryZoneString = SL_GetString("gate3-zone", 4u);
    DB_RegisterStringZoneOwnership(temporaryZoneString, 1u);
    DB_ReleaseStringZoneOwnership(std::uint64_t{1} << 1u);
    assert(!SL_FindString("gate3-zone") &&
        SL_FindString("gate3-default") == defaultString &&
        DB_HasRegisteredStringOwnership());
    DB_UnregisterDefaultStringOwnership(defaultString);
    assert(SL_FindString("gate3-default") == defaultString);
    DB_UnregisterDefaultStringOwnership(defaultString);
    assert(!SL_FindString("gate3-default") &&
        !DB_HasRegisteredStringOwnership());
    std::puts("canonical-db-string-ownership shared-zones=retained default-refs=retained final-owner=retired");

    dvar_s *domainDvar = const_cast<dvar_s *>(Dvar_RegisterInt(
        "gate3_domain", 1, 0, 10, 0, "domain callback ABI probe"));
    Dvar_SetDomainFunc(domainDvar, ValidateDomainValue);
    Dvar_SetInt(domainDvar, 7);
    assert(g_domainCallCount == 2);
    assert(g_domainValue == 7);
    assert(domainDvar->current.integer == 7);

    Dvar_RegisterString("gate3_serverinfo", "server", DVAR_SERVERINFO, "info mask probe");
    Dvar_RegisterString("gate3_systeminfo", "system", DVAR_SYSTEMINFO, "info mask probe");
    Dvar_RegisterString("gate3_cheatinfo", "cheat", DVAR_CHEAT, "info mask probe");
    for (char mask : {char(DVAR_SERVERINFO), char(DVAR_SYSTEMINFO), char(0)})
    {
        const char *info = Dvar_InfoString(0, mask);
        assert((std::strstr(info, "gate3_serverinfo") != nullptr) == (mask == DVAR_SERVERINFO));
        assert((std::strstr(info, "gate3_systeminfo") != nullptr) == (mask == DVAR_SYSTEMINFO));
        assert(std::strstr(info, "gate3_archive") == nullptr);
        assert(std::strstr(info, "gate3_cheatinfo") == nullptr);
    }

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

#if KISAK_RUNTIME_COMMANDS_TEST
    // The web runtime resumes the native common.cpp command registration
    // immediately after the filesystem/profile boundary.
    Com_RegisterRuntimeCommands();
    assert(Cmd_FindCommand("quit") != nullptr);
    assert(Cmd_FindCommand("writeconfig") != nullptr);
    assert(Cmd_FindCommand("writeconfig")->function == Com_WriteConfig_f);
    assert(Cmd_FindCommand("writedefaults") != nullptr);
#endif

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
