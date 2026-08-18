#include <EffectsCore/fx_system.h>
#include <database/database.h>

namespace
{
struct FxEnumeration
{
    void(__cdecl *callback)(const FxEffectDef *, void *);
    void *data;
};

void __cdecl EnumerateFx(XAssetHeader header, void *data)
{
    auto *enumeration = static_cast<FxEnumeration *>(data);
    enumeration->callback(header.fx, enumeration->data);
}
}

void __cdecl FX_ForEachEffectDef(
    void(__cdecl *callback)(const FxEffectDef *, void *), void *data)
{
    FxEnumeration enumeration{callback, data};
    DB_EnumXAssets(ASSET_TYPE_FX, EnumerateFx, &enumeration, false);
}

void FX_UnregisterAll()
{
    // Fastfile FX definitions are DB-owned and retire with their zone. The
    // native function only clears the separate loose-editor registry.
}
