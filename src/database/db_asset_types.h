#pragma once

#include <cstdint>

// Canonical KisakCOD/IW3 database-facing asset types. Keep this header free of
// renderer and platform dependencies so the native database, portable tests,
// and the Wasm target can share one asset ABI.

struct XModelPieces;
struct PhysPreset;
struct XAnimParts;
struct XModel;
struct Material;
struct MaterialPixelShader;
struct MaterialVertexShader;
struct MaterialTechniqueSet;
struct GfxImage;
struct snd_alias_list_t;
struct SndCurve;
struct LoadedSound;
struct clipMap_t;
struct ComWorld;
struct GameWorldSp;
struct GameWorldMp;
struct MapEnts;
struct GfxWorld;
struct GfxLightDef;
struct Font_s;
struct MenuList;
struct menuDef_t;
struct LocalizeEntry;
struct WeaponDef;
struct SndDriverGlobals;
struct FxEffectDef;
struct FxImpactTable;
struct StringTable;

struct RawFile // IW3 size: 0xC
{
    const char *name;
    std::int32_t len;
    const char *buffer;
};

union XAssetHeader // IW3 size: 0x4
{
    XAssetHeader() : data(nullptr) {}
    XAssetHeader(void *value) : data(value) {}

    XModelPieces *xmodelPieces;
    PhysPreset *physPreset;
    XAnimParts *parts;
    XModel *model;
    Material *material;
    MaterialPixelShader *pixelShader;
    MaterialVertexShader *vertexShader;
    MaterialTechniqueSet *techniqueSet;
    GfxImage *image;
    snd_alias_list_t *sound;
    SndCurve *sndCurve;
    LoadedSound *loadSnd;
    clipMap_t *clipMap;
    ComWorld *comWorld;
    GameWorldSp *gameWorldSp;
    GameWorldMp *gameWorldMp;
    MapEnts *mapEnts;
    GfxWorld *gfxWorld;
    GfxLightDef *lightDef;
    Font_s *font;
    MenuList *menuList;
    menuDef_t *menu;
    LocalizeEntry *localize;
    WeaponDef *weapon;
    SndDriverGlobals *sndDriverGlobals;
    const FxEffectDef *fx;
    FxImpactTable *impactFx;
    RawFile *rawfile;
    StringTable *stringTable;
    void *data;
};

enum XAssetType : std::int32_t // Canonical PC SP/MP ordering
{
    ASSET_TYPE_XMODELPIECES = 0x0,
    ASSET_TYPE_PHYSPRESET = 0x1,
    ASSET_TYPE_XANIMPARTS = 0x2,
    ASSET_TYPE_XMODEL = 0x3,
    ASSET_TYPE_MATERIAL = 0x4,
    ASSET_TYPE_TECHNIQUE_SET = 0x5,
    ASSET_TYPE_IMAGE = 0x6,
    ASSET_TYPE_SOUND = 0x7,
    ASSET_TYPE_SOUND_CURVE = 0x8,
    ASSET_TYPE_LOADED_SOUND = 0x9,
    ASSET_TYPE_CLIPMAP = 0xA,
    ASSET_TYPE_CLIPMAP_PVS = 0xB,
    ASSET_TYPE_COMWORLD = 0xC,
    ASSET_TYPE_GAMEWORLD_SP = 0xD,
    ASSET_TYPE_GAMEWORLD_MP = 0xE,
    ASSET_TYPE_MAP_ENTS = 0xF,
    ASSET_TYPE_GFXWORLD = 0x10,
    ASSET_TYPE_LIGHT_DEF = 0x11,
    ASSET_TYPE_UI_MAP = 0x12,
    ASSET_TYPE_FONT = 0x13,
    ASSET_TYPE_MENULIST = 0x14,
    ASSET_TYPE_MENU = 0x15,
    ASSET_TYPE_LOCALIZE_ENTRY = 0x16,
    ASSET_TYPE_WEAPON = 0x17,
    ASSET_TYPE_SNDDRIVER_GLOBALS = 0x18,
    ASSET_TYPE_FX = 0x19,
    ASSET_TYPE_IMPACT_FX = 0x1A,
    ASSET_TYPE_AITYPE = 0x1B,
    ASSET_TYPE_MPTYPE = 0x1C,
    ASSET_TYPE_CHARACTER = 0x1D,
    ASSET_TYPE_XMODELALIAS = 0x1E,
    ASSET_TYPE_RAWFILE = 0x1F,
    ASSET_TYPE_STRINGTABLE = 0x20,
    ASSET_TYPE_COUNT = 0x21,
    ASSET_TYPE_STRING = 0x21,
    ASSET_TYPE_ASSETLIST = 0x22,
};

inline XAssetType &operator++(XAssetType &type)
{
    type = static_cast<XAssetType>(static_cast<std::int32_t>(type) + 1);
    return type;
}

inline XAssetType &operator++(XAssetType &type, int)
{
    return ++type;
}

struct XAsset // IW3 size: 0x8
{
    XAssetType type;
    XAssetHeader header;
};

// Native KisakCOD and Wasm both target the original 32-bit IW3 ABI. Keep the
// assertions beside the canonical declarations while still allowing portable
// 64-bit parser tests to include the header as a semantic reference.
static_assert(sizeof(void *) != 4 || sizeof(RawFile) == 12);
static_assert(sizeof(void *) != 4 || sizeof(XAssetHeader) == 4);
static_assert(sizeof(void *) != 4 || sizeof(XAsset) == 8);
static_assert(static_cast<std::int32_t>(ASSET_TYPE_RAWFILE) == 31);
static_assert(static_cast<std::int32_t>(ASSET_TYPE_GFXWORLD) == 16);
