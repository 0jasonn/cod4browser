#pragma once

#include <database/database.h>
#include <EffectsCore/fx_types.h>

using FxArchiveEffectDefEntryWriter =
    void(__cdecl *)(const FxEffectDef *, void *);

// DB_EnumXAssets passes XAssetHeader by value. Keep this adapter's ABI exact
// at the database seam, then dispatch the canonical effect-definition writer.
inline void __cdecl FX_ArchiveDispatchEffectDefAsset(
    XAssetHeader header, void *data, FxArchiveEffectDefEntryWriter writer)
{
    writer(header.fx, data);
}
