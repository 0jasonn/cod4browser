#pragma once

#include <database/db_asset_types.h>

#include <cstdint>

// Canonical 32-bit fastfile envelope types. They are kept renderer-free so
// the shared stream/PMem path does not inherit the D3D-heavy xanim aggregate.
struct ScriptStringList
{
    std::int32_t count;
    const char **strings;
};

struct XAssetList
{
    ScriptStringList stringList;
    std::int32_t assetCount;
    XAsset *assets;
};

struct XFile
{
    std::uint32_t size;
    std::uint32_t externalSize;
    std::uint32_t blockSize[9];
};

static_assert(sizeof(void *) != 4 || sizeof(ScriptStringList) == 8);
static_assert(sizeof(void *) != 4 || sizeof(XAssetList) == 16);
static_assert(sizeof(XFile) == 44);
