#pragma once

#include <database/db_registry_types.h>

#include <cstdint>

void DB_LoadXAssets(
    XZoneInfo *zoneInfo, std::uint32_t zoneCount, std::int32_t sync);
bool DB_ModFileExists();
