#include <qcommon/com_init_trace.h>

#include <cstdio>
#include <cstring>

#ifdef KISAK_WEB
#include <emscripten.h>
#endif

namespace
{
ComInitTraceSnapshot g_trace{};

#ifdef KISAK_WEB
EM_JS(
    void,
    DispatchComInitTrace,
    (const char *stopStage,
     const char *stages,
     const char *dvars,
     int stageCount,
     int startupVariableCount,
     int commandCount,
     int dvarCount),
    {
        if (typeof globalThis.dispatchEvent !== "function" ||
            typeof globalThis.CustomEvent !== "function") return;
        globalThis.dispatchEvent(new CustomEvent("kisakcod:gate3-init", {
            detail: {
                stopStage: UTF8ToString(stopStage),
                stages: UTF8ToString(stages).split("|").filter(Boolean),
                dvars: UTF8ToString(dvars).split("|").filter(Boolean),
                stageCount,
                startupVariableCount,
                commandCount,
                dvarCount
            }
        }));
    });
#endif
} // namespace

void Com_InitTraceReset()
{
    g_trace = {};
    g_trace.stopStage = "not-started";
}

void Com_InitTraceStage(const char *stage)
{
    if (g_trace.stageCount < ComInitTraceSnapshot::MAX_STAGES)
    {
        g_trace.stages[g_trace.stageCount++] = stage;
    }
}

void Com_InitTraceDvar(const char *name)
{
    if (g_trace.dvarCount < ComInitTraceSnapshot::MAX_DVARS)
    {
        g_trace.dvarNames[g_trace.dvarCount++] = name;
    }
}

void Com_InitTraceSetCounts(std::size_t startupVariableCount, std::size_t commandCount)
{
    g_trace.startupVariableCount = startupVariableCount;
    g_trace.commandCount = commandCount;
}

void Com_InitTraceStop(const char *stage)
{
    g_trace.stopStage = stage;
    Com_InitTraceStage("stop");
#ifdef KISAK_WEB
    char stages[512]{};
    char dvars[1024]{};
    for (std::size_t index = 0; index < g_trace.stageCount; ++index)
    {
        std::snprintf(
            stages + std::strlen(stages),
            sizeof(stages) - std::strlen(stages),
            "%s%s",
            index ? "|" : "",
            g_trace.stages[index]);
    }
    for (std::size_t index = 0; index < g_trace.dvarCount; ++index)
    {
        std::snprintf(
            dvars + std::strlen(dvars),
            sizeof(dvars) - std::strlen(dvars),
            "%s%s",
            index ? "|" : "",
            g_trace.dvarNames[index]);
    }
    DispatchComInitTrace(
        g_trace.stopStage,
        stages,
        dvars,
        static_cast<int>(g_trace.stageCount),
        static_cast<int>(g_trace.startupVariableCount),
        static_cast<int>(g_trace.commandCount),
        static_cast<int>(g_trace.dvarCount));
#endif
}

const ComInitTraceSnapshot &Com_GetInitTrace()
{
    return g_trace;
}
