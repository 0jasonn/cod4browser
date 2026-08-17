#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_registry_pools.h>
#include <database/db_registry_publication.h>
#include <database/db_runtime_prefix.h>
#include <database/db_generated_material_platform.h>
#include <gfx_d3d/material_types.h>
#include <physics/phys_preset.h>

#include <qcommon/threads.h>
#include <qcommon/system.h>

#include <cctype>
#include <cstddef>
#include <cstring>

XZone g_zones[ASSET_TYPE_COUNT]{};
std::uint32_t g_zoneIndex = 0;
FastCriticalSection db_hashCritSect{};

namespace
{
const char *AssetName(const XAsset &asset)
{
    const char *name = nullptr;
    switch (asset.type)
    {
    case ASSET_TYPE_PHYSPRESET:
        name = asset.header.physPreset ? asset.header.physPreset->name : nullptr;
        break;
    case ASSET_TYPE_TECHNIQUE_SET:
        name = asset.header.techniqueSet ? asset.header.techniqueSet->name : nullptr;
        break;
    case ASSET_TYPE_RAWFILE:
        name = asset.header.rawfile ? asset.header.rawfile->name : nullptr;
        break;
    default:
        break;
    }
    if (!name)
    {
        DB_RuntimeGeneratedFailure("publication/unsupported or unnamed asset");
        return nullptr;
    }
    return name;
}

std::size_t AssetSize(XAssetType type)
{
    switch (type)
    {
    case ASSET_TYPE_PHYSPRESET: return sizeof(PhysPreset);
    case ASSET_TYPE_TECHNIQUE_SET: return sizeof(MaterialTechniqueSet);
    case ASSET_TYPE_RAWFILE: return sizeof(RawFile);
    default: return 0;
    }
}

XAssetHeader AllocAssetHeader(XAssetType type)
{
    XAssetHeader header{};
    if (!AssetSize(type) || !DB_XAssetPool[type])
        return header;
    auto **freeHead = static_cast<void **>(DB_XAssetPool[type]);
    if (!*freeHead) return header;
    header.data = *freeHead;
    std::memcpy(freeHead, header.data, sizeof(*freeHead));
    return header;
}

XAssetEntryPoolEntry *AllocAssetEntry(XAssetType type, std::uint8_t zoneIndex)
{
    if (!g_freeAssetEntryHead) return nullptr;
    XAssetHeader header = AllocAssetHeader(type);
    if (!header.data) return nullptr;
    XAssetEntryPoolEntry *entry = g_freeAssetEntryHead;
    g_freeAssetEntryHead = entry->next;
    entry->entry.asset.type = type;
    entry->entry.asset.header = header;
    entry->entry.zoneIndex = zoneIndex;
    entry->entry.inuse = false;
    entry->entry.nextHash = 0;
    entry->entry.nextOverride = 0;
    entry->entry.usageFrame = 0;
    return entry;
}

void CloneAsset(const XAsset &from, XAsset &to)
{
    iassert(from.type == to.type);
    const std::size_t size = AssetSize(from.type);
    iassert(size && from.header.data && to.header.data);
    std::memcpy(to.header.data, from.header.data, size);
}

XAssetEntryPoolEntry *DB_LinkXAssetEntry(
    XAssetEntryPoolEntry *newEntry, std::int32_t allowOverride)
{
    iassert(newEntry && !allowOverride);
    const XAsset &asset = newEntry->entry.asset;
    const char *name = AssetName(asset);
    if (!name) return nullptr;
    const std::uint32_t hash = DB_HashForNameCanonical(name, asset.type);
    XAssetEntryPoolEntry *existing = DB_FindXAssetEntryCanonical(asset.type, name);
    if (existing && existing->entry.zoneIndex == g_zoneIndex)
    {
        DB_RuntimeGeneratedFailure("publication/duplicate asset in one zone");
        return nullptr;
    }
    XAssetEntryPoolEntry *entry = AllocAssetEntry(asset.type,
        static_cast<std::uint8_t>(g_zoneIndex));
    if (!entry)
    {
        DB_RuntimeGeneratedFailure(g_freeAssetEntryHead
            ? "publication/asset pool exhaustion"
            : "publication/asset entry exhaustion");
        return nullptr;
    }
    CloneAsset(asset, entry->entry.asset);

    if (!existing)
    {
        entry->entry.nextHash = db_hashTable[hash];
        db_hashTable[hash] = static_cast<std::uint16_t>(entry - g_assetEntryPool);
        return entry;
    }

    if (g_zones[entry->entry.zoneIndex].flags <
        g_zones[existing->entry.zoneIndex].flags)
    {
        entry->entry.nextOverride = existing->entry.nextOverride;
        existing->entry.nextOverride = static_cast<std::uint16_t>(
            entry - g_assetEntryPool);
        return existing;
    }

    alignas(4) std::byte previous[sizeof(MaterialTechniqueSet)]{};
    const std::size_t assetSize = AssetSize(asset.type);
    std::memcpy(previous, existing->entry.asset.header.data, assetSize);
    const std::uint8_t previousZone = existing->entry.zoneIndex;
    std::memcpy(existing->entry.asset.header.data,
        entry->entry.asset.header.data, assetSize);
    existing->entry.zoneIndex = entry->entry.zoneIndex;
    std::memcpy(entry->entry.asset.header.data, previous, assetSize);
    entry->entry.zoneIndex = previousZone;
    entry->entry.nextOverride = existing->entry.nextOverride;
    existing->entry.nextOverride = static_cast<std::uint16_t>(
        entry - g_assetEntryPool);
    return existing;
}
} // namespace

void DB_SetLoadingZoneIndex(std::uint32_t zoneIndex)
{
    iassert(zoneIndex > 0 && zoneIndex < ASSET_TYPE_COUNT);
    g_zoneIndex = zoneIndex;
}

std::uint32_t DB_HashForNameCanonical(const char *name, XAssetType type)
{
    std::int32_t value = static_cast<std::int32_t>(type);
    while (name && *name)
    {
        std::int32_t c = std::tolower(static_cast<unsigned char>(*name++));
        if (c == '\\') c = '/';
        value = c + 31 * value;
    }
    return static_cast<std::uint32_t>(value) % 0x8000u;
}

XAssetEntryPoolEntry *DB_FindXAssetEntryCanonical(XAssetType type, const char *name)
{
    if (!name || type < 0 || type >= ASSET_TYPE_COUNT) return nullptr;
    std::uint32_t index = db_hashTable[DB_HashForNameCanonical(name, type)];
    while (index)
    {
        XAssetEntryPoolEntry *entry = &g_assetEntryPool[index];
        if (entry->entry.asset.type == type)
        {
            const char *entryName = AssetName(entry->entry.asset);
            if (entryName && !I_stricmp(entryName, name)) return entry;
        }
        index = entry->entry.nextHash;
    }
    return nullptr;
}

XAssetHeader DB_AddXAsset(XAssetType type, XAssetHeader header)
{
    XAssetHeader result{};
    XAsset asset{type, header};
    const char *name = AssetName(asset);
    if (!name || DB_RuntimeGeneratedLoadFailed()) return result;
    const std::size_t freeBefore = DB_GetFreeAssetEntryCount();
    DB_RuntimeTracePublicationBegin(type, name, freeBefore);
    XAssetEntryPoolEntry newEntry{};
    newEntry.entry.asset = asset;
    Sys_LockWrite(&db_hashCritSect);
    XAssetEntryPoolEntry *entry = DB_LinkXAssetEntry(&newEntry, 0);
    Sys_UnlockWrite(&db_hashCritSect);
    if (!entry || DB_RuntimeGeneratedLoadFailed()) return result;
    result = entry->entry.asset.header;
    DB_RuntimeTracePublicationEnd(type, name,
        static_cast<std::uint32_t>(entry - g_assetEntryPool),
        DB_GetAssetPoolIndex(type, result),
        freeBefore, DB_GetFreeAssetEntryCount(),
        DB_HashForNameCanonical(name, type), entry->entry.zoneIndex);
    return result;
}

void __cdecl Load_RawFileAsset(XAssetHeader *rawfile)
{
    if (!rawfile || !rawfile->rawfile)
    {
        DB_RuntimeGeneratedFailure("publication/null RawFile");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_RAWFILE, *rawfile);
    if (!published.data) return;
    *rawfile = published;
}

void __cdecl Load_PhysPresetAsset(XAssetHeader *physPreset)
{
    if (!physPreset || !physPreset->physPreset)
    {
        DB_RuntimeGeneratedFailure("publication/null PhysPreset");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_PHYSPRESET, *physPreset);
    if (!published.data) return;
    *physPreset = published;
}

void __cdecl Load_MaterialTechniqueSetAsset(XAssetHeader *techniqueSet)
{
    if (!techniqueSet || !techniqueSet->techniqueSet)
    {
        DB_RuntimeGeneratedFailure("publication/null MaterialTechniqueSet");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_TECHNIQUE_SET,
        *techniqueSet);
    if (!published.data) return;
    *techniqueSet = published;
    Material_OriginalRemapTechniqueSet(techniqueSet->techniqueSet);
    Material_UploadShaders(techniqueSet->techniqueSet);
}

XAssetHeader __cdecl DB_FindXAssetHeader(XAssetType type, const char *name)
{
    XAssetEntryPoolEntry *entry = DB_FindXAssetEntryCanonical(type, name);
    return entry ? entry->entry.asset.header : XAssetHeader{};
}
