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
     const char *commands,
     int stageCount,
     int startupVariableCount,
     int commandCount,
     int dvarCount,
     int physicalMemorySize,
     int pmemLowPosition,
     int pmemHighPosition,
     int pmemHighAllocationCount,
     int databaseInitializing),
    {
        if (typeof globalThis.dispatchEvent !== "function" ||
            typeof globalThis.CustomEvent !== "function") return;
        globalThis.dispatchEvent(new CustomEvent("kisakcod:gate3-init", {
            detail: {
                stopStage: UTF8ToString(stopStage),
                stages: UTF8ToString(stages).split("|").filter(Boolean),
                dvars: UTF8ToString(dvars).split("|").filter(Boolean),
                commands: UTF8ToString(commands).split("|").filter(Boolean),
                stageCount,
                startupVariableCount,
                commandCount,
                dvarCount,
                physicalMemorySize,
                pmemLowPosition,
                pmemHighPosition,
                pmemHighAllocationCount,
                databaseInitializing: databaseInitializing !== 0
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

void Com_InitTraceCommand(const char *name)
{
    if (g_trace.commandCount < ComInitTraceSnapshot::MAX_COMMANDS)
    {
        g_trace.commandNames[g_trace.commandCount++] = name;
    }
}

void Com_InitTraceSetCounts(std::size_t startupVariableCount, std::size_t commandCount)
{
    g_trace.startupVariableCount = startupVariableCount;
    g_trace.commandCount = commandCount;
}

void Com_InitTraceSetMemory(
    std::size_t physicalMemorySize,
    std::size_t lowPosition,
    std::size_t highPosition,
    std::size_t highAllocationCount,
    bool databaseInitializing)
{
    g_trace.physicalMemorySize = physicalMemorySize;
    g_trace.pmemLowPosition = lowPosition;
    g_trace.pmemHighPosition = highPosition;
    g_trace.pmemHighAllocationCount = highAllocationCount;
    g_trace.databaseInitializing = databaseInitializing;
}

void Com_InitTraceStop(const char *stage)
{
    g_trace.stopStage = stage;
    Com_InitTraceStage("stop");
#ifdef KISAK_WEB
    char stages[512]{};
    char dvars[1024]{};
    char commands[256]{};
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
    for (std::size_t index = 0; index < g_trace.commandCount; ++index)
    {
        std::snprintf(
            commands + std::strlen(commands),
            sizeof(commands) - std::strlen(commands),
            "%s%s",
            index ? "|" : "",
            g_trace.commandNames[index]);
    }
    DispatchComInitTrace(
        g_trace.stopStage,
        stages,
        dvars,
        commands,
        static_cast<int>(g_trace.stageCount),
        static_cast<int>(g_trace.startupVariableCount),
        static_cast<int>(g_trace.commandCount),
        static_cast<int>(g_trace.dvarCount),
        static_cast<int>(g_trace.physicalMemorySize),
        static_cast<int>(g_trace.pmemLowPosition),
        static_cast<int>(g_trace.pmemHighPosition),
        static_cast<int>(g_trace.pmemHighAllocationCount),
        g_trace.databaseInitializing ? 1 : 0);
#endif
}

const ComInitTraceSnapshot &Com_GetInitTrace()
{
    return g_trace;
}
