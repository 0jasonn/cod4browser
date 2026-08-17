// TEMPORARY CANONICAL-RUNTIME INTEGRATION PREFIX -- SHRINK-ONLY CONTRACT.
//
// Native db_registry.cpp owns DB_BuildOSPath, DB_TryLoadXFile*, DB_Thread,
// DB_LoadXZone, DB_LoadZone_f, DB_InitThread, and DB_LoadXAssets. They remain
// here only because db_registry.cpp's dependency closure is not yet portable
// enough for this Wasm target. The DB_Runtime* functions are temporary
// deterministic trace/failure scaffolding; the exported start function is a
// browser platform hook. Do not add canonical DB behavior here when its native
// owner can instead be compiled. Move browser I/O through narrow Sys/FS/thread
// interfaces and delete each duplicate as its native translation unit lands.

#include <database/db_runtime_prefix.h>

#include <database/database.h>
#include <database/db_initialization.h>
#include <database/db_registry_pools.h>
#include <database/db_registry_publication.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <qcommon/threads.h>
#include <universal/q_shared.h>
#include <universal/physicalmemory.h>
#include <web/web_database_filesystem.h>
#include <web/web_system.h>

#include <emscripten.h>

#include <array>
#include <cstring>

namespace
{
struct ZoneRequest
{
    char name[64]{};
    int flags = 0;
};

DBRuntimeTraceSnapshot g_trace;
std::array<ZoneRequest, 8> g_zoneInfo{};
std::array<std::uint8_t, 32> g_zoneHandles{};
std::uint32_t g_zoneInfoCount = 0;
std::uint32_t g_zoneCount = 0;
std::uint32_t g_loadingAssets = 0;
bool g_zoneInited = false;
bool g_databaseThreadEntered = false;
char g_logicalPath[256]{};
cmd_function_s g_loadZoneCommand{};
alignas(16) std::array<std::uint8_t, 0x80000> g_fileBuffer{};

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

void DB_BuildOSPath(const char *zoneName, std::uint32_t size, char *filename)
{
    Com_sprintf(filename, size, "zone/english/%s.ff", zoneName);
    g_trace.logicalPath = g_logicalPath;
    Trace("DB_BuildOSPath");
    Trace("resolved logical path");
}

bool DB_TryLoadXFileInternal(char *zoneName, int zoneFlags)
{
    Trace("DB_TryLoadXFileInternal");
    DB_BuildOSPath(zoneName, sizeof(g_logicalPath), g_logicalPath);
    Trace("FS/platform open");
    const WebDatabaseFile file = WebDatabaseFS_Open(g_logicalPath);
    if (file == WEB_DATABASE_INVALID_FILE)
    {
        Stop("FS/platform open failed");
        return false;
    }
    g_trace.openSucceeded = true;
    Trace("FS/platform open success");

    const std::int64_t size = WebDatabaseFS_Size(file);
    if (size < 14 || size > UINT32_MAX)
    {
        WebDatabaseFS_Close(file);
        Stop("zone file size invalid");
        return false;
    }
    g_trace.fileSize = static_cast<std::uint32_t>(size);

    std::uint32_t zoneIndex = 0;
    for (std::uint32_t index = 1; index < ASSET_TYPE_COUNT; ++index)
    {
        if (!g_zones[index].name[0]) { zoneIndex = index; break; }
    }
    iassert(zoneIndex != 0);
    XZone &zone = g_zones[zoneIndex];
    std::memset(&zone, 0, sizeof(zone));
    I_strncpyz(zone.name, zoneName, sizeof(zone.name));
    zone.flags = zoneFlags;
    zone.fileSize = g_trace.fileSize;
    g_zoneHandles[g_zoneCount++] = static_cast<std::uint8_t>(zoneIndex);

    const int allocType = zoneFlags == 1 || zoneFlags == 4 ||
        zoneFlags == 16 || zoneFlags == 32 || zoneFlags == 64 ? 1 : 0;
    zone.allocType = allocType;
    DB_SetLoadingZoneIndex(zoneIndex);
    PMem_BeginAlloc(zone.name, static_cast<std::uint32_t>(allocType));
    DB_LoadXFile(
        g_logicalPath,
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(file) + 1u),
        zone.name,
        &zone.mem,
        nullptr,
        g_fileBuffer.data(),
        allocType);
    DB_LoadXFileInternal();
    PMem_EndAlloc(zone.name, static_cast<std::uint32_t>(allocType));
    return g_trace.streamInitialized;
}

void DB_TryLoadXFile()
{
    Trace("DB_TryLoadXFile");
    const std::uint32_t count = g_zoneInfoCount;
    g_zoneInfoCount = 0;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        (void)DB_TryLoadXFileInternal(g_zoneInfo[index].name, g_zoneInfo[index].flags);
        if (g_loadingAssets) --g_loadingAssets;
    }
    Sys_DatabaseCompleted();
}

void DB_Thread(std::uint32_t threadContext)
{
    iassert(threadContext == THREAD_CONTEXT_DATABASE);
    if (!g_databaseThreadEntered)
    {
        g_databaseThreadEntered = true;
        g_trace.threadInitialized = true;
        Trace("DB_Thread initialized");
        return;
    }
    DB_TryLoadXFile();
}

void DB_LoadXZone(XZoneInfo *zoneInfo, std::uint32_t zoneCount)
{
    Trace("DB_LoadXZone");
    iassert(g_zoneInfoCount == 0);
    iassert(g_loadingAssets == 0);
    for (std::uint32_t index = 0; index < zoneCount; ++index)
    {
        if (!zoneInfo[index].name) continue;
        iassert(g_zoneInfoCount < g_zoneInfo.size());
        I_strncpyz(g_zoneInfo[g_zoneInfoCount].name, zoneInfo[index].name, 64);
        g_zoneInfo[g_zoneInfoCount].flags = zoneInfo[index].allocFlags;
        ++g_zoneInfoCount;
    }
    g_loadingAssets = g_zoneInfoCount;
    Sys_WakeDatabase2();
    Sys_WakeDatabase();
    Sys_NotifyDatabase();
}

void DB_LoadZone_f()
{
    if (Cmd_Argc() != 2)
    {
        Com_Printf(CON_CHANNEL_SYSTEM, "usage: loadzone <zone>\n");
        return;
    }
    XZoneInfo zoneInfo{Cmd_Argv(1), 0, 0};
    DB_LoadXAssets(&zoneInfo, 1, 1);
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
    for (std::uint32_t index = 0; index < 9; ++index)
    {
        const std::uint8_t *position = index == g_streamPosIndex
            ? g_streamPos : g_streamPosArray[index];
        const std::uint8_t *base = g_streamZoneMem->blocks[index].data;
        g_trace.streamOffsets[index] = position && base && position >= base
            ? static_cast<std::uint32_t>(position - base) : 0u;
    }
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

void DB_InitThread()
{
    Trace("DB_InitThread");
    if (!Sys_SpawnDatabaseThread(DB_Thread))
        Sys_Error("Failed to create database execution context");
}

void DB_LoadXAssets(XZoneInfo *zoneInfo, std::uint32_t zoneCount, int sync)
{
    Trace("DB_LoadXAssets");
    iassert(Sys_IsMainThread());
    iassert(zoneInfo && zoneCount);
    if (!g_zoneInited)
    {
        g_zoneInited = true;
        Trace("DB_Init");
        DB_InitAssetPools();
        g_trace.initializedPoolCount = static_cast<std::uint32_t>(DB_GetInitializedAssetPoolCount());
        g_trace.freeAssetEntryCount = static_cast<std::uint32_t>(DB_GetFreeAssetEntryCount());
        Trace("asset-pool initialization");
        Cmd_AddCommandInternal("loadzone", DB_LoadZone_f, &g_loadZoneCommand);
    }
    DB_LoadXZone(zoneInfo, zoneCount);
    if (sync) Sys_SyncDatabase();
}

const DBRuntimeTraceSnapshot &DB_GetRuntimeTrace()
{
    return g_trace;
}

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_StartCanonicalDbRuntimeCheck()
{
    // The isolated Com_Init prefix intentionally published its trace while the
    // native $init high-allocation scope was still open. The mounted DB request
    // occurs after that boundary, matching the native ordering where $init is
    // closed and database initialization is released before high-zone PMem.
    const PhysicalMemory *memory = PMem_GetState();
    if (memory->prim[1].allocName)
    {
        const char *initScope = memory->prim[1].allocName;
        PMem_EndAlloc(initScope, 1u);
        DB_SetInitializing(false);
    }
    XZoneInfo zoneInfo{"code_post_gfx", 4, 0};
    DB_LoadXAssets(&zoneInfo, 1, 1);
}
