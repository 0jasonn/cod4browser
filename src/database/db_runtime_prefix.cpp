// TEMPORARY CANONICAL-RUNTIME INTEGRATION PREFIX -- SHRINK-ONLY CONTRACT.
//
// Canonical db_registry.cpp now owns the DB lifecycle. This file contains only
// temporary deterministic trace/failure scaffolding and the exported browser
// start hook. It remains shrink-only.

#include <database/db_runtime_prefix.h>

#include <database/database.h>
#include <universal/q_shared.h>

#include <emscripten.h>

#include <cstring>
#include <iterator>

namespace
{
DBRuntimeTraceSnapshot g_trace;
char g_logicalPath[256]{};

EM_JS(void, EmitDatabaseTrace, (
    const char *stage, const char *path, std::uint32_t bytesRead,
    std::uint32_t fileSize, std::uint32_t readOffset,
    std::uint32_t requestedBytes, std::uint32_t poolCount,
    std::uint32_t freeEntryCount, int threadInitialized,
    int headerValid, int openSucceeded, const char *stopStage,
    std::uint32_t compressedBytesConsumed,
    std::uint32_t decompressedBytesProduced,
    std::uint32_t inputRefillCount, std::uint32_t xfileSize,
    std::uint32_t xfileExternalSize, const std::uint32_t *blockSizes,
    std::uint32_t blockAllocationCount, std::uint32_t blockAllocationBytes,
    std::uint32_t streamBlock, std::uint32_t streamOffset,
    int inflateInitialized, int streamInitialized, int cleanupComplete,
    int xassetListBegin, int xassetListEnd,
    std::uint32_t scriptStringCount, std::uint32_t scriptStringObservedCount,
    const char *scriptStringIdentity, std::uint32_t xassetCount,
    std::uint32_t assetIndex, std::uint32_t assetType, const char *assetName,
    const char *pointerClassification, int publicationBegin, int publicationEnd,
    std::uint32_t assetEntryIndex, std::uint32_t assetPoolIndex,
    std::uint32_t freeEntryCountBefore, std::uint32_t freeEntryCountAfter,
    std::uint32_t assetHash, std::uint32_t assetZoneIndex,
    int generatedLoadFailed, const std::uint32_t *streamOffsets), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:database", { detail: {
        stage: UTF8ToString(stage),
        logicalPath: path ? UTF8ToString(path) : "",
        bytesRead: bytesRead >>> 0,
        fileSize: fileSize >>> 0,
        readOffset: readOffset >>> 0,
        requestedBytes: requestedBytes >>> 0,
        initializedPoolCount: poolCount >>> 0,
        freeAssetEntryCount: freeEntryCount >>> 0,
        threadInitialized: Boolean(threadInitialized),
        headerValid: Boolean(headerValid),
        openSucceeded: Boolean(openSucceeded),
        stopStage: stopStage ? UTF8ToString(stopStage) : "",
        compressedBytesConsumed: compressedBytesConsumed >>> 0,
        decompressedBytesProduced: decompressedBytesProduced >>> 0,
        inputRefillCount: inputRefillCount >>> 0,
        xfileSize: xfileSize >>> 0,
        xfileExternalSize: xfileExternalSize >>> 0,
        blockSizes: Array.from(HEAPU32.subarray(blockSizes >>> 2, (blockSizes >>> 2) + 9)),
        blockAllocationCount: blockAllocationCount >>> 0,
        blockAllocationBytes: blockAllocationBytes >>> 0,
        streamBlock: streamBlock >>> 0,
        streamOffset: streamOffset >>> 0,
        inflateInitialized: Boolean(inflateInitialized),
        streamInitialized: Boolean(streamInitialized),
        cleanupComplete: Boolean(cleanupComplete),
        xassetListBegin: Boolean(xassetListBegin),
        xassetListEnd: Boolean(xassetListEnd),
        scriptStringCount: scriptStringCount >>> 0,
        scriptStringObservedCount: scriptStringObservedCount >>> 0,
        scriptStringIdentity: scriptStringIdentity ? UTF8ToString(scriptStringIdentity) : "",
        xassetCount: xassetCount >>> 0,
        assetIndex: assetIndex >>> 0,
        assetType: assetType >>> 0,
        assetName: assetName ? UTF8ToString(assetName) : "",
        pointerClassification: pointerClassification ? UTF8ToString(pointerClassification) : "",
        publicationBegin: Boolean(publicationBegin),
        publicationEnd: Boolean(publicationEnd),
        assetEntryIndex: assetEntryIndex >>> 0,
        assetPoolIndex: assetPoolIndex >>> 0,
        freeEntryCountBefore: freeEntryCountBefore >>> 0,
        freeEntryCountAfter: freeEntryCountAfter >>> 0,
        assetHash: assetHash >>> 0,
        assetZoneIndex: assetZoneIndex >>> 0,
        generatedLoadFailed: Boolean(generatedLoadFailed),
        streamOffsets: Array.from(HEAPU32.subarray(streamOffsets >>> 2, (streamOffsets >>> 2) + 9)),
    }}));
});

const char *CurrentScriptIdentity()
{
    return g_trace.scriptStringObservedCount
        ? g_trace.scriptStringIdentities[g_trace.scriptStringObservedCount - 1]
        : "";
}

void CaptureStreamState()
{
    g_trace.streamBlock = g_streamPosIndex;
    for (std::uint32_t index = 0; index < 9; ++index)
    {
        const std::uint8_t *position = index == g_streamPosIndex
            ? g_streamPos : g_streamPosArray[index];
        const std::uint8_t *base = g_streamZoneMem
            ? g_streamZoneMem->blocks[index].data : nullptr;
        g_trace.streamOffsets[index] = position && base && position >= base
            ? static_cast<std::uint32_t>(position - base) : 0u;
    }
    g_trace.streamOffset = g_trace.streamBlock < 9
        ? g_trace.streamOffsets[g_trace.streamBlock] : 0u;
}

void Trace(const char *stage)
{
    if (g_trace.stageCount < std::size(g_trace.stages))
        g_trace.stages[g_trace.stageCount++] = stage;
    EmitDatabaseTrace(stage, g_trace.logicalPath, g_trace.bytesRead,
        g_trace.fileSize, g_trace.readOffset, g_trace.requestedBytes,
        g_trace.initializedPoolCount,
        g_trace.freeAssetEntryCount, g_trace.threadInitialized,
        g_trace.headerValid, g_trace.openSucceeded, g_trace.stopStage,
        g_trace.compressedBytesConsumed, g_trace.decompressedBytesProduced,
        g_trace.inputRefillCount, g_trace.xfileSize, g_trace.xfileExternalSize,
        g_trace.blockSizes, g_trace.blockAllocationCount,
        g_trace.blockAllocationBytes, g_trace.streamBlock,
        g_trace.streamOffset, g_trace.inflateInitialized,
        g_trace.streamInitialized, g_trace.cleanupComplete,
        g_trace.xassetListBegin, g_trace.xassetListEnd,
        g_trace.scriptStringCount, g_trace.scriptStringObservedCount,
        CurrentScriptIdentity(), g_trace.xassetCount, g_trace.assetIndex,
        g_trace.assetType, g_trace.assetName, g_trace.pointerClassification,
        g_trace.publicationBegin, g_trace.publicationEnd,
        g_trace.assetEntryIndex, g_trace.assetPoolIndex,
        g_trace.freeEntryCountBefore, g_trace.freeEntryCountAfter,
        g_trace.assetHash, g_trace.assetZoneIndex,
        g_trace.generatedLoadFailed, g_trace.streamOffsets);
}

void Stop(const char *stage)
{
    g_trace.stopStage = stage;
    EmitDatabaseTrace("DB stop", g_trace.logicalPath, g_trace.bytesRead,
        g_trace.fileSize, g_trace.readOffset, g_trace.requestedBytes,
        g_trace.initializedPoolCount,
        g_trace.freeAssetEntryCount, g_trace.threadInitialized,
        g_trace.headerValid, g_trace.openSucceeded, stage,
        g_trace.compressedBytesConsumed, g_trace.decompressedBytesProduced,
        g_trace.inputRefillCount, g_trace.xfileSize, g_trace.xfileExternalSize,
        g_trace.blockSizes, g_trace.blockAllocationCount,
        g_trace.blockAllocationBytes, g_trace.streamBlock,
        g_trace.streamOffset, g_trace.inflateInitialized,
        g_trace.streamInitialized, g_trace.cleanupComplete,
        g_trace.xassetListBegin, g_trace.xassetListEnd,
        g_trace.scriptStringCount, g_trace.scriptStringObservedCount,
        CurrentScriptIdentity(), g_trace.xassetCount, g_trace.assetIndex,
        g_trace.assetType, g_trace.assetName, g_trace.pointerClassification,
        g_trace.publicationBegin, g_trace.publicationEnd,
        g_trace.assetEntryIndex, g_trace.assetPoolIndex,
        g_trace.freeEntryCountBefore, g_trace.freeEntryCountAfter,
        g_trace.assetHash, g_trace.assetZoneIndex,
        g_trace.generatedLoadFailed, g_trace.streamOffsets);
}

} // namespace

void DB_RuntimeTraceStage(const char *stage)
{
    Trace(stage);
}

void DB_RuntimeTraceStop(const char *stage)
{
    Stop(stage);
}

void DB_RuntimeSetLogicalPath(const char *path)
{
    I_strncpyz(g_logicalPath, path ? path : "", sizeof(g_logicalPath));
    g_trace.logicalPath = g_logicalPath;
}

void DB_RuntimeSetFileSize(std::uint32_t fileSize)
{
    g_trace.fileSize = fileSize;
}

void DB_RuntimeTraceOpenSucceeded()
{
    g_trace.openSucceeded = true;
}

void DB_RuntimeTraceThreadInitialized()
{
    g_trace.threadInitialized = true;
}

void DB_RuntimeTracePoolsInitialized(
    std::uint32_t poolCount, std::uint32_t freeEntryCount)
{
    g_trace.initializedPoolCount = poolCount;
    g_trace.freeAssetEntryCount = freeEntryCount;
}

void DB_RuntimeTraceHeaderRead(std::uint32_t bytesRead, std::uint32_t fileSize)
{
    g_trace.headerValid = true;
    if (fileSize) g_trace.fileSize = fileSize;
    g_trace.requestedBytes = bytesRead;
    Trace("zone header read");
}

void DB_RuntimeTraceInputRefill(std::uint32_t bytesRead)
{
    g_trace.readOffset = g_trace.bytesRead;
    g_trace.requestedBytes = 0x40000u;
    g_trace.bytesRead += bytesRead;
    ++g_trace.inputRefillCount;
    Trace("compressed input refill");
}

void DB_RuntimeTraceInflate(
    std::uint32_t compressedBytesConsumed,
    std::uint32_t decompressedBytesProduced)
{
    g_trace.compressedBytesConsumed = compressedBytesConsumed;
    g_trace.decompressedBytesProduced = decompressedBytesProduced;
    Trace("inflate progress");
}

void DB_RuntimeTraceInflateInitialized()
{
    g_trace.inflateInitialized = true;
    Trace("inflate init");
}

void DB_RuntimeTraceXFile(
    std::uint32_t size,
    std::uint32_t externalSize,
    const std::uint32_t *blockSizes)
{
    g_trace.xfileSize = size;
    g_trace.xfileExternalSize = externalSize;
    std::memcpy(g_trace.blockSizes, blockSizes, sizeof(g_trace.blockSizes));
    Trace("XFile block sizes");
}

void DB_RuntimeTraceBlockAllocation(std::uint32_t blockIndex, std::uint32_t size)
{
    (void)blockIndex;
    ++g_trace.blockAllocationCount;
    g_trace.blockAllocationBytes += size;
    Trace("PMem block allocation");
}

void DB_RuntimeTraceStreamsInitialized(std::uint32_t block, std::uint32_t offset)
{
    g_trace.streamBlock = block;
    g_trace.streamOffset = offset;
    g_trace.streamInitialized = true;
    Trace("stream block initialization");
}

void DB_RuntimeTraceCleanupComplete()
{
    g_trace.cleanupComplete = true;
    Trace("XFile cleanup");
}

void DB_RuntimeTraceXAssetListBegin(
    std::int32_t scriptStringCount, std::int32_t assetCount)
{
    g_trace.xassetListBegin = true;
    g_trace.scriptStringCount = scriptStringCount >= 0
        ? static_cast<std::uint32_t>(scriptStringCount) : UINT32_MAX;
    g_trace.xassetCount = assetCount >= 0
        ? static_cast<std::uint32_t>(assetCount) : UINT32_MAX;
    Trace("XAssetList begin");
}

void DB_RuntimeTraceXAssetListEnd()
{
    g_trace.xassetListEnd = true;
    CaptureStreamState();
    Trace("XAssetList end");
}

void DB_RuntimeTraceScriptString(std::uint32_t index, const char *identity)
{
    if (index < std::size(g_trace.scriptStringIdentities))
        I_strncpyz(g_trace.scriptStringIdentities[index], identity ? identity : "",
            sizeof(g_trace.scriptStringIdentities[index]));
    g_trace.scriptStringObservedCount = index + 1;
    Trace("script string");
}

void DB_RuntimeTraceAssetBegin(
    std::uint32_t index, XAssetType type, const char *pointerClassification)
{
    g_trace.assetIndex = index;
    g_trace.assetType = static_cast<std::uint32_t>(type);
    I_strncpyz(g_trace.pointerClassification,
        pointerClassification ? pointerClassification : "",
        sizeof(g_trace.pointerClassification));
    Trace("XAsset begin");
}

void DB_RuntimeTraceAssetLoaded(const char *name)
{
    I_strncpyz(g_trace.assetName, name ? name : "", sizeof(g_trace.assetName));
    Trace("XAsset loaded");
}

void DB_RuntimeTracePublicationBegin(
    XAssetType type, const char *name, std::size_t freeEntryCount)
{
    g_trace.publicationBegin = true;
    g_trace.assetType = static_cast<std::uint32_t>(type);
    I_strncpyz(g_trace.assetName, name ? name : "", sizeof(g_trace.assetName));
    g_trace.freeEntryCountBefore = static_cast<std::uint32_t>(freeEntryCount);
    Trace("publication begin");
}

void DB_RuntimeTracePublicationEnd(
    XAssetType type, const char *name, std::uint32_t entryIndex,
    std::uint32_t poolIndex, std::size_t freeBefore, std::size_t freeAfter,
    std::uint32_t hash, std::uint32_t zoneIndex)
{
    g_trace.publicationEnd = true;
    g_trace.assetType = static_cast<std::uint32_t>(type);
    I_strncpyz(g_trace.assetName, name ? name : "", sizeof(g_trace.assetName));
    g_trace.assetEntryIndex = entryIndex;
    g_trace.assetPoolIndex = poolIndex;
    g_trace.freeEntryCountBefore = static_cast<std::uint32_t>(freeBefore);
    g_trace.freeEntryCountAfter = static_cast<std::uint32_t>(freeAfter);
    g_trace.assetHash = hash;
    g_trace.assetZoneIndex = zoneIndex;
    Trace("publication end");
}

void DB_RuntimeGeneratedFailure(const char *stage)
{
    if (!g_trace.generatedLoadFailed)
    {
        g_trace.generatedLoadFailed = true;
        CaptureStreamState();
        DB_FailXFileLoad(stage);
        Trace(stage);
    }
}

bool DB_RuntimeGeneratedLoadFailed()
{
    return g_trace.generatedLoadFailed || DB_HasXFileLoadFailure();
}

bool DB_RuntimeStreamCanRead(std::size_t size)
{
    if (g_streamPosIndex >= 9 || !g_streamZoneMem || !g_streamPos) return false;
    const XBlock &block = g_streamZoneMem->blocks[g_streamPosIndex];
    if (!block.data || g_streamPos < block.data) return size == 0;
    const std::size_t offset = static_cast<std::size_t>(g_streamPos - block.data);
    return offset <= block.size && size <= block.size - offset;
}

const DBRuntimeTraceSnapshot &DB_GetRuntimeTrace()
{
    return g_trace;
}
