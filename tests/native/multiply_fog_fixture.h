#pragma once

#include <gfx_d3d/gfx_world_types.h>
#include <algorithm>
#include <cstddef>

// Synthetic material, authored for this repository under GPL-3.0. Contains
// no retail image or shader bytecode. Exercises the canonical binding ABI.
struct MultiplyFogFixture
{
    Material material{};
    MaterialTechniqueSet set{}, remapped{};
    alignas(MaterialTechnique) std::byte storage[sizeof(MaterialTechnique) + sizeof(MaterialPass)]{};
    MaterialTechnique &technique = *reinterpret_cast<MaterialTechnique *>(storage);
    MaterialVertexShader vs[2]{};
    MaterialPixelShader ps[2]{};
    MaterialShaderArgument args[2][5]{};
    GfxStateBits states[3]{{}, {{0x19288931u, 12u}}, {{0x18128922u, 12u}}};
    MaterialTextureDef texture{};
    GfxImage image{};

    MultiplyFogFixture()
    {
        material.info.name = "synthetic/multiply-fog";
        material.techniqueSet = &set;
        set.remappedTechniqueSet = &remapped;
        remapped.techniques[7] = &technique;
        material.stateBitsTable = states;
        material.stateBitsCount = 3;
        std::fill_n(material.stateBitsEntry, 34, 255);
        material.stateBitsEntry[7] = 1;
        material.textureTable = &texture;
        material.textureCount = 1;
        texture.nameHash = 0xa0ab1041u;
        texture.semantic = 2;
        texture.samplerState = 0x62;
        texture.u.image = &image;
        technique.name = "vertcol_mul_fog";
        technique.passCount = 2;
        for (unsigned p = 0; p < 2; ++p)
        {
            vs[p].name = ps[p].name = p ? "mul_fog.hlsl" : "mul.hlsl";
            auto &pass = technique.passArray[p];
            pass.vertexShader = &vs[p];
            pass.pixelShader = &ps[p];
            pass.args = args[p];
            pass.stableArgCount = p ? 5 : 3;
            args[p][0].type = 3; args[p][0].dest = 4;
            args[p][0].u.codeConst = {60, 0, 4};
            args[p][1].type = 3; args[p][1].dest = 0;
            args[p][1].u.codeConst = {76, 0, 4};
            args[p][2].type = 2; args[p][2].dest = 0;
            args[p][2].u.nameHash = 0xa0ab1041u;
            args[p][3].type = 3; args[p][3].dest = 21;
            args[p][3].u.codeConst = {41, 0, 1};
            args[p][4].type = 3; args[p][4].dest = 22;
            args[p][4].u.codeConst = {42, 0, 1};
        }
    }
};
