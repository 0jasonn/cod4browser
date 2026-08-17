#pragma once

#include <database/db_registry_types.h>

#include <cstddef>
#include <cstdint>

// Canonical Kisak database pool state.  Keeping this in a small shared
// translation unit lets the browser initialize the real registry envelope
// without linking unrelated renderer, client, and game behavior from
// db_registry.cpp.
void DB_InitAssetPools();
void DB_TrackAssetPools();
bool DB_AreAssetPoolsInitialized();
std::size_t DB_GetInitializedAssetPoolCount();
std::size_t DB_GetFreeAssetEntryCount();

extern int32_t g_poolSize[ASSET_TYPE_COUNT];
extern void *DB_XAssetPool[ASSET_TYPE_COUNT];
extern XAssetEntryPoolEntry g_assetEntryPool[32768];
extern XAssetEntryPoolEntry *g_freeAssetEntryHead;
extern uint16_t db_hashTable[32768];
