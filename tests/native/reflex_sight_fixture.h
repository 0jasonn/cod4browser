#pragma once

#include <gfx_d3d/gfx_image_types.h>
#include <web/web_renderer_material_lookup.h>

// Repository-authored metadata for the native reflex shader interface.
struct ReflexSightFixture
{
    GfxImage base{}, detail{};
    MaterialTextureDef textures[2]{};
    MaterialConstantDef scale{};
    MaterialVertexShader vertex{"reflexsight.hlsl", {}};
    MaterialPixelShader pixel{"reflexsight.hlsl", {}};
    MaterialShaderArgument args[6]{};
    MaterialTechnique technique{};
    MaterialTechniqueSet set{}, remapped{};
    GfxStateBits state{{0x19288962u, 0xcu}};
    Material material{};

    ReflexSightFixture()
    {
        // Both textures may use the color semantic; hashes select the binding.
        textures[0].nameHash = 0xeb529b4du; textures[0].semantic = 2;
        textures[0].samplerState = 0x11; textures[0].u.image = &detail;
        textures[1].nameHash = 0xa0ab1041u; textures[1].semantic = 2;
        textures[1].samplerState = 0x62; textures[1].u.image = &base;
        scale.nameHash = 0x08d36a09u;
        scale.literal[0] = 10; scale.literal[1] = 25;
        args[0] = {3, 0, {}}; args[0].u.codeConst = {80, 0, 4};
        args[1] = {3, 4, {}}; args[1].u.codeConst = {72, 0, 3};
        args[2] = {3, 8, {}}; args[2].u.codeConst = {71, 0, 3};
        args[3] = {0, 12, {}}; args[3].u.nameHash = scale.nameHash;
        args[4] = {2, 0, {}}; args[4].u.nameHash = textures[1].nameHash;
        args[5] = {2, 4, {}}; args[5].u.nameHash = textures[0].nameHash;
        technique.name = "reflexsight_dtex"; technique.passCount = 1;
        auto &pass = technique.passArray[0];
        pass.vertexShader = &vertex; pass.pixelShader = &pixel;
        pass.perPrimArgCount = 3; pass.stableArgCount = 3; pass.args = args;
        set.remappedTechniqueSet = &remapped;
        for (unsigned slot : {4u, 5u, 6u, 24u, 25u}) remapped.techniques[slot] = &technique;
        material.techniqueSet = &set; material.cameraRegion = 2;
        material.textureCount = 2; material.textureTable = textures;
        material.constantCount = 1; material.constantTable = &scale;
        material.stateBitsCount = 1; material.stateBitsTable = &state;
        std::fill_n(material.stateBitsEntry, 34, 255);
        for (unsigned slot : {4u, 5u, 6u, 24u, 25u}) material.stateBitsEntry[slot] = 0;
    }
};
