#include <database/database.h>
#include <universal/q_shared.h>
#include <xanim/xmodel.h>

// Portable renderer-frontend registrations used by the browser fastfile
// runtime. Native loose-source registration remains in the D3D/asset tools.
XModel *__cdecl R_RegisterModel(const char *name)
{
    iassert(name && name[0]);
    return XModelPrecache_FastFile(name);
}

Material *__cdecl Material_RegisterHandle(const char *name, int imageTrack)
{
    (void)imageTrack;
    iassert(name);
    return DB_FindXAssetHeader(ASSET_TYPE_MATERIAL, name).material;
}

const FxEffectDef *__cdecl FX_Register(const char *name)
{
    iassert(name && name[0]);
    iassert(I_strncmp(name, "fx/", 3));
    return DB_FindXAssetHeader(ASSET_TYPE_FX, name).fx;
}
