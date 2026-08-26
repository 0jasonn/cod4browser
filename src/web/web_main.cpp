#include <client/cl_input.h>
#include <client/cl_scrn.h>
#include <client/client.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <server/server.h>
#include <qcommon/system.h>
#include <universal/dvar.h>
#include <ui/ui.h>
#include <web/web_client_server_lifecycle.h>
#include <web/web_filesystem.h>
#include <web/web_renderer.h>
#include <web/web_system.h>

#include <algorithm>
#include <cstdint>
#include <csetjmp>

#if KISAK_WEB_DIAGNOSTICS
#include <emscripten/emscripten.h>
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
    frameMilliseconds = SV_Frame(frameMilliseconds);
    CL_RunOncePerClientFrame(0, frameMilliseconds);
    if (!Cbuf_TryExecute(0, CL_ControllerIndexFromClientNum(0)) &&
        !g_reentrantCommandPumpReported)
    {
        g_reentrantCommandPumpReported = true;
        Web_Log(
            WebLogLevel::Info,
            "[kisakcod-web] Deferred a re-entrant command-buffer pump to "
            "the next browser frame.\n");
    }
    CL_Frame(0, frameMilliseconds);
    SCR_UpdateScreen();
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
    WebFs_PumpCompletions();
    RunCommands();
    const bool gameplayFrame = RunCGameFrame(frame);
    // Before a local game is active the renderer remains responsible for the
    // launcher/bootstrap surface. During gameplay, presentation follows the
    // same non-blocking com_maxfps admission decision as the engine frame.
    if (!CL_IsLocalClientInGame(0) || gameplayFrame)
        WebRenderer_DrawFrame(frame);
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
        Web_Log(WebLogLevel::Error, "[kisakcod-web] qcommon command/dvar smoke test failed.\n");
        Web_EmitRuntimeState("failed", "The headless command/dvar slice failed to initialize");
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
