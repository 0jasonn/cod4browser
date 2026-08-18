#include <gfx_d3d/r_asset_load.h>

#include <database/db_zone_loading.h>
#include <gfx_d3d/r_configuration.h>

void R_LoadGraphicsAssetZones(const GfxConfiguration &config)
{
    XZoneInfo zoneInfo[6]{};
    std::uint32_t zoneCount = 0;

    zoneInfo[zoneCount++] = {config.codeFastFileName, 2, 0};
    if (config.localizedCodeFastFileName)
        zoneInfo[zoneCount++] = {config.localizedCodeFastFileName, 0, 0};
    if (config.uiFastFileName)
        zoneInfo[zoneCount++] = {config.uiFastFileName, 8, 0};
    zoneInfo[zoneCount++] = {config.commonFastFileName, 4, 0};
    if (config.localizedCommonFastFileName)
        zoneInfo[zoneCount++] = {config.localizedCommonFastFileName, 1, 0};
    if (config.modFastFileName)
        zoneInfo[zoneCount++] = {config.modFastFileName, 16, 0};

    DB_LoadXAssets(zoneInfo, zoneCount, 0);
}
