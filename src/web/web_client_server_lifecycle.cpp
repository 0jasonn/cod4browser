#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include <client/cl_fastfile_config.h>
#include <client/client.h>
#include <database/database.h>
#include <database/db_registry_publication.h>
#include <database/db_initialization.h>
#include <gfx_d3d/r_asset_load.h>
#include <gfx_d3d/r_configuration.h>
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
#include <script/scr_vm_runtime.h>
#include <stringed/stringed_hooks.h>
#include <universal/dvar.h>
#include <universal/com_files.h>
#include <universal/com_memory.h>
#include <universal/physicalmemory.h>
#include <web/web_system.h>
#include <web/web_browser_bindings.h>
#include <web/web_client_server_lifecycle.h>
#include <xanim/dobj_runtime_init.h>
#include <xanim/xanim_runtime_init.h>

#include <emscripten.h>

#include <array>
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
    Com_Shutdown("EXE_SERVERQUIT");
}

void __cdecl Com_Restart()
{
    // Match the native restart boundary for the owners that are active in the
    // web runtime.  Collision and common-world assets are retired by
    // DB_ReleaseXAssets, whose removal hooks require these canonical owners to
    // have been shut down first.
    Com_ShutdownWorld();
    CM_Shutdown();
    DB_ReleaseXAssets();
    Hunk_Clear();
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

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_MountCanonicalRuntime()
{
    if (g_clientLifecycleReady)
        return;

    SetEngineLifecycleTraceObserver(PublishLifecycle);
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
        char browserProfile[] = "browser";
        Com_SetPlayerProfile(0, browserProfile);
    }
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Canonical runtime: initializing Hunk and shared runtime owners.\n");
    Com_InitHunkMemory();
    Hunk_InitDebugMemory();
    ProfLoad_Init();
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
    Com_ExecStartupConfigs(0, nullptr);
    // A fresh browser profile has no native config.cfg. Seed only absent core
    // controls in the canonical binding table; imported or future persisted
    // bindings always win, and movement remains owned by CL_Input/usercmd.
    InstallBrowserProfileDefaultBindings();
    // Native SP can hold the pregame menu for Bink playback. The browser
    // backend deliberately omits Bink, so select the canonical UI
    // auto-continue path after the retail defaults have been applied.
    Dvar_SetBoolByName("ui_autoContinue", true);

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
    g_clientLifecycleReady = true;
    Web_Log(
        WebLogLevel::Info,
        "[kisakcod-web] Canonical renderer prerequisite-zone request completed.\n");
}

#if KISAK_WEB_DIAGNOSTICS
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
    if (!g_clientLifecycleReady || !command)
        return 0;
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
