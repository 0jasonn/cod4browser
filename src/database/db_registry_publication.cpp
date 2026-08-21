#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_registry_pools.h>
#include <database/db_registry_publication.h>
#include <database/db_generated_gfxworld_platform.h>
#include <database/db_runtime_prefix.h>
#include <database/db_generated_material_platform.h>
#include <bgame/weapon_types.h>
#include <EffectsCore/fx_types.h>
#include <database/localize_types.h>
#include <gfx_d3d/gfx_image_types.h>
#include <gfx_d3d/gfx_light_types.h>
#include <gfx_d3d/material_types.h>
#include <gfx_d3d/r_font.h>
#include <physics/phys_preset.h>
#include <qcommon/com_world_types.h>
#include <qcommon/cm_types.h>
#include <gfx_d3d/gfx_world_types.h>
#include <game/g_bsp.h>
#include <sound/snd_alias_types.h>
#if defined(KISAK_DB_REGISTRY_LIFECYCLE_SLICE)
#include <universal/com_sndalias.h>
#endif
#include <ui/ui_asset_types.h>
#include <xanim/xmodel_types.h>
#include <xanim/xanim_types.h>

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
    case ASSET_TYPE_XMODEL:
        name = asset.header.model ? asset.header.model->name : nullptr;
        break;
    case ASSET_TYPE_XANIMPARTS:
        name = asset.header.parts ? asset.header.parts->name : nullptr;
        break;
    case ASSET_TYPE_WEAPON:
        name = asset.header.weapon ? asset.header.weapon->szInternalName : nullptr;
        break;
    case ASSET_TYPE_PHYSPRESET:
        name = asset.header.physPreset ? asset.header.physPreset->name : nullptr;
        break;
    case ASSET_TYPE_TECHNIQUE_SET:
        name = asset.header.techniqueSet ? asset.header.techniqueSet->name : nullptr;
        break;
    case ASSET_TYPE_MATERIAL:
        name = asset.header.material ? asset.header.material->info.name : nullptr;
        break;
    case ASSET_TYPE_IMAGE:
        name = asset.header.image ? asset.header.image->name : nullptr;
        break;
    case ASSET_TYPE_SOUND:
        name = asset.header.sound ? asset.header.sound->aliasName : nullptr;
        break;
    case ASSET_TYPE_SOUND_CURVE:
        name = asset.header.sndCurve ? asset.header.sndCurve->filename : nullptr;
        break;
    case ASSET_TYPE_LOADED_SOUND:
        name = asset.header.loadSnd ? asset.header.loadSnd->name : nullptr;
        break;
    case ASSET_TYPE_COMWORLD:
        name = asset.header.comWorld ? asset.header.comWorld->name : nullptr;
        break;
    case ASSET_TYPE_GFXWORLD:
        name = asset.header.gfxWorld ? asset.header.gfxWorld->name : nullptr;
        break;
    case ASSET_TYPE_GAMEWORLD_SP:
        name = asset.header.gameWorldSp ? asset.header.gameWorldSp->name : nullptr;
        break;
    case ASSET_TYPE_CLIPMAP:
    case ASSET_TYPE_CLIPMAP_PVS:
        name = asset.header.clipMap ? asset.header.clipMap->name : nullptr;
        break;
    case ASSET_TYPE_MAP_ENTS:
        name = asset.header.mapEnts ? asset.header.mapEnts->name : nullptr;
        break;
    case ASSET_TYPE_FONT:
        name = asset.header.font ? asset.header.font->fontName : nullptr;
        break;
    case ASSET_TYPE_FX:
        name = asset.header.fx ? asset.header.fx->name : nullptr;
        break;
    case ASSET_TYPE_IMPACT_FX:
        name = asset.header.impactFx ? asset.header.impactFx->name : nullptr;
        break;
    case ASSET_TYPE_LIGHT_DEF:
        name = asset.header.lightDef ? asset.header.lightDef->name : nullptr;
        break;
    case ASSET_TYPE_MENULIST:
        name = asset.header.menuList ? asset.header.menuList->name : nullptr;
        break;
    case ASSET_TYPE_MENU:
        name = asset.header.menu ? asset.header.menu->window.name : nullptr;
        break;
    case ASSET_TYPE_LOCALIZE_ENTRY:
        name = asset.header.localize ? asset.header.localize->name : nullptr;
        break;
    case ASSET_TYPE_RAWFILE:
        name = asset.header.rawfile ? asset.header.rawfile->name : nullptr;
        break;
    case ASSET_TYPE_STRINGTABLE:
        name = asset.header.stringTable ? asset.header.stringTable->name : nullptr;
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
    case ASSET_TYPE_XMODEL: return sizeof(XModel);
    case ASSET_TYPE_XANIMPARTS: return sizeof(XAnimParts);
    case ASSET_TYPE_WEAPON: return sizeof(WeaponDef);
    case ASSET_TYPE_PHYSPRESET: return sizeof(PhysPreset);
    case ASSET_TYPE_MATERIAL: return sizeof(Material);
    case ASSET_TYPE_TECHNIQUE_SET: return sizeof(MaterialTechniqueSet);
    case ASSET_TYPE_IMAGE: return sizeof(GfxImage);
    case ASSET_TYPE_SOUND: return sizeof(snd_alias_list_t);
    case ASSET_TYPE_SOUND_CURVE: return sizeof(SndCurve);
    case ASSET_TYPE_LOADED_SOUND: return sizeof(LoadedSound);
    case ASSET_TYPE_COMWORLD: return sizeof(ComWorld);
    case ASSET_TYPE_GFXWORLD: return sizeof(GfxWorld);
    case ASSET_TYPE_GAMEWORLD_SP: return sizeof(GameWorldSp);
    case ASSET_TYPE_CLIPMAP:
    case ASSET_TYPE_CLIPMAP_PVS: return sizeof(clipMap_t);
    case ASSET_TYPE_MAP_ENTS: return sizeof(MapEnts);
    case ASSET_TYPE_FONT: return sizeof(Font_s);
    case ASSET_TYPE_FX: return sizeof(FxEffectDef);
    case ASSET_TYPE_IMPACT_FX: return sizeof(FxImpactTable);
    case ASSET_TYPE_LIGHT_DEF: return sizeof(GfxLightDef);
    case ASSET_TYPE_MENULIST: return sizeof(MenuList);
    case ASSET_TYPE_MENU: return sizeof(menuDef_t);
    case ASSET_TYPE_LOCALIZE_ENTRY: return sizeof(LocalizeEntry);
    case ASSET_TYPE_RAWFILE: return sizeof(RawFile);
    case ASSET_TYPE_STRINGTABLE: return sizeof(StringTable);
    default: return 0;
    }
}

XAssetHeader AllocAssetHeader(XAssetType type)
{
    XAssetHeader header{};
    if (!AssetSize(type) || !DB_XAssetPool[type])
        return header;
    if (DB_IsSingletonAssetPool(type))
    {
        header.data = DB_XAssetPool[type];
        return header;
    }
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

    alignas(4) std::byte previous[sizeof(WeaponDef)]{};
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

int32_t __cdecl DB_GetAllXAssetOfType_FastFile(
    XAssetType type, XAssetHeader *assets, int32_t maxCount)
{
    if (type < 0 || type >= ASSET_TYPE_COUNT || maxCount < 0)
        return 0;
    int32_t count = 0;
    for (std::uint32_t hash = 0; hash < 0x8000u; ++hash)
    {
        for (std::uint32_t index = db_hashTable[hash]; index;
            index = g_assetEntryPool[index].entry.nextHash)
        {
            const XAssetEntry &entry = g_assetEntryPool[index].entry;
            if (entry.asset.type != type) continue;
            if (assets)
            {
                if (count >= maxCount)
                {
                    DB_RuntimeGeneratedFailure(
                        "registry/enumeration output capacity");
                    return count;
                }
                assets[count] = entry.asset.header;
            }
            ++count;
        }
    }
    return count;
}

int32_t __cdecl DB_GetAllXAssetOfType(
    XAssetType type, XAssetHeader *assets, int32_t maxCount)
{
    if (!IsFastFileLoad())
    {
        Com_Error(ERR_DROP,
            "Loose-object asset enumeration is unavailable in the browser runtime");
        return 0;
    }
    return DB_GetAllXAssetOfType_FastFile(type, assets, maxCount);
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

void __cdecl Load_StringTableAsset(XAssetHeader *stringTable)
{
    if (!stringTable || !stringTable->stringTable)
    {
        DB_RuntimeGeneratedFailure("publication/null StringTable");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_STRINGTABLE, *stringTable);
    if (!published.data) return;
    *stringTable = published;
}

void __cdecl Load_ComWorldAsset(XAssetHeader *comWorld)
{
    if (!comWorld || !comWorld->comWorld)
    {
        DB_RuntimeGeneratedFailure("publication/null ComWorld");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_COMWORLD, *comWorld);
    if (!published.data) return;
    *comWorld = published;
}

void __cdecl Load_GfxWorldAsset(XAssetHeader *gfxWorld)
{
    if (!gfxWorld || !gfxWorld->gfxWorld)
    {
        DB_RuntimeGeneratedFailure("publication/null GfxWorld");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_GFXWORLD, *gfxWorld);
    if (!published.data) return;
    *gfxWorld = published;
    DB_PlatformPublishGfxWorld(gfxWorld->gfxWorld);
}

void __cdecl Load_GameWorldSpAsset(XAssetHeader *gameWorldSp)
{
    if (!gameWorldSp || !gameWorldSp->gameWorldSp)
    {
        DB_RuntimeGeneratedFailure("publication/null GameWorldSp");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_GAMEWORLD_SP, *gameWorldSp);
    if (!published.data) return;
    *gameWorldSp = published;
}

void __cdecl Load_ClipMapAsset(XAssetHeader *clipMap)
{
    if (!clipMap || !clipMap->clipMap)
    {
        DB_RuntimeGeneratedFailure("publication/null ClipMap");
        return;
    }
#if defined(KISAK_MP)
    constexpr XAssetType type = ASSET_TYPE_CLIPMAP_PVS;
#else
    constexpr XAssetType type = ASSET_TYPE_CLIPMAP;
#endif
    XAssetHeader published = DB_AddXAsset(type, *clipMap);
    if (!published.data) return;
    *clipMap = published;
}

void __cdecl Load_MapEntsAsset(XAssetHeader *mapEnts)
{
    if (!mapEnts || !mapEnts->mapEnts)
    {
        DB_RuntimeGeneratedFailure("publication/null MapEnts");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_MAP_ENTS, *mapEnts);
    if (!published.data) return;
    *mapEnts = published;
}

void __cdecl Load_XModelAsset(XAssetHeader *model)
{
    if (!model || !model->model)
    {
        DB_RuntimeGeneratedFailure("publication/null XModel");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_XMODEL, *model);
    if (!published.data) return;
    *model = published;
}

void __cdecl Load_XAnimPartsAsset(XAssetHeader *parts)
{
    if (!parts || !parts->parts)
    {
        DB_RuntimeGeneratedFailure("publication/null XAnimParts");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_XANIMPARTS, *parts);
    if (!published.data) return;
    *parts = published;
}

void __cdecl Load_WeaponDefAsset(XAssetHeader *weapon)
{
    if (!weapon || !weapon->weapon)
    {
        DB_RuntimeGeneratedFailure("publication/null WeaponDef");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_WEAPON, *weapon);
    if (!published.data) return;
    *weapon = published;
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

void __cdecl Load_MaterialAsset(XAssetHeader *material)
{
    if (!material || !material->material)
    {
        DB_RuntimeGeneratedFailure("publication/null Material");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_MATERIAL, *material);
    if (!published.data) return;
    *material = published;
}

void __cdecl Load_GfxImageAsset(XAssetHeader *image)
{
    if (!image || !image->image)
    {
        DB_RuntimeGeneratedFailure("publication/null GfxImage");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_IMAGE, *image);
    if (!published.data) return;
    *image = published;
}

void __cdecl Load_SndCurveAsset(XAssetHeader *sndCurve)
{
    if (!sndCurve || !sndCurve->sndCurve)
    {
        DB_RuntimeGeneratedFailure("publication/null SndCurve");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_SOUND_CURVE, *sndCurve);
    if (!published.data) return;
    *sndCurve = published;
}

void __cdecl Load_snd_alias_list_Asset(XAssetHeader *sound)
{
    if (!sound || !sound->sound)
    {
        DB_RuntimeGeneratedFailure("publication/null sound alias list");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_SOUND, *sound);
    if (!published.data) return;
    *sound = published;

#if defined(KISAK_DB_REGISTRY_LIFECYCLE_SLICE)
    // Fastfile aliases carry their curve pointer in the canonical asset.  Do
    // this at publication, before any mixer frame can observe the alias; a
    // malformed/null pointer falls back to the same default storage used by
    // load-object aliases, while valid DB curve identity is preserved.
    if (sound->sound->head && sound->sound->count > 0)
    {
        for (int index = 0; index < sound->sound->count; ++index)
        {
            snd_alias_t &alias = sound->sound->head[index];
            SndCurve *defaultCurve = Com_GetDefaultSoundAliasVolumeFalloffCurve();
            if (!Com_IsValidSoundAliasVolumeFalloffCurve(defaultCurve))
            {
                Com_InitDefaultSoundAliasVolumeFalloffCurve(defaultCurve);
            }
            if (!Com_IsValidSoundAliasVolumeFalloffCurve(
                alias.volumeFalloffCurve))
            {
                Com_ReportInvalidSoundAliasVolumeFalloffCurve(&alias);
            }
            alias.volumeFalloffCurve =
                Com_ResolveSoundAliasVolumeFalloffCurve(
                    alias.volumeFalloffCurve, defaultCurve);
            if (!alias.volumeFalloffCurve)
            {
                DB_RuntimeGeneratedFailure(
                    "publication/no valid default SndCurve");
                return;
            }
        }
    }
#endif
}

void __cdecl Load_LoadedSoundAsset(XAssetHeader *loadedSound)
{
    if (!loadedSound || !loadedSound->loadSnd)
    {
        DB_RuntimeGeneratedFailure("publication/null LoadedSound");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_LOADED_SOUND,
        *loadedSound);
    if (!published.data) return;
    *loadedSound = published;
}

void __cdecl Load_FontAsset(XAssetHeader *font)
{
    if (!font || !font->font)
    {
        DB_RuntimeGeneratedFailure("publication/null Font");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_FONT, *font);
    if (!published.data) return;
    *font = published;
}

void __cdecl Load_FxEffectDefAsset(XAssetHeader *fx)
{
    if (!fx || !fx->fx)
    {
        DB_RuntimeGeneratedFailure("publication/null FxEffectDef");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_FX, *fx);
    if (!published.data) return;
    *fx = published;
}

void __cdecl Load_FxEffectDefFromName(const char **name)
{
    if (!name || !*name) return;
    *reinterpret_cast<XAssetHeader *>(name) = DB_FindXAssetHeader(
        ASSET_TYPE_FX, *name);
}

void __cdecl Load_FxImpactTableAsset(XAssetHeader *impactFx)
{
    if (!impactFx || !impactFx->impactFx)
    {
        DB_RuntimeGeneratedFailure("publication/null FxImpactTable");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_IMPACT_FX, *impactFx);
    if (!published.data) return;
    *impactFx = published;
}

void __cdecl Load_LightDefAsset(XAssetHeader *lightDef)
{
    if (!lightDef || !lightDef->lightDef)
    {
        DB_RuntimeGeneratedFailure("publication/null GfxLightDef");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_LIGHT_DEF, *lightDef);
    if (!published.data) return;
    *lightDef = published;
}

void __cdecl Load_MenuListAsset(XAssetHeader *menuList)
{
    if (!menuList || !menuList->menuList)
    {
        DB_RuntimeGeneratedFailure("publication/null MenuList");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_MENULIST, *menuList);
    if (!published.data) return;
    *menuList = published;
}

void __cdecl Load_MenuAsset(XAssetHeader *menu)
{
    if (!menu || !menu->menu)
    {
        DB_RuntimeGeneratedFailure("publication/null Menu");
        return;
    }
    menuDef_t *source = menu->menu;
    if (source->itemCount < 0 || (source->itemCount && !source->items))
    {
        DB_RuntimeGeneratedFailure("publication/invalid Menu items");
        return;
    }
    for (std::int32_t index = 0; index < source->itemCount; ++index)
    {
        if (!source->items[index])
        {
            DB_RuntimeGeneratedFailure("publication/null Menu item");
            return;
        }
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_MENU, *menu);
    if (!published.data) return;
    *menu = published;
    for (std::int32_t index = 0; index < source->itemCount; ++index)
        source->items[index]->parent = menu->menu;
}

void __cdecl Load_LocalizeEntryAsset(XAssetHeader *localize)
{
    if (!localize || !localize->localize)
    {
        DB_RuntimeGeneratedFailure("publication/null LocalizeEntry");
        return;
    }
    XAssetHeader published = DB_AddXAsset(ASSET_TYPE_LOCALIZE_ENTRY,
        *localize);
    if (!published.data) return;
    *localize = published;
}

XAssetHeader __cdecl DB_FindXAssetHeader(XAssetType type, const char *name)
{
    XAssetEntryPoolEntry *entry = DB_FindXAssetEntryCanonical(type, name);
    return entry ? entry->entry.asset.header : XAssetHeader{};
}
