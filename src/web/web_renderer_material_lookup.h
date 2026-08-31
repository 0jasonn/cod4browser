#pragma once

#include <gfx_d3d/material_types.h>

#include <algorithm>
#include <cstdint>

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

