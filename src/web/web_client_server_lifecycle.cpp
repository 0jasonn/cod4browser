#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include <client/cl_fastfile_config.h>
#include <client/client.h>
#include <database/database.h>
#include <database/db_initialization.h>
#include <gfx_d3d/r_asset_load.h>
#include <gfx_d3d/r_configuration.h>
#include <qcommon/cmd.h>
#include <qcommon/engine_lifecycle_trace.h>
#include <qcommon/qcommon.h>
#include <qcommon/threads.h>
#include <server/server.h>
#include <server/sv_public.h>
#include <script/scr_variable.h>
#include <script/scr_vm_runtime.h>
#include <stringed/stringed_hooks.h>
#include <universal/dvar.h>
#include <universal/com_memory.h>
#include <universal/physicalmemory.h>
#include <web/web_system.h>
#include <xanim/dobj_runtime_init.h>
#include <xanim/xanim_runtime_init.h>

#include <emscripten.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
GfxConfiguration g_rendererConfiguration{};
bool g_rendererConfigured = false;
bool g_clientLifecycleReady = false;
std::uint32_t g_remoteScreenDepth = 0;

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

} // namespace

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

void __cdecl Com_Restart()
{
    // This is the exact canonical DB publication reset from Com_Restart. The
    // xanim, DObj, collision, script, and sound portions do not yet have
    // runtime owners. Hunk ownership is live and resets in native order.
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

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_StartCanonicalDbRuntimeCheck()
{
    if (g_clientLifecycleReady)
        return;

    SetEngineLifecycleTraceObserver(PublishLifecycle);
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Canonical runtime: initializing language, Hunk, and shared runtime owners.\n");
    SEH_InitLanguage();
    Com_InitHunkMemory();
    Scr_InitVariables();
    Scr_Init();
    Scr_Settings(
        com_developer->current.integer || com_logfile->current.integer,
        com_developer_script->current.enabled,
        com_developer_script_abort_on_error->current.enabled);
    XAnimInit();
    DObjInit();
    SV_Init();

    // CL_InitRef is the canonical owner of the SP renderer configuration and
    // fastfile names. The native renderer consumes that configuration while
    // creating its device; WebGL2 is already platform-owned, so consume it at
    // the same boundary without mirroring the names in browser state.
    CL_InitRef();
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
    g_clientLifecycleReady = true;
    Web_Log(
        WebLogLevel::Info,
        "[kisakcod-web] Canonical renderer prerequisite-zone request completed.\n");
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_SubmitCanonicalCommand(
    const char *command)
{
    if (!g_clientLifecycleReady || !command)
        return 0;
    const std::size_t length = std::strlen(command);
    if (!length || length > 1023u)
        return 0;
    Cbuf_ExecuteBuffer(
        0,
        CL_ControllerIndexFromClientNum(0),
        command);
    return 1;
}
