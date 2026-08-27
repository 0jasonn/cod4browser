#include <client/cl_input.h>
#include <client/cl_scrn.h>
#include <client/client.h>
#include <cgame/cg_local.h>
#include <cgame/cg_main.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <server/server.h>
#include <qcommon/system.h>
#include <universal/dvar.h>
#include <ui/ui.h>
#include <web/web_client_server_lifecycle.h>
#include <web/web_filesystem.h>
#include <web/web_frame_profile.h>
#include <web/web_renderer.h>
#include <web/web_system.h>

#include <algorithm>
#include <cstdint>
#include <csetjmp>

#if KISAK_WEB_DIAGNOSTICS
#include <emscripten/emscripten.h>
#endif

#if KISAK_WEB_DIAGNOSTICS
namespace
{
WebFrameProfileSample g_frameProfileSample{};
std::uint32_t g_frameProfileRemaining = 0u;
bool g_frameProfilePumpActive = false;

EM_JS(void, DispatchFrameProfile,
    (std::uint32_t pumpTick, std::uint32_t contextGeneration,
     std::uint32_t viewSubmissionGeneration, bool gameplayFrame,
     bool rendererSubmitted, bool gpuTimingsAvailable, bool gpuQueryIssued,
     bool gpuQueryDropped,
     double filesystemMs, double commandMs, double serverMs,
     double clientOnceMs, double commandBufferMs, double clientFrameMs,
     double cgameFrameMs, double sceneBuildMs, double rendererFrontendMs,
     double soundMs, double rendererBackendMs, double totalMs,
     double rendererSetupMs, double lodMs, double sunShadowPrepareMs,
     double sunShadowDrawMs, double spotShadowPrepareMs,
     double spotShadowDrawMs, double skyMs, double worldMs,
     double staticModelsMs, double dynamicModelsMs, double fxModelsMs,
     double particlesMs, double marksMs, double uiMs, double postProcessMs,
     double bufferUploadMs, double textureUploadMs,
     double worldSurfacesSubmitted, double worldSurfacesDrawn,
     double staticModelInstancesRetained, double staticModelInstanceDraws,
     double dynamicBatchesDrawn, double fxModelBatchesDrawn,
     double particleBatchesDrawn, double markBatchesDrawn,
     double worldDrawCalls, double staticModelDrawCalls,
     double dynamicDrawCalls, double fxDrawCalls, double shadowDrawCalls,
     double uiDrawCalls, double postProcessDrawCalls, double queryDrawCalls,
     double resolveBlits, double submittedIndices, double submittedTriangles,
     double textureBindCalls, double programSwitches,
     double bufferUploadBytes, double textureUploadBytes,
     double unmeasuredTextureUploads, double lodChanges,
     double shadowCasterDraws), {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:frame-profile", {
            detail: {
                kind: "frame",
                pumpTick: pumpTick >>> 0,
                contextGeneration: contextGeneration >>> 0,
                viewSubmissionGeneration: viewSubmissionGeneration >>> 0,
                gameplayFrame: Boolean(gameplayFrame),
                rendererSubmitted: Boolean(rendererSubmitted),
                cpu: {
                    filesystemMs, commandMs, serverMs, clientOnceMs,
                    commandBufferMs, clientFrameMs, cgameFrameMs,
                    sceneBuildMs, rendererFrontendMs, soundMs,
                    rendererBackendMs, totalMs
                },
                renderer: {
                    setupMs: rendererSetupMs, lodMs, sunShadowPrepareMs,
                    sunShadowDrawMs, spotShadowPrepareMs, spotShadowDrawMs,
                    skyMs, worldMs, staticModelsMs,
                    dynamicModelsMs, fxModelsMs, particlesMs, marksMs,
                    uiMs, postProcessMs, bufferUploadMs, textureUploadMs
                },
                gpu: {
                    timingsAvailable: Boolean(gpuTimingsAvailable),
                    queryIssued: Boolean(gpuQueryIssued),
                    queryDropped: Boolean(gpuQueryDropped)
                },
                counters: {
                    worldSurfacesSubmitted, worldSurfacesDrawn,
                    staticModelInstancesRetained, staticModelInstanceDraws,
                    dynamicBatchesDrawn, fxModelBatchesDrawn,
                    particleBatchesDrawn, markBatchesDrawn,
                    worldDrawCalls, staticModelDrawCalls, dynamicDrawCalls,
                    fxDrawCalls, shadowDrawCalls, uiDrawCalls,
                    postProcessDrawCalls, queryDrawCalls, resolveBlits,
                    submittedIndices, submittedTriangles, textureBindCalls,
                    programSwitches, bufferUploadBytes, textureUploadBytes,
                    unmeasuredTextureUploads, lodChanges, shadowCasterDraws
                }
            }
        }));
    });

EM_JS(void, DispatchFrameProfileGpuResult,
    (std::uint32_t pumpTick, std::uint32_t contextGeneration,
     std::uint32_t viewSubmissionGeneration, double gpuMilliseconds,
     std::uint32_t queryLagFrames, const char *status), {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:frame-profile", {
            detail: {
                kind: "gpu-result",
                pumpTick: pumpTick >>> 0,
                contextGeneration: contextGeneration >>> 0,
                viewSubmissionGeneration: viewSubmissionGeneration >>> 0,
                gpu: {
                    status: UTF8ToString(status),
                    backendDrawMs: gpuMilliseconds,
                    queryLagFrames: queryLagFrames >>> 0
                }
            }
        }));
    });
}

bool WebFrameProfile_BeginPump(std::uint32_t pumpTick) noexcept
{
    g_frameProfilePumpActive = g_frameProfileRemaining != 0u;
    if (!g_frameProfilePumpActive) return false;
    g_frameProfileSample = {};
    g_frameProfileSample.pumpTick = pumpTick;
    return true;
}

WebFrameProfileSample *WebFrameProfile_Current() noexcept
{
    return g_frameProfilePumpActive ? &g_frameProfileSample : nullptr;
}

double WebFrameProfile_Now() noexcept { return emscripten_get_now(); }

void WebFrameProfile_EndPump(bool gameplayFrame, bool rendererSubmitted)
{
    if (!g_frameProfilePumpActive) return;
    g_frameProfilePumpActive = false;
    g_frameProfileSample.gameplayFrame = gameplayFrame;
    g_frameProfileSample.rendererSubmitted = rendererSubmitted;
    const WebFrameProfileSample &s = g_frameProfileSample;
    DispatchFrameProfile(
        s.pumpTick, s.contextGeneration, s.viewSubmissionGeneration,
        s.gameplayFrame, s.rendererSubmitted, s.gpuTimingsAvailable,
        s.gpuQueryIssued, s.gpuQueryDropped,
        s.filesystemMs, s.commandMs, s.serverMs, s.clientOnceMs,
        s.commandBufferMs, s.clientFrameMs, s.cgameFrameMs, s.sceneBuildMs,
        s.rendererFrontendMs, s.soundMs, s.rendererBackendMs, s.totalMs,
        s.rendererSetupMs, s.lodMs, s.sunShadowPrepareMs,
        s.sunShadowDrawMs, s.spotShadowPrepareMs, s.spotShadowDrawMs,
        s.skyMs, s.worldMs, s.staticModelsMs, s.dynamicModelsMs,
        s.fxModelsMs, s.particlesMs, s.marksMs, s.uiMs, s.postProcessMs,
        s.bufferUploadMs, s.textureUploadMs,
        static_cast<double>(s.worldSurfacesSubmitted),
        static_cast<double>(s.worldSurfacesDrawn),
        static_cast<double>(s.staticModelInstancesRetained),
        static_cast<double>(s.staticModelInstanceDraws),
        static_cast<double>(s.dynamicBatchesDrawn),
        static_cast<double>(s.fxModelBatchesDrawn),
        static_cast<double>(s.particleBatchesDrawn),
        static_cast<double>(s.markBatchesDrawn),
        static_cast<double>(s.worldDrawCalls),
        static_cast<double>(s.staticModelDrawCalls),
        static_cast<double>(s.dynamicDrawCalls),
        static_cast<double>(s.fxDrawCalls),
        static_cast<double>(s.shadowDrawCalls),
        static_cast<double>(s.uiDrawCalls),
        static_cast<double>(s.postProcessDrawCalls),
        static_cast<double>(s.queryDrawCalls),
        static_cast<double>(s.resolveBlits),
        static_cast<double>(s.submittedIndices),
        static_cast<double>(s.submittedTriangles),
        static_cast<double>(s.textureBindCalls),
        static_cast<double>(s.programSwitches),
        static_cast<double>(s.bufferUploadBytes),
        static_cast<double>(s.textureUploadBytes),
        static_cast<double>(s.unmeasuredTextureUploads),
        static_cast<double>(s.lodChanges),
        static_cast<double>(s.shadowCasterDraws));
    --g_frameProfileRemaining;
}

void WebFrameProfile_PublishGpuResult(
    std::uint32_t pumpTick,
    std::uint32_t contextGeneration,
    std::uint32_t viewSubmissionGeneration,
    double gpuMilliseconds,
    std::uint32_t queryLagFrames,
    const char *status)
{
    DispatchFrameProfileGpuResult(pumpTick, contextGeneration,
        viewSubmissionGeneration, gpuMilliseconds, queryLagFrames, status);
}
#endif

namespace
{
std::uint32_t g_lastCGameFrameMilliseconds = 0u;
std::uint32_t g_cgameFrameAccumulatorMilliseconds = 0u;
bool g_reentrantCommandPumpReported = false;
#if KISAK_WEB_DIAGNOSTICS
std::uint32_t g_testSlowCommandMilliseconds = 0u;
#endif

bool InitializeCanonicalEngine()
{
    // Match the native entry envelope: dvars precede the canonical Com_Init
    // error boundary, while command and core initialization remain owned by
    // common.cpp in their native order.
    Dvar_Init();
    char commandLine[] = "";
    Com_Init(commandLine);
    Web_EmitEngineState("initialized", "canonical", "pending", 0);
    return true;
}

void RunCommands()
{
    char browserCommand[1024]{};
    if (Web_TakePendingCanonicalCommand(
            browserCommand, sizeof(browserCommand)))
    {
#if KISAK_WEB_DIAGNOSTICS
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical command executing: %s (nesting=%d).\n",
            browserCommand,
            cmd_args.nesting);
#endif
        Cbuf_ExecuteBuffer(
            0, CL_ControllerIndexFromClientNum(0), browserCommand);
#if KISAK_WEB_DIAGNOSTICS
        const std::uint32_t delay = g_testSlowCommandMilliseconds;
        g_testSlowCommandMilliseconds = 0u;
        const std::uint32_t started = Sys_Milliseconds();
        while (Sys_Milliseconds() - started < delay)
        {
            // Diagnostic-only evidence for a long canonical command frame.
        }
#endif
    }
    if (!Cbuf_TryExecute(0, 0) && !g_reentrantCommandPumpReported)
    {
        g_reentrantCommandPumpReported = true;
        Web_Log(
            WebLogLevel::Info,
            "[kisakcod-web] Deferred a re-entrant command-buffer pump to "
            "the next browser frame.\n");
    }
}

bool RunCGameFrame(const WebFrameInfo &frame)
{
    static std::uint32_t activeFrameCount = 0u;
    // CA_ACTIVE is enough to enter the native SP frame order. Do not wait for
    // CL_IsCgameInitialized here: that flag is set by CL_FirstSnapshot, and the
    // first snapshot itself is produced only after SV_Frame runs. Gating on
    // both states deadlocks the authoritative server at the load-screen view.
    if (!CL_IsLocalClientInGame(0))
    {
        g_lastCGameFrameMilliseconds = 0u;
        g_cgameFrameAccumulatorMilliseconds = 0u;
        return false;
    }

    int frameMilliseconds = 16;
    if (g_lastCGameFrameMilliseconds != 0u)
    {
        const std::uint32_t elapsed =
            frame.monotonicMilliseconds - g_lastCGameFrameMilliseconds;
        g_lastCGameFrameMilliseconds = frame.monotonicMilliseconds;

        // An Emscripten main loop in an OffscreenCanvas Worker is not
        // guaranteed to be display-vsynced. It can run several callbacks in
        // one millisecond. Never invent a 1 ms engine step for those calls:
        // doing so advances the authoritative SP clock faster than wall time,
        // and the canonical integer velocity snap can then preserve small
        // components indefinitely. Accumulate real time without blocking the
        // browser and honor the canonical com_maxfps dvar. An uncapped value
        // retains the web safety ceiling of 125 Hz.
        g_cgameFrameAccumulatorMilliseconds += std::min(elapsed, 100u);
        const int configuredMaxFps = com_maxfps
            ? com_maxfps->current.integer : 0;
        const std::uint32_t minimumFrameMilliseconds = configuredMaxFps > 0
            ? static_cast<std::uint32_t>(
                std::max(1, 1000 / configuredMaxFps))
            : 8u;
        if (g_cgameFrameAccumulatorMilliseconds < minimumFrameMilliseconds)
            return false;

        frameMilliseconds = static_cast<int>(
            std::min(g_cgameFrameAccumulatorMilliseconds, 100u));
        g_cgameFrameAccumulatorMilliseconds = 0u;
    }
    else
    {
        g_lastCGameFrameMilliseconds = frame.monotonicMilliseconds;
    }
    // Native Com_Frame refreshes this clock before the server/client frame.
    // CL_CreateNewCommands derives frame_msec from it; leaving it unchanged
    // makes the canonical mouse path discard motion as a zero-duration sample.
    com_frameTime = static_cast<int>(frame.monotonicMilliseconds);

    // The browser pump owns only timing. Preserve the native SP frame order.
    // SV_Frame consumes the command produced by the previous cgame frame;
    // CG_DrawActiveFrame calls canonical CL_Input while SCR_UpdateScreen builds
    // this frame, producing the next command. Sending another command here
    // would duplicate the same serverTime and Pmove would correctly ignore it.
    #if KISAK_WEB_DIAGNOSTICS
    WebFrameProfileSample *const profile = WebFrameProfile_Current();
    double profileStarted = profile ? WebFrameProfile_Now() : 0.0;
    #endif
    frameMilliseconds = SV_Frame(frameMilliseconds);
    #if KISAK_WEB_DIAGNOSTICS
    if (profile)
    {
        profile->serverMs = WebFrameProfile_Now() - profileStarted;
        profileStarted = WebFrameProfile_Now();
    }
    #endif
    CL_RunOncePerClientFrame(0, frameMilliseconds);
    #if KISAK_WEB_DIAGNOSTICS
    if (profile)
    {
        profile->clientOnceMs = WebFrameProfile_Now() - profileStarted;
        profileStarted = WebFrameProfile_Now();
    }
    #endif
    if (!Cbuf_TryExecute(0, CL_ControllerIndexFromClientNum(0)) &&
        !g_reentrantCommandPumpReported)
    {
        g_reentrantCommandPumpReported = true;
        Web_Log(
            WebLogLevel::Info,
            "[kisakcod-web] Deferred a re-entrant command-buffer pump to "
            "the next browser frame.\n");
    }
    #if KISAK_WEB_DIAGNOSTICS
    if (profile)
    {
        profile->commandBufferMs = WebFrameProfile_Now() - profileStarted;
        profileStarted = WebFrameProfile_Now();
    }
    #endif
    CL_Frame(0, frameMilliseconds);
    #if KISAK_WEB_DIAGNOSTICS
    if (profile)
    {
        profile->clientFrameMs = WebFrameProfile_Now() - profileStarted;
        profileStarted = WebFrameProfile_Now();
    }
    #endif
    SCR_UpdateScreen();
    #if KISAK_WEB_DIAGNOSTICS
    if (profile)
    {
        profile->cgameFrameMs = WebFrameProfile_Now() - profileStarted;
        profileStarted = WebFrameProfile_Now();
    }
    #endif
    // Native SCR_UpdateFrame updates sound on non-cgame screens. The web
    // renderer reports every active SP frame as refreshed UI, so that native
    // branch is never reached while gameplay is running. Keep the canonical
    // CL_UpdateSound pump at the existing frame boundary for active cgame
    // frames; this also reconciles pause state and resumes delayed channels.
    // Match CL_CGameRendering's positive lifecycle gate without using
    // CL_IsCGameRendering: the latter additionally requires cls.uiStarted,
    // which is not part of the cgame rendering contract on this web path.
    if (clientUIActives[0].connectionState == CA_ACTIVE &&
        !UI_IsFullscreen() && !CL_SkipRendering())
        CL_UpdateSound();
    #if KISAK_WEB_DIAGNOSTICS
    if (profile)
        profile->soundMs = WebFrameProfile_Now() - profileStarted;
    #endif
    ++activeFrameCount;
    if (activeFrameCount == 1u || activeFrameCount == 120u)
    {
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical SP pump frame %u: msec=%d, "
            "levelTime=%d, paused=%d, firstSnapshot=%d.\n",
            activeFrameCount, frameMilliseconds, sv.levelTime,
            cl_paused ? cl_paused->current.integer : -1,
            CL_IsCgameInitialized(0) ? 1 : 0);
    }
    return true;
}

void RenderFrame(const WebFrameInfo &frame, void *)
{
    #if KISAK_WEB_DIAGNOSTICS
    const bool profiling = WebFrameProfile_BeginPump(frame.pumpTick);
    const double totalStarted = profiling ? WebFrameProfile_Now() : 0.0;
    double stageStarted = totalStarted;
    #endif
    WebFs_PumpCompletions();
    #if KISAK_WEB_DIAGNOSTICS
    if (profiling)
    {
        WebFrameProfile_Current()->filesystemMs =
            WebFrameProfile_Now() - stageStarted;
        stageStarted = WebFrameProfile_Now();
    }
    #endif
    RunCommands();
    #if KISAK_WEB_DIAGNOSTICS
    if (profiling)
    {
        WebFrameProfile_Current()->commandMs =
            WebFrameProfile_Now() - stageStarted;
    }
    #endif
    const bool gameplayFrame = RunCGameFrame(frame);
    // Before a local game is active the renderer remains responsible for the
    // launcher/bootstrap surface. During gameplay, presentation follows the
    // same non-blocking com_maxfps admission decision as the engine frame.
    const bool rendererSubmitted = !CL_IsLocalClientInGame(0) || gameplayFrame;
    #if KISAK_WEB_DIAGNOSTICS
    stageStarted = profiling ? WebFrameProfile_Now() : 0.0;
    #endif
    if (rendererSubmitted)
        WebRenderer_DrawFrame(frame);
    #if KISAK_WEB_DIAGNOSTICS
    if (profiling)
    {
        WebFrameProfileSample *const profile = WebFrameProfile_Current();
        profile->rendererBackendMs = WebFrameProfile_Now() - stageStarted;
        profile->totalMs = WebFrameProfile_Now() - totalStarted;
    }
    WebFrameProfile_EndPump(gameplayFrame, rendererSubmitted);
    #endif
}
} // namespace

#if KISAK_WEB_DIAGNOSTICS
extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestSlowNextCommand(int milliseconds)
{
    if (milliseconds < 1 || milliseconds > 250)
        return 0;
    g_testSlowCommandMilliseconds = static_cast<std::uint32_t>(milliseconds);
    return 1;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestBeginFrameProfile(int samples)
{
    if (samples < 1 || samples > 600) return 0;
    g_frameProfileRemaining = static_cast<std::uint32_t>(samples);
    g_frameProfilePumpActive = false;
    return 1;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestFrameProfileRemaining()
{
    return static_cast<int>(g_frameProfileRemaining);
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestUsingAds()
{
    return clients[0].usingAds ? 1 : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestGameplayState(
    int field, int weaponIndex)
{
    if (!CL_IsLocalClientInGame(0) || !CL_IsCgameInitialized(0))
        return -1;

    cg_s *const cgame = CG_GetLocalClientGlobals(0);
    if (!cgame || !cgame->nextSnap)
        return -1;

    const playerState_s &snapshot = cgame->nextSnap->ps;
    switch (field)
    {
    case 0:
        return static_cast<int>(snapshot.weapon);
    case 1:
    {
        if (weaponIndex < 0 ||
            static_cast<std::uint32_t>(weaponIndex) >= BG_GetNumWeapons())
            return -1;
        const int clip = BG_ClipForWeapon(
            static_cast<std::uint32_t>(weaponIndex));
        const int clipCount = static_cast<int>(
            sizeof(snapshot.ammoclip) / sizeof(snapshot.ammoclip[0]));
        return clip >= 0 && clip < clipCount ? snapshot.ammoclip[clip] : -1;
    }
    case 2:
        return static_cast<int>(cgame->weaponSelect);
    case 3:
        return BG_PlayerWeaponCountPrimaryTypes(&snapshot);
    case 4:
        return static_cast<int>(cgame->predictedPlayerState.weapon);
    case 5:
        return static_cast<int>(
            BG_GetViewmodelWeaponIndex(&cgame->predictedPlayerState));
    case 6:
    {
        if (weaponIndex < 0 ||
            static_cast<std::uint32_t>(weaponIndex) >= BG_GetNumWeapons())
            return -1;
        const int clip = BG_ClipForWeapon(
            static_cast<std::uint32_t>(weaponIndex));
        const int clipCount = static_cast<int>(
            sizeof(cgame->predictedPlayerState.ammoclip) /
            sizeof(cgame->predictedPlayerState.ammoclip[0]));
        return clip >= 0 && clip < clipCount
            ? cgame->predictedPlayerState.ammoclip[clip] : -1;
    }
    case 7:
        return BG_PlayerWeaponCountPrimaryTypes(&cgame->predictedPlayerState);
    case 8:
        return WeaponCycleAllowed(cgame) ? 1 : 0;
    case 9:
        return cgame->predictedPlayerState.eFlags;
    case 10:
        return cgame->predictedPlayerState.pm_flags;
    case 11:
        return cgame->predictedPlayerState.weapFlags;
    case 12:
        return clientUIActives[0].keyCatchers;
    case 13:
        return cgame->predictedPlayerState.weaponTime;
    case 14:
        return cgame->predictedPlayerState.weaponDelay;
    case 15:
        return static_cast<int>(cgame->predictedPlayerState.weaponstate);
    case 16:
        return static_cast<int>(cgame->predictedPlayerState.weaponShotCount);
    default:
        return -1;
    }
}
#endif

int main()
{
    // Establish the same lazy monotonic epoch used by the native system layer
    // before engine initialization begins.
    (void)Sys_Milliseconds();
    Sys_InitializeCriticalSections();
    Sys_InitMainThread();
    static jmp_buf errorBoundary;
    static va_info_t formattedText;
    Sys_SetValue(1, &formattedText);
    Sys_SetValue(2, &errorBoundary);
    Web_Log(WebLogLevel::Info, "[kisakcod-web] Browser system layer starting.\n");
    Web_EmitRuntimeState("loading", "Validating portable engine code");

    if (!InitializeCanonicalEngine())
    {
        Web_Log(WebLogLevel::Error, "[kisakcod-web] Canonical engine initialization failed.\n");
        Web_EmitRuntimeState("failed", "The canonical engine failed to initialize");
        return 1;
    }

    if (!WebRenderer_Initialize())
    {
        Web_EmitRuntimeState("failed", "The WebGL2 renderer could not initialize");
        return 1;
    }
    Web_EmitRuntimeState(
        "runtime-ready",
        "Canonical runtime and browser platform boundaries are initialized");
    if (!Web_StartFramePump(RenderFrame, nullptr))
    {
        WebFs_CancelAll();
        Web_EmitRuntimeState("failed", "The browser frame pump could not start");
        return 1;
    }
    Web_EmitRuntimeState(
        "running",
        "The browser frame pump and WebGL2 backend are ready for canonical assets");
    return 0;
}
