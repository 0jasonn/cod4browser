#include <web/web_engine_scheduler.h>

#include <web/web_system.h>

#include <emscripten.h>

#include <cstddef>
#include <cstdint>

namespace
{
constexpr kisak::web::CooperativeSchedulerLimits ENGINE_LIMITS{
    8u,
    320u * 1024u,
    320u,
    12u * 1000u,
    3u,
};

kisak::web::CooperativeScheduler g_scheduler;
std::uint32_t g_lastPublishedWarnings = 0u;
std::uint32_t g_lastPublishedViolations = 0u;

std::uint64_t SchedulerClock(void *)
{
    const double microseconds = emscripten_get_now() * 1000.0;
    return microseconds > 0.0 ? static_cast<std::uint64_t>(microseconds) : 0u;
}

EM_JS(
    void,
    BeginScheduleTrace,
    (uint32_t frame,
     uint32_t schedulerGeneration,
     uint32_t registeredTasks,
     uint32_t runnableTasks,
     uint32_t taskCalls,
     uint32_t reservedBytes,
     uint32_t reservedRecords,
     uint32_t bytesUsed,
     uint32_t recordsUsed,
     uint32_t starvedTasks,
     uint32_t starvationWarnings,
     uint32_t protocolViolations,
     double elapsedMicroseconds,
     int budgetExhausted),
    {
        globalThis.__KISAKCOD_SCHEDULE_DETAIL__ = {
            state: protocolViolations > 0 ? "failed" :
                (budgetExhausted ? "budget-exhausted" : "running"),
            frame: frame >>> 0,
            schedulerGeneration: schedulerGeneration >>> 0,
            registeredTasks: registeredTasks >>> 0,
            runnableTasks: runnableTasks >>> 0,
            taskCalls: taskCalls >>> 0,
            reservedBytes: reservedBytes >>> 0,
            reservedRecords: reservedRecords >>> 0,
            bytesUsed: bytesUsed >>> 0,
            recordsUsed: recordsUsed >>> 0,
            starvedTasks: starvedTasks >>> 0,
            starvationWarnings: starvationWarnings >>> 0,
            protocolViolations: protocolViolations >>> 0,
            elapsedMicroseconds,
            budgetExhausted: Boolean(budgetExhausted),
            maxTaskCallsPerFrame: 8,
            maxReservedBytesPerFrame: 320 * 1024,
            maxReservedRecordsPerFrame: 320,
            maxWallMicrosecondsPerFrame: 12 * 1000,
            starvationWarningFrames: 3,
            deterministic: true,
            trace: []
        };
    });

EM_JS(
    void,
    AppendScheduleTrace,
    (const char *name,
     uint32_t order,
     uint32_t taskGeneration,
     const char *state,
     uint32_t reservedBytes,
     uint32_t reservedRecords,
     uint32_t bytesUsed,
     uint32_t recordsUsed,
     uint32_t consecutiveStarvedFrames),
    {
        globalThis.__KISAKCOD_SCHEDULE_DETAIL__?.trace.push({
            name: UTF8ToString(name),
            order: order >>> 0,
            taskGeneration: taskGeneration >>> 0,
            state: UTF8ToString(state),
            reservedBytes: reservedBytes >>> 0,
            reservedRecords: reservedRecords >>> 0,
            bytesUsed: bytesUsed >>> 0,
            recordsUsed: recordsUsed >>> 0,
            consecutiveStarvedFrames: consecutiveStarvedFrames >>> 0
        });
    });

EM_JS(void, EndScheduleTrace, (), {
    const detail = globalThis.__KISAKCOD_SCHEDULE_DETAIL__;
    delete globalThis.__KISAKCOD_SCHEDULE_DETAIL__;
    if (detail) {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:schedule", { detail }));
    }
});

void Publish(const kisak::web::CooperativeFrameReport &report)
{
    BeginScheduleTrace(
        report.frame,
        report.schedulerGeneration,
        report.registeredTasks,
        report.runnableTasks,
        report.taskCalls,
        report.reservedBytes,
        report.reservedRecords,
        report.bytesUsed,
        report.recordsUsed,
        report.starvedTasks,
        report.starvationWarnings,
        report.protocolViolations,
        static_cast<double>(report.elapsedMicroseconds),
        report.budgetExhausted ? 1 : 0);
    for (std::size_t index = 0u; index < report.traceCount; ++index)
    {
        const kisak::web::CooperativeTraceEntry &entry = report.trace[index];
        AppendScheduleTrace(
            entry.name,
            entry.order,
            entry.taskGeneration,
            kisak::web::CooperativeTraceStateString(entry.state),
            entry.reservation.maxBytes,
            entry.reservation.maxRecords,
            entry.bytesUsed,
            entry.recordsUsed,
            entry.consecutiveStarvedFrames);
    }
    EndScheduleTrace();
}
} // namespace

bool WebEngineScheduler_Initialize()
{
    g_lastPublishedWarnings = 0u;
    g_lastPublishedViolations = 0u;
    return g_scheduler.Initialize(ENGINE_LIMITS, SchedulerClock, nullptr);
}

bool WebEngineScheduler_Register(
    const kisak::web::CooperativeTaskSpec &spec,
    kisak::web::CooperativeTaskHandle &handle)
{
    return g_scheduler.Register(spec, handle);
}

void WebEngineScheduler_RunFrame(const WebFrameInfo &frame)
{
    kisak::web::CooperativeFrameReport report;
    if (!g_scheduler.RunFrame(frame.pumpTick, report))
    {
        Web_Log(WebLogLevel::Error, "[kisakcod-web] Cooperative scheduler rejected a frame.\n");
        return;
    }
    const bool publish = report.frame <= 8u || report.frame % 30u == 0u ||
        report.budgetExhausted ||
        report.starvationWarnings != g_lastPublishedWarnings ||
        report.protocolViolations != g_lastPublishedViolations;
    if (publish)
    {
        Publish(report);
    }
    g_lastPublishedWarnings = report.starvationWarnings;
    g_lastPublishedViolations = report.protocolViolations;
}

void WebEngineScheduler_Shutdown()
{
    g_scheduler.Reset();
}
