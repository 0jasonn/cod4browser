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
    bool xassetListBegin = false;
    bool xassetListEnd = false;
    std::uint32_t scriptStringCount = 0;
    std::uint32_t scriptStringObservedCount = 0;
    char scriptStringIdentities[8][64]{};
    std::uint32_t xassetCount = 0;
    std::uint32_t assetIndex = 0;
    std::uint32_t assetType = 0;
    char assetName[128]{};
    char pointerClassification[32]{};
    bool publicationBegin = false;
    bool publicationEnd = false;
    std::uint32_t assetEntryIndex = UINT32_MAX;
    std::uint32_t assetPoolIndex = UINT32_MAX;
    std::uint32_t freeEntryCountBefore = 0;
    std::uint32_t freeEntryCountAfter = 0;
    std::uint32_t assetHash = 0;
    std::uint32_t assetZoneIndex = 0;
    bool generatedLoadFailed = false;
    std::uint32_t streamOffsets[9]{};
};

void DB_InitThread();
void DB_LoadXAssets(XZoneInfo *zoneInfo, std::uint32_t zoneCount, int sync);
const DBRuntimeTraceSnapshot &DB_GetRuntimeTrace();

// Address-independent observations emitted by the canonical shared database
// translation units. These functions do not own decoding or allocation.
void DB_RuntimeTraceStage(const char *stage);
void DB_RuntimeTraceStop(const char *stage);
void DB_RuntimeSetLogicalPath(const char *path);
void DB_RuntimeSetFileSize(std::uint32_t fileSize);
void DB_RuntimeTraceOpenSucceeded();
void DB_RuntimeTraceThreadInitialized();
void DB_RuntimeTracePoolsInitialized(
    std::uint32_t poolCount, std::uint32_t freeEntryCount);
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
void DB_RuntimeTraceXAssetListBegin(
    std::int32_t scriptStringCount, std::int32_t assetCount);
void DB_RuntimeTraceXAssetListEnd();
void DB_RuntimeTraceScriptString(
    std::uint32_t index, const char *identity);
void DB_RuntimeTraceAssetBegin(
    std::uint32_t index, XAssetType type, const char *pointerClassification);
void DB_RuntimeTraceAssetLoaded(const char *name);
void DB_RuntimeTracePublicationBegin(
    XAssetType type, const char *name, std::size_t freeEntryCount);
void DB_RuntimeTracePublicationEnd(
    XAssetType type, const char *name, std::uint32_t entryIndex,
    std::uint32_t poolIndex, std::size_t freeBefore, std::size_t freeAfter,
    std::uint32_t hash, std::uint32_t zoneIndex);
// Canonical validation lives in db_file_load.cpp/db_stream.cpp. The trace
// snapshot observes these owners and cannot control load success or retry.
void DB_RuntimeGeneratedFailure(const char *stage);
bool DB_RuntimeGeneratedLoadFailed();
bool DB_RuntimeStreamCanRead(std::size_t size);
