#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include <client/cl_fastfile_config.h>
#include <client/client.h>
#include <cgame/cg_main.h>
#include <cgame/cg_draw.h>
#include <cgame/cg_scoreboard.h>
#include <cgame/cg_servercmds.h>
#include <database/database.h>
#include <database/db_registry_publication.h>
#include <database/db_initialization.h>
#include <gfx_d3d/r_asset_load.h>
#include <gfx_d3d/r_configuration.h>
#include <gfx_d3d/r_savegame_image.h>
#include <gfx_d3d/r_material.h>
#include <qcommon/cmd.h>
#include <qcommon/com_world_runtime.h>
#include <qcommon/engine_lifecycle_trace.h>
#include <qcommon/qcommon.h>
#include <qcommon/com_playerprofile.h>
#include <qcommon/threads.h>
#include <server/server.h>
#include <server/sv_public.h>
#include <sound/snd_public.h>
#include <script/scr_variable.h>
#include <script/scr_stringlist.h>
#include <script/scr_vm_runtime.h>
#include <stringed/stringed_hooks.h>
#include <universal/dvar.h>
#include <universal/com_files.h>
#include <universal/com_memory.h>
#include <universal/physicalmemory.h>
#include <ui/ui.h>
#include <ui/keycodes.h>
#include <web/web_system.h>
#include <web/web_browser_bindings.h>
#include <web/web_client_server_lifecycle.h>
#include <xanim/dobj.h>
#include <xanim/dobj_runtime_init.h>
#include <xanim/xanim.h>
#include <xanim/xanim_runtime_init.h>
#include <buildnumber.h>

#include <emscripten.h>

#include <array>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{
GfxConfiguration g_rendererConfiguration{};
bool g_rendererConfigured = false;
bool g_clientLifecycleReady = false;
std::uint32_t g_remoteScreenDepth = 0;
constexpr std::size_t PENDING_COMMAND_CAPACITY = 8u;
std::array<std::array<char, 1024>, PENDING_COMMAND_CAPACITY>
    g_pendingCanonicalCommands{};
std::size_t g_pendingCanonicalCommandRead = 0u;
std::size_t g_pendingCanonicalCommandCount = 0u;

EM_JS(
    void,
    DispatchEngineLifecycle,
    (const char *stage,
     const char *name,
     std::uint32_t zoneCount,
     std::int32_t allocFlags,
     std::int32_t freeFlags,
     std::int32_t sync),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:engine-lifecycle", {
            detail: {
                stage: UTF8ToString(stage),
                name: name ? UTF8ToString(name) : "",
                zoneCount: zoneCount >>> 0,
                allocFlags: allocFlags | 0,
                freeFlags: freeFlags | 0,
                sync: sync | 0,
                asyncify: false,
                pthreads: false
            }
        }));
    });

EM_JS(
    void,
    DispatchCanonicalFilesystem,
    (const char *searchPaths, std::uint32_t searchPathCount, std::uint32_t archiveCount),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:canonical-filesystem", {
            detail: {
                state: "ready",
                searchPathCount: searchPathCount >>> 0,
                archiveCount: archiveCount >>> 0,
                searchPaths: UTF8ToString(searchPaths).split("\n").filter(Boolean),
                canonical: true,
                asyncify: false,
                browserOwnedSearchPaths: false
            }
        }));
    });

void PublishLifecycle(const EngineLifecycleTraceEvent &event, void *)
{
    DispatchEngineLifecycle(
        EngineLifecycleStageName(event.stage),
        event.name,
        event.zoneCount,
        event.allocFlags,
        event.freeFlags,
        event.sync);
}

void InstallBrowserProfileDefaultBindings()
{
    const auto lookup = [](std::uint32_t key) {
        return Key_GetBinding(0, key);
    };
    const auto setter = [](std::uint32_t key, const char *command) {
        Key_SetBinding(0, static_cast<int32_t>(key), const_cast<char *>(command));
    };
    const std::uint32_t installed = InstallWebBrowserDefaultBindings(lookup, setter);
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Browser profile installed %u missing canonical "
        "default bindings.\n", installed);
}

} // namespace

bool Web_TakePendingCanonicalCommand(char *command, std::size_t capacity)
{
    if (!command || !capacity || !g_pendingCanonicalCommandCount)
        return false;
    const auto &pending =
        g_pendingCanonicalCommands[g_pendingCanonicalCommandRead];
    I_strncpyz(command, pending.data(), static_cast<int>(capacity));
    g_pendingCanonicalCommandRead =
        (g_pendingCanonicalCommandRead + 1u) % PENDING_COMMAND_CAPACITY;
    --g_pendingCanonicalCommandCount;
    return true;
}

std::uint32_t Web_RendererEntityCount() noexcept
{
    return g_rendererConfigured ? g_rendererConfiguration.entCount : 0u;
}

bool __cdecl DB_ModFileExists()
{
    return false;
}

void __cdecl R_ConfigureRenderer(const GfxConfiguration *configuration)
{
    iassert(configuration);
    g_rendererConfiguration = *configuration;
    g_rendererConfigured = true;
}

void __cdecl Com_SyncThreads()
{
    iassert(Sys_IsMainThread());
    Sys_SyncDatabase();
}

void __cdecl DB_SyncXAssets()
{
    Sys_SyncDatabase();
}

void __cdecl Com_LoadBsp(char *)
{
    Com_Error(ERR_DROP,
        "Loose BSP loading is unavailable in the browser fastfile runtime");
}

void __cdecl Com_Shutdown(const char *)
{
    Com_Error(ERR_DROP,
        "Native process shutdown is unavailable inside the browser Worker");
}

void __cdecl Com_Quit_f()
{
    Web_RequestQuit();
}

void __cdecl Com_CleanupBsp()
{
    // Loose BSP loading is excluded above. Fastfile ClipMap/ComWorld owners
    // are retired by canonical Com_ShutdownInternal -> Com_Restart below.
    iassert(IsFastFileLoad());
}

void __cdecl Com_Restart()
{
    // Match the native restart boundary for the owners that are active in the
    // web runtime.  Collision and common-world assets are retired by
    // DB_ReleaseXAssets, whose removal hooks require these canonical owners to
    // have been shut down first.
    Com_ShutdownDObj();
    DObjShutdown();
    XAnimShutdown();
    Com_ShutdownWorld();
    CM_Shutdown();
    Hunk_Clear();
    DB_ReleaseXAssets();
    XAnimInit();
    DObjInit();
    Com_InitDObj();
}

void __cdecl R_BeginRemoteScreenUpdate()
{
    ++g_remoteScreenDepth;
}

void __cdecl R_EndRemoteScreenUpdate()
{
    iassert(g_remoteScreenDepth);
    --g_remoteScreenDepth;
}

void __cdecl R_PushRemoteScreenUpdate(int nesting)
{
    iassert(nesting >= 0);
    while (nesting-- > 0) R_BeginRemoteScreenUpdate();
}

int __cdecl R_PopRemoteScreenUpdate()
{
    const int nesting = static_cast<int>(g_remoteScreenDepth);
    while (g_remoteScreenDepth) R_EndRemoteScreenUpdate();
    return nesting;
}

void __cdecl Sys_BeginLoadThreadPriorities()
{
    // The engine is intentionally single-threaded in its dedicated Worker.
}

void __cdecl CL_ShutdownDemo() {}

void __cdecl CM_LoadMapData_LoadObj(const char *)
{
    Com_Error(
        ERR_DROP,
        "Loose BSP collision loading is unavailable in the browser fastfile runtime");
}

static void MountCanonicalRuntime()
{
    if (g_clientLifecycleReady)
        return;

    SetEngineLifecycleTraceObserver(PublishLifecycle);
    // Continue the native Com_Init order at the filesystem boundary. Key
    // commands must exist before profile config execution can replay binds.
    CL_InitKeyCommands();
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] FS_InitFilesystem begin.\n");
    FS_InitFilesystem();
    std::uint32_t searchPathCount = 0;
    std::uint32_t archiveCount = 0;
    std::string searchPathEvidence;
    for (const searchpath_s *search = fs_searchpaths; search; search = search->next)
    {
        ++searchPathCount;
        if (search->iwd)
        {
            ++archiveCount;
            searchPathEvidence += "iwd:";
            searchPathEvidence += search->iwd->iwdBasename;
        }
        else if (search->dir)
        {
            searchPathEvidence += "dir:";
            searchPathEvidence += search->dir->gamedir;
        }
        searchPathEvidence += '\n';
    }
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] FS_InitFilesystem complete: search paths=%u, IWDs=%u.\n",
        searchPathCount,
        archiveCount);
    DispatchCanonicalFilesystem(
        searchPathEvidence.c_str(), searchPathCount, archiveCount);

    // Resume the native post-filesystem startup sequence before any client or
    // server command can observe profile-owned config and save paths. Browser
    // storage has one local engine user until the launcher exposes profile
    // selection, so provide that platform identity through the canonical
    // profile owner rather than bypassing save construction.
    Com_InitPlayerProfiles(0);
    if (!Com_HasPlayerProfile())
    {
        if (!Com_EnsureInitialPlayerProfile(0, "browser"))
            Com_Error(ERR_FATAL, "Could not create the initial browser profile");
    }
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Canonical runtime: initializing Hunk and shared runtime owners.\n");
    Com_InitHunkMemory();
    Hunk_InitDebugMemory();
    ProfLoad_Init();
    Com_RegisterRuntimeCommands();
    const char *const buildVersion = va(
        "%s %s build %s %s", "CoD4", "1.0", getBuildNumber(), CPUSTRING);
    version = Dvar_RegisterString(
        "version", "", DVAR_ROM, "Game version");
    Dvar_SetString(version, buildVersion);
    shortversion = Dvar_RegisterString(
        "shortversion", "1.0", DVAR_ROM | DVAR_SERVERINFO,
        "Short game version");
    Scr_InitVariables();
    Scr_Init();
    Scr_Settings(
        com_developer->current.integer || com_logfile->current.integer,
        com_developer_script->current.enabled,
        com_developer_script_abort_on_error->current.enabled);
    XAnimInit();
    DObjInit();
    SV_Init();
    // The initial browser deployment is intentionally single-threaded. The
    // canonical dvar defaults to an SMP server thread, but the Worker platform
    // cannot run that native thread; leaving it enabled advances snapshot time
    // without executing G_RunFrame or script-owned movers.
    Dvar_SetBoolByName("sv_smp", false);
    CL_InitOnceForAllClients();
    CL_Init(0);
    // Native renderer/console output enters CL_ConsolePrint during startup and
    // lazily runs this canonical console owner before map scripts can call
    // SetSavedDvar. Browser logging bypasses CL_ConsolePrint, so complete the
    // same owner transition explicitly; otherwise Killhouse aborts at
    // con_typewriterColorBase before its spawn mover can descend.
    if (!con.initialized)
        Con_OneTimeInit();
    // Preserve the native post-client ownership transition:
    // SND_InitDriver -> CL_InitRenderer -> SND_Init. Native renderer
    // registration loads these prerequisite zones before sound consumes their
    // rawfiles. The browser backend performs that renderer-owned load explicitly
    // because WebGL2 was created by the platform host before this continuation.
    SND_InitDriver();
    iassert(g_rendererConfigured);
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Canonical runtime: loading renderer prerequisite zones.\n");
    R_LoadGraphicsAssetZones(g_rendererConfiguration);

    const PhysicalMemory *memory = PMem_GetState();
    if (memory->prim[1].allocName)
    {
        const char *initScope = memory->prim[1].allocName;
        PMem_EndAlloc(initScope, 1u);
        DB_SetInitializing(false);
    }
    if (!Sys_IsDatabaseReady())
    {
        // Resume the canonical DB-thread queue that was waiting for the
        // native $init scope to close, then prove it drained synchronously.
        Sys_NotifyDatabase();
        Sys_SyncDatabase();
    }

    // The temporary Com_Init prefix reaches profile/config startup before the
    // renderer prerequisite fastfiles exist, so its canonical `exec
    // default.cfg` cannot yet resolve the RawFile. Repeat that same canonical
    // config owner now that code_post_gfx/ui/common are published. This seeds
    // any retail defaults available from the asset database.
    char profileConfig[64];
    Com_BuildPlayerProfilePath(profileConfig, sizeof(profileConfig), "config.cfg");
    Com_ExecStartupConfigs(0, profileConfig);
    com_recommendedSet = Dvar_RegisterBool(
        "com_recommendedSet", false, DVAR_ARCHIVE,
        "Use recommended settings");
    Com_CheckSetRecommended(0);
    // A fresh browser profile has no native config.cfg. Seed only absent core
    // controls in the canonical binding table; imported or future persisted
    // bindings always win, and movement remains owned by CL_Input/usercmd.
    InstallBrowserProfileDefaultBindings();
    // Native SP can hold the pregame menu for Bink playback. The browser
    // backend deliberately omits Bink, so select the canonical UI
    // auto-continue path after the retail defaults have been applied.
    Dvar_SetBoolByName("ui_autoContinue", true);

    // Keep browser gameplay on wall time through slow frames. The native
    // 100 ms limit otherwise intentionally dilates time below 10 FPS, even
    // after the browser pump admits the full elapsed time. Retain its maximum
    // long-stall limit; fixedtime and scripted timescales remain canonical.
    Dvar_SetInt(com_maxFrameTime, 5000);

    // Complete the canonical renderer/sound transition only after the browser
    // DB queue has published the renderer prerequisites. In particular,
    // SND_InitEntChannels now resolves soundaliases/channels.def from the
    // renderer-owned code_post_gfx zone just as it does in native startup.
    CL_InitRenderer();
    iassert(!cls.soundStarted);
    cls.soundStarted = 1;
    SND_Init();

    // Native startup starts renderer-backed hunk users before a console map
    // command can execute. Retain that ownership phase after renderer and sound
    // are both live.
    R_BeginRemoteScreenUpdate();
    CL_StartHunkUsers();
    R_EndRemoteScreenUpdate();
    com_fullyInitialized = 1;
    g_clientLifecycleReady = true;
    Web_Log(
        WebLogLevel::Info,
        "[kisakcod-web] Canonical renderer prerequisite-zone request completed.\n");
}

EM_JS(void, ThrowCanonicalMountError, (const char *message), {
    throw new Error(UTF8ToString(message));
});

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_MountCanonicalRuntime()
{
    // Com_Init's setjmp frame has returned before the asynchronous import.
    // Keep a live canonical error boundary for this continuation so Com_Error
    // reaches the Worker as its actual message, not Emscripten's longjmp object.
    extern char com_errorMessage[4096];
    if (setjmp(*static_cast<jmp_buf *>(Sys_GetValue(2))))
    {
        ThrowCanonicalMountError(com_errorMessage);
        return;
    }
    MountCanonicalRuntime();
}

#if KISAK_WEB_DIAGNOSTICS
extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestUiState(int field)
{
    switch (field)
    {
    case 0: return cls.uiStarted;
    case 1: return Menu_Count(&uiInfo.uiDC);
    case 2: return uiInfo.uiDC.openMenuCount;
    case 3: return clientUIActives[0].keyCatchers;
    case 4: return UI_GetActiveMenu(0);
    case 5: return cl_paused ? cl_paused->current.integer : -1;
    case 6: return uiInfo.playerProfileCount;
    case 7: return uiInfo.savegameCount;
    case 8: return cgArray[0].objectives[15].state;
    case 9: return cgDC.menuCount;
    case 10: return CG_FadeObjectives(&cgArray[0]) > 0.0f;
    default: return -1;
    }
}

namespace
{
std::uint32_t HashDiagnosticName(const char *text)
{
    std::uint32_t hash = 2166136261u;
    while (*text)
    {
        char character = *text++;
        if (character >= 'A' && character <= 'Z')
            character += 'a' - 'A';
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 16777619u;
    }
    return hash;
}
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestMenuState(
    std::uint32_t nameHash)
{
    for (int index = 0; index < uiInfo.uiDC.menuCount; ++index)
    {
        menuDef_t *const menu = uiInfo.uiDC.Menus[index];
        if (!menu || !menu->window.name ||
            HashDiagnosticName(menu->window.name) != nameHash)
            continue;
        int state = 1;
        if (Menus_MenuIsInStack(&uiInfo.uiDC, menu)) state |= 2;
        if (Menu_IsVisible(&uiInfo.uiDC, menu)) state |= 4;
        return state;
    }
    for (int index = 0; index < cgDC.menuCount; ++index)
    {
        menuDef_t *const menu = cgDC.Menus[index];
        if (!menu || !menu->window.name ||
            HashDiagnosticName(menu->window.name) != nameHash)
            continue;
        int state = 1;
        if (Menus_MenuIsInStack(&cgDC, menu)) state |= 2;
        if (Menu_IsVisible(&cgDC, menu)) state |= 4;
        return state;
    }
    return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestObjectiveNotification(
    int state)
{
    if (!CL_IsCgameInitialized(0))
        return 0;
    if (state == 6)
    {
        const int oldTime = cgArray[0].time;
        cgArray[0].time = cgArray[0].scoreFadeTime + 601;
        CG_CheckHudObjectiveDisplay(0);
        const int faded = CG_FadeObjectives(&cgArray[0]) == 0.0f;
        cgArray[0].time = oldTime;
        return faded;
    }
    if (state < 0 || state > 5)
        return 0;
    char configString[128];
    Com_sprintf(configString, sizeof(configString),
        "\\state\\%d\\str\\Kisak web objective test", state);
    SV_SetConfigstring(26, configString);
    std::uint16_t &clientConfigString = clients[0].configstrings[26];
    if (clientConfigString)
        SL_RemoveRefToString(clientConfigString);
    clientConfigString = static_cast<std::uint16_t>(
        SL_GetString_(configString, 0, MT_TYPE_CONFIG_STRING));
    CG_ParseObjectiveChange(0, 26);
    cgArray[0].showScores = 0;
    cgArray[0].scoreFadeTime = 0;
    CG_MenuShowNotify(0, 5);
    return 1;
}

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_TestResumeGame()
{
    UI_SetActiveMenu(0, UIMENU_NONE);
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestConfigState(int field)
{
    if (field == 0)
        return Dvar_GetInt("kisak_ui_archive");
    if (field == 1)
    {
        char path[64];
        Com_BuildPlayerProfilePath(path, sizeof(path), "config.cfg");
        void *contents = nullptr;
        const int size = FS_ReadFile(path, &contents);
        if (contents) FS_FreeFile(static_cast<char *>(contents));
        return size;
    }
    if (field == 2)
    {
        const char *binding = Key_GetBinding(0, K_F9);
        return binding && !I_stricmp(binding, "+scores");
    }
    if (field == 3)
    {
        static const char *enumValues[] = {"first", "second", nullptr};
        const dvar_s *values[] = {
            Dvar_RegisterBool("kisak_test_bool", false, DVAR_ROM, "test"),
            Dvar_RegisterInt("kisak_test_int", 2, 0, 4,
                DVAR_CHEAT, "test"),
            Dvar_RegisterFloat("kisak_test_float", 0.5f, 0.0f, 1.0f,
                DVAR_ARCHIVE, "test"),
            Dvar_RegisterString("kisak_test_string", "value",
                DVAR_LATCH, "test"),
            Dvar_RegisterEnum("kisak_test_enum", enumValues, 1, 0, "test"),
            Dvar_RegisterVec3("kisak_test_vector", 1.0f, 2.0f, 3.0f,
                -4.0f, 4.0f, 0, "test"),
            Dvar_RegisterColor("kisak_test_color", 0.1f, 0.2f, 0.3f,
                0.4f, 0, "test"),
        };
        const std::uint8_t types[] = {DVAR_TYPE_BOOL, DVAR_TYPE_INT,
            DVAR_TYPE_FLOAT, DVAR_TYPE_STRING, DVAR_TYPE_ENUM,
            DVAR_TYPE_FLOAT_3, DVAR_TYPE_COLOR};
        int mask = 0;
        for (std::size_t index = 0; index < std::size(values); ++index)
            if (values[index] && values[index]->type == types[index])
                mask |= 1 << index;
        if ((values[0]->flags & DVAR_ROM) &&
            (values[1]->flags & DVAR_CHEAT) &&
            (values[2]->flags & DVAR_ARCHIVE) &&
            (values[3]->flags & DVAR_LATCH))
            mask |= 1 << 7;
        return mask;
    }
    if (field == 4)
        return (Cmd_FindCommand("bind") ? 1 : 0) |
            (Key_StringToKeynum("F9") << 8);
    return -1;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestProfileState(int operation)
{
    constexpr const char *PROFILE_A = "kisak_web_test_a";
    constexpr const char *PROFILE_B = "kisak_web_test_b";
    Dvar_RegisterInt("kisak_profile_value", 0, 0, 1000, DVAR_ARCHIVE,
        "Diagnostic profile-isolation value");
    const auto activeProfile = [=]() {
        if (!I_stricmp(com_playerProfile->current.string, PROFILE_A)) return 1;
        if (!I_stricmp(com_playerProfile->current.string, PROFILE_B)) return 2;
        if (!I_stricmp(com_playerProfile->current.string, "browser")) return 3;
        return 0;
    };
    const auto selectProfile = [=](const char *name) {
        UI_AddPlayerProfiles();
        const int index = UI_GetPlayerProfileListIndexFromName(name);
        if (index < 0) return false;
        UI_FeederSelection(0, 24.0f, index);
        UI_LoadPlayerProfile(0);
        return !I_stricmp(com_playerProfile->current.string, name);
    };

    if (operation == 0)
    {
        UI_AddPlayerProfiles();
        int state = activeProfile() << 8;
        if (UI_GetPlayerProfileListIndexFromName("browser") >= 0) state |= 1;
        if (UI_GetPlayerProfileListIndexFromName(PROFILE_A) >= 0) state |= 2;
        if (UI_GetPlayerProfileListIndexFromName(PROFILE_B) >= 0) state |= 4;
        return state;
    }
    if (operation == 1 || operation == 2)
    {
        const char *name = operation == 1 ? PROFILE_A : PROFILE_B;
        UI_AddPlayerProfiles();
        if (UI_GetPlayerProfileListIndexFromName(name) < 0)
        {
            Dvar_SetString(
                const_cast<dvar_s *>(ui_playerProfileNameNew), name);
            UI_CreatePlayerProfile();
        }
        return Com_IsValidPlayerProfileDir(name);
    }
    if (operation == 3) return selectProfile(PROFILE_A);
    if (operation == 4) return selectProfile(PROFILE_B);
    if (operation == 5)
    {
        if (!I_stricmp(com_playerProfile->current.string, PROFILE_A)) return -2;
        UI_AddPlayerProfiles();
        const int index = UI_GetPlayerProfileListIndexFromName(PROFILE_A);
        if (index < 0) return 1;
        UI_FeederSelection(0, 24.0f, index);
        UI_DeletePlayerProfile();
        return !Com_IsValidPlayerProfileDir(PROFILE_A);
    }
    if (operation == 6) return activeProfile();
    if (operation == 7)
    {
        char path[64];
        Com_BuildPlayerProfilePath(path, sizeof(path), "config.cfg");
        char *contents = nullptr;
        if (FS_ReadFile(path, reinterpret_cast<void **>(&contents)) < 0)
            return 0;
        int value = 0;
        if (std::strstr(contents, "kisak_profile_value"))
        {
            if (std::strstr(contents, "\"101\"")) value = 101;
            if (std::strstr(contents, "\"202\"")) value = 202;
        }
        FS_FreeFile(contents);
        return value;
    }
    if (operation == 8)
    {
        char *contents = nullptr;
        if (FS_ReadFile("profiles/active.txt",
                reinterpret_cast<void **>(&contents)) < 0)
            return 0;
        const int value = !I_stricmp(contents, PROFILE_A) ? 1
            : !I_stricmp(contents, PROFILE_B) ? 2 : 0;
        FS_FreeFile(contents);
        return value;
    }
    if (operation == 9) return Dvar_GetInt("kisak_profile_value");
    if (operation == 10) return selectProfile("browser");
    return -1;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestSaveState(int operation)
{
    if (operation == 12) return uiInfo.sshotImage && uiInfo.sshotImageName[0];
    constexpr const char *SAVE_NAME = "kisak_web_ui_test";
    const auto saveIndex = [=]() {
        UI_LoadSavegames(0);
        return UI_SavegameIndexFromFilename(SAVE_NAME);
    };
    const auto selectSave = [=]() {
        const int index = saveIndex();
        if (index < 0) return false;
        UI_FeederSelection(0, 16.0f, index);
        return !I_stricmp(ui_savegame->current.string, SAVE_NAME);
    };

    if (operation == 0)
    {
        UI_LoadSavegames(0);
        return uiInfo.savegameCount;
    }
    if (operation == 1) return saveIndex() + 1;
    if (operation == 2) return selectSave();
    if (operation == 13)
    {
        const int index = UI_SavegameIndexFromFilename(SAVE_NAME);
        if (index < 0) return 0;
        const SavegameInfo &save = uiInfo.savegameList[uiInfo.savegameStatus.displaySavegames[index]];
        Material *handle = nullptr;
        return (!I_stricmp(UI_FeederItemText(0, nullptr, 16, index, 1, &handle), save.date) ? 1 : 0) |
            (!I_stricmp(UI_GetSavegameInfo(), save.savegameInfoText) ? 2 : 0);
    }
    if (operation == 3)
    {
        if (!selectSave()) return 0;
        // Menu script argument streams retain their trailing separator. The
        // shared String_Parse owner treats a fully exhausted stream as absent.
        const char *args = "Loadgame ;";
        UI_RunMenuScript(0, &args, "Loadgame");
        return 1;
    }
    if (operation == 4)
    {
        if (!selectSave()) return 0;
        UI_DelSavegame();
        return saveIndex() < 0;
    }
    if (operation == 5)
    {
        UI_LoadSavegames(0);
        return uiInfo.savegameCount == 0 && !uiInfo.savegameName[0] &&
            !ui_savegame->current.string[0];
    }
    if (operation == 6)
    {
        char path[64];
        Com_BuildPlayerProfilePath(
            path, sizeof(path), "save/%s.svg", SAVE_NAME);
        int file = 0;
        const int size = static_cast<int>(FS_FOpenFileRead(path, &file));
        if (file) FS_FCloseFile(file);
        return file ? size : 0;
    }
    if (operation == 7)
    {
        const int index = saveIndex();
        if (index < 0) return 0;
        const int slot = uiInfo.savegameStatus.displaySavegames[index];
        return slot >= 0 && slot < uiInfo.savegameCount &&
            uiInfo.savegameList[slot].imageName != nullptr;
    }
    if (operation == 8)
        return std::strstr(Dvar_GetString("sv_lastSaveGame"), SAVE_NAME) != nullptr;
    if (operation == 9)
    {
        const int index = saveIndex();
        if (index < 0) return 0;
        const SavegameInfo &save = uiInfo.savegameList[uiInfo.savegameStatus.displaySavegames[index]];
        return (save.mapName && std::strstr(save.mapName, "airplane") ? 1 : 0) |
            (save.date && save.date[0] ? 2 : 0) |
            (save.time && save.time[0] ? 4 : 0) |
            (save.tm.tm_year >= 126 && save.savegameName && save.savegameName[0] ? 8 : 0);
    }
    if (operation == 10 || operation == 11 || operation == 14)
    {
        char path[64];
        Com_BuildPlayerProfilePath(path, sizeof(path), "save/%s.jpg", operation == 14 ? "airplane" : SAVE_NAME);
        if (operation == 11) return Material_RegisterRawImage(path, 3) != nullptr;
        int file = 0;
        const unsigned size = FS_FOpenFileRead(path, &file);
        if (!file) return 0;
        std::vector<uint8_t> jpeg(size <= SAVEGAME_JPEG_MAX_BYTES ? size : 0);
        const unsigned read = jpeg.empty() ? 0 : FS_Read(jpeg.data(), size, file);
        FS_FCloseFile(file);
        return read == size && R_IsSaveGameJpeg(jpeg) ? size : -1;
    }
    return -1;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_CanonicalFsFileSize(
    const char *logicalPath)
{
    if (!g_clientLifecycleReady || !logicalPath)
        return -1;
    int file = 0;
    const int size = static_cast<int>(FS_FOpenFileRead(logicalPath, &file));
    if (!file)
        return -1;
    FS_FCloseFile(file);
    return size;
}

extern "C" EMSCRIPTEN_KEEPALIVE std::uint32_t KisakWeb_CanonicalFsReadHash(
    const char *logicalPath, std::uint32_t offset, std::uint32_t length)
{
    if (!g_clientLifecycleReady || !logicalPath || length > 1024u * 1024u)
        return 0u;
    int file = 0;
    const std::uint32_t size = FS_FOpenFileRead(logicalPath, &file);
    if (!file || offset > size || length > size - offset ||
        (offset && FS_Seek(file, static_cast<int>(offset), 2) != 0))
    {
        if (file)
            FS_FCloseFile(file);
        return 0u;
    }
    std::uint32_t hash = 2166136261u;
    std::uint8_t bytes[4096];
    std::uint32_t remaining = length;
    while (remaining)
    {
        const std::uint32_t chunk = remaining < sizeof(bytes)
            ? remaining
            : static_cast<std::uint32_t>(sizeof(bytes));
        if (FS_Read(bytes, chunk, file) != chunk)
        {
            FS_FCloseFile(file);
            return 0u;
        }
        for (std::uint32_t index = 0; index < chunk; ++index)
        {
            hash ^= bytes[index];
            hash *= 16777619u;
        }
        remaining -= chunk;
    }
    FS_FCloseFile(file);
    return hash;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_CanonicalFsListCount(
    const char *logicalPath, const char *extension)
{
    if (!g_clientLifecycleReady || !logicalPath || !extension)
        return -1;
    int count = 0;
    const char **files = FS_ListFiles(
        logicalPath, extension, FS_LIST_ALL, &count);
    FS_FreeFileList(files);
    return count;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_CanonicalFsWriteRename(
    const char *temporaryPath, const char *finalPath,
    const std::uint8_t *bytes, std::uint32_t length)
{
    if (!g_clientLifecycleReady || !temporaryPath || !*temporaryPath ||
        !finalPath || !*finalPath || (!bytes && length) ||
        length > 1024u * 1024u)
    {
        return 0;
    }
    int file = FS_FOpenFileWrite(temporaryPath);
    if (!file)
        return 0;
    const bool wrote = FS_Write(
        reinterpret_cast<const char *>(bytes), length, file) == length;
    FS_FCloseFile(file);
    if (!wrote)
        return 0;
    FS_Rename(const_cast<char *>(temporaryPath), fs_gamedir,
        const_cast<char *>(finalPath), fs_gamedir);
    int verify = 0;
    const std::uint32_t size = FS_FOpenFileRead(finalPath, &verify);
    if (verify)
        FS_FCloseFile(verify);
    return verify && size == length ? 1 : 0;
}

#endif

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_SubmitCanonicalCommand(
    const char *command)
{
    if (!command)
        return 0;
#if !KISAK_WEB_DIAGNOSTICS
    if (!g_clientLifecycleReady)
        return 0;
#endif
    const std::size_t length = std::strlen(command);
    if (!length || length > 1023u)
        return 0;

    // RPC callbacks run between cooperative engine frames. Retain the text at
    // this platform boundary and let CommandTask execute it at the next frame
    // boundary, outside Cbuf_Execute's global nesting guard. A synchronous map
    // load constructs server info while that guard must remain available to
    // canonical config/command code.
    if (g_pendingCanonicalCommandCount == PENDING_COMMAND_CAPACITY)
        return 0;
    const std::size_t write =
        (g_pendingCanonicalCommandRead + g_pendingCanonicalCommandCount) %
        PENDING_COMMAND_CAPACITY;
    std::memcpy(g_pendingCanonicalCommands[write].data(), command, length + 1u);
    ++g_pendingCanonicalCommandCount;
    return 1;
}
