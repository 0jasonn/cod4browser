#pragma once

#include <database/db_asset_types.h>
#include <database/db_file_types.h>

#include <cstdint>

// Shared extraction of the first generated db_load.cpp closure. The full
// monolithic generated unit remains authoritative for native builds; this
// closure keeps its stream/global ordering while making the first family
// independently linkable by both 32-bit native tests and Wasm.
extern const char **varTempString;
extern const char *varConstChar;
extern const char **varXString;
extern RawFile *varRawFile;
extern RawFile **varRawFilePtr;
extern XAssetHeader *varXAssetHeader;

void DB_SetGeneratedAssetIndex(std::uint32_t index);
