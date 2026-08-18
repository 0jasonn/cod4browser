#pragma once

#include <database/db_asset_types.h>
#include <database/db_file_types.h>
#include <database/localize_types.h>
#include <EffectsCore/fx_types.h>
#include <gfx_d3d/material_types.h>
#include <gfx_d3d/gfx_light_types.h>
#include <gfx_d3d/r_font.h>
#include <physics/phys_preset.h>
#include <qcommon/com_world_types.h>
#include <sound/snd_alias_types.h>
#include <xanim/xmodel_types.h>
#include <xanim/xanim_types.h>
#include <bgame/weapon_types.h>

#include <cstdint>

// Shared extractions of generated db_load.cpp closures. The full monolithic
// generated unit remains authoritative for native builds; these units preserve
// its stream/global ordering while making reached families independently
// linkable by both 32-bit native tests and Wasm.
extern const char **varTempString;
extern const char *varConstChar;
extern const char **varXString;
extern RawFile *varRawFile;
extern RawFile **varRawFilePtr;
extern PhysPreset *varPhysPreset;
extern PhysPreset **varPhysPresetPtr;
extern MaterialTechniqueSet *varMaterialTechniqueSet;
extern MaterialTechniqueSet **varMaterialTechniqueSetPtr;
extern Material *varMaterial;
extern Material **varMaterialHandle;
extern GfxImage *varGfxImage;
extern GfxImage **varGfxImagePtr;
extern LocalizeEntry *varLocalizeEntry;
extern LocalizeEntry **varLocalizeEntryPtr;
extern SndCurve *varSndCurve;
extern SndCurve **varSndCurvePtr;
extern LoadedSound *varLoadedSound;
extern LoadedSound **varLoadedSoundPtr;
extern snd_alias_list_t *varsnd_alias_list_t;
extern snd_alias_list_t **varsnd_alias_list_ptr;
extern Font_s *varFont;
extern Font_s **varFontHandle;
extern FxEffectDef *varFxEffectDef;
extern const FxEffectDef **varFxEffectDefHandle;
extern FxImpactEntry *varFxImpactEntry;
extern FxImpactTable *varFxImpactTable;
extern FxImpactTable **varFxImpactTablePtr;
extern GfxLightDef *varGfxLightDef;
extern GfxLightDef **varGfxLightDefPtr;
extern GfxLightImage *varGfxLightImage;
extern MenuList *varMenuList;
extern MenuList **varMenuListPtr;
extern menuDef_t *varmenuDef_t;
extern menuDef_t **varmenuDefPtr;
extern XAssetHeader *varXAssetHeader;
extern XModel *varXModel;
extern XModel **varXModelPtr;
extern std::uint16_t *varScriptString;
extern WeaponDef *varWeaponDef;
extern WeaponDef **varWeaponDefPtr;
extern XAnimParts *varXAnimParts;
extern XAnimParts **varXAnimPartsPtr;
extern StringTable *varStringTable;
extern StringTable **varStringTablePtr;
extern ComWorld *varComWorld;
extern ComWorld **varComWorldPtr;
extern ComPrimaryLight *varComPrimaryLight;
extern GfxWorld *varGfxWorld;
extern GfxWorld **varGfxWorldPtr;

void __cdecl Load_XString(bool atStreamStart);
void __cdecl Load_ScriptStringArray(bool atStreamStart, std::int32_t count);
void __cdecl Load_PhysPresetPtrGenerated(bool atStreamStart);
void __cdecl Load_MaterialHandleArrayGenerated(bool atStreamStart,
    std::int32_t count);
void __cdecl Load_XModelPtr(bool atStreamStart);
void __cdecl Load_XModelPtrArray(bool atStreamStart, std::int32_t count);
void __cdecl Load_WeaponDefPtr(bool atStreamStart);
void __cdecl Load_XAnimPartsPtr(bool atStreamStart);
void __cdecl Load_StringTablePtr(bool atStreamStart);
void __cdecl Load_ComWorldPtr(bool atStreamStart);
void __cdecl Load_GfxWorldPtr(bool atStreamStart);
void __cdecl Load_MaterialTechniqueSetPtr(bool atStreamStart);
void __cdecl Load_MaterialHandle(bool atStreamStart);
void __cdecl Load_GfxImagePtr(bool atStreamStart);
void __cdecl Load_LocalizeEntryPtr(bool atStreamStart);
void __cdecl Load_SndCurvePtr(bool atStreamStart);
void __cdecl Load_LoadedSoundPtr(bool atStreamStart);
void __cdecl Load_snd_alias_list_ptr(bool atStreamStart);
void __cdecl Load_FontHandle(bool atStreamStart);
void __cdecl Load_FxEffectDefHandle(bool atStreamStart);
void __cdecl Load_FxEffectDefHandleArray(bool atStreamStart,
    std::int32_t count);
void __cdecl Load_FxImpactTablePtr(bool atStreamStart);
void __cdecl Load_GfxLightDefPtr(bool atStreamStart);
void __cdecl Load_MenuListPtr(bool atStreamStart);
void __cdecl Load_menuDef_ptr(bool atStreamStart);
void DB_SetGeneratedAssetIndex(std::uint32_t index);
