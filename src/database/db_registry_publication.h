#pragma once

#include <database/db_asset_types.h>
#include <database/db_registry_types.h>

#include <cstddef>
#include <cstdint>

struct Material;

extern XZone g_zones[ASSET_TYPE_COUNT];
extern std::uint32_t g_zoneIndex;

void DB_SetLoadingZoneIndex(std::uint32_t zoneIndex);
void DB_BeginXZonePublication();
void DB_CommitXZonePublication();
void DB_RollbackXZonePublication(
    const std::uint8_t *zoneIndices, std::size_t zoneCount);
void DB_UnloadXZonesForFreeFlags(int freeFlags);
void DB_UnloadFailedXZone(std::uint32_t zoneIndex);
void DB_MarkXAsset(XAssetType type, XAssetHeader header);
XAssetHeader DB_AddXAsset(XAssetType type, XAssetHeader header);
Material *DB_DuplicateMaterialAsset(Material *source, const char *name);
XAssetEntryPoolEntry *DB_FindXAssetEntryCanonical(XAssetType type, const char *name);
std::uint32_t DB_HashForNameCanonical(const char *name, XAssetType type);
