#include <universal/q_shared.h>
#include <database/db_generated_material_platform.h>

#include <algorithm>

void __cdecl Load_BuildVertexDecl(MaterialVertexDeclaration **mtlVertDecl)
{
    iassert(mtlVertDecl && *mtlVertDecl);
    std::fill(std::begin((*mtlVertDecl)->routing.decl),
        std::end((*mtlVertDecl)->routing.decl), nullptr);
    (*mtlVertDecl)->isLoaded = true;
}

void __cdecl Load_CreateMaterialVertexShader(
    GfxVertexShaderLoadDef *loadDef, MaterialVertexShader *mtlShader)
{
    iassert(mtlShader && loadDef == &mtlShader->prog.loadDef);
    mtlShader->prog.vs = nullptr;
}

void __cdecl Load_CreateMaterialPixelShader(
    GfxPixelShaderLoadDef *loadDef, MaterialPixelShader *mtlShader)
{
    iassert(mtlShader && loadDef == &mtlShader->prog.loadDef);
    mtlShader->prog.ps = nullptr;
}

void __cdecl Material_OriginalRemapTechniqueSet(MaterialTechniqueSet *techSet)
{
    iassert(techSet);
    // The complete alias graph is not necessarily published when the
    // generated loader invokes this hook. Keep the asset usable immediately;
    // The web renderer frontend performs native's renderer remap once
    // DB_LoadXZone has atomically published the zone.
    techSet->remappedTechniqueSet = techSet;
}

void __cdecl Material_UploadShaders(MaterialTechniqueSet *techSet)
{
    iassert(techSet);
}

void __cdecl Load_PicmipWater(water_t **waterRef)
{
    iassert(waterRef && *waterRef);
}
