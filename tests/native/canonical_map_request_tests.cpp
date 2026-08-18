#include <database/db_zone_loading.h>
#include <client/client.h>
#include <game/save_error.h>
#include <qcommon/cmd.h>
#include <qcommon/engine_lifecycle_trace.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <server/sv_map_assets.h>
#include <server/sv_map_command.h>
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
XZoneInfo g_zoneRequest{};
std::uint32_t g_zoneCount = 0;
std::int32_t g_zoneSync = -1;
std::array<char, 64> g_spawnMap{};
int g_spawnSavegame = -1;
int g_spawnCount = 0;
std::array<const char *, 6> g_clientStages{};
std::size_t g_clientStageCount = 0;
struct CapturedLifecycleEvent
{
    EngineLifecycleStage stage{};
    std::array<char, 64> name{};
    std::uint32_t zoneCount = 0;
    std::int32_t allocFlags = 0;
    std::int32_t freeFlags = 0;
    std::int32_t sync = 0;
};
std::array<CapturedLifecycleEvent, 16> g_lifecycleEvents{};
std::size_t g_lifecycleEventCount = 0;

void ClientStage(const char *stage)
{
    assert(g_clientStageCount < g_clientStages.size());
    g_clientStages[g_clientStageCount++] = stage;
}

void CaptureLifecycle(
    const EngineLifecycleTraceEvent &event, void *)
{
    assert(g_lifecycleEventCount < g_lifecycleEvents.size());
    CapturedLifecycleEvent &captured =
        g_lifecycleEvents[g_lifecycleEventCount++];
    captured.stage = event.stage;
    std::snprintf(captured.name.data(), captured.name.size(), "%s", event.name);
    captured.zoneCount = event.zoneCount;
    captured.allocFlags = event.allocFlags;
    captured.freeFlags = event.freeFlags;
    captured.sync = event.sync;
}
} // namespace

std::uint32_t com_errorPrintsCount = 0;
int com_inServerFrame = 0;
const dvar_t *sv_cheats = nullptr;
const dvar_t *sv_loadMyChanges = nullptr;
const dvar_t *cg_drawCrosshair = nullptr;
const dvar_t *cg_subtitles = nullptr;
const dvar_t *cl_yawspeed = nullptr;
const dvar_t *cl_pitchspeed = nullptr;
const dvar_t *cl_anglespeedkey = nullptr;
const dvar_t *nextmap = nullptr;
const dvar_t *loc_language = nullptr;
const dvar_t *loc_translate = nullptr;
const dvar_t *loc_warnings = nullptr;
const dvar_t *loc_warningsAsErrors = nullptr;
int cl_controller_in_use = 0;

void __cdecl Vec3Mad(const float *start, float scale, const float *direction,
    float *result)
{
    for (int index = 0; index < 3; ++index)
        result[index] = start[index] + scale * direction[index];
}
void __cdecl AngleVectors(const float *, float *, float *, float *) {}
float __cdecl Q_fabs(float value) { return value < 0.0f ? -value : value; }
const FxEffectDef *__cdecl FX_Register(const char *) { return nullptr; }
XModel *__cdecl R_RegisterModel(const char *) { return nullptr; }
Material *__cdecl Material_RegisterHandle(const char *, int) { return nullptr; }
snd_alias_list_t *__cdecl Com_FindSoundAlias(const char *) { return nullptr; }
void __cdecl G_AddCommandNotify(volatile std::uint16_t) {}
void Scr_Error(const char *) { std::abort(); }
void __cdecl MemFile_WriteData(MemoryFile *, int, const void *) {}
void __cdecl MemFile_WriteCString(MemoryFile *, const char *) {}
const char *__cdecl MemFile_ReadCString(MemoryFile *) { return ""; }
void __cdecl MemFile_ReadData(MemoryFile *, int, std::uint8_t *) {}

void DB_InitThread() {}
void DB_LoadXAssets(
    XZoneInfo *zoneInfo, std::uint32_t zoneCount, std::int32_t sync)
{
    assert(zoneInfo);
    assert(zoneCount == 1);
    g_zoneRequest = zoneInfo[0];
    g_zoneCount = zoneCount;
    g_zoneSync = sync;
}
bool DB_ModFileExists() { return false; }

void __cdecl SV_SpawnServer(const char *mapname, int savegame)
{
    ++g_spawnCount;
    std::snprintf(g_spawnMap.data(), g_spawnMap.size(), "%s", mapname);
    g_spawnSavegame = savegame;
    SV_LoadLevelAssets(mapname);
}

void __cdecl CL_ShutdownDemo() {}
void Con_Init() { ClientStage("Con_Init"); }
void CL_InitInput() { ClientStage("CL_InitInput"); }
void Campaign_RegisterDvars() { ClientStage("Campaign_RegisterDvars"); }
void CL_InitRef() { ClientStage("CL_InitRef"); }
void SCR_Init() { ClientStage("SCR_Init"); }
void CL_ForwardToServer_f() {}
void CL_Disconnect_f() {}
void CL_PlayDemo_f() {}
void CL_Vid_Restart_f() {}
void CL_Snd_Restart_f() {}
void CL_Record_f() {}
void CL_StopRecord_f() {}
void CL_PlayLogo_f() {}
void CL_PlayCinematic_f() {}
void CL_PlayUnskippableCinematic_f() {}
void CL_Pause_f() {}
void CL_VoidCommand() {}
void CL_startMultiplayer_f() {}
void CL_ShellExecute_URL_f() {}
void CL_IncAnimWeight_f() {}
void CL_DecAnimWeight_f() {}
void XModelDumpInfo() {}
void CL_StopControllerRumbles() {}
int __cdecl CL_ControllerIndexFromClientNum(int) { return 0; }
int __cdecl ExtractMapStringFromSaveGame(const char *, char *) { return 0; }
void __cdecl ShowLoadErrorsSummary(const char *, unsigned int) {}
void __cdecl FS_ConvertPath(char *path)
{
    for (; *path; ++path)
    {
        if (*path == '\\') *path = '/';
    }
}
void G_SaveError(errorParm_t, SaveErrorType, const char *, ...)
{
    std::abort();
}

void __cdecl SV_WaitServer()
{
    assert(!com_inServerFrame);
}

void Sys_InitializeCriticalSections() {}
void Sys_EnterCriticalSection(int) {}
void Sys_LeaveCriticalSection(int) {}
void Sys_LockWrite(FastCriticalSection *section)
{
    section->writeCount = section->writeCount + 1;
}
void Sys_UnlockWrite(FastCriticalSection *section)
{
    section->writeCount = section->writeCount - 1;
}
std::uint32_t Sys_GetCpuCount() { return 1; }
void Sys_InitMainThread() { g_mainThreadInitialized = true; }
bool Sys_IsMainThread() { return g_mainThreadInitialized; }
bool Sys_IsRenderThread() { return false; }
bool Sys_IsServerThread() { return false; }
void Sys_SetValue(int index, void *value)
{
    g_values[static_cast<std::size_t>(index)] = value;
}
void *Sys_GetValue(int index)
{
    return g_values[static_cast<std::size_t>(index)];
}
void NET_Sleep(int) {}
double __cdecl ConvertToMB(int bytes)
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}
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
std::uint32_t __cdecl Sys_MillisecondsRaw() { return 7; }
void __cdecl Sys_Print(const char *) {}

void Sys_Error(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::abort();
}

void MyAssertHandler(
    const char *filename, int line, int type, const char *format, ...)
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
    loc_language = Dvar_RegisterInt(
        "loc_language", 0, 0, 15, DVAR_ARCHIVE | DVAR_LATCH, "Language");
    loc_translate = Dvar_RegisterBool(
        "loc_translate", true, DVAR_LATCH, "Enable translations");
    loc_warnings = Dvar_RegisterBool(
        "loc_warnings", false, 0, "Enable localization warnings");
    loc_warningsAsErrors = Dvar_RegisterBool(
        "loc_warningsAsErrors", false, 0, "Treat localization warnings as errors");
    char commandLine[] = "";
    Com_Init(commandLine);
    SetEngineLifecycleTraceObserver(CaptureLifecycle);
    CL_Init(0);
    ClientStage("CL_Init returned");
    constexpr std::array<const char *, 6> expectedClientStages{{
        "Con_Init",
        "CL_InitInput",
        "Campaign_RegisterDvars",
        "CL_InitRef",
        "SCR_Init",
        "CL_Init returned",
    }};
    assert(g_clientStageCount == expectedClientStages.size());
    for (std::size_t index = 0; index < expectedClientStages.size(); ++index)
        assert(std::strcmp(g_clientStages[index], expectedClientStages[index]) == 0);
    assert(clientUIActives[0].isRunning == 1);
    assert(clients[0].usingAds == 0);
    assert(Dvar_FindVar("cl_noprint"));
    assert(Dvar_FindVar("nextmap"));
    assert(Cmd_FindCommand("disconnect"));

    sv_cheats = Dvar_FindVar("sv_cheats");
    assert(sv_cheats);
    sv_loadMyChanges = Dvar_RegisterBool("sv_loadMyChanges", false, 0, "test");
    SV_RegisterMapCommands();

    Cbuf_ExecuteBuffer(0, 0, "map KiLlHoUsE");

    constexpr std::array<EngineLifecycleStage, 10> expectedLifecycle{{
        EngineLifecycleStage::ClientInitBegin,
        EngineLifecycleStage::ClientInputBegin,
        EngineLifecycleStage::ClientInputEnd,
        EngineLifecycleStage::ClientRendererConfigure,
        EngineLifecycleStage::ClientInitComplete,
        EngineLifecycleStage::MapCommandAccepted,
        EngineLifecycleStage::MapNameSelected,
        EngineLifecycleStage::MapSpawnBegin,
        EngineLifecycleStage::MapLoadingBegin,
        EngineLifecycleStage::MapZoneRequestConstructed,
    }};
    assert(g_lifecycleEventCount == expectedLifecycle.size());
    for (std::size_t index = 0; index < expectedLifecycle.size(); ++index)
        assert(g_lifecycleEvents[index].stage == expectedLifecycle[index]);
    assert(std::strcmp(g_lifecycleEvents[6].name.data(), "killhouse") == 0);
    assert(std::strcmp(g_lifecycleEvents[7].name.data(), "killhouse") == 0);
    assert(std::strcmp(g_lifecycleEvents[8].name.data(), "killhouse") == 0);
    assert(std::strcmp(g_lifecycleEvents[9].name.data(), "killhouse") == 0);
    assert(g_lifecycleEvents[9].zoneCount == 1);
    assert(g_lifecycleEvents[9].allocFlags == 8);
    assert(g_lifecycleEvents[9].freeFlags == 8);
    assert(g_lifecycleEvents[9].sync == 0);
    ClearEngineLifecycleTraceObserver();

    assert(g_spawnCount == 1);
    assert(std::strcmp(g_spawnMap.data(), "killhouse") == 0);
    assert(g_spawnSavegame == 0);
    assert(sv_cheats->current.enabled);
    assert(g_zoneCount == 1);
    assert(g_zoneSync == 0);
    assert(std::strcmp(g_zoneRequest.name, "killhouse") == 0);
    assert(g_zoneRequest.allocFlags == 8);
    assert(g_zoneRequest.freeFlags == 8);

    std::puts(
        "canonical-client-map stages=Con_Init,CL_InitInput,Campaign_RegisterDvars,"
        "CL_InitRef,SCR_Init,CL_Init-returned command=map input=KiLlHoUsE "
        "normalized=killhouse savegame=0 zone=killhouse alloc=8 free=8 sync=0");
    return 0;
}
