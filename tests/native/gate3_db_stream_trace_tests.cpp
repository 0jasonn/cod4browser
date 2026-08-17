#include <database/database.h>
#include <database/db_registry_pools.h>
#include <database/db_registry_publication.h>
#include <database/db_runtime_prefix.h>
#include <database/localize_types.h>
#include <EffectsCore/fx_types.h>
#include <gfx_d3d/gfx_image_types.h>
#include <gfx_d3d/material_types.h>
#include <gfx_d3d/r_font.h>
#include <physics/phys_preset.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <script/scr_stringlist.h>
#include <sound/snd_alias_types.h>
#include <universal/physicalmemory.h>
#include <web/web_database_filesystem.h>

#include <zlib/zlib.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
std::vector<std::uint8_t> g_file;
std::size_t g_filePosition = 0;
DBRuntimeTraceSnapshot g_trace{};
alignas(4096) std::array<std::uint8_t, 4 * 1024 * 1024> g_arena{};
std::uint32_t g_lowPosition = 0;
std::uint32_t g_highPosition = static_cast<std::uint32_t>(g_arena.size());
std::string g_scriptString;

void AppendU32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
}

void AppendU16(std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void AppendZeros(std::vector<std::uint8_t> &bytes, std::size_t count)
{
    bytes.insert(bytes.end(), count, 0);
}

void WriteU32(std::vector<std::uint8_t> &bytes, std::size_t offset,
    std::uint32_t value)
{
    assert(offset + sizeof(value) <= bytes.size());
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::uint32_t Align4(std::uint32_t value)
{
    return (value + 3u) & ~3u;
}

std::uint32_t Align16(std::uint32_t value)
{
    return (value + 15u) & ~15u;
}

void AppendF32(std::vector<std::uint8_t> &bytes, float value)
{
    std::uint32_t encoded = 0;
    static_assert(sizeof(encoded) == sizeof(value));
    std::memcpy(&encoded, &value, sizeof(encoded));
    AppendU32(bytes, encoded);
}

void AppendCString(std::vector<std::uint8_t> &bytes, const char *value)
{
    bytes.insert(bytes.end(), value, value + std::strlen(value) + 1);
}

std::vector<std::uint8_t> CompressXFile(const std::vector<std::uint8_t> &inflated)
{
    uLongf compressedSize = static_cast<uLongf>(inflated.size() * 2 + 64);
    std::vector<std::uint8_t> compressed(compressedSize);
    assert(compress2(compressed.data(), &compressedSize, inflated.data(),
        static_cast<uLong>(inflated.size()), Z_BEST_COMPRESSION) == Z_OK);
    compressed.resize(compressedSize);
    std::vector<std::uint8_t> result{'I', 'W', 'f', 'f', 'u', '1', '0', '0', 5, 0, 0, 0};
    result.insert(result.end(), compressed.begin(), compressed.end());
    return result;
}

std::vector<std::uint8_t> MakeGeneratedPrefixXFile()
{
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 8192);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);

    AppendU32(inflated, 1);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 1);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, UINT32_MAX);
    AppendCString(inflated, "gate3_script_identity");
    AppendU32(inflated, ASSET_TYPE_RAWFILE);
    AppendU32(inflated, UINT32_MAX - 1u);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 5);
    AppendU32(inflated, 1);
    AppendCString(inflated, "tests/gate3_first.txt");
    AppendCString(inflated, "first");
    assert(inflated.size() == 134);
    return CompressXFile(inflated);
}

std::vector<std::uint8_t> MakePhysPresetXFile(
    std::uint32_t assetPointer = UINT32_MAX - 1u,
    std::uint32_t namePointer = UINT32_MAX,
    std::uint32_t sndAliasPointer = 0x40000015u,
    bool priorAlias = true,
    bool includeName = true,
    bool terminateName = true,
    std::size_t nameLength = 0)
{
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 8192);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);

    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, priorAlias ? 2u : 1u);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_PHYSPRESET);
    AppendU32(inflated, assetPointer);
    if (priorAlias)
    {
        AppendU32(inflated, ASSET_TYPE_PHYSPRESET);
        AppendU32(inflated, 0x40000011u);
    }
    if (assetPointer == UINT32_MAX || assetPointer == UINT32_MAX - 1u)
    {
        AppendU32(inflated, namePointer);
        AppendU32(inflated, 7);
        AppendF32(inflated, 12.5f);
        AppendF32(inflated, 0.25f);
        AppendF32(inflated, 0.75f);
        AppendF32(inflated, 2.0f);
        AppendF32(inflated, 3.0f);
        AppendU32(inflated, sndAliasPointer);
        AppendF32(inflated, 0.5f);
        AppendF32(inflated, 4.0f);
        AppendU32(inflated, 1);
        if (includeName)
        {
            const char *name = "physics/gate3";
            if (nameLength)
                inflated.insert(inflated.end(), nameLength, 'x');
            else
                inflated.insert(inflated.end(), name, name + std::strlen(name));
            if (terminateName) inflated.push_back(0);
        }
        if (sndAliasPointer == UINT32_MAX)
            AppendCString(inflated, "metal");
    }
    return CompressXFile(inflated);
}

struct TechniqueSetFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    std::uint32_t techniquePointer = UINT32_MAX;
    bool includeAliasAsset = true;
    bool includeTechniqueAlias = true;
    bool terminateTechniqueName = true;
    std::uint16_t passCount = 1;
    std::uint16_t vertexProgramSize = 1;
};

std::vector<std::uint8_t> MakeTechniqueSetXFile(
    const TechniqueSetFixtureOptions &options = {})
{
    constexpr const char *setName = "techsets/gate3";
    constexpr const char *pixelShaderName = "ps_gate3";
    constexpr const char *techniqueName = "tech_gate3";
    const bool inlineAsset = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const std::uint32_t assetCount = options.includeAliasAsset ? 2u : 1u;

    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 8192);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);

    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, assetCount);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_TECHNIQUE_SET);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_TECHNIQUE_SET);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset) return CompressXFile(inflated);

    std::uint32_t block4Offset = assetCount * sizeof(XAsset);
    if (options.assetPointer == UINT32_MAX - 1u)
        block4Offset = Align4(block4Offset) + 4u;
    const std::uint32_t setNameOffset = block4Offset;
    block4Offset += static_cast<std::uint32_t>(std::strlen(setName) + 1u);
    const std::uint32_t techniqueOffset = Align4(block4Offset);
    const std::uint32_t techniqueAlias = 0x40000001u + techniqueOffset;
    const std::uint32_t setNameAlias = 0x40000001u + setNameOffset;

    AppendU32(inflated, UINT32_MAX);
    inflated.push_back(2);
    inflated.push_back(0);
    inflated.push_back(0);
    inflated.push_back(0);
    AppendU32(inflated, 0);
    AppendU32(inflated, options.techniquePointer);
    AppendU32(inflated, options.includeTechniqueAlias &&
        options.techniquePointer == UINT32_MAX ? techniqueAlias : 0u);
    for (std::uint32_t index = 2; index < 34; ++index) AppendU32(inflated, 0);
    AppendCString(inflated, setName);

    if (options.techniquePointer != UINT32_MAX)
        return CompressXFile(inflated);

    AppendU32(inflated, UINT32_MAX);
    AppendU16(inflated, 0x12u);
    AppendU16(inflated, options.passCount);
    if (options.passCount != 1)
        return CompressXFile(inflated);

    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, UINT32_MAX);
    inflated.push_back(1);
    inflated.push_back(0);
    inflated.push_back(0);
    inflated.push_back(0);
    AppendU32(inflated, 1);

    inflated.push_back(1);
    inflated.push_back(0);
    inflated.push_back(0);
    inflated.push_back(0);
    AppendZeros(inflated, 96);

    AppendU32(inflated, setNameAlias);
    AppendU32(inflated, 0);
    AppendU32(inflated, 1);
    AppendU16(inflated, options.vertexProgramSize);
    AppendU16(inflated, 0);
    if (options.vertexProgramSize != 1)
        return CompressXFile(inflated);
    AppendU32(inflated, 0x56530001u);

    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 0);
    AppendU32(inflated, 1);
    AppendU16(inflated, 1);
    AppendU16(inflated, 0);
    AppendCString(inflated, pixelShaderName);
    AppendU32(inflated, 0x50530001u);

    AppendU16(inflated, 1);
    AppendU16(inflated, 3);
    AppendU32(inflated, UINT32_MAX);
    AppendF32(inflated, 1.0f);
    AppendF32(inflated, 2.0f);
    AppendF32(inflated, 3.0f);
    AppendF32(inflated, 4.0f);
    inflated.insert(inflated.end(), techniqueName,
        techniqueName + std::strlen(techniqueName));
    if (options.terminateTechniqueName) inflated.push_back(0);
    return CompressXFile(inflated);
}

struct MaterialFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    std::uint32_t materialNamePointer = UINT32_MAX;
    std::uint32_t techniqueSetPointer = 0;
    std::uint32_t textureTablePointer = UINT32_MAX;
    std::uint32_t constantTablePointer = UINT32_MAX;
    std::uint32_t stateBitsTablePointer = UINT32_MAX;
    std::uint32_t imagePointer = UINT32_MAX - 1u;
    std::uint32_t imageTexturePointer = UINT32_MAX;
    std::uint32_t waterPointer = UINT32_MAX;
    bool directImageName = false;
    bool terminateMaterialName = true;
    bool terminateImageName = true;
    std::int32_t waterM = 2;
    std::int32_t waterN = 2;
    std::int32_t imageResourceSize = 4;
};

std::vector<std::uint8_t> MakeMaterialXFile(
    const MaterialFixtureOptions &options = {})
{
    constexpr const char *materialName = "materials/gate3";
    constexpr const char *imageName = "images/gate3";
    const bool inlineAsset = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const std::uint32_t assetCount = options.includeAliasAsset ? 2u : 1u;

    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 16384);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        8192, 0, 0, 0, 8192, 0, 0, 0, 0}) AppendU32(inflated, size);

    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, assetCount);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_MATERIAL);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_MATERIAL);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset) return CompressXFile(inflated);

    std::uint32_t block0Offset = 0;
    std::uint32_t block4Offset = assetCount * sizeof(XAsset);
    if (options.assetPointer == UINT32_MAX - 1u)
        block4Offset = Align4(block4Offset) + 4u;
    const std::uint32_t materialNameOffset = block4Offset;
    const std::uint32_t materialNameAlias =
        0x40000001u + materialNameOffset;

    const std::size_t materialBodyStart = inflated.size();
    AppendU32(inflated, options.materialNamePointer);
    inflated.push_back(1);
    inflated.push_back(2);
    inflated.push_back(1);
    inflated.push_back(1);
    AppendU32(inflated, 0x11223344u);
    AppendU32(inflated, 0x55667788u);
    AppendU32(inflated, 0xA5A5A5A5u);
    AppendU16(inflated, 0x1234u);
    AppendU16(inflated, 0);
    AppendZeros(inflated, 34);
    inflated.push_back(2);
    inflated.push_back(1);
    inflated.push_back(1);
    inflated.push_back(3);
    inflated.push_back(4);
    inflated.push_back(0);
    AppendU32(inflated, options.techniqueSetPointer);
    AppendU32(inflated, options.textureTablePointer);
    AppendU32(inflated, options.constantTablePointer);
    AppendU32(inflated, options.stateBitsTablePointer);
    assert(inflated.size() - materialBodyStart == sizeof(Material));
    block0Offset += sizeof(Material);

    if (options.materialNamePointer == UINT32_MAX)
    {
        inflated.insert(inflated.end(), materialName,
            materialName + std::strlen(materialName));
        if (options.terminateMaterialName) inflated.push_back(0);
        block4Offset += static_cast<std::uint32_t>(std::strlen(materialName) +
            (options.terminateMaterialName ? 1u : 0u));
    }
    if (options.textureTablePointer != UINT32_MAX)
        return CompressXFile(inflated);

    block4Offset = Align4(block4Offset);
    const std::uint32_t imageInsertionOffset = block4Offset +
        2u * sizeof(MaterialTextureDef);
    const std::uint32_t imageInsertionAlias =
        0x40000001u + imageInsertionOffset;

    AppendU32(inflated, 0x11111111u);
    inflated.push_back('i');
    inflated.push_back('m');
    inflated.push_back(1);
    inflated.push_back(2);
    AppendU32(inflated, options.imagePointer);
    AppendU32(inflated, 0x22222222u);
    inflated.push_back('w');
    inflated.push_back('t');
    inflated.push_back(2);
    inflated.push_back(11);
    AppendU32(inflated, options.waterPointer);
    block4Offset += 2u * sizeof(MaterialTextureDef);

    if (options.imagePointer == UINT32_MAX ||
        options.imagePointer == UINT32_MAX - 1u)
    {
        if (options.imagePointer == UINT32_MAX - 1u)
            block4Offset = Align4(block4Offset) + 4u;
        block0Offset = Align4(block0Offset);
        const std::size_t imageBodyStart = inflated.size();
        AppendU32(inflated, MAPTYPE_2D);
        AppendU32(inflated, options.imageTexturePointer);
        inflated.push_back(0);
        inflated.push_back(0);
        inflated.push_back(0);
        inflated.push_back(2);
        inflated.push_back(3);
        AppendZeros(inflated, 3);
        AppendU32(inflated, 64);
        AppendU32(inflated, 32);
        AppendU16(inflated, 4);
        AppendU16(inflated, 4);
        AppendU16(inflated, 1);
        inflated.push_back(1);
        inflated.push_back(0);
        AppendU32(inflated, options.directImageName
            ? materialNameAlias : UINT32_MAX);
        assert(inflated.size() - imageBodyStart == sizeof(GfxImage));
        block0Offset += sizeof(GfxImage);

        if (!options.directImageName)
        {
            inflated.insert(inflated.end(), imageName,
                imageName + std::strlen(imageName));
            if (options.terminateImageName) inflated.push_back(0);
            block4Offset += static_cast<std::uint32_t>(std::strlen(imageName) +
                (options.terminateImageName ? 1u : 0u));
        }
        if (options.imageTexturePointer == UINT32_MAX ||
            options.imageTexturePointer == UINT32_MAX - 1u)
        {
            if (options.imageTexturePointer == UINT32_MAX - 1u)
                block4Offset = Align4(block4Offset) + 4u;
            block0Offset = Align4(block0Offset);
            inflated.push_back(1);
            inflated.push_back(0);
            AppendU16(inflated, 4);
            AppendU16(inflated, 4);
            AppendU16(inflated, 1);
            AppendU32(inflated, 21);
            AppendU32(inflated,
                static_cast<std::uint32_t>(options.imageResourceSize));
            if (options.imageResourceSize == 4)
                AppendU32(inflated, 0x494D4733u);
            block0Offset += 16u + (options.imageResourceSize == 4 ? 4u : 0u);
        }
    }

    if (options.waterPointer == UINT32_MAX)
    {
        block4Offset = Align4(block4Offset);
        const std::size_t waterBodyStart = inflated.size();
        AppendF32(inflated, 0.5f);
        AppendU32(inflated, 1);
        AppendU32(inflated, 1);
        AppendU32(inflated, static_cast<std::uint32_t>(options.waterM));
        AppendU32(inflated, static_cast<std::uint32_t>(options.waterN));
        for (float value : std::array<float, 7>{
            4.0f, 4.0f, 9.8f, 2.0f, 1.0f, 0.0f, 0.25f})
            AppendF32(inflated, value);
        for (float value : std::array<float, 4>{1.0f, 2.0f, 3.0f, 4.0f})
            AppendF32(inflated, value);
        AppendU32(inflated, options.imagePointer == UINT32_MAX - 1u
            ? imageInsertionAlias : 0u);
        assert(inflated.size() - waterBodyStart == sizeof(water_t));
        block4Offset += sizeof(water_t);
        if (options.waterM == 2 && options.waterN == 2)
        {
            block4Offset = Align4(block4Offset);
            for (std::uint32_t index = 0; index < 8; ++index)
                AppendF32(inflated, static_cast<float>(index + 1));
            block4Offset += 4u * sizeof(complex_s);
            block4Offset = Align4(block4Offset);
            for (std::uint32_t index = 0; index < 4; ++index)
                AppendF32(inflated, static_cast<float>(index + 10));
            block4Offset += 4u * sizeof(float);
        }
    }

    if (options.constantTablePointer == UINT32_MAX)
    {
        block4Offset = Align16(block4Offset);
        AppendU32(inflated, 0x33333333u);
        inflated.insert(inflated.end(), {'g','a','t','e','3',0,0,0,0,0,0,0});
        for (float value : std::array<float, 4>{5.0f, 6.0f, 7.0f, 8.0f})
            AppendF32(inflated, value);
        block4Offset += sizeof(MaterialConstantDef);
    }
    if (options.stateBitsTablePointer == UINT32_MAX)
    {
        block4Offset = Align4(block4Offset);
        AppendU32(inflated, 0x44444444u);
        AppendU32(inflated, 0x55555555u);
        block4Offset += sizeof(GfxStateBits);
    }
    return CompressXFile(inflated);
}

struct LocalizeFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    std::uint32_t valuePointer = UINT32_MAX;
    std::uint32_t namePointer = UINT32_MAX;
    bool directNameToValueInterior = false;
    bool includeValue = true;
    bool includeName = true;
    bool terminateValue = true;
    bool terminateName = true;
    std::size_t valueLength = 0;
    std::size_t nameLength = 0;
};

std::vector<std::uint8_t> MakeLocalizeXFile(
    const LocalizeFixtureOptions &options = {})
{
    constexpr const char *localizedValue = "Localized gate3";
    constexpr const char *localizedName = "LOCALIZE_GATE3";
    const bool inlineAsset = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const std::uint32_t assetCount = options.includeAliasAsset ? 2u : 1u;

    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 8192);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);

    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, assetCount);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_LOCALIZE_ENTRY);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_LOCALIZE_ENTRY);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset) return CompressXFile(inflated);

    std::uint32_t block4Offset = assetCount * sizeof(XAsset);
    if (options.assetPointer == UINT32_MAX - 1u)
        block4Offset = Align4(block4Offset) + 4u;
    const std::uint32_t valueOffset = block4Offset;
    const std::uint32_t namePointer = options.directNameToValueInterior
        ? 0x40000001u + valueOffset + 10u : options.namePointer;

    AppendU32(inflated, options.valuePointer);
    AppendU32(inflated, namePointer);
    if (options.valuePointer == UINT32_MAX && options.includeValue)
    {
        if (options.valueLength)
            inflated.insert(inflated.end(), options.valueLength, 'v');
        else
            inflated.insert(inflated.end(), localizedValue,
                localizedValue + std::strlen(localizedValue));
        if (options.terminateValue) inflated.push_back(0);
    }
    if (namePointer == UINT32_MAX && options.includeName)
    {
        if (options.nameLength)
            inflated.insert(inflated.end(), options.nameLength, 'n');
        else
            inflated.insert(inflated.end(), localizedName,
                localizedName + std::strlen(localizedName));
        if (options.terminateName) inflated.push_back(0);
    }
    return CompressXFile(inflated);
}

struct SndCurveFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    std::uint32_t filenamePointer = UINT32_MAX;
    bool includeBody = true;
    bool includeFilename = true;
    bool terminateFilename = true;
};

std::vector<std::uint8_t> MakeSndCurveXFile(
    const SndCurveFixtureOptions &options = {})
{
    constexpr const char *filename = "soundcurves/gate3";
    const bool inlineAsset = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const std::uint32_t assetCount = options.includeAliasAsset ? 2u : 1u;

    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 8192);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);

    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, assetCount);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_SOUND_CURVE);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_SOUND_CURVE);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset || !options.includeBody) return CompressXFile(inflated);

    AppendU32(inflated, options.filenamePointer);
    AppendU32(inflated, 3);
    for (std::uint32_t knot = 0; knot < 8; ++knot)
    {
        AppendF32(inflated, static_cast<float>(knot) / 7.0f);
        AppendF32(inflated, 1.0f - static_cast<float>(knot) / 7.0f);
    }
    if (options.filenamePointer == UINT32_MAX && options.includeFilename)
    {
        inflated.insert(inflated.end(), filename,
            filename + std::strlen(filename));
        if (options.terminateFilename) inflated.push_back(0);
    }
    return CompressXFile(inflated);
}

std::vector<std::uint8_t> MakeSndDriverGlobalsXFile()
{
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 4096);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        0, 0, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, 1);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_SNDDRIVER_GLOBALS);
    AppendU32(inflated, 0x40000001u);
    return CompressXFile(inflated);
}

struct SoundAliasFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    std::int32_t aliasCount = 1;
    std::uint32_t headPointer = UINT32_MAX;
    bool terminateListName = true;
};

std::vector<std::uint8_t> MakeSoundAliasXFile(
    const SoundAliasFixtureOptions &options = {})
{
    constexpr const char *listName = "sound/gate3";
    constexpr const char *streamDir = "sound";
    constexpr const char *streamName = "gate3.wav";
    constexpr const char *speakerName = "speaker/gate3";
    const bool inlineAsset = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const std::uint32_t assetCount = options.includeAliasAsset ? 2u : 1u;

    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 16384);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 8192, 0, 0, 0, 0}) AppendU32(inflated, size);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, assetCount);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_SOUND);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_SOUND);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset) return CompressXFile(inflated);

    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, options.headPointer);
    AppendU32(inflated, static_cast<std::uint32_t>(options.aliasCount));
    inflated.insert(inflated.end(), listName,
        listName + std::strlen(listName));
    if (options.terminateListName) inflated.push_back(0);
    if (!options.terminateListName || options.headPointer != UINT32_MAX ||
        options.aliasCount != 1) return CompressXFile(inflated);

    while ((inflated.size() & 3u) != 0u) inflated.push_back(0);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 0);
    for (float value : std::array<float, 6>{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 100.0f})
        AppendF32(inflated, value);
    AppendU32(inflated, 0);
    for (float value : std::array<float, 4>{0.0f, 1.0f, 0.0f, 0.0f})
        AppendF32(inflated, value);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    for (float value : std::array<float, 3>{0.0f, 0.0f, 0.0f})
        AppendF32(inflated, value);
    AppendU32(inflated, UINT32_MAX);

    AppendCString(inflated, listName);
    while ((inflated.size() & 3u) != 0u) inflated.push_back(0);
    AppendU32(inflated, 0x00000100u);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, UINT32_MAX);
    AppendCString(inflated, streamDir);
    AppendCString(inflated, streamName);
    while ((inflated.size() & 3u) != 0u) inflated.push_back(0);
    AppendU32(inflated, 1);
    AppendU32(inflated, UINT32_MAX);
    AppendZeros(inflated, sizeof(SpeakerMap) - 8u);
    AppendCString(inflated, speakerName);
    return CompressXFile(inflated);
}

struct LoadedSoundFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    std::uint32_t dataPointer = UINT32_MAX - 1u;
    std::uint32_t dataLength = 4;
    bool includeData = true;
};

std::vector<std::uint8_t> MakeLoadedSoundXFile(
    const LoadedSoundFixtureOptions &options = {})
{
    constexpr const char *name = "loaded/gate3.wav";
    const bool inlineAsset = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const std::uint32_t assetCount = options.includeAliasAsset ? 2u : 1u;
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 8192);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, assetCount);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_LOADED_SOUND);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_LOADED_SOUND);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset) return CompressXFile(inflated);

    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 1);
    AppendU32(inflated, 0);
    AppendU32(inflated, options.dataLength);
    AppendU32(inflated, 44100);
    AppendU32(inflated, 16);
    AppendU32(inflated, 1);
    AppendU32(inflated, 2);
    AppendU32(inflated, 2);
    AppendU32(inflated, 0);
    AppendU32(inflated, options.dataPointer);
    AppendCString(inflated, name);
    if (options.dataPointer == UINT32_MAX - 1u)
    {
        while ((inflated.size() & 3u) != 0u) inflated.push_back(0);
        AppendU32(inflated, 0);
    }
    if (options.includeData)
        for (std::uint32_t index = 0; index < options.dataLength; ++index)
            inflated.push_back(static_cast<std::uint8_t>(index + 1u));
    return CompressXFile(inflated);
}

struct FontFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    std::int32_t glyphCount = 2;
    std::uint32_t glyphPointer = UINT32_MAX;
    bool includeGlyphs = true;
};

std::vector<std::uint8_t> MakeFontXFile(
    const FontFixtureOptions &options = {})
{
    constexpr const char *name = "fonts/gate3";
    const bool inlineAsset = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const std::uint32_t assetCount = options.includeAliasAsset ? 2u : 1u;
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 8192);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, assetCount);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_FONT);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_FONT);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset) return CompressXFile(inflated);

    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 16);
    AppendU32(inflated, static_cast<std::uint32_t>(options.glyphCount));
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, options.glyphPointer);
    AppendCString(inflated, name);
    if (options.glyphPointer == UINT32_MAX && options.includeGlyphs &&
        options.glyphCount > 0 && options.glyphCount < 64)
    {
        while ((inflated.size() & 3u) != 0u) inflated.push_back(0);
        AppendZeros(inflated,
            static_cast<std::size_t>(options.glyphCount) * sizeof(Glyph));
    }
    return CompressXFile(inflated);
}

struct FxFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    bool includeBody = true;
    bool terminateName = true;
    std::int32_t elementCount = 1;
    std::uint8_t elementType = 6;
    std::uint8_t visualCount = 0;
    std::uint32_t visualPointer = 0;
    bool includeSamples = true;
    bool includeTrail = true;
    std::int32_t trailVertCount = 2;
    std::int32_t trailIndCount = 3;
    bool includeTrailData = true;
};

std::vector<std::uint8_t> MakeFxXFile(
    const FxFixtureOptions &options = {})
{
    constexpr const char *name = "fx/gate3";
    const bool inlineAsset = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const std::uint32_t assetCount = options.includeAliasAsset ? 2u : 1u;
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 16384);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 12288, 0, 0, 0, 0}) AppendU32(inflated, size);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, assetCount);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_FX);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_FX);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset || !options.includeBody) return CompressXFile(inflated);

    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 0x1234u);
    AppendU32(inflated, sizeof(FxEffectDef));
    AppendU32(inflated, 250);
    AppendU32(inflated, 0);
    AppendU32(inflated, static_cast<std::uint32_t>(options.elementCount));
    AppendU32(inflated, 0);
    AppendU32(inflated, options.elementCount == 0 ? 0u : 1u);
    inflated.insert(inflated.end(), name, name + std::strlen(name));
    if (options.terminateName) inflated.push_back(0);
    if (!options.terminateName || options.elementCount != 1)
        return CompressXFile(inflated);

    while ((inflated.size() & 3u) != 0u) inflated.push_back(0);
    const std::size_t elementOffset = inflated.size();
    AppendZeros(inflated, sizeof(FxElemDef));
    inflated[elementOffset + offsetof(FxElemDef, elemType)] =
        options.elementType;
    inflated[elementOffset + offsetof(FxElemDef, visualCount)] =
        options.visualCount;
    if (options.includeSamples)
    {
        WriteU32(inflated, elementOffset + offsetof(FxElemDef, velSamples), 1);
        WriteU32(inflated, elementOffset + offsetof(FxElemDef, visSamples), 1);
    }
    WriteU32(inflated, elementOffset + offsetof(FxElemDef, visuals),
        options.visualPointer);
    if (options.includeTrail)
        WriteU32(inflated, elementOffset + offsetof(FxElemDef, trailDef), 1);

    if (options.includeSamples)
    {
        while ((inflated.size() & 3u) != 0u) inflated.push_back(0);
        AppendZeros(inflated, sizeof(FxElemVelStateSample));
        AppendZeros(inflated, sizeof(FxElemVisStateSample));
    }
    if (options.visualPointer == UINT32_MAX && options.elementType == 8)
        AppendCString(inflated, "sound/fx_gate3");
    if (!options.includeTrail) return CompressXFile(inflated);

    while ((inflated.size() & 3u) != 0u) inflated.push_back(0);
    const std::size_t trailOffset = inflated.size();
    AppendZeros(inflated, sizeof(FxTrailDef));
    WriteU32(inflated, trailOffset + offsetof(FxTrailDef, vertCount),
        static_cast<std::uint32_t>(options.trailVertCount));
    WriteU32(inflated, trailOffset + offsetof(FxTrailDef, verts), 1);
    WriteU32(inflated, trailOffset + offsetof(FxTrailDef, indCount),
        static_cast<std::uint32_t>(options.trailIndCount));
    WriteU32(inflated, trailOffset + offsetof(FxTrailDef, inds), 1);
    if (!options.includeTrailData) return CompressXFile(inflated);
    if (options.trailVertCount > 0 && options.trailVertCount < 64)
    {
        while ((inflated.size() & 3u) != 0u) inflated.push_back(0);
        AppendZeros(inflated, static_cast<std::size_t>(options.trailVertCount) *
            sizeof(FxTrailVertex));
    }
    if (options.trailIndCount > 0 && options.trailIndCount < 64)
    {
        while ((inflated.size() & 1u) != 0u) inflated.push_back(0);
        for (std::int32_t index = 0; index < options.trailIndCount; ++index)
            AppendU16(inflated, static_cast<std::uint16_t>(index));
    }
    return CompressXFile(inflated);
}

std::vector<std::uint8_t> MakeEmptyXFile(
    const std::array<std::uint32_t, 9> &blocks)
{
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 4096);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : blocks) AppendU32(inflated, size);
    for (int index = 0; index < 4; ++index) AppendU32(inflated, 0);
    return CompressXFile(inflated);
}

std::vector<std::uint8_t> MakeScriptListXFile()
{
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 8192);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);
    AppendU32(inflated, 2);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, UINT32_MAX);
    AppendCString(inflated, "gate3_alpha");
    AppendCString(inflated, "gate3_beta");
    return CompressXFile(inflated);
}

void Reset(const std::vector<std::uint8_t> &file)
{
    g_file = file;
    g_filePosition = 0;
    g_trace = {};
    g_lowPosition = 0;
    g_highPosition = static_cast<std::uint32_t>(g_arena.size());
    std::fill(g_arena.begin(), g_arena.end(), 0);
    g_scriptString.clear();
    std::memset(g_zones, 0, sizeof(g_zones));
    g_zones[1].flags = 1;
    DB_InitAssetPools();
    DB_SetLoadingZoneIndex(1);
}

void RunPrepared(XZoneMemory &zone)
{
    zone = {};
    alignas(16) static std::array<std::uint8_t, 0x80000> inputBuffer{};
    DB_LoadXFile("zone/english/synthetic.ff", reinterpret_cast<void *>(1),
        "synthetic", &zone, nullptr, inputBuffer.data(), 0);
    DB_LoadXFileInternal();
}

void Run(const std::vector<std::uint8_t> &file, XZoneMemory &zone)
{
    Reset(file);
    RunPrepared(zone);
}
} // namespace

WebDatabaseFile WebDatabaseFS_Open(const char *) { return 0; }
std::int64_t WebDatabaseFS_Size(WebDatabaseFile) { return static_cast<std::int64_t>(g_file.size()); }
bool WebDatabaseFS_Seek(WebDatabaseFile, std::uint32_t offset)
{
    if (offset > g_file.size()) return false;
    g_filePosition = offset;
    return true;
}
std::int32_t WebDatabaseFS_Read(WebDatabaseFile, void *destination, std::uint32_t length)
{
    const std::size_t count = std::min<std::size_t>(length, g_file.size() - g_filePosition);
    if (count) std::memcpy(destination, g_file.data() + g_filePosition, count);
    g_filePosition += count;
    return static_cast<std::int32_t>(count);
}
void WebDatabaseFS_Close(WebDatabaseFile) {}

void DB_RuntimeTraceStage(const char *) {}
void DB_RuntimeTraceStop(const char *stage) { g_trace.stopStage = stage; }
void DB_RuntimeTraceHeaderRead(std::uint32_t, std::uint32_t) { g_trace.headerValid = true; }
void DB_RuntimeTraceInputRefill(std::uint32_t bytesRead)
{
    g_trace.bytesRead += bytesRead;
    ++g_trace.inputRefillCount;
}
void DB_RuntimeTraceInflate(std::uint32_t consumed, std::uint32_t produced)
{
    g_trace.compressedBytesConsumed = consumed;
    g_trace.decompressedBytesProduced = produced;
}
void DB_RuntimeTraceInflateInitialized() { g_trace.inflateInitialized = true; }
void DB_RuntimeTraceXFile(std::uint32_t size, std::uint32_t externalSize,
    const std::uint32_t *blockSizes)
{
    g_trace.xfileSize = size;
    g_trace.xfileExternalSize = externalSize;
    std::memcpy(g_trace.blockSizes, blockSizes, sizeof(g_trace.blockSizes));
}
void DB_RuntimeTraceBlockAllocation(std::uint32_t, std::uint32_t size)
{
    ++g_trace.blockAllocationCount;
    g_trace.blockAllocationBytes += size;
}
void DB_RuntimeTraceStreamsInitialized(std::uint32_t block, std::uint32_t offset)
{
    g_trace.streamBlock = block;
    g_trace.streamOffset = offset;
    g_trace.streamInitialized = true;
}
void DB_RuntimeTraceCleanupComplete() { g_trace.cleanupComplete = true; }
void DB_RuntimeTraceXAssetListBegin(std::int32_t strings, std::int32_t assets)
{
    g_trace.xassetListBegin = true;
    g_trace.scriptStringCount = strings >= 0 ? strings : UINT32_MAX;
    g_trace.xassetCount = assets >= 0 ? assets : UINT32_MAX;
}
void DB_RuntimeTraceXAssetListEnd()
{
    g_trace.xassetListEnd = true;
    for (std::uint32_t index = 0; index < 9; ++index)
    {
        const std::uint8_t *position = index == g_streamPosIndex
            ? g_streamPos : g_streamPosArray[index];
        const std::uint8_t *base = g_streamZoneMem->blocks[index].data;
        g_trace.streamOffsets[index] = position && base && position >= base
            ? static_cast<std::uint32_t>(position - base) : 0u;
    }
}
void DB_RuntimeTraceScriptString(std::uint32_t index, const char *identity)
{
    if (index < std::size(g_trace.scriptStringIdentities))
        std::snprintf(g_trace.scriptStringIdentities[index],
            sizeof(g_trace.scriptStringIdentities[index]), "%s", identity ? identity : "");
    g_trace.scriptStringObservedCount = index + 1;
}
void DB_RuntimeTraceAssetBegin(std::uint32_t index, XAssetType type,
    const char *classification)
{
    g_trace.assetIndex = index;
    g_trace.assetType = type;
    std::snprintf(g_trace.pointerClassification,
        sizeof(g_trace.pointerClassification), "%s", classification ? classification : "");
}
void DB_RuntimeTraceAssetLoaded(const char *name)
{
    std::snprintf(g_trace.assetName, sizeof(g_trace.assetName), "%s", name ? name : "");
}
void DB_RuntimeTracePublicationBegin(XAssetType type, const char *name,
    std::size_t freeCount)
{
    g_trace.publicationBegin = true;
    g_trace.assetType = type;
    g_trace.freeEntryCountBefore = static_cast<std::uint32_t>(freeCount);
    std::snprintf(g_trace.assetName, sizeof(g_trace.assetName), "%s", name ? name : "");
}
void DB_RuntimeTracePublicationEnd(XAssetType type, const char *name,
    std::uint32_t entryIndex, std::uint32_t poolIndex, std::size_t freeBefore,
    std::size_t freeAfter, std::uint32_t hash, std::uint32_t zoneIndex)
{
    g_trace.publicationEnd = true;
    g_trace.assetType = type;
    g_trace.assetEntryIndex = entryIndex;
    g_trace.assetPoolIndex = poolIndex;
    g_trace.freeEntryCountBefore = static_cast<std::uint32_t>(freeBefore);
    g_trace.freeEntryCountAfter = static_cast<std::uint32_t>(freeAfter);
    g_trace.assetHash = hash;
    g_trace.assetZoneIndex = zoneIndex;
    std::snprintf(g_trace.assetName, sizeof(g_trace.assetName), "%s", name ? name : "");
}
void DB_RuntimeGeneratedFailure(const char *stage)
{
    if (!g_trace.generatedLoadFailed)
    {
        g_trace.generatedLoadFailed = true;
        DB_FailXFileLoad(stage);
    }
}
bool DB_RuntimeGeneratedLoadFailed()
{
    return g_trace.generatedLoadFailed || DB_HasXFileLoadFailure();
}
bool DB_RuntimeStreamCanRead(std::size_t size)
{
    if (g_streamPosIndex >= 9 || !g_streamZoneMem || !g_streamPos) return false;
    const XBlock &block = g_streamZoneMem->blocks[g_streamPosIndex];
    if (!block.data || g_streamPos < block.data) return size == 0;
    const std::size_t offset = static_cast<std::size_t>(g_streamPos - block.data);
    return offset <= block.size && size <= block.size - offset;
}

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    throw std::runtime_error("canonical DB assertion");
}
void Com_Error(errorParm_t, const char *, ...) { throw std::runtime_error("canonical DB error"); }
void Com_PrintError(int, const char *, ...) {}
void Com_Printf(int, const char *, ...) {}
int I_stricmp(const char *left, const char *right)
{
    while (*left && std::tolower(static_cast<unsigned char>(*left)) ==
        std::tolower(static_cast<unsigned char>(*right)))
    {
        ++left;
        ++right;
    }
    return std::tolower(static_cast<unsigned char>(*left)) -
        std::tolower(static_cast<unsigned char>(*right));
}
void track_static_alloc_internal(void *, int, const char *, int) {}
void Sys_LockWrite(FastCriticalSection *section)
{
    section->writeCount = section->writeCount + 1;
}
void Sys_UnlockWrite(FastCriticalSection *section)
{
    section->writeCount = section->writeCount - 1;
}
std::uint32_t SL_GetString(const char *value, std::uint32_t)
{
    g_scriptString = value ? value : "";
    return g_scriptString.empty() ? 0u : 1u;
}
const char *SL_ConvertToString(std::uint32_t value)
{
    return value == 1 ? g_scriptString.c_str() : "";
}

std::uint8_t *PMem_Alloc(std::uint32_t size, std::uint32_t alignment,
    std::uint32_t, std::uint32_t allocType)
{
    const std::uint32_t mask = alignment - 1;
    if (allocType == 0)
    {
        const std::uint32_t start = (g_lowPosition + mask) & ~mask;
        if (start > g_highPosition || size > g_highPosition - start) return nullptr;
        g_lowPosition = start + size;
        return g_arena.data() + start;
    }
    if (size > g_highPosition) return nullptr;
    const std::uint32_t start = (g_highPosition - size) & ~mask;
    if (start < g_lowPosition) return nullptr;
    g_highPosition = start;
    return g_arena.data() + start;
}
std::uint32_t PMem_GetFreeAmount() { return g_highPosition - g_lowPosition; }
int PMem_GetOverAllocatedSize() { return 0; }
const PhysicalMemory *PMem_GetState()
{
    static PhysicalMemory memory{};
    memory.buf = g_arena.data();
    memory.prim[0].pos = g_lowPosition;
    memory.prim[1].pos = g_highPosition;
    return &memory;
}

int main()
{
    XZoneMemory zone{};
    const std::array<std::uint32_t, 9> prefixBlocks{
        4096, 0, 0, 0, 4096, 0, 0, 0, 0};
    Run(MakeEmptyXFile(prefixBlocks), zone);
    assert(g_trace.decompressedBytesProduced == 60);
    assert(g_trace.xassetListBegin && g_trace.xassetListEnd);
    assert(g_trace.scriptStringCount == 0 && g_trace.xassetCount == 0);
    assert(std::all_of(std::begin(g_trace.streamOffsets),
        std::end(g_trace.streamOffsets), [](std::uint32_t value) { return value == 0; }));
    assert(std::strcmp(g_trace.stopStage, "Load_XAssetHeader/next-family-closure") == 0);

    Run(MakeScriptListXFile(), zone);
    assert(g_trace.scriptStringCount == 2 && g_trace.scriptStringObservedCount == 2);
    assert(std::strcmp(g_trace.scriptStringIdentities[0], "gate3_alpha") == 0);
    assert(std::strcmp(g_trace.scriptStringIdentities[1], "gate3_beta") == 0);
    assert(g_trace.xassetCount == 0 && g_trace.streamOffsets[4] == 31);
    assert(!g_trace.publicationBegin && !g_trace.generatedLoadFailed);

    const std::vector<std::uint8_t> generated = MakeGeneratedPrefixXFile();
    Run(generated, zone);
    assert(g_trace.headerValid && g_trace.inflateInitialized);
    assert(g_trace.decompressedBytesProduced == 134);
    assert(g_trace.xfileSize == 8192 && g_trace.blockAllocationCount == 2);
    assert(g_trace.blockAllocationBytes == 8192 && g_trace.streamInitialized);
    assert(g_trace.xassetListBegin && g_trace.xassetListEnd);
    assert(g_trace.scriptStringCount == 1 && g_trace.scriptStringObservedCount == 1);
    assert(std::strcmp(g_trace.scriptStringIdentities[0], "gate3_script_identity") == 0);
    assert(g_trace.xassetCount == 1 && g_trace.assetIndex == 0);
    assert(g_trace.assetType == ASSET_TYPE_RAWFILE);
    assert(std::strcmp(g_trace.pointerClassification, "inline-insert/-2") == 0);
    assert(std::strcmp(g_trace.assetName, "tests/gate3_first.txt") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32752 && g_trace.freeEntryCountAfter == 32751);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "tests/gate3_first.txt", ASSET_TYPE_RAWFILE));
    assert(g_trace.assetZoneIndex == 1);
    assert(g_trace.streamOffsets[0] == 0 && g_trace.streamOffsets[4] == 68);
    assert(g_trace.cleanupComplete && !g_trace.generatedLoadFailed);
    assert(std::strcmp(g_trace.stopStage, "Load_XAssetHeader/next-family-closure") == 0);
    const XAssetHeader published = DB_FindXAssetHeader(
        ASSET_TYPE_RAWFILE, "tests/gate3_first.txt");
    assert(published.rawfile && published.rawfile->len == 5);
    assert(std::strcmp(published.rawfile->buffer, "first") == 0);

    const std::vector<std::uint8_t> physInsertAlias = MakePhysPresetXFile();
    Run(physInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_PHYSPRESET);
    assert(std::strcmp(g_trace.pointerClassification, "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32752 && g_trace.freeEntryCountAfter == 32751);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "physics/gate3", ASSET_TYPE_PHYSPRESET));
    assert(g_trace.assetZoneIndex == 1);
    assert(g_trace.streamOffsets[0] == sizeof(PhysPreset));
    assert(g_trace.streamOffsets[4] == 34);
    const XAssetHeader publishedPhys = DB_FindXAssetHeader(
        ASSET_TYPE_PHYSPRESET, "physics/gate3");
    assert(publishedPhys.physPreset);
    assert(publishedPhys.physPreset->type == 7);
    assert(publishedPhys.physPreset->mass == 12.5f);
    assert(publishedPhys.physPreset->tempDefaultToCylinder);
    assert(publishedPhys.physPreset->sndAliasPrefix ==
        publishedPhys.physPreset->name);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_PHYSPRESET) == 63);

    Run(MakePhysPresetXFile(UINT32_MAX, UINT32_MAX, UINT32_MAX,
        false), zone);
    assert(std::strcmp(g_trace.pointerClassification, "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.assetPoolIndex == 0);
    const XAssetHeader sharedPhys = DB_FindXAssetHeader(
        ASSET_TYPE_PHYSPRESET, "physics/gate3");
    assert(sharedPhys.physPreset);
    assert(std::strcmp(sharedPhys.physPreset->sndAliasPrefix, "metal") == 0);

    const std::vector<std::uint8_t> techniqueInsertAlias =
        MakeTechniqueSetXFile();
    Run(techniqueInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_TECHNIQUE_SET);
    assert(std::strcmp(g_trace.pointerClassification, "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32752 &&
        g_trace.freeEntryCountAfter == 32751);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "techsets/gate3", ASSET_TYPE_TECHNIQUE_SET));
    assert(g_trace.assetZoneIndex == 1);
    assert(g_trace.streamOffsets[0] == sizeof(MaterialTechniqueSet));
    assert(g_trace.streamOffsets[4] == 251);
    const XAssetHeader publishedTechniqueSet = DB_FindXAssetHeader(
        ASSET_TYPE_TECHNIQUE_SET, "techsets/gate3");
    assert(publishedTechniqueSet.techniqueSet);
    assert(publishedTechniqueSet.techniqueSet->worldVertFormat == 2);
    assert(publishedTechniqueSet.techniqueSet->remappedTechniqueSet ==
        publishedTechniqueSet.techniqueSet);
    assert(publishedTechniqueSet.techniqueSet->techniques[0]);
    assert(publishedTechniqueSet.techniqueSet->techniques[1] ==
        publishedTechniqueSet.techniqueSet->techniques[0]);
    const MaterialTechnique *publishedTechnique =
        publishedTechniqueSet.techniqueSet->techniques[0];
    assert(publishedTechnique->flags == 0x12u &&
        publishedTechnique->passCount == 1);
    assert(std::strcmp(publishedTechnique->name, "tech_gate3") == 0);
    const MaterialPass &publishedPass = publishedTechnique->passArray[0];
    assert(publishedPass.vertexDecl && publishedPass.vertexDecl->isLoaded);
    assert(std::all_of(std::begin(publishedPass.vertexDecl->routing.decl),
        std::end(publishedPass.vertexDecl->routing.decl),
        [](const void *decl) { return decl == nullptr; }));
    assert(publishedPass.vertexShader && publishedPass.pixelShader);
    assert(publishedPass.vertexShader->name ==
        publishedTechniqueSet.techniqueSet->name);
    assert(publishedPass.vertexShader->prog.vs == nullptr);
    assert(publishedPass.vertexShader->prog.loadDef.program &&
        *static_cast<std::uint32_t *>(
            publishedPass.vertexShader->prog.loadDef.program) == 0x56530001u);
    assert(std::strcmp(publishedPass.pixelShader->name, "ps_gate3") == 0);
    assert(publishedPass.pixelShader->prog.ps == nullptr);
    assert(publishedPass.pixelShader->prog.loadDef.program &&
        *static_cast<std::uint32_t *>(
            publishedPass.pixelShader->prog.loadDef.program) == 0x50530001u);
    assert(publishedPass.args && publishedPass.args[0].type == 1 &&
        publishedPass.args[0].dest == 3);
    assert(publishedPass.args[0].u.literalConst[0] == 1.0f &&
        publishedPass.args[0].u.literalConst[3] == 4.0f);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_TECHNIQUE_SET) == 1023);
    const XAsset *techniqueAssets = reinterpret_cast<const XAsset *>(
        zone.blocks[4].data);
    assert(techniqueAssets[0].header.techniqueSet == publishedTechniqueSet.techniqueSet);
    assert(techniqueAssets[1].header.techniqueSet == publishedTechniqueSet.techniqueSet);

    TechniqueSetFixtureOptions sharedTechniqueOptions{};
    sharedTechniqueOptions.assetPointer = UINT32_MAX;
    sharedTechniqueOptions.includeAliasAsset = false;
    sharedTechniqueOptions.includeTechniqueAlias = false;
    Run(MakeTechniqueSetXFile(sharedTechniqueOptions), zone);
    assert(std::strcmp(g_trace.pointerClassification, "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.assetPoolIndex == 0);
    assert(DB_FindXAssetHeader(ASSET_TYPE_TECHNIQUE_SET,
        "techsets/gate3").techniqueSet);

    const std::vector<std::uint8_t> materialInsertAlias = MakeMaterialXFile();
    Run(materialInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_MATERIAL);
    assert(std::strcmp(g_trace.pointerClassification,
        "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 17 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32751 &&
        g_trace.freeEntryCountAfter == 32750);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "materials/gate3", ASSET_TYPE_MATERIAL));
    assert(g_trace.streamOffsets[0] == 136);
    assert(g_trace.streamOffsets[4] == 248);
    const XAssetHeader publishedMaterial = DB_FindXAssetHeader(
        ASSET_TYPE_MATERIAL, "materials/gate3");
    const XAssetHeader publishedImage = DB_FindXAssetHeader(
        ASSET_TYPE_IMAGE, "images/gate3");
    assert(publishedMaterial.material && publishedImage.image);
    assert(publishedMaterial.material->info.gameFlags == 1);
    assert(publishedMaterial.material->info.sortKey == 2);
    assert(publishedMaterial.material->techniqueSet == nullptr);
    assert(publishedMaterial.material->textureCount == 2);
    assert(publishedMaterial.material->textureTable);
    assert(publishedMaterial.material->textureTable[0].semantic == 2);
    assert(publishedMaterial.material->textureTable[0].u.image ==
        publishedImage.image);
    assert(publishedImage.image->mapType == MAPTYPE_2D);
    assert(publishedImage.image->width == 4 && publishedImage.image->height == 4);
    assert(publishedImage.image->texture.basemap == nullptr);
    const water_t *publishedWater =
        publishedMaterial.material->textureTable[1].u.water;
    assert(publishedWater && publishedWater->M == 2 && publishedWater->N == 2);
    assert(publishedWater->image == publishedImage.image);
    assert(publishedWater->H0 && publishedWater->H0[0].real == 1.0f &&
        publishedWater->H0[3].imag == 8.0f);
    assert(publishedWater->wTerm && publishedWater->wTerm[0] == 10.0f &&
        publishedWater->wTerm[3] == 13.0f);
    assert(publishedMaterial.material->constantTable &&
        publishedMaterial.material->constantTable[0].nameHash == 0x33333333u &&
        publishedMaterial.material->constantTable[0].literal[3] == 8.0f);
    assert(publishedMaterial.material->stateBitsTable &&
        publishedMaterial.material->stateBitsTable[0].loadBits[0] ==
            0x44444444u &&
        publishedMaterial.material->stateBitsTable[0].loadBits[1] ==
            0x55555555u);
    assert(reinterpret_cast<const std::uint8_t *>(
        publishedMaterial.material->constantTable) == zone.blocks[4].data + 208);
    assert(reinterpret_cast<const std::uint8_t *>(
        publishedMaterial.material->stateBitsTable) == zone.blocks[4].data + 240);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_MATERIAL) == 2047);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_IMAGE) == 2399);
    const XAsset *materialAssets = reinterpret_cast<const XAsset *>(
        zone.blocks[4].data);
    assert(materialAssets[0].header.material == publishedMaterial.material);
    assert(materialAssets[1].header.material == publishedMaterial.material);
    std::uint32_t materialInsertion = 0;
    std::uint32_t imageInsertion = 0;
    std::memcpy(&materialInsertion, zone.blocks[4].data + 16,
        sizeof(materialInsertion));
    std::memcpy(&imageInsertion, zone.blocks[4].data + 60,
        sizeof(imageInsertion));
    assert(materialInsertion == reinterpret_cast<std::uint32_t>(
        publishedMaterial.material));
    assert(imageInsertion == reinterpret_cast<std::uint32_t>(
        publishedImage.image));

    MaterialFixtureOptions sharedMaterialOptions{};
    sharedMaterialOptions.assetPointer = UINT32_MAX;
    sharedMaterialOptions.includeAliasAsset = false;
    sharedMaterialOptions.imagePointer = UINT32_MAX;
    sharedMaterialOptions.directImageName = true;
    sharedMaterialOptions.waterPointer = 0;
    Run(MakeMaterialXFile(sharedMaterialOptions), zone);
    assert(std::strcmp(g_trace.pointerClassification,
        "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.assetEntryIndex == 17);
    assert(DB_FindXAssetHeader(ASSET_TYPE_MATERIAL,
        "materials/gate3").material);
    assert(DB_FindXAssetHeader(ASSET_TYPE_IMAGE,
        "materials/gate3").image);

    MaterialFixtureOptions nullMaterialDependencies{};
    nullMaterialDependencies.assetPointer = UINT32_MAX;
    nullMaterialDependencies.includeAliasAsset = false;
    nullMaterialDependencies.textureTablePointer = 0;
    nullMaterialDependencies.constantTablePointer = 0;
    nullMaterialDependencies.stateBitsTablePointer = 0;
    Run(MakeMaterialXFile(nullMaterialDependencies), zone);
    const XAssetHeader nullDependencyMaterial = DB_FindXAssetHeader(
        ASSET_TYPE_MATERIAL, "materials/gate3");
    assert(nullDependencyMaterial.material);
    assert(!nullDependencyMaterial.material->textureTable &&
        !nullDependencyMaterial.material->constantTable &&
        !nullDependencyMaterial.material->stateBitsTable &&
        !nullDependencyMaterial.material->techniqueSet);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_IMAGE) == 2400);

    const std::vector<std::uint8_t> sndCurveInsertAlias =
        MakeSndCurveXFile();
    Run(sndCurveInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_SOUND_CURVE);
    assert(std::strcmp(g_trace.pointerClassification,
        "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32752 &&
        g_trace.freeEntryCountAfter == 32751);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "soundcurves/gate3", ASSET_TYPE_SOUND_CURVE));
    assert(g_trace.assetZoneIndex == 1);
    assert(g_trace.streamOffsets[0] == sizeof(SndCurve));
    assert(g_trace.streamOffsets[4] == 38);
    const XAssetHeader publishedSndCurve = DB_FindXAssetHeader(
        ASSET_TYPE_SOUND_CURVE, "soundcurves/gate3");
    assert(publishedSndCurve.sndCurve);
    assert(publishedSndCurve.sndCurve->knotCount == 3);
    assert(publishedSndCurve.sndCurve->knots[0][0] == 0.0f &&
        publishedSndCurve.sndCurve->knots[0][1] == 1.0f &&
        publishedSndCurve.sndCurve->knots[7][0] == 1.0f &&
        publishedSndCurve.sndCurve->knots[7][1] == 0.0f);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_SOUND_CURVE) == 63);
    const XAsset *sndCurveAssets = reinterpret_cast<const XAsset *>(
        zone.blocks[4].data);
    assert(sndCurveAssets[0].header.sndCurve == publishedSndCurve.sndCurve);
    assert(sndCurveAssets[1].header.sndCurve == publishedSndCurve.sndCurve);
    std::uint32_t sndCurveInsertion = 0;
    std::memcpy(&sndCurveInsertion, zone.blocks[4].data + 16,
        sizeof(sndCurveInsertion));
    assert(sndCurveInsertion == reinterpret_cast<std::uint32_t>(
        publishedSndCurve.sndCurve));

    SndCurveFixtureOptions sharedSndCurve{};
    sharedSndCurve.assetPointer = UINT32_MAX;
    sharedSndCurve.includeAliasAsset = false;
    Run(MakeSndCurveXFile(sharedSndCurve), zone);
    assert(std::strcmp(g_trace.pointerClassification,
        "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.assetEntryIndex == 16);
    assert(g_trace.streamOffsets[0] == sizeof(SndCurve) &&
        g_trace.streamOffsets[4] == 26);

    SndCurveFixtureOptions nullSndCurve{};
    nullSndCurve.assetPointer = 0;
    nullSndCurve.includeAliasAsset = false;
    Run(MakeSndCurveXFile(nullSndCurve), zone);
    assert(std::strcmp(g_trace.pointerClassification, "null") == 0);
    assert(!g_trace.publicationBegin && !g_trace.publicationEnd);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_SOUND_CURVE) == 64);

    SndCurveFixtureOptions malformedSndCurve{};
    malformedSndCurve.assetPointer = UINT32_MAX - 2u;
    malformedSndCurve.includeAliasAsset = false;
    Run(MakeSndCurveXFile(malformedSndCurve), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    SndCurveFixtureOptions malformedSndCurveName{};
    malformedSndCurveName.assetPointer = UINT32_MAX;
    malformedSndCurveName.includeAliasAsset = false;
    malformedSndCurveName.filenamePointer = UINT32_MAX - 1u;
    Run(MakeSndCurveXFile(malformedSndCurveName), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "stream/invalid pointer offset") == 0);

    SndCurveFixtureOptions truncatedSndCurveBody{};
    truncatedSndCurveBody.assetPointer = UINT32_MAX;
    truncatedSndCurveBody.includeAliasAsset = false;
    truncatedSndCurveBody.includeBody = false;
    Run(MakeSndCurveXFile(truncatedSndCurveBody), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    SndCurveFixtureOptions truncatedSndCurveName{};
    truncatedSndCurveName.assetPointer = UINT32_MAX;
    truncatedSndCurveName.includeAliasAsset = false;
    truncatedSndCurveName.terminateFilename = false;
    Run(MakeSndCurveXFile(truncatedSndCurveName), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0 ||
        std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    const std::vector<std::uint8_t> soundAliasInsertAlias =
        MakeSoundAliasXFile();
    Run(soundAliasInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_SOUND);
    assert(std::strcmp(g_trace.pointerClassification,
        "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    assert(g_trace.streamOffsets[0] == sizeof(snd_alias_list_t));
    assert(g_trace.streamOffsets[4] == 586);
    const XAssetHeader publishedSound = DB_FindXAssetHeader(
        ASSET_TYPE_SOUND, "sound/gate3");
    assert(publishedSound.sound && publishedSound.sound->count == 1);
    assert(std::strcmp(publishedSound.sound->head[0].aliasName,
        "sound/gate3") == 0);
    assert(publishedSound.sound->head[0].soundFile &&
        publishedSound.sound->head[0].soundFile->type == 0);
    assert(std::strcmp(publishedSound.sound->head[0].soundFile->u.streamSnd
        .filename.info.raw.dir, "sound") == 0);
    assert(std::strcmp(publishedSound.sound->head[0].soundFile->u.streamSnd
        .filename.info.raw.name, "gate3.wav") == 0);
    assert(!publishedSound.sound->head[0].volumeFalloffCurve);
    assert(publishedSound.sound->head[0].speakerMap &&
        std::strcmp(publishedSound.sound->head[0].speakerMap->name,
            "speaker/gate3") == 0);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_SOUND) == 15999);

    SoundAliasFixtureOptions sharedSoundAlias{};
    sharedSoundAlias.assetPointer = UINT32_MAX;
    sharedSoundAlias.includeAliasAsset = false;
    Run(MakeSoundAliasXFile(sharedSoundAlias), zone);
    assert(std::strcmp(g_trace.pointerClassification,
        "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.streamOffsets[4] == 574);

    SoundAliasFixtureOptions nullSoundAlias{};
    nullSoundAlias.assetPointer = 0;
    nullSoundAlias.includeAliasAsset = false;
    Run(MakeSoundAliasXFile(nullSoundAlias), zone);
    assert(!g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_SOUND) == 16000);

    SoundAliasFixtureOptions malformedSoundAlias{};
    malformedSoundAlias.assetPointer = UINT32_MAX - 2u;
    malformedSoundAlias.includeAliasAsset = false;
    Run(MakeSoundAliasXFile(malformedSoundAlias), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    SoundAliasFixtureOptions invalidSoundAliasCount{};
    invalidSoundAliasCount.assetPointer = UINT32_MAX;
    invalidSoundAliasCount.includeAliasAsset = false;
    invalidSoundAliasCount.aliasCount = -1;
    Run(MakeSoundAliasXFile(invalidSoundAliasCount), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "SoundAlias/alias array") == 0);

    SoundAliasFixtureOptions truncatedSoundAlias{};
    truncatedSoundAlias.assetPointer = UINT32_MAX;
    truncatedSoundAlias.includeAliasAsset = false;
    truncatedSoundAlias.terminateListName = false;
    Run(MakeSoundAliasXFile(truncatedSoundAlias), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0 ||
        std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    const std::vector<std::uint8_t> loadedSoundInsertAlias =
        MakeLoadedSoundXFile();
    Run(loadedSoundInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_LOADED_SOUND);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.streamOffsets[0] == 48 && g_trace.streamOffsets[4] == 44);
    const XAssetHeader publishedLoadedSound = DB_FindXAssetHeader(
        ASSET_TYPE_LOADED_SOUND, "loaded/gate3.wav");
    assert(publishedLoadedSound.loadSnd);
    assert(publishedLoadedSound.loadSnd->sound.info.data_len == 4);
    assert(publishedLoadedSound.loadSnd->sound.data[0] == 1 &&
        publishedLoadedSound.loadSnd->sound.data[3] == 4);
    assert(publishedLoadedSound.loadSnd->sound.info.data_ptr ==
        publishedLoadedSound.loadSnd->sound.data);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_LOADED_SOUND) == 1199);

    LoadedSoundFixtureOptions truncatedLoadedSound{};
    truncatedLoadedSound.assetPointer = UINT32_MAX;
    truncatedLoadedSound.includeAliasAsset = false;
    truncatedLoadedSound.includeData = false;
    Run(MakeLoadedSoundXFile(truncatedLoadedSound), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "LoadedSound/data array") == 0 ||
        std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    const std::vector<std::uint8_t> fontInsertAlias = MakeFontXFile();
    Run(fontInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_FONT);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.streamOffsets[0] == sizeof(Font_s));
    assert(g_trace.streamOffsets[4] == 80);
    const XAssetHeader publishedFont = DB_FindXAssetHeader(
        ASSET_TYPE_FONT, "fonts/gate3");
    assert(publishedFont.font && publishedFont.font->glyphCount == 2);
    assert(publishedFont.font->glyphs && !publishedFont.font->material &&
        !publishedFont.font->glowMaterial);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_FONT) == 15);

    FontFixtureOptions sharedFont{};
    sharedFont.assetPointer = UINT32_MAX;
    sharedFont.includeAliasAsset = false;
    Run(MakeFontXFile(sharedFont), zone);
    assert(std::strcmp(g_trace.pointerClassification,
        "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.streamOffsets[4] == 68);

    FontFixtureOptions nullFont{};
    nullFont.assetPointer = 0;
    nullFont.includeAliasAsset = false;
    Run(MakeFontXFile(nullFont), zone);
    assert(!g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_FONT) == 16);

    FontFixtureOptions malformedFont{};
    malformedFont.assetPointer = UINT32_MAX - 2u;
    malformedFont.includeAliasAsset = false;
    Run(MakeFontXFile(malformedFont), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    FontFixtureOptions invalidFontCount{};
    invalidFontCount.assetPointer = UINT32_MAX;
    invalidFontCount.includeAliasAsset = false;
    invalidFontCount.glyphCount = -1;
    Run(MakeFontXFile(invalidFontCount), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "Font/glyph array") == 0);

    FontFixtureOptions truncatedFont{};
    truncatedFont.assetPointer = UINT32_MAX;
    truncatedFont.includeAliasAsset = false;
    truncatedFont.includeGlyphs = false;
    Run(MakeFontXFile(truncatedFont), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "Font/glyph array") == 0 ||
        std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    const std::vector<std::uint8_t> fxInsertAlias = MakeFxXFile();
    Run(fxInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_FX);
    assert(std::strcmp(g_trace.pointerClassification,
        "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32752 &&
        g_trace.freeEntryCountAfter == 32751);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "fx/gate3", ASSET_TYPE_FX));
    assert(g_trace.assetZoneIndex == 1);
    assert(g_trace.streamOffsets[0] == sizeof(FxEffectDef));
    assert(g_trace.streamOffsets[4] == 502);
    const XAssetHeader publishedFx = DB_FindXAssetHeader(
        ASSET_TYPE_FX, "fx/gate3");
    assert(publishedFx.fx && publishedFx.fx->flags == 0x1234);
    assert(publishedFx.fx->elemDefCountOneShot == 1 &&
        publishedFx.fx->elemDefs);
    const FxElemDef &publishedElement = publishedFx.fx->elemDefs[0];
    assert(publishedElement.elemType == 6 && publishedElement.visualCount == 0);
    assert(publishedElement.velSamples && publishedElement.visSamples);
    assert(publishedElement.trailDef &&
        publishedElement.trailDef->vertCount == 2 &&
        publishedElement.trailDef->indCount == 3);
    assert(publishedElement.trailDef->verts &&
        publishedElement.trailDef->inds &&
        publishedElement.trailDef->inds[2] == 2);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_FX) == 399);
    const XAsset *fxAssets = reinterpret_cast<const XAsset *>(
        zone.blocks[4].data);
    assert(fxAssets[0].header.fx == publishedFx.fx &&
        fxAssets[1].header.fx == publishedFx.fx);
    std::uint32_t fxInsertion = 0;
    std::memcpy(&fxInsertion, zone.blocks[4].data + 16,
        sizeof(fxInsertion));
    assert(fxInsertion == reinterpret_cast<std::uint32_t>(publishedFx.fx));

    FxFixtureOptions sharedFx{};
    sharedFx.assetPointer = UINT32_MAX;
    sharedFx.includeAliasAsset = false;
    sharedFx.includeSamples = false;
    sharedFx.includeTrail = false;
    Run(MakeFxXFile(sharedFx), zone);
    assert(std::strcmp(g_trace.pointerClassification,
        "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.streamOffsets[0] == 32 &&
        g_trace.streamOffsets[4] == 272);
    assert(DB_FindXAssetHeader(ASSET_TYPE_FX, "fx/gate3").fx);

    FxFixtureOptions soundVisualFx{};
    soundVisualFx.assetPointer = UINT32_MAX;
    soundVisualFx.includeAliasAsset = false;
    soundVisualFx.elementType = 8;
    soundVisualFx.visualCount = 1;
    soundVisualFx.visualPointer = UINT32_MAX;
    soundVisualFx.includeTrail = false;
    Run(MakeFxXFile(soundVisualFx), zone);
    const XAssetHeader publishedSoundVisualFx = DB_FindXAssetHeader(
        ASSET_TYPE_FX, "fx/gate3");
    assert(publishedSoundVisualFx.fx &&
        std::strcmp(publishedSoundVisualFx.fx->elemDefs[0]
            .visuals.instance.soundName, "sound/fx_gate3") == 0);

    FxFixtureOptions nullFx{};
    nullFx.assetPointer = 0;
    nullFx.includeAliasAsset = false;
    Run(MakeFxXFile(nullFx), zone);
    assert(!g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_FX) == 400);

    FxFixtureOptions malformedFx{};
    malformedFx.assetPointer = UINT32_MAX - 2u;
    malformedFx.includeAliasAsset = false;
    Run(MakeFxXFile(malformedFx), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    FxFixtureOptions invalidFxCount{};
    invalidFxCount.assetPointer = UINT32_MAX;
    invalidFxCount.includeAliasAsset = false;
    invalidFxCount.elementCount = -1;
    Run(MakeFxXFile(invalidFxCount), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "FX/element array") == 0);

    FxFixtureOptions truncatedFx{};
    truncatedFx.assetPointer = UINT32_MAX;
    truncatedFx.includeAliasAsset = false;
    truncatedFx.includeBody = false;
    Run(MakeFxXFile(truncatedFx), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    FxFixtureOptions truncatedFxTrail{};
    truncatedFxTrail.assetPointer = UINT32_MAX;
    truncatedFxTrail.includeAliasAsset = false;
    truncatedFxTrail.includeTrailData = false;
    Run(MakeFxXFile(truncatedFxTrail), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "FX/trail vertices") == 0 ||
        std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    FxFixtureOptions inlineModelFx{};
    inlineModelFx.elementType = 5;
    inlineModelFx.visualCount = 1;
    inlineModelFx.visualPointer = UINT32_MAX;
    inlineModelFx.includeTrail = false;
    Run(MakeFxXFile(inlineModelFx), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "Load_XModelPtr/unsupported inline body closure") == 0);
    std::uint32_t failedFxModelInsertion = UINT32_MAX;
    std::memcpy(&failedFxModelInsertion, zone.blocks[4].data + 16,
        sizeof(failedFxModelInsertion));
    assert(failedFxModelInsertion == 0);

    Run(MakeSndDriverGlobalsXFile(), zone);
    assert(!g_trace.generatedLoadFailed && g_trace.xassetListEnd);
    assert(g_trace.assetType == ASSET_TYPE_SNDDRIVER_GLOBALS);
    assert(std::strcmp(g_trace.pointerClassification,
        "prior-offset/alias") == 0);
    assert(!g_trace.publicationBegin && !g_trace.publicationEnd);
    assert(g_trace.streamOffsets[0] == 0 && g_trace.streamOffsets[4] == 8);

    const std::vector<std::uint8_t> localizeInsertAlias =
        MakeLocalizeXFile();
    Run(localizeInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_LOCALIZE_ENTRY);
    assert(std::strcmp(g_trace.pointerClassification,
        "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32752 &&
        g_trace.freeEntryCountAfter == 32751);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "LOCALIZE_GATE3", ASSET_TYPE_LOCALIZE_ENTRY));
    assert(g_trace.assetZoneIndex == 1);
    assert(g_trace.streamOffsets[0] == sizeof(LocalizeEntry));
    assert(g_trace.streamOffsets[4] == 51);
    const XAssetHeader publishedLocalize = DB_FindXAssetHeader(
        ASSET_TYPE_LOCALIZE_ENTRY, "LOCALIZE_GATE3");
    assert(publishedLocalize.localize);
    assert(std::strcmp(publishedLocalize.localize->value,
        "Localized gate3") == 0);
    assert(std::strcmp(publishedLocalize.localize->name,
        "LOCALIZE_GATE3") == 0);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_LOCALIZE_ENTRY) == 6143);
    const XAsset *localizeAssets = reinterpret_cast<const XAsset *>(
        zone.blocks[4].data);
    assert(localizeAssets[0].header.localize == publishedLocalize.localize);
    assert(localizeAssets[1].header.localize == publishedLocalize.localize);
    std::uint32_t localizeInsertion = 0;
    std::memcpy(&localizeInsertion, zone.blocks[4].data + 16,
        sizeof(localizeInsertion));
    assert(localizeInsertion == reinterpret_cast<std::uint32_t>(
        publishedLocalize.localize));

    LocalizeFixtureOptions sharedLocalizeOptions{};
    sharedLocalizeOptions.assetPointer = UINT32_MAX;
    sharedLocalizeOptions.includeAliasAsset = false;
    Run(MakeLocalizeXFile(sharedLocalizeOptions), zone);
    assert(std::strcmp(g_trace.pointerClassification,
        "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.assetEntryIndex == 16);
    assert(g_trace.streamOffsets[0] == sizeof(LocalizeEntry) &&
        g_trace.streamOffsets[4] == 39);

    LocalizeFixtureOptions directLocalizeOptions{};
    directLocalizeOptions.assetPointer = UINT32_MAX;
    directLocalizeOptions.includeAliasAsset = false;
    directLocalizeOptions.directNameToValueInterior = true;
    Run(MakeLocalizeXFile(directLocalizeOptions), zone);
    const XAssetHeader directLocalize = DB_FindXAssetHeader(
        ASSET_TYPE_LOCALIZE_ENTRY, "gate3");
    assert(directLocalize.localize);
    assert(directLocalize.localize->name ==
        directLocalize.localize->value + 10);
    assert(g_trace.streamOffsets[4] == 24);

    LocalizeFixtureOptions nullLocalizeValue{};
    nullLocalizeValue.assetPointer = UINT32_MAX;
    nullLocalizeValue.includeAliasAsset = false;
    nullLocalizeValue.valuePointer = 0;
    Run(MakeLocalizeXFile(nullLocalizeValue), zone);
    const XAssetHeader nullValueLocalize = DB_FindXAssetHeader(
        ASSET_TYPE_LOCALIZE_ENTRY, "LOCALIZE_GATE3");
    assert(nullValueLocalize.localize && !nullValueLocalize.localize->value);

    LocalizeFixtureOptions nullLocalizeAsset{};
    nullLocalizeAsset.assetPointer = 0;
    nullLocalizeAsset.includeAliasAsset = false;
    Run(MakeLocalizeXFile(nullLocalizeAsset), zone);
    assert(std::strcmp(g_trace.pointerClassification, "null") == 0);
    assert(!g_trace.publicationBegin && !g_trace.publicationEnd);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_LOCALIZE_ENTRY) == 6144);

    LocalizeFixtureOptions invalidLocalizeAsset{};
    invalidLocalizeAsset.assetPointer = UINT32_MAX - 2u;
    invalidLocalizeAsset.includeAliasAsset = false;
    Run(MakeLocalizeXFile(invalidLocalizeAsset), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    LocalizeFixtureOptions invalidLocalizeString{};
    invalidLocalizeString.assetPointer = UINT32_MAX;
    invalidLocalizeString.includeAliasAsset = false;
    invalidLocalizeString.valuePointer = 0;
    invalidLocalizeString.namePointer = UINT32_MAX - 1u;
    Run(MakeLocalizeXFile(invalidLocalizeString), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid pointer offset") == 0);
    assert(DB_GetFreeAssetEntryCount() == 32752);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_LOCALIZE_ENTRY) == 6144);

    LocalizeFixtureOptions nullLocalizeName{};
    nullLocalizeName.assetPointer = UINT32_MAX;
    nullLocalizeName.includeAliasAsset = false;
    nullLocalizeName.valuePointer = 0;
    nullLocalizeName.namePointer = 0;
    Run(MakeLocalizeXFile(nullLocalizeName), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/unsupported or unnamed asset") == 0);

    LocalizeFixtureOptions truncatedLocalize{};
    truncatedLocalize.assetPointer = UINT32_MAX;
    truncatedLocalize.includeAliasAsset = false;
    truncatedLocalize.valuePointer = 0;
    truncatedLocalize.terminateName = false;
    Run(MakeLocalizeXFile(truncatedLocalize), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0 ||
        std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    LocalizeFixtureOptions excessiveLocalize{};
    excessiveLocalize.assetPointer = UINT32_MAX;
    excessiveLocalize.includeAliasAsset = false;
    excessiveLocalize.valuePointer = 0;
    excessiveLocalize.nameLength = 4088;
    excessiveLocalize.terminateName = false;
    Run(MakeLocalizeXFile(excessiveLocalize), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    MaterialFixtureOptions nullMaterialOptions{};
    nullMaterialOptions.assetPointer = 0;
    nullMaterialOptions.includeAliasAsset = false;
    Run(MakeMaterialXFile(nullMaterialOptions), zone);
    assert(std::strcmp(g_trace.pointerClassification, "null") == 0);
    assert(!g_trace.publicationBegin && !g_trace.publicationEnd);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_MATERIAL) == 2048);

    MaterialFixtureOptions invalidMaterialAsset{};
    invalidMaterialAsset.assetPointer = UINT32_MAX - 2u;
    invalidMaterialAsset.includeAliasAsset = false;
    Run(MakeMaterialXFile(invalidMaterialAsset), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    MaterialFixtureOptions invalidWaterPointer{};
    invalidWaterPointer.includeAliasAsset = false;
    invalidWaterPointer.waterPointer = UINT32_MAX - 1u;
    Run(MakeMaterialXFile(invalidWaterPointer), zone);
    assert(g_trace.generatedLoadFailed);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_MATERIAL,
        "materials/gate3").material);
    assert(DB_FindXAssetHeader(ASSET_TYPE_IMAGE, "images/gate3").image);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid pointer offset") == 0);

    MaterialFixtureOptions invalidStatePointer{};
    invalidStatePointer.includeAliasAsset = false;
    invalidStatePointer.stateBitsTablePointer = UINT32_MAX - 1u;
    Run(MakeMaterialXFile(invalidStatePointer), zone);
    assert(g_trace.generatedLoadFailed);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_MATERIAL,
        "materials/gate3").material);
    assert(DB_FindXAssetHeader(ASSET_TYPE_IMAGE, "images/gate3").image);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid pointer offset") == 0);

    MaterialFixtureOptions excessiveWater{};
    excessiveWater.includeAliasAsset = false;
    excessiveWater.waterM = (std::numeric_limits<std::int32_t>::max)();
    excessiveWater.waterN = 2;
    Run(MakeMaterialXFile(excessiveWater), zone);
    assert(g_trace.generatedLoadFailed);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_MATERIAL,
        "materials/gate3").material);
    assert(std::strcmp(g_trace.stopStage, "water/H0 array") == 0);

    MaterialFixtureOptions truncatedImage{};
    truncatedImage.includeAliasAsset = false;
    truncatedImage.imageResourceSize = 8192;
    Run(MakeMaterialXFile(truncatedImage), zone);
    assert(g_trace.generatedLoadFailed);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_IMAGE, "images/gate3").image);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_MATERIAL,
        "materials/gate3").material);
    assert(std::strcmp(g_trace.stopStage,
        "GfxImageLoadDef/resource size") == 0);

    TechniqueSetFixtureOptions nullTechniqueOptions{};
    nullTechniqueOptions.assetPointer = 0;
    nullTechniqueOptions.includeAliasAsset = false;
    Run(MakeTechniqueSetXFile(nullTechniqueOptions), zone);
    assert(std::strcmp(g_trace.pointerClassification, "null") == 0);
    assert(!g_trace.publicationBegin && !g_trace.publicationEnd);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_TECHNIQUE_SET) == 1024);

    TechniqueSetFixtureOptions invalidTechniqueAssetOptions{};
    invalidTechniqueAssetOptions.assetPointer = UINT32_MAX - 2u;
    invalidTechniqueAssetOptions.includeAliasAsset = false;
    Run(MakeTechniqueSetXFile(invalidTechniqueAssetOptions), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    TechniqueSetFixtureOptions invalidTechniqueChildOptions{};
    invalidTechniqueChildOptions.techniquePointer = UINT32_MAX - 1u;
    invalidTechniqueChildOptions.includeAliasAsset = false;
    invalidTechniqueChildOptions.includeTechniqueAlias = false;
    Run(MakeTechniqueSetXFile(invalidTechniqueChildOptions), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid pointer offset") == 0);

    TechniqueSetFixtureOptions excessivePassOptions{};
    excessivePassOptions.includeAliasAsset = false;
    excessivePassOptions.includeTechniqueAlias = false;
    excessivePassOptions.passCount = 1000;
    Run(MakeTechniqueSetXFile(excessivePassOptions), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "MaterialTechnique/pass array") == 0);

    TechniqueSetFixtureOptions excessiveProgramOptions{};
    excessiveProgramOptions.includeAliasAsset = false;
    excessiveProgramOptions.includeTechniqueAlias = false;
    excessiveProgramOptions.vertexProgramSize = 1024;
    Run(MakeTechniqueSetXFile(excessiveProgramOptions), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "MaterialShader/program array") == 0);

    TechniqueSetFixtureOptions truncatedTechniqueOptions{};
    truncatedTechniqueOptions.includeAliasAsset = false;
    truncatedTechniqueOptions.includeTechniqueAlias = false;
    truncatedTechniqueOptions.terminateTechniqueName = false;
    Run(MakeTechniqueSetXFile(truncatedTechniqueOptions), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0 ||
        std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    Run(MakePhysPresetXFile(0, 0, 0, false, false), zone);
    assert(std::strcmp(g_trace.pointerClassification, "null") == 0);
    assert(!g_trace.publicationBegin && !g_trace.publicationEnd);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_PHYSPRESET) == 64);

    Run(MakePhysPresetXFile(UINT32_MAX - 2u, 0, 0, false, false), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    Run(MakePhysPresetXFile(UINT32_MAX - 1u, UINT32_MAX, 0x4000000du,
        false, true, false), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0 ||
        std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    Run(MakePhysPresetXFile(UINT32_MAX - 1u, UINT32_MAX, 0x4000000du,
        false, true, false, 4084), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    Reset(physInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_PHYSPRESET]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage, "publication/asset pool exhaustion") == 0);
    assert(DB_FindXAssetEntryCanonical(
        ASSET_TYPE_PHYSPRESET, "physics/gate3") == nullptr);

    Reset(techniqueInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_TECHNIQUE_SET]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(DB_FindXAssetEntryCanonical(ASSET_TYPE_TECHNIQUE_SET,
        "techsets/gate3") == nullptr);
    std::uint32_t failedInsertion = UINT32_MAX;
    std::memcpy(&failedInsertion, zone.blocks[4].data + 16,
        sizeof(failedInsertion));
    assert(failedInsertion == 0);

    Reset(techniqueInsertAlias);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(DB_FindXAssetEntryCanonical(ASSET_TYPE_TECHNIQUE_SET,
        "techsets/gate3") == nullptr);
    std::memcpy(&failedInsertion, zone.blocks[4].data + 16,
        sizeof(failedInsertion));
    assert(failedInsertion == 0);

    Reset(materialInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_IMAGE]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_IMAGE, "images/gate3").image);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_MATERIAL,
        "materials/gate3").material);
    std::uint32_t failedImageInsertion = UINT32_MAX;
    std::memcpy(&failedInsertion, zone.blocks[4].data + 16,
        sizeof(failedInsertion));
    std::memcpy(&failedImageInsertion, zone.blocks[4].data + 60,
        sizeof(failedImageInsertion));
    assert(failedInsertion == 0 && failedImageInsertion == 0);

    Reset(materialInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_MATERIAL]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(DB_FindXAssetHeader(ASSET_TYPE_IMAGE, "images/gate3").image);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_MATERIAL,
        "materials/gate3").material);
    std::memcpy(&failedInsertion, zone.blocks[4].data + 16,
        sizeof(failedInsertion));
    assert(failedInsertion == 0);

    const std::uint32_t localizeHash = DB_HashForNameCanonical(
        "LOCALIZE_GATE3", ASSET_TYPE_LOCALIZE_ENTRY);
    Reset(localizeInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_LOCALIZE_ENTRY]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[localizeHash] == 0);
    assert(DB_GetFreeAssetEntryCount() == 32752);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_LOCALIZE_ENTRY,
        "LOCALIZE_GATE3").localize);
    std::uint32_t failedLocalizeInsertion = UINT32_MAX;
    std::memcpy(&failedLocalizeInsertion, zone.blocks[4].data + 16,
        sizeof(failedLocalizeInsertion));
    assert(failedLocalizeInsertion == 0);

    Reset(localizeInsertAlias);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(db_hashTable[localizeHash] == 0);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_LOCALIZE_ENTRY) == 6144);
    std::memcpy(&failedLocalizeInsertion, zone.blocks[4].data + 16,
        sizeof(failedLocalizeInsertion));
    assert(failedLocalizeInsertion == 0);

    const std::uint32_t sndCurveHash = DB_HashForNameCanonical(
        "soundcurves/gate3", ASSET_TYPE_SOUND_CURVE);
    Reset(sndCurveInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_SOUND_CURVE]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[sndCurveHash] == 0);
    assert(DB_GetFreeAssetEntryCount() == 32752);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_SOUND_CURVE,
        "soundcurves/gate3").sndCurve);
    std::uint32_t failedSndCurveInsertion = UINT32_MAX;
    std::memcpy(&failedSndCurveInsertion, zone.blocks[4].data + 16,
        sizeof(failedSndCurveInsertion));
    assert(failedSndCurveInsertion == 0);

    const std::uint32_t soundAliasHash = DB_HashForNameCanonical(
        "sound/gate3", ASSET_TYPE_SOUND);
    Reset(soundAliasInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_SOUND]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[soundAliasHash] == 0);
    assert(DB_GetFreeAssetEntryCount() == 32752);
    std::uint32_t failedSoundAliasInsertion = UINT32_MAX;
    std::memcpy(&failedSoundAliasInsertion, zone.blocks[4].data + 16,
        sizeof(failedSoundAliasInsertion));
    assert(failedSoundAliasInsertion == 0);

    Reset(soundAliasInsertAlias);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(db_hashTable[soundAliasHash] == 0);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_SOUND) == 16000);
    std::memcpy(&failedSoundAliasInsertion, zone.blocks[4].data + 16,
        sizeof(failedSoundAliasInsertion));
    assert(failedSoundAliasInsertion == 0);

    const std::uint32_t loadedSoundHash = DB_HashForNameCanonical(
        "loaded/gate3.wav", ASSET_TYPE_LOADED_SOUND);
    Reset(loadedSoundInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_LOADED_SOUND]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[loadedSoundHash] == 0);
    assert(DB_GetFreeAssetEntryCount() == 32752);
    std::uint32_t failedLoadedSoundInsertion = UINT32_MAX;
    std::memcpy(&failedLoadedSoundInsertion, zone.blocks[4].data + 16,
        sizeof(failedLoadedSoundInsertion));
    assert(failedLoadedSoundInsertion == 0);

    const std::uint32_t fontHash = DB_HashForNameCanonical(
        "fonts/gate3", ASSET_TYPE_FONT);
    Reset(fontInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_FONT]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[fontHash] == 0);
    assert(DB_GetFreeAssetEntryCount() == 32752);
    std::uint32_t failedFontInsertion = UINT32_MAX;
    std::memcpy(&failedFontInsertion, zone.blocks[4].data + 16,
        sizeof(failedFontInsertion));
    assert(failedFontInsertion == 0);

    Reset(fontInsertAlias);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(db_hashTable[fontHash] == 0);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_FONT) == 16);
    std::memcpy(&failedFontInsertion, zone.blocks[4].data + 16,
        sizeof(failedFontInsertion));
    assert(failedFontInsertion == 0);

    const std::uint32_t fxHash = DB_HashForNameCanonical(
        "fx/gate3", ASSET_TYPE_FX);
    Reset(fxInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_FX]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[fxHash] == 0);
    assert(DB_GetFreeAssetEntryCount() == 32752);
    std::uint32_t failedFxInsertion = UINT32_MAX;
    std::memcpy(&failedFxInsertion, zone.blocks[4].data + 16,
        sizeof(failedFxInsertion));
    assert(failedFxInsertion == 0);

    Reset(fxInsertAlias);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(db_hashTable[fxHash] == 0);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_FX) == 400);
    std::memcpy(&failedFxInsertion, zone.blocks[4].data + 16,
        sizeof(failedFxInsertion));
    assert(failedFxInsertion == 0);

    Reset(loadedSoundInsertAlias);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(db_hashTable[loadedSoundHash] == 0);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_LOADED_SOUND) == 1200);
    std::memcpy(&failedLoadedSoundInsertion, zone.blocks[4].data + 16,
        sizeof(failedLoadedSoundInsertion));
    assert(failedLoadedSoundInsertion == 0);

    Reset(sndCurveInsertAlias);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(db_hashTable[sndCurveHash] == 0);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_SOUND_CURVE) == 64);
    std::memcpy(&failedSndCurveInsertion, zone.blocks[4].data + 16,
        sizeof(failedSndCurveInsertion));
    assert(failedSndCurveInsertion == 0);

    Reset(materialInsertAlias);
    g_assetEntryPool[16].next = nullptr;
    g_freeAssetEntryHead = &g_assetEntryPool[16];
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(DB_FindXAssetHeader(ASSET_TYPE_IMAGE, "images/gate3").image);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_MATERIAL,
        "materials/gate3").material);
    std::memcpy(&failedInsertion, zone.blocks[4].data + 16,
        sizeof(failedInsertion));
    assert(failedInsertion == 0);

    Reset(physInsertAlias);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage, "publication/asset entry exhaustion") == 0);
    assert(DB_FindXAssetEntryCanonical(
        ASSET_TYPE_PHYSPRESET, "physics/gate3") == nullptr);

    Reset(generated);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_RAWFILE]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin && !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage, "publication/asset pool exhaustion") == 0);
    assert(DB_FindXAssetEntryCanonical(ASSET_TYPE_RAWFILE,
        "tests/gate3_first.txt") == nullptr);

    Reset(generated);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin && !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage, "publication/asset entry exhaustion") == 0);
    assert(DB_FindXAssetEntryCanonical(ASSET_TYPE_RAWFILE,
        "tests/gate3_first.txt") == nullptr);

    const std::array<std::uint32_t, 9> oversized{0x08000001u, 0, 0, 0, 0, 0, 0, 0, 0};
    Run(MakeEmptyXFile(oversized), zone);
    assert(std::strcmp(g_trace.stopStage, "XFile/block allocation exhaustion") == 0);
    assert(g_trace.blockAllocationCount == 0 && !g_trace.streamInitialized);

    const std::array<std::uint32_t, 9> overflowing{0xfffffff8u, 0, 0, 0, 0, 0, 0, 0, 0};
    Run(MakeEmptyXFile(overflowing), zone);
    assert(std::strcmp(g_trace.stopStage, "XFile/block allocation exhaustion") == 0);
    assert(g_trace.blockAllocationCount == 0 && !g_trace.streamInitialized);

    Run(generated, zone);
    assert(g_trace.publicationEnd && g_trace.cleanupComplete);
    assert(std::strcmp(g_trace.stopStage, "Load_XAssetHeader/next-family-closure") == 0);

    std::printf("gate3-db-stream rawfile=published physpreset=published technique-set=published material=published image=published water=loaded sound-curve=published sound-alias=published loaded-sound=published font=published fx=published snddriver=canonical-noop localize=published insert=-2 alias=block4:16 technique=block4:36 direct-xstring=block4:18 technique-children=251 material-children=block0:136,block4:248 sound-curve-children=block0:72,block4:38 sound-alias-children=block0:12,block4:586 loaded-sound-children=block0:48,block4:44 font-children=block0:24,block4:80 fx-children=block0:32,block4:502 localize-children=block0:8,block4:51 image-entry=16 material-entry=17 sound-curve-entry=16 sound-alias-entry=16 loaded-sound-entry=16 font-entry=16 fx-entry=16 localize-entry=16 free=32752->32751 zone=1 xmodel-inline=blocked stop=next-family-closure\n");
    return 0;
}
