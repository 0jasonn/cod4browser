#pragma once

#include <database/db_registry_types.h>

#include <cstddef>
#include <cstdint>

struct DBRuntimeTraceSnapshot
{
    const char *stages[24]{};
    std::size_t stageCount = 0;
    const char *logicalPath = nullptr;
    const char *stopStage = nullptr;
    std::uint32_t bytesRead = 0;
    std::uint32_t fileSize = 0;
    std::uint32_t readOffset = 0;
    std::uint32_t requestedBytes = 0;
    std::uint32_t initializedPoolCount = 0;
    std::uint32_t freeAssetEntryCount = 0;
    bool threadInitialized = false;
    bool headerValid = false;
    bool openSucceeded = false;
};

void DB_InitThread();
void DB_LoadXAssets(XZoneInfo *zoneInfo, std::uint32_t zoneCount, int sync);
const DBRuntimeTraceSnapshot &DB_GetRuntimeTrace();
