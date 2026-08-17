#pragma once

#include <database/db_asset_types.h>
#include <database/db_file_types.h>
#include <gfx_d3d/material_types.h>
#include <physics/phys_preset.h>

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
extern XAssetHeader *varXAssetHeader;

void __cdecl Load_XString(bool atStreamStart);
void __cdecl Load_MaterialTechniqueSetPtr(bool atStreamStart);
void __cdecl Load_MaterialHandle(bool atStreamStart);
void __cdecl Load_GfxImagePtr(bool atStreamStart);
void DB_SetGeneratedAssetIndex(std::uint32_t index);
