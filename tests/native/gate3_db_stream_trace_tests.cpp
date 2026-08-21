#include <universal/q_shared.h>
#include <database/database.h>
#include <database/db_generated_loaders.h>
#include <database/db_generated_image_platform.h>
#include <game/g_bsp.h>
#include <database/db_registry_pools.h>
#include <database/db_registry_publication.h>
#include <database/db_runtime_prefix.h>
#include <database/localize_types.h>
#include <bgame/weapon_types.h>
#include <EffectsCore/fx_types.h>
#include <gfx_d3d/gfx_image_types.h>
#include <gfx_d3d/gfx_light_types.h>
#include <gfx_d3d/gfx_world_types.h>
#include <gfx_d3d/material_types.h>
#include <gfx_d3d/r_font.h>
#include <physics/phys_preset.h>
#include <physics/phys_geom_types.h>
#include <qcommon/qcommon.h>
#include <qcommon/cm_types.h>
#include <qcommon/com_world_types.h>
#include <qcommon/system.h>
#include <script/scr_stringlist.h>
#include <sound/snd_alias_types.h>
#include <ui/ui_asset_types.h>
#include <xanim/xmodel_types.h>
#include <xanim/xanim_types.h>
#include <xanim/xsurface_types.h>
#include <universal/physicalmemory.h>
#include <universal/com_sndalias_curve.h>
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

dvar_t g_testUseFastFile{};
const dvar_t *useFastFile = &g_testUseFastFile;

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

template <typename T>
void AppendObject(std::vector<std::uint8_t> &bytes, const T &value)
{
    const auto *begin = reinterpret_cast<const std::uint8_t *>(&value);
    bytes.insert(bytes.end(), begin, begin + sizeof(value));
}

template <typename T>
T *PointerToken(std::uint32_t token)
{
    return reinterpret_cast<T *>(static_cast<std::uintptr_t>(token));
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

struct XModelFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    bool includeBody = true;
    bool includeName = true;
    bool terminateName = true;
    bool includeSurfaceVertices = true;
    bool includeSurfaceIndices = true;
};

std::vector<std::uint8_t> MakeXModelXFile(
    const XModelFixtureOptions &options = {})
{
    const bool inlineAsset = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const std::uint32_t assetCount = options.includeAliasAsset ? 2u : 1u;
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 32768);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        8192, 0, 0, 0, 16384, 0, 0, 4096, 4096}) AppendU32(inflated, size);

    AppendU32(inflated, 1);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, assetCount);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, UINT32_MAX);
    AppendCString(inflated, "tag_gate3");
    AppendU32(inflated, ASSET_TYPE_XMODEL);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_XMODEL);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset || !options.includeBody) return CompressXFile(inflated);

    XModel model{};
    model.name = PointerToken<const char>(UINT32_MAX);
    model.numBones = 1;
    model.numRootBones = 1;
    model.numsurfs = 1;
    model.boneNames = PointerToken<std::uint16_t>(UINT32_MAX);
    model.partClassification = PointerToken<std::uint8_t>(UINT32_MAX);
    model.baseMat = PointerToken<DObjAnimMat>(UINT32_MAX);
    model.surfs = PointerToken<XSurface>(1);
    model.materialHandles = PointerToken<Material *>(1);
    model.collSurfs = PointerToken<XModelCollSurf_s>(1);
    model.numCollSurfs = 1;
    model.boneInfo = PointerToken<XBoneInfo>(1);
    model.physPreset = PointerToken<PhysPreset>(UINT32_MAX - 1u);
    model.physGeoms = PointerToken<PhysGeomList>(UINT32_MAX);
    AppendObject(inflated, model);
    if (options.includeName)
    {
        inflated.insert(inflated.end(), {'x','m','o','d','e','l','/','g','a','t','e','3'});
        if (options.terminateName) inflated.push_back(0);
    }
    AppendU16(inflated, 0);
    inflated.push_back(7);
    DObjAnimMat baseMat{};
    baseMat.quat[3] = 1.0f;
    baseMat.transWeight = 2.0f;
    AppendObject(inflated, baseMat);

    XSurface surface{};
    surface.vertCount = 1;
    surface.triCount = 1;
    surface.vertInfo.vertCount[0] = 1;
    surface.vertInfo.vertsBlend = PointerToken<std::uint16_t>(UINT32_MAX);
    surface.verts0 = PointerToken<GfxPackedVertex>(UINT32_MAX);
    surface.vertListCount = 1;
    surface.vertList = PointerToken<XRigidVertList>(UINT32_MAX);
    surface.triIndices = PointerToken<std::uint16_t>(UINT32_MAX);
    AppendObject(inflated, surface);
    AppendU16(inflated, 0x1234u);
    if (options.includeSurfaceVertices)
    {
        GfxPackedVertex vertex{};
        vertex.xyz[0] = 1.0f;
        vertex.xyz[1] = 2.0f;
        vertex.xyz[2] = 3.0f;
        vertex.color.packed = 0xff804020u;
        AppendObject(inflated, vertex);
    }
    XRigidVertList rigid{};
    rigid.vertCount = 1;
    rigid.triCount = 1;
    rigid.collisionTree = PointerToken<XSurfaceCollisionTree>(UINT32_MAX);
    AppendObject(inflated, rigid);
    XSurfaceCollisionTree tree{};
    tree.nodeCount = 1;
    tree.nodes = PointerToken<XSurfaceCollisionNode>(1);
    tree.leafCount = 1;
    tree.leafs = PointerToken<XSurfaceCollisionLeaf>(1);
    AppendObject(inflated, tree);
    XSurfaceCollisionNode node{};
    node.childCount = 1;
    AppendObject(inflated, node);
    XSurfaceCollisionLeaf leaf{};
    leaf.triangleBeginIndex = 3;
    AppendObject(inflated, leaf);
    if (options.includeSurfaceIndices)
    {
        AppendU16(inflated, 0);
        AppendU16(inflated, 0);
        AppendU16(inflated, 0);
    }
    AppendU32(inflated, 0); // null material handle

    XModelCollSurf_s collSurf{};
    collSurf.collTris = PointerToken<XModelCollTri_s>(1);
    collSurf.numCollTris = 1;
    collSurf.boneIdx = 0;
    AppendObject(inflated, collSurf);
    XModelCollTri_s collTri{};
    collTri.plane[2] = 1.0f;
    AppendObject(inflated, collTri);
    XBoneInfo boneInfo{};
    boneInfo.radiusSquared = 4.0f;
    AppendObject(inflated, boneInfo);

    PhysPreset preset{};
    preset.name = PointerToken<const char>(UINT32_MAX);
    preset.type = 3;
    preset.mass = 5.0f;
    AppendObject(inflated, preset);
    AppendCString(inflated, "physics/xmodel_gate3");

    PhysGeomList geomList{};
    geomList.count = 1;
    geomList.geoms = PointerToken<PhysGeomInfo>(1);
    AppendObject(inflated, geomList);
    PhysGeomInfo geom{};
    geom.brush = PointerToken<BrushWrapper>(UINT32_MAX);
    geom.type = 12;
    AppendObject(inflated, geom);
    BrushWrapper brush{};
    brush.numsides = 1;
    brush.sides = PointerToken<cbrushside_t>(1);
    brush.baseAdjacentSide = PointerToken<std::uint8_t>(1);
    brush.totalEdgeCount = 1;
    brush.planes = PointerToken<cplane_s>(UINT32_MAX);
    AppendObject(inflated, brush);
    cbrushside_t side{};
    side.plane = PointerToken<cplane_s>(UINT32_MAX);
    side.materialNum = 9;
    AppendObject(inflated, side);
    cplane_s sidePlane{};
    sidePlane.normal[0] = 1.0f;
    AppendObject(inflated, sidePlane);
    inflated.push_back(0);
    cplane_s planes{};
    planes.normal[2] = 1.0f;
    AppendObject(inflated, planes);
    return CompressXFile(inflated);
}

struct WeaponFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    bool includeBody = true;
    bool includeXModelBody = true;
    bool terminateName = true;
    std::int32_t accuracyCount = 2;
};

std::vector<std::uint8_t> MakeWeaponXFile(
    const WeaponFixtureOptions &options = {})
{
    const bool inlineAsset = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const std::uint32_t assetCount = options.includeAliasAsset ? 2u : 1u;
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 32768);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        8192, 0, 0, 0, 16384, 0, 0, 0, 0}) AppendU32(inflated, size);
    AppendU32(inflated, 1);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, assetCount);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, UINT32_MAX);
    AppendCString(inflated, "tag_weapon");
    AppendU32(inflated, ASSET_TYPE_WEAPON);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_WEAPON);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset || !options.includeBody) return CompressXFile(inflated);

    WeaponDef weapon{};
    weapon.szInternalName = PointerToken<const char>(UINT32_MAX);
    // The ScriptStringList consumes block4:0..14, the asset array 16..31,
    // and the inserted Weapon cell 32..35, so its name starts at 36.
    weapon.szDisplayName = PointerToken<const char>(0x40000025u);
    weapon.gunXModel[0] = PointerToken<XModel>(UINT32_MAX - 1u);
    // The 13-byte Weapon name ends at 49; XModel insertion aligns to block4:52.
    weapon.handXModel = PointerToken<XModel>(0x40000035u);
    weapon.szXAnims[0] = PointerToken<const char>(UINT32_MAX);
    weapon.pickupSound = PointerToken<snd_alias_list_t>(UINT32_MAX);
    weapon.bounceSound = PointerToken<snd_alias_list_t *>(UINT32_MAX);
    weapon.accuracyGraphName[0] = PointerToken<const char>(UINT32_MAX);
    weapon.accuracyGraphKnots[0] = PointerToken<float[WEAP_ACCURACY_COUNT]>(
        UINT32_MAX);
    weapon.originalAccuracyGraphKnots[0] =
        PointerToken<float[WEAP_ACCURACY_COUNT]>(UINT32_MAX);
    weapon.accuracyGraphKnotCount[0] = options.accuracyCount;
    weapon.szScript = PointerToken<const char>(UINT32_MAX);
    AppendObject(inflated, weapon);
    inflated.insert(inflated.end(), {'w','e','a','p','o','n','/','g','a','t','e','3'});
    if (options.terminateName) inflated.push_back(0);

    if (options.includeXModelBody)
    {
        XModel model{};
        model.name = PointerToken<const char>(UINT32_MAX);
        AppendObject(inflated, model);
        AppendCString(inflated, "xmodel/weapon_gate3");
        AppendCString(inflated, "anim_gate3");
        AppendU32(inflated, UINT32_MAX);
        AppendCString(inflated, "sound/gate3_missing");
        AppendZeros(inflated, 29u * sizeof(snd_alias_list_t *));
        AppendCString(inflated, "accuracy/gate3");
        if (options.accuracyCount >= 0 && options.accuracyCount < 32)
        {
            for (std::int32_t set = 0; set < 2; ++set)
                for (std::int32_t index = 0;
                    index < options.accuracyCount * WEAP_ACCURACY_COUNT; ++index)
                    AppendF32(inflated, static_cast<float>(index + set * 10));
        }
        AppendCString(inflated, "scripts/gate3_weapon");
    }
    return CompressXFile(inflated);
}

struct XAnimFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    bool includePayload = true;
};

std::vector<std::uint8_t> MakeXAnimXFile(
    const XAnimFixtureOptions &options = {})
{
    const bool inlineAsset = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const std::uint32_t assetCount = options.includeAliasAsset ? 2u : 1u;
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 16384);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 0, 0, 0, 12288, 0, 0, 0, 0}) AppendU32(inflated, size);
    AppendU32(inflated, 1);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, assetCount);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, UINT32_MAX);
    AppendCString(inflated, "tag_xanim");
    AppendU32(inflated, ASSET_TYPE_XANIMPARTS);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_XANIMPARTS);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset || !options.includePayload) return CompressXFile(inflated);

    XAnimParts parts{};
    parts.name = PointerToken<const char>(UINT32_MAX);
    parts.numframes = 300;
    parts.boneCount[9] = 2;
    parts.names = PointerToken<std::uint16_t>(1);
    parts.notifyCount = 1;
    parts.notify = PointerToken<XAnimNotifyInfo>(1);
    parts.deltaPart = PointerToken<XAnimDeltaPart>(1);
    parts.dataByteCount = 1;
    parts.dataShortCount = 1;
    parts.dataIntCount = 1;
    parts.randomDataShortCount = 1;
    parts.randomDataByteCount = 1;
    parts.randomDataIntCount = 1;
    parts.indexCount = 2;
    parts.dataByte = PointerToken<std::uint8_t>(1);
    parts.dataShort = PointerToken<std::int16_t>(1);
    parts.dataInt = PointerToken<int>(1);
    parts.randomDataShort = PointerToken<std::int16_t>(1);
    parts.randomDataByte = PointerToken<std::uint8_t>(1);
    parts.randomDataInt = PointerToken<int>(1);
    parts.indices.data = PointerToken<void>(1);
    AppendObject(inflated, parts);
    AppendCString(inflated, "xanim/gate3");
    AppendU16(inflated, 0);
    AppendU16(inflated, 0);
    XAnimNotifyInfo notify{};
    notify.name = 0;
    notify.time = 0.5f;
    AppendObject(inflated, notify);
    XAnimDeltaPart delta{};
    delta.trans = PointerToken<XAnimPartTrans>(1);
    delta.quat = PointerToken<XAnimDeltaPartQuat>(1);
    AppendObject(inflated, delta);
    AppendU16(inflated, 1);
    inflated.push_back(0);
    inflated.push_back(0);
    for (int index = 0; index < 6; ++index) AppendF32(inflated, float(index));
    AppendU32(inflated, 1);
    AppendU16(inflated, 0);
    AppendU16(inflated, 1);
    for (int index = 0; index < 6; ++index) AppendU16(inflated, std::uint16_t(index));
    AppendU16(inflated, 1);
    AppendU16(inflated, 0);
    AppendU32(inflated, 1);
    AppendU16(inflated, 0);
    AppendU16(inflated, 1);
    for (int index = 0; index < 4; ++index) AppendU16(inflated, std::uint16_t(index + 10));
    inflated.push_back(0xaa);
    AppendU16(inflated, 0x1234);
    AppendU32(inflated, 0x55667788u);
    AppendU16(inflated, 0x2345);
    inflated.push_back(0xbb);
    AppendU32(inflated, 0x66778899u);
    AppendU16(inflated, 2);
    AppendU16(inflated, 3);
    return CompressXFile(inflated);
}

struct StringTableFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX;
    bool includeAliasAsset = true;
    bool includeBody = true;
    std::int32_t columns = 2;
    std::int32_t rows = 2;
};

std::vector<std::uint8_t> MakeStringTableXFile(
    const StringTableFixtureOptions &options = {})
{
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
    AppendU32(inflated, ASSET_TYPE_STRINGTABLE);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_STRINGTABLE);
        AppendU32(inflated, 0x40000011u);
    }
    if (options.assetPointer != UINT32_MAX || !options.includeBody)
        return CompressXFile(inflated);
    StringTable table{};
    table.name = PointerToken<const char>(UINT32_MAX);
    table.columnCount = options.columns;
    table.rowCount = options.rows;
    table.values = PointerToken<const char *>(1);
    AppendObject(inflated, table);
    AppendCString(inflated, "stringtable/gate3.csv");
    if (options.columns == 2 && options.rows == 2)
    {
        AppendU32(inflated, UINT32_MAX);
        AppendU32(inflated, UINT32_MAX);
        AppendU32(inflated, 0);
        AppendU32(inflated, 0x40000021u);
        AppendCString(inflated, "key");
        AppendCString(inflated, "value");
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
        const float x = knot < 2 ? static_cast<float>(knot) * 0.5f : 1.0f;
        AppendF32(inflated, x);
        AppendF32(inflated, 1.0f - x);
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
        AppendZeros(inflated, sizeof(FxElemVelStateSample));
        AppendZeros(inflated, sizeof(FxElemVisStateSample));
    }
    if (options.visualPointer == UINT32_MAX && options.elementType == 8)
        AppendCString(inflated, "sound/fx_gate3");
    if (!options.includeTrail) return CompressXFile(inflated);

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
        AppendZeros(inflated, static_cast<std::size_t>(options.trailVertCount) *
            sizeof(FxTrailVertex));
    }
    if (options.trailIndCount > 0 && options.trailIndCount < 64)
    {
        for (std::int32_t index = 0; index < options.trailIndCount; ++index)
            AppendU16(inflated, static_cast<std::uint16_t>(index));
    }
    return CompressXFile(inflated);
}

struct FxImpactFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    std::uint32_t tablePointer = 1;
    bool includeTable = true;
    std::uint32_t firstFxPointer = UINT32_MAX - 1u;
    bool includeFxBody = true;
    bool includeFxAlias = true;
    bool terminateName = true;
};

std::vector<std::uint8_t> MakeFxImpactXFile(
    const FxImpactFixtureOptions &options = {})
{
    constexpr const char *name = "impact/gate3";
    constexpr const char *fxName = "fx/impact_child";
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
    AppendU32(inflated, ASSET_TYPE_IMPACT_FX);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_IMPACT_FX);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset) return CompressXFile(inflated);

    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, options.tablePointer);
    inflated.insert(inflated.end(), name, name + std::strlen(name));
    if (options.terminateName) inflated.push_back(0);
    if (!options.terminateName || !options.tablePointer ||
        !options.includeTable) return CompressXFile(inflated);

    const std::size_t entryOffset = inflated.size();
    AppendZeros(inflated, 12u * sizeof(FxImpactEntry));
    WriteU32(inflated, entryOffset, options.firstFxPointer);
    if (options.firstFxPointer == UINT32_MAX - 1u && options.includeFxAlias)
    {
        std::uint32_t block4Offset = assetCount * sizeof(XAsset);
        if (options.assetPointer == UINT32_MAX - 1u)
            block4Offset = Align4(block4Offset) + 4u;
        block4Offset += static_cast<std::uint32_t>(std::strlen(name) + 1u);
        block4Offset = Align4(block4Offset) +
            12u * static_cast<std::uint32_t>(sizeof(FxImpactEntry));
        WriteU32(inflated, entryOffset + sizeof(FxEffectDef *),
            0x40000001u + block4Offset);
    }
    if ((options.firstFxPointer != UINT32_MAX &&
            options.firstFxPointer != UINT32_MAX - 1u) ||
        !options.includeFxBody) return CompressXFile(inflated);

    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 0);
    AppendU32(inflated, sizeof(FxEffectDef));
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendCString(inflated, fxName);
    return CompressXFile(inflated);
}

struct ComWorldFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    bool includeBody = true;
    bool terminateName = true;
    std::uint32_t primaryLightCount = 2;
    std::uint32_t primaryLightsPointer = 1;
    bool includeLights = true;
    std::uint32_t includedLightCount = 2;
    std::uint32_t inlineDefNameCount = 2;
};

std::vector<std::uint8_t> MakeComWorldXFile(
    const ComWorldFixtureOptions &options = {})
{
    constexpr const char *name = "maps/gate3.d3dbsp";
    constexpr const char *lightNames[] = {
        "light/gate3_0", "light/gate3_1",
    };
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
    AppendU32(inflated, ASSET_TYPE_COMWORLD);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_COMWORLD);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset || !options.includeBody) return CompressXFile(inflated);

    ComWorld world{};
    world.name = PointerToken<const char>(UINT32_MAX);
    world.isInUse = 1;
    world.primaryLightCount = options.primaryLightCount;
    world.primaryLights = PointerToken<ComPrimaryLight>(
        options.primaryLightsPointer);
    AppendObject(inflated, world);
    inflated.insert(inflated.end(), name, name + std::strlen(name));
    if (options.terminateName) inflated.push_back(0);
    if (!options.terminateName || !options.primaryLightsPointer ||
        !options.includeLights) return CompressXFile(inflated);

    for (std::uint32_t index = 0; index < options.includedLightCount; ++index)
    {
        ComPrimaryLight light{};
        light.type = static_cast<std::uint8_t>(index + 1u);
        light.canUseShadowMap = 1;
        light.exponent = static_cast<std::uint8_t>(index + 2u);
        light.color[0] = 0.25f + static_cast<float>(index);
        light.radius = 128.0f + static_cast<float>(index);
        light.defName = PointerToken<const char>(UINT32_MAX);
        AppendObject(inflated, light);
    }
    for (std::uint32_t index = 0; index < options.inlineDefNameCount; ++index)
        AppendCString(inflated, lightNames[index % std::size(lightNames)]);
    return CompressXFile(inflated);
}

struct GfxWorldFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    bool includeBody = true;
    bool terminateName = true;
    bool terminateBaseName = true;
    int indexCount = 3;
    std::uint32_t indexPointer = 1;
    std::uint32_t vertexCount = 3;
    std::uint32_t vertexPointer = 1;
    int surfaceCount = 1;
    std::uint32_t surfacePointer = 1;
    std::uint32_t includedIndexCount = 3;
    std::uint32_t includedVertexCount = 3;
    std::uint32_t includedSurfaceCount = 1;
    std::uint32_t primaryLightCount = 1;
    std::uint32_t sunPrimaryLightIndex = 0;
};

std::vector<std::uint8_t> MakeGfxWorldXFile(
    const GfxWorldFixtureOptions &options = {})
{
    constexpr const char *name = "maps/gfxworld_gate3.d3dbsp";
    constexpr const char *baseName = "gfxworld_gate3";
    const bool inlineAsset = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const std::uint32_t assetCount = options.includeAliasAsset ? 2u : 1u;
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 12288);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 4096, 0, 0, 4096, 0, 0, 0, 0}) AppendU32(inflated, size);
    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, assetCount);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_GFXWORLD);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_GFXWORLD);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset || !options.includeBody) return CompressXFile(inflated);

    GfxWorld world{};
    world.name = PointerToken<const char>(UINT32_MAX);
    world.baseName = PointerToken<const char>(UINT32_MAX);
    world.indexCount = options.indexCount;
    world.indices = PointerToken<std::uint16_t>(options.indexPointer);
    world.surfaceCount = options.surfaceCount;
    world.vertexCount = options.vertexCount;
    world.vd.vertices = PointerToken<GfxWorldVertex>(options.vertexPointer);
    world.primaryLightCount = options.primaryLightCount;
    world.sunPrimaryLightIndex = options.sunPrimaryLightIndex;
    world.lightGrid.rowAxis = 0;
    world.dpvs.staticSurfaceCount = options.surfaceCount > 0
        ? static_cast<std::uint32_t>(options.surfaceCount) : 0u;
    world.dpvs.staticSurfaceCountNoDecal = world.dpvs.staticSurfaceCount;
    world.dpvs.surfaces = PointerToken<GfxSurface>(options.surfacePointer);
    AppendObject(inflated, world);
    inflated.insert(inflated.end(), name, name + std::strlen(name));
    if (options.terminateName) inflated.push_back(0);
    if (!options.terminateName) return CompressXFile(inflated);
    inflated.insert(inflated.end(), baseName,
        baseName + std::strlen(baseName));
    if (options.terminateBaseName) inflated.push_back(0);
    if (!options.terminateBaseName) return CompressXFile(inflated);

    if (options.indexPointer)
    {
        constexpr std::uint16_t indices[] = {0, 1, 2};
        for (std::uint32_t index = 0; index < options.includedIndexCount;
            ++index) AppendU16(inflated, indices[index % std::size(indices)]);
    }
    if (options.vertexPointer)
    {
        constexpr float positions[3][3] = {
            {-1.0f, -1.0f, 0.0f},
            { 1.0f, -1.0f, 0.0f},
            { 0.0f,  1.0f, 0.0f},
        };
        for (std::uint32_t index = 0; index < options.includedVertexCount;
            ++index)
        {
            GfxWorldVertex vertex{};
            std::memcpy(vertex.xyz, positions[index % std::size(positions)],
                sizeof(vertex.xyz));
            vertex.color.packed = 0xffffffffu;
            AppendObject(inflated, vertex);
        }
    }
    if (options.surfacePointer)
    {
        for (std::uint32_t index = 0; index < options.includedSurfaceCount;
            ++index)
        {
            GfxSurface surface{};
            surface.tris.firstVertex = 0;
            surface.tris.vertexCount = 3;
            surface.tris.triCount = 1;
            surface.tris.baseIndex = 0;
            surface.bounds[0][0] = -1.0f;
            surface.bounds[0][1] = -1.0f;
            surface.bounds[1][0] = 1.0f;
            surface.bounds[1][1] = 1.0f;
            AppendObject(inflated, surface);
        }
    }
    return CompressXFile(inflated);
}

struct LightDefFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    bool includeBody = true;
    bool terminateName = true;
    std::uint32_t imagePointer = UINT32_MAX - 1u;
    bool includeImageBody = true;
    bool terminateImageName = true;
};

std::vector<std::uint8_t> MakeLightDefXFile(
    const LightDefFixtureOptions &options = {})
{
    constexpr const char *name = "lights/gate3";
    constexpr const char *imageName = "images/light_gate3";
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
    AppendU32(inflated, ASSET_TYPE_LIGHT_DEF);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_LIGHT_DEF);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineAsset || !options.includeBody) return CompressXFile(inflated);

    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, options.imagePointer);
    inflated.push_back(7);
    AppendZeros(inflated, 3);
    AppendU32(inflated, 42);
    inflated.insert(inflated.end(), name, name + std::strlen(name));
    if (options.terminateName) inflated.push_back(0);
    if (!options.terminateName ||
        (options.imagePointer != UINT32_MAX &&
            options.imagePointer != UINT32_MAX - 1u) ||
        !options.includeImageBody) return CompressXFile(inflated);

    const std::size_t imageBodyStart = inflated.size();
    AppendU32(inflated, MAPTYPE_2D);
    AppendU32(inflated, 0);
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
    AppendU32(inflated, UINT32_MAX);
    assert(inflated.size() - imageBodyStart == sizeof(GfxImage));
    inflated.insert(inflated.end(), imageName,
        imageName + std::strlen(imageName));
    if (options.terminateImageName) inflated.push_back(0);
    return CompressXFile(inflated);
}

struct MenuListFixtureOptions
{
    std::uint32_t assetPointer = UINT32_MAX - 1u;
    bool includeAliasAsset = true;
    bool includeListBody = true;
    bool terminateListName = true;
    std::int32_t menuCount = 2;
    std::uint32_t menusPointer = 1;
    std::uint32_t firstMenuPointer = UINT32_MAX - 1u;
    bool includeMenuBody = true;
    std::int32_t itemCount = 1;
    std::uint32_t itemsPointer = 1;
    bool includeItemBody = true;
    std::int32_t expressionCount = 2;
    bool includeExpressionBodies = true;
};

std::vector<std::uint8_t> MakeMenuListXFile(
    const MenuListFixtureOptions &options = {})
{
    constexpr const char *listName = "menus/gate3";
    constexpr const char *menuName = "menu/gate3";
    constexpr const char *itemText = "menu item";
    constexpr const char *keyAction = "key/action";
    constexpr const char *enableDvar = "enable_gate";
    constexpr const char *doubleClick = "double/click";
    constexpr const char *expressionString = "expr/string";
    const bool inlineList = options.assetPointer == UINT32_MAX ||
        options.assetPointer == UINT32_MAX - 1u;
    const bool inlineMenu = options.firstMenuPointer == UINT32_MAX ||
        options.firstMenuPointer == UINT32_MAX - 1u;
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
    AppendU32(inflated, ASSET_TYPE_MENULIST);
    AppendU32(inflated, options.assetPointer);
    if (options.includeAliasAsset)
    {
        AppendU32(inflated, ASSET_TYPE_MENULIST);
        AppendU32(inflated, 0x40000011u);
    }
    if (!inlineList || !options.includeListBody) return CompressXFile(inflated);

    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, static_cast<std::uint32_t>(options.menuCount));
    AppendU32(inflated, options.menusPointer);
    inflated.insert(inflated.end(), listName,
        listName + std::strlen(listName));
    if (options.terminateListName) inflated.push_back(0);
    if (!options.terminateListName || !options.menusPointer ||
        options.menuCount <= 0) return CompressXFile(inflated);

    const std::uint32_t rootInsertionBytes =
        options.assetPointer == UINT32_MAX - 1u ? 4u : 0u;
    const std::uint32_t menuTableOffset = Align4(assetCount * 8u +
        rootInsertionBytes + static_cast<std::uint32_t>(
            std::strlen(listName) + 1u));
    const std::uint32_t menuInsertionOffset = menuTableOffset +
        static_cast<std::uint32_t>(options.menuCount) * 4u;
    AppendU32(inflated, options.firstMenuPointer);
    for (std::int32_t index = 1; index < options.menuCount; ++index)
        AppendU32(inflated, 0x40000001u + menuInsertionOffset);
    if (!inlineMenu || !options.includeMenuBody) return CompressXFile(inflated);

    const std::size_t menuOffset = inflated.size();
    AppendZeros(inflated, sizeof(menuDef_t));
    WriteU32(inflated, menuOffset + offsetof(menuDef_t, window) +
        offsetof(windowDef_t, name), UINT32_MAX);
    WriteU32(inflated, menuOffset + offsetof(menuDef_t, window) +
        offsetof(windowDef_t, group), 0x40000001u + assetCount * 8u +
        rootInsertionBytes);
    WriteU32(inflated, menuOffset + offsetof(menuDef_t, itemCount),
        static_cast<std::uint32_t>(options.itemCount));
    WriteU32(inflated, menuOffset + offsetof(menuDef_t, items),
        options.itemsPointer);
    AppendCString(inflated, menuName);
    if (!options.itemsPointer || options.itemCount <= 0)
        return CompressXFile(inflated);

    for (std::int32_t index = 0; index < options.itemCount; ++index)
        AppendU32(inflated, 1);
    if (!options.includeItemBody) return CompressXFile(inflated);

    const std::size_t itemOffset = inflated.size();
    AppendZeros(inflated, sizeof(itemDef_s));
    WriteU32(inflated, itemOffset + offsetof(itemDef_s, window) +
        offsetof(windowDef_t, name), 0x40000001u + menuInsertionOffset + 4u);
    WriteU32(inflated, itemOffset + offsetof(itemDef_s, type), 6u);
    WriteU32(inflated, itemOffset + offsetof(itemDef_s, text), UINT32_MAX);
    WriteU32(inflated, itemOffset + offsetof(itemDef_s, onKey), 1u);
    WriteU32(inflated, itemOffset + offsetof(itemDef_s, enableDvar),
        UINT32_MAX);
    WriteU32(inflated, itemOffset + offsetof(itemDef_s, typeData), 1u);
    WriteU32(inflated, itemOffset + offsetof(itemDef_s, visibleExp) +
        offsetof(statement_s, numEntries),
        static_cast<std::uint32_t>(options.expressionCount));
    WriteU32(inflated, itemOffset + offsetof(itemDef_s, visibleExp) +
        offsetof(statement_s, entries), 1u);
    AppendCString(inflated, itemText);

    const std::size_t keyOffset = inflated.size();
    AppendZeros(inflated, sizeof(ItemKeyHandler));
    WriteU32(inflated, keyOffset + offsetof(ItemKeyHandler, key), 13u);
    WriteU32(inflated, keyOffset + offsetof(ItemKeyHandler, action),
        UINT32_MAX);
    AppendCString(inflated, keyAction);
    AppendCString(inflated, enableDvar);

    const std::size_t listBoxOffset = inflated.size();
    AppendZeros(inflated, sizeof(listBoxDef_s));
    WriteU32(inflated, listBoxOffset + offsetof(listBoxDef_s, doubleClick),
        UINT32_MAX);
    AppendCString(inflated, doubleClick);
    if (options.expressionCount <= 0) return CompressXFile(inflated);

    for (std::int32_t index = 0; index < options.expressionCount; ++index)
        AppendU32(inflated, 1u);
    if (!options.includeExpressionBodies) return CompressXFile(inflated);
    for (std::int32_t index = 0; index < options.expressionCount; ++index)
    {
        const std::size_t entryOffset = inflated.size();
        AppendZeros(inflated, sizeof(expressionEntry));
        if (index == 0)
        {
            WriteU32(inflated, entryOffset + offsetof(expressionEntry, type),
                1u);
            WriteU32(inflated, entryOffset + offsetof(expressionEntry, data) +
                offsetof(Operand, dataType), VAL_STRING);
            WriteU32(inflated, entryOffset + offsetof(expressionEntry, data) +
                offsetof(Operand, internals), UINT32_MAX);
            AppendCString(inflated, expressionString);
        }
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

std::vector<std::uint8_t> MakeGameWorldSpXFile(bool invalidNodeCount = false)
{
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 16384);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        4096, 4096, 0, 0, 8192, 0, 0, 0, 0}) AppendU32(inflated, size);

    // Script-string index zero is the canonical null entry. Keep the remap
    // table present so native and Wasm execute the same indexed lookup.
    AppendU32(inflated, 1);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 1);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, 0);
    AppendU32(inflated, ASSET_TYPE_GAMEWORLD_SP);
    AppendU32(inflated, UINT32_MAX - 1u);

    GameWorldSp world{};
    world.name = PointerToken<const char>(UINT32_MAX);
    world.path.nodeCount = invalidNodeCount ? PATH_MAX_NODES + 1u : 1u;
    world.path.nodes = PointerToken<pathnode_t>(1u);
    world.path.basenodes = PointerToken<pathbasenode_t>(1u);
    world.path.chainNodeCount = 1u;
    world.path.chainNodeForNode = PointerToken<std::uint16_t>(1u);
    world.path.nodeForChainNode = PointerToken<std::uint16_t>(1u);
    world.path.visBytes = 3;
    world.path.pathVis = PointerToken<std::uint8_t>(1u);
    world.path.nodeTreeCount = 1;
    world.path.nodeTree = PointerToken<pathnode_tree_t>(1u);
    AppendObject(inflated, world);
    AppendCString(inflated, "maps/killhouse.d3dbsp");
    if (invalidNodeCount) return CompressXFile(inflated);

    pathnode_t node{};
    node.constant.type = NODE_PATHNODE;
    node.constant.totalLinkCount = 1;
    node.constant.Links = PointerToken<pathlink_s>(1u);
    AppendObject(inflated, node);
    pathlink_s link{};
    link.fDist = 64.0f;
    link.nodeNum = 0;
    AppendObject(inflated, link);

    AppendU16(inflated, 0);
    AppendU16(inflated, 0);
    inflated.push_back(0x01);
    inflated.push_back(0x02);
    inflated.push_back(0x04);

    pathnode_tree_t tree{};
    tree.axis = -1;
    tree.u.s.nodeCount = 1;
    tree.u.s.nodes = PointerToken<std::uint16_t>(1u);
    AppendObject(inflated, tree);
    AppendU16(inflated, 0);
    return CompressXFile(inflated);
}

std::vector<std::uint8_t> MakeClipMapXFile(bool invalidPlaneCount = false)
{
    constexpr const char *mapName = "maps/killhouse.d3dbsp";
    constexpr const char *entities =
        "{\n\"classname\" \"worldspawn\"\n}\n";
    std::vector<std::uint8_t> inflated;
    AppendU32(inflated, 32768);
    AppendU32(inflated, 0);
    for (const std::uint32_t size : std::array<std::uint32_t, 9>{
        8192, 4096, 0, 0, 16384, 0, 0, 0, 0}) AppendU32(inflated, size);

    AppendU32(inflated, 0);
    AppendU32(inflated, 0);
    AppendU32(inflated, 1);
    AppendU32(inflated, UINT32_MAX);
    AppendU32(inflated, ASSET_TYPE_CLIPMAP);
    AppendU32(inflated, UINT32_MAX - 1u);

    clipMap_t clip{};
    clip.name = PointerToken<const char>(UINT32_MAX);
    clip.isInUse = 1;
    clip.planeCount = invalidPlaneCount ? -1 : 1;
    clip.planes = PointerToken<cplane_s>(UINT32_MAX);
    clip.numMaterials = 1;
    clip.materials = PointerToken<dmaterial_t>(1u);
    clip.numBrushSides = 1;
    clip.brushsides = PointerToken<cbrushside_t>(1u);
    clip.numBrushEdges = 2;
    clip.brushEdges = PointerToken<std::uint8_t>(1u);
    clip.numNodes = 1;
    clip.nodes = PointerToken<cNode_t>(1u);
    clip.numLeafs = 1;
    clip.leafs = PointerToken<cLeaf_t>(1u);
    clip.leafbrushNodesCount = 1;
    clip.leafbrushNodes = PointerToken<cLeafBrushNode_s>(1u);
    clip.numLeafBrushes = 1;
    clip.leafbrushes = PointerToken<std::uint16_t>(1u);
    clip.numLeafSurfaces = 1;
    clip.leafsurfaces = PointerToken<std::uint32_t>(1u);
    clip.vertCount = 1;
    clip.verts = PointerToken<float[3]>(1u);
    clip.triCount = 1;
    clip.triIndices = PointerToken<std::uint16_t>(1u);
    clip.triEdgeIsWalkable = PointerToken<std::uint8_t>(1u);
    clip.borderCount = 1;
    clip.borders = PointerToken<CollisionBorder>(1u);
    clip.partitionCount = 1;
    clip.partitions = PointerToken<CollisionPartition>(1u);
    clip.aabbTreeCount = 1;
    clip.aabbTrees = PointerToken<CollisionAabbTree>(1u);
    clip.numSubModels = 1;
    clip.cmodels = PointerToken<cmodel_t>(1u);
    clip.numBrushes = 1;
    clip.brushes = PointerToken<cbrush_t>(1u);
    clip.numClusters = 1;
    clip.clusterBytes = 1;
    clip.visibility = PointerToken<std::uint8_t>(1u);
    clip.mapEnts = PointerToken<MapEnts>(UINT32_MAX - 1u);
    clip.dynEntCount[0] = 1;
    clip.dynEntPoseList[0] = PointerToken<DynEntityPose>(1u);
    clip.dynEntClientList[0] = PointerToken<DynEntityClient>(1u);
    clip.dynEntCollList[0] = PointerToken<DynEntityColl>(1u);
    clip.checksum = 0x4b484f55u;
    AppendObject(inflated, clip);
    AppendCString(inflated, mapName);
    if (invalidPlaneCount) return CompressXFile(inflated);

    cplane_s plane{};
    plane.normal[2] = 1.0f;
    plane.dist = 32.0f;
    plane.type = 2;
    AppendObject(inflated, plane);

    dmaterial_t material{};
    std::memcpy(material.material, "concrete", 9);
    material.contentFlags = 1;
    AppendObject(inflated, material);

    cbrushside_t side{};
    side.plane = PointerToken<cplane_s>(UINT32_MAX);
    side.materialNum = 0;
    side.edgeCount = 1;
    AppendObject(inflated, side);
    AppendObject(inflated, plane);
    inflated.push_back(3);
    inflated.push_back(4);

    cNode_t node{};
    node.plane = PointerToken<cplane_s>(UINT32_MAX);
    node.children[0] = -1;
    node.children[1] = -1;
    AppendObject(inflated, node);
    AppendObject(inflated, plane);

    cLeaf_t leaf{};
    leaf.brushContents = 1;
    leaf.cluster = 0;
    AppendObject(inflated, leaf);
    AppendU16(inflated, 0);

    cLeafBrushNode_s leafNode{};
    leafNode.leafBrushCount = 1;
    leafNode.data.leaf.brushes = PointerToken<std::uint16_t>(UINT32_MAX);
    AppendObject(inflated, leafNode);
    AppendU16(inflated, 0);
    AppendU32(inflated, 0);
    AppendF32(inflated, 1.0f);
    AppendF32(inflated, 2.0f);
    AppendF32(inflated, 3.0f);
    AppendU16(inflated, 0);
    AppendU16(inflated, 0);
    AppendU16(inflated, 0);
    AppendU32(inflated, 1);

    CollisionBorder border{};
    border.length = 16.0f;
    AppendObject(inflated, border);
    CollisionPartition partition{};
    partition.triCount = 1;
    partition.borderCount = 1;
    partition.borders = PointerToken<CollisionBorder>(UINT32_MAX);
    AppendObject(inflated, partition);
    AppendObject(inflated, border);
    CollisionAabbTree tree{};
    tree.halfSize[0] = 4.0f;
    tree.childCount = 0;
    AppendObject(inflated, tree);
    cmodel_t model{};
    model.radius = 64.0f;
    AppendObject(inflated, model);
    cbrush_t brush{};
    brush.contents = 1;
    AppendObject(inflated, brush);
    inflated.push_back(0xff);

    MapEnts mapEnts{};
    mapEnts.name = PointerToken<const char>(UINT32_MAX);
    mapEnts.entityString = PointerToken<char>(1u);
    mapEnts.numEntityChars = static_cast<int>(std::strlen(entities) + 1u);
    AppendObject(inflated, mapEnts);
    AppendCString(inflated, mapName);
    AppendCString(inflated, entities);

    AppendZeros(inflated, 32);
    AppendZeros(inflated, 12);
    AppendZeros(inflated, 20);
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
void __cdecl PMem_Free(const char *, std::uint32_t) {}
void __cdecl CM_Unload() {}
void __cdecl Com_UnloadWorld() {}
void __cdecl R_UnloadWorld() {}
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
void DB_RuntimeTraceStop(const char *stage)
{
    g_trace.stopStage = stage;
    // The differential fixture normalizes transport/inflate and generated
    // loader failures into one platform-independent failure bit.
    g_trace.generatedLoadFailed =
        g_trace.generatedLoadFailed || DB_HasXFileLoadFailure();
}
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
void SL_AddUser(std::uint32_t, std::uint32_t) {}
void Load_GetCurrentZoneHandle(std::uint8_t *handle)
{
    assert(handle);
    *handle = static_cast<std::uint8_t>(g_zoneIndex);
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

    Run(MakeGameWorldSpXFile(), zone);
    assert(g_trace.xassetListEnd && !g_trace.generatedLoadFailed);
    assert(g_trace.assetType == ASSET_TYPE_GAMEWORLD_SP);
    assert(std::strcmp(g_trace.assetName, "maps/killhouse.d3dbsp") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    const XAssetHeader publishedGameWorld = DB_FindXAssetHeader(
        ASSET_TYPE_GAMEWORLD_SP, "maps/killhouse.d3dbsp");
    assert(publishedGameWorld.gameWorldSp ==
        DB_XAssetPool[ASSET_TYPE_GAMEWORLD_SP]);
    assert(publishedGameWorld.gameWorldSp->path.nodeCount == 1);
    assert(publishedGameWorld.gameWorldSp->path.nodes);
    assert(publishedGameWorld.gameWorldSp->path.nodes[0].constant.Links);
    assert(publishedGameWorld.gameWorldSp->path.nodes[0].constant.Links[0].fDist == 64.0f);
    assert(publishedGameWorld.gameWorldSp->path.basenodes[0].vOrigin[2] == 0.0f);
    assert(publishedGameWorld.gameWorldSp->path.pathVis[2] == 0x04);
    assert(publishedGameWorld.gameWorldSp->path.nodeTree[0].axis == -1);
    assert(publishedGameWorld.gameWorldSp->path.nodeTree[0].u.s.nodes[0] == 0);
    assert(std::strcmp(g_trace.stopStage,
        "Load_XAssetHeader/next-family-closure") == 0);

    Run(MakeGameWorldSpXFile(true), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "GameWorldSp/invalid path counts") == 0);

    Run(MakeClipMapXFile(), zone);
    assert(g_trace.xassetListEnd && !g_trace.generatedLoadFailed);
    assert(g_trace.assetType == ASSET_TYPE_CLIPMAP);
    assert(std::strcmp(g_trace.assetName, "maps/killhouse.d3dbsp") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    const XAssetHeader publishedClipMap = DB_FindXAssetHeader(
        ASSET_TYPE_CLIPMAP, "maps/killhouse.d3dbsp");
    assert(publishedClipMap.clipMap == DB_XAssetPool[ASSET_TYPE_CLIPMAP]);
    assert(publishedClipMap.clipMap->planeCount == 1 &&
        publishedClipMap.clipMap->planes[0].dist == 32.0f);
    assert(publishedClipMap.clipMap->materials &&
        std::strcmp(publishedClipMap.clipMap->materials[0].material,
            "concrete") == 0);
    assert(publishedClipMap.clipMap->brushsides[0].plane->type == 2);
    assert(publishedClipMap.clipMap->nodes[0].plane->normal[2] == 1.0f);
    assert(publishedClipMap.clipMap->leafbrushNodes[0]
        .data.leaf.brushes[0] == 0);
    assert(publishedClipMap.clipMap->partitions[0].borders->length == 16.0f);
    assert(publishedClipMap.clipMap->cmodels[0].radius == 64.0f);
    assert(publishedClipMap.clipMap->mapEnts);
    assert(std::strcmp(publishedClipMap.clipMap->mapEnts->entityString,
        "{\n\"classname\" \"worldspawn\"\n}\n") == 0);
    assert(publishedClipMap.clipMap->dynEntPoseList[0] &&
        publishedClipMap.clipMap->dynEntClientList[0] &&
        publishedClipMap.clipMap->dynEntCollList[0]);
    const XAssetHeader publishedMapEnts = DB_FindXAssetHeader(
        ASSET_TYPE_MAP_ENTS, "maps/killhouse.d3dbsp");
    assert(publishedMapEnts.mapEnts == publishedClipMap.clipMap->mapEnts);

    // Exercise the generated loader's top-level alias path directly. An
    // existing alias must be rebound to the registry singleton, and an alias
    // whose old entry was retired must publish that same singleton again.
    alignas(16) std::array<std::uint8_t, 64> aliasBlock{};
    XZoneMemory aliasZone{};
    aliasZone.blocks[0] = {aliasBlock.data(),
        static_cast<std::uint32_t>(aliasBlock.size())};
    DB_InitStreams(&aliasZone);
    static clipMap_t existingAliasSource{};
    existingAliasSource = *publishedClipMap.clipMap;
    existingAliasSource.name = "maps/killhouse.d3dbsp";
    *reinterpret_cast<std::uint32_t *>(aliasBlock.data() + 16) =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
            &existingAliasSource));
    std::uint32_t existingAliasToken = 17;
    clipMap_t *existingAlias = reinterpret_cast<clipMap_t *>(
        static_cast<std::uintptr_t>(existingAliasToken));
    varclipMap_ptr = &existingAlias;
    Load_clipMap_ptr(false);
    assert(existingAlias == publishedClipMap.clipMap);

    std::strncpy(g_zones[2].name, "cargoship", sizeof(g_zones[2].name));
    g_zones[2].flags = 8;
    DB_SetLoadingZoneIndex(2);
    static clipMap_t retiredAliasSource{};
    retiredAliasSource = *publishedClipMap.clipMap;
    retiredAliasSource.name = "maps/cargoship.d3dbsp";
    *reinterpret_cast<std::uint32_t *>(aliasBlock.data() + 20) =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
            &retiredAliasSource));
    std::uint32_t retiredAliasToken = 21;
    clipMap_t *retiredAlias = reinterpret_cast<clipMap_t *>(
        static_cast<std::uintptr_t>(retiredAliasToken));
    varclipMap_ptr = &retiredAlias;
    Load_clipMap_ptr(false);
    assert(retiredAlias == DB_XAssetPool[ASSET_TYPE_CLIPMAP]);
    assert(DB_FindXAssetHeader(ASSET_TYPE_CLIPMAP,
        "maps/cargoship.d3dbsp").clipMap ==
        DB_XAssetPool[ASSET_TYPE_CLIPMAP]);

    Run(MakeClipMapXFile(true), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage, "ClipMap/planes") == 0);

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
    assert(g_trace.streamOffsets[0] == 0);
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

    const std::vector<std::uint8_t> xmodelInsertAlias = MakeXModelXFile();
    Run(xmodelInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_XMODEL);
    assert(std::strcmp(g_trace.pointerClassification, "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 17 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32751 &&
        g_trace.freeEntryCountAfter == 32750);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "xmodel/gate3", ASSET_TYPE_XMODEL));
    const XAssetHeader publishedXModel = DB_FindXAssetHeader(
        ASSET_TYPE_XMODEL, "xmodel/gate3");
    assert(publishedXModel.model && publishedXModel.model->numBones == 1);
    assert(publishedXModel.model->boneNames &&
        publishedXModel.model->boneNames[0] == 1);
    assert(publishedXModel.model->partClassification[0] == 7);
    assert(publishedXModel.model->baseMat[0].transWeight == 2.0f);
    assert(publishedXModel.model->surfs &&
        publishedXModel.model->surfs[0].zoneHandle == 1);
    assert(publishedXModel.model->surfs[0].verts0[0].xyz[2] == 3.0f);
    assert(publishedXModel.model->surfs[0].triIndices[2] == 0);
    assert(publishedXModel.model->surfs[0].vertList[0].collisionTree);
    assert(publishedXModel.model->collSurfs[0].collTris[0].plane[2] == 1.0f);
    assert(publishedXModel.model->boneInfo[0].radiusSquared == 4.0f);
    assert(publishedXModel.model->physPreset &&
        std::strcmp(publishedXModel.model->physPreset->name,
            "physics/xmodel_gate3") == 0);
    assert(publishedXModel.model->physGeoms &&
        publishedXModel.model->physGeoms->count == 1);
    assert(publishedXModel.model->physGeoms->geoms[0].brush->sides[0].materialNum == 9);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_XMODEL) == 999);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_PHYSPRESET) == 63);

    XModelFixtureOptions sharedXModel{};
    sharedXModel.assetPointer = UINT32_MAX;
    sharedXModel.includeAliasAsset = false;
    Run(MakeXModelXFile(sharedXModel), zone);
    assert(g_trace.publicationEnd && !g_trace.generatedLoadFailed);
    assert(std::strcmp(g_trace.pointerClassification,
        "inline-shared/-1") == 0);

    XModelFixtureOptions nullXModel{};
    nullXModel.assetPointer = 0;
    nullXModel.includeAliasAsset = false;
    Run(MakeXModelXFile(nullXModel), zone);
    assert(!g_trace.generatedLoadFailed && !g_trace.publicationBegin);

    XModelFixtureOptions malformedXModel{};
    malformedXModel.assetPointer = UINT32_MAX - 2u;
    malformedXModel.includeAliasAsset = false;
    Run(MakeXModelXFile(malformedXModel), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    XModelFixtureOptions truncatedXModel{};
    truncatedXModel.includeAliasAsset = false;
    truncatedXModel.includeSurfaceVertices = false;
    Run(MakeXModelXFile(truncatedXModel), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationEnd);

    const std::vector<std::uint8_t> weaponInsertAlias = MakeWeaponXFile();
    Run(weaponInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_WEAPON);
    assert(std::strcmp(g_trace.pointerClassification, "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 17 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32751 &&
        g_trace.freeEntryCountAfter == 32750);
    const XAssetHeader publishedWeapon = DB_FindXAssetHeader(
        ASSET_TYPE_WEAPON, "weapon/gate3");
    assert(publishedWeapon.weapon);
    assert(publishedWeapon.weapon->szDisplayName ==
        publishedWeapon.weapon->szInternalName);
    assert(publishedWeapon.weapon->gunXModel[0] &&
        publishedWeapon.weapon->handXModel ==
            publishedWeapon.weapon->gunXModel[0]);
    assert(std::strcmp(publishedWeapon.weapon->gunXModel[0]->name,
        "xmodel/weapon_gate3") == 0);
    assert(std::strcmp(publishedWeapon.weapon->szXAnims[0],
        "anim_gate3") == 0);
    assert(publishedWeapon.weapon->hideTags[0] == 1);
    assert(!publishedWeapon.weapon->pickupSound);
    assert(publishedWeapon.weapon->bounceSound &&
        !publishedWeapon.weapon->bounceSound[28]);
    assert(publishedWeapon.weapon->accuracyGraphKnots[0][1][1] == 3.0f);
    assert(publishedWeapon.weapon->originalAccuracyGraphKnots[0][0][0] == 10.0f);
    assert(std::strcmp(publishedWeapon.weapon->szScript,
        "scripts/gate3_weapon") == 0);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_WEAPON) == 127);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_XMODEL) == 999);

    WeaponFixtureOptions sharedWeapon{};
    sharedWeapon.assetPointer = UINT32_MAX;
    sharedWeapon.includeAliasAsset = false;
    Run(MakeWeaponXFile(sharedWeapon), zone);
    assert(g_trace.publicationEnd && !g_trace.generatedLoadFailed);

    WeaponFixtureOptions nullWeapon{};
    nullWeapon.assetPointer = 0;
    nullWeapon.includeAliasAsset = false;
    Run(MakeWeaponXFile(nullWeapon), zone);
    assert(!g_trace.publicationBegin && !g_trace.generatedLoadFailed);

    WeaponFixtureOptions malformedWeapon{};
    malformedWeapon.assetPointer = UINT32_MAX - 2u;
    malformedWeapon.includeAliasAsset = false;
    Run(MakeWeaponXFile(malformedWeapon), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);

    WeaponFixtureOptions truncatedWeapon{};
    truncatedWeapon.includeAliasAsset = false;
    truncatedWeapon.includeXModelBody = false;
    Run(MakeWeaponXFile(truncatedWeapon), zone);
    assert(DB_RuntimeGeneratedLoadFailed() && !g_trace.publicationEnd);

    WeaponFixtureOptions excessiveWeapon{};
    excessiveWeapon.includeAliasAsset = false;
    excessiveWeapon.accuracyCount = (std::numeric_limits<std::int32_t>::max)();
    Run(MakeWeaponXFile(excessiveWeapon), zone);
    assert(g_trace.generatedLoadFailed);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_WEAPON,
        "weapon/gate3").weapon);
    assert(std::strcmp(g_trace.stopStage, "Weapon/accuracy graph 0") == 0);

    const std::vector<std::uint8_t> xanimInsertAlias = MakeXAnimXFile();
    Run(xanimInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_XANIMPARTS);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    const XAssetHeader publishedXAnim = DB_FindXAssetHeader(
        ASSET_TYPE_XANIMPARTS, "xanim/gate3");
    assert(publishedXAnim.parts && publishedXAnim.parts->numframes == 300);
    assert(publishedXAnim.parts->names[0] == 1 &&
        publishedXAnim.parts->names[1] == 1);
    assert(publishedXAnim.parts->notify[0].name == 1 &&
        publishedXAnim.parts->notify[0].time == 0.5f);
    assert(publishedXAnim.parts->deltaPart->trans->size == 1);
    assert(publishedXAnim.parts->deltaPart->trans->u.frames.frames._2[1][2] == 5);
    assert(publishedXAnim.parts->deltaPart->quat->u.frames.frames[1][1] == 13);
    assert(publishedXAnim.parts->dataByte[0] == 0xaa);
    assert(publishedXAnim.parts->dataShort[0] == 0x1234);
    assert(publishedXAnim.parts->dataInt[0] == 0x55667788);
    assert(publishedXAnim.parts->indices._2[1] == 3);

    XAnimFixtureOptions nullXAnim{};
    nullXAnim.assetPointer = 0;
    nullXAnim.includeAliasAsset = false;
    Run(MakeXAnimXFile(nullXAnim), zone);
    assert(!g_trace.generatedLoadFailed && !g_trace.publicationBegin);

    XAnimFixtureOptions malformedXAnim{};
    malformedXAnim.assetPointer = UINT32_MAX - 2u;
    malformedXAnim.includeAliasAsset = false;
    Run(MakeXAnimXFile(malformedXAnim), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);

    XAnimFixtureOptions truncatedXAnim{};
    truncatedXAnim.includeAliasAsset = false;
    truncatedXAnim.includePayload = false;
    Run(MakeXAnimXFile(truncatedXAnim), zone);
    assert(DB_RuntimeGeneratedLoadFailed() && !g_trace.publicationEnd);

    const std::vector<std::uint8_t> stringTableFixture =
        MakeStringTableXFile();
    Run(stringTableFixture, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_STRINGTABLE);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    const XAssetHeader publishedStringTable = DB_FindXAssetHeader(
        ASSET_TYPE_STRINGTABLE, "stringtable/gate3.csv");
    assert(publishedStringTable.stringTable);
    assert(publishedStringTable.stringTable->columnCount == 2 &&
        publishedStringTable.stringTable->rowCount == 2);
    assert(std::strcmp(publishedStringTable.stringTable->values[0], "key") == 0);
    assert(std::strcmp(publishedStringTable.stringTable->values[1], "value") == 0);
    assert(!publishedStringTable.stringTable->values[2]);
    assert(publishedStringTable.stringTable->values[3] ==
        publishedStringTable.stringTable->name);

    StringTableFixtureOptions nullStringTable{};
    nullStringTable.assetPointer = 0;
    nullStringTable.includeAliasAsset = false;
    Run(MakeStringTableXFile(nullStringTable), zone);
    assert(!g_trace.generatedLoadFailed && !g_trace.publicationBegin);

    StringTableFixtureOptions malformedStringTable{};
    malformedStringTable.assetPointer = UINT32_MAX - 1u;
    malformedStringTable.includeAliasAsset = false;
    Run(MakeStringTableXFile(malformedStringTable), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);

    StringTableFixtureOptions excessiveStringTable{};
    excessiveStringTable.includeAliasAsset = false;
    excessiveStringTable.columns = (std::numeric_limits<std::int32_t>::max)();
    excessiveStringTable.rows = 2;
    Run(MakeStringTableXFile(excessiveStringTable), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage, "StringTable/excessive values") == 0);

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
    assert(g_trace.streamOffsets[0] == 0);
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
    assert(g_trace.streamOffsets[0] == 0);
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
    WebDbImageLoadDef publishedImageLoadDef{};
    assert(DB_WebGetImageLoadDef(publishedImage.image, publishedImageLoadDef));
    assert(publishedImageLoadDef.format == 21);
    assert(publishedImageLoadDef.byteLength == 4);
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
    assert(g_trace.streamOffsets[0] == 0);
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

    // Publication owns malformed curve repair.  The source body remains
    // untouched, while the pooled header receives canonical two-knot values
    // under the original requested filename.
    static SndCurve malformedPublishedCurve{};
    malformedPublishedCurve.filename = "soundcurves/publication_default";
    malformedPublishedCurve.knotCount = 0;
    XAssetHeader malformedPublishedHeader{&malformedPublishedCurve};
    Load_SndCurveAsset(&malformedPublishedHeader);
    assert(malformedPublishedCurve.knotCount == 0);
    assert(malformedPublishedHeader.sndCurve);
    assert(malformedPublishedHeader.sndCurve != &malformedPublishedCurve);
    const SndCurve *stablePublishedCurve = malformedPublishedHeader.sndCurve;
    assert(Com_IsValidSoundAliasVolumeFalloffCurve(stablePublishedCurve));
    assert(stablePublishedCurve->knotCount == 2);
    assert(stablePublishedCurve->knots[0][0] == 0.0f &&
        stablePublishedCurve->knots[0][1] == 1.0f &&
        stablePublishedCurve->knots[1][0] == 1.0f &&
        stablePublishedCurve->knots[1][1] == 0.0f);
    assert(std::strcmp(stablePublishedCurve->filename,
        malformedPublishedCurve.filename) == 0);

    static char mutableCurveName[] = "soundcurves/stable_name";
    static SndCurve mutableNameCurve{};
    mutableNameCurve.filename = mutableCurveName;
    mutableNameCurve.knotCount = 2;
    mutableNameCurve.knots[0][0] = 0.0f;
    mutableNameCurve.knots[0][1] = 1.0f;
    mutableNameCurve.knots[1][0] = 1.0f;
    mutableNameCurve.knots[1][1] = 0.0f;
    XAssetHeader mutableNameHeader{&mutableNameCurve};
    Load_SndCurveAsset(&mutableNameHeader);
    assert(mutableNameHeader.sndCurve);
    mutableCurveName[0] = '\0';
    assert(std::strcmp(mutableNameHeader.sndCurve->filename,
        "soundcurves/stable_name") == 0);

    static SndCurve emptyNameCurve{};
    emptyNameCurve.filename = "";
    emptyNameCurve.knotCount = 0;
    XAssetHeader emptyNameHeader{&emptyNameCurve};
    Load_SndCurveAsset(&emptyNameHeader);
    assert(emptyNameHeader.sndCurve);
    assert(std::strcmp(emptyNameHeader.sndCurve->filename, "default") == 0);
    assert(Com_IsValidSoundAliasVolumeFalloffCurve(
        emptyNameHeader.sndCurve));
    assert(emptyNameHeader.sndCurve == DB_FindXAssetHeader(
        ASSET_TYPE_SOUND_CURVE, "default").sndCurve);

    // A higher-priority replacement keeps the pooled primary identity while
    // replacing the repaired body with valid retail curve data.
    g_zones[2].flags = 2;
    DB_SetLoadingZoneIndex(2);
    static SndCurve replacementPublishedCurve{};
    replacementPublishedCurve.filename =
        "soundcurves/publication_default";
    replacementPublishedCurve.knotCount = 3;
    replacementPublishedCurve.knots[0][0] = 0.0f;
    replacementPublishedCurve.knots[0][1] = 1.0f;
    replacementPublishedCurve.knots[1][0] = 0.5f;
    replacementPublishedCurve.knots[1][1] = 0.5f;
    replacementPublishedCurve.knots[2][0] = 1.0f;
    replacementPublishedCurve.knots[2][1] = 0.0f;
    XAssetHeader replacementPublishedHeader{&replacementPublishedCurve};
    Load_SndCurveAsset(&replacementPublishedHeader);
    assert(replacementPublishedHeader.sndCurve == stablePublishedCurve);
    assert(replacementPublishedHeader.sndCurve->knotCount == 3);
    assert(replacementPublishedHeader.sndCurve->knots[1][0] == 0.5f);
    assert(std::strcmp(replacementPublishedHeader.sndCurve->filename,
        replacementPublishedCurve.filename) == 0);

    SndCurveFixtureOptions sharedSndCurve{};
    sharedSndCurve.assetPointer = UINT32_MAX;
    sharedSndCurve.includeAliasAsset = false;
    Run(MakeSndCurveXFile(sharedSndCurve), zone);
    assert(std::strcmp(g_trace.pointerClassification,
        "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.assetEntryIndex == 16);
    assert(g_trace.streamOffsets[0] == 0 &&
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
    assert(g_trace.streamOffsets[0] == 0);
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
#if defined(KISAK_SOUND_CURVE_PUBLICATION_TEST)
    SndCurve *publishedDefaultCurve =
        Com_GetDefaultSoundAliasVolumeFalloffCurve();
    assert(publishedSound.sound->head[0].volumeFalloffCurve
        == publishedDefaultCurve);
    assert(Com_IsValidSoundAliasVolumeFalloffCurve(publishedDefaultCurve));
    // DB_FindXAssetHeader observes the repaired pointer; no selection-time
    // mutation is involved.
#else
    assert(!publishedSound.sound->head[0].volumeFalloffCurve);
#endif
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
    assert(g_trace.streamOffsets[0] == 0 && g_trace.streamOffsets[4] == 44);
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
    assert(g_trace.streamOffsets[0] == 0);
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
    assert(g_trace.streamOffsets[0] == 0);
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
    assert(g_trace.publicationEnd && g_trace.streamOffsets[0] == 0 &&
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
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);
    std::uint32_t failedFxModelInsertion = UINT32_MAX;
    std::memcpy(&failedFxModelInsertion, zone.blocks[4].data + 16,
        sizeof(failedFxModelInsertion));
    assert(failedFxModelInsertion == 0);

    const std::vector<std::uint8_t> fxImpactInsertAlias =
        MakeFxImpactXFile();
    Run(fxImpactInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_IMPACT_FX);
    assert(std::strcmp(g_trace.pointerClassification,
        "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 17 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32751 &&
        g_trace.freeEntryCountAfter == 32750);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "impact/gate3", ASSET_TYPE_IMPACT_FX));
    assert(g_trace.assetZoneIndex == 1);
    assert(g_trace.streamOffsets[0] == 0);
    assert(g_trace.streamOffsets[4] == 1640);
    const XAssetHeader publishedImpact = DB_FindXAssetHeader(
        ASSET_TYPE_IMPACT_FX, "impact/gate3");
    const XAssetHeader publishedImpactFx = DB_FindXAssetHeader(
        ASSET_TYPE_FX, "fx/impact_child");
    assert(publishedImpact.impactFx && publishedImpact.impactFx->table);
    assert(publishedImpactFx.fx);
    assert(publishedImpact.impactFx->table[0].nonflesh[0] ==
        publishedImpactFx.fx);
    assert(publishedImpact.impactFx->table[0].nonflesh[1] ==
        publishedImpactFx.fx);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_FX) == 399);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_IMPACT_FX) == 3);
    const XAsset *fxImpactAssets = reinterpret_cast<const XAsset *>(
        zone.blocks[4].data);
    assert(fxImpactAssets[0].header.impactFx == publishedImpact.impactFx &&
        fxImpactAssets[1].header.impactFx == publishedImpact.impactFx);
    std::uint32_t fxImpactInsertion = 0;
    std::uint32_t nestedFxInsertion = 0;
    std::memcpy(&fxImpactInsertion, zone.blocks[4].data + 16,
        sizeof(fxImpactInsertion));
    std::memcpy(&nestedFxInsertion, zone.blocks[4].data + 1620,
        sizeof(nestedFxInsertion));
    assert(fxImpactInsertion == reinterpret_cast<std::uint32_t>(
        publishedImpact.impactFx));
    assert(nestedFxInsertion == reinterpret_cast<std::uint32_t>(
        publishedImpactFx.fx));

    FxImpactFixtureOptions sharedImpact{};
    sharedImpact.assetPointer = UINT32_MAX;
    sharedImpact.includeAliasAsset = false;
    sharedImpact.tablePointer = 0;
    Run(MakeFxImpactXFile(sharedImpact), zone);
    assert(std::strcmp(g_trace.pointerClassification,
        "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.assetEntryIndex == 16);
    assert(g_trace.streamOffsets[0] == 0 &&
        g_trace.streamOffsets[4] == 21);
    assert(DB_FindXAssetHeader(ASSET_TYPE_IMPACT_FX,
        "impact/gate3").impactFx);

    FxImpactFixtureOptions nullImpact{};
    nullImpact.assetPointer = 0;
    nullImpact.includeAliasAsset = false;
    Run(MakeFxImpactXFile(nullImpact), zone);
    assert(!g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_IMPACT_FX) == 4);

    FxImpactFixtureOptions malformedImpact{};
    malformedImpact.assetPointer = UINT32_MAX - 2u;
    malformedImpact.includeAliasAsset = false;
    Run(MakeFxImpactXFile(malformedImpact), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    FxImpactFixtureOptions truncatedImpactName{};
    truncatedImpactName.assetPointer = UINT32_MAX;
    truncatedImpactName.includeAliasAsset = false;
    truncatedImpactName.terminateName = false;
    Run(MakeFxImpactXFile(truncatedImpactName), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0 ||
        std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    FxImpactFixtureOptions truncatedImpactTable{};
    truncatedImpactTable.assetPointer = UINT32_MAX;
    truncatedImpactTable.includeAliasAsset = false;
    truncatedImpactTable.includeTable = false;
    Run(MakeFxImpactXFile(truncatedImpactTable), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "FX impact/entry array") == 0 ||
        std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    FxImpactFixtureOptions malformedImpactFx{};
    malformedImpactFx.assetPointer = UINT32_MAX;
    malformedImpactFx.includeAliasAsset = false;
    malformedImpactFx.firstFxPointer = UINT32_MAX - 2u;
    Run(MakeFxImpactXFile(malformedImpactFx), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    FxImpactFixtureOptions truncatedImpactFx{};
    truncatedImpactFx.assetPointer = UINT32_MAX;
    truncatedImpactFx.includeAliasAsset = false;
    truncatedImpactFx.includeFxBody = false;
    Run(MakeFxImpactXFile(truncatedImpactFx), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    const std::vector<std::uint8_t> comWorldInsertAlias = MakeComWorldXFile();
    Run(comWorldInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_COMWORLD);
    assert(std::strcmp(g_trace.pointerClassification,
        "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32752 &&
        g_trace.freeEntryCountAfter == 32751);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "maps/gate3.d3dbsp", ASSET_TYPE_COMWORLD));
    assert(g_trace.streamOffsets[0] == 0 &&
        g_trace.streamOffsets[4] == 204);
    const XAssetHeader publishedComWorld = DB_FindXAssetHeader(
        ASSET_TYPE_COMWORLD, "maps/gate3.d3dbsp");
    assert(publishedComWorld.comWorld);
    assert(publishedComWorld.comWorld->isInUse == 1 &&
        publishedComWorld.comWorld->primaryLightCount == 2);
    assert(publishedComWorld.comWorld->primaryLights);
    assert(publishedComWorld.comWorld->primaryLights[0].type == 1 &&
        publishedComWorld.comWorld->primaryLights[1].type == 2);
    assert(publishedComWorld.comWorld->primaryLights[0].radius == 128.0f &&
        publishedComWorld.comWorld->primaryLights[1].radius == 129.0f);
    assert(std::strcmp(publishedComWorld.comWorld->primaryLights[0].defName,
        "light/gate3_0") == 0);
    assert(std::strcmp(publishedComWorld.comWorld->primaryLights[1].defName,
        "light/gate3_1") == 0);
    const XAsset *comWorldAssets = reinterpret_cast<const XAsset *>(
        zone.blocks[4].data);
    assert(comWorldAssets[0].header.comWorld == publishedComWorld.comWorld &&
        comWorldAssets[1].header.comWorld == publishedComWorld.comWorld);
    std::uint32_t comWorldInsertion = 0;
    std::memcpy(&comWorldInsertion, zone.blocks[4].data + 16,
        sizeof(comWorldInsertion));
    assert(comWorldInsertion == reinterpret_cast<std::uint32_t>(
        publishedComWorld.comWorld));

    ComWorldFixtureOptions sharedComWorld{};
    sharedComWorld.assetPointer = UINT32_MAX;
    sharedComWorld.includeAliasAsset = false;
    sharedComWorld.primaryLightCount = 0;
    sharedComWorld.primaryLightsPointer = 0;
    sharedComWorld.includedLightCount = 0;
    sharedComWorld.inlineDefNameCount = 0;
    Run(MakeComWorldXFile(sharedComWorld), zone);
    assert(std::strcmp(g_trace.pointerClassification,
        "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.assetEntryIndex == 16 &&
        g_trace.assetPoolIndex == 0);
    assert(g_trace.streamOffsets[0] == 0 &&
        g_trace.streamOffsets[4] == 26);

    ComWorldFixtureOptions nullComWorld{};
    nullComWorld.assetPointer = 0;
    nullComWorld.includeAliasAsset = false;
    Run(MakeComWorldXFile(nullComWorld), zone);
    assert(!g_trace.generatedLoadFailed && !g_trace.publicationBegin);

    ComWorldFixtureOptions malformedComWorld{};
    malformedComWorld.assetPointer = UINT32_MAX - 2u;
    malformedComWorld.includeAliasAsset = false;
    Run(MakeComWorldXFile(malformedComWorld), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    ComWorldFixtureOptions truncatedComWorld{};
    truncatedComWorld.assetPointer = UINT32_MAX;
    truncatedComWorld.includeAliasAsset = false;
    truncatedComWorld.includeBody = false;
    Run(MakeComWorldXFile(truncatedComWorld), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    ComWorldFixtureOptions unterminatedComWorld{};
    unterminatedComWorld.assetPointer = UINT32_MAX;
    unterminatedComWorld.includeAliasAsset = false;
    unterminatedComWorld.terminateName = false;
    Run(MakeComWorldXFile(unterminatedComWorld), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0 ||
        std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    ComWorldFixtureOptions excessiveComWorld{};
    excessiveComWorld.assetPointer = UINT32_MAX;
    excessiveComWorld.includeAliasAsset = false;
    excessiveComWorld.primaryLightCount = UINT32_MAX;
    excessiveComWorld.includeLights = false;
    Run(MakeComWorldXFile(excessiveComWorld), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "ComWorld/excessive primary lights") == 0);

    ComWorldFixtureOptions truncatedComWorldLights{};
    truncatedComWorldLights.assetPointer = UINT32_MAX;
    truncatedComWorldLights.includeAliasAsset = false;
    truncatedComWorldLights.includedLightCount = 1;
    truncatedComWorldLights.inlineDefNameCount = 0;
    Run(MakeComWorldXFile(truncatedComWorldLights), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);

    ComWorldFixtureOptions truncatedComWorldLightName{};
    truncatedComWorldLightName.assetPointer = UINT32_MAX;
    truncatedComWorldLightName.includeAliasAsset = false;
    truncatedComWorldLightName.inlineDefNameCount = 1;
    Run(MakeComWorldXFile(truncatedComWorldLightName), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);

    const std::vector<std::uint8_t> gfxWorldInsertAlias =
        MakeGfxWorldXFile();
    Run(gfxWorldInsertAlias, zone);
    assert(!g_trace.generatedLoadFailed);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_GFXWORLD);
    assert(std::strcmp(g_trace.pointerClassification,
        "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 16 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32752 &&
        g_trace.freeEntryCountAfter == 32751);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "maps/gfxworld_gate3.d3dbsp", ASSET_TYPE_GFXWORLD));
    assert(g_trace.streamOffsets[0] == 0 &&
        g_trace.streamOffsets[1] == 0 &&
        g_trace.streamOffsets[4] == 248);
    const XAssetHeader publishedGfxWorld = DB_FindXAssetHeader(
        ASSET_TYPE_GFXWORLD, "maps/gfxworld_gate3.d3dbsp");
    assert(publishedGfxWorld.gfxWorld);
    assert(std::strcmp(publishedGfxWorld.gfxWorld->baseName,
        "gfxworld_gate3") == 0);
    assert(publishedGfxWorld.gfxWorld->indexCount == 3 &&
        publishedGfxWorld.gfxWorld->vertexCount == 3 &&
        publishedGfxWorld.gfxWorld->surfaceCount == 1);
    assert(publishedGfxWorld.gfxWorld->indices[0] == 0 &&
        publishedGfxWorld.gfxWorld->indices[1] == 1 &&
        publishedGfxWorld.gfxWorld->indices[2] == 2);
    assert(publishedGfxWorld.gfxWorld->vd.vertices[0].xyz[0] == -1.0f &&
        publishedGfxWorld.gfxWorld->vd.vertices[1].xyz[0] == 1.0f &&
        publishedGfxWorld.gfxWorld->vd.vertices[2].xyz[1] == 1.0f);
    assert(publishedGfxWorld.gfxWorld->dpvs.surfaces);
    assert(publishedGfxWorld.gfxWorld->dpvs.surfaces[0].tris.vertexCount == 3 &&
        publishedGfxWorld.gfxWorld->dpvs.surfaces[0].tris.triCount == 1);
    const XAsset *gfxWorldAssets = reinterpret_cast<const XAsset *>(
        zone.blocks[4].data);
    assert(gfxWorldAssets[0].header.gfxWorld == publishedGfxWorld.gfxWorld &&
        gfxWorldAssets[1].header.gfxWorld == publishedGfxWorld.gfxWorld);
    std::uint32_t gfxWorldInsertion = 0;
    std::memcpy(&gfxWorldInsertion, zone.blocks[4].data + 16,
        sizeof(gfxWorldInsertion));
    assert(gfxWorldInsertion == reinterpret_cast<std::uint32_t>(
        publishedGfxWorld.gfxWorld));

    GfxWorldFixtureOptions sharedGfxWorld{};
    sharedGfxWorld.assetPointer = UINT32_MAX;
    sharedGfxWorld.includeAliasAsset = false;
    Run(MakeGfxWorldXFile(sharedGfxWorld), zone);
    assert(!g_trace.generatedLoadFailed && g_trace.publicationEnd);
    assert(std::strcmp(g_trace.pointerClassification,
        "inline-shared/-1") == 0);

    GfxWorldFixtureOptions nullGfxWorld{};
    nullGfxWorld.assetPointer = 0;
    nullGfxWorld.includeAliasAsset = false;
    Run(MakeGfxWorldXFile(nullGfxWorld), zone);
    assert(!g_trace.generatedLoadFailed && !g_trace.publicationBegin);

    GfxWorldFixtureOptions malformedGfxWorld{};
    malformedGfxWorld.assetPointer = UINT32_MAX - 2u;
    malformedGfxWorld.includeAliasAsset = false;
    Run(MakeGfxWorldXFile(malformedGfxWorld), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    GfxWorldFixtureOptions truncatedGfxWorld{};
    truncatedGfxWorld.assetPointer = UINT32_MAX;
    truncatedGfxWorld.includeAliasAsset = false;
    truncatedGfxWorld.includeBody = false;
    Run(MakeGfxWorldXFile(truncatedGfxWorld), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    GfxWorldFixtureOptions unterminatedGfxWorld{};
    unterminatedGfxWorld.assetPointer = UINT32_MAX;
    unterminatedGfxWorld.includeAliasAsset = false;
    unterminatedGfxWorld.terminateName = false;
    Run(MakeGfxWorldXFile(unterminatedGfxWorld), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);

    GfxWorldFixtureOptions excessiveGfxWorldIndices{};
    excessiveGfxWorldIndices.assetPointer = UINT32_MAX;
    excessiveGfxWorldIndices.includeAliasAsset = false;
    excessiveGfxWorldIndices.indexCount =
        (std::numeric_limits<std::int32_t>::max)();
    Run(MakeGfxWorldXFile(excessiveGfxWorldIndices), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "GfxWorld/index array") == 0);

    GfxWorldFixtureOptions invalidGfxWorldLights{};
    invalidGfxWorldLights.assetPointer = UINT32_MAX;
    invalidGfxWorldLights.includeAliasAsset = false;
    invalidGfxWorldLights.primaryLightCount = 0;
    Run(MakeGfxWorldXFile(invalidGfxWorldLights), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "GfxWorld/invalid primary light range") == 0);

    GfxWorldFixtureOptions truncatedGfxWorldVertices{};
    truncatedGfxWorldVertices.assetPointer = UINT32_MAX;
    truncatedGfxWorldVertices.includeAliasAsset = false;
    truncatedGfxWorldVertices.includedVertexCount = 2;
    truncatedGfxWorldVertices.includedSurfaceCount = 0;
    Run(MakeGfxWorldXFile(truncatedGfxWorldVertices), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);

    const std::vector<std::uint8_t> lightInsertAlias = MakeLightDefXFile();
    Run(lightInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_LIGHT_DEF);
    assert(std::strcmp(g_trace.pointerClassification,
        "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 17 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32751 &&
        g_trace.freeEntryCountAfter == 32750);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "lights/gate3", ASSET_TYPE_LIGHT_DEF));
    assert(g_trace.streamOffsets[0] == 0);
    assert(g_trace.streamOffsets[4] == 59);
    const XAssetHeader publishedLight = DB_FindXAssetHeader(
        ASSET_TYPE_LIGHT_DEF, "lights/gate3");
    const XAssetHeader publishedLightImage = DB_FindXAssetHeader(
        ASSET_TYPE_IMAGE, "images/light_gate3");
    assert(publishedLight.lightDef && publishedLightImage.image);
    assert(publishedLight.lightDef->attenuation.image ==
        publishedLightImage.image);
    assert(publishedLight.lightDef->attenuation.samplerState == 7);
    assert(publishedLight.lightDef->lmapLookupStart == 42);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_LIGHT_DEF) == 31);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_IMAGE) == 2399);
    const XAsset *lightAssets = reinterpret_cast<const XAsset *>(
        zone.blocks[4].data);
    assert(lightAssets[0].header.lightDef == publishedLight.lightDef &&
        lightAssets[1].header.lightDef == publishedLight.lightDef);
    std::uint32_t lightInsertion = 0;
    std::uint32_t lightImageInsertion = 0;
    std::memcpy(&lightInsertion, zone.blocks[4].data + 16,
        sizeof(lightInsertion));
    std::memcpy(&lightImageInsertion, zone.blocks[4].data + 36,
        sizeof(lightImageInsertion));
    assert(lightInsertion == reinterpret_cast<std::uint32_t>(
        publishedLight.lightDef));
    assert(lightImageInsertion == reinterpret_cast<std::uint32_t>(
        publishedLightImage.image));

    LightDefFixtureOptions sharedLight{};
    sharedLight.assetPointer = UINT32_MAX;
    sharedLight.includeAliasAsset = false;
    sharedLight.imagePointer = 0;
    Run(MakeLightDefXFile(sharedLight), zone);
    assert(std::strcmp(g_trace.pointerClassification,
        "inline-shared/-1") == 0);
    assert(g_trace.publicationEnd && g_trace.assetEntryIndex == 16);
    assert(g_trace.streamOffsets[0] == 0 &&
        g_trace.streamOffsets[4] == 21);
    assert(DB_FindXAssetHeader(ASSET_TYPE_LIGHT_DEF,
        "lights/gate3").lightDef);

    LightDefFixtureOptions nullLight{};
    nullLight.assetPointer = 0;
    nullLight.includeAliasAsset = false;
    Run(MakeLightDefXFile(nullLight), zone);
    assert(!g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_LIGHT_DEF) == 32);

    LightDefFixtureOptions malformedLight{};
    malformedLight.assetPointer = UINT32_MAX - 2u;
    malformedLight.includeAliasAsset = false;
    Run(MakeLightDefXFile(malformedLight), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    LightDefFixtureOptions truncatedLight{};
    truncatedLight.assetPointer = UINT32_MAX;
    truncatedLight.includeAliasAsset = false;
    truncatedLight.includeBody = false;
    Run(MakeLightDefXFile(truncatedLight), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    LightDefFixtureOptions malformedLightImage{};
    malformedLightImage.assetPointer = UINT32_MAX;
    malformedLightImage.includeAliasAsset = false;
    malformedLightImage.imagePointer = UINT32_MAX - 2u;
    Run(MakeLightDefXFile(malformedLightImage), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    LightDefFixtureOptions truncatedLightImage{};
    truncatedLightImage.assetPointer = UINT32_MAX;
    truncatedLightImage.includeAliasAsset = false;
    truncatedLightImage.includeImageBody = false;
    Run(MakeLightDefXFile(truncatedLightImage), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    LightDefFixtureOptions truncatedLightImageName{};
    truncatedLightImageName.assetPointer = UINT32_MAX;
    truncatedLightImageName.includeAliasAsset = false;
    truncatedLightImageName.terminateImageName = false;
    Run(MakeLightDefXFile(truncatedLightImageName), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0 ||
        std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    const std::vector<std::uint8_t> menuListInsertAlias =
        MakeMenuListXFile();
    Run(menuListInsertAlias, zone);
    assert(g_trace.xassetCount == 2 && g_trace.assetIndex == 1);
    assert(g_trace.assetType == ASSET_TYPE_MENULIST);
    assert(std::strcmp(g_trace.pointerClassification,
        "prior-offset/alias") == 0);
    assert(g_trace.publicationBegin && g_trace.publicationEnd);
    assert(g_trace.assetEntryIndex == 17 && g_trace.assetPoolIndex == 0);
    assert(g_trace.freeEntryCountBefore == 32751 &&
        g_trace.freeEntryCountAfter == 32750);
    assert(g_trace.assetHash == DB_HashForNameCanonical(
        "menus/gate3", ASSET_TYPE_MENULIST));
    assert(g_trace.streamOffsets[0] == 0);
    assert(g_trace.streamOffsets[4] == 880);
    const XAssetHeader publishedMenuList = DB_FindXAssetHeader(
        ASSET_TYPE_MENULIST, "menus/gate3");
    const XAssetHeader publishedMenu = DB_FindXAssetHeader(
        ASSET_TYPE_MENU, "menu/gate3");
    assert(publishedMenuList.menuList && publishedMenu.menu);
    assert(publishedMenuList.menuList->menuCount == 2);
    assert(publishedMenuList.menuList->menus[0] == publishedMenu.menu &&
        publishedMenuList.menuList->menus[1] == publishedMenu.menu);
    assert(std::strcmp(publishedMenu.menu->window.group, "menus/gate3") == 0);
    assert(publishedMenu.menu->itemCount == 1 && publishedMenu.menu->items);
    const itemDef_s *publishedItem = publishedMenu.menu->items[0];
    assert(publishedItem && publishedItem->parent == publishedMenu.menu);
    assert(std::strcmp(publishedItem->window.name, "menu/gate3") == 0);
    assert(std::strcmp(publishedItem->text, "menu item") == 0);
    assert(publishedItem->onKey && publishedItem->onKey->key == 13);
    assert(std::strcmp(publishedItem->onKey->action, "key/action") == 0);
    assert(std::strcmp(publishedItem->enableDvar, "enable_gate") == 0);
    assert(publishedItem->typeData.listBox &&
        std::strcmp(publishedItem->typeData.listBox->doubleClick,
            "double/click") == 0);
    assert(publishedItem->visibleExp.numEntries == 2 &&
        publishedItem->visibleExp.entries);
    assert(publishedItem->visibleExp.entries[0]->type == 1 &&
        publishedItem->visibleExp.entries[0]->data.operand.dataType ==
            VAL_STRING);
    assert(std::strcmp(publishedItem->visibleExp.entries[0]->data.operand.
        internals.string, "expr/string") == 0);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_MENU) == 639);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_MENULIST) == 127);
    const XAsset *menuListAssets = reinterpret_cast<const XAsset *>(
        zone.blocks[4].data);
    assert(menuListAssets[0].header.menuList == publishedMenuList.menuList &&
        menuListAssets[1].header.menuList == publishedMenuList.menuList);
    std::uint32_t menuListInsertion = 0;
    std::uint32_t menuInsertion = 0;
    std::memcpy(&menuListInsertion, zone.blocks[4].data + 16,
        sizeof(menuListInsertion));
    std::memcpy(&menuInsertion, zone.blocks[4].data + 40,
        sizeof(menuInsertion));
    assert(menuListInsertion == reinterpret_cast<std::uint32_t>(
        publishedMenuList.menuList));
    assert(menuInsertion == reinterpret_cast<std::uint32_t>(
        publishedMenu.menu));

    MenuListFixtureOptions sharedMenuList{};
    sharedMenuList.assetPointer = UINT32_MAX;
    sharedMenuList.includeAliasAsset = false;
    sharedMenuList.menuCount = 0;
    sharedMenuList.menusPointer = 0;
    Run(MakeMenuListXFile(sharedMenuList), zone);
    assert(!g_trace.generatedLoadFailed && g_trace.publicationEnd);
    assert(std::strcmp(g_trace.pointerClassification,
        "inline-shared/-1") == 0);
    assert(g_trace.streamOffsets[0] == 0);
    assert(g_trace.streamOffsets[4] == 20);

    MenuListFixtureOptions nullMenuList{};
    nullMenuList.assetPointer = 0;
    nullMenuList.includeAliasAsset = false;
    Run(MakeMenuListXFile(nullMenuList), zone);
    assert(!g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_MENULIST) == 128);

    MenuListFixtureOptions malformedMenuList{};
    malformedMenuList.assetPointer = UINT32_MAX - 2u;
    malformedMenuList.includeAliasAsset = false;
    Run(MakeMenuListXFile(malformedMenuList), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    MenuListFixtureOptions truncatedMenuList{};
    truncatedMenuList.assetPointer = UINT32_MAX;
    truncatedMenuList.includeAliasAsset = false;
    truncatedMenuList.includeListBody = false;
    Run(MakeMenuListXFile(truncatedMenuList), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    MenuListFixtureOptions truncatedMenuListName{};
    truncatedMenuListName.assetPointer = UINT32_MAX;
    truncatedMenuListName.includeAliasAsset = false;
    truncatedMenuListName.terminateListName = false;
    Run(MakeMenuListXFile(truncatedMenuListName), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0 ||
        std::strcmp(g_trace.stopStage, "stream/truncated string") == 0);

    MenuListFixtureOptions invalidMenuCount{};
    invalidMenuCount.assetPointer = UINT32_MAX;
    invalidMenuCount.includeAliasAsset = false;
    invalidMenuCount.menuCount = -1;
    Run(MakeMenuListXFile(invalidMenuCount), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "MenuList/menu pointer table") == 0);

    MenuListFixtureOptions malformedMenu{};
    malformedMenu.assetPointer = UINT32_MAX;
    malformedMenu.includeAliasAsset = false;
    malformedMenu.menuCount = 1;
    malformedMenu.firstMenuPointer = UINT32_MAX - 2u;
    Run(MakeMenuListXFile(malformedMenu), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "stream/invalid alias offset") == 0);

    MenuListFixtureOptions truncatedMenu{};
    truncatedMenu.assetPointer = UINT32_MAX;
    truncatedMenu.includeAliasAsset = false;
    truncatedMenu.menuCount = 1;
    truncatedMenu.includeMenuBody = false;
    Run(MakeMenuListXFile(truncatedMenu), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    MenuListFixtureOptions invalidItemCount{};
    invalidItemCount.assetPointer = UINT32_MAX;
    invalidItemCount.includeAliasAsset = false;
    invalidItemCount.menuCount = 1;
    invalidItemCount.itemCount = -1;
    Run(MakeMenuListXFile(invalidItemCount), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "Menu/item pointer table") == 0);

    MenuListFixtureOptions truncatedItem{};
    truncatedItem.assetPointer = UINT32_MAX;
    truncatedItem.includeAliasAsset = false;
    truncatedItem.menuCount = 1;
    truncatedItem.includeItemBody = false;
    Run(MakeMenuListXFile(truncatedItem), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    MenuListFixtureOptions invalidExpressionCount{};
    invalidExpressionCount.assetPointer = UINT32_MAX;
    invalidExpressionCount.includeAliasAsset = false;
    invalidExpressionCount.menuCount = 1;
    invalidExpressionCount.expressionCount = -1;
    Run(MakeMenuListXFile(invalidExpressionCount), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "Menu/statement entry table") == 0);

    MenuListFixtureOptions truncatedExpression{};
    truncatedExpression.assetPointer = UINT32_MAX;
    truncatedExpression.includeAliasAsset = false;
    truncatedExpression.menuCount = 1;
    truncatedExpression.includeExpressionBodies = false;
    Run(MakeMenuListXFile(truncatedExpression), zone);
    assert(g_trace.generatedLoadFailed && !g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

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
    assert(g_trace.streamOffsets[0] == 0);
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
    assert(g_trace.streamOffsets[0] == 0 &&
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
    assert(std::strcmp(g_trace.stopStage, "stream/truncated string") == 0 ||
        std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

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
    assert(std::strcmp(g_trace.stopStage, "stream/truncated string") == 0 ||
        std::strcmp(g_trace.stopStage, "inflate/premature EOF") == 0);

    Reset(physInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_PHYSPRESET]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage, "publication/asset pool exhaustion") == 0);
    assert(DB_FindXAssetEntryCanonical(
        ASSET_TYPE_PHYSPRESET, "physics/gate3") == nullptr);

    const std::uint32_t xmodelHash = DB_HashForNameCanonical(
        "xmodel/gate3", ASSET_TYPE_XMODEL);
    Reset(xmodelInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_XMODEL]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[xmodelHash] == 0);
    assert(DB_GetFreeAssetEntryCount() == 32751);
    assert(DB_FindXAssetHeader(ASSET_TYPE_PHYSPRESET,
        "physics/xmodel_gate3").physPreset);
    std::uint32_t failedXModelInsertion = UINT32_MAX;
    std::memcpy(&failedXModelInsertion, zone.blocks[4].data + 32,
        sizeof(failedXModelInsertion));
    assert(failedXModelInsertion == 0);

    Reset(xmodelInsertAlias);
    g_assetEntryPool[16].next = nullptr;
    g_freeAssetEntryHead = &g_assetEntryPool[16];
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(db_hashTable[xmodelHash] == 0);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_XMODEL) == 1000);
    std::memcpy(&failedXModelInsertion, zone.blocks[4].data + 32,
        sizeof(failedXModelInsertion));
    assert(failedXModelInsertion == 0);

    const std::uint32_t weaponHash = DB_HashForNameCanonical(
        "weapon/gate3", ASSET_TYPE_WEAPON);
    Reset(weaponInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_WEAPON]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[weaponHash] == 0);
    assert(DB_FindXAssetHeader(ASSET_TYPE_XMODEL,
        "xmodel/weapon_gate3").model);
    std::uint32_t failedWeaponInsertion = UINT32_MAX;
    std::memcpy(&failedWeaponInsertion, zone.blocks[4].data + 32,
        sizeof(failedWeaponInsertion));
    assert(failedWeaponInsertion == 0);

    Reset(weaponInsertAlias);
    g_assetEntryPool[16].next = nullptr;
    g_freeAssetEntryHead = &g_assetEntryPool[16];
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(db_hashTable[weaponHash] == 0);
    assert(DB_FindXAssetHeader(ASSET_TYPE_XMODEL,
        "xmodel/weapon_gate3").model);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_WEAPON) == 128);
    std::memcpy(&failedWeaponInsertion, zone.blocks[4].data + 32,
        sizeof(failedWeaponInsertion));
    assert(failedWeaponInsertion == 0);

    const std::uint32_t xanimHash = DB_HashForNameCanonical(
        "xanim/gate3", ASSET_TYPE_XANIMPARTS);
    Reset(xanimInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_XANIMPARTS]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[xanimHash] == 0);
    std::uint32_t failedXAnimInsertion = UINT32_MAX;
    std::memcpy(&failedXAnimInsertion, zone.blocks[4].data + 32,
        sizeof(failedXAnimInsertion));
    assert(failedXAnimInsertion == 0);

    Reset(xanimInsertAlias);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(db_hashTable[xanimHash] == 0);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_XANIMPARTS) == 4096);
    std::memcpy(&failedXAnimInsertion, zone.blocks[4].data + 32,
        sizeof(failedXAnimInsertion));
    assert(failedXAnimInsertion == 0);

    const std::uint32_t stringTableHash = DB_HashForNameCanonical(
        "stringtable/gate3.csv", ASSET_TYPE_STRINGTABLE);
    Reset(stringTableFixture);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_STRINGTABLE]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[stringTableHash] == 0);

    Reset(stringTableFixture);
    g_freeAssetEntryHead = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(db_hashTable[stringTableHash] == 0);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_STRINGTABLE) == 50);

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

    const std::uint32_t fxImpactHash = DB_HashForNameCanonical(
        "impact/gate3", ASSET_TYPE_IMPACT_FX);
    Reset(fxImpactInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_IMPACT_FX]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[fxImpactHash] == 0);
    assert(DB_FindXAssetHeader(ASSET_TYPE_FX, "fx/impact_child").fx);
    assert(DB_GetFreeAssetEntryCount() == 32751);
    std::uint32_t failedFxImpactInsertion = UINT32_MAX;
    std::memcpy(&failedFxImpactInsertion, zone.blocks[4].data + 16,
        sizeof(failedFxImpactInsertion));
    assert(failedFxImpactInsertion == 0);

    Reset(fxImpactInsertAlias);
    g_assetEntryPool[16].next = nullptr;
    g_freeAssetEntryHead = &g_assetEntryPool[16];
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(db_hashTable[fxImpactHash] == 0);
    assert(DB_FindXAssetHeader(ASSET_TYPE_FX, "fx/impact_child").fx);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_IMPACT_FX) == 4);
    std::memcpy(&failedFxImpactInsertion, zone.blocks[4].data + 16,
        sizeof(failedFxImpactInsertion));
    assert(failedFxImpactInsertion == 0);

    const std::uint32_t lightHash = DB_HashForNameCanonical(
        "lights/gate3", ASSET_TYPE_LIGHT_DEF);
    Reset(lightInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_LIGHT_DEF]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[lightHash] == 0);
    assert(DB_FindXAssetHeader(ASSET_TYPE_IMAGE,
        "images/light_gate3").image);
    std::uint32_t failedLightInsertion = UINT32_MAX;
    std::memcpy(&failedLightInsertion, zone.blocks[4].data + 16,
        sizeof(failedLightInsertion));
    assert(failedLightInsertion == 0);

    Reset(lightInsertAlias);
    g_assetEntryPool[16].next = nullptr;
    g_freeAssetEntryHead = &g_assetEntryPool[16];
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(db_hashTable[lightHash] == 0);
    assert(DB_FindXAssetHeader(ASSET_TYPE_IMAGE,
        "images/light_gate3").image);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_LIGHT_DEF) == 32);
    std::memcpy(&failedLightInsertion, zone.blocks[4].data + 16,
        sizeof(failedLightInsertion));
    assert(failedLightInsertion == 0);

    const std::uint32_t menuHash = DB_HashForNameCanonical(
        "menu/gate3", ASSET_TYPE_MENU);
    const std::uint32_t menuListHash = DB_HashForNameCanonical(
        "menus/gate3", ASSET_TYPE_MENULIST);
    Reset(menuListInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_MENU]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin &&
        !g_trace.publicationEnd);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[menuHash] == 0 && db_hashTable[menuListHash] == 0);
    assert(DB_GetFreeAssetEntryCount() == 32752);
    std::uint32_t failedMenuListInsertion = UINT32_MAX;
    std::uint32_t failedMenuInsertion = UINT32_MAX;
    std::memcpy(&failedMenuListInsertion, zone.blocks[4].data + 16,
        sizeof(failedMenuListInsertion));
    std::memcpy(&failedMenuInsertion, zone.blocks[4].data + 40,
        sizeof(failedMenuInsertion));
    assert(failedMenuListInsertion == 0 && failedMenuInsertion == 0);

    Reset(menuListInsertAlias);
    *static_cast<void **>(DB_XAssetPool[ASSET_TYPE_MENULIST]) = nullptr;
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset pool exhaustion") == 0);
    assert(db_hashTable[menuListHash] == 0);
    assert(DB_FindXAssetHeader(ASSET_TYPE_MENU, "menu/gate3").menu);
    assert(DB_GetFreeAssetEntryCount() == 32751);
    std::memcpy(&failedMenuListInsertion, zone.blocks[4].data + 16,
        sizeof(failedMenuListInsertion));
    std::memcpy(&failedMenuInsertion, zone.blocks[4].data + 40,
        sizeof(failedMenuInsertion));
    assert(failedMenuListInsertion == 0);
    assert(failedMenuInsertion == reinterpret_cast<std::uint32_t>(
        DB_FindXAssetHeader(ASSET_TYPE_MENU, "menu/gate3").menu));

    Reset(menuListInsertAlias);
    g_assetEntryPool[16].next = nullptr;
    g_freeAssetEntryHead = &g_assetEntryPool[16];
    RunPrepared(zone);
    assert(g_trace.generatedLoadFailed && g_trace.publicationBegin);
    assert(std::strcmp(g_trace.stopStage,
        "publication/asset entry exhaustion") == 0);
    assert(db_hashTable[menuListHash] == 0);
    assert(DB_FindXAssetHeader(ASSET_TYPE_MENU, "menu/gate3").menu);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_MENULIST) == 128);
    std::memcpy(&failedMenuListInsertion, zone.blocks[4].data + 16,
        sizeof(failedMenuListInsertion));
    assert(failedMenuListInsertion == 0);

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

    // DB_Find marks only the requested root. Dependency marking is deferred
    // to the native unload mark walk, so a live sound can protect a curve in
    // another zone without making every lookup recursively in-use.
    Reset({});
    std::strncpy(g_zones[1].name, "sound-primary", sizeof(g_zones[1].name));
    g_zones[1].flags = 8;
    std::strncpy(g_zones[2].name, "sound-dependency", sizeof(g_zones[2].name));
    g_zones[2].flags = 2;
    static SndCurve dependencyCurve{};
    dependencyCurve.filename = "curve/dependency";
    dependencyCurve.knotCount = 2;
    dependencyCurve.knots[0][0] = 0.0f;
    dependencyCurve.knots[0][1] = 1.0f;
    dependencyCurve.knots[1][0] = 1.0f;
    dependencyCurve.knots[1][1] = 0.0f;
    DB_SetLoadingZoneIndex(2);
    const SndCurve *publishedDependencyCurve =
        DB_AddXAsset(ASSET_TYPE_SOUND_CURVE, {&dependencyCurve}).sndCurve;
    assert(publishedDependencyCurve);
    static snd_alias_t dependencyAlias{};
    dependencyAlias.volumeFalloffCurve = const_cast<SndCurve *>(
        publishedDependencyCurve);
    static snd_alias_list_t dependencySound{};
    dependencySound.aliasName = "sound/dependency";
    dependencySound.count = 1;
    dependencySound.head = &dependencyAlias;
    DB_SetLoadingZoneIndex(1);
    assert(DB_AddXAsset(ASSET_TYPE_SOUND, {&dependencySound}).sound);
    XAssetEntryPoolEntry *dependencyEntry = DB_FindXAssetEntryCanonical(
        ASSET_TYPE_SOUND_CURVE, "curve/dependency");
    XAssetEntryPoolEntry *rootEntry = DB_FindXAssetEntryCanonical(
        ASSET_TYPE_SOUND, "sound/dependency");
    assert(dependencyEntry && rootEntry && !dependencyEntry->entry.inuse &&
        !rootEntry->entry.inuse);
    assert(DB_FindXAssetHeader(ASSET_TYPE_SOUND, "sound/dependency").sound);
    assert(rootEntry->entry.inuse && !dependencyEntry->entry.inuse);
    const SndCurve *dependencyIdentity =
        dependencyEntry->entry.asset.header.sndCurve;
    assert(dependencyIdentity && dependencyIdentity != &dependencyCurve);
    const std::uint32_t dependencyFreeBefore =
        DB_GetAssetPoolFreeCount(ASSET_TYPE_SOUND_CURVE);
    DB_UnloadXZonesForFreeFlags(8);
    assert(DB_FindXAssetHeader(ASSET_TYPE_SOUND, "sound/dependency").data == nullptr);
    assert(DB_FindXAssetHeader(ASSET_TYPE_SOUND_CURVE,
        "curve/dependency").sndCurve == dependencyIdentity);
    assert(dependencyEntry->entry.inuse);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_SOUND_CURVE) ==
        dependencyFreeBefore);

    // Menu promotion uses the native dynamic-clone step: the stable primary
    // keeps its window/item focus state while the replacement body is copied.
    Reset({});
    std::strncpy(g_zones[1].name, "menu-primary", sizeof(g_zones[1].name));
    g_zones[1].flags = 8;
    std::strncpy(g_zones[2].name, "menu-override", sizeof(g_zones[2].name));
    g_zones[2].flags = 2;
    static itemDef_s oldMenuItem{};
    static itemDef_s newMenuItem{};
    oldMenuItem.window.name = "menu-item";
    oldMenuItem.window.dynamicFlags[0] = 7;
    newMenuItem.window.name = "menu-item";
    newMenuItem.window.dynamicFlags[0] = 0x40;
    static itemDef_s *oldMenuItems[] = {&oldMenuItem};
    static itemDef_s *newMenuItems[] = {&newMenuItem};
    static menuDef_t oldMenu{};
    static menuDef_t newMenu{};
    oldMenu.window.name = "menu/dynamic";
    oldMenu.window.dynamicFlags[0] = 7;
    oldMenu.itemCount = 1;
    oldMenu.items = oldMenuItems;
    newMenu.window.name = "menu/dynamic";
    newMenu.window.dynamicFlags[0] = 0x40;
    newMenu.itemCount = 1;
    newMenu.items = newMenuItems;
    DB_SetLoadingZoneIndex(1);
    const menuDef_t *menuIdentity =
        DB_AddXAsset(ASSET_TYPE_MENU, {&oldMenu}).menu;
    assert(menuIdentity);
    assert(DB_FindXAssetHeader(ASSET_TYPE_MENU,
        "menu/dynamic").menu == menuIdentity);
    DB_SetLoadingZoneIndex(2);
    assert(DB_AddXAsset(ASSET_TYPE_MENU, {&newMenu}).menu);
    DB_UnloadXZonesForFreeFlags(8);
    const menuDef_t *promotedMenu = DB_FindXAssetHeader(
        ASSET_TYPE_MENU, "menu/dynamic").menu;
    assert(promotedMenu == menuIdentity);
    assert(promotedMenu->window.dynamicFlags[0] == 7);
    assert(promotedMenu->items[0]->window.dynamicFlags[0] == 5);

    // The MenuList -> Menu -> Material/Sound -> SndCurve graph is marked only
    // when the in-use root is retired, matching native DB_UnloadXZone.
    Reset({});
    std::strncpy(g_zones[1].name, "menu-root", sizeof(g_zones[1].name));
    g_zones[1].flags = 8;
    std::strncpy(g_zones[2].name, "menu-dependencies", sizeof(g_zones[2].name));
    g_zones[2].flags = 2;
    static SndCurve menuCurve{};
    menuCurve.filename = "curve/menu-dependency";
    menuCurve.knotCount = 2;
    menuCurve.knots[0][1] = 1.0f;
    menuCurve.knots[1][0] = 1.0f;
    DB_SetLoadingZoneIndex(2);
    const SndCurve *publishedMenuCurve = DB_AddXAsset(
        ASSET_TYPE_SOUND_CURVE, {&menuCurve}).sndCurve;
    static snd_alias_t menuSoundAlias{};
    menuSoundAlias.volumeFalloffCurve = const_cast<SndCurve *>(
        publishedMenuCurve);
    static snd_alias_list_t menuSound{};
    menuSound.aliasName = "sound/menu-dependency";
    menuSound.count = 1;
    menuSound.head = &menuSoundAlias;
    const snd_alias_list_t *publishedMenuSound = DB_AddXAsset(
        ASSET_TYPE_SOUND, {&menuSound}).sound;
    static Material menuMaterial{};
    menuMaterial.info.name = "material/menu-dependency";
    const Material *publishedMenuMaterial = DB_AddXAsset(
        ASSET_TYPE_MATERIAL, {&menuMaterial}).material;
    assert(publishedMenuCurve && publishedMenuSound && publishedMenuMaterial);
    static itemDef_s dependencyItem{};
    dependencyItem.window.name = "dependency-item";
    dependencyItem.window.background = const_cast<Material *>(
        publishedMenuMaterial);
    dependencyItem.focusSound = const_cast<snd_alias_list_t *>(
        publishedMenuSound);
    static itemDef_s *dependencyItems[] = {&dependencyItem};
    static menuDef_t dependencyMenu{};
    dependencyMenu.window.name = "menu/dependency";
    dependencyMenu.itemCount = 1;
    dependencyMenu.items = dependencyItems;
    dependencyMenu.window.background = const_cast<Material *>(
        publishedMenuMaterial);
    DB_SetLoadingZoneIndex(1);
    const menuDef_t *publishedDependencyMenu = DB_AddXAsset(
        ASSET_TYPE_MENU, {&dependencyMenu}).menu;
    static menuDef_t *dependencyMenus[] = {
        const_cast<menuDef_t *>(publishedDependencyMenu)};
    static MenuList dependencyMenuList{};
    dependencyMenuList.name = "menus/dependency";
    dependencyMenuList.menuCount = 1;
    dependencyMenuList.menus = dependencyMenus;
    assert(DB_AddXAsset(ASSET_TYPE_MENULIST,
        {&dependencyMenuList}).menuList);
    XAssetEntryPoolEntry *menuRootEntry = DB_FindXAssetEntryCanonical(
        ASSET_TYPE_MENULIST, "menus/dependency");
    XAssetEntryPoolEntry *menuDependencyEntry = DB_FindXAssetEntryCanonical(
        ASSET_TYPE_MENU, "menu/dependency");
    XAssetEntryPoolEntry *menuMaterialEntry = DB_FindXAssetEntryCanonical(
        ASSET_TYPE_MATERIAL, "material/menu-dependency");
    XAssetEntryPoolEntry *menuSoundEntry = DB_FindXAssetEntryCanonical(
        ASSET_TYPE_SOUND, "sound/menu-dependency");
    XAssetEntryPoolEntry *menuCurveEntry = DB_FindXAssetEntryCanonical(
        ASSET_TYPE_SOUND_CURVE, "curve/menu-dependency");
    assert(menuRootEntry && menuDependencyEntry && menuMaterialEntry &&
        menuSoundEntry && menuCurveEntry);
    assert(!menuRootEntry->entry.inuse && !menuDependencyEntry->entry.inuse &&
        !menuMaterialEntry->entry.inuse && !menuSoundEntry->entry.inuse &&
        !menuCurveEntry->entry.inuse);
    assert(DB_FindXAssetHeader(ASSET_TYPE_MENULIST,
        "menus/dependency").menuList);
    assert(menuRootEntry->entry.inuse && !menuDependencyEntry->entry.inuse);
    const MenuList *retainedDependencyList = DB_FindXAssetHeader(
        ASSET_TYPE_MENULIST, "menus/dependency").menuList;
    assert(retainedDependencyList->menus[0] == publishedDependencyMenu);
    DB_UnloadXZonesForFreeFlags(8);
    assert(menuMaterialEntry->entry.inuse && menuSoundEntry->entry.inuse &&
        menuCurveEntry->entry.inuse);
    assert(DB_FindXAssetHeader(ASSET_TYPE_MATERIAL,
        "material/menu-dependency").material);
    assert(DB_FindXAssetHeader(ASSET_TYPE_SOUND,
        "sound/menu-dependency").sound);
    assert(DB_FindXAssetHeader(ASSET_TYPE_SOUND_CURVE,
        "curve/menu-dependency").sndCurve);

    // A surviving zone-backed root is traversed even before first lookup;
    // otherwise its released-zone dependency would become a stale pointer.
    Reset({});
    std::strncpy(g_zones[1].name, "retained-dependency", sizeof(g_zones[1].name));
    g_zones[1].flags = 8;
    std::strncpy(g_zones[2].name, "retained-root", sizeof(g_zones[2].name));
    g_zones[2].flags = 2;
    static SndCurve retainedCurve{};
    retainedCurve.filename = "curve/retained";
    retainedCurve.knotCount = 2;
    DB_SetLoadingZoneIndex(1);
    const SndCurve *publishedRetainedCurve = DB_AddXAsset(
        ASSET_TYPE_SOUND_CURVE, {&retainedCurve}).sndCurve;
    static SndCurve retainedDefault{};
    retainedDefault.filename = "default";
    retainedDefault.knotCount = 2;
    retainedDefault.knots[1][0] = 1.0f;
    DB_SetLoadingZoneIndex(2);
    assert(DB_AddXAsset(ASSET_TYPE_SOUND_CURVE,
        {&retainedDefault}).sndCurve);
    static snd_alias_t retainedAlias{};
    retainedAlias.volumeFalloffCurve = const_cast<SndCurve *>(
        publishedRetainedCurve);
    static snd_alias_list_t retainedSound{};
    retainedSound.aliasName = "sound/retained";
    retainedSound.count = 1;
    retainedSound.head = &retainedAlias;
    DB_SetLoadingZoneIndex(2);
    assert(DB_AddXAsset(ASSET_TYPE_SOUND, {&retainedSound}).sound);
    XAssetEntryPoolEntry *retainedCurveEntry = DB_FindXAssetEntryCanonical(
        ASSET_TYPE_SOUND_CURVE, "curve/retained");
    XAssetEntryPoolEntry *retainedRootEntry = DB_FindXAssetEntryCanonical(
        ASSET_TYPE_SOUND, "sound/retained");
    assert(retainedCurveEntry && retainedRootEntry &&
        !retainedCurveEntry->entry.inuse && !retainedRootEntry->entry.inuse);
    DB_UnloadXZonesForFreeFlags(8);
    assert(retainedCurveEntry->entry.inuse);
    const XAssetHeader retainedAfterUnload = DB_FindXAssetHeader(
        ASSET_TYPE_SOUND_CURVE, "curve/retained");
    assert(retainedAfterUnload.sndCurve == publishedRetainedCurve);
    assert(retainedAfterUnload.sndCurve->knotCount == 2);
    assert(std::strcmp(retainedAfterUnload.sndCurve->filename,
        "curve/retained") == 0);

    // Native unload keeps the pooled primary address while replacing a live
    // zone asset with the published per-type default. A later same-name load
    // must overwrite that default in place rather than creating a default
    // override.
    Reset(sndCurveInsertAlias);
    std::strncpy(g_zones[1].name, "map-one", sizeof(g_zones[1].name));
    g_zones[1].flags = 8;
    DB_SetLoadingZoneIndex(1);
    RunPrepared(zone);
    const XAssetHeader curveBefore = DB_FindXAssetHeader(
        ASSET_TYPE_SOUND_CURVE, "soundcurves/gate3");
    assert(curveBefore.sndCurve && curveBefore.sndCurve->knotCount == 3);
    const std::uintptr_t curveIdentity =
        reinterpret_cast<std::uintptr_t>(curveBefore.sndCurve);

    static SndCurve defaultCurve{};
    defaultCurve.filename = "default";
    defaultCurve.knotCount = 2;
    defaultCurve.knots[0][0] = 0.0f;
    defaultCurve.knots[0][1] = 1.0f;
    defaultCurve.knots[1][0] = 1.0f;
    defaultCurve.knots[1][1] = 0.0f;
    std::strncpy(g_zones[2].name, "code-post", sizeof(g_zones[2].name));
    g_zones[2].flags = 2;
    DB_SetLoadingZoneIndex(2);
    DB_AddXAsset(ASSET_TYPE_SOUND_CURVE, {&defaultCurve});
    static SndCurve lowerDefaultCurve{};
    lowerDefaultCurve.filename = "default";
    lowerDefaultCurve.knotCount = 4;
    std::strncpy(g_zones[5].name, "code-lower", sizeof(g_zones[5].name));
    g_zones[5].flags = 1;
    DB_SetLoadingZoneIndex(5);
    DB_AddXAsset(ASSET_TYPE_SOUND_CURVE, {&lowerDefaultCurve});

    DB_UnloadXZonesForFreeFlags(8);
    const XAssetHeader fallbackCurve = DB_FindXAssetHeader(
        ASSET_TYPE_SOUND_CURVE, "soundcurves/gate3");
    assert(reinterpret_cast<std::uintptr_t>(fallbackCurve.sndCurve) ==
        curveIdentity);
    assert(fallbackCurve.sndCurve->knotCount == 4);
    assert(std::strcmp(fallbackCurve.sndCurve->filename,
        "soundcurves/gate3") == 0);
    assert(g_zones[1].name[0] == '\0');

    std::strncpy(g_zones[3].name, "map-two", sizeof(g_zones[3].name));
    g_zones[3].flags = 8;
    DB_SetLoadingZoneIndex(3);
    g_filePosition = 0;
    RunPrepared(zone);
    const XAssetHeader curveReplacement = DB_FindXAssetHeader(
        ASSET_TYPE_SOUND_CURVE, "soundcurves/gate3");
    assert(reinterpret_cast<std::uintptr_t>(curveReplacement.sndCurve) ==
        curveIdentity);
    assert(curveReplacement.sndCurve->knotCount == 3);
    assert(DB_FindXAssetEntryCanonical(
        ASSET_TYPE_SOUND_CURVE, "soundcurves/gate3")->entry.nextOverride == 0);
    const std::uint32_t freeEntriesAfterReplacement =
        DB_GetFreeAssetEntryCount();
    const std::uint32_t freeCurvesAfterReplacement =
        DB_GetAssetPoolFreeCount(ASSET_TYPE_SOUND_CURVE);
    DB_UnloadXZonesForFreeFlags(8);
    assert(DB_FindXAssetHeader(ASSET_TYPE_SOUND_CURVE,
        "soundcurves/gate3").sndCurve->knotCount == 4);
    std::strncpy(g_zones[4].name, "map-three", sizeof(g_zones[4].name));
    g_zones[4].flags = 8;
    DB_SetLoadingZoneIndex(4);
    g_filePosition = 0;
    RunPrepared(zone);
    assert(DB_FindXAssetHeader(ASSET_TYPE_SOUND_CURVE,
        "soundcurves/gate3").sndCurve->knotCount == 3);
    DB_UnloadXZonesForFreeFlags(8);
    assert(DB_FindXAssetHeader(ASSET_TYPE_SOUND_CURVE,
        "soundcurves/gate3").sndCurve->knotCount == 4);
    assert(DB_GetFreeAssetEntryCount() == freeEntriesAfterReplacement);
    assert(DB_GetAssetPoolFreeCount(ASSET_TYPE_SOUND_CURVE) ==
        freeCurvesAfterReplacement);

    // WeaponDef may resolve a sound dependency before the real alias zone is
    // published. Native DB_FindXAssetHeader creates a named zone-0 default
    // in that case, and later publication promotes the real alias in place.
    Reset({});
    static snd_alias_list_t defaultSound{};
    defaultSound.aliasName = "null";
    g_zoneIndex = 0;
    const snd_alias_list_t *publishedDefaultSound = DB_AddXAsset(
        ASSET_TYPE_SOUND, {&defaultSound}).sound;
    assert(publishedDefaultSound);
    DB_SetLoadingZoneIndex(1);
    const snd_alias_list_t *placeholderSound = DB_FindXAssetHeader(
        ASSET_TYPE_SOUND, "sound/weapon_pending").sound;
    assert(placeholderSound && placeholderSound != publishedDefaultSound);
    assert(placeholderSound->count == 0 && placeholderSound->head == nullptr);
    XAssetEntryPoolEntry *placeholderEntry = DB_FindXAssetEntryCanonical(
        ASSET_TYPE_SOUND, "sound/weapon_pending");
    assert(placeholderEntry && placeholderEntry->entry.zoneIndex == 0 &&
        placeholderEntry->entry.inuse);
    static snd_alias_t publishedWeaponAlias{};
    publishedWeaponAlias.aliasName = "sound/weapon_pending";
    static snd_alias_list_t publishedWeaponSound{};
    publishedWeaponSound.aliasName = "sound/weapon_pending";
    publishedWeaponSound.count = 1;
    publishedWeaponSound.head = &publishedWeaponAlias;
    const snd_alias_list_t *promotedWeaponSound = DB_AddXAsset(
        ASSET_TYPE_SOUND, {&publishedWeaponSound}).sound;
    assert(promotedWeaponSound == placeholderSound);
    assert(promotedWeaponSound->count == 1 &&
        promotedWeaponSound->head == &publishedWeaponAlias);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_LOCALIZE_ENTRY,
        "localize/missing").data);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_RAWFILE,
        "rawfile/missing").data);

    // ClipMap is a true engine singleton, not a per-zone pool entry. A map
    // replacement must retire the previous name before reusing &cm; otherwise
    // the old hash bucket points at a body whose name was overwritten by the
    // replacement. Path separators are canonicalized consistently with the
    // DB hash used by the native lookup.
    Reset({});
    std::strncpy(g_zones[1].name, "killhouse", sizeof(g_zones[1].name));
    g_zones[1].flags = 1;
    DB_SetLoadingZoneIndex(1);
    static clipMap_t firstClipMap{};
    firstClipMap = {};
    firstClipMap.name = "maps/killhouse.d3dbsp";
    assert(DB_AddXAsset(ASSET_TYPE_CLIPMAP, {&firstClipMap}).clipMap ==
        DB_XAssetPool[ASSET_TYPE_CLIPMAP]);
    assert(DB_FindXAssetHeader(ASSET_TYPE_CLIPMAP,
        "maps\\killhouse.d3dbsp").clipMap ==
        DB_XAssetPool[ASSET_TYPE_CLIPMAP]);

    // Normal map teardown retires the old zone before its replacement is
    // queued. Keep this explicit in the regression so a stale singleton entry
    // cannot accidentally make a clean transition appear valid.
    DB_UnloadXZonesForFreeFlags(1);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_CLIPMAP,
        "maps/killhouse.d3dbsp").data);

    std::strncpy(g_zones[2].name, "cargoship", sizeof(g_zones[2].name));
    g_zones[2].flags = 8;
    DB_SetLoadingZoneIndex(2);
    static clipMap_t secondClipMap{};
    secondClipMap = {};
    secondClipMap.name = "maps/cargoship.d3dbsp";
    assert(DB_AddXAsset(ASSET_TYPE_CLIPMAP, {&secondClipMap}).clipMap ==
        DB_XAssetPool[ASSET_TYPE_CLIPMAP]);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_CLIPMAP,
        "maps/killhouse.d3dbsp").data);
    assert(DB_FindXAssetHeader(ASSET_TYPE_CLIPMAP,
        "maps\\cargoship.d3dbsp").clipMap ==
        DB_XAssetPool[ASSET_TYPE_CLIPMAP]);
    std::strncpy(g_zones[3].name, "campaign-next", sizeof(g_zones[3].name));
    g_zones[3].flags = 16;
    DB_SetLoadingZoneIndex(3);
    static clipMap_t thirdClipMap{};
    thirdClipMap = {};
    thirdClipMap.name = "maps/campaign_next.d3dbsp";
    assert(DB_AddXAsset(ASSET_TYPE_CLIPMAP, {&thirdClipMap}).clipMap ==
        DB_XAssetPool[ASSET_TYPE_CLIPMAP]);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_CLIPMAP,
        "maps/cargoship.d3dbsp").data);
    assert(DB_FindXAssetHeader(ASSET_TYPE_CLIPMAP,
        "maps\\campaign_next.d3dbsp").clipMap ==
        DB_XAssetPool[ASSET_TYPE_CLIPMAP]);
    DB_UnloadXZonesForFreeFlags(16);
    assert(!DB_FindXAssetHeader(ASSET_TYPE_CLIPMAP,
        "maps/campaign_next.d3dbsp").data);
    DB_UnloadXZonesForFreeFlags(8);

    std::printf("gate3-db-stream rawfile=published physpreset=published xmodel=published weapon=published xanim=published stringtable=published technique-set=published material=published image=published water=loaded sound-curve=published sound-alias=published loaded-sound=published font=published fx=published impact-fx=published comworld=published gfxworld=published light-def=published menu=published menu-list=published snddriver=canonical-noop localize=published insert=-2 alias=block4:16 technique=block4:36 direct-xstring=block4:18 technique-children=block0:0,block4:251 material-children=block0:0,block4:248 sound-curve-children=block0:0,block4:38 sound-alias-children=block0:0,block4:586 loaded-sound-children=block0:0,block4:44 font-children=block0:0,block4:80 fx-children=block0:0,block4:502 impact-fx-children=block0:0,block4:1640 comworld-children=block0:0,block4:204 gfxworld-children=block0:0,block1:0,block4:248 light-def-children=block0:0,block4:59 menu-children=block0:0,block4:880 localize-children=block0:0,block4:51 image-entry=16 material-entry=17 sound-curve-entry=16 sound-alias-entry=16 loaded-sound-entry=16 font-entry=16 fx-entry=16 impact-fx-entry=17 comworld-entry=16 gfxworld-entry=16 light-def-entry=17 menu-entry=16 menu-list-entry=17 localize-entry=16 free=32752->32750 zone=1 stop=code-post-complete\n");
    return 0;
}
