#pragma once

#include <database/db_asset_types.h>
#include <database/db_registry_types.h>

#include <cstddef>
#include <cstdint>

extern XZone g_zones[ASSET_TYPE_COUNT];
extern std::uint32_t g_zoneIndex;

void DB_SetLoadingZoneIndex(std::uint32_t zoneIndex);
void DB_UnloadXZonesForFreeFlags(int freeFlags);
void DB_MarkXAsset(XAssetType type, XAssetHeader header);
void DB_DiagnosePublishedSoundCurves(const char *phase);
XAssetHeader DB_AddXAsset(XAssetType type, XAssetHeader header);
XAssetEntryPoolEntry *DB_FindXAssetEntryCanonical(XAssetType type, const char *name);
std::uint32_t DB_HashForNameCanonical(const char *name, XAssetType type);
