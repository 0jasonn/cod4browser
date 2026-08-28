#include <client/cl_input.h>
#include <client/cl_scrn.h>
#include <client/client.h>
#include <cgame/cg_local.h>
#include <cgame/cg_main.h>
#include <cgame/cg_vehicle_hud.h>
#include <game/actor.h>
#include <game/g_main.h>
#include <game/savememory.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <server/server.h>
#include <server/sv_public.h>
#include <script/scr_vm.h>
#include <qcommon/system.h>
#include <universal/dvar.h>
#include <ui/ui.h>
#include <web/web_client_server_lifecycle.h>
#include <web/web_filesystem.h>
#include <web/web_frame_profile.h>
#include <web/web_renderer.h>
#include <web/web_system.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <limits>

#if KISAK_WEB_DIAGNOSTICS
#include <emscripten/emscripten.h>
#endif

#if KISAK_WEB_DIAGNOSTICS
namespace
{
WebFrameProfileSample g_frameProfileSample{};
WebFrameProfileCapture g_frameProfileCapture{};
bool g_frameProfilePumpActive = false;
constexpr double DEFAULT_FRAME_PROFILE_TIMEOUT_MS = 120'000.0;

EM_JS(void, DispatchFrameProfile,
    (std::uint32_t pumpTick, std::uint32_t contextGeneration,
     std::uint32_t worldGeneration,
     std::uint32_t viewSubmissionGeneration, bool gameplayFrame,
     bool rendererSubmitted, bool gpuTimingsAvailable, bool gpuQueryIssued,
     bool gpuQueryDropped, const char *gpuQueryStage,
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
                worldGeneration: worldGeneration >>> 0,
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
                    queryDropped: Boolean(gpuQueryDropped),
                    stage: UTF8ToString(gpuQueryStage)
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

EM_JS(void, DispatchFrameProfileCapture,
    (bool profileComplete, std::uint32_t requestedSamples,
     std::uint32_t collectedSamples, const char *incompleteReason), {
        const reason = UTF8ToString(incompleteReason);
        globalThis.dispatchEvent(new CustomEvent("kisakcod:frame-profile", {
            detail: {
                kind: "capture",
                profileComplete: Boolean(profileComplete),
                profileSamplesRequested: requestedSamples >>> 0,
                profileSamplesCollected: collectedSamples >>> 0,
                profileIncompleteReason: reason || null
            }
        }));
    });

EM_JS(void, DispatchFrameProfileGpuResult,
    (std::uint32_t pumpTick, std::uint32_t contextGeneration,
     std::uint32_t worldGeneration,
     std::uint32_t viewSubmissionGeneration, const char *stage,
     const char *mapName, double gpuMilliseconds,
     std::uint32_t queryLagFrames, const char *status), {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:frame-profile", {
            detail: {
                kind: "gpu-result",
                pumpTick: pumpTick >>> 0,
                contextGeneration: contextGeneration >>> 0,
                worldGeneration: worldGeneration >>> 0,
                viewSubmissionGeneration: viewSubmissionGeneration >>> 0,
                map: UTF8ToString(mapName),
                gpu: {
                    status: UTF8ToString(status),
                    stage: UTF8ToString(stage),
                    stageMs: gpuMilliseconds,
                    queryLagFrames: queryLagFrames >>> 0
                }
            }
        }));
    });

const char *FrameProfileIncompleteReasonString(
    WebFrameProfileIncompleteReason reason) noexcept
{
    switch (reason)
    {
    case WebFrameProfileIncompleteReason::None: return "";
    case WebFrameProfileIncompleteReason::Timeout: return "TIMEOUT";
    case WebFrameProfileIncompleteReason::ContextChanged:
        return "CONTEXT_CHANGED";
    case WebFrameProfileIncompleteReason::WorldChanged:
        return "MAP_CHANGED";
    }
    return "UNKNOWN";
}

void DispatchFrameProfileCaptureState()
{
    DispatchFrameProfileCapture(
        g_frameProfileCapture.state == WebFrameProfileCaptureState::Complete,
        g_frameProfileCapture.requestedSamples,
        g_frameProfileCapture.collectedSamples,
        FrameProfileIncompleteReasonString(
            g_frameProfileCapture.incompleteReason));
}

int BeginFrameProfileCapture(int samples, int timeoutMilliseconds)
{
    if (samples < 1 || samples > 600 || timeoutMilliseconds < 1 ||
        timeoutMilliseconds > 300'000)
        return 0;
    g_frameProfileCapture.Begin(
        static_cast<std::uint32_t>(samples), WebFrameProfile_Now(),
        static_cast<double>(timeoutMilliseconds));
    g_frameProfileSample = {};
    g_frameProfilePumpActive = false;
    return 1;
}
}

bool WebFrameProfile_BeginPump(std::uint32_t pumpTick) noexcept
{
    g_frameProfilePumpActive = false;
    if (g_frameProfileCapture.Poll(WebFrameProfile_Now()))
        DispatchFrameProfileCaptureState();
    g_frameProfilePumpActive =
        g_frameProfileCapture.state == WebFrameProfileCaptureState::Active;
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
    const WebFrameProfilePumpResult result =
        g_frameProfileCapture.FinishPump(
            WebFrameProfile_Now(), gameplayFrame, rendererSubmitted,
            g_frameProfileSample.contextGeneration,
            g_frameProfileSample.worldGeneration);
    if (result == WebFrameProfilePumpResult::Ignored) return;
    if (result == WebFrameProfilePumpResult::CaptureIncomplete)
    {
        DispatchFrameProfileCaptureState();
        return;
    }
    const WebFrameProfileSample &s = g_frameProfileSample;
    DispatchFrameProfile(
        s.pumpTick, s.contextGeneration, s.worldGeneration,
        s.viewSubmissionGeneration,
        s.gameplayFrame, s.rendererSubmitted, s.gpuTimingsAvailable,
        s.gpuQueryIssued, s.gpuQueryDropped,
        WebFrameProfile_GpuStageName(s.gpuStage),
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
    if (result == WebFrameProfilePumpResult::CaptureComplete)
        DispatchFrameProfileCaptureState();
}

void WebFrameProfile_PublishGpuResult(
    std::uint32_t pumpTick,
    std::uint32_t contextGeneration,
    std::uint32_t worldGeneration,
    std::uint32_t viewSubmissionGeneration,
    WebFrameProfileGpuStage stage,
    const char *mapName,
    double gpuMilliseconds,
    std::uint32_t queryLagFrames,
    const char *status)
{
    DispatchFrameProfileGpuResult(pumpTick, contextGeneration, worldGeneration,
        viewSubmissionGeneration, WebFrameProfile_GpuStageName(stage), mapName,
        gpuMilliseconds, queryLagFrames, status);
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
    const bool rendererScheduled = !CL_IsLocalClientInGame(0) || gameplayFrame;
    #if KISAK_WEB_DIAGNOSTICS
    if (profiling)
        WebFrameProfile_Current()->gameplayFrame = gameplayFrame;
    stageStarted = profiling ? WebFrameProfile_Now() : 0.0;
    #endif
    const bool rendererSubmitted = rendererScheduled &&
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
    return BeginFrameProfileCapture(
        samples, static_cast<int>(DEFAULT_FRAME_PROFILE_TIMEOUT_MS));
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestBeginFrameProfileWithTimeout(
    int samples, int timeoutMilliseconds)
{
    return BeginFrameProfileCapture(samples, timeoutMilliseconds);
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestFrameProfileRemaining()
{
    return static_cast<int>(g_frameProfileCapture.Remaining());
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestUsingAds()
{
    return clients[0].usingAds ? 1 : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE double KisakWeb_TestGameplayFloat(
    int field, int component)
{
    if (!CL_IsLocalClientInGame(0) || !CL_IsCgameInitialized(0) ||
        component < 0 || component >= 3)
        return std::numeric_limits<double>::quiet_NaN();

    cg_s *const cgame = CG_GetLocalClientGlobals(0);
    if (!cgame || !cgame->nextSnap)
        return std::numeric_limits<double>::quiet_NaN();
    if (field == 0)
        return cgame->predictedPlayerState.origin[component];
    if (field == 1)
        return cgame->predictedPlayerState.viewangles[component];
    return std::numeric_limits<double>::quiet_NaN();
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestObjectiveState(int slot)
{
    if (!CL_IsLocalClientInGame(0) || !CL_IsCgameInitialized(0) ||
        slot < 0 || slot >= 16)
        return -1;
    cg_s *const cgame = CG_GetLocalClientGlobals(0);
    return cgame && cgame->nextSnap
        ? static_cast<int>(cgame->objectives[slot].state) : -1;
}

extern "C" EMSCRIPTEN_KEEPALIVE double KisakWeb_TestObjectiveOrigin(
    int slot, int marker, int component)
{
    if (!CL_IsLocalClientInGame(0) || !CL_IsCgameInitialized(0) ||
        slot < 0 || slot >= 16 || marker < 0 || marker >= 8 ||
        component < 0 || component >= 3)
        return std::numeric_limits<double>::quiet_NaN();
    cg_s *const cgame = CG_GetLocalClientGlobals(0);
    return cgame && cgame->nextSnap
        ? cgame->objectives[slot].origin[marker][component]
        : std::numeric_limits<double>::quiet_NaN();
}

namespace
{
bool ReadServerObjective(
    int slot, int marker, int *state, float (&origin)[3])
{
    if (slot < 0 || slot >= 16 || marker < 0 || marker >= 8)
        return false;
    char configString[1024]{};
    SV_GetConfigstring(slot + 11, configString, sizeof(configString));
    const char *const stateValue = Info_ValueForKey(configString, "state");
    *state = *stateValue ? std::atoi(stateValue) : 0;
    char key[16]{};
    std::snprintf(key, sizeof(key), "org%d", marker);
    const char *const originValue = Info_ValueForKey(configString, key);
    return *originValue && std::sscanf(originValue, "%f %f %f",
        &origin[0], &origin[1], &origin[2]) == 3;
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestServerObjectiveState(int slot)
{
    int state = -1;
    float origin[3]{};
    return ReadServerObjective(slot, 0, &state, origin) ? state :
        slot >= 0 && slot < 16 ? state : -1;
}

extern "C" EMSCRIPTEN_KEEPALIVE double KisakWeb_TestServerObjectiveOrigin(
    int slot, int marker, int component)
{
    if (component < 0 || component >= 3)
        return std::numeric_limits<double>::quiet_NaN();
    int state = 0;
    float origin[3]{};
    return ReadServerObjective(slot, marker, &state, origin)
        ? origin[component] : std::numeric_limits<double>::quiet_NaN();
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestTargetEntity(int slot)
{
    if (!CL_IsLocalClientInGame(0) || !CL_IsCgameInitialized(0) ||
        slot < 0 || slot >= 32)
        return -1;
    cg_s *const cgame = CG_GetLocalClientGlobals(0);
    if (!cgame || !cgame->nextSnap ||
        cgame->targets[slot].entNum == ENTITYNUM_NONE)
        return -1;
    return cgame->targets[slot].entNum;
}

extern "C" EMSCRIPTEN_KEEPALIVE double KisakWeb_TestTargetOrigin(
    int slot, int component)
{
    const int entity = KisakWeb_TestTargetEntity(slot);
    if (entity < 0 || component < 0 || component >= 3)
        return std::numeric_limits<double>::quiet_NaN();
    cg_s *const cgame = CG_GetLocalClientGlobals(0);
    const targetInfo_t &target = cgame->targets[slot];
    const centity_s *const centity = CG_GetEntity(0, entity);
    return centity->pose.origin[component] + target.offset[component];
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestPlayerTeam()
{
    return g_entities[0].sentient
        ? static_cast<int>(g_entities[0].sentient->eTeam) : -1;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestActorState(
    int slot, int field)
{
    if (!level.actors || slot < 0 || slot >= MAX_ACTORS)
        return -1;
    const actor_s &actor = level.actors[slot];
    if (!actor.inuse || !actor.ent || !actor.ent->r.inuse || !actor.sentient)
        return -1;
    switch (field)
    {
    case 0: return static_cast<int>(actor.sentient->eTeam);
    case 1: return actor.ent->health;
    case 2: return actor.bDrawOnCompass ? 1 : 0;
    case 3: return actor.Path.wPathLen > 0 ? 1 : 0;
    case 4: return static_cast<int>(actor.eState[
        actor.stateLevel < 5 ? actor.stateLevel : 0]);
    case 5: return static_cast<int>(actor.moveMode);
    case 6: return actor.lastShotTime;
    default: return -1;
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE double KisakWeb_TestActorVector(
    int slot, int vector, int component)
{
    if (KisakWeb_TestActorState(slot, 0) < 0 ||
        component < 0 || component >= 3)
        return std::numeric_limits<double>::quiet_NaN();
    const actor_s &actor = level.actors[slot];
    if (vector == 0)
        return actor.ent->r.currentOrigin[component];
    if (vector == 1 && actor.Path.wPathLen > 0)
        return actor.Path.vFinalGoal[component];
    return std::numeric_limits<double>::quiet_NaN();
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
    case 17:
        return g_entities[0].health;
    case 18:
        return level.clients ? level.clients[0].ps.pm_type : -1;
    case 19:
    {
        std::uint32_t hash = 2166136261u;
        for (const objectiveInfo_t &objective : cgame->objectives)
        {
            hash ^= static_cast<std::uint32_t>(objective.state);
            hash *= 16777619u;
            for (std::size_t i = 0;
                 i < sizeof(objective.string) && objective.string[i]; ++i)
            {
                hash ^= static_cast<unsigned char>(objective.string[i]);
                hash *= 16777619u;
            }
        }
        return static_cast<int>(hash);
    }
    case 20:
    case 21:
    case 22:
    {
        if (!level.actors)
            return -1;
        int active = 0;
        int alive = 0;
        std::uint32_t fingerprint = 2166136261u;
        for (int i = 0; i < MAX_ACTORS; ++i)
        {
            const actor_s &actor = level.actors[i];
            if (!actor.inuse || !actor.ent || !actor.ent->r.inuse)
                continue;
            const unsigned int stateLevel =
                actor.stateLevel < 5 ? actor.stateLevel : 0;
            ++active;
            if (actor.ent->health > 0)
                ++alive;
            const std::uint32_t values[] = {
                static_cast<std::uint32_t>(i),
                static_cast<std::uint32_t>(actor.ent->health),
                static_cast<std::uint32_t>(actor.eState[stateLevel]),
                static_cast<std::uint32_t>(actor.eSubState[stateLevel]),
                static_cast<std::uint32_t>(actor.lastShotTime),
                static_cast<std::uint32_t>(static_cast<int>(
                    actor.ent->r.currentOrigin[0] * 8.0f)),
                static_cast<std::uint32_t>(static_cast<int>(
                    actor.ent->r.currentOrigin[1] * 8.0f)),
                static_cast<std::uint32_t>(static_cast<int>(
                    actor.ent->r.currentOrigin[2] * 8.0f)),
            };
            for (const std::uint32_t value : values)
            {
                fingerprint ^= value;
                fingerprint *= 16777619u;
            }
        }
        if (field == 20) return active;
        if (field == 21) return alive;
        return static_cast<int>(fingerprint);
    }
    case 23:
        return static_cast<int>(Scr_GetNumScriptThreads());
    case 24:
        return level.time;
    case 25:
        return level.framenum;
    case 26:
        return (level.bMissionSuccess ? 1 : 0) |
            (level.bMissionFailed ? 2 : 0);
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
    case 35:
    {
        SaveGame *const save = SaveMemory_GetLastCommittedSave();
        const bool committed = save && save->saveState == COMMITTED &&
            *save->header.filename && save->header.bodySize > 0;
        if (!committed)
            return 0;
        if (field == 27) return 1;
        if (field == 28) return save->isWrittenToDevice ? 1 : 0;
        if (field == 29) return save->header.health;
        if (field == 30) return save->header.bodySize;
        if (field == 31) return sv_mapname &&
            I_stricmp(save->header.mapName, sv_mapname->current.string) == 0;
        if (field == 32) return save->header.saveId;
        return save->header.saveCheckSum;
    }
    case 33:
    case 34:
    {
        int count = 0;
        for (const objectiveInfo_t &objective : cgame->objectives)
        {
            if (field == 33 && (objective.state == OBJST_ACTIVE ||
                objective.state == OBJST_CURRENT))
                ++count;
            if (field == 34 && objective.state == OBJST_DONE)
                ++count;
        }
        return count;
    }
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
