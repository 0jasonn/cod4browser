#pragma once

#include <database/db_registry_types.h>

#include <cstddef>
#include <cstdint>

struct DBRuntimeTraceSnapshot
{
    const char *stages[64]{};
    std::size_t stageCount = 0;
    const char *logicalPath = nullptr;
    const char *stopStage = nullptr;
    std::uint32_t bytesRead = 0;
    std::uint32_t fileSize = 0;
    std::uint32_t readOffset = 0;
    std::uint32_t requestedBytes = 0;
    std::uint32_t initializedPoolCount = 0;
    std::uint32_t freeAssetEntryCount = 0;
    std::uint32_t compressedBytesConsumed = 0;
    std::uint32_t decompressedBytesProduced = 0;
    std::uint32_t inputRefillCount = 0;
    std::uint32_t xfileSize = 0;
    std::uint32_t xfileExternalSize = 0;
    std::uint32_t blockSizes[9]{};
    std::uint32_t blockAllocationCount = 0;
    std::uint32_t blockAllocationBytes = 0;
    std::uint32_t streamBlock = 0;
    std::uint32_t streamOffset = 0;
    bool threadInitialized = false;
    bool headerValid = false;
    bool openSucceeded = false;
    bool inflateInitialized = false;
    bool streamInitialized = false;
    bool cleanupComplete = false;
};

void DB_InitThread();
void DB_LoadXAssets(XZoneInfo *zoneInfo, std::uint32_t zoneCount, int sync);
const DBRuntimeTraceSnapshot &DB_GetRuntimeTrace();

// Address-independent observations emitted by the canonical shared database
// translation units. These functions do not own decoding or allocation.
void DB_RuntimeTraceStage(const char *stage);
void DB_RuntimeTraceStop(const char *stage);
void DB_RuntimeTraceHeaderRead(std::uint32_t bytesRead, std::uint32_t fileSize);
void DB_RuntimeTraceInputRefill(std::uint32_t bytesRead);
void DB_RuntimeTraceInflate(
    std::uint32_t compressedBytesConsumed,
    std::uint32_t decompressedBytesProduced);
void DB_RuntimeTraceInflateInitialized();
void DB_RuntimeTraceXFile(
    std::uint32_t size,
    std::uint32_t externalSize,
    const std::uint32_t *blockSizes);
void DB_RuntimeTraceBlockAllocation(std::uint32_t blockIndex, std::uint32_t size);
void DB_RuntimeTraceStreamsInitialized(std::uint32_t block, std::uint32_t offset);
void DB_RuntimeTraceCleanupComplete();
