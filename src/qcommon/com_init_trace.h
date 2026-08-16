#pragma once

#include <cstddef>

struct ComInitTraceSnapshot
{
    static constexpr std::size_t MAX_STAGES = 24;
    static constexpr std::size_t MAX_DVARS = 40;
    static constexpr std::size_t MAX_COMMANDS = 16;

    const char *stages[MAX_STAGES];
    std::size_t stageCount;
    const char *dvarNames[MAX_DVARS];
    std::size_t dvarCount;
    std::size_t startupVariableCount;
    const char *commandNames[MAX_COMMANDS];
    std::size_t commandCount;
    std::size_t physicalMemorySize;
    std::size_t pmemLowPosition;
    std::size_t pmemHighPosition;
    std::size_t pmemHighAllocationCount;
    bool databaseInitializing;
    const char *stopStage;
};

void Com_InitTraceReset();
void Com_InitTraceStage(const char *stage);
void Com_InitTraceDvar(const char *name);
void Com_InitTraceCommand(const char *name);
void Com_InitTraceSetCounts(std::size_t startupVariableCount, std::size_t commandCount);
void Com_InitTraceSetMemory(
    std::size_t physicalMemorySize,
    std::size_t lowPosition,
    std::size_t highPosition,
    std::size_t highAllocationCount,
    bool databaseInitializing);
void Com_InitTraceStop(const char *stage);
const ComInitTraceSnapshot &Com_GetInitTrace();
