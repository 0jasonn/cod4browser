#include <ode/odemath.h>
#include <qcommon/cmd.h>
#include <qcommon/com_init_trace.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <universal/dvar.h>
#include <web/web_archive_job.h>
#include <web/web_cooperative_scheduler.h>
#include <web/web_engine_asset.h>
#include <web/web_engine_surface.h>
#include <web/web_engine_scheduler.h>
#include <web/web_filesystem.h>
#include <web/web_qcommon_runtime.h>
#include <web/web_retail_census_job.h>
#include <web/web_renderer.h>
#include <web/web_system.h>

#include <cmath>
#include <array>
#include <cstdint>
#include <cstring>
#include <csetjmp>

namespace
{
bool g_frameCommandReported = false;

enum class BootstrapPhase : std::uint8_t
{
    Initializing = 0,
    ExtractingSurface,
    Running,
    Failed,
};

BootstrapPhase g_bootstrapPhase = BootstrapPhase::Initializing;
WebFrameInfo g_scheduledFrame{};

bool NearlyEqual(float actual, float expected, float tolerance = 0.0001f)
{
    return std::fabs(actual - expected) <= tolerance;
}

float Dot3(const dVector3 lhs, const dVector3 rhs)
{
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

bool RunPhysicsMathSmokeTest()
{
    dVector3 normalized = {3.0f, 4.0f, 0.0f, 0.0f};
    dNormalize3(normalized);
    if (!NearlyEqual(normalized[0], 0.6f) || !NearlyEqual(normalized[1], 0.8f) ||
        !NearlyEqual(normalized[2], 0.0f))
    {
        return false;
    }

    dVector3 zero = {0.0f, 0.0f, 0.0f, 0.0f};
    dNormalize3(zero);
    if (!NearlyEqual(zero[0], 1.0f) || !NearlyEqual(zero[1], 0.0f) ||
        !NearlyEqual(zero[2], 0.0f))
    {
        return false;
    }

    dVector3 normal = {0.0f, 0.0f, 1.0f, 0.0f};
    dVector3 tangent = {};
    dVector3 bitangent = {};
    dPlaneSpace(normal, tangent, bitangent);

    return NearlyEqual(Dot3(normal, tangent), 0.0f) &&
        NearlyEqual(Dot3(normal, bitangent), 0.0f) &&
        NearlyEqual(Dot3(tangent, bitangent), 0.0f) &&
        NearlyEqual(Dot3(tangent, tangent), 1.0f) &&
        NearlyEqual(Dot3(bitangent, bitangent), 1.0f);
}

bool InitializeHeadlessEngineSlice()
{
    // Match the native entry envelope: dvars precede the canonical Com_Init
    // error boundary, while command and core initialization remain owned by
    // common.cpp in their native order.
    Dvar_Init();
    char commandLine[] = "+set gate3_startup wasm +seta gate3_archive 1";
    Com_Init(commandLine);
    const ComInitTraceSnapshot &trace = Com_GetInitTrace();
    if (!trace.stopStage || std::strcmp(trace.stopStage, "PMem_Init/DB_SetInitializing") != 0 ||
        std::strcmp(Dvar_GetString("gate3_startup"), "wasm") != 0 ||
        std::strcmp(Dvar_GetString("gate3_archive"), "1") != 0)
    {
        return false;
    }

    char limitedTokens[] = "first second token with spaces";
    Cmd_TokenizeStringWithLimit(limitedTokens, 2);
    const bool limitedTokenBehaviorMatches =
        Cmd_Argc() == 2 && std::strcmp(Cmd_Argv(0), "first") == 0 &&
        std::strcmp(Cmd_Argv(1), "second token with spaces") == 0;
    Cmd_EndTokenizedString();
    if (!limitedTokenBehaviorMatches)
    {
        return false;
    }

    Cbuf_ExecuteBuffer(
        0,
        0,
        "set web_qcommon ready;"
        "set web_quoted \"value with spaces;still quoted\";"
        "set web_escaped \"value with \\\"quotes\\\"\";"
        "set web_comment /* ignored */ ready;"
        "SET web_case MixedCase;"
        "web_qcommon");
    if (std::strcmp(Dvar_GetString("web_qcommon"), "ready") != 0 ||
        std::strcmp(Dvar_GetString("web_quoted"), "value with spaces;still quoted") != 0 ||
        std::strcmp(Dvar_GetString("web_escaped"), "value with \"quotes\"") != 0 ||
        std::strcmp(Dvar_GetString("web_comment"), "ready") != 0 ||
        std::strcmp(Dvar_GetString("WEB_CASE"), "MixedCase") != 0)
    {
        return false;
    }

    Web_Log(
        WebLogLevel::Info,
        "[kisakcod-web] qcommon command/dvar smoke test passed.\n");
    Web_EmitEngineState("initialized", Dvar_GetString("web_qcommon"), "pending", 0);

    // This sequence deliberately crosses frame boundaries.  `wait` leaves
    // the final set command queued until the next browser frame pump tick.
    Cbuf_AddText(
        0,
        "set web_frame_command queued\n"
        "wait\n"
        "set web_frame_command executed\n");
    return true;
}

using kisak::web::CooperativeTaskBudget;
using kisak::web::CooperativeTaskHandle;
using kisak::web::CooperativeTaskResult;
using kisak::web::CooperativeTaskSpec;
using kisak::web::CooperativeTaskState;

CooperativeTaskResult FilesystemTask(
    std::uint32_t,
    const CooperativeTaskBudget &,
    void *)
{
    WebFs_PumpCompletions();
    return {CooperativeTaskState::Progress, 0u, 0u};
}

void CancelFilesystemTask(void *)
{
    WebFs_CancelAll();
}

CooperativeTaskResult QcommonTask(
    std::uint32_t,
    const CooperativeTaskBudget &,
    void *userData)
{
    const auto *frame = static_cast<const WebFrameInfo *>(userData);
    WebQcommonRuntime_Frame(*frame);
    return {CooperativeTaskState::Progress, 0u, 0u};
}

void CancelQcommonTask(void *)
{
    KisakWeb_CancelQcommonRuntime();
}

CooperativeTaskResult RetailCensusTask(
    std::uint32_t,
    const CooperativeTaskBudget &,
    void *)
{
    const WebRetailCensusFrameResult result = WebRetailCensusJob_Frame();
    return {CooperativeTaskState::Progress, result.bytesUsed, result.recordsUsed};
}

void CancelRetailCensusTask(void *)
{
    WebRetailCensusJob_Cancel();
}

CooperativeTaskResult ArchiveTask(
    std::uint32_t,
    const CooperativeTaskBudget &,
    void *)
{
    WebArchiveJob_Frame();
    return {CooperativeTaskState::Progress, 0u, 0u};
}

void CancelArchiveTask(void *)
{
    WebArchiveJob_Cancel();
}

CooperativeTaskResult EngineAssetTask(
    std::uint32_t,
    const CooperativeTaskBudget &,
    void *)
{
    WebEngineAsset_Frame();
    return {CooperativeTaskState::Progress, 0u, 0u};
}

void CancelEngineAssetTask(void *)
{
    (void)WebEngineAsset_Cancel();
}

CooperativeTaskResult CommandTask(
    std::uint32_t,
    const CooperativeTaskBudget &,
    void *userData)
{
    const auto *frame = static_cast<const WebFrameInfo *>(userData);
    Cbuf_Execute(0, 0);
    if (!g_frameCommandReported &&
        std::strcmp(Dvar_GetString("web_frame_command"), "executed") == 0)
    {
        g_frameCommandReported = true;
        Web_Log(
            WebLogLevel::Info,
            "[kisakcod-web] Command buffer advanced across browser frames.\n");
        Web_EmitEngineState(
            "ready",
            Dvar_GetString("web_qcommon"),
            Dvar_GetString("web_frame_command"),
            frame->pumpTick);
    }
    return {CooperativeTaskState::Progress, 0u, 1u};
}

CooperativeTaskResult SurfaceTask(
    std::uint32_t,
    const CooperativeTaskBudget &,
    void *userData)
{
    const auto *frame = static_cast<const WebFrameInfo *>(userData);
    if (g_bootstrapPhase == BootstrapPhase::ExtractingSurface)
    {
        const WebEngineSurfaceFrameResult surfaceResult =
            WebEngineSurface_Frame(*frame);
        if (surfaceResult == WebEngineSurfaceFrameResult::Ready)
        {
            if (WebRenderer_Initialize())
            {
                g_bootstrapPhase = BootstrapPhase::Running;
                Web_EmitRuntimeState(
                    "runtime-ready",
                    "Timing, commands, physics, incremental fastfile extraction, and WebGL2 are initialized");
            }
            else
            {
                g_bootstrapPhase = BootstrapPhase::Failed;
            }
            return {CooperativeTaskState::Complete, 0u, 1u};
        }
        else if (surfaceResult == WebEngineSurfaceFrameResult::Failed)
        {
            g_bootstrapPhase = BootstrapPhase::Failed;
            Web_EmitRuntimeState(
                "failed",
                "The bounded synthetic fastfile surface could not be incrementally extracted, converted, and submitted");
            return {CooperativeTaskState::Failed, 0u, 1u};
        }
        return {CooperativeTaskState::Progress, 0u, 1u};
    }
    return {CooperativeTaskState::Idle, 0u, 0u};
}

CooperativeTaskResult RendererTask(
    std::uint32_t,
    const CooperativeTaskBudget &,
    void *userData)
{
    const auto *frame = static_cast<const WebFrameInfo *>(userData);
    WebRenderer_DrawFrame(*frame);
    return {CooperativeTaskState::Progress, 0u, 1u};
}

bool InitializeEngineScheduler()
{
    if (!WebEngineScheduler_Initialize())
    {
        return false;
    }
    constexpr std::size_t TASK_COUNT = 8u;
    const std::array<CooperativeTaskSpec, TASK_COUNT> tasks = {{
        {"filesystem-completions", 10u, {0u, 8u}, FilesystemTask, CancelFilesystemTask, nullptr},
        {"qcommon", 20u, {14u, 1u}, QcommonTask, CancelQcommonTask, &g_scheduledFrame},
        {"retail-census", 30u, {64u * 1024u, 64u}, RetailCensusTask, CancelRetailCensusTask, nullptr},
        {"archive", 40u, {64u * 1024u, 64u}, ArchiveTask, CancelArchiveTask, nullptr},
        {"engine-asset", 50u, {64u * 1024u, 64u}, EngineAssetTask, CancelEngineAssetTask, nullptr},
        {"command-buffer", 60u, {4u * 1024u, 1u}, CommandTask, nullptr, &g_scheduledFrame},
        {"world-surface", 70u, {64u * 1024u, 64u}, SurfaceTask, nullptr, &g_scheduledFrame},
        {"renderer", 80u, {0u, 1u}, RendererTask, nullptr, &g_scheduledFrame},
    }};
    std::array<CooperativeTaskHandle, TASK_COUNT> handles{};
    for (std::size_t index = 0u; index < tasks.size(); ++index)
    {
        if (!WebEngineScheduler_Register(tasks[index], handles[index]))
        {
            WebEngineScheduler_Shutdown();
            return false;
        }
    }
    return true;
}

void RenderFrame(const WebFrameInfo &frame, void *)
{
    g_scheduledFrame = frame;
    WebEngineScheduler_RunFrame(frame);
}
} // namespace

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

    if (!RunPhysicsMathSmokeTest())
    {
        Web_Log(WebLogLevel::Error, "[kisakcod-web] ODE physics math smoke test failed.\n");
        Web_EmitRuntimeState("failed", "ODE physics math validation failed");
        return 1;
    }
    Web_Log(WebLogLevel::Info, "[kisakcod-web] ODE physics math verified in WebAssembly.\n");

    if (!InitializeHeadlessEngineSlice())
    {
        Web_Log(WebLogLevel::Error, "[kisakcod-web] qcommon command/dvar smoke test failed.\n");
        Web_EmitRuntimeState("failed", "The headless command/dvar slice failed to initialize");
        return 1;
    }

    if (!WebEngineSurface_Start())
    {
        Web_EmitRuntimeState(
            "failed",
            "The bounded synthetic fastfile extraction job could not start");
        return 1;
    }
    g_bootstrapPhase = BootstrapPhase::ExtractingSurface;
    if (!InitializeEngineScheduler())
    {
        Web_EmitRuntimeState("failed", "The cooperative engine scheduler could not initialize");
        return 1;
    }
    if (!Web_StartFramePump(RenderFrame, nullptr))
    {
        WebEngineScheduler_Shutdown();
        Web_EmitRuntimeState("failed", "The browser frame pump could not start");
        return 1;
    }
    return 0;
}
