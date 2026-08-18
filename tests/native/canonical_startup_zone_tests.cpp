#include <client/cl_fastfile_config.h>
#include <database/db_zone_loading.h>
#include <gfx_d3d/r_asset_load.h>
#include <gfx_d3d/r_configuration.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{
std::array<XZoneInfo, 6> g_request{};
std::uint32_t g_requestCount = 0;
std::int32_t g_sync = -1;

void RequireZone(std::uint32_t index, const char *name, int flags)
{
    assert(index < g_requestCount);
    assert(std::strcmp(g_request[index].name, name) == 0);
    assert(g_request[index].allocFlags == flags);
    assert(g_request[index].freeFlags == 0);
}
} // namespace

void DB_LoadXAssets(
    XZoneInfo *zoneInfo, std::uint32_t zoneCount, std::int32_t sync)
{
    assert(zoneCount <= g_request.size());
    g_requestCount = zoneCount;
    g_sync = sync;
    for (std::uint32_t index = 0; index < zoneCount; ++index)
        g_request[index] = zoneInfo[index];
}

bool DB_ModFileExists()
{
    return false;
}

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    assert(false && "unexpected canonical startup-zone assertion");
}

int main()
{
    GfxConfiguration config{};
    CL_SetFastFileNames(&config, false);
    R_LoadGraphicsAssetZones(config);

    assert(g_requestCount == 3);
    assert(g_sync == 0);
    RequireZone(0, "code_post_gfx", 2);
    RequireZone(1, "ui", 8);
    RequireZone(2, "common", 4);

    config.localizedCodeFastFileName = "localized_code";
    config.localizedCommonFastFileName = "localized_common";
    config.modFastFileName = "mod";
    R_LoadGraphicsAssetZones(config);
    assert(g_requestCount == 6);
    RequireZone(0, "code_post_gfx", 2);
    RequireZone(1, "localized_code", 0);
    RequireZone(2, "ui", 8);
    RequireZone(3, "common", 4);
    RequireZone(4, "localized_common", 1);
    RequireZone(5, "mod", 16);

    std::puts("canonical-startup-zones code_post_gfx:2 ui:8 common:4 sync:0");
    return 0;
}
