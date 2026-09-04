#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>

#include "material_types.h"

template <typename Feature>
bool Material_RemapTechniqueSetNameCore(
    const char *sourceName, char *remappedName, std::size_t remappedCapacity,
    std::uint32_t remapMask, std::uint32_t remapValue,
    const Feature *features, std::size_t featureCount,
    bool prependShaderModel2) noexcept
{
    if (!sourceName || !remappedName || !remappedCapacity ||
        (!features && featureCount))
        return false;

    std::size_t outputLength = 0;
    remappedName[0] = '\0';
    const auto append = [&](const char *text, std::size_t length,
                            bool prependUnderscore) {
        const std::size_t separator = prependUnderscore ? 1u : 0u;
        if (outputLength + separator + length >= remappedCapacity)
            return false;
        if (prependUnderscore) remappedName[outputLength++] = '_';
        std::memcpy(remappedName + outputLength, text, length);
        outputLength += length;
        remappedName[outputLength] = '\0';
        return true;
    };

    if (prependShaderModel2 && !append("sm2/", 4u, false)) return false;
    const char *parse = std::strncmp(sourceName, "sm2/", 4u) == 0
        ? sourceName + 4 : sourceName;
    while (*parse)
    {
        const bool prependUnderscore = outputLength && parse > sourceName &&
            parse[-1] == '_';
        const char *token = parse;
        std::size_t tokenLength = 0;
        while (*parse)
        {
            if (*parse == '_')
            {
                ++parse;
                break;
            }
            if (tokenLength && token[tokenLength - 1] >= '0' &&
                token[tokenLength - 1] <= '9' &&
                (*parse < '0' || *parse > '9'))
                break;
            ++tokenLength;
            ++parse;
        }
        if (!tokenLength) break;

        const Feature *feature = nullptr;
        for (std::size_t index = 0; index < featureCount; ++index)
            if (std::strlen(features[index].name) == tokenLength &&
                std::memcmp(features[index].name, token, tokenLength) == 0)
            {
                feature = &features[index];
                break;
            }

        if (!feature || !(remapMask & feature->mask))
        {
            if (!append(token, tokenLength, prependUnderscore)) return false;
            continue;
        }
        if (!feature->value)
        {
            if (remapValue & feature->mask) return false;
            continue;
        }

        const std::uint32_t selectedValue = remapValue & feature->mask;
        if (!selectedValue) continue;
        const Feature *replacement = nullptr;
        for (std::size_t index = 0; index < featureCount; ++index)
            if (features[index].mask == feature->mask &&
                features[index].value == selectedValue)
            {
                replacement = &features[index];
                break;
            }
        if (!replacement ||
            !append(replacement->name, std::strlen(replacement->name),
                prependUnderscore))
            return false;
    }
    return true;
}

struct MaterialTechniqueSetRemapStats
{
    std::uint32_t shaderModel3 = 0u;
    std::uint32_t references = 0u;
    std::uint32_t featureRemaps = 0u;
};

template <typename Feature, typename FindTechniqueSet>
MaterialTechniqueSetRemapStats Material_ResolveTechniqueSetRemapsCore(
    MaterialTechniqueSet *const *techniqueSets, std::size_t techniqueSetCount,
    std::uint32_t remapMask, std::uint32_t remapValue,
    const Feature *features, std::size_t featureCount,
    FindTechniqueSet findTechniqueSet) noexcept
{
    MaterialTechniqueSetRemapStats stats{};
    for (std::size_t index = 0; index < techniqueSetCount; ++index)
    {
        MaterialTechniqueSet *source = techniqueSets[index];
        if (!source) continue;
        if (source->name && source->name[0] == ',') continue;
        source->remappedTechniqueSet = source;
        if (!source->name) continue;
        if (std::strncmp(source->name, "sm2/", 4u) != 0)
            ++stats.shaderModel3;
        std::array<char, 64> remappedName{};
        if (!Material_RemapTechniqueSetNameCore(
                source->name, remappedName.data(), remappedName.size(),
                remapMask, remapValue, features, featureCount, false) ||
            std::strcmp(source->name, remappedName.data()) == 0)
            continue;
        MaterialTechniqueSet *remapped = findTechniqueSet(remappedName.data());
        if (remapped)
        {
            source->remappedTechniqueSet = remapped;
            ++stats.featureRemaps;
        }
    }
    for (std::size_t index = 0; index < techniqueSetCount; ++index)
    {
        MaterialTechniqueSet *source = techniqueSets[index];
        if (!source || !source->name || source->name[0] != ',' ||
            source->name[1] == '\0')
            continue;
        MaterialTechniqueSet *canonical = findTechniqueSet(source->name + 1);
        if (canonical)
        {
            source->remappedTechniqueSet = canonical->remappedTechniqueSet
                ? canonical->remappedTechniqueSet : canonical;
            ++stats.references;
        }
        else
            source->remappedTechniqueSet = source;
    }
    return stats;
}
