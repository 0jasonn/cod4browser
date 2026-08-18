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
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <qcommon/threads.h>
#include <universal/physicalmemory.h>
#include <universal/q_shared.h>

#include <array>
#include <cstring>

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
std::uint32_t g_zoneAllocType = 0;
volatile std::uint32_t g_loadingAssets = 0;
std::array<XZoneInfoInternal, 8> g_zoneInfo{};
std::int32_t g_sync = 0;
bool g_databaseThreadEntered = false;
alignas(16) std::array<std::uint8_t, 0x80000> g_fileBuf{};
cmd_function_s DB_LoadZone_f_VAR{};

void DB_BuildOSPath(const char *zoneName, std::uint32_t size, char *filename)
{
    DB_PlatformBuildZonePath(zoneName, size, filename);
    DB_RuntimeSetLogicalPath(filename);
    DB_RuntimeTraceStage("DB_BuildOSPath");
    DB_RuntimeTraceStage("resolved logical path");
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
    iassert(!g_zoneInfoCount);

    char filename[256]{};
    DB_BuildOSPath(zoneName, sizeof(filename), filename);
    DB_RuntimeTraceStage("FS/platform open");
    const DBPlatformFile zoneFile = DB_PlatformOpenFile(filename);
    if (zoneFile == DB_PLATFORM_INVALID_FILE)
    {
        DB_RuntimeTraceStop("FS/platform open failed");
        return 0;
    }
    DB_RuntimeTraceOpenSucceeded();
    DB_RuntimeTraceStage("FS/platform open success");

    const std::int64_t fileSize = DB_PlatformFileSize(zoneFile);
    if (fileSize < 14 || fileSize > UINT32_MAX)
    {
        DB_PlatformCloseFile(zoneFile);
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
    DB_SetLoadingZoneIndex(g_zoneIndex);
    DB_LoadXFile(filename, DB_PlatformFileToOpaque(zoneFile), zone->name,
        &zone->mem, nullptr, g_fileBuf.data(), zone->allocType);
    DB_LoadXFileInternal();
    PMem_EndAlloc(zone->name, g_zoneAllocType);
    iassert(g_loadingZone);
    g_loadingZone = false;
    return DB_RuntimeGeneratedLoadFailed() ? 0 : 1;
}

void DB_TryLoadXFile()
{
    DB_RuntimeTraceStage("DB_TryLoadXFile");
    if (!g_zoneInfoCount)
    {
        iassert(!g_loadingAssets);
        return;
    }

    const std::uint32_t zoneInfoCount = g_zoneInfoCount;
    g_zoneInfoCount = 0;
    iassert(!g_loadingZone);
    for (std::uint32_t index = 0; index < zoneInfoCount; ++index)
    {
        (void)DB_TryLoadXFileInternal(
            g_zoneInfo[index].name, g_zoneInfo[index].flags);
        g_loadingAssets = g_loadingAssets - 1u;
    }
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
    iassert(Sys_IsMainThread());
    iassert(zoneInfo && zoneCount);

    if (!g_zoneInited)
    {
        g_zoneInited = true;
        DB_RuntimeTraceStage("DB_Init");
        DB_InitAssetPools();
        DB_RuntimeTracePoolsInitialized(
            static_cast<std::uint32_t>(DB_GetInitializedAssetPoolCount()),
            static_cast<std::uint32_t>(DB_GetFreeAssetEntryCount()));
        DB_RuntimeTraceStage("asset-pool initialization");
        Cmd_AddCommandInternal("loadzone", DB_LoadZone_f, &DB_LoadZone_f_VAR);
    }

    // Browser SP has no previously loaded zone in this slice, so the native
    // renderer/archive/unload branch has no work. Keep its ownership gated
    // until those engine subsystems compile rather than substituting behavior.
    g_sync = sync;
    DB_LoadXZone(zoneInfo, zoneCount);
    if (sync) Sys_SyncDatabase();
}
