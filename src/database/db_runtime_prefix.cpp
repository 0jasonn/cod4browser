#include <database/db_runtime_prefix.h>

#include <database/db_registry_pools.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <qcommon/threads.h>
#include <universal/q_shared.h>
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
std::array<XZone, 33> g_zones{};
std::array<std::uint8_t, 32> g_zoneHandles{};
std::uint32_t g_zoneInfoCount = 0;
std::uint32_t g_zoneCount = 0;
std::uint32_t g_loadingAssets = 0;
bool g_zoneInited = false;
bool g_databaseThreadEntered = false;
char g_logicalPath[256]{};
cmd_function_s g_loadZoneCommand{};

EM_JS(void, EmitDatabaseTrace, (
    const char *stage, const char *path, std::uint32_t bytesRead,
    std::uint32_t fileSize, std::uint32_t readOffset,
    std::uint32_t requestedBytes, std::uint32_t poolCount,
    std::uint32_t freeEntryCount, int threadInitialized,
    int headerValid, int openSucceeded, const char *stopStage), {
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
    }}));
});

void Trace(const char *stage)
{
    if (g_trace.stageCount < std::size(g_trace.stages))
        g_trace.stages[g_trace.stageCount++] = stage;
    EmitDatabaseTrace(stage, g_trace.logicalPath, g_trace.bytesRead,
        g_trace.fileSize, g_trace.readOffset, g_trace.requestedBytes,
        g_trace.initializedPoolCount,
        g_trace.freeAssetEntryCount, g_trace.threadInitialized,
        g_trace.headerValid, g_trace.openSucceeded, g_trace.stopStage);
}

void Stop(const char *stage)
{
    g_trace.stopStage = stage;
    EmitDatabaseTrace("DB stop", g_trace.logicalPath, g_trace.bytesRead,
        g_trace.fileSize, g_trace.readOffset, g_trace.requestedBytes,
        g_trace.initializedPoolCount,
        g_trace.freeAssetEntryCount, g_trace.threadInitialized,
        g_trace.headerValid, g_trace.openSucceeded, stage);
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
    for (std::uint32_t index = 1; index < g_zones.size(); ++index)
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

    std::array<std::uint8_t, 14> header{};
    g_trace.readOffset = 0;
    g_trace.requestedBytes = static_cast<std::uint32_t>(header.size());
    const std::int32_t bytesRead = WebDatabaseFS_Read(
        file, header.data(), static_cast<std::uint32_t>(header.size()));
    WebDatabaseFS_Close(file);
    g_trace.bytesRead = bytesRead > 0 ? static_cast<std::uint32_t>(bytesRead) : 0;
    Trace("zone header read");
    if (bytesRead != static_cast<std::int32_t>(header.size()))
    {
        Stop("zone header short read");
        return false;
    }

    std::uint32_t version = 0;
    std::memcpy(&version, header.data() + 8, sizeof(version));
    const bool magicValid = std::memcmp(header.data(), "IWff0100", 8) == 0 ||
        std::memcmp(header.data(), "IWffu100", 8) == 0;
    const bool zlibFraming = header[12] == 0x78;
    g_trace.headerValid = magicValid && version == 5 && zlibFraming;
    Trace("zone header/framing validation");
    if (!g_trace.headerValid)
    {
        Stop("zone header/framing rejected");
        return false;
    }

    Stop("DB_LoadXFile/streaming-inflate-closure");
    return true;
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

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_StartCanonicalDbHeaderProbe()
{
    XZoneInfo zoneInfo{"code_post_gfx", 4, 0};
    DB_LoadXAssets(&zoneInfo, 1, 1);
}
