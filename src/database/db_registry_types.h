#pragma once

#include <database/db_asset_types.h>

#include <cstdint>

struct XAssetEntry
{
    XAsset asset;
    std::uint8_t zoneIndex;
    bool inuse;
    std::uint16_t nextHash;
    std::uint16_t nextOverride;
    std::uint16_t usageFrame;
};

union XAssetEntryPoolEntry
{
    XAssetEntryPoolEntry() {}
    XAssetEntry entry;
    XAssetEntryPoolEntry *next;
};

struct XZoneInfo
{
    const char *name;
    int allocFlags;
    int freeFlags;
};

struct XBlock
{
    std::uint8_t *data;
    std::uint32_t size;
};

struct XZoneMemory
{
    XBlock blocks[9];
    std::uint8_t *lockedVertexData;
    std::uint8_t *lockedIndexData;
    void *vertexBuffer;
    void *indexBuffer;
};

struct XZone
{
    char name[64];
    int flags;
    int allocType;
    XZoneMemory mem;
    int fileSize;
    bool modZone;
};

static_assert(sizeof(void *) != 4 || sizeof(XAssetEntry) == 16);
static_assert(sizeof(void *) != 4 || sizeof(XAssetEntryPoolEntry) == 16);
static_assert(sizeof(void *) != 4 || sizeof(XZoneInfo) == 12);
static_assert(sizeof(void *) != 4 || sizeof(XZoneMemory) == 88);
static_assert(sizeof(void *) != 4 || sizeof(XZone) == 168);

