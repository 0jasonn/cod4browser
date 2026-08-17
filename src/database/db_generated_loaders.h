#pragma once

#include <database/db_asset_types.h>
#include <database/db_file_types.h>
#include <database/localize_types.h>
#include <gfx_d3d/material_types.h>
#include <gfx_d3d/r_font.h>
#include <physics/phys_preset.h>
#include <sound/snd_alias_types.h>

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
extern XAssetHeader *varXAssetHeader;

void __cdecl Load_XString(bool atStreamStart);
void __cdecl Load_MaterialTechniqueSetPtr(bool atStreamStart);
void __cdecl Load_MaterialHandle(bool atStreamStart);
void __cdecl Load_GfxImagePtr(bool atStreamStart);
void __cdecl Load_LocalizeEntryPtr(bool atStreamStart);
void __cdecl Load_SndCurvePtr(bool atStreamStart);
void __cdecl Load_LoadedSoundPtr(bool atStreamStart);
void __cdecl Load_snd_alias_list_ptr(bool atStreamStart);
void __cdecl Load_FontHandle(bool atStreamStart);
void DB_SetGeneratedAssetIndex(std::uint32_t index);
