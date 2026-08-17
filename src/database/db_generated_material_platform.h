#pragma once

#include <gfx_d3d/material_types.h>

// Canonical generated loaders call these renderer post-load hooks. Native
// renderer builds own the D3D implementations in r_material.cpp. The web SP
// and standalone differential targets compile the renderer-disabled
// implementation in db_generated_material_platform.cpp.
void __cdecl Load_BuildVertexDecl(MaterialVertexDeclaration **mtlVertDecl);
void __cdecl Load_CreateMaterialVertexShader(
    GfxVertexShaderLoadDef *loadDef, MaterialVertexShader *mtlShader);
void __cdecl Load_CreateMaterialPixelShader(
    GfxPixelShaderLoadDef *loadDef, MaterialPixelShader *mtlShader);
void __cdecl Material_OriginalRemapTechniqueSet(MaterialTechniqueSet *techSet);
void __cdecl Material_UploadShaders(MaterialTechniqueSet *techSet);
