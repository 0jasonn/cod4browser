#pragma once

#include <gfx_d3d/material_types.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <iterator>

// The encountered vertcol_mul_fog family has exactly two draws. Keep its
// canonical pass/state identity; this is a shader boundary, not a new asset.
inline const GfxStateBits *WebRenderer_GetMultiplyFogPass(
    const Material *material, unsigned type) noexcept
{
    const auto *set = material ? material->techniqueSet : nullptr;
    if (set && set->remappedTechniqueSet) set = set->remappedTechniqueSet;
    const auto *technique = set && type < 34 ? set->techniques[type] : nullptr;
    if (!technique || technique->passCount != 2 || !material->stateBitsTable)
        return nullptr;
    const unsigned entry = material->stateBitsEntry[type];
    if (entry == 255 || entry + 1 >= material->stateBitsCount) return nullptr;
    bool colorMap = false;
    for (unsigned i = 0; material->textureTable && i < material->textureCount; ++i)
        colorMap |= material->textureTable[i].nameHash == 0xa0ab1041u &&
            material->textureTable[i].semantic != 11 && material->textureTable[i].u.image;
    if (!colorMap) return nullptr;
    for (unsigned p = 0; p < 2; ++p)
    {
        const auto &pass = technique->passArray[p];
        const char *name = p ? "mul_fog.hlsl" : "mul.hlsl";
        if (!pass.vertexShader || !pass.vertexShader->name ||
            std::strcmp(pass.vertexShader->name, name) ||
            !pass.pixelShader || !pass.pixelShader->name ||
            std::strcmp(pass.pixelShader->name, name) || pass.customSamplerFlags)
            return nullptr;
        const unsigned count = pass.perPrimArgCount + pass.perObjArgCount + pass.stableArgCount;
        if (!pass.args || count != (p ? 5u : 3u)) return nullptr;
        unsigned bindings = 0;
        for (unsigned a = 0; a < count; ++a)
        {
            const auto &arg = pass.args[a];
            if (arg.type == 2 && arg.dest == 0 && arg.u.nameHash == 0xa0ab1041u)
                bindings |= 1;
            else if (arg.type == 3 && arg.u.codeConst.firstRow == 0)
            {
                const auto &c = arg.u.codeConst;
                if (arg.dest == 4 && c.index == 60 && c.rowCount == 4) bindings |= 2;
                if (arg.dest == 0 && c.index == 76 && c.rowCount == 4) bindings |= 4;
                if (p && arg.dest == 21 && c.index == 41 && c.rowCount == 1) bindings |= 8;
                if (p && arg.dest == 22 && c.index == 42 && c.rowCount == 1) bindings |= 16;
            }
        }
        if (bindings != (p ? 31u : 7u)) return nullptr;
    }
    return &material->stateBitsTable[entry + 1];
}

inline bool WebRenderer_IsCinematicMaterial(const Material *material, unsigned type) noexcept
{
    const auto *set = material ? material->techniqueSet : nullptr;
    if (set && set->remappedTechniqueSet) set = set->remappedTechniqueSet;
    const auto *technique = set && type < 34 ? set->techniques[type] : nullptr;
    if (!technique || technique->passCount != 1) return false;
    const auto &pass = technique->passArray[0];
    if (!pass.pixelShader || !pass.pixelShader->name ||
        std::strcmp(pass.pixelShader->name, "cinematic.hlsl")) return false;
    unsigned planes = 0;
    const unsigned count = pass.perPrimArgCount + pass.perObjArgCount + pass.stableArgCount;
    for (unsigned i = 0; pass.args && i < count; ++i)
    {
        const auto &arg = pass.args[i];
        // MTL_ARG_CODE_PIXEL_SAMPLER, TEXTURE_SRC_CODE_CINEMATIC_Y..A.
        // material_types.h exposes these serialized types without D3D headers.
        if (arg.type == 4 && arg.u.codeSampler >= 22 && arg.u.codeSampler <= 25 &&
            arg.dest == static_cast<unsigned>(arg.u.codeSampler) - 18)
            planes |= 1u << (arg.u.codeSampler - 22);
    }
    return planes == 15;
}

// Canonical Material table lookups only. Technique selection stays with each
// draw path; a missing entry leaves the caller's sampler/constant unchanged.
inline const GfxImage *WebRenderer_FindBaseImage(const Material *material, std::uint8_t &sampler) noexcept
{
    if (!material || !material->textureTable) return nullptr;
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (texture.nameHash == 0xa0ab1041u && texture.u.image)
        {
            sampler = texture.samplerState;
            return texture.u.image;
        }
    }
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (texture.semantic == 2u && texture.u.image)
        {
            sampler = texture.samplerState;
            return texture.u.image;
        }
    }
    return nullptr;
}

inline const GfxImage *WebRenderer_FindDetailImage(
    const Material *material, std::uint8_t &sampler) noexcept
{
    if (!material || !material->textureTable) return nullptr;
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (texture.nameHash == 0xeb529b4du && texture.u.image)
        {
            sampler = texture.samplerState;
            return texture.u.image;
        }
    }
    return nullptr;
}

inline const GfxImage *WebRenderer_FindNormalImage(
    const Material *material, std::uint8_t &sampler) noexcept
{
    if (!material || !material->textureTable) return nullptr;
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (texture.semantic == 5u && texture.u.image)
        {
            sampler = texture.samplerState;
            return texture.u.image;
        }
    }
    return nullptr;
}

inline const GfxImage *WebRenderer_FindSpecularImage(
    const Material *material, std::uint8_t &sampler) noexcept
{
    if (!material || !material->textureTable) return nullptr;
    for (std::uint32_t index = 0u; index < material->textureCount; ++index)
    {
        const MaterialTextureDef &texture = material->textureTable[index];
        if (texture.semantic == 8u && texture.u.image)
        {
            sampler = texture.samplerState;
            return texture.u.image;
        }
    }
    return nullptr;
}

inline bool WebRenderer_CopyMaterialConstant(
    const Material *material,
    std::uint32_t nameHash,
    float output[4]) noexcept
{
    if (!material || !material->constantTable) return false;
    for (std::uint32_t index = 0u; index < material->constantCount; ++index)
    {
        const MaterialConstantDef &constant = material->constantTable[index];
        if (constant.nameHash == nameHash)
        {
            std::copy_n(constant.literal, 4u, output);
            return true;
        }
    }
    return false;
}

inline const MaterialTechnique *WebRenderer_MaterialTechnique(
    const Material *material, unsigned type) noexcept
{
    const auto *set = material ? material->techniqueSet : nullptr;
    if (set && set->remappedTechniqueSet) set = set->remappedTechniqueSet;
    return set && type < 34 ? set->techniques[type] : nullptr;
}

// Transient renderer parameters read from canonical shader arguments. This
// does not retain another material or change the native technique set.
struct WebRendererSoftParticle
{
    unsigned flags = 0; // 1 fog, 2 premultiply, 4 angle falloff, 8 eye offset
    float feather[2]{}; // vertex and pixel feather scales
    float eyeOffset = 0;
    float falloff[4]{}, beginColor[4]{}, endColor[4]{}, fogColor[4]{};
    bool sceneFog = false;
};

inline bool WebRenderer_ShaderConstant(const Material *material,
    const MaterialPass &pass, unsigned type, unsigned dest, float out[4]) noexcept
{
    const unsigned count = pass.perPrimArgCount + pass.perObjArgCount + pass.stableArgCount;
    for (unsigned i = 0; pass.args && i < count; ++i)
    {
        const auto &arg = pass.args[i];
        if (arg.dest != dest) continue;
        if (arg.type == type)
        {
            if (!WebRenderer_CopyMaterialConstant(material, arg.u.nameHash, out)) return false;
        }
        else if (arg.type == type + 1 && arg.u.literalConst)
            std::copy_n(arg.u.literalConst, 4, out);
        else continue;
        return std::all_of(out, out + 4, [](float x) { return std::isfinite(x); });
    }
    return false;
}

inline bool WebRenderer_GetSoftParticle(const Material *material, unsigned type,
    WebRendererSoftParticle &out) noexcept
{
    const auto *tech = WebRenderer_MaterialTechnique(material, type);
    if (!tech || tech->passCount != 1) return false;
    const auto &pass = tech->passArray[0];
    if (!pass.pixelShader || !pass.vertexShader || !pass.pixelShader->name ||
        !pass.vertexShader->name) return false;
    const char *pixel = pass.pixelShader->name;
    WebRendererSoftParticle value{};
    if (!std::strcmp(pixel, "zfeather.hlsl")) value.flags = 1;
    else if (!std::strcmp(pixel, "zfeather_add.hlsl")) value.flags = 3;
    else if (!std::strcmp(pixel, "zfeather_nf.hlsl")) value.flags = 0;
    else if (!std::strcmp(pixel, "zfeather_add_nf.hlsl")) value.flags = 2;
    else return false;
    constexpr const char *vertices[] = {"zfeather_dtex.hlsl", "zfeather_nf_dtex.hlsl",
        "zfeather_foa_dtex.hlsl", "zfeather_foa_nf_dtex.hlsl",
        "zfeather_eo_dtex.hlsl", "zfeather_nf_eo_dtex.hlsl", "zfeather_foa_nf_eo_dtex.hlsl"};
    const char *vertex = pass.vertexShader->name;
    if (std::none_of(std::begin(vertices), std::end(vertices),
            [vertex](const char *name) { return !std::strcmp(vertex, name); })) return false;
    if (std::strstr(vertex, "_foa_")) value.flags |= 4;
    if (std::strstr(vertex, "_eo_")) value.flags |= 8;
    const unsigned count = pass.perPrimArgCount + pass.perObjArgCount + pass.stableArgCount;
    bool depthSampler = false, colorSampler = false, fog = !(value.flags & 1);
    for (unsigned i = 0; pass.args && i < count; ++i)
    {
        const auto &arg = pass.args[i];
        depthSampler |= arg.type == 4 && arg.dest == 4 && arg.u.codeSampler == 18;
        colorSampler |= arg.type == 2 && arg.dest == 0 && arg.u.nameHash == 0xa0ab1041u;
        if (arg.type == 5 && arg.dest == 0 && arg.u.codeConst.index == 42)
            value.sceneFog = fog = true;
    }
    float vertexFeather[4]{}, pixelFeather[4]{}, eye[4]{};
    if (!depthSampler || !colorSampler ||
        !WebRenderer_ShaderConstant(material, pass, 0, 12, vertexFeather) ||
        !WebRenderer_ShaderConstant(material, pass, 6, 5, pixelFeather) ||
        vertexFeather[0] < 0 || pixelFeather[0] < 0) return false;
    value.feather[0] = vertexFeather[0]; value.feather[1] = pixelFeather[0];
    if ((value.flags & 4) &&
        (!WebRenderer_ShaderConstant(material, pass, 0, 13, value.falloff) ||
         !WebRenderer_ShaderConstant(material, pass, 0, 14, value.beginColor) ||
         !WebRenderer_ShaderConstant(material, pass, 0, 15, value.endColor))) return false;
    if (value.flags & 8)
    {
        if (!WebRenderer_ShaderConstant(material, pass, 0, 16, eye)) return false;
        value.eyeOffset = eye[0];
    }
    if (!fog && !WebRenderer_ShaderConstant(material, pass, 6, 0, value.fogColor)) return false;
    out = value;
    return true;
}

inline bool WebRenderer_GetFloatZ(const Material *material,
    std::uint32_t state[2], bool &alphaTest) noexcept
{
    const auto *tech = WebRenderer_MaterialTechnique(material, 1);
    if (!tech || tech->passCount != 1 || !material->stateBitsTable ||
        material->stateBitsEntry[1] >= material->stateBitsCount) return false;
    const auto &pass = tech->passArray[0];
    if (!pass.pixelShader || !pass.pixelShader->name ||
        !pass.vertexShader || !pass.vertexShader->name) return false;
    alphaTest = !std::strcmp(pass.pixelShader->name, "floatz_build_atest.hlsl");
    if ((!alphaTest && std::strcmp(pass.pixelShader->name, "floatz_build.hlsl")) ||
        (alphaTest ? (std::strcmp(pass.vertexShader->name, "floatz_build_atest.hlsl") &&
            std::strcmp(pass.vertexShader->name, "floatz_build_atest_dtex.hlsl")) :
            std::strcmp(pass.vertexShader->name, "floatz_build.hlsl"))) return false;
    bool depth = false, color = !alphaTest;
    const unsigned count = pass.perPrimArgCount + pass.perObjArgCount + pass.stableArgCount;
    for (unsigned i = 0; pass.args && i < count; ++i)
    {
        const auto &arg = pass.args[i];
        depth |= arg.type == 3 && arg.dest == 20 && arg.u.codeConst.index == 54;
        color |= arg.type == 2 && arg.dest == 0 && arg.u.nameHash == 0xa0ab1041u;
    }
    if (!depth || !color) return false;
    std::copy_n(material->stateBitsTable[material->stateBitsEntry[1]].loadBits, 2, state);
    return true;
}

inline bool WebRenderer_GetDistortion(const Material *material, unsigned type,
    float scale[4]) noexcept
{
    const auto *tech = WebRenderer_MaterialTechnique(material, type);
    if (!tech || tech->passCount != 1 || (tech->flags & 33) != 33) return false;
    const auto &pass = tech->passArray[0];
    if (!pass.vertexShader || !pass.vertexShader->name ||
        !pass.pixelShader || !pass.pixelShader->name ||
        std::strcmp(pass.vertexShader->name, "distortion_scale_zfeather_dtex.hlsl") ||
        std::strcmp(pass.pixelShader->name, "distortion_zfeather.hlsl")) return false;
    unsigned bindings = 0;
    const unsigned count = pass.perPrimArgCount + pass.perObjArgCount + pass.stableArgCount;
    for (unsigned i = 0; pass.args && i < count; ++i)
    {
        const auto &arg = pass.args[i];
        if (arg.type == 2 && arg.dest == 4 && arg.u.nameHash == 0xa0ab1041u) bindings |= 1;
        if (arg.type == 4 && arg.dest == 0 && arg.u.codeSampler == 10) bindings |= 2;
        if (arg.type == 4 && arg.dest == 5 && arg.u.codeSampler == 18) bindings |= 4;
        if (arg.type == 3 && arg.dest == 0 && arg.u.codeConst.index == 80) bindings |= 8;
        if (arg.type == 3 && arg.dest == 17 && arg.u.codeConst.index == 51) bindings |= 16;
        if (arg.type == 3 && arg.dest == 18 && arg.u.codeConst.index == 52) bindings |= 32;
    }
    float value[4]{};
    if (bindings != 63 || !WebRenderer_ShaderConstant(material, pass, 0, 12, value))
        return false;
    std::copy_n(value, 4, scale);
    return true;
}

inline bool WebRenderer_SkipsDistortion(const Material *material, unsigned type,
    bool enabled) noexcept
{
    const auto *tech = WebRenderer_MaterialTechnique(material, type);
    // Native R_SetupMaterial skips every resolved-post-sun group when disabled.
    return !enabled && tech && (tech->flags & 1) != 0;
}

inline bool WebRenderer_IsOutdoorParticleCloud(
    const Material *material, unsigned type) noexcept
{
    const auto *tech = WebRenderer_MaterialTechnique(material, type);
    if (!tech || tech->passCount != 1) return false;
    const auto &pass = tech->passArray[0];
    if (!pass.vertexShader || !pass.vertexShader->name ||
        !pass.pixelShader || !pass.pixelShader->name ||
        std::strcmp(pass.vertexShader->name, "particle_cloud_outdoor.hlsl") ||
        std::strcmp(pass.pixelShader->name, "particle_cloud_outdoor.hlsl"))
        return false;
    unsigned bindings = 0u;
    const unsigned count = pass.perPrimArgCount + pass.perObjArgCount +
        pass.stableArgCount;
    for (unsigned index = 0u; pass.args && index < count; ++index)
    {
        const auto &arg = pass.args[index];
        if (arg.type == 2u && arg.dest == 0u &&
            arg.u.nameHash == 0xa0ab1041u) bindings |= 1u;
        if (arg.type == 4u && arg.dest == 4u &&
            arg.u.codeSampler == 17u) bindings |= 2u;
        if (arg.type == 3u && arg.dest == 4u &&
            arg.u.codeConst.index == 72u) bindings |= 4u;
        if (arg.type == 3u && arg.dest == 8u &&
            arg.u.codeConst.index == 68u) bindings |= 8u;
        if (arg.type == 3u && arg.dest == 16u &&
            arg.u.codeConst.index == 53u) bindings |= 16u;
        if (arg.type == 3u && arg.dest == 24u &&
            arg.u.codeConst.index == 88u) bindings |= 32u;
        if (arg.type == 5u && arg.dest == 3u &&
            arg.u.codeConst.index == 17u) bindings |= 64u;
    }
    return bindings == 127u;
}
