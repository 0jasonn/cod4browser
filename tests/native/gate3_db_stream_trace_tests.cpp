#include <database/database.h>
#include <database/db_registry_pools.h>
#include <database/db_registry_publication.h>
#include <database/db_runtime_prefix.h>
#include <database/localize_types.h>
#include <gfx_d3d/gfx_image_types.h>
#include <gfx_d3d/material_types.h>
#include <physics/phys_preset.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <script/scr_stringlist.h>
#include <universal/physicalmemory.h>
#include <web/web_database_filesystem.h>

#include <zlib/zlib.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
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

    std::printf("gate3-db-stream rawfile=published physpreset=published technique-set=published material=published image=published water=loaded localize=published insert=-2 alias=block4:16 technique=block4:36 direct-xstring=block4:18 technique-children=251 material-children=block0:136,block4:248 localize-children=block0:8,block4:51 image-entry=16 material-entry=17 localize-entry=16 free=32752->32751 zone=1 stop=next-family-closure\n");
    return 0;
}
