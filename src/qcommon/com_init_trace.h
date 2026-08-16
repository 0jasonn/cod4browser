#pragma once

#include <cstddef>

struct ComInitTraceSnapshot
{
    static constexpr std::size_t MAX_STAGES = 16;
    static constexpr std::size_t MAX_DVARS = 40;

    const char *stages[MAX_STAGES];
    std::size_t stageCount;
    const char *dvarNames[MAX_DVARS];
    std::size_t dvarCount;
    std::size_t startupVariableCount;
    std::size_t commandCount;
    const char *stopStage;
};

void Com_InitTraceReset();
void Com_InitTraceStage(const char *stage);
void Com_InitTraceDvar(const char *name);
void Com_InitTraceSetCounts(std::size_t startupVariableCount, std::size_t commandCount);
void Com_InitTraceStop(const char *stage);
const ComInitTraceSnapshot &Com_GetInitTrace();

