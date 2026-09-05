// Browser-SP compilation slice of canonical db_registry.cpp ownership.
// Keep behavior here engine-facing; browser storage and Worker details live
// below db_file_platform.h and the Sys thread/event interface.

#include <database/database.h>
#include <database/db_file_platform.h>
#include <database/db_initialization.h>
#include <database/db_registry_pools.h>
#include <database/db_registry_publication.h>
#include <database/db_runtime_prefix.h>
#include <qcommon/cmd.h>
#include <qcommon/engine_lifecycle_trace.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <qcommon/loading_keepalive.h>
#include <qcommon/threads.h>
#include <universal/physicalmemory.h>
#include <universal/q_shared.h>
#include <xanim/xmodel.h>
#include <bgame/bg_local.h>
#include <gfx_d3d/r_material.h>

#include <array>
#include <cstring>

fileData_s *com_fileDataHashTable[1024]{};

void __cdecl DB_Cleanup()
{
    // Same native synchronization boundary. This synchronous XFile path has
    // no native g_archiveBuf renderer-thread upload transaction to unwind.
    Sys_SyncDatabase();
}

const char *g_assetNames[ASSET_TYPE_COUNT] =
{
    "xmodelpieces", "physpreset", "xanim", "xmodel", "material",
    "techset", "image", "sound", "sndcurve", "loaded_sound",
    "col_map_sp", "col_map_mp", "com_map", "game_map_sp",
    "game_map_mp", "map_ents", "gfx_map", "lightdef", "ui_map",
    "font", "menufile", "menu", "localize", "weapon",
    "snddriverglobals", "fx", "impactfx", "aitype", "mptype",
    "character", "xmodelalias", "rawfile", "stringtable"
};

namespace
{
struct XZoneInfoInternal
{
    char name[64]{};
    int flags = 0;
};

bool g_zoneInited = false;
std::int32_t g_zoneCount = 0;
std::array<std::uint8_t, 32> g_zoneHandles{};
volatile bool g_loadingZone = false;
volatile std::uint32_t g_zoneInfoCount = 0;
std::uint32_t g_pendingZoneInfoCount = 0;
std::uint32_t g_pendingZoneInfoIndex = 0;
std::array<std::uint8_t, 8> g_pendingLoadedZoneIndices{};
std::uint32_t g_pendingLoadedZoneCount = 0;
std::uint32_t g_zoneAllocType = 0;
volatile std::uint32_t g_loadingAssets = 0;
std::array<XZoneInfoInternal, 8> g_zoneInfo{};
std::int32_t g_sync = 0;
bool g_databaseThreadEntered = false;
alignas(16) std::array<std::uint8_t, 0x80000> g_fileBuf{};
cmd_function_s DB_LoadZone_f_VAR{};

void CompactReleasedZoneHandles()
{
    std::int32_t writeIndex = 0;
    for (std::int32_t readIndex = 0; readIndex < g_zoneCount; ++readIndex)
    {
        const std::uint8_t handle = g_zoneHandles[readIndex];
        if (handle && g_zones[handle].name[0])
            g_zoneHandles[writeIndex++] = handle;
    }
    for (std::int32_t index = writeIndex; index < g_zoneCount; ++index)
        g_zoneHandles[index] = 0;
    g_zoneCount = writeIndex;
}

void DB_BuildOSPath(const char *zoneName, std::uint32_t size, char *filename)
{
    DB_PlatformBuildZonePath(zoneName, size, filename);
    DB_RuntimeSetLogicalPath(filename);
    DB_RuntimeTraceStage("DB_BuildOSPath");
    DB_RuntimeTraceStage("resolved logical path");
}

bool DB_PreflightXFile(const char *zoneName)
{
    char filename[256]{};
    DB_BuildOSPath(zoneName, sizeof(filename), filename);
    DB_RuntimeTraceStage("FS/platform preflight");

    const DBPlatformFile zoneFile = DB_PlatformOpenFile(filename);
    if (zoneFile == DB_PLATFORM_INVALID_FILE)
    {
        DB_FailXFileLoad("FS/platform preflight failed");
        Com_Printf(CON_CHANNEL_ERROR,
            "Could not open required fastfile '%s'. Re-select the owned "
            "installation if this map was added after the current browser "
            "import. The currently published world was preserved.\n",
            filename);
        DB_RuntimeTraceStop("FS/platform preflight failed");
        return false;
    }

    const std::int64_t fileSize = DB_PlatformFileSize(zoneFile);
    DB_PlatformCloseFile(zoneFile);
    if (fileSize < 14 || fileSize > UINT32_MAX)
    {
        DB_FailXFileLoad("zone file preflight size invalid");
        Com_Printf(CON_CHANNEL_ERROR,
            "Required fastfile '%s' has an invalid size. The currently "
            "published world was preserved.\n",
            filename);
        DB_RuntimeTraceStop("zone file preflight size invalid");
        return false;
    }

    return true;
}

std::int32_t DB_GetZoneAllocType(std::int32_t zoneFlags)
{
    switch (zoneFlags)
    {
    case 1:
    case 4:
    case 16:
    case 32:
    case 64:
        return 1;
    default:
        return 0;
    }
}

std::int32_t DB_TryLoadXFileInternal(char *zoneName, std::int32_t zoneFlags)
{
    DB_RuntimeTraceStage("DB_TryLoadXFileInternal");
    EmitEngineLifecycleTrace(
        EngineLifecycleStage::LogicalFastfileRequest,
        zoneName,
        1,
        zoneFlags);
    iassert(!g_zoneInfoCount);

    char filename[256]{};
    DB_BuildOSPath(zoneName, sizeof(filename), filename);
    DB_RuntimeTraceStage("FS/platform open");
    const DBPlatformFile zoneFile = DB_PlatformOpenFile(filename);
    if (zoneFile == DB_PLATFORM_INVALID_FILE)
    {
        DB_FailXFileLoad("FS/platform open failed");
        Com_Printf(CON_CHANNEL_ERROR,
            "Could not open required fastfile '%s'. Re-select the owned "
            "installation if this map was added after the current browser "
            "import.\n",
            filename);
        DB_RuntimeTraceStop("FS/platform open failed");
        return 0;
    }
    DB_RuntimeTraceOpenSucceeded();
    DB_RuntimeTraceStage("FS/platform open success");

    const std::int64_t fileSize = DB_PlatformFileSize(zoneFile);
    if (fileSize < 14 || fileSize > UINT32_MAX)
    {
        DB_PlatformCloseFile(zoneFile);
        DB_FailXFileLoad("zone file size invalid");
        Com_Printf(CON_CHANNEL_ERROR,
            "Required fastfile '%s' has an invalid size.\n", filename);
        DB_RuntimeTraceStop("zone file size invalid");
        return 0;
    }
    DB_RuntimeSetFileSize(static_cast<std::uint32_t>(fileSize));

    g_zoneIndex = 0;
    for (std::uint32_t index = 1; index < ASSET_TYPE_COUNT; ++index)
    {
        if (!g_zones[index].name[0])
        {
            g_zoneIndex = index;
            break;
        }
    }
    iassert(g_zoneIndex);
    iassert(zoneName && zoneName[0]);

    XZone *zone = &g_zones[g_zoneIndex];
    std::memset(zone, 0, sizeof(*zone));
    g_zoneHandles[static_cast<std::size_t>(g_zoneCount++)] =
        static_cast<std::uint8_t>(g_zoneIndex);
    I_strncpyz(zone->name, zoneName, sizeof(zone->name));
    zone->flags = zoneFlags;
    zone->fileSize = static_cast<std::int32_t>(fileSize);
    zone->modZone = false;

    iassert(!g_loadingZone);
    g_loadingZone = true;
    g_zoneAllocType = static_cast<std::uint32_t>(DB_GetZoneAllocType(zoneFlags));
    iassert(!(g_zoneAllocType == 1 && g_initializing));
    PMem_BeginAlloc(zone->name, g_zoneAllocType);
    zone->allocType = static_cast<std::int32_t>(g_zoneAllocType);
    DB_ResetZoneSize((zoneFlags & 8) != 0);
    DB_SetLoadingZoneIndex(g_zoneIndex);
    DB_LoadXFile(filename, DB_PlatformFileToOpaque(zoneFile), zone->name,
        &zone->mem, Sys_LoadingKeepAlive, g_fileBuf.data(), zone->allocType);
    DB_LoadXFileInternal();
    PMem_EndAlloc(zone->name, g_zoneAllocType);
    iassert(g_loadingZone);
    g_loadingZone = false;
    return DB_RuntimeGeneratedLoadFailed() ? 0 : 1;
}

void DB_TryLoadXFile()
{
    DB_RuntimeTraceStage("DB_TryLoadXFile");
    if (!g_pendingZoneInfoCount && !g_zoneInfoCount)
    {
        iassert(!g_loadingAssets);
        return;
    }

    if (!g_pendingZoneInfoCount)
    {
        g_pendingZoneInfoCount = g_zoneInfoCount;
        g_pendingZoneInfoIndex = 0;
        g_pendingLoadedZoneCount = 0;
        g_zoneInfoCount = 0;
    }
    iassert(!g_loadingZone);
    bool requestFailed = false;
    while (g_pendingZoneInfoIndex < g_pendingZoneInfoCount)
    {
        const std::uint32_t index = g_pendingZoneInfoIndex;
        // The native DB thread waits here while Com_Init owns the $init
        // physical-memory scope. A Worker has no second execution stack to
        // block, so retain the engine queue and resume it after Com_Init
        // releases that scope.
        if (DB_GetZoneAllocType(g_zoneInfo[index].flags) == 1 && g_initializing)
            return;
        if (!g_pendingZoneInfoIndex && !g_pendingLoadedZoneCount)
            DB_BeginXZonePublication();
        const bool loaded = DB_TryLoadXFileInternal(
            g_zoneInfo[index].name, g_zoneInfo[index].flags) != 0;
        if (g_zoneIndex > 0 && g_zoneIndex < ASSET_TYPE_COUNT &&
            g_zones[g_zoneIndex].name[0] &&
            !I_stricmp(g_zones[g_zoneIndex].name, g_zoneInfo[index].name))
        {
            iassert(g_pendingLoadedZoneCount <
                g_pendingLoadedZoneIndices.size());
            g_pendingLoadedZoneIndices[g_pendingLoadedZoneCount++] =
                static_cast<std::uint8_t>(g_zoneIndex);
        }
        ++g_pendingZoneInfoIndex;
        g_loadingAssets = g_loadingAssets - 1u;
        if (!loaded)
        {
            // A DB_LoadXAssets request is one publication transaction. Retire
            // the failed file and every earlier file from this request with a
            // single reverse-allocation-order PMem release, preserving zones
            // that existed before the request.
            DB_RollbackXZonePublication(g_pendingLoadedZoneIndices.data(),
                g_pendingLoadedZoneCount);
            CompactReleasedZoneHandles();
            requestFailed = true;
            g_loadingAssets -= g_pendingZoneInfoCount - g_pendingZoneInfoIndex;
            break;
        }
    }
    if (!requestFailed)
        DB_CommitXZonePublication();
    g_pendingZoneInfoCount = 0;
    g_pendingZoneInfoIndex = 0;
    g_pendingLoadedZoneCount = 0;
    iassert(!g_loadingZone);
    iassert(!g_loadingAssets);
    Sys_DatabaseCompleted();
}

void DB_Thread(std::uint32_t threadContext)
{
    iassert(threadContext == THREAD_CONTEXT_DATABASE);
    if (!g_databaseThreadEntered)
    {
        g_databaseThreadEntered = true;
        DB_RuntimeTraceThreadInitialized();
        DB_RuntimeTraceStage("DB_Thread initialized");
        return;
    }
    DB_TryLoadXFile();
}

void DB_LoadXZone(XZoneInfo *zoneInfo, std::uint32_t zoneCount)
{
    DB_RuntimeTraceStage("DB_LoadXZone");
    iassert(g_zoneCount < static_cast<std::int32_t>(g_zoneHandles.size()));
    iassert(!g_zoneInfoCount);
    iassert(!g_loadingAssets);

    std::uint32_t requestCount = 0;
    for (std::uint32_t index = 0; index < zoneCount; ++index)
    {
        if (!zoneInfo[index].name) continue;
        EmitEngineLifecycleTrace(
            EngineLifecycleStage::DatabaseLoadZone,
            zoneInfo[index].name,
            zoneCount,
            zoneInfo[index].allocFlags,
            zoneInfo[index].freeFlags);
        iassert(requestCount < g_zoneInfo.size());
        I_strncpyz(g_zoneInfo[requestCount].name, zoneInfo[index].name,
            sizeof(g_zoneInfo[requestCount].name));
        g_zoneInfo[requestCount].flags = zoneInfo[index].allocFlags;
        ++requestCount;
    }
    if (!requestCount) return;

    g_loadingAssets = requestCount;
    Sys_WakeDatabase2();
    Sys_WakeDatabase();
    g_zoneInfoCount = requestCount;
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

void __cdecl DB_InitThread()
{
    DB_RuntimeTraceStage("DB_InitThread");
    if (!Sys_SpawnDatabaseThread(DB_Thread))
        Sys_Error("Failed to create database thread");
}

void __cdecl Load_GetCurrentZoneHandle(std::uint8_t *handle)
{
    iassert(handle);
    iassert(g_loadingZone);
    *handle = static_cast<std::uint8_t>(g_zoneIndex);
}

void __cdecl DB_LoadXAssets(
    XZoneInfo *zoneInfo, std::uint32_t zoneCount, std::int32_t sync)
{
    DB_RuntimeTraceStage("DB_LoadXAssets");
    EmitEngineLifecycleTrace(
        EngineLifecycleStage::DatabaseLoadAssets,
        zoneInfo && zoneCount ? zoneInfo[0].name : nullptr,
        zoneCount,
        zoneInfo && zoneCount ? zoneInfo[0].allocFlags : 0,
        zoneInfo && zoneCount ? zoneInfo[0].freeFlags : 0,
        sync);
    iassert(Sys_IsMainThread());
    iassert(zoneInfo && zoneCount);

    if (!g_zoneInited)
    {
        g_zoneInited = true;
        DB_RuntimeTraceStage("DB_Init");
        DB_ResetStringOwnership();
        DB_InitAssetPools();
        DB_RuntimeTracePoolsInitialized(
            static_cast<std::uint32_t>(DB_GetInitializedAssetPoolCount()),
            static_cast<std::uint32_t>(DB_GetFreeAssetEntryCount()));
        DB_RuntimeTraceStage("asset-pool initialization");
        Cmd_AddCommandInternal("loadzone", DB_LoadZone_f, &DB_LoadZone_f_VAR);
    }

    // Validate the complete replacement set before retiring any live zone.
    // Browser imports can legitimately predate a newly requested campaign
    // fastfile; in that case keep the last published renderer world intact and
    // let the server load-failure path return without touching collision state.
    for (std::uint32_t index = 0; index < zoneCount; ++index)
        if (zoneInfo[index].name && !DB_PreflightXFile(zoneInfo[index].name))
            return;

    // Match native DB_LoadXAssets: retire all zones selected by the incoming
    // freeFlags mask before queuing replacement files.  Publication keeps
    // live primary header identity and promotes defaults/overrides at this
    // boundary; request history is not used as ownership metadata.
    for (std::uint32_t index = 0; index < zoneCount; ++index)
        if (zoneInfo[index].name)
            DB_UnloadXZonesForFreeFlags(zoneInfo[index].freeFlags);
    CompactReleasedZoneHandles();

    // The web client can retain startup/UI and map zones across repeated map
    // requests. Their canonical publication/unload work is complete before
    // this replacement queue is entered; no browser-side zone policy is used.
    g_sync = sync;
    DB_LoadXZone(zoneInfo, zoneCount);
    if (sync) Sys_SyncDatabase();
}

void __cdecl DB_ReleaseXAssets()
{
    iassert(Sys_IsMainThread());
    Sys_SyncDatabase();
    for (std::uint32_t hash = 0; hash < 0x8000u; ++hash)
    {
        for (std::uint32_t assetEntryIndex = db_hashTable[hash];
             assetEntryIndex;
             assetEntryIndex =
                 g_assetEntryPool[assetEntryIndex].entry.nextHash)
        {
            g_assetEntryPool[assetEntryIndex].entry.inuse = 0;
        }
    }
}

void __cdecl DB_EnumXAssets_FastFile(
    XAssetType type,
    void(__cdecl *func)(XAssetHeader, void *),
    void *inData,
    bool includeOverride)
{
    iassert(type >= 0 && type < ASSET_TYPE_COUNT);
    iassert(func);
    Sys_LockWrite(&db_hashCritSect);
    for (std::uint32_t hash = 0; hash < 0x8000u; ++hash)
    {
        for (std::uint32_t index = db_hashTable[hash]; index;
             index = g_assetEntryPool[index].entry.nextHash)
        {
            XAssetEntryPoolEntry *entry = &g_assetEntryPool[index];
            if (entry->entry.asset.type != type) continue;
            func(entry->entry.asset.header, inData);
            if (!includeOverride) continue;
            for (std::uint32_t overrideIndex = entry->entry.nextOverride;
                 overrideIndex;
                 overrideIndex =
                     g_assetEntryPool[overrideIndex].entry.nextOverride)
            {
                func(g_assetEntryPool[overrideIndex].entry.asset.header,
                    inData);
            }
        }
    }
    Sys_UnlockWrite(&db_hashCritSect);
}

void __cdecl DB_EnumXAssets(
    XAssetType type,
    void(__cdecl *func)(XAssetHeader, void *),
    void *inData,
    bool includeOverride)
{
    DB_EnumXAssets_FastFile(type, func, inData, includeOverride);
}

void __cdecl DB_Update()
{
    iassert(Sys_IsMainThread());
    if (!Sys_IsDatabaseReady2() && Sys_IsDatabaseReady())
    {
        // This is the no-copy branch of canonical DB_PostLoadXZone. The Web
        // loader publishes atomically during generated loading, so there is
        // no g_copyInfo archive/relink phase to perform here.
        Material_DirtyTechniqueSetOverrides();
        BG_FillInAllWeaponItems();
        Sys_DatabaseCompleted2();
    }
}

const char *__cdecl DB_GetXAssetTypeName(std::uint32_t type)
{
    iassert(type < ASSET_TYPE_COUNT);
    return g_assetNames[type];
}

bool __cdecl DB_IsXAssetDefault(XAssetType type, const char *name)
{
    XAssetEntryPoolEntry *entry = DB_FindXAssetEntryCanonical(type, name);
    return entry && entry->entry.zoneIndex == 0;
}

void __cdecl DB_ReplaceModel(const char *original, const char *replacement)
{
    XAssetEntryPoolEntry *originalEntry =
        DB_FindXAssetEntryCanonical(ASSET_TYPE_XMODEL, original);
    XAssetEntryPoolEntry *replacementEntry =
        DB_FindXAssetEntryCanonical(ASSET_TYPE_XMODEL, replacement);
    if (!originalEntry || !replacementEntry) return;

    const char *originalName = originalEntry->entry.asset.header.model->name;
    *originalEntry->entry.asset.header.model =
        *replacementEntry->entry.asset.header.model;
    originalEntry->entry.asset.header.model->name = originalName;
}
