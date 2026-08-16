#include <web/web_retail_fastfile_census.h>
#include <web/web_fastfile_zone_registry.h>
#include <web/web_engine_xmodel_surface.h>
#include <web/web_engine_xmodel_material.h>
#include <web/web_engine_xmodel_draw_list.h>
#include <web/web_shader_compatibility.h>
#include <qcommon/cm_types.h>
#include "zlib_test_support.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
void Require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct SyntheticWeaponSoundLookup
{
    std::array<std::uint32_t, 3> pickup{};
    std::array<std::uint32_t, 3> bounce{};
    std::uint32_t pickupLookups = 0u;
    std::uint32_t bounceLookups = 0u;
};

snd_alias_list_t *LookupSyntheticWeaponSound(
    std::string_view name, void *userData) noexcept
{
    auto &lookup = *static_cast<SyntheticWeaponSoundLookup *>(userData);
    if (name == "web/pickup")
    {
        ++lookup.pickupLookups;
        return reinterpret_cast<snd_alias_list_t *>(lookup.pickup.data());
    }
    if (name == "web/bounce")
    {
        ++lookup.bounceLookups;
        return reinterpret_cast<snd_alias_list_t *>(lookup.bounce.data());
    }
    return nullptr;
}

void CollectSemanticTrace(
    const kisak::database::SemanticTraceEntry &entry, void *userData)
{
    static_cast<std::vector<kisak::database::SemanticTraceEntry> *>(userData)
        ->push_back(entry);
}

void TestSoundAliasCatalogLookupContract()
{
    using namespace kisak::fastfile;
    RetailSoundAliasCatalog catalog;
    auto pickupOwner = std::make_shared<RetailFastfileCensus>();
    pickupOwner->worldSoundAliasLists.emplace_back();
    RetailPublishedSoundAliasList &published =
        pickupOwner->worldSoundAliasLists.back();
    published.storage = std::make_shared<CanonicalSoundAliasListStorage>();
    published.storage->aliasName =
        std::make_shared<std::string>("Web/Pickup");
    published.storage->aliases =
        std::make_shared<std::vector<snd_alias_t>>(1u);
    published.asset = std::make_shared<snd_alias_list_t>();
    published.asset->aliasName = published.storage->aliasName->c_str();
    published.asset->head = published.storage->aliases->data();
    published.asset->count = 1;
    published.published = true;
    auto bounceOwner = std::make_shared<std::array<std::uint32_t, 3>>();
    std::weak_ptr<const void> pickupLifetime = pickupOwner;
    snd_alias_list_t *pickup = published.asset.get();
    snd_alias_list_t *bounce =
        reinterpret_cast<snd_alias_list_t *>(bounceOwner->data());

    Require(catalog.Publish("Web/Pickup", pickup, pickupOwner) ==
                RetailSoundAliasCatalogError::None &&
            catalog.Publish("web/bounce", bounce, bounceOwner) ==
                RetailSoundAliasCatalogError::None &&
            catalog.EntryCount() == 2u &&
            catalog.TotalNameBytes() == 20u,
        "sound catalog retains canonical cross-zone entries");
    pickupOwner.reset();
    Require(!pickupLifetime.expired() &&
            catalog.Find("web/pickup") == pickup &&
            catalog.Find("web/pickup")->head ==
                published.storage->aliases->data() &&
            catalog.Find("WEB/BOUNCE") == bounce &&
            catalog.Find("web/missing") == nullptr,
        "sound catalog indexes the exact zone-owned canonical graph without copying it");
    const RetailSoundAliasLookup lookup = catalog.Lookup();
    Require(lookup.function != nullptr &&
            lookup.function("WEB/PICKUP", lookup.userData) == pickup &&
            lookup.function("web/missing", lookup.userData) == nullptr,
        "sound catalog exposes the WeaponDef lookup contract directly");
    auto nullOwner = std::make_shared<std::array<std::uint32_t, 3>>();
    snd_alias_list_t *nullSound =
        reinterpret_cast<snd_alias_list_t *>(nullOwner->data());
    Require(catalog.Publish("null", nullSound, nullOwner) ==
                RetailSoundAliasCatalogError::None &&
            lookup.function("web/missing", lookup.userData) == nullSound &&
            catalog.Find("web/missing") == nullptr,
        "lookup uses the indexed zone-owned native sound default without changing exact Find semantics");
    Require(catalog.Publish("web/PICKUP", bounce, bounceOwner) ==
                RetailSoundAliasCatalogError::Duplicate &&
            catalog.Publish({}, bounce, bounceOwner) ==
                RetailSoundAliasCatalogError::InvalidArgument,
        "sound catalog rejects ambiguous or invalid publication");
    catalog.Reset();
    Require(catalog.EntryCount() == 0u && pickupLifetime.expired(),
        "sound catalog reset releases retained zone ownership");
}

void PutU32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16u));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24u));
}

void PutU16(std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
}

void SetU32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    bytes[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
    bytes[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
}

void AppendString(std::vector<std::uint8_t> &bytes, const std::string &value)
{
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0u);
}

void PutU16At(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

void SetF32(std::vector<std::uint8_t> &bytes, std::size_t offset, float value)
{
    SetU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

std::vector<std::uint32_t> BuildShaderProgram(bool vertex)
{
    struct Binding { const char *name; std::uint16_t set; std::uint16_t index; std::uint16_t count; };
    const std::vector<Binding> bindings = vertex
        ? std::vector<Binding>{{"viewProjectionMatrix", 2u, 0u, 4u},
              {"worldMatrix", 2u, 4u, 4u}}
        : std::vector<Binding>{{"colorMapSampler", 3u, 0u, 1u}};
    const std::uint32_t version = vertex ? 0xfffe0101u : 0xffff0200u;
    std::vector<std::uint8_t> table(28u + bindings.size() * 20u + bindings.size() * 16u, 0u);
    const auto appendTableString = [&](const char *value) {
        const std::uint32_t offset = static_cast<std::uint32_t>(table.size());
        while (*value != '\0') table.push_back(static_cast<std::uint8_t>(*value++));
        table.push_back(0u);
        return offset;
    };
    const std::uint32_t creatorOffset = appendTableString("web synthetic");
    const std::uint32_t targetOffset = appendTableString(vertex ? "vs_1_1" : "ps_2_0");
    std::vector<std::uint32_t> nameOffsets;
    for (const Binding &binding : bindings) nameOffsets.push_back(appendTableString(binding.name));
    while (table.size() % 4u != 0u) table.push_back(0u);
    SetU32(table, 0u, 28u);
    SetU32(table, 4u, creatorOffset);
    SetU32(table, 8u, version);
    SetU32(table, 12u, static_cast<std::uint32_t>(bindings.size()));
    SetU32(table, 16u, 28u);
    SetU32(table, 24u, targetOffset);
    const std::uint32_t typeBase = 28u + static_cast<std::uint32_t>(bindings.size()) * 20u;
    for (std::size_t index = 0u; index < bindings.size(); ++index)
    {
        const std::size_t info = 28u + index * 20u;
        SetU32(table, info, nameOffsets[index]);
        PutU16At(table, info + 4u, bindings[index].set);
        PutU16At(table, info + 6u, bindings[index].index);
        PutU16At(table, info + 8u, bindings[index].count);
        SetU32(table, info + 12u, typeBase + static_cast<std::uint32_t>(index) * 16u);
        const std::size_t type = typeBase + index * 16u;
        PutU16At(table, type, vertex ? 3u : 4u);
        PutU16At(table, type + 2u, vertex ? 3u : 12u);
        PutU16At(table, type + 4u, vertex ? 4u : 1u);
        PutU16At(table, type + 6u, vertex ? 4u : 1u);
        PutU16At(table, type + 8u, 1u);
    }
    std::vector<std::uint32_t> words;
    words.push_back(version);
    words.push_back((1u + static_cast<std::uint32_t>(table.size() / 4u)) << 16u | 0xfffeu);
    words.push_back(0x42415443u);
    for (std::size_t offset = 0u; offset < table.size(); offset += 4u)
        words.push_back(static_cast<std::uint32_t>(table[offset]) |
            static_cast<std::uint32_t>(table[offset + 1u]) << 8u |
            static_cast<std::uint32_t>(table[offset + 2u]) << 16u |
            static_cast<std::uint32_t>(table[offset + 3u]) << 24u);
    const auto instruction = [&](std::uint32_t opcode, std::uint32_t operands) {
        words.push_back(vertex ? opcode : opcode | operands << 24u);
        for (std::uint32_t index = 0u; index < operands; ++index) words.push_back(0u);
    };
    if (vertex)
    {
        instruction(0x51u, 5u);
        for (int index = 0; index < 3; ++index) instruction(0x1fu, 2u);
        instruction(0x04u, 4u);
        for (int index = 0; index < 8; ++index) instruction(0x09u, 3u);
        for (int index = 0; index < 2; ++index) instruction(0x01u, 2u);
    }
    else
    {
        for (int index = 0; index < 3; ++index) instruction(0x1fu, 2u);
        instruction(0x42u, 3u);
        instruction(0x05u, 3u);
        instruction(0x01u, 2u);
    }
    words.push_back(0x0000ffffu);
    return words;
}

std::vector<std::uint8_t> ShaderBytes(const std::vector<std::uint32_t> &words)
{
    std::vector<std::uint8_t> bytes;
    for (const std::uint32_t word : words) PutU32(bytes, word);
    return bytes;
}

std::vector<std::uint8_t> BuildInflated()
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 1378265u);
    PutU32(bytes, 950499u);
    const std::array<std::uint32_t, 9> blocks = {{
        498816u, 0u, 0u, 0u, 407412u, 0u, 0u, 4224u, 480u,
    }};
    for (const std::uint32_t block : blocks) PutU32(bytes, block);
    PutU32(bytes, 3u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 5u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0xffffffffu);
    AppendString(bytes, "end");
    AppendString(bytes, "tag_origin");
    const std::array<std::uint32_t, 5> types = {{5u, 5u, 4u, 22u, 32u}};
    const std::array<std::uint32_t, 5> references = {{
        0xffffffffu, 0xffffffffu, 0xffffffffu, 0x40000011u, 0u,
    }};
    for (std::size_t index = 0u; index < types.size(); ++index)
    {
        PutU32(bytes, types[index]);
        PutU32(bytes, references[index]);
    }
    std::vector<std::uint8_t> techniqueSet(148u, 0u);
    SetU32(techniqueSet, 0u, 0xffffffffu);
    SetU32(techniqueSet, 12u + 4u * 4u, 0xffffffffu);
    bytes.insert(bytes.end(), techniqueSet.begin(), techniqueSet.end());
    AppendString(bytes, "web/synthetic_techset");

    PutU32(bytes, 0xffffffffu);
    bytes.push_back(0u);
    bytes.push_back(0u);
    bytes.push_back(1u);
    bytes.push_back(0u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0xffffffffu);
    bytes.push_back(1u);
    bytes.push_back(1u);
    bytes.push_back(1u);
    bytes.push_back(0u);
    PutU32(bytes, 0xffffffffu);

    std::vector<std::uint8_t> vertexDeclaration(100u, 0u);
    vertexDeclaration[0] = 3u;
    vertexDeclaration[6] = 1u;
    vertexDeclaration[7] = 2u;
    vertexDeclaration[8] = 2u;
    vertexDeclaration[9] = 4u;
    bytes.insert(bytes.end(), vertexDeclaration.begin(), vertexDeclaration.end());

    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0u);
    PutU32(bytes, 0xffffffffu);
    const auto vertexProgram = BuildShaderProgram(true);
    PutU32(bytes, static_cast<std::uint32_t>(vertexProgram.size()));
    AppendString(bytes, "web_synthetic_vs");
    for (const std::uint32_t word : vertexProgram) PutU32(bytes, word);

    PutU32(bytes, 0x400000edu);
    PutU32(bytes, 0u);
    PutU32(bytes, 0xffffffffu);
    const auto pixelProgram = BuildShaderProgram(false);
    PutU32(bytes, static_cast<std::uint32_t>(pixelProgram.size()));
    for (const std::uint32_t word : pixelProgram) PutU32(bytes, word);
    PutU32(bytes, 0x00040003u);
    PutU32(bytes, 0x0400003cu);
    PutU32(bytes, 0x00000003u);
    PutU32(bytes, 0x0400004cu);
    PutU32(bytes, 0x00000002u);
    PutU32(bytes, 0xa0ab1041u);
    AppendString(bytes, "web_synthetic2d");

    std::vector<std::uint8_t> materialTechniqueSet(148u, 0u);
    SetU32(materialTechniqueSet, 0u, 0xffffffffu);
    SetU32(materialTechniqueSet, 12u + 4u * 4u, 0xffffffffu);
    bytes.insert(bytes.end(), materialTechniqueSet.begin(), materialTechniqueSet.end());
    AppendString(bytes, "web/material_techset");
    PutU32(bytes, 0x40000385u);
    PutU16(bytes, 0u);
    PutU16(bytes, 1u);
    PutU32(bytes, 0x40000079u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0xffffffffu);
    bytes.push_back(1u);
    bytes.push_back(1u);
    bytes.push_back(1u);
    bytes.push_back(0u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0x400000edu);
    PutU32(bytes, 0u);
    PutU32(bytes, 0xffffffffu);
    PutU16(bytes, static_cast<std::uint16_t>(vertexProgram.size()));
    PutU16(bytes, 1u);
    for (const std::uint32_t word : vertexProgram) PutU32(bytes, word);
    PutU32(bytes, 0x400000edu);
    PutU32(bytes, 0u);
    PutU32(bytes, 0xffffffffu);
    PutU16(bytes, static_cast<std::uint16_t>(pixelProgram.size()));
    PutU16(bytes, 1u);
    for (const std::uint32_t word : pixelProgram) PutU32(bytes, word);
    for (const std::uint32_t word : std::array<std::uint32_t, 6>{{
        0x00040003u, 0x0400003cu, 0x00000003u,
        0x0400004cu, 0x00000002u, 0xa0ab1041u}})
        PutU32(bytes, word);

    std::vector<std::uint8_t> material(80u, 0u);
    SetU32(material, 0u, 0xffffffffu);
    material[5u] = 43u;
    material[6u] = 1u;
    material[7u] = 1u;
    material[58u] = 1u;
    material[60u] = 1u;
    material[62u] = 3u;
    SetU32(material, 64u, 0x40000029u);
    SetU32(material, 68u, 0xffffffffu);
    SetU32(material, 76u, 0xffffffffu);
    bytes.insert(bytes.end(), material.begin(), material.end());
    AppendString(bytes, "web_cursor");
    PutU32(bytes, 0xa0ab1041u);
    bytes.push_back('c');
    bytes.push_back('p');
    bytes.push_back(0xe2u);
    bytes.push_back(0u);
    PutU32(bytes, 0xffffffffu);

    std::vector<std::uint8_t> image(36u, 0u);
    SetU32(image, 0u, 3u);
    SetU32(image, 4u, 0xfffffffeu);
    image[10u] = 1u;
    PutU16At(image, 24u, 4u);
    PutU16At(image, 26u, 4u);
    PutU16At(image, 28u, 1u);
    image[30u] = 3u;
    SetU32(image, 32u, 0xffffffffu);
    bytes.insert(bytes.end(), image.begin(), image.end());
    AppendString(bytes, "synthetic_engine_asset");
    bytes.push_back(1u);
    bytes.push_back(2u);
    PutU16(bytes, 4u);
    PutU16(bytes, 4u);
    PutU16(bytes, 1u);
    PutU32(bytes, 0x31545844u);
    PutU32(bytes, 0u);
    bytes.insert(bytes.end(), 8u, 0u);
    return bytes;
}

std::vector<std::uint8_t> BuildFile(std::vector<std::uint8_t> inflated = BuildInflated())
{
    uLongf compressedSize = KisakTestCompressBound(
        static_cast<uLong>(inflated.size()));
    std::vector<std::uint8_t> compressed(compressedSize);
    Require(compress2(
        compressed.data(), &compressedSize, inflated.data(),
        static_cast<uLong>(inflated.size()), Z_BEST_COMPRESSION) == Z_OK,
        "synthetic census fixture compresses");
    compressed.resize(compressedSize);
    std::vector<std::uint8_t> file = {
        'I','W','f','f','u','1','0','0', 5u,0u,0u,0u,
    };
    file.insert(file.end(), compressed.begin(), compressed.end());
    return file;
}

std::uint32_t Block4Token(std::uint32_t offset);

std::vector<std::uint8_t> BuildMinimalGfxWorldZoneInflated(
    std::uint32_t rootReference = 0xffffffffu,
    bool appendPriorAlias = false,
    bool allocateRuntimeTextures = true)
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 1024u * 1024u);
    PutU32(bytes, 0u);
    for (const std::uint32_t block :
         std::array<std::uint32_t, 9>{{1024u * 1024u, 4096u, 0u, 0u,
                                      1024u * 1024u, 0u, 0u, 0u, 0u}})
        PutU32(bytes, block);
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    const std::uint32_t assetCount = appendPriorAlias ? 3u : 2u;
    PutU32(bytes, assetCount);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, ASSET_TYPE_GFXWORLD);
    PutU32(bytes, rootReference);
    if (appendPriorAlias)
    {
        PutU32(bytes, ASSET_TYPE_GFXWORLD);
        PutU32(bytes, Block4Token(assetCount * 8u));
    }
    PutU32(bytes, ASSET_TYPE_RAWFILE);
    PutU32(bytes, 0u);
    if (rootReference == 0u) return bytes;

    std::vector<std::uint8_t> world(732u, 0u);
    SetU32(world, 0u, 0xffffffffu);
    SetU32(world, 4u, 0xffffffffu);
    SetU32(world, 0xdcu, 1u); // one sun primary light, no non-sun lights
    if (allocateRuntimeTextures)
    {
        SetU32(world, 0xe4u, 1u); // reflectionProbeCount
        SetU32(world, 0xecu, 1u); // block-1 reflection runtime slots
        SetU32(world, 0x108u, 1u); // lightmapCount
        SetU32(world, 0x148u, 1u); // block-1 primary runtime slots
        SetU32(world, 0x14cu, 1u); // block-1 secondary runtime slots
    }
    bytes.insert(bytes.end(), world.begin(), world.end());
    AppendString(bytes, "maps/mp/web_minimal_gfxworld.d3dbsp");
    AppendString(bytes, "web_minimal_gfxworld");
    return bytes;
}

std::vector<std::uint8_t> BuildLocalizeZoneInflated()
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 4096u);
    PutU32(bytes, 0u);
    for (const std::uint32_t block :
         std::array<std::uint32_t, 9>{{4096u, 0u, 0u, 0u, 4096u,
                                      0u, 0u, 0u, 0u}})
    {
        PutU32(bytes, block);
    }
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    PutU32(bytes, 4u);
    PutU32(bytes, 0xffffffffu);
    for (const std::pair<std::uint32_t, std::uint32_t> asset : {
             std::pair{22u, 0xffffffffu},
             std::pair{22u, 0xffffffffu},
             std::pair{4u, 0xffffffffu},
             std::pair{32u, 0u},
         })
    {
        PutU32(bytes, asset.first);
        PutU32(bytes, asset.second);
    }

    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0xffffffffu);
    AppendString(bytes, "Hello");
    AppendString(bytes, "KEY_ONE");
    PutU32(bytes, 0x40000021u); // block-4 offset 32: first value payload
    PutU32(bytes, 0xffffffffu);
    AppendString(bytes, "KEY_TWO");
    std::vector<std::uint8_t> material(80u, 0u);
    SetU32(material, 0u, 0xffffffffu);
    bytes.insert(bytes.end(), material.begin(), material.end());
    AppendString(bytes, "web/localize_boundary_material");
    return bytes;
}

std::vector<std::uint8_t> BuildClipMapZoneInflated(
    bool withArrays = false,
    bool withMapEnts = false,
    bool withAlias = false)
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 4096u);
    PutU32(bytes, 0u);
    for (const std::uint32_t block :
         std::array<std::uint32_t, 9>{{4096u, 0u, 0u, 0u, 4096u,
                                      0u, 0u, 0u, 0u}})
        PutU32(bytes, block);
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    PutU32(bytes, withAlias ? 3u : 2u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 10u);
    PutU32(bytes, withAlias ? 0xfffffffeu : 0xffffffffu);
    if (withAlias)
    {
        PutU32(bytes, 10u);
        PutU32(bytes, 0x40000019u); // block-4 offset 24: insertion cell
    }
    PutU32(bytes, 32u);
    PutU32(bytes, 0u);

    std::vector<std::uint8_t> clipMap(284u, 0u);
    SetU32(clipMap, 0u, 0xffffffffu);
    SetU32(clipMap, 4u, 1u);
    if (withArrays)
    {
        for (const std::size_t offset : {8u, 16u, 24u, 32u, 40u, 48u,
                 56u, 64u, 72u, 80u, 88u, 96u, 108u, 116u, 124u,
                 132u, 148u, 152u})
            SetU32(clipMap, offset, offset == 148u || offset == 152u ? 2u : 1u);
        for (const std::size_t offset : {12u, 20u, 28u, 36u, 44u, 52u,
                 60u, 68u, 76u, 84u, 92u, 100u, 104u, 112u, 120u,
                 128u, 136u, 156u})
            SetU32(clipMap, offset, 0xffffffffu);
    }
    if (withMapEnts) SetU32(clipMap, 164u, 0xffffffffu);
    SetU32(clipMap, 280u, 0x12345678u);
    bytes.insert(bytes.end(), clipMap.begin(), clipMap.end());
    AppendString(bytes, "maps/mp/web_clipmap.d3dbsp");
    if (withArrays)
    {
        std::vector<std::uint8_t> plane(20u, 0u);
        SetF32(plane, 0u, 1.0f);
        plane[16u] = 0u;
        plane[17u] = 0u;
        bytes.insert(bytes.end(), plane.begin(), plane.end());
        bytes.insert(bytes.end(), 80u, 0u); // cStaticModel_s
        std::vector<std::uint8_t> material(72u, 0u);
        std::copy_n("clip_material", 13u, material.begin());
        SetU32(material, 64u, 7u);
        SetU32(material, 68u, 9u);
        bytes.insert(bytes.end(), material.begin(), material.end());
        bytes.insert(bytes.end(), 12u, 0u); // cbrushside_t
        bytes.push_back(3u); // brush edge
        bytes.insert(bytes.end(), 8u, 0u); // cNode_t
        std::vector<std::uint8_t> leaf(44u, 0u);
        SetU32(leaf, 4u, 5u);
        bytes.insert(bytes.end(), leaf.begin(), leaf.end());
        PutU16(bytes, 4u);
        bytes.insert(bytes.end(), 20u, 0u); // cLeafBrushNode_s
        PutU32(bytes, 6u);
        for (const float value : {1.0f, 2.0f, 3.0f})
            PutU32(bytes, std::bit_cast<std::uint32_t>(value));
        PutU16(bytes, 0u);
        PutU16(bytes, 1u);
        PutU16(bytes, 2u);
        bytes.insert(bytes.end(), {1u, 0u, 0u, 0u});
        std::vector<std::uint8_t> border(28u, 0u);
        SetF32(border, 20u, 4.0f);
        bytes.insert(bytes.end(), border.begin(), border.end());
        bytes.insert(bytes.end(), 12u, 0u); // CollisionPartition
        std::vector<std::uint8_t> tree(32u, 0u);
        PutU16At(tree, 24u, 2u);
        bytes.insert(bytes.end(), tree.begin(), tree.end());
        std::vector<std::uint8_t> cmodel(72u, 0u);
        SetF32(cmodel, 24u, 8.0f);
        bytes.insert(bytes.end(), cmodel.begin(), cmodel.end());
        bytes.insert(bytes.end(), {0xaau, 0xbbu, 0xccu, 0xddu});
    }
    if (withMapEnts)
    {
        PutU32(bytes, 0xffffffffu);
        PutU32(bytes, 0xffffffffu);
        PutU32(bytes, 4u);
        AppendString(bytes, "maps/mp/web_clipmap_entities.d3dbsp");
        bytes.insert(bytes.end(), {'{', ' ', '}', 0u});
    }
    return bytes;
}

std::uint32_t Block4Token(std::uint32_t offset)
{
    return 0x40000000u | (offset + 1u);
}

std::uint32_t Align4(std::uint32_t value)
{
    return (value + 3u) & ~3u;
}

std::vector<std::uint8_t> BuildComWorldZoneInflated(
    std::uint32_t rootReference = 0xffffffffu,
    bool priorRootAlias = false,
    bool multipleLights = false,
    bool nonnullZeroLightArray = false,
    std::string worldName = "maps/mp/web_comworld.d3dbsp",
    std::string firstLightName = "light/web_primary")
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 4096u);
    PutU32(bytes, 0u);
    for (const std::uint32_t block :
         std::array<std::uint32_t, 9>{{4096u, 0u, 0u, 0u, 4096u,
                                      0u, 0u, 0u, 0u}})
        PutU32(bytes, block);
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    const std::uint32_t assetCount = priorRootAlias ? 3u : 2u;
    PutU32(bytes, assetCount);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 12u);
    PutU32(bytes, rootReference);
    if (priorRootAlias)
    {
        PutU32(bytes, 12u);
        // Load_ComWorldPtr(-2) creates DB_InsertPointer immediately after the
        // complete block-4 XAsset table.
        PutU32(bytes, Block4Token(assetCount * 8u));
    }
    PutU32(bytes, 32u);
    PutU32(bytes, 0u);

    if (rootReference == 0u) return bytes;

    std::vector<std::uint8_t> world(16u, 0u);
    SetU32(world, 0u, 0xffffffffu);
    SetU32(world, 4u, 1u);
    SetU32(world, 8u, multipleLights ? 3u : 0u);
    SetU32(world, 12u,
        multipleLights || nonnullZeroLightArray ? 1u : 0u);
    bytes.insert(bytes.end(), world.begin(), world.end());

    const std::uint32_t tableBytes = assetCount * 8u;
    const std::uint32_t insertionBytes = rootReference == 0xfffffffeu ? 4u : 0u;
    const std::uint32_t nameOffset = tableBytes + insertionBytes;
    AppendString(bytes, worldName);
    if (!multipleLights) return bytes;

    const std::uint32_t arrayOffset = Align4(
        nameOffset + static_cast<std::uint32_t>(worldName.size()) + 1u);
    const std::uint32_t firstDefNameOffset = arrayOffset + 3u * 68u;
    std::vector<std::uint8_t> lights(3u * 68u, 0u);
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        const std::size_t offset = static_cast<std::size_t>(index) * 68u;
        lights[offset] = static_cast<std::uint8_t>(index + 1u);
        lights[offset + 1u] = static_cast<std::uint8_t>(index & 1u);
        lights[offset + 2u] = static_cast<std::uint8_t>(8u + index);
        SetF32(lights, offset + 4u, 0.25f + static_cast<float>(index));
        SetF32(lights, offset + 16u, -1.0f + static_cast<float>(index));
        SetF32(lights, offset + 28u, 10.0f + static_cast<float>(index));
        SetF32(lights, offset + 40u, 128.0f + static_cast<float>(index));
    }
    SetU32(lights, 64u, 0xffffffffu);
    SetU32(lights, 68u + 64u, Block4Token(firstDefNameOffset));
    SetU32(lights, 2u * 68u + 64u, Block4Token(firstDefNameOffset + 6u));
    bytes.insert(bytes.end(), lights.begin(), lights.end());
    AppendString(bytes, firstLightName);
    return bytes;
}

std::vector<std::uint8_t> BuildLightDefZoneInflated(
    std::uint32_t rootReference = 0xffffffffu,
    bool priorRootAlias = false,
    std::uint32_t imageReference = 0u,
    bool malformedImage = false)
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 4096u);
    PutU32(bytes, 0u);
    for (const std::uint32_t block :
         std::array<std::uint32_t, 9>{{4096u, 0u, 0u, 0u, 4096u,
                                      0u, 0u, 0u, 0u}})
        PutU32(bytes, block);
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    const std::uint32_t assetCount = priorRootAlias ? 3u : 2u;
    PutU32(bytes, assetCount);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 17u);
    PutU32(bytes, rootReference);
    if (priorRootAlias)
    {
        PutU32(bytes, 17u);
        PutU32(bytes, Block4Token(assetCount * 8u));
    }
    PutU32(bytes, 32u);
    PutU32(bytes, 0u);
    if (rootReference == 0u) return bytes;

    std::vector<std::uint8_t> lightDef(16u, 0u);
    SetU32(lightDef, 0u, 0xffffffffu);
    SetU32(lightDef, 4u, imageReference);
    lightDef[8u] = 7u;
    SetU32(lightDef, 12u, 42u);
    bytes.insert(bytes.end(), lightDef.begin(), lightDef.end());
    AppendString(bytes, "light/web_attenuation");

    if (imageReference == 0xffffffffu || imageReference == 0xfffffffeu)
    {
        std::vector<std::uint8_t> image(36u, 0u);
        SetU32(image, 0u, 3u);
        SetU32(image, 4u, 0xffffffffu);
        PutU16At(image, 24u, 4u);
        PutU16At(image, 26u, 4u);
        PutU16At(image, 28u, 1u);
        SetU32(image, 32u, 0xffffffffu);
        bytes.insert(bytes.end(), image.begin(), image.end());
        AppendString(bytes, "web/light_attenuation_image");
        bytes.push_back(1u);
        bytes.push_back(2u);
        PutU16(bytes, malformedImage ? 8u : 4u);
        PutU16(bytes, 4u);
        PutU16(bytes, 1u);
        PutU32(bytes, 0x31545844u);
        PutU32(bytes, 0u);
    }
    return bytes;
}

std::vector<std::uint8_t> BuildLightDefStringAliasZoneInflated()
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 4096u);
    PutU32(bytes, 0u);
    for (const std::uint32_t block :
         std::array<std::uint32_t, 9>{{4096u, 0u, 0u, 0u, 4096u,
                                      0u, 0u, 0u, 0u}})
        PutU32(bytes, block);
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    PutU32(bytes, 4u);
    PutU32(bytes, 0xffffffffu);
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        PutU32(bytes, 17u);
        PutU32(bytes, 0xffffffffu);
    }
    PutU32(bytes, 32u);
    PutU32(bytes, 0u);

    constexpr std::uint32_t firstNameOffset = 32u;
    const auto appendBody = [&](std::uint32_t nameToken) {
        std::vector<std::uint8_t> lightDef(16u, 0u);
        SetU32(lightDef, 0u, nameToken);
        bytes.insert(bytes.end(), lightDef.begin(), lightDef.end());
    };
    appendBody(0xffffffffu);
    AppendString(bytes, "light/web_name");
    appendBody(Block4Token(firstNameOffset));
    appendBody(Block4Token(firstNameOffset + 6u));
    return bytes;
}

std::vector<std::uint8_t> BuildLightDefImageAliasZoneInflated()
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 4096u);
    PutU32(bytes, 0u);
    for (const std::uint32_t block :
         std::array<std::uint32_t, 9>{{4096u, 0u, 0u, 0u, 4096u,
                                      0u, 0u, 0u, 0u}})
        PutU32(bytes, block);
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    PutU32(bytes, 3u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 17u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 17u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 32u);
    PutU32(bytes, 0u);

    std::vector<std::uint8_t> first(16u, 0u);
    SetU32(first, 0u, 0xffffffffu);
    SetU32(first, 4u, 0xfffffffeu);
    bytes.insert(bytes.end(), first.begin(), first.end());
    AppendString(bytes, "light/web_attenuation");
    std::vector<std::uint8_t> image(36u, 0u);
    SetU32(image, 0u, 3u);
    SetU32(image, 4u, 0xffffffffu);
    PutU16At(image, 24u, 4u);
    PutU16At(image, 26u, 4u);
    PutU16At(image, 28u, 1u);
    SetU32(image, 32u, 0xffffffffu);
    bytes.insert(bytes.end(), image.begin(), image.end());
    AppendString(bytes, "web/light_attenuation_image");
    bytes.push_back(1u);
    bytes.push_back(2u);
    PutU16(bytes, 4u);
    PutU16(bytes, 4u);
    PutU16(bytes, 1u);
    PutU32(bytes, 0x31545844u);
    PutU32(bytes, 0u);

    std::vector<std::uint8_t> second(16u, 0u);
    SetU32(second, 0u, 0xffffffffu);
    // Table bytes [0,24), first name [24,46), image insertion cell [48,52).
    SetU32(second, 4u, Block4Token(48u));
    bytes.insert(bytes.end(), second.begin(), second.end());
    AppendString(bytes, "light/web_attenuation_alias");
    return bytes;
}

std::vector<std::uint8_t> BuildWorldTechniquePrefixInflated(
    bool firstDependency = false,
    bool secondDependency = false,
    bool invalidSecondHeader = false)
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 2000000u);
    PutU32(bytes, 1000000u);
    for (const std::uint32_t block :
        std::array<std::uint32_t, 9>{{1024u, 0u, 0u, 0u, 1024u, 0u, 0u, 0u, 0u}})
        PutU32(bytes, block);
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    const std::array<std::uint32_t, 7> types = {{5u, 5u, 3u, 2u, 31u, 16u, 32u}};
    const std::array<std::uint32_t, 7> references = {{
        0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
        0xffffffffu, 0xffffffffu, 0u,
    }};
    PutU32(bytes, static_cast<std::uint32_t>(types.size()));
    PutU32(bytes, 0xffffffffu);
    for (std::size_t index = 0u; index < types.size(); ++index)
    {
        PutU32(bytes, types[index]);
        PutU32(bytes, references[index]);
    }
    std::vector<std::uint8_t> first(148u, 0u);
    SetU32(first, 0u, 0xffffffffu);
    if (firstDependency) SetU32(first, 12u + 4u * 4u, 0xffffffffu);
    bytes.insert(bytes.end(), first.begin(), first.end());
    AppendString(bytes, ",web/mc_l_sm_r0c0s0");
    std::vector<std::uint8_t> second(148u, 0u);
    SetU32(second, 0u, 0xffffffffu);
    if (secondDependency) SetU32(second, 12u + 4u * 4u, 0xffffffffu);
    if (invalidSecondHeader) second[5u] = 1u;
    bytes.insert(bytes.end(), second.begin(), second.end());
    AppendString(bytes, ",web/mc_l_sm_r0c0s1");
    return bytes;
}

std::vector<std::uint8_t> BuildWorldXModelPrefixInflated(
    bool invalidBounds = false,
    bool unsupportedBoneNames = false,
    bool invalidBoneString = false,
    std::uint8_t surfaceCount = 2u,
    bool withCollisionSurface = false,
    bool withPhysPreset = false,
    bool sharedPhysPreset = false,
    bool withPhysGeoms = false)
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 2000000u);
    PutU32(bytes, 1000000u);
    for (const std::uint32_t block :
        std::array<std::uint32_t, 9>{{4096u, 0u, 0u, 0u, 4096u, 0u, 0u,
                                      4096u, 4096u}})
        PutU32(bytes, block);
    PutU32(bytes, 1u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 7u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0xffffffffu);
    AppendString(bytes, "tag_origin");
    const std::array<std::uint32_t, 7> types = {{5u, 5u, 3u, 2u, 31u, 16u, 32u}};
    const std::array<std::uint32_t, 7> references = {{
        0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
        0xffffffffu, 0xffffffffu, 0u,
    }};
    for (std::size_t index = 0u; index < types.size(); ++index)
    {
        PutU32(bytes, types[index]);
        PutU32(bytes, references[index]);
    }
    for (const char *name : {",web/mc_l_sm_r0c0s0", ",web/mc_l_sm_r0c0s1"})
    {
        std::vector<std::uint8_t> technique(148u, 0u);
        SetU32(technique, 0u, 0xffffffffu);
        bytes.insert(bytes.end(), technique.begin(), technique.end());
        AppendString(bytes, name);
    }
    std::vector<std::uint8_t> model(220u, 0u);
    SetU32(model, 0u, 0xffffffffu);
    model[4u] = 1u;
    model[5u] = 1u;
    model[6u] = surfaceCount;
    SetU32(model, 8u, unsupportedBoneNames ? 0x40000001u : 0xffffffffu);
    SetU32(model, 24u, 0xffffffffu);
    SetU32(model, 28u, 0xffffffffu);
    SetU32(model, 32u, 0xffffffffu);
    SetU32(model, 36u, 0xffffffffu);
    SetF32(model, 40u, 800.0f);
    PutU16At(model, 44u, surfaceCount == 6u ? 2u : surfaceCount);
    SetU32(model, 48u, 0x80000000u);
    if (withCollisionSurface)
    {
        SetU32(model, 152u, 0xffffffffu);
        SetU32(model, 156u, 1u);
    }
    SetU32(model, 164u, 0xffffffffu);
    SetF32(model, 168u, 10.0f);
    SetF32(model, 172u, invalidBounds ? 2.0f : -1.0f);
    SetF32(model, 176u, -2.0f);
    SetF32(model, 180u, -3.0f);
    SetF32(model, 184u, 1.0f);
    SetF32(model, 188u, 2.0f);
    SetF32(model, 192u, 3.0f);
    PutU16At(model, 196u, 1u);
    PutU16At(model, 198u, 0u);
    SetU32(model, 204u, 100u);
    if (withPhysPreset)
        SetU32(model, 212u, sharedPhysPreset ? 0xfffffffeu : 0xffffffffu);
    if (withPhysGeoms) SetU32(model, 216u, 0xffffffffu);
    bytes.insert(bytes.end(), model.begin(), model.end());
    AppendString(bytes, "web/xmodel_wall");
    PutU16(bytes, invalidBoneString ? 1u : 0u);
    bytes.push_back(0u);
    const std::array<float, 8> baseMat = {{0.0f, 0.0f, 0.0f, 1.0f,
                                           0.0f, 0.0f, 0.0f, 1.0f}};
    for (const float value : baseMat)
        PutU32(bytes, std::bit_cast<std::uint32_t>(value));
    return bytes;
}

std::vector<std::uint8_t> BuildWorldXSurfacePrefixInflated(
    bool invalidSurfaceLayout = false,
    bool invalidCollisionTree = false,
    std::uint8_t surfaceCount = 2u,
    bool m26MaterialHandles = false,
    bool withCollisionSurface = false,
    bool withPhysPreset = false,
    bool sharedPhysPreset = false,
    bool withPhysGeoms = false)
{
    std::vector<std::uint8_t> bytes = BuildWorldXModelPrefixInflated(
        false, false, false, surfaceCount, withCollisionSurface,
        withPhysPreset, sharedPhysPreset, withPhysGeoms);
    for (std::uint32_t index = 0u; index < surfaceCount; ++index)
    {
        std::vector<std::uint8_t> surface(56u, 0u);
        surface[0u] = index == 0u ? 1u : 0u;
        PutU16At(surface, 2u, 3u);
        PutU16At(surface, 4u, 1u);
        PutU16At(surface, 8u, static_cast<std::uint16_t>(index));
        PutU16At(surface, 10u, static_cast<std::uint16_t>(index * 3u));
        SetU32(surface, 12u,
            invalidSurfaceLayout && index == 0u ? 0u : 0xffffffffu);
        SetU32(surface, 28u, 0xffffffffu);
        SetU32(surface, 32u, 1u);
        SetU32(surface, 36u, 0xffffffffu);
        SetU32(surface, 40u, 0x80000000u);
        bytes.insert(bytes.end(), surface.begin(), surface.end());
    }
    for (std::uint32_t index = 0u; index < surfaceCount; ++index)
    {
        for (std::uint32_t byte = 0u; byte < 3u * 32u; ++byte)
            bytes.push_back(static_cast<std::uint8_t>(byte + index));
        PutU16(bytes, 0u);
        PutU16(bytes, 3u);
        PutU16(bytes, 0u);
        PutU16(bytes, 1u);
        PutU32(bytes, index == 0u ? 0xffffffffu : 0u);
        if (index == 0u)
        {
            for (std::uint32_t axis = 0u; axis < 3u; ++axis) PutU32(bytes, 0u);
            for (std::uint32_t axis = 0u; axis < 3u; ++axis)
            {
                const float scale = invalidCollisionTree && axis == 0u ? 0.0f : 1.0f;
                PutU32(bytes, std::bit_cast<std::uint32_t>(scale));
            }
            PutU32(bytes, 1u);
            PutU32(bytes, 0xffffffffu);
            PutU32(bytes, 1u);
            PutU32(bytes, 0xffffffffu);
            for (std::uint32_t byte = 0u; byte < 16u; ++byte)
                bytes.push_back(static_cast<std::uint8_t>(0x40u + byte));
            PutU16(bytes, 0u);
        }
        PutU16(bytes, 0u);
        PutU16(bytes, 1u);
        PutU16(bytes, 2u);
    }
    if (m26MaterialHandles)
    {
        PutU32(bytes, 0xffffffffu);
        PutU32(bytes, 0xffffffffu);
        for (std::uint32_t index = 2u; index < surfaceCount; ++index)
            PutU32(bytes, (index & 1u) == 0u ? 0x40000281u : 0x40000285u);
    }
    else
    {
        PutU32(bytes, 0xffffffffu);
        for (std::uint32_t index = 1u; index < surfaceCount; ++index)
            PutU32(bytes, 0x40000001u);
    }
    return bytes;
}

std::vector<std::uint8_t> BuildWorldXModelDependenciesInflated(
    bool invalidMaterialAlias = false,
    bool invalidCollisionBounds = false,
    bool invalidBoneInfo = false,
    bool withPhysPreset = false,
    bool uncompressedImage = false,
    bool invalidPhysPresetValues = false,
    bool invalidPhysPresetSoundAlias = false,
    bool sharedPhysPreset = false,
    bool withPhysGeoms = false)
{
    std::vector<std::uint8_t> bytes = BuildWorldXSurfacePrefixInflated(
        false, false, 6u, true, true, withPhysPreset, sharedPhysPreset,
        withPhysGeoms);

    auto appendMaterial = [&](const char *name,
                              std::uint32_t techniqueAlias,
                              std::uint32_t imageReference,
                              bool includeImage,
                              bool includeConstant) {
        std::vector<std::uint8_t> material(80u, 0u);
        SetU32(material, 0u, 0xffffffffu);
        material[24u] = 0u;
        std::fill(material.begin() + 25u, material.begin() + 58u, 0xffu);
        material[58u] = 1u;
        material[59u] = includeConstant ? 1u : 0u;
        material[60u] = 1u;
        SetU32(material, 64u, techniqueAlias);
        SetU32(material, 68u, 0xffffffffu);
        SetU32(material, 72u, includeConstant ? 0xffffffffu : 0u);
        SetU32(material, 76u, 0xffffffffu);
        bytes.insert(bytes.end(), material.begin(), material.end());
        AppendString(bytes, name);

        PutU32(bytes, 0x12345678u);
        bytes.push_back('c');
        bytes.push_back('p');
        bytes.push_back(1u);
        bytes.push_back(2u);
        PutU32(bytes, imageReference);
        if (includeImage)
        {
            std::vector<std::uint8_t> image(36u, 0u);
            SetU32(image, 0u, 3u);
            SetU32(image, 4u, 0xfffffffeu);
            PutU16At(image, 24u, 4u);
            PutU16At(image, 26u, 4u);
            PutU16At(image, 28u, 1u);
            SetU32(image, 32u, 0xffffffffu);
            bytes.insert(bytes.end(), image.begin(), image.end());
            AppendString(bytes, "synthetic_xmodel_color");
            PutU16(bytes, 0u);
            PutU16(bytes, 4u);
            PutU16(bytes, 4u);
            PutU16(bytes, 1u);
            PutU32(bytes, uncompressedImage ? 0x16u : 0x31545844u);
            PutU32(bytes, 0u);
        }
        if (includeConstant)
        {
            std::vector<std::uint8_t> constant(32u, 0u);
            SetU32(constant, 0u, 0x9abcdef0u);
            const char constantName[] = "colorTint";
            std::copy(std::begin(constantName), std::end(constantName) - 1,
                constant.begin() + 4u);
            for (std::size_t index = 0u; index < 4u; ++index)
                SetF32(constant, 16u + index * 4u, 1.0f);
            bytes.insert(bytes.end(), constant.begin(), constant.end());
        }
        for (std::uint32_t index = 0u; index < 8u; ++index)
            bytes.push_back(static_cast<std::uint8_t>(index));
    };

    appendMaterial("web/material_a", 0x40000015u, 0xffffffffu, true, true);
    appendMaterial(
        "web/material_b", 0x4000001du,
        invalidMaterialAlias ? 0x40000001u : 0x400002b1u,
        false, false);

    std::vector<std::uint8_t> collision(44u, 0u);
    SetU32(collision, 0u, 0xffffffffu);
    SetU32(collision, 4u, 1u);
    SetF32(collision, 8u, invalidCollisionBounds ? 2.0f : -1.0f);
    SetF32(collision, 12u, -1.0f);
    SetF32(collision, 16u, -1.0f);
    SetF32(collision, 20u, 1.0f);
    SetF32(collision, 24u, 1.0f);
    SetF32(collision, 28u, 1.0f);
    SetU32(collision, 36u, 1u);
    bytes.insert(bytes.end(), collision.begin(), collision.end());
    for (std::size_t index = 0u; index < 12u; ++index)
        PutU32(bytes, std::bit_cast<std::uint32_t>(
            index == 0u ? 1.0f : 0.0f));

    std::vector<std::uint8_t> boneInfo(40u, 0u);
    SetF32(boneInfo, 0u, invalidBoneInfo ? 2.0f : -1.0f);
    SetF32(boneInfo, 4u, -1.0f);
    SetF32(boneInfo, 8u, -1.0f);
    SetF32(boneInfo, 12u, 1.0f);
    SetF32(boneInfo, 16u, 1.0f);
    SetF32(boneInfo, 20u, 1.0f);
    SetF32(boneInfo, 36u, 3.0f);
    bytes.insert(bytes.end(), boneInfo.begin(), boneInfo.end());
    if (withPhysPreset)
    {
        std::vector<std::uint8_t> preset(44u, 0u);
        SetU32(preset, 0u, 0xffffffffu);
        SetF32(preset, 8u,
            invalidPhysPresetValues
                ? std::numeric_limits<float>::quiet_NaN()
                : 100.0f);
        SetF32(preset, 12u, 0.25f);
        SetF32(preset, 16u, 0.5f);
        SetF32(preset, 20u, 1.0f);
        SetF32(preset, 24u, 2.0f);
        SetU32(preset, 28u, 0xffffffffu);
        SetF32(preset, 32u, 0.4f);
        SetF32(preset, 36u, 12.0f);
        preset[40u] = 1u;
        bytes.insert(bytes.end(), preset.begin(), preset.end());
        AppendString(bytes, "web/phys_sandbag");
        if (invalidPhysPresetSoundAlias)
        {
            bytes.push_back('b');
            bytes.push_back('a');
            bytes.push_back('d');
            bytes.push_back(' ');
            bytes.push_back('s');
            bytes.push_back('o');
            bytes.push_back('u');
            bytes.push_back('n');
            bytes.push_back('d');
            bytes.push_back(0u);
        }
        else
        {
            AppendString(bytes, "sandbag");
        }
    }
    if (withPhysGeoms)
    {
        std::vector<std::uint8_t> list(44u, 0u);
        SetU32(list, 0u, 1u);
        SetU32(list, 4u, 0xffffffffu);
        bytes.insert(bytes.end(), list.begin(), list.end());
        std::vector<std::uint8_t> geom(68u, 0u);
        SetU32(geom, 4u, 1u);
        SetF32(geom, 8u, 1.0f);
        SetF32(geom, 24u, 1.0f);
        SetF32(geom, 40u, 1.0f);
        bytes.insert(bytes.end(), geom.begin(), geom.end());
    }
    return bytes;
}

std::vector<std::uint8_t> BuildWorldPostXModelTechniqueSetInflated(
    bool invalidHeader = false,
    bool withTechniqueDependency = false,
    bool invalidLaterHeader = false,
    bool withLaterTechniqueDependency = false)
{
    std::vector<std::uint8_t> bytes = BuildWorldXModelDependenciesInflated();
    constexpr std::size_t assetTableOffset = 75u;
    constexpr std::size_t postXModelAssetIndex = 3u;
    SetU32(bytes, assetTableOffset + postXModelAssetIndex * 8u, 5u);
    SetU32(bytes, assetTableOffset + (postXModelAssetIndex + 1u) * 8u, 5u);

    std::vector<std::uint8_t> techniqueSet(148u, 0u);
    SetU32(techniqueSet, 0u, 0xffffffffu);
    if (invalidHeader) techniqueSet[5u] = 1u;
    if (withTechniqueDependency)
        SetU32(techniqueSet, 12u + 4u * 4u, 0xffffffffu);
    bytes.insert(bytes.end(), techniqueSet.begin(), techniqueSet.end());
    AppendString(bytes, ",web/mc_l_sm_r0c0n0s0");
    std::vector<std::uint8_t> laterTechniqueSet(148u, 0u);
    SetU32(laterTechniqueSet, 0u, 0xffffffffu);
    if (invalidLaterHeader) laterTechniqueSet[5u] = 1u;
    if (withLaterTechniqueDependency)
        SetU32(laterTechniqueSet, 12u + 7u * 4u, 0xffffffffu);
    bytes.insert(bytes.end(), laterTechniqueSet.begin(), laterTechniqueSet.end());
    AppendString(bytes, ",web/mc_l_sm_r0c0n0s1");
    return bytes;
}

std::vector<std::uint8_t> BuildWorldSecondXModelPrefixInflated(
    bool invalidBounds = false,
    bool unsupportedBoneNames = false,
    bool invalidSurfaceLayout = false,
    std::uint32_t priorBoneNamesReference = 0u,
    std::uint32_t priorPartClassificationReference = 0u,
    std::uint32_t priorBaseMatReference = 0u)
{
    std::vector<std::uint8_t> bytes =
        BuildWorldPostXModelTechniqueSetInflated();
    constexpr std::size_t assetTableOffset = 75u;
    constexpr std::size_t secondXModelAssetIndex = 5u;
    SetU32(bytes, assetTableOffset + secondXModelAssetIndex * 8u, 3u);
    SetU32(bytes, assetTableOffset + secondXModelAssetIndex * 8u + 4u,
        0xffffffffu);
    SetU32(bytes, assetTableOffset + (secondXModelAssetIndex + 1u) * 8u, 16u);
    SetU32(bytes, assetTableOffset + (secondXModelAssetIndex + 1u) * 8u + 4u,
        0xffffffffu);

    std::vector<std::uint8_t> model(220u, 0u);
    SetU32(model, 0u, 0xffffffffu);
    model[4u] = 1u;
    model[5u] = 1u;
    model[6u] = 3u;
    const std::uint32_t boneNamesReference = priorBoneNamesReference != 0u
        ? priorBoneNamesReference
        : (unsupportedBoneNames ? 0x40000001u : 0xffffffffu);
    SetU32(model, 8u, boneNamesReference);
    const std::uint32_t partClassificationReference =
        priorPartClassificationReference != 0u
            ? priorPartClassificationReference : 0xffffffffu;
    const std::uint32_t baseMatReference = priorBaseMatReference != 0u
        ? priorBaseMatReference : 0xffffffffu;
    SetU32(model, 24u, partClassificationReference);
    SetU32(model, 28u, baseMatReference);
    SetU32(model, 32u, 0xffffffffu);
    SetU32(model, 36u, 0xffffffffu);
    SetF32(model, 40u, 1200.0f);
    PutU16At(model, 44u, 3u);
    SetU32(model, 48u, 0x80000000u);
    SetU32(model, 152u, 0xffffffffu);
    SetU32(model, 156u, 1u);
    SetU32(model, 164u, 0xffffffffu);
    SetF32(model, 168u, 20.0f);
    SetF32(model, 172u, invalidBounds ? 3.0f : -2.0f);
    SetF32(model, 176u, -3.0f);
    SetF32(model, 180u, -4.0f);
    SetF32(model, 184u, 2.0f);
    SetF32(model, 188u, 3.0f);
    SetF32(model, 192u, 4.0f);
    PutU16At(model, 196u, 1u);
    PutU16At(model, 198u, 0u);
    SetU32(model, 204u, 512u);
    bytes.insert(bytes.end(), model.begin(), model.end());
    AppendString(bytes, "web/xmodel_second");
    if (boneNamesReference == 0xffffffffu ||
        boneNamesReference == 0xfffffffeu)
    {
        PutU16(bytes, 0u);
    }
    if (partClassificationReference == 0xffffffffu ||
        partClassificationReference == 0xfffffffeu)
    {
        bytes.push_back(0u);
    }
    if (baseMatReference == 0xffffffffu ||
        baseMatReference == 0xfffffffeu)
    {
        std::vector<std::uint8_t> baseMat(32u, 0u);
        SetF32(baseMat, 12u, 1.0f);
        SetF32(baseMat, 28u, 1.0f);
        bytes.insert(bytes.end(), baseMat.begin(), baseMat.end());
    }
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        std::vector<std::uint8_t> surface(56u, 0u);
        PutU16At(surface, 2u, 3u);
        PutU16At(surface, 4u, 1u);
        PutU16At(surface, 8u, static_cast<std::uint16_t>(index));
        PutU16At(surface, 10u, static_cast<std::uint16_t>(index * 3u));
        SetU32(surface, 12u,
            invalidSurfaceLayout && index == 0u ? 0u : 0xffffffffu);
        SetU32(surface, 28u, 0xffffffffu);
        SetU32(surface, 32u, 1u);
        SetU32(surface, 36u, 0xffffffffu);
        SetU32(surface, 40u, 0x80000000u);
        bytes.insert(bytes.end(), surface.begin(), surface.end());
    }
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        const float offset = static_cast<float>(index) * 1.5f;
        const std::array<std::array<float, 3>, 3> positions{{
            {{offset - 0.5f, -0.5f, 0.0f}},
            {{offset + 0.5f, -0.5f, 0.0f}},
            {{offset, 0.5f, 0.0f}},
        }};
        for (std::size_t vertexIndex = 0u;
             vertexIndex < positions.size(); ++vertexIndex)
        {
            std::vector<std::uint8_t> vertex(32u, 0u);
            SetF32(vertex, 0u, positions[vertexIndex][0]);
            SetF32(vertex, 4u, positions[vertexIndex][1]);
            SetF32(vertex, 8u, positions[vertexIndex][2]);
            SetF32(vertex, 12u, 1.0f);
            SetU32(vertex, 16u, 0xffffffffu);
            const std::uint32_t u = vertexIndex == 1u ? 0x3c00u : 0u;
            const std::uint32_t v = vertexIndex == 2u ? 0x3c00u : 0u;
            SetU32(vertex, 20u, (u << 16u) | v);
            SetU32(vertex, 24u, 0x7f7fffffu);
            SetU32(vertex, 28u, 0x7f7fffffu);
            bytes.insert(bytes.end(), vertex.begin(), vertex.end());
        }
        PutU16(bytes, 0u);
        PutU16(bytes, 3u);
        PutU16(bytes, 0u);
        PutU16(bytes, 1u);
        PutU32(bytes, 0u);
        PutU16(bytes, 0u);
        PutU16(bytes, 1u);
        PutU16(bytes, 2u);
    }
    for (std::uint32_t index = 0u; index < 3u; ++index)
        PutU32(bytes, 0xffffffffu);
    return bytes;
}

std::vector<std::uint8_t> BuildWorldSecondXModelDependenciesInflated(
    bool invalidMaterialTechniqueAlias = false,
    bool invalidImageAlias = false)
{
    std::vector<std::uint8_t> bytes =
        BuildWorldSecondXModelPrefixInflated();
    constexpr std::uint32_t secondMaterialHandleAlias = 0x400004f1u;
    SetU32(bytes, bytes.size() - 8u, secondMaterialHandleAlias);
    SetU32(bytes, bytes.size() - 4u, secondMaterialHandleAlias);

    std::vector<std::uint8_t> material(80u, 0u);
    SetU32(material, 0u, 0xffffffffu);
    std::fill(material.begin() + 24u, material.begin() + 58u, 0xffu);
    material[58u] = 1u;
    SetU32(material, 64u,
        invalidMaterialTechniqueAlias ? 0x40000001u : 0x40000015u);
    SetU32(material, 68u, 0xffffffffu);
    bytes.insert(bytes.end(), material.begin(), material.end());
    AppendString(bytes, "web/material_second");
    PutU32(bytes, 0x12345678u);
    bytes.push_back('c');
    bytes.push_back('p');
    bytes.push_back(1u);
    bytes.push_back(2u);
    PutU32(bytes, invalidImageAlias ? 0x40000001u : 0x400002b1u);

    std::vector<std::uint8_t> collision(44u, 0u);
    SetU32(collision, 0u, 0xffffffffu);
    SetU32(collision, 4u, 1u);
    SetF32(collision, 8u, -1.0f);
    SetF32(collision, 12u, -1.0f);
    SetF32(collision, 16u, -1.0f);
    SetF32(collision, 20u, 1.0f);
    SetF32(collision, 24u, 1.0f);
    SetF32(collision, 28u, 1.0f);
    bytes.insert(bytes.end(), collision.begin(), collision.end());
    for (std::size_t index = 0u; index < 12u; ++index)
        PutU32(bytes, std::bit_cast<std::uint32_t>(
            index == 0u ? 1.0f : 0.0f));

    std::vector<std::uint8_t> boneInfo(40u, 0u);
    SetF32(boneInfo, 0u, -1.0f);
    SetF32(boneInfo, 4u, -1.0f);
    SetF32(boneInfo, 8u, -1.0f);
    SetF32(boneInfo, 12u, 1.0f);
    SetF32(boneInfo, 16u, 1.0f);
    SetF32(boneInfo, 20u, 1.0f);
    SetF32(boneInfo, 36u, 3.0f);
    bytes.insert(bytes.end(), boneInfo.begin(), boneInfo.end());
    return bytes;
}

std::vector<std::uint8_t> BuildWorldXModelCollectionInflated(
    bool invalidThirdBounds = false)
{
    std::vector<std::uint8_t> bytes =
        BuildWorldSecondXModelDependenciesInflated();
    constexpr std::size_t assetTableOffset = 75u;
    constexpr std::size_t thirdXModelAssetIndex = 6u;
    SetU32(bytes, 52u, 8u);
    SetU32(bytes, assetTableOffset + thirdXModelAssetIndex * 8u, 3u);
    SetU32(bytes, assetTableOffset + thirdXModelAssetIndex * 8u + 4u,
        0xffffffffu);
    const std::array<std::uint8_t, 8> gfxWorldEntry = {{
        16u, 0u, 0u, 0u, 0xffu, 0xffu, 0xffu, 0xffu,
    }};
    bytes.insert(
        bytes.begin() + static_cast<std::ptrdiff_t>(assetTableOffset + 7u * 8u),
        gfxWorldEntry.begin(), gfxWorldEntry.end());

    std::vector<std::uint8_t> model(220u, 0u);
    SetU32(model, 0u, 0xffffffffu);
    SetF32(model, 40u, 1600.0f);
    SetF32(model, 168u, 0.0f);
    SetF32(model, 172u, invalidThirdBounds ? 1.0f : 0.0f);
    SetF32(model, 176u, 0.0f);
    SetF32(model, 180u, 0.0f);
    SetF32(model, 184u, 0.0f);
    SetF32(model, 188u, 0.0f);
    SetF32(model, 192u, 0.0f);
    PutU16At(model, 196u, 1u);
    PutU16At(model, 198u, 0xffffu);
    bytes.insert(bytes.end(), model.begin(), model.end());
    AppendString(bytes, "web/xmodel_third");
    return bytes;
}

void AppendWorldMaterialTechnique(
    std::vector<std::uint8_t> &bytes,
    const std::string &name,
    bool invalidVertexProgram = false)
{
    PutU32(bytes, 0xffffffffu);
    PutU16(bytes, 0u);
    PutU16(bytes, 1u);

    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0xffffffffu);
    bytes.push_back(0u);
    bytes.push_back(0u);
    bytes.push_back(1u);
    bytes.push_back(0u);
    PutU32(bytes, 0xffffffffu);

    std::vector<std::uint8_t> declaration(100u, 0u);
    declaration[0u] = 1u;
    bytes.insert(bytes.end(), declaration.begin(), declaration.end());

    auto vertexProgram = BuildShaderProgram(true);
    if (invalidVertexProgram) vertexProgram.front() = 0u;
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, static_cast<std::uint32_t>(vertexProgram.size()));
    AppendString(bytes, name + "_vs");
    for (const std::uint32_t word : vertexProgram) PutU32(bytes, word);

    const auto pixelProgram = BuildShaderProgram(false);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, static_cast<std::uint32_t>(pixelProgram.size()));
    AppendString(bytes, name + "_ps");
    for (const std::uint32_t word : pixelProgram) PutU32(bytes, word);

    PutU32(bytes, 2u);
    PutU32(bytes, 0u);
    AppendString(bytes, name);
}

std::vector<std::uint8_t> BuildReusableWorldXModelLoaderInflated(
    bool withTechniqueDependencies = false,
    bool invalidSecondTechnique = false)
{
    std::vector<std::uint8_t> bytes =
        BuildWorldSecondXModelDependenciesInflated();
    constexpr std::size_t assetTableOffset = 75u;
    constexpr std::size_t trailingTechniqueSetAssetIndex = 6u;
    SetU32(bytes, 52u, 8u);
    SetU32(bytes,
        assetTableOffset + trailingTechniqueSetAssetIndex * 8u, 5u);
    const std::array<std::uint8_t, 8> gfxWorldEntry = {{
        16u, 0u, 0u, 0u, 0xffu, 0xffu, 0xffu, 0xffu,
    }};
    bytes.insert(
        bytes.begin() + static_cast<std::ptrdiff_t>(assetTableOffset + 7u * 8u),
        gfxWorldEntry.begin(), gfxWorldEntry.end());

    std::vector<std::uint8_t> techniqueSet(148u, 0u);
    SetU32(techniqueSet, 0u, 0xffffffffu);
    if (withTechniqueDependencies)
    {
        SetU32(techniqueSet, 12u + 4u * 4u, 0xffffffffu);
        SetU32(techniqueSet, 12u + 28u * 4u, 0xffffffffu);
    }
    bytes.insert(bytes.end(), techniqueSet.begin(), techniqueSet.end());
    AppendString(bytes, ",web/reusable_xmodel_loader_tail");
    if (withTechniqueDependencies)
    {
        AppendWorldMaterialTechnique(bytes, "web/reusable_first");
        AppendWorldMaterialTechnique(
            bytes, "web/reusable_second", invalidSecondTechnique);
    }
    return bytes;
}

std::vector<std::uint8_t> BuildReusableWorldFxLoaderInflated(
    bool invalidMaterial = false,
    std::uint32_t secondVisualReference = 0xffffffffu);

std::vector<std::uint8_t> BuildReusableWorldRawFileLoaderInflated(
    std::uint32_t rawFileCount = 1u)
{
    std::vector<std::uint8_t> bytes =
        BuildReusableWorldFxLoaderInflated(false);
    constexpr std::size_t assetTableOffset = 60u;
    constexpr std::size_t rawFileAssetIndex = 2u;
    SetU32(bytes, 52u, 3u + rawFileCount);
    const std::array<std::uint8_t, 8> rawFileEntry = {{
        31u, 0u, 0u, 0u, 0xffu, 0xffu, 0xffu, 0xffu,
    }};
    for (std::uint32_t index = 0u; index < rawFileCount; ++index)
    {
        bytes.insert(
            bytes.begin() + static_cast<std::ptrdiff_t>(
                assetTableOffset + (rawFileAssetIndex + index) * 8u),
            rawFileEntry.begin(), rawFileEntry.end());
    }
    for (std::uint32_t index = 0u; index < rawFileCount; ++index)
    {
        PutU32(bytes, 0xffffffffu);
        PutU32(bytes, 4u);
        PutU32(bytes, 1u);
        AppendString(bytes, index == 0u
            ? "scripts/web_rawfile.gsc"
            : "scripts/web_rawfile_second.gsc");
        const std::array<std::uint8_t, 5> payload = {{'t', 'e', 's', 't', 0u}};
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }
    return bytes;
}

std::vector<std::uint8_t> BuildInterleavedWorldRawFileLoaderInflated()
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 4096u);
    PutU32(bytes, 0u);
    const std::array<std::uint32_t, 9> blocks = {{
        1024u * 1024u, 0u, 0u, 0u, 1024u * 1024u,
        0u, 0u, 0u, 0u,
    }};
    for (const std::uint32_t block : blocks) PutU32(bytes, block);
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    PutU32(bytes, 5u);
    PutU32(bytes, 0xffffffffu);
    for (const std::uint32_t type : {5u, 31u, 3u, 31u, 16u})
    {
        PutU32(bytes, type);
        PutU32(bytes, 0xffffffffu);
    }

    std::vector<std::uint8_t> techniqueSet(148u, 0u);
    SetU32(techniqueSet, 0u, 0xffffffffu);
    bytes.insert(bytes.end(), techniqueSet.begin(), techniqueSet.end());
    AppendString(bytes, ",web/rawfile_interleave_prefix");

    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 4u);
    PutU32(bytes, 1u);
    AppendString(bytes, "scripts/web_rawfile_before.gsc");
    for (const std::uint8_t byte :
        std::array<std::uint8_t, 5>{{'b', 'e', 'f', 'o', 0u}})
        bytes.push_back(byte);

    std::vector<std::uint8_t> model(220u, 0u);
    SetU32(model, 0u, 0xffffffffu);
    bytes.insert(bytes.end(), model.begin(), model.end());
    AppendString(bytes, ",web/empty_xmodel_between_rawfiles");

    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 4u);
    PutU32(bytes, 1u);
    AppendString(bytes, "scripts/web_rawfile_after.gsc");
    for (const std::uint8_t byte :
        std::array<std::uint8_t, 5>{{'a', 'f', 't', 'r', 0u}})
        bytes.push_back(byte);
    return bytes;
}

std::vector<std::uint8_t> BuildReusableWorldXAnimLoaderInflated(
    bool invalidBoneName = false,
    bool invalidAlias = false)
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 4096u);
    PutU32(bytes, 0u);
    const std::array<std::uint32_t, 9> blocks = {{
        1024u * 1024u, 0u, 0u, 0u, 1024u * 1024u,
        0u, 0u, 0u, 0u,
    }};
    for (const std::uint32_t block : blocks) PutU32(bytes, block);

    const std::array<std::string, 2> scriptStrings = {{"tag_origin", "end"}};
    PutU32(bytes, static_cast<std::uint32_t>(scriptStrings.size()));
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 5u);
    PutU32(bytes, 0xffffffffu);
    for (std::size_t index = 0u; index < scriptStrings.size(); ++index)
        PutU32(bytes, 0xffffffffu);
    for (const std::string &value : scriptStrings) AppendString(bytes, value);

    const std::string techniqueName = ",web/xanim_prefix";
    std::uint32_t block4Cursor =
        static_cast<std::uint32_t>(scriptStrings.size() * 4u);
    for (const std::string &value : scriptStrings)
        block4Cursor += static_cast<std::uint32_t>(value.size() + 1u);
    block4Cursor = (block4Cursor + 3u) & ~3u;
    block4Cursor += 5u * 8u;
    block4Cursor += static_cast<std::uint32_t>(techniqueName.size() + 1u);
    const std::uint32_t insertionOffset = (block4Cursor + 3u) & ~3u;
    const std::uint32_t insertionAlias =
        0x40000000u | (insertionOffset + 1u);

    const std::array<std::uint32_t, 5> types = {{5u, 2u, 2u, 2u, 16u}};
    const std::array<std::uint32_t, 5> references = {{
        0xffffffffu,
        0xfffffffeu,
        invalidAlias ? insertionAlias + 4u : insertionAlias,
        0xffffffffu,
        0xffffffffu,
    }};
    for (std::size_t index = 0u; index < types.size(); ++index)
    {
        PutU32(bytes, types[index]);
        PutU32(bytes, references[index]);
    }

    std::vector<std::uint8_t> techniqueSet(148u, 0u);
    SetU32(techniqueSet, 0u, 0xffffffffu);
    bytes.insert(bytes.end(), techniqueSet.begin(), techniqueSet.end());
    AppendString(bytes, techniqueName);

    std::vector<std::uint8_t> first(88u, 0u);
    SetU32(first, 0u, 0xffffffffu);
    PutU16At(first, 4u, 2u);
    PutU16At(first, 6u, 2u);
    PutU16At(first, 8u, 1u);
    PutU16At(first, 10u, 2u);
    PutU16At(first, 12u, 1u);
    PutU16At(first, 14u, 10u);
    first[16u] = 1u;
    first[17u] = 1u;
    first[27u] = 2u;
    first[28u] = 2u;
    SetU32(first, 32u, 2u);
    SetU32(first, 36u, 3u);
    SetF32(first, 40u, 30.0f);
    SetF32(first, 44u, 0.1f);
    for (const std::size_t offset : {
             48u, 52u, 56u, 60u, 64u, 68u, 72u, 76u, 80u, 84u})
    {
        SetU32(first, offset, 1u);
    }
    bytes.insert(bytes.end(), first.begin(), first.end());
    AppendString(bytes, "web/xanim_full");
    PutU16(bytes, 0u);
    PutU16(bytes, invalidBoneName ? 2u : 1u);
    PutU16(bytes, 0u);
    PutU16(bytes, 0u);
    PutU32(bytes, std::bit_cast<std::uint32_t>(0.25f));
    PutU16(bytes, 1u);
    PutU16(bytes, 0u);
    PutU32(bytes, std::bit_cast<std::uint32_t>(0.75f));
    PutU32(bytes, 1u);
    PutU32(bytes, 1u);
    PutU16(bytes, 1u);
    bytes.push_back(1u);
    bytes.push_back(0u);
    for (const float value : {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f})
        PutU32(bytes, std::bit_cast<std::uint32_t>(value));
    PutU32(bytes, 1u);
    bytes.push_back(0u);
    bytes.push_back(9u);
    for (const std::uint8_t value : {1u, 2u, 3u, 4u, 5u, 6u})
        bytes.push_back(value);
    PutU16(bytes, 1u);
    PutU16(bytes, 0u);
    PutU32(bytes, 1u);
    bytes.push_back(0u);
    bytes.push_back(9u);
    for (const std::int16_t value : {10, 11, 12, 13})
        PutU16(bytes, static_cast<std::uint16_t>(value));
    bytes.push_back(0xa1u);
    bytes.push_back(0xa2u);
    PutU16(bytes, 0x101u);
    PutU16(bytes, 0x202u);
    PutU32(bytes, 0x01020304u);
    PutU16(bytes, 0x303u);
    PutU16(bytes, 0x404u);
    bytes.push_back(0xb1u);
    bytes.push_back(0xb2u);
    PutU32(bytes, 0x11121314u);
    bytes.push_back(1u);
    bytes.push_back(2u);
    bytes.push_back(3u);

    std::vector<std::uint8_t> second(88u, 0u);
    SetU32(second, 0u, 0xffffffffu);
    PutU16At(second, 14u, 300u);
    SetU32(second, 52u, 1u);
    SetU32(second, 36u, 2u);
    SetU32(second, 76u, 1u);
    SetF32(second, 40u, 20.0f);
    SetF32(second, 44u, 1.0f / 20.0f);
    bytes.insert(bytes.end(), second.begin(), second.end());
    AppendString(bytes, "web/xanim_wide_indices");
    PutU16(bytes, 257u);
    PutU16(bytes, 299u);
    return bytes;
}

std::vector<std::uint8_t> BuildReusableWorldWeaponLoaderInflated(
    bool invalidScriptString = false,
    bool unsupportedDependency = false,
    bool invalidAlias = false)
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 8192u);
    PutU32(bytes, 0u);
    const std::array<std::uint32_t, 9> blocks = {{
        1024u * 1024u, 0u, 0u, 0u, 1024u * 1024u,
        0u, 0u, 0u, 0u,
    }};
    for (const std::uint32_t block : blocks) PutU32(bytes, block);

    const std::string scriptString = "tag_none";
    PutU32(bytes, 1u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 4u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0xffffffffu);
    AppendString(bytes, scriptString);

    const std::string techniqueName = ",web/weapon_prefix";
    std::uint32_t block4Cursor = 4u +
        static_cast<std::uint32_t>(scriptString.size() + 1u);
    block4Cursor = (block4Cursor + 3u) & ~3u;
    block4Cursor += 4u * 8u;
    block4Cursor += static_cast<std::uint32_t>(techniqueName.size() + 1u);
    const std::uint32_t insertionOffset = (block4Cursor + 3u) & ~3u;
    const std::uint32_t insertionAlias =
        0x40000000u | (insertionOffset + 1u);

    const std::array<std::uint32_t, 4> types = {{5u, 23u, 23u, 16u}};
    const std::array<std::uint32_t, 4> references = {{
        0xffffffffu,
        0xfffffffeu,
        invalidAlias ? insertionAlias + 4u : insertionAlias,
        0xffffffffu,
    }};
    for (std::size_t index = 0u; index < types.size(); ++index)
    {
        PutU32(bytes, types[index]);
        PutU32(bytes, references[index]);
    }

    std::vector<std::uint8_t> techniqueSet(148u, 0u);
    SetU32(techniqueSet, 0u, 0xffffffffu);
    bytes.insert(bytes.end(), techniqueSet.begin(), techniqueSet.end());
    AppendString(bytes, techniqueName);

    std::vector<std::uint8_t> weapon(2168u, 0u);
    for (const std::size_t offset : {0u, 4u, 804u, 1900u, 1904u, 2036u})
        SetU32(weapon, offset, 0xffffffffu);
    SetU32(weapon, 1908u, 0xffffffffu);
    SetU32(weapon, 1912u, 0xffffffffu);
    SetU32(weapon, 1916u, 0xffffffffu);
    SetU32(weapon, 1920u, 0xffffffffu);
    PutU16At(weapon, 216u, invalidScriptString ? 1u : 0u);
    SetU32(weapon, 296u, 7u);
    SetU32(weapon, 300u, 2u);
    SetU32(weapon, 304u, 4u);
    SetU32(weapon, 548u, 31u);
    SetF32(weapon, 1024u, 123.5f);
    SetU32(weapon, 1344u, 19u);
    SetU32(weapon, 1416u, 3u);
    SetF32(weapon, 1468u, 0.25f);
    SetF32(weapon, 1740u, 1.5f);
    SetU32(weapon, 1924u, 2u);
    SetU32(weapon, 1928u, 1u);
    SetU32(weapon, 1932u, 9u);
    SetU32(weapon, 1936u, 8u);
    SetU32(weapon, 1940u, 77u);
    SetU32(weapon, 2020u, 11u);
    SetU32(weapon, 2048u, 55u);
    SetF32(weapon, 2160u, 4.5f);
    if (unsupportedDependency) SetU32(weapon, 76u, 0xffffffffu);
    bytes.insert(bytes.end(), weapon.begin(), weapon.end());

    AppendString(bytes, "web/weapon_full");
    AppendString(bytes, "WEAPON_WEB_FULL");
    AppendString(bytes, "web_ammo");
    AppendString(bytes, "web_accuracy_zero");
    for (const float value : {0.0f, 1.0f, 2.0f, 3.0f})
        PutU32(bytes, std::bit_cast<std::uint32_t>(value));
    for (const float value : {4.0f, 5.0f, 6.0f, 7.0f})
        PutU32(bytes, std::bit_cast<std::uint32_t>(value));
    AppendString(bytes, "web_accuracy_one");
    for (const float value : {8.0f, 9.0f})
        PutU32(bytes, std::bit_cast<std::uint32_t>(value));
    for (const float value : {10.0f, 11.0f})
        PutU32(bytes, std::bit_cast<std::uint32_t>(value));
    AppendString(bytes, "web_weapon_script");
    return bytes;
}

std::vector<std::uint8_t> BuildWeaponCanonicalAliasDependenciesInflated(
    bool withSounds = false)
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 8192u);
    PutU32(bytes, 0u);
    for (const std::uint32_t block :
         std::array<std::uint32_t, 9>{{
             1024u * 1024u, 0u, 0u, 0u, 1024u * 1024u,
             0u, 0u, 0u, 0u}})
    {
        PutU32(bytes, block);
    }
    PutU32(bytes, 1u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 5u);
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0xffffffffu);
    AppendString(bytes, "tag_none");
    for (const std::uint32_t type : {5u, 3u, 25u, 23u, 16u})
    {
        PutU32(bytes, type);
        PutU32(bytes, 0xffffffffu);
    }

    constexpr std::uint32_t xmodelAlias = 0x4000001du;
    constexpr std::uint32_t fxAlias = 0x40000025u;
    // The script table/name consume 13 bytes and align the asset table to
    // offset 16. The table ends at 56. The technique, XModel, and FX
    // names consume 14, 15, and 11 bytes, so the aligned FxElemDef starts at
    // offset 96 and its inline visual handle cell is at 96 + 188.
    constexpr std::uint32_t materialAlias = 0x4000011du;
    constexpr std::uint32_t materialNameAlias = 0x4000015du;
    constexpr std::uint32_t pickupSoundCellAlias = 0x4000017du;

    std::vector<std::uint8_t> technique(148u, 0u);
    SetU32(technique, 0u, 0xffffffffu);
    bytes.insert(bytes.end(), technique.begin(), technique.end());
    AppendString(bytes, ",web/dep_tech");

    std::vector<std::uint8_t> model(220u, 0u);
    SetU32(model, 0u, 0xffffffffu);
    bytes.insert(bytes.end(), model.begin(), model.end());
    AppendString(bytes, ",web/dep_model");

    std::vector<std::uint8_t> effect(32u, 0u);
    SetU32(effect, 0u, 0xffffffffu);
    SetU32(effect, 16u, 1u);
    SetU32(effect, 28u, 1u);
    bytes.insert(bytes.end(), effect.begin(), effect.end());
    AppendString(bytes, "web/dep_fx");

    std::vector<std::uint8_t> elem(252u, 0u);
    elem[177u] = 1u;
    SetU32(elem, 188u, 0xffffffffu);
    bytes.insert(bytes.end(), elem.begin(), elem.end());

    std::vector<std::uint8_t> material(80u, 0u);
    SetU32(material, 0u, 0xffffffffu);
    std::fill(material.begin() + 24u, material.begin() + 58u, 0xffu);
    bytes.insert(bytes.end(), material.begin(), material.end());
    AppendString(bytes, "web/dep_material");

    std::vector<std::uint8_t> weapon(2168u, 0u);
    SetU32(weapon, 0u, 0xffffffffu);
    SetU32(weapon, 8u, materialNameAlias);
    SetU32(weapon, 12u, xmodelAlias);
    SetU32(weapon, 332u, fxAlias);
    SetU32(weapon, 540u, materialAlias);
    if (withSounds)
    {
        SetU32(weapon, 340u, 0xffffffffu);
        SetU32(weapon, 520u, 0xffffffffu);
        SetU32(weapon, 1736u, pickupSoundCellAlias);
    }
    bytes.insert(bytes.end(), weapon.begin(), weapon.end());
    AppendString(bytes, "web/dep_weapon");
    if (withSounds)
    {
        PutU32(bytes, 0xffffffffu);
        AppendString(bytes, "web/pickup");
        for (std::size_t index = 0u; index < 29u; ++index)
        {
            PutU32(bytes, index == 0u
                ? 0xffffffffu
                : index == 1u ? pickupSoundCellAlias : 0u);
        }
        PutU32(bytes, 0xffffffffu);
        AppendString(bytes, "web/bounce");
    }
    return bytes;
}

std::vector<std::uint8_t> BuildReusableWorldFxLoaderInflated(
    bool invalidMaterial,
    std::uint32_t secondVisualReference)
{
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 4096u);
    PutU32(bytes, 0u);
    const std::array<std::uint32_t, 9> blocks = {{
        1024u * 1024u, 0u, 0u, 0u, 1024u * 1024u,
        0u, 0u, 0u, 0u,
    }};
    for (const std::uint32_t block : blocks) PutU32(bytes, block);
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    PutU32(bytes, 3u);
    PutU32(bytes, 0xffffffffu);
    for (const std::uint32_t type : {5u, 25u, 16u})
    {
        PutU32(bytes, type);
        PutU32(bytes, 0xffffffffu);
    }

    std::vector<std::uint8_t> techniqueSet(148u, 0u);
    SetU32(techniqueSet, 0u, 0xffffffffu);
    bytes.insert(bytes.end(), techniqueSet.begin(), techniqueSet.end());
    AppendString(bytes, ",web/fx_prefix");

    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    PutU32(bytes, 1u);
    PutU32(bytes, 0u);
    PutU32(bytes, 1u);
    AppendString(bytes, "web/fx_mark");

    std::vector<std::uint8_t> elem(252u, 0u);
    elem[176u] = 9u;
    elem[177u] = 1u;
    SetU32(elem, 188u, 0xffffffffu);
    bytes.insert(bytes.end(), elem.begin(), elem.end());
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, secondVisualReference);
    const std::uint32_t materialCount =
        secondVisualReference == 0xffffffffu ? 2u : 1u;
    for (std::uint32_t index = 0u; index < materialCount; ++index)
    {
        std::vector<std::uint8_t> material(80u, 0u);
        SetU32(material, 0u, 0xffffffffu);
        if (index == 0u)
        {
            material[58u] = 1u;
            SetU32(material, 68u, 0xffffffffu);
        }
        if (invalidMaterial && index == 1u) material[58u] = 1u;
        bytes.insert(bytes.end(), material.begin(), material.end());
        AppendString(bytes, index == 0u
            ? ",web/fx_mark_world"
            : ",web/fx_mark_model");
        if (index == 0u)
        {
            PutU32(bytes, 0x12345678u);
            bytes.push_back('c');
            bytes.push_back('p');
            bytes.push_back(1u);
            bytes.push_back(2u);
            PutU32(bytes, 0xffffffffu);
            std::vector<std::uint8_t> image(36u, 0u);
            SetU32(image, 32u, 0xffffffffu);
            bytes.insert(bytes.end(), image.begin(), image.end());
            AppendString(bytes, ",web/fx_mark_builtin");
        }
    }
    return bytes;
}

kisak::fastfile::RetailFastfileCensus Run(
    const std::vector<std::uint8_t> &file,
    std::size_t chunkBytes = 7u,
    std::uint32_t stepRecords = 2u,
    std::uint32_t stepBytes = 3u,
    kisak::fastfile::RetailCensusMode mode =
        kisak::fastfile::RetailCensusMode::CodePostGfxMaterial,
    kisak::fastfile::RetailSoundAliasLookup soundLookup = {},
    bool loadGfxWorld = false)
{
    using namespace kisak::fastfile;
    RetailFastfileCensusJob job;
    RetailCensusLimits limits;
    limits.loadGfxWorld = loadGfxWorld;
    Require(job.BeginStreaming(mode, limits, soundLookup) ==
        RetailCensusError::None, "census starts");
    std::size_t offset = 0u;
    std::uint32_t steps = 0u;
    while (job.Progress() == RetailCensusProgress::Running && steps++ < 10000u)
    {
        if (job.NeedsSource() && offset < file.size())
        {
            const std::size_t count = std::min(chunkBytes, file.size() - offset);
            Require(job.FeedSource(
                std::span<const std::uint8_t>(file).subspan(offset, count),
                offset + count == file.size()) == RetailCensusError::None,
                "source chunk accepted");
            offset += count;
        }
        const RetailCensusStepReport report = job.Step({stepBytes, stepRecords});
        Require(report.sourceBytesConsumed <= stepBytes, "source step ceiling");
        Require(report.inflatedBytesProduced <= stepBytes, "inflate step ceiling");
        Require(report.traversedBytes <= stepBytes, "traversal step ceiling");
        Require(report.recordsProcessed <= stepRecords, "record step ceiling");
    }
    if (job.Progress() != RetailCensusProgress::Succeeded)
    {
        std::cerr << "census stopped at " << RetailCensusStageString(job.Stage())
                  << ": " << RetailCensusErrorString(job.Failure()) << '\n';
    }
    Require(job.Progress() == RetailCensusProgress::Succeeded, "census reaches body boundary");
    RetailFastfileCensus result;
    Require(job.TakeResult(result), "census result is available once");
    Require(!job.TakeResult(result), "census result is one shot");
    return result;
}

void TestWorldAssetInventory()
{
    using namespace kisak::fastfile;
    const auto result = Run(
        BuildFile(BuildWorldXModelPrefixInflated()), 7u, 2u, 3u,
        RetailCensusMode::WorldAssetInventory);
    Require(result.stoppedBeforeAssetBody && result.completedAssetCount == 0u &&
        result.firstGfxWorldAssetIndex == 5u &&
        result.firstGfxWorldReference == 0xffffffffu &&
        result.typesBeforeFirstGfxWorld[5u] == 2u &&
        result.typesBeforeFirstGfxWorld[3u] == 1u &&
        result.inlineReferencesBeforeFirstGfxWorld == 5u,
        "world inventory retains table order and stops before body zero");
}

void TestCanonicalLocalizeZoneLoader()
{
    using namespace kisak::fastfile;
    const RetailFastfileCensus result = Run(
        BuildFile(BuildLocalizeZoneInflated()),
        7u, 2u, 3u, RetailCensusMode::WorldAssetLoader);
    Require(result.completedAssetCount == 3u &&
            result.nextBodyIndex == 3u && result.nextBodyType == 32u &&
            result.firstGfxWorldAssetIndex == UINT32_MAX &&
            result.worldLocalizeEntries.size() == 2u &&
            result.worldMaterials.size() == 1u,
        "generic zone dispatcher publishes LocalizeEntry and Material assets without a GfxWorld");
    const RetailPublishedLocalizeEntry &first =
        result.worldLocalizeEntries[0u];
    const RetailPublishedLocalizeEntry &second =
        result.worldLocalizeEntries[1u];
    Require(first.published && second.published && first.asset && second.asset &&
            std::string_view(first.asset->value) == "Hello" &&
            std::string_view(first.asset->name) == "KEY_ONE" &&
            std::string_view(second.asset->value) == "Hello" &&
            std::string_view(second.asset->name) == "KEY_TWO" &&
            second.storage->value == first.storage->value,
        "canonical LocalizeEntry values retain native XString alias identity");
    const RetailXModelMaterial &material = result.worldMaterials[0u];
    Require(material.published && material.asset &&
            std::string_view(material.asset->info.name) ==
                "web/localize_boundary_material" &&
            material.textureCount == 0u && material.constantCount == 0u &&
            material.stateBitsCount == 0u,
        "generic zone dispatcher publishes a zero-dependency canonical Material");
}

void TestCanonicalGfxWorldLoader()
{
    using namespace kisak::fastfile;
    const RetailFastfileCensus nullRoot = Run(
        BuildFile(BuildMinimalGfxWorldZoneInflated(0u)),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader, {}, true);
    Require(nullRoot.completedAssetCount == 1u &&
            nullRoot.nextBodyIndex == 1u &&
            nullRoot.nextBodyType == ASSET_TYPE_RAWFILE &&
            nullRoot.worldGfxWorlds.size() == 1u &&
            nullRoot.worldGfxWorlds.front().nullRoot &&
            !nullRoot.worldGfxWorlds.front().asset,
        "Load_GfxWorldPtr preserves a null root without exposing a canonical asset");

    const RetailFastfileCensus minimal = Run(
        BuildFile(BuildMinimalGfxWorldZoneInflated()),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader, {}, true);
    Require(minimal.completedAssetCount == 1u &&
            minimal.nextBodyIndex == 1u &&
            minimal.nextBodyType == ASSET_TYPE_RAWFILE &&
            minimal.worldGfxWorlds.size() == 1u,
        "dispatcher completes a minimal canonical GfxWorld before the next asset");
    const RetailPublishedGfxWorld &published = minimal.worldGfxWorlds.front();
    Require(published.published && published.asset && published.storage &&
            published.identity != 0u &&
            std::string_view(published.asset->name) ==
                "maps/mp/web_minimal_gfxworld.d3dbsp" &&
            std::string_view(published.asset->baseName) ==
                "web_minimal_gfxworld" &&
            published.asset->reflectionProbeTextures &&
            published.asset->reflectionProbeTextures[0u].basemap == nullptr &&
            published.asset->lightmapPrimaryTextures &&
            published.asset->lightmapPrimaryTextures[0u].basemap == nullptr &&
            published.asset->lightmapSecondaryTextures &&
            published.asset->lightmapSecondaryTextures[0u].basemap == nullptr &&
            published.block1HighWaterAtPublication == 12u &&
            minimal.semanticTrace.back().kind ==
                kisak::database::SemanticTraceEventKind::Boundary,
        "minimal GfxWorld publishes only after its null children and zero-filled block-1 runtime slots complete");

    const RetailFastfileCensus aliased = Run(
        BuildFile(BuildMinimalGfxWorldZoneInflated(0xfffffffeu, true)),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader, {}, true);
    Require(aliased.completedAssetCount == 2u &&
            aliased.worldGfxWorlds.size() == 2u &&
            aliased.worldGfxWorlds[0u].published &&
            aliased.worldGfxWorlds[0u].insertPointerBlock4Offset == 24u &&
            aliased.worldGfxWorlds[1u].pointerAlias &&
            aliased.worldGfxWorlds[1u].identity ==
                aliased.worldGfxWorlds[0u].identity &&
            aliased.worldGfxWorlds[1u].asset.get() ==
                aliased.worldGfxWorlds[0u].asset.get(),
        "Load_GfxWorldPtr preserves -2 insertion-cell and later prior-alias identity");

    const auto expectAtomicFailure = [](std::vector<std::uint8_t> inflated,
                                        RetailCensusError expected,
                                        const char *message) {
        RetailFastfileCensusJob job;
        RetailCensusLimits limits;
        limits.loadGfxWorld = true;
        const std::vector<std::uint8_t> file = BuildFile(std::move(inflated));
        Require(job.BeginStreaming(RetailCensusMode::WorldAssetLoader, limits) ==
                    RetailCensusError::None &&
                job.FeedSource(file, true) == RetailCensusError::None,
            "malformed GfxWorld fixture starts");
        while (job.Progress() == RetailCensusProgress::Running)
            (void)job.Step({5u, 1u});
        RetailFastfileCensus unavailable;
        Require(job.Failure() == expected && !job.TakeResult(unavailable), message);
    };

    std::vector<std::uint8_t> overflowing = BuildMinimalGfxWorldZoneInflated();
    constexpr std::size_t rootOffset = 60u + 2u * 8u;
    SetU32(overflowing, rootOffset + 0x30u, UINT32_MAX);
    expectAtomicFailure(std::move(overflowing),
        RetailCensusError::GfxWorldCountInvalid,
        "overflowing GfxWorld counts fail without publishing partial ownership");

    std::vector<std::uint8_t> truncated = BuildMinimalGfxWorldZoneInflated();
    truncated.resize(rootOffset + 100u);
    expectAtomicFailure(std::move(truncated),
        RetailCensusError::RecordTruncated,
        "truncated GfxWorld bodies fail without publishing partial ownership");

    expectAtomicFailure(BuildMinimalGfxWorldZoneInflated(0xffffffffu, true),
        RetailCensusError::GfxWorldAliasInvalid,
        "an undefined GfxWorld prior alias fails atomically");
}

void TestCanonicalClipMapLoader()
{
    using namespace kisak::fastfile;
    const RetailFastfileCensus result = Run(
        BuildFile(BuildClipMapZoneInflated()),
        7u, 2u, 3u, RetailCensusMode::WorldAssetLoader);
    Require(result.completedAssetCount == 1u &&
            result.nextBodyIndex == 1u && result.nextBodyType == 32u &&
            result.worldClipMaps.size() == 1u,
        "top-level dispatch routes col_map_sp through the ClipMap family");
    const RetailPublishedClipMap &published = result.worldClipMaps.front();
    Require(published.published && published.asset && published.storage &&
            published.assetType == 10u &&
            std::string_view(published.asset->name) ==
                "maps/mp/web_clipmap.d3dbsp" &&
            published.asset->isInUse == 1 &&
            published.asset->checksum == 0x12345678u &&
            published.asset->planes == nullptr &&
            published.asset->mapEnts == nullptr &&
            published.identity != 0u,
        "ClipMap family publishes the canonical clipMap_t only after its child graph completes");
    Require(result.semanticTrace.size() == 3u &&
            result.semanticTrace[0u].kind ==
                kisak::database::SemanticTraceEventKind::AssetBegin &&
            result.semanticTrace[1u].kind ==
                kisak::database::SemanticTraceEventKind::AssetPublish &&
            result.semanticTrace[1u].assetType == ASSET_TYPE_CLIPMAP &&
            result.semanticTrace[2u].kind ==
                kisak::database::SemanticTraceEventKind::Boundary,
        "ClipMap preserves begin/publication/boundary semantic trace ordering");

    const RetailFastfileCensus populated = Run(
        BuildFile(BuildClipMapZoneInflated(true)),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    const clipMap_t &map = *populated.worldClipMaps.front().asset;
    Require(map.planeCount == 1 && map.planes && map.planes[0].normal[0] == 1.0f &&
            map.numStaticModels == 1u && map.staticModelList &&
            map.numMaterials == 1u && map.materials &&
            std::string_view(map.materials[0].material) == "clip_material" &&
            map.numLeafs == 1u && map.leafs && map.leafs[0].brushContents == 5 &&
            map.vertCount == 1u && map.verts && map.verts[0][2] == 3.0f &&
            map.triCount == 1 && map.triIndices && map.triIndices[2] == 2u &&
            map.borderCount == 1 && map.borders && map.borders[0].start == 4.0f &&
            map.aabbTreeCount == 1 && map.aabbTrees &&
            map.aabbTrees[0].materialIndex == 2u &&
            map.numSubModels == 1u && map.cmodels && map.cmodels[0].radius == 8.0f &&
            map.visibility && map.visibility[0] == 0xaau && map.visibility[3] == 0xddu,
        "ClipMap family retains canonical simple child arrays in generated order under tiny step budgets");

    const RetailFastfileCensus nested = Run(
        BuildFile(BuildClipMapZoneInflated(false, true)),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    const clipMap_t &nestedMap = *nested.worldClipMaps.front().asset;
    Require(nestedMap.mapEnts && nestedMap.mapEnts->name &&
            std::string_view(nestedMap.mapEnts->name) ==
                "maps/mp/web_clipmap_entities.d3dbsp" &&
            nestedMap.mapEnts->numEntityChars == 4 &&
            nestedMap.mapEnts->entityString &&
            std::string_view(nestedMap.mapEnts->entityString, 4u) ==
                std::string_view("{ }\0", 4u) &&
            nested.registryAssetCount == 2u,
        "ClipMap loads and publishes its canonical nested MapEnts dependency before the parent");

    const RetailFastfileCensus aliased = Run(
        BuildFile(BuildClipMapZoneInflated(false, false, true)),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    Require(aliased.worldClipMaps.size() == 2u &&
            aliased.worldClipMaps[0u].published &&
            aliased.worldClipMaps[1u].published &&
            aliased.worldClipMaps[0u].serializedReference == 0xfffffffeu &&
            aliased.worldClipMaps[0u].insertPointerBlock4Offset == 24u &&
            aliased.worldClipMaps[1u].pointerAlias &&
            aliased.worldClipMaps[1u].identity ==
                aliased.worldClipMaps[0u].identity &&
            aliased.worldClipMaps[1u].asset.get() ==
                aliased.worldClipMaps[0u].asset.get() &&
            aliased.completedAssetCount == 2u &&
            aliased.nextBodyIndex == 2u && aliased.nextBodyType == 32u,
        "Load_ClipMapPtr publishes its insertion cell and converts a later reference to the canonical asset");

    std::vector<std::uint8_t> malformedInflated = BuildClipMapZoneInflated();
    SetU32(malformedInflated, 84u, UINT32_MAX); // clipMap_t::planeCount
    RetailFastfileCensusJob malformed;
    const std::vector<std::uint8_t> malformedFile = BuildFile(malformedInflated);
    Require(malformed.BeginStreaming(RetailCensusMode::WorldAssetLoader) ==
            RetailCensusError::None &&
            malformed.FeedSource(malformedFile, true) == RetailCensusError::None,
        "malformed ClipMap fixture starts");
    while (malformed.Progress() == RetailCensusProgress::Running)
        (void)malformed.Step({5u, 1u});
    RetailFastfileCensus unavailable;
    Require(malformed.Failure() == RetailCensusError::ClipMapCountInvalid &&
            !malformed.TakeResult(unavailable),
        "invalid ClipMap child counts fail atomically without exposing partial ownership");
}

void TestCanonicalComWorldLoader()
{
    using namespace kisak::fastfile;

    const RetailFastfileCensus nullRoot = Run(
        BuildFile(BuildComWorldZoneInflated(0u)),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    Require(nullRoot.completedAssetCount == 1u &&
            nullRoot.nextBodyIndex == 1u && nullRoot.nextBodyType == 32u &&
            nullRoot.worldComWorlds.size() == 1u &&
            nullRoot.worldComWorlds[0u].nullRoot &&
            !nullRoot.worldComWorlds[0u].asset &&
            !nullRoot.worldComWorlds[0u].published,
        "Load_ComWorldPtr preserves a null top-level root without allocating or publishing");

    const RetailFastfileCensus zeroLights = Run(
        BuildFile(BuildComWorldZoneInflated()),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    const RetailPublishedComWorld &zero = zeroLights.worldComWorlds.front();
    Require(zero.published && zero.asset && zero.storage &&
            zero.serializedReference == 0xffffffffu &&
            zero.headerBlock0Offset == 0u &&
            std::string_view(zero.asset->name) ==
                "maps/mp/web_comworld.d3dbsp" &&
            zero.asset->isInUse == 1 &&
            zero.asset->primaryLightCount == 0u &&
            zero.asset->primaryLights == nullptr && zero.identity != 0u,
        "Load_ComWorld publishes the canonical 16-byte body with a null zero-light array");

    const RetailFastfileCensus allocatedZeroLights = Run(
        BuildFile(BuildComWorldZoneInflated(
            0xffffffffu, false, false, true)),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    Require(allocatedZeroLights.worldComWorlds[0u].asset->primaryLightCount == 0u &&
            allocatedZeroLights.worldComWorlds[0u].asset->primaryLights != nullptr,
        "a non-null serialized primaryLights marker retains native zero-count allocation semantics");

    const RetailFastfileCensus multiple = Run(
        BuildFile(BuildComWorldZoneInflated(
            0xffffffffu, false, true)),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    const RetailPublishedComWorld &lit = multiple.worldComWorlds.front();
    Require(lit.published && lit.asset && lit.asset->primaryLightCount == 3u &&
            lit.asset->primaryLights && lit.asset->primaryLights[0u].type == 1u &&
            lit.asset->primaryLights[1u].type == 2u &&
            lit.asset->primaryLights[2u].type == 3u &&
            lit.asset->primaryLights[0u].radius == 128.0f &&
            lit.asset->primaryLights[2u].origin[0u] == 12.0f &&
            std::string_view(lit.asset->primaryLights[0u].defName) ==
                "light/web_primary" &&
            lit.asset->primaryLights[1u].defName ==
                lit.asset->primaryLights[0u].defName &&
            std::string_view(lit.asset->primaryLights[2u].defName) ==
                "web_primary" &&
            lit.lightDefNameBlock4Offsets[1u] ==
                lit.lightDefNameBlock4Offsets[0u] &&
            lit.lightDefNameBlock4Offsets[2u] ==
                lit.lightDefNameBlock4Offsets[0u] + 6u,
        "ComPrimaryLight array loading preserves scalar bytes plus exact and interior XString aliases");

    const RetailFastfileCensus inserted = Run(
        BuildFile(BuildComWorldZoneInflated(
            0xfffffffeu, true, true)),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    Require(inserted.worldComWorlds.size() == 2u &&
            inserted.worldComWorlds[0u].published &&
            inserted.worldComWorlds[0u].insertPointerBlock4Offset == 24u &&
            inserted.worldComWorlds[1u].pointerAlias &&
            inserted.worldComWorlds[1u].published &&
            inserted.worldComWorlds[1u].identity ==
                inserted.worldComWorlds[0u].identity &&
            inserted.worldComWorlds[1u].asset.get() ==
                inserted.worldComWorlds[0u].asset.get() &&
            inserted.completedAssetCount == 2u &&
            inserted.nextBodyIndex == 2u && inserted.nextBodyType == 32u,
        "Load_ComWorldPtr(-2) publishes its insertion cell and later aliases resolve to the canonical world");

    const auto expectFailure = [](std::vector<std::uint8_t> inflated,
                                  RetailCensusError expected,
                                  const char *message) {
        RetailFastfileCensusJob job;
        const auto file = BuildFile(std::move(inflated));
        Require(job.BeginStreaming(RetailCensusMode::WorldAssetLoader) ==
                RetailCensusError::None &&
                job.FeedSource(file, true) == RetailCensusError::None,
            "ComWorld negative fixture starts");
        for (std::uint32_t step = 0u; step < 10000u &&
             job.Progress() == RetailCensusProgress::Running; ++step)
            (void)job.Step();
        RetailFastfileCensus unavailable;
        unavailable.assetCount = 99u;
        if (job.Progress() != RetailCensusProgress::Failed ||
            job.Failure() != expected)
            std::cerr << "ComWorld negative stopped at "
                      << RetailCensusStageString(job.Stage()) << ": "
                      << RetailCensusErrorString(job.Failure()) << '\n';
        Require(job.Progress() == RetailCensusProgress::Failed &&
                job.Failure() == expected &&
                !job.TakeResult(unavailable) && unavailable.assetCount == 99u,
            message);
    };

    std::vector<std::uint8_t> malformedCount = BuildComWorldZoneInflated();
    SetU32(malformedCount, 84u, UINT32_MAX);
    expectFailure(std::move(malformedCount),
        RetailCensusError::ComWorldLightCountInvalid,
        "malformed ComWorld primary-light counts fail before allocation and publish nothing");

    std::vector<std::uint8_t> truncated =
        BuildComWorldZoneInflated(0xffffffffu, false, true);
    truncated.pop_back();
    expectFailure(std::move(truncated), RetailCensusError::InflateTruncated,
        "truncated ComPrimaryLight defName input fails atomically");

    expectFailure(BuildComWorldZoneInflated(
            0xffffffffu, false, false, false, std::string(255u, 'w')),
        RetailCensusError::ComWorldNameTooLong,
        "ComWorld XStrings enforce their explicit byte ceiling before publication");

    expectFailure(BuildComWorldZoneInflated(
            0xffffffffu, false, true, false,
            "maps/mp/web_comworld.d3dbsp", std::string(255u, 'l')),
        RetailCensusError::ComWorldLightNameTooLong,
        "ComPrimaryLight defName XStrings enforce their explicit byte ceiling before publication");
}

void TestCanonicalLightDefLoader()
{
    using namespace kisak::fastfile;

    const RetailFastfileCensus nullRoot = Run(
        BuildFile(BuildLightDefZoneInflated(0u)),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    Require(nullRoot.completedAssetCount == 1u &&
            nullRoot.nextBodyIndex == 1u && nullRoot.nextBodyType == 32u &&
            nullRoot.worldLightDefs.size() == 1u &&
            nullRoot.worldLightDefs[0u].nullRoot &&
            !nullRoot.worldLightDefs[0u].asset &&
            !nullRoot.worldLightDefs[0u].published,
        "Load_GfxLightDefPtr preserves a null four-byte root cell");

    const RetailFastfileCensus direct = Run(
        BuildFile(BuildLightDefZoneInflated()),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    const RetailPublishedLightDef &light = direct.worldLightDefs.front();
    Require(light.published && light.asset && light.storage &&
            light.serializedReference == 0xffffffffu &&
            light.headerBlock0Offset == 0u &&
            std::string_view(light.asset->name) ==
                "light/web_attenuation" &&
            light.asset->attenuation.image == nullptr &&
            light.asset->attenuation.samplerState == 7u &&
            light.asset->lmapLookupStart == 42 && light.identity != 0u,
        "Load_GfxLightDef(-1) publishes the canonical 16-byte body after its null image graph");

    const RetailFastfileCensus inserted = Run(
        BuildFile(BuildLightDefZoneInflated(
            0xfffffffeu, true)),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    Require(inserted.worldLightDefs.size() == 2u &&
            inserted.worldLightDefs[0u].published &&
            inserted.worldLightDefs[0u].insertPointerBlock4Offset == 24u &&
            inserted.worldLightDefs[1u].pointerAlias &&
            inserted.worldLightDefs[1u].published &&
            inserted.worldLightDefs[1u].identity ==
                inserted.worldLightDefs[0u].identity &&
            inserted.worldLightDefs[1u].asset.get() ==
                inserted.worldLightDefs[0u].asset.get() &&
            inserted.completedAssetCount == 2u,
        "Load_GfxLightDefPtr(-2) fills its insertion cell and later root aliases retain canonical identity");

    const RetailFastfileCensus strings = Run(
        BuildFile(BuildLightDefStringAliasZoneInflated()),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    Require(strings.worldLightDefs.size() == 3u &&
            std::string_view(strings.worldLightDefs[0u].asset->name) ==
                "light/web_name" &&
            strings.worldLightDefs[1u].asset->name ==
                strings.worldLightDefs[0u].asset->name &&
            std::string_view(strings.worldLightDefs[2u].asset->name) ==
                "web_name" &&
            strings.worldLightDefs[1u].nameBlock4Offset ==
                strings.worldLightDefs[0u].nameBlock4Offset &&
            strings.worldLightDefs[2u].nameBlock4Offset ==
                strings.worldLightDefs[0u].nameBlock4Offset + 6u,
        "GfxLightDef XStrings preserve direct, prior, and interior block-4 aliases");

    const RetailFastfileCensus inlineImage = Run(
        BuildFile(BuildLightDefZoneInflated(
            0xffffffffu, false, 0xffffffffu)),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    const RetailPublishedLightDef &imaged =
        inlineImage.worldLightDefs.front();
    Require(imaged.published && imaged.asset &&
            imaged.asset->attenuation.image &&
            imaged.attenuationImageIdentity != 0u &&
            inlineImage.worldImages.size() == 1u &&
            inlineImage.worldImages[0u].published &&
            inlineImage.worldImages[0u].asset.get() ==
                imaged.asset->attenuation.image &&
            inlineImage.worldImages[0u].asset->texture.basemap == nullptr &&
            inlineImage.worldImages[0u].asset->width == 4u &&
            std::string_view(inlineImage.worldImages[0u].asset->name) ==
                "web/light_attenuation_image",
        "non-null attenuation loads a canonical GfxImage dependency without a renderer texture handle");

    const RetailFastfileCensus imageAlias = Run(
        BuildFile(BuildLightDefImageAliasZoneInflated()),
        5u, 1u, 1u, RetailCensusMode::WorldAssetLoader);
    Require(imageAlias.worldLightDefs.size() == 2u &&
            imageAlias.worldImages.size() == 2u &&
            imageAlias.worldImages[0u].published &&
            imageAlias.worldImages[0u].assetInsertPointerBlock4Offset == 48u &&
            imageAlias.worldImages[1u].pointerAlias &&
            imageAlias.worldImages[1u].identity ==
                imageAlias.worldImages[0u].identity &&
            imageAlias.worldLightDefs[0u].asset->attenuation.image ==
                imageAlias.worldLightDefs[1u].asset->attenuation.image,
        "Load_GfxLightImage resolves a prior image insertion alias through the canonical image registry");

    const auto expectFailure = [](std::vector<std::uint8_t> inflated,
                                  RetailCensusError expected,
                                  const char *message) {
        RetailFastfileCensusJob job;
        const auto file = BuildFile(std::move(inflated));
        Require(job.BeginStreaming(RetailCensusMode::WorldAssetLoader) ==
                RetailCensusError::None &&
                job.FeedSource(file, true) == RetailCensusError::None,
            "LightDef negative fixture starts");
        for (std::uint32_t step = 0u; step < 10000u &&
             job.Progress() == RetailCensusProgress::Running; ++step)
            (void)job.Step({5u, 1u});
        RetailFastfileCensus unavailable;
        unavailable.assetCount = 99u;
        if (job.Progress() != RetailCensusProgress::Failed ||
            job.Failure() != expected)
            std::cerr << "LightDef negative stopped at "
                      << RetailCensusStageString(job.Stage()) << ": "
                      << RetailCensusErrorString(job.Failure()) << '\n';
        Require(job.Progress() == RetailCensusProgress::Failed &&
                job.Failure() == expected &&
                !job.TakeResult(unavailable) && unavailable.assetCount == 99u,
            message);
    };

    expectFailure(BuildLightDefZoneInflated(
            0xffffffffu, false, Block4Token(1000u)),
        RetailCensusError::LightDefImageInvalid,
        "an unresolved attenuation image fails before LightDef publication");
    expectFailure(BuildLightDefZoneInflated(
            0xffffffffu, false, 0xffffffffu, true),
        RetailCensusError::ImageLayoutUnsupported,
        "a malformed canonical image dependency fails the LightDef atomically");
    std::vector<std::uint8_t> truncated = BuildLightDefZoneInflated(
        0xffffffffu, false, 0xffffffffu);
    truncated.pop_back();
    expectFailure(std::move(truncated), RetailCensusError::RecordTruncated,
        "truncated LightDef dependency data exposes no partial result");
}

void TestWorldTechniqueSetPrefixBoundary()
{
    using namespace kisak::fastfile;
    const auto published = Run(
        BuildFile(BuildWorldTechniquePrefixInflated()), 7u, 2u, 3u,
        RetailCensusMode::WorldTechniqueSetPrefix);
    Require(published.worldTechniqueSets.size() == 2u &&
        published.worldTechniqueSets[0].name == ",web/mc_l_sm_r0c0s0" &&
        published.worldTechniqueSets[1].name == ",web/mc_l_sm_r0c0s1" &&
        published.worldTechniqueSets[0].published &&
        published.worldTechniqueSets[1].published &&
        published.completedAssetCount == 2u &&
        published.worldRegistryAliasCount == 2u &&
        published.worldRegistryDefinedAliasCount == 2u &&
        published.nextBodyIndex == 2u && published.nextBodyType == 3u &&
        published.stoppedBeforeDifferentWorldAssetType,
        "consecutive zero-dependency technique sets publish before XModel");

    const auto dependency = Run(
        BuildFile(BuildWorldTechniquePrefixInflated(false, true)), 7u, 2u, 3u,
        RetailCensusMode::WorldTechniqueSetPrefix);
    Require(dependency.worldTechniqueSets.size() == 2u &&
        dependency.worldTechniqueSets[0].published &&
        !dependency.worldTechniqueSets[1].published &&
        dependency.completedAssetCount == 1u &&
        dependency.stoppedBeforeWorldTechniqueDependency,
        "later technique dependency preserves prior publication only");

    const auto malformed = BuildFile(
        BuildWorldTechniquePrefixInflated(false, false, true));
    RetailFastfileCensusJob job;
    Require(job.BeginStreaming(RetailCensusMode::WorldTechniqueSetPrefix) ==
        RetailCensusError::None, "invalid world technique fixture starts");
    Require(job.FeedSource(malformed, true) == RetailCensusError::None,
        "invalid world technique source accepted");
    while (job.Progress() == RetailCensusProgress::Running) (void)job.Step();
    Require(job.Failure() == RetailCensusError::TechniqueSetLayoutUnsupported,
        "malformed later technique set fails closed");
}

void TestWorldXModelPrefixBoundary()
{
    using namespace kisak::fastfile;
    const auto result = Run(
        BuildFile(BuildWorldXModelPrefixInflated()), 7u, 2u, 3u,
        RetailCensusMode::WorldXModelPrefix);
    const RetailWorldXModel &model = result.worldXModels.at(0u);
    Require(result.worldTechniqueSets.size() == 2u &&
        result.completedAssetCount == 2u &&
        result.worldRegistryAliasCount == 3u &&
        result.worldRegistryDefinedAliasCount == 2u &&
        result.stoppedBeforeWorldXModelDependency &&
        std::string(result.unsupportedOperation) == "Load_XSurfaceArray",
        "XModel mode reserves asset two without publishing it");
    Require(model.headerTraversed && model.skeletonPrefixTraversed &&
        model.stoppedBeforeSurfaceArray && model.name == "web/xmodel_wall" &&
        model.numBones == 1u && model.numRootBones == 1u &&
        model.surfaceCount == 2u && model.lodCount == 1 &&
        model.boneNames == std::vector<std::string>{"tag_origin"} &&
        model.boundaryInflatedOffset == 738u &&
        result.block4CursorAtBoundary == 164u,
        "bounded XModel header and skeleton prefix retain exact metadata");

    RetailFastfileCensusJob invalidBoneAlias;
    Require(invalidBoneAlias.BeginStreaming(
        RetailCensusMode::WorldXModelPrefix) == RetailCensusError::None,
        "invalid XModel bone alias fixture starts");
    const auto invalidBoneAliasFile = BuildFile(
        BuildWorldXModelPrefixInflated(false, true));
    Require(invalidBoneAlias.FeedSource(invalidBoneAliasFile, true) ==
        RetailCensusError::None, "invalid XModel bone alias source is accepted");
    while (invalidBoneAlias.Progress() == RetailCensusProgress::Running)
        (void)invalidBoneAlias.Step();
    Require(invalidBoneAlias.Failure() ==
        RetailCensusError::XModelScriptStringAliasInvalid,
        "unpublished XModel bone alias fails closed");

    const auto malformed = BuildFile(BuildWorldXModelPrefixInflated(true));
    RetailFastfileCensusJob job;
    Require(job.BeginStreaming(RetailCensusMode::WorldXModelPrefix) ==
        RetailCensusError::None, "invalid XModel fixture starts");
    Require(job.FeedSource(malformed, true) == RetailCensusError::None,
        "invalid XModel source accepted");
    while (job.Progress() == RetailCensusProgress::Running) (void)job.Step();
    Require(job.Failure() == RetailCensusError::XModelBoundsInvalid,
        "invalid XModel bounds fail closed");
}

void TestWorldXSurfacePrefixBoundary()
{
    using namespace kisak::fastfile;
    const auto result = Run(
        BuildFile(BuildWorldXSurfacePrefixInflated()), 7u, 2u, 3u,
        RetailCensusMode::WorldXSurfacePrefix);
    const RetailWorldXModel &model = result.worldXModels.at(0u);
    Require(model.headerTraversed && model.skeletonPrefixTraversed &&
        model.surfaceHeadersTraversed && model.surfaceDependenciesTraversed &&
        model.materialHandlesTraversed && model.stoppedBeforeMaterialDependency &&
        !model.stoppedBeforeSurfaceArray &&
        std::string(result.unsupportedOperation) == "Load_Material",
        "XSurface mode reaches the first inline material body");
    Require(model.surfaces.size() == 2u && model.totalVertices == 6u &&
        model.totalTriangles == 2u && model.totalRigidVertLists == 2u &&
        model.totalCollisionNodes == 1u && model.totalCollisionLeaves == 1u &&
        model.materialReferences ==
            std::vector<std::uint32_t>{0xffffffffu, 0x40000001u},
        "XSurface mode retains aggregate geometry and material order");
    const RetailXSurface &first = model.surfaces[0];
    const RetailXSurface &second = model.surfaces[1];
    Require(first.vertCount == 3u && first.triCount == 1u &&
        first.rigidVertLists.size() == 1u &&
        first.rigidVertLists[0].collisionTree.traversed &&
        first.rigidVertLists[0].collisionTree.nodeCount == 1u &&
        first.rigidVertLists[0].collisionTree.leafCount == 1u &&
        first.verticesHash != 2166136261u && first.indicesHash != 2166136261u &&
        second.rigidVertLists.size() == 1u &&
        second.rigidVertLists[0].collisionTree.reference == 0u,
        "surface records retain rigid-list and collision evidence");
    Require(model.surfacesBlock4Offset == 164u &&
        first.verticesBlock7Offset == 0u && first.vertListsBlock4Offset == 276u &&
        first.rigidVertLists[0].collisionTree.headerBlock4Offset == 288u &&
        first.rigidVertLists[0].collisionTree.nodesBlock4Offset == 336u &&
        first.rigidVertLists[0].collisionTree.leafsBlock4Offset == 352u &&
        first.indicesBlock8Offset == 0u && second.verticesBlock7Offset == 96u &&
        second.vertListsBlock4Offset == 356u && second.indicesBlock8Offset == 16u &&
        model.materialHandlesBlock4Offset == 368u &&
        model.boundaryInflatedOffset == 1144u &&
        model.surfacePayloadBytes == 406u &&
        result.block4CursorAtBoundary == 376u,
        "XSurface traversal retains exact physical and logical boundaries");

    RetailFastfileCensusJob layoutJob;
    const auto invalidLayout = BuildFile(BuildWorldXSurfacePrefixInflated(true));
    Require(layoutJob.BeginStreaming(RetailCensusMode::WorldXSurfacePrefix) ==
        RetailCensusError::None, "invalid XSurface fixture starts");
    Require(layoutJob.FeedSource(invalidLayout, true) == RetailCensusError::None,
        "invalid XSurface fixture accepted");
    while (layoutJob.Progress() == RetailCensusProgress::Running)
        (void)layoutJob.Step();
    Require(layoutJob.Failure() == RetailCensusError::XSurfaceLayoutUnsupported,
        "surface pointer/count mismatch fails closed");

    RetailFastfileCensusJob collisionJob;
    const auto invalidCollision = BuildFile(
        BuildWorldXSurfacePrefixInflated(false, true));
    Require(collisionJob.BeginStreaming(RetailCensusMode::WorldXSurfacePrefix) ==
        RetailCensusError::None, "invalid collision fixture starts");
    Require(collisionJob.FeedSource(invalidCollision, true) == RetailCensusError::None,
        "invalid collision fixture accepted");
    while (collisionJob.Progress() == RetailCensusProgress::Running)
        (void)collisionJob.Step();
    Require(collisionJob.Failure() == RetailCensusError::XSurfaceCollisionInvalid,
        "invalid collision scale fails closed");

    RetailCensusLimits limits;
    limits.maxXModelSurfacePayloadBytes = 100u;
    RetailFastfileCensusJob payloadJob;
    const auto oversized = BuildFile(BuildWorldXSurfacePrefixInflated());
    Require(payloadJob.BeginStreaming(RetailCensusMode::WorldXSurfacePrefix, limits) ==
        RetailCensusError::None, "tight XSurface payload limit starts");
    Require(payloadJob.FeedSource(oversized, true) == RetailCensusError::None,
        "tight XSurface payload source accepted");
    while (payloadJob.Progress() == RetailCensusProgress::Running)
        (void)payloadJob.Step();
    Require(payloadJob.Failure() == RetailCensusError::XSurfacePayloadLimit,
        "XSurface payload ceiling is enforced before traversal");
}

void TestWorldXModelDependenciesBoundary()
{
    using namespace kisak::fastfile;
    const auto result = Run(
        BuildFile(BuildWorldXModelDependenciesInflated()), 7u, 2u, 3u,
        RetailCensusMode::WorldXModelDependencies);
    const RetailWorldXModel &model = result.worldXModels.at(0u);
    Require(model.published && model.materialsTraversed &&
        model.collisionSurfacesTraversed && model.boneInfoTraversed &&
        model.physPresetTraversed && model.physGeomsTraversed &&
        model.identity == 6u && result.completedAssetCount == 3u &&
        result.worldRegistryAliasCount == 6u &&
        result.worldRegistryDefinedAliasCount == 6u &&
        result.unsupportedOperation == nullptr,
        "M26 publishes the XModel only after its complete dependency chain");
    Require(model.materials.size() == 2u &&
        model.materials[0].name == "web/material_a" &&
        model.materials[0].images.size() == 1u &&
        model.materials[0].images[0].name == "synthetic_xmodel_color" &&
        model.materials[0].images[0].loadDefTraversed &&
        model.materials[0].images[0].published &&
        model.materials[0].images[0].asset &&
        model.materials[0].images[0].asset->texture.basemap == nullptr &&
        result.worldImages.size() == 1u &&
        result.worldImages[0u].asset.get() ==
            model.materials[0].images[0].asset.get() &&
        std::string_view(result.worldImages[0u].asset->name) ==
            "synthetic_xmodel_color" &&
        model.materials[1].name == "web/material_b" &&
        model.materials[1].images.empty() &&
        model.materialIdentities ==
            std::vector<std::uint32_t>{4u, 5u, 4u, 5u, 4u, 5u},
        "M26 resolves two inline materials, their image, and four aliases");
    const auto uncompressed = Run(
        BuildFile(BuildWorldXModelDependenciesInflated(
            false, false, false, false, true)),
        7u, 2u, 3u, RetailCensusMode::WorldXModelDependencies);
    Require(uncompressed.worldXModels[0].published &&
        uncompressed.worldXModels[0].materials[0].images[0].format == 0x16u &&
        uncompressed.worldXModels[0].materials[0].images[0].resourceBytes == 0u,
        "bounded zero-resource X8R8G8B8 image metadata publishes safely");
    WebEngineXModelMaterialImageBinding colorMap;
    Require(WebEngine_SelectXModelColorMap(
            model, model.materialIdentities.front(), colorMap) ==
            WebEngineXModelMaterialResult::Success &&
        colorMap.materialName == "web/material_a" &&
        colorMap.imageName == "synthetic_xmodel_color" &&
        colorMap.imagePath == "images/synthetic_xmodel_color.iwi" &&
        colorMap.materialIdentity == 4u && colorMap.imageIdentity == 3u &&
        colorMap.semantic == WEB_ENGINE_TEXTURE_SEMANTIC_COLOR_MAP,
        "M28 follows the first surface material to one typed external color map");
    Require(model.collisionSurfaces.size() == 1u &&
        model.collisionSurfaces[0].traversed &&
        model.collisionTriangleCount == 1u &&
        model.collisionPayloadBytes == 92u &&
        model.collisionSurfaces[0].trianglesHash != 2166136261u &&
        model.boneInfoHash != 2166136261u,
        "M26 retains bounded collision and bone-info traversal evidence");
    Require(model.surfaces[0].renderPayloadRetained &&
        model.surfaces[1].renderPayloadRetained &&
        std::all_of(
            model.surfaces.begin() + 2u, model.surfaces.end(),
            [](const RetailXSurface &surface) {
                return !surface.renderPayloadRetained &&
                    surface.retainedPackedVertices.empty() &&
                    surface.retainedPackedIndices.empty();
            }),
        "M29 retains only bounded packed XSurfaces in the declared first LOD");

    auto requireFailure = [](std::vector<std::uint8_t> inflated,
                             RetailCensusError expected,
                             const char *message) {
        RetailFastfileCensusJob job;
        const auto file = BuildFile(inflated);
        Require(job.BeginStreaming(RetailCensusMode::WorldXModelDependencies) ==
            RetailCensusError::None, "M26 failure fixture starts");
        Require(job.FeedSource(file, true) == RetailCensusError::None,
            "M26 failure source accepted");
        while (job.Progress() == RetailCensusProgress::Running) (void)job.Step();
        Require(job.Failure() == expected, message);
    };
    requireFailure(
        BuildWorldXModelDependenciesInflated(true),
        RetailCensusError::XModelImageAliasInvalid,
        "undefined material image alias fails closed");
    requireFailure(
        BuildWorldXModelDependenciesInflated(false, true),
        RetailCensusError::XModelCollisionInvalid,
        "invalid model collision bounds fail closed");
    requireFailure(
        BuildWorldXModelDependenciesInflated(false, false, true),
        RetailCensusError::XModelBoneInfoInvalid,
        "invalid bone info fails closed");

    const auto withPhysics = Run(
        BuildFile(BuildWorldXModelDependenciesInflated(
            false, false, false, true)),
        7u, 2u, 3u, RetailCensusMode::WorldXModelDependencies);
    const RetailWorldXModel &physicsModel = withPhysics.worldXModels.at(0u);
    Require(physicsModel.published && physicsModel.identity == 7u &&
        physicsModel.physPresetTraversed &&
        physicsModel.physPresetIdentity == 6u &&
        physicsModel.physPreset.name == "web/phys_sandbag" &&
        physicsModel.physPreset.soundAliasPrefix == "sandbag" &&
        physicsModel.physPreset.mass == 100.0f &&
        physicsModel.physPreset.tempDefaultToCylinder &&
        physicsModel.physPreset.traversed &&
        physicsModel.physPreset.published &&
        withPhysics.registryAssetCount == 7u &&
        withPhysics.worldRegistryAliasCount == 6u &&
        withPhysics.worldRegistryDefinedAliasCount == 6u &&
        withPhysics.completedAssetCount == 3u &&
        withPhysics.unsupportedOperation == nullptr,
        "M39 publishes an inline PhysPreset before its parent XModel");
    const auto withSharedPhysics = Run(
        BuildFile(BuildWorldXModelDependenciesInflated(
            false, false, false, true, false, false, false, true)),
        7u, 2u, 3u, RetailCensusMode::WorldXModelDependencies);
    const RetailWorldXModel &sharedPhysicsModel =
        withSharedPhysics.worldXModels.at(0u);
    Require(sharedPhysicsModel.published &&
        sharedPhysicsModel.physPreset.published &&
        sharedPhysicsModel.physPreset.insertPointerBlock4Offset != 0u &&
        withSharedPhysics.registryAssetCount == 7u &&
        withSharedPhysics.registryAliasCount == 7u &&
        withSharedPhysics.registryDefinedAliasCount == 7u,
        "M39 publishes the generated insertion alias for a shared PhysPreset");
    const auto withPhysicsGeometry = Run(
        BuildFile(BuildWorldXModelDependenciesInflated(
            false, false, false, true, false, false, false, false, true)),
        7u, 2u, 3u, RetailCensusMode::WorldXModelDependencies);
    const RetailWorldXModel &geometryModel =
        withPhysicsGeometry.worldXModels.at(0u);
    Require(geometryModel.published && geometryModel.physGeomsTraversed &&
        geometryModel.physGeomCount == 1u &&
        geometryModel.physGeomBrushCount == 0u &&
        geometryModel.physGeomPayloadBytes == 112u,
        "throughput loader completes a bounded PhysGeomList before XModel publication");
    requireFailure(
        BuildWorldXModelDependenciesInflated(
            false, false, false, true, false, true),
        RetailCensusError::PhysPresetValuesInvalid,
        "non-finite physics preset values fail closed");
    requireFailure(
        BuildWorldXModelDependenciesInflated(
            false, false, false, true, false, false, true),
        RetailCensusError::PhysPresetSoundAliasInvalid,
        "invalid physics preset sound alias prefixes fail closed");
}

void TestWorldPostXModelTechniqueSetBoundary()
{
    using namespace kisak::fastfile;
    const auto result = Run(
        BuildFile(BuildWorldPostXModelTechniqueSetInflated()),
        7u, 2u, 3u, RetailCensusMode::WorldPostXModelTechniqueSet);
    Require(result.worldXModels.at(0u).published &&
        result.worldPostXModelTechniqueSetAssetIndex == 3u &&
        result.worldPostXModelTechniqueSetPublished &&
        result.worldPostXModelTechniqueSetBodiesEntered == 2u &&
        result.worldPostXModelTechniqueSetCompletedCount == 2u &&
        result.worldTechniqueSets.size() == 4u,
        "M31 enters the consecutive typed technique-set run after the XModel");
    const RetailWorldTechniqueSet &firstPost = result.worldTechniqueSets[2u];
    const RetailWorldTechniqueSet &lastPost = result.worldTechniqueSets[3u];
    Require(firstPost.assetIndex == 3u &&
        firstPost.name == ",web/mc_l_sm_r0c0n0s0" &&
        firstPost.identity == 7u && firstPost.published &&
        lastPost.assetIndex == 4u &&
        lastPost.name == ",web/mc_l_sm_r0c0n0s1" &&
        lastPost.nullTechniqueReferences == 34u &&
        lastPost.inlineTechniqueReferences == 0u &&
        lastPost.identity == 8u && lastPost.published &&
        result.completedAssetCount == 5u &&
        result.registryAssetCount == 8u &&
        result.worldRegistryAliasCount == 8u &&
        result.worldRegistryDefinedAliasCount == 8u &&
        result.nextBodyIndex == 5u && result.nextBodyType == 16u &&
        result.nextBodyReference == 0xffffffffu &&
        result.stoppedBeforeDifferentWorldAssetType &&
        std::string(result.unsupportedOperation) ==
            "Load_XAssetHeader(non-technique-set)",
        "M31 publishes the zero-dependency run and stops before the next asset class");

    RetailFastfileCensusJob malformed;
    Require(malformed.BeginStreaming(RetailCensusMode::WorldPostXModelTechniqueSet) ==
        RetailCensusError::None, "malformed M31 fixture starts");
    const auto malformedFile = BuildFile(
        BuildWorldPostXModelTechniqueSetInflated(true));
    Require(malformed.FeedSource(malformedFile, true) == RetailCensusError::None,
        "malformed M31 source is accepted");
    while (malformed.Progress() == RetailCensusProgress::Running)
        (void)malformed.Step();
    RetailFastfileCensus unavailable;
    unavailable.completedAssetCount = 99u;
    Require(malformed.Failure() == RetailCensusError::TechniqueSetLayoutUnsupported &&
        !malformed.TakeResult(unavailable) && unavailable.completedAssetCount == 99u,
        "malformed post-XModel set cannot expose the prior dependency prefix");

    RetailFastfileCensusJob malformedLater;
    Require(malformedLater.BeginStreaming(
            RetailCensusMode::WorldPostXModelTechniqueSet) ==
        RetailCensusError::None, "malformed later M31 fixture starts");
    const auto malformedLaterFile = BuildFile(
        BuildWorldPostXModelTechniqueSetInflated(false, false, true));
    Require(malformedLater.FeedSource(malformedLaterFile, true) ==
        RetailCensusError::None, "malformed later M31 source is accepted");
    while (malformedLater.Progress() == RetailCensusProgress::Running)
        (void)malformedLater.Step();
    Require(malformedLater.Failure() ==
            RetailCensusError::TechniqueSetLayoutUnsupported &&
        !malformedLater.TakeResult(unavailable),
        "malformed later post-XModel set exposes no partial run");

    RetailFastfileCensusJob wrongType;
    Require(wrongType.BeginStreaming(RetailCensusMode::WorldPostXModelTechniqueSet) ==
        RetailCensusError::None, "wrong-type M31 fixture starts");
    const auto wrongTypeFile = BuildFile(BuildWorldXModelDependenciesInflated());
    Require(wrongType.FeedSource(wrongTypeFile, true) == RetailCensusError::None,
        "wrong-type M31 source is accepted");
    while (wrongType.Progress() == RetailCensusProgress::Running)
        (void)wrongType.Step();
    Require(wrongType.Failure() == RetailCensusError::PostXModelAssetUnsupported,
        "M31 rejects a missing post-XModel technique-set run");

    const auto dependency = Run(
        BuildFile(BuildWorldPostXModelTechniqueSetInflated(false, true)),
        7u, 2u, 3u, RetailCensusMode::WorldPostXModelTechniqueSet);
    const RetailWorldTechniqueSet &blocked = dependency.worldTechniqueSets.back();
    Require(dependency.worldXModels.at(0u).published && !blocked.published &&
        blocked.firstTechniqueSlot == 4u &&
        blocked.inlineTechniqueReferences == 1u &&
        dependency.completedAssetCount == 3u &&
        dependency.worldRegistryAliasCount == 7u &&
        dependency.worldRegistryDefinedAliasCount == 6u &&
        std::string(dependency.unsupportedOperation) == "Load_MaterialTechnique",
        "M31 stops conservatively before a nested technique body");

    const auto laterDependency = Run(
        BuildFile(BuildWorldPostXModelTechniqueSetInflated(
            false, false, false, true)),
        7u, 2u, 3u, RetailCensusMode::WorldPostXModelTechniqueSet);
    const RetailWorldTechniqueSet &laterBlocked =
        laterDependency.worldTechniqueSets.back();
    Require(laterDependency.worldTechniqueSets[2u].published &&
        !laterBlocked.published && laterBlocked.assetIndex == 4u &&
        laterBlocked.firstTechniqueSlot == 7u &&
        laterDependency.worldPostXModelTechniqueSetBodiesEntered == 2u &&
        laterDependency.worldPostXModelTechniqueSetCompletedCount == 1u &&
        laterDependency.completedAssetCount == 4u &&
        laterDependency.worldRegistryAliasCount == 8u &&
        laterDependency.worldRegistryDefinedAliasCount == 7u &&
        std::string(laterDependency.unsupportedOperation) ==
            "Load_MaterialTechnique",
        "M31 preserves the published prefix when a later set has a dependency");
}

void TestWorldSecondXModelPrefixBoundary()
{
    using namespace kisak::fastfile;
    const auto result = Run(
        BuildFile(BuildWorldSecondXModelPrefixInflated()),
        7u, 2u, 3u, RetailCensusMode::WorldSecondXModelPrefix);
    const RetailWorldXModel &first = result.worldXModels.at(0u);
    const RetailWorldXModel &second = result.worldXModels.at(1u);
    Require(first.published && first.name == "web/xmodel_wall" &&
        first.identity == 6u && first.surfaces.size() == 6u,
        "M32 preserves the complete first XModel result");
    Require(second.assetIndex == 5u &&
        second.name == "web/xmodel_second" &&
        second.headerTraversed && second.skeletonPrefixTraversed &&
        second.stoppedBeforeSurfaceArray && !second.published &&
        second.numBones == 1u && second.numRootBones == 1u &&
        second.surfaceCount == 3u && second.lodCount == 1 &&
        second.boneNames == std::vector<std::string>{"tag_origin"} &&
        result.block0HighWaterAtBoundary == 352u &&
        result.block4CursorAtBoundary == 1028u &&
        result.completedAssetCount == 5u &&
        result.registryAssetCount == 8u &&
        result.worldRegistryAliasCount == 9u &&
        result.worldRegistryDefinedAliasCount == 8u &&
        result.nextBodyIndex == 5u && result.nextBodyType == 3u &&
        std::string(result.unsupportedOperation) == "Load_XSurfaceArray",
        "M32 retains the second header and skeleton before XSurface traversal");

    RetailFastfileCensusJob invalid;
    Require(invalid.BeginStreaming(RetailCensusMode::WorldSecondXModelPrefix) ==
        RetailCensusError::None, "invalid M32 fixture starts");
    const auto invalidFile = BuildFile(
        BuildWorldSecondXModelPrefixInflated(true));
    Require(invalid.FeedSource(invalidFile, true) == RetailCensusError::None,
        "invalid M32 source is accepted");
    while (invalid.Progress() == RetailCensusProgress::Running)
        (void)invalid.Step();
    RetailFastfileCensus unavailable;
    Require(invalid.Failure() == RetailCensusError::XModelBoundsInvalid &&
        !invalid.TakeResult(unavailable),
        "invalid second-XModel bounds expose no partial public result");

    const std::uint32_t priorBoneNamesToken =
        0x40000001u + first.boneNamesBlock4Offset;
    const std::uint32_t priorPartClassificationToken =
        0x40000001u + first.partClassificationBlock4Offset;
    const std::uint32_t priorBaseMatToken =
        0x40000001u + first.baseMatBlock4Offset;
    const auto reused = Run(
        BuildFile(BuildWorldSecondXModelPrefixInflated(
            false, false, false, priorBoneNamesToken,
            priorPartClassificationToken, priorBaseMatToken)),
        7u, 2u, 3u, RetailCensusMode::WorldSecondXModelPrefix);
    const RetailWorldXModel &reusedFirst = reused.worldXModels.at(0u);
    const RetailWorldXModel &reusedSecond = reused.worldXModels.at(1u);
    Require(reusedFirst.published && reusedSecond.skeletonPrefixTraversed,
        "prior bone array resolution continues through the second skeleton");
    Require(reusedSecond.boneNames == std::vector<std::string>{"tag_origin"} &&
        reusedSecond.boneNameScriptStringIndices ==
            std::vector<std::uint16_t>{0u},
        "prior bone array resolution retains the typed script strings");
    Require(reusedSecond.boneNamesBlock4Offset == reusedFirst.boneNamesBlock4Offset,
        "prior bone array resolution retains its logical block-4 address");
    Require(reusedSecond.partClassification == reusedFirst.partClassification &&
        reusedSecond.partClassificationBlock4Offset ==
            reusedFirst.partClassificationBlock4Offset &&
        reusedSecond.baseMat == reusedFirst.baseMat &&
        reusedSecond.baseMatBlock4Offset == reusedFirst.baseMatBlock4Offset,
        "typed-array resolver reuses classification and base-matrix slices");
    Require(reusedSecond.boundaryInflatedOffset ==
            second.boundaryInflatedOffset - 35u &&
        std::string(reused.unsupportedOperation) == "Load_XSurfaceArray",
        "prior skeleton-array resolution consumes no inline payload");

    RetailFastfileCensusJob invalidSecondBoneAlias;
    Require(invalidSecondBoneAlias.BeginStreaming(
        RetailCensusMode::WorldSecondXModelPrefix) == RetailCensusError::None,
        "invalid second-XModel bone alias fixture starts");
    const auto invalidSecondBoneAliasFile = BuildFile(
        BuildWorldSecondXModelPrefixInflated(false, true));
    Require(invalidSecondBoneAlias.FeedSource(
        invalidSecondBoneAliasFile, true) == RetailCensusError::None,
        "invalid second-XModel bone alias source is accepted");
    while (invalidSecondBoneAlias.Progress() == RetailCensusProgress::Running)
        (void)invalidSecondBoneAlias.Step();
    Require(invalidSecondBoneAlias.Failure() ==
        RetailCensusError::XModelScriptStringAliasInvalid,
        "unpublished second-XModel bone alias fails closed");
}

void TestWorldSecondXSurfacePrefixBoundary()
{
    using namespace kisak::fastfile;
    const auto result = Run(
        BuildFile(BuildWorldSecondXModelPrefixInflated()),
        7u, 2u, 3u, RetailCensusMode::WorldSecondXSurfacePrefix);
    const RetailWorldXModel &first = result.worldXModels.at(0u);
    const RetailWorldXModel &second = result.worldXModels.at(1u);
    Require(first.published && first.name == "web/xmodel_wall" &&
        first.surfaces.size() == 6u,
        "M33 preserves the complete first XModel result");
    Require(second.headerTraversed && second.skeletonPrefixTraversed &&
        second.surfaceHeadersTraversed && second.surfaceDependenciesTraversed &&
        second.materialHandlesTraversed &&
        second.stoppedBeforeMaterialDependency &&
        !second.stoppedBeforeSurfaceArray && !second.published &&
        second.surfaces.size() == 3u &&
        second.materialReferences == std::vector<std::uint32_t>(
            3u, 0xffffffffu) &&
        second.totalVertices == 9u && second.totalTriangles == 3u &&
        second.totalRigidVertLists == 3u &&
        std::all_of(second.surfaces.begin(), second.surfaces.end(),
            [](const RetailXSurface &surface) {
                return surface.vertCount == 3u && surface.triCount == 1u &&
                    surface.rigidVertLists.size() == 1u &&
                    surface.dependenciesTraversed &&
                    !surface.renderPayloadRetained;
            }) &&
        std::string(result.unsupportedOperation) == "Load_Material",
        "M33 traverses the second XSurface prefix through material handles");

    RetailFastfileCensusJob invalid;
    Require(invalid.BeginStreaming(RetailCensusMode::WorldSecondXSurfacePrefix) ==
        RetailCensusError::None, "invalid M33 fixture starts");
    const auto invalidFile = BuildFile(
        BuildWorldSecondXModelPrefixInflated(false, false, true));
    Require(invalid.FeedSource(invalidFile, true) == RetailCensusError::None,
        "invalid M33 source is accepted");
    while (invalid.Progress() == RetailCensusProgress::Running)
        (void)invalid.Step();
    RetailFastfileCensus unavailable;
    Require(invalid.Failure() == RetailCensusError::XSurfaceLayoutUnsupported &&
        !invalid.TakeResult(unavailable),
        "invalid second-XSurface layout exposes no partial public result");
}

void TestWorldSecondXModelDependenciesBoundary()
{
    using namespace kisak::fastfile;
    const auto result = Run(
        BuildFile(BuildWorldSecondXModelDependenciesInflated()),
        7u, 2u, 3u, RetailCensusMode::WorldSecondXModelDependencies);
    const RetailWorldXModel &first = result.worldXModels.at(0u);
    const RetailWorldXModel &second = result.worldXModels.at(1u);
    Require(first.published && first.identity == 6u &&
        first.name == "web/xmodel_wall" && first.surfaces.size() == 6u,
        "M34 preserves the first published XModel");
    Require(second.published && second.identity == 10u &&
        second.name == "web/xmodel_second" &&
        second.materialsTraversed && second.materials.size() == 1u &&
        second.materials[0].name == "web/material_second" &&
        second.materials[0].identity == 9u &&
        second.materials[0].textures.size() == 1u &&
        second.materials[0].textures[0].resolved &&
        second.materials[0].textures[0].imageIdentity == 3u &&
        first.materials[0].images[0].textureInsertPointerBlock4Offset !=
            UINT32_MAX &&
        second.materialReferences == std::vector<std::uint32_t>{
            0xffffffffu, 0x400004f1u, 0x400004f1u} &&
        second.materialIdentities == std::vector<std::uint32_t>{9u, 9u, 9u} &&
        second.collisionSurfacesTraversed &&
        second.collisionSurfaces.size() == 1u &&
        second.collisionTriangleCount == 1u &&
        second.collisionPayloadBytes == 92u &&
        second.boneInfoTraversed && second.boneInfoHash != 0u &&
        second.physPresetTraversed && second.physGeomsTraversed &&
        result.completedAssetCount == 6u &&
        result.registryAssetCount == 10u &&
        result.worldRegistryAliasCount == 11u &&
        result.worldRegistryDefinedAliasCount == 11u &&
        result.nextBodyIndex == 6u && result.nextBodyType == 16u &&
        result.unsupportedOperation == nullptr,
        "M34 publishes the complete second XModel dependency chain");

    RetailFastfileCensusJob invalid;
    Require(invalid.BeginStreaming(
            RetailCensusMode::WorldSecondXModelDependencies) ==
        RetailCensusError::None, "invalid M34 fixture starts");
    const auto invalidFile = BuildFile(
        BuildWorldSecondXModelDependenciesInflated(true));
    Require(invalid.FeedSource(invalidFile, true) == RetailCensusError::None,
        "invalid M34 source is accepted");
    while (invalid.Progress() == RetailCensusProgress::Running)
        (void)invalid.Step();
    RetailFastfileCensus unavailable;
    Require(invalid.Failure() == RetailCensusError::MaterialTechniqueSetInvalid &&
        !invalid.TakeResult(unavailable),
        "invalid second-model material alias exposes no partial result");

    RetailFastfileCensusJob invalidImage;
    Require(invalidImage.BeginStreaming(
            RetailCensusMode::WorldSecondXModelDependencies) ==
        RetailCensusError::None, "invalid M38 image alias fixture starts");
    const auto invalidImageFile = BuildFile(
        BuildWorldSecondXModelDependenciesInflated(false, true));
    Require(invalidImage.FeedSource(invalidImageFile, true) ==
        RetailCensusError::None, "invalid M38 image alias source is accepted");
    while (invalidImage.Progress() == RetailCensusProgress::Running)
        (void)invalidImage.Step();
    Require(invalidImage.Failure() ==
            RetailCensusError::XModelImageAliasInvalid &&
        !invalidImage.TakeResult(unavailable),
        "undefined M38 image alias cannot publish its material or XModel");
}

void TestWorldXModelCollectionBoundary()
{
    using namespace kisak::fastfile;
    const auto file = BuildFile(BuildWorldXModelCollectionInflated());
    const auto result = Run(
        file, 7u, 2u, 3u, RetailCensusMode::WorldXModelCollection);
    Require(result.worldXModels.size() == 3u,
        "M35 stores every completed XModel in one collection");
    const RetailWorldXModel &first = result.worldXModels.at(0u);
    const RetailWorldXModel &second = result.worldXModels.at(1u);
    const RetailWorldXModel &third = result.worldXModels.at(2u);
    Require(first.published && first.assetIndex == 2u && first.identity == 6u &&
        first.rendererPayloadSelected &&
        first.rendererPayloadAvailable &&
        second.published && second.assetIndex == 5u && second.identity == 10u &&
        !second.rendererPayloadSelected && second.rendererPayloadAvailable &&
        third.published && third.assetIndex == 6u && third.identity == 11u &&
        !third.rendererPayloadSelected && !third.rendererPayloadAvailable &&
        third.name == "web/xmodel_third" && third.surfaceCount == 0u &&
        third.surfaceHeadersTraversed && third.surfaceDependenciesTraversed &&
        third.materialHandlesTraversed && third.materialsTraversed &&
        third.collisionSurfacesTraversed && third.boneInfoTraversed &&
        third.physPresetTraversed && third.physGeomsTraversed &&
        result.completedAssetCount == 7u && result.registryAssetCount == 11u &&
        result.worldRegistryAliasCount == 12u &&
        result.worldRegistryDefinedAliasCount == 12u &&
        result.nextBodyIndex == 7u && result.nextBodyType == 16u &&
        result.stoppedBeforeDifferentWorldAssetType &&
        result.unsupportedOperation == nullptr,
        "M35 loops the complete loader through the consecutive third model");
    Require(std::any_of(first.surfaces.begin(), first.surfaces.end(),
                [](const RetailXSurface &surface) {
                    return surface.renderPayloadRetained;
                }) &&
            std::any_of(second.surfaces.begin(), second.surfaces.end(),
                [](const RetailXSurface &surface) {
                    return surface.renderPayloadRetained;
                }),
        "the loader retains bounded selectable payloads without changing selection");

    RetailFastfileCensusJob malformed;
    Require(malformed.BeginStreaming(RetailCensusMode::WorldXModelCollection) ==
        RetailCensusError::None, "invalid M35 fixture starts");
    const auto invalidFile = BuildFile(
        BuildWorldXModelCollectionInflated(true));
    Require(malformed.FeedSource(invalidFile, true) == RetailCensusError::None,
        "invalid M35 source is accepted");
    while (malformed.Progress() == RetailCensusProgress::Running)
        (void)malformed.Step();
    RetailFastfileCensus unavailable;
    Require(malformed.Failure() == RetailCensusError::XModelBoundsInvalid &&
        !malformed.TakeResult(unavailable),
        "invalid third-model bounds expose no partial collection");

    RetailCensusLimits limits;
    limits.maxWorldXModels = 2u;
    RetailFastfileCensusJob bounded;
    Require(bounded.BeginStreaming(
            RetailCensusMode::WorldXModelCollection, limits) ==
        RetailCensusError::None, "bounded M35 fixture starts");
    Require(bounded.FeedSource(file, true) == RetailCensusError::None,
        "bounded M35 source is accepted");
    while (bounded.Progress() == RetailCensusProgress::Running)
        (void)bounded.Step();
    Require(bounded.Failure() == RetailCensusError::XModelCollectionLimit &&
        !bounded.TakeResult(unavailable),
        "M35 rejects a collection that exceeds its explicit model limit");
}

void TestReusableWorldXModelLoader()
{
    using namespace kisak::fastfile;
    const auto result = Run(
        BuildFile(BuildReusableWorldXModelLoaderInflated()),
        7u, 2u, 3u, RetailCensusMode::WorldXModelLoader);
    Require(result.worldXModels.size() == 2u &&
        result.worldXModels[0].assetIndex == 2u &&
        result.worldXModels[0].published &&
        result.worldXModels[0].rendererPayloadSelected &&
        result.worldXModels[0].rendererPayloadAvailable &&
        result.worldXModels[0].resolvedMaterials.size() == 2u &&
        result.worldXModels[0].resolvedImages.size() == 1u &&
        result.worldXModels[1].assetIndex == 5u &&
        result.worldXModels[1].published &&
        !result.worldXModels[1].rendererPayloadSelected &&
        result.worldXModels[1].rendererPayloadAvailable &&
        result.worldXModels[1].resolvedMaterials.size() == 1u &&
        result.worldXModels[1].resolvedImages.size() == 1u,
        "the reusable loader handles XModels separated by technique-set runs");
    Require(result.worldXModels[0u].asset &&
            result.worldXModels[0u].canonicalName &&
            result.worldXModels[0u].asset->name ==
                result.worldXModels[0u].canonicalName->c_str() &&
            result.worldXModels[0u].asset->numBones ==
                result.worldXModels[0u].numBones &&
            result.worldXModels[0u].asset->numsurfs ==
                result.worldXModels[0u].surfaceCount &&
            result.worldXModels[0u].asset->materialHandles ==
                result.worldXModels[0u].canonicalMaterialHandles->data() &&
            result.worldXModels[0u].asset->materialHandles[0u] ==
                result.worldXModels[0u].materials[0u].asset.get(),
        "published XModels expose canonical scalar and material pointer identity");
    Material *aliasedMaterial = nullptr;
    for (const RetailWorldXModel &model : result.worldXModels)
    {
        const auto found = std::find_if(
            model.materials.begin(), model.materials.end(),
            [&](const RetailXModelMaterial &material) {
                return material.identity ==
                    result.worldXModels[1u].materialIdentities[0u];
            });
        if (found != model.materials.end())
        {
            aliasedMaterial = found->asset.get();
            break;
        }
    }
    Require(aliasedMaterial != nullptr && result.worldXModels[1u].asset &&
            result.worldXModels[1u].asset->materialHandles &&
            result.worldXModels[1u].asset->materialHandles[0u] ==
                aliasedMaterial,
        "canonical XModel material aliases preserve prior asset pointer identity");
    Require(result.worldTechniqueSets.size() == 5u &&
        result.worldTechniqueSets.back().assetIndex == 6u &&
        result.worldTechniqueSets.back().name ==
            ",web/reusable_xmodel_loader_tail" &&
        result.worldTechniqueSets.back().published &&
        result.completedAssetCount == 7u &&
        result.nextBodyIndex == 7u && result.nextBodyType == 16u &&
        result.stoppedBeforeDifferentWorldAssetType &&
        result.unsupportedOperation == nullptr,
        "the supported dispatcher resumes after every completed XModel");
    Require(result.semanticTrace.size() == 15u &&
        result.semanticTrace.front().kind ==
            kisak::database::SemanticTraceEventKind::AssetBegin &&
        result.semanticTrace.front().assetType == ASSET_TYPE_TECHNIQUE_SET &&
        result.semanticTrace.front().assetIndex == 0u &&
        result.semanticTrace[1u].kind ==
            kisak::database::SemanticTraceEventKind::AssetPublish &&
        result.semanticTrace[1u].name == ",web/mc_l_sm_r0c0s0" &&
        result.semanticTrace.back().kind ==
            kisak::database::SemanticTraceEventKind::Boundary &&
        result.semanticTrace.back().assetType == ASSET_TYPE_GFXWORLD &&
        result.semanticTrace.back().assetIndex == 7u &&
        result.semanticTraceHash ==
            kisak::database::SemanticTraceHash(result.semanticTrace),
        "the reusable loader emits a normalized begin/publish/boundary trace");
    Require(std::any_of(result.worldXModels[0].surfaces.begin(),
                result.worldXModels[0].surfaces.end(),
                [](const RetailXSurface &surface) {
                    return surface.renderPayloadRetained;
                }) &&
            std::any_of(result.worldXModels[1].surfaces.begin(),
                result.worldXModels[1].surfaces.end(),
                [](const RetailXSurface &surface) {
                    return surface.renderPayloadRetained;
                }),
        "each bounded model is selectable while only one is initially active");
    WebEngineXModelDrawList secondDrawList;
    const WebEngineXModelDrawListResult secondDrawResult =
        WebEngine_BuildXModelDrawList(result.worldXModels[1], secondDrawList);
    if (secondDrawResult != WebEngineXModelDrawListResult::Success)
    {
        std::cerr << "second retained XModel draw list: "
                  << WebEngine_XModelDrawListResultString(secondDrawResult)
                  << "; lods=" << result.worldXModels[1].lodCount
                  << "; first=" << result.worldXModels[1].lods[0].surfaceIndex
                  << "; count=" << result.worldXModels[1].lods[0].surfaceCount
                  << "; surfaces=" << result.worldXModels[1].surfaces.size()
                  << "; materials=" << result.worldXModels[1].materialIdentities.size()
                  << '\n';
        for (const RetailXSurface &surface : result.worldXModels[1].surfaces)
        {
            WebEngineConvertedXModelSurface converted;
            WebEngineXModelMaterialImageBinding binding;
            const auto materialResult = WebEngine_SelectXModelColorMap(
                result.worldXModels[1],
                result.worldXModels[1].materialIdentities[surface.index],
                binding);
            const WebEnginePackedXSurfaceView view{
                surface.retainedPackedVertices.data(),
                surface.retainedPackedVertices.size(),
                surface.vertCount,
                surface.retainedPackedIndices.data(),
                surface.retainedPackedIndices.size(),
                surface.triCount,
                result.worldXModels[1].materialIdentities[surface.index],
            };
            std::cerr << "  surface " << surface.index
                      << " retained=" << surface.renderPayloadRetained
                      << " material="
                      << WebEngine_XModelMaterialResultString(materialResult)
                      << " conversion=" << WebEngine_XModelSurfaceResultString(
                            WebEngine_ConvertPackedXModelSurface(view, converted))
                      << '\n';
        }
    }
    Require(secondDrawResult == WebEngineXModelDrawListResult::Success &&
        secondDrawList.renderer.draws.size() == 3u &&
        secondDrawList.renderer.vertices.size() == 9u &&
        secondDrawList.renderer.indices.size() == 9u,
        "the second retained XModel builds a complete renderer draw list");

    RetailCensusLimits limits;
    limits.maxRetainedXModelRendererBytes = 1u;
    RetailFastfileCensusJob bounded;
    const auto file = BuildFile(BuildReusableWorldXModelLoaderInflated());
    Require(bounded.BeginStreaming(
            RetailCensusMode::WorldXModelLoader, limits) ==
            RetailCensusError::None,
        "the renderer-retention budget fixture starts");
    Require(bounded.FeedSource(file, true) == RetailCensusError::None,
        "the renderer-retention budget source is accepted");
    while (bounded.Progress() == RetailCensusProgress::Running)
        (void)bounded.Step();
    RetailFastfileCensus boundedResult;
    Require(bounded.Progress() == RetailCensusProgress::Succeeded &&
        bounded.TakeResult(boundedResult) &&
        boundedResult.worldXModels.size() == 2u &&
        std::none_of(
            boundedResult.worldXModels.begin(),
            boundedResult.worldXModels.end(),
            [](const RetailWorldXModel &model) {
                return model.rendererPayloadAvailable;
            }),
        "the aggregate renderer-byte ceiling disables retention without rejecting inventory");

    RetailCensusLimits traceLimits;
    traceLimits.maxSemanticTraceEntries = 1u;
    RetailFastfileCensusJob traceBounded;
    Require(traceBounded.BeginStreaming(
            RetailCensusMode::WorldXModelLoader, traceLimits) ==
        RetailCensusError::None,
        "the semantic-trace ceiling fixture starts");
    Require(traceBounded.FeedSource(file, true) == RetailCensusError::None,
        "the semantic-trace ceiling source is accepted");
    while (traceBounded.Progress() == RetailCensusProgress::Running)
        (void)traceBounded.Step();
    RetailFastfileCensus traceUnavailable;
    Require(traceBounded.Failure() == RetailCensusError::SemanticTraceLimit &&
        !traceBounded.TakeResult(traceUnavailable),
        "the semantic trace is bounded and unavailable after overflow");
}

void TestReusableWorldRawFileLoader()
{
    using namespace kisak::fastfile;
    using namespace kisak::database;
    const auto file = BuildFile(BuildReusableWorldRawFileLoaderInflated());
    const auto result = Run(
        file,
        7u, 2u, 3u, RetailCensusMode::WorldAssetLoader);

    Require(result.worldRawFiles.size() == 1u,
        "the reusable dispatcher reaches one canonical RawFile");
    const RetailWorldRawFile &rawFile = result.worldRawFiles.front();
    Require(rawFile.assetIndex == 2u && rawFile.published && rawFile.asset &&
            rawFile.name == "scripts/web_rawfile.gsc" &&
            rawFile.length == 4 && rawFile.bufferStorage &&
            rawFile.bufferStorage->size() == 5u &&
            std::string_view(rawFile.asset->name) == rawFile.name &&
            rawFile.asset->name == rawFile.nameStorage->c_str() &&
            rawFile.asset->len == 4 &&
            rawFile.asset->buffer == rawFile.bufferStorage->data() &&
            std::string_view(rawFile.asset->buffer, 4u) == "test",
        "RawFile publication owns storage behind the canonical engine ABI");
    Require(result.completedAssetCount == 3u &&
            result.nextBodyIndex == 3u &&
            result.nextBodyType == ASSET_TYPE_GFXWORLD &&
            result.stoppedAfterCanonicalRawFile &&
            result.stoppedBeforeDifferentWorldAssetType &&
            result.semanticTrace.size() == 7u &&
            result.semanticTrace[4u].kind ==
                SemanticTraceEventKind::AssetBegin &&
            result.semanticTrace[4u].assetType == ASSET_TYPE_RAWFILE &&
            result.semanticTrace[5u].kind ==
                SemanticTraceEventKind::AssetPublish &&
            result.semanticTrace[5u].name == "scripts/web_rawfile.gsc" &&
            result.semanticTrace.back().kind ==
                SemanticTraceEventKind::Boundary &&
            result.semanticTraceContractHash ==
                SemanticTraceContractHash(result.semanticTrace),
        "RawFile publish returns to the dispatcher and stops at GfxWorld");

    std::vector<SemanticTraceEntry> nativeProjection;
    SetSemanticTraceObserver(CollectSemanticTrace, &nativeProjection);
    ResetNativeSemanticTraceContext();
    EnterNativeSemanticTraceAsset(rawFile.assetIndex, ASSET_TYPE_RAWFILE);
    for (const std::size_t index : {4u, 5u})
    {
        const SemanticTraceEntry &webEntry = result.semanticTrace[index];
        EmitNativeSemanticTrace(
            webEntry.kind,
            0u,
            0u,
            webEntry.streamBlock,
            webEntry.streamOffset,
            webEntry.relatedBlock,
            webEntry.relatedOffset,
            webEntry.name);
    }
    LeaveNativeSemanticTraceAsset();
    ClearSemanticTraceObserver();
    const std::span<const SemanticTraceEntry> webProjection(
        result.semanticTrace.data() + 4u, 2u);
    Require(nativeProjection.size() == 2u &&
            SemanticTraceContractHash(nativeProjection) ==
                SemanticTraceContractHash(webProjection),
        "native generated-loader observer and web RawFile trace share a contract");

    const auto runFile = BuildFile(
        BuildReusableWorldRawFileLoaderInflated(2u));
    const RetailFastfileCensus run = Run(
        runFile, 7u, 2u, 3u, RetailCensusMode::WorldAssetLoader);
    Require(run.worldRawFiles.size() == 2u &&
            run.worldRawFiles[0u].assetIndex == 2u &&
            run.worldRawFiles[1u].assetIndex == 3u &&
            run.worldRawFiles[1u].name ==
                "scripts/web_rawfile_second.gsc" &&
            run.worldRawFiles[0u].published &&
            run.worldRawFiles[1u].published &&
            run.worldRawFiles[0u].identity !=
                run.worldRawFiles[1u].identity &&
            run.completedAssetCount == 4u &&
            run.nextBodyIndex == 4u &&
            run.nextBodyType == ASSET_TYPE_GFXWORLD &&
            run.stoppedAfterCanonicalRawFile &&
            run.semanticTrace.size() == 9u,
        "the canonical RawFile operation handles a consecutive top-level run");

    RetailCensusLimits limits;
    limits.maxRawFileBytes = 3u;
    RetailFastfileCensusJob bounded;
    Require(bounded.BeginStreaming(RetailCensusMode::WorldAssetLoader, limits) ==
            RetailCensusError::None &&
            bounded.FeedSource(file, true) == RetailCensusError::None,
        "bounded RawFile fixture starts");
    while (bounded.Progress() == RetailCensusProgress::Running)
        (void)bounded.Step();
    RetailFastfileCensus unavailable;
    Require(bounded.Failure() == RetailCensusError::RawFilePayloadLimit &&
            !bounded.TakeResult(unavailable),
        "RawFile payloads are rejected atomically above the explicit ceiling");

    limits = {};
    limits.maxRawFiles = 1u;
    RetailFastfileCensusJob collectionBounded;
    Require(collectionBounded.BeginStreaming(
                RetailCensusMode::WorldAssetLoader, limits) ==
            RetailCensusError::None &&
            collectionBounded.FeedSource(runFile, true) ==
                RetailCensusError::None,
        "RawFile collection-ceiling fixture starts");
    while (collectionBounded.Progress() == RetailCensusProgress::Running)
        (void)collectionBounded.Step();
    Require(collectionBounded.Failure() ==
                RetailCensusError::RawFileCollectionLimit &&
            !collectionBounded.TakeResult(unavailable),
        "a RawFile run cannot exceed its explicit collection ceiling");

    const RetailFastfileCensus interleaved = Run(
        BuildFile(BuildInterleavedWorldRawFileLoaderInflated()),
        7u, 2u, 3u, RetailCensusMode::WorldAssetLoader);
    Require(interleaved.worldRawFiles.size() == 2u &&
            interleaved.worldRawFiles[0u].assetIndex == 1u &&
            interleaved.worldRawFiles[0u].published &&
            interleaved.worldRawFiles[1u].assetIndex == 3u &&
            interleaved.worldRawFiles[1u].published &&
            interleaved.worldXModels.size() == 1u &&
            interleaved.worldXModels[0u].assetIndex == 2u &&
            interleaved.worldXModels[0u].published &&
            interleaved.completedAssetCount == 4u &&
            interleaved.nextBodyIndex == 4u &&
            interleaved.nextBodyType == ASSET_TYPE_GFXWORLD &&
            interleaved.stoppedAfterCanonicalRawFile &&
            interleaved.semanticTrace.size() == 9u &&
            interleaved.semanticTrace[6u].kind ==
                SemanticTraceEventKind::AssetBegin &&
            interleaved.semanticTrace[6u].assetType == ASSET_TYPE_RAWFILE &&
            interleaved.semanticTrace[6u].assetIndex == 3u,
        "dispatcher resumes RawFile loading after an intervening XModel");
}

void TestReusableWorldXAnimPartsLoader()
{
    using namespace kisak::fastfile;
    using namespace kisak::database;
    const auto file = BuildFile(BuildReusableWorldXAnimLoaderInflated());
    const RetailFastfileCensus result = Run(
        file, 7u, 2u, 3u, RetailCensusMode::WorldAssetLoader);
    Require(result.worldXAnimParts.size() == 3u,
        "the dispatcher retains two bodies and one XAnimParts alias");
    const RetailPublishedXAnimParts &first = result.worldXAnimParts[0u];
    const RetailPublishedXAnimParts &alias = result.worldXAnimParts[1u];
    const RetailPublishedXAnimParts &wide = result.worldXAnimParts[2u];
    Require(first.assetIndex == 1u && first.published && first.asset &&
            first.storage && first.storage->name &&
            *first.storage->name == "web/xanim_full" &&
            first.asset->name == first.storage->name->c_str() &&
            first.serializedReference == 0xfffffffeu &&
            first.insertPointerBlock4Offset != UINT32_MAX &&
            first.payloadBytes == 109u && first.identity == 2u,
        "shared XAnimParts publishes the canonical header after complete payload traversal");
    Require(alias.assetIndex == 2u && alias.published && alias.pointerAlias &&
            alias.identity == first.identity &&
            alias.asset.get() == first.asset.get() &&
            alias.storage.get() == first.storage.get() &&
            alias.serializedReference ==
                (0x40000000u | (first.insertPointerBlock4Offset + 1u)),
        "Load_XAnimPartsPtr alias conversion resolves the shared insertion cell");

    const XAnimParts &parts = *first.asset;
    Require(parts.dataByteCount == 2u && parts.dataShortCount == 2u &&
            parts.dataIntCount == 1u && parts.randomDataShortCount == 2u &&
            parts.randomDataByteCount == 2u &&
            parts.randomDataIntCount == 1u && parts.indexCount == 3u &&
            parts.numframes == 10u && parts.bLoop && parts.bDelta &&
            parts.boneCount[9] == 2u && parts.notifyCount == 2u,
        "canonical XAnimParts preserves counts, flags, and the bone/name table shape");
    Require(parts.names && parts.names[0u] == 0u && parts.names[1u] == 1u &&
            parts.notify && parts.notify[0u].name == 0u &&
            parts.notify[0u].time == 0.25f &&
            parts.notify[1u].name == 1u &&
            parts.notify[1u].time == 0.75f,
        "bone names and notifications retain checked script-string indices");
    Require(parts.deltaPart && parts.deltaPart->trans && parts.deltaPart->quat &&
            parts.deltaPart->trans->size == 1u &&
            parts.deltaPart->trans->smallTrans == 1u &&
            parts.deltaPart->trans->u.frames.mins[1u] == 1.0f &&
            parts.deltaPart->trans->u.frames.size[2u] == 5.0f &&
            parts.deltaPart->trans->u.frames.frames._1 &&
            parts.deltaPart->trans->u.frames.frames._1[1u][2u] == 6u &&
            parts.deltaPart->quat->size == 1u &&
            parts.deltaPart->quat->u.frames.frames &&
            parts.deltaPart->quat->u.frames.frames[1u][1u] == 13,
        "delta translation and quaternion frame pointers use canonical flexible structures");
    const auto *transIndices = reinterpret_cast<const std::uint8_t *>(
        &parts.deltaPart->trans->u.frames.indices);
    const auto *quatIndices = reinterpret_cast<const std::uint8_t *>(
        &parts.deltaPart->quat->u.frames.indices);
    Require(transIndices[0u] == 0u && transIndices[1u] == 9u &&
            quatIndices[0u] == 0u && quatIndices[1u] == 9u,
        "packed low-frame delta indices remain inline after their canonical headers");
    Require(parts.dataByte[1u] == 0xa2u && parts.dataShort[1u] == 0x202 &&
            parts.dataInt[0u] == 0x01020304 &&
            parts.randomDataShort[1u] == 0x404 &&
            parts.randomDataByte[1u] == 0xb2u &&
            parts.randomDataInt[0u] == 0x11121314 &&
            parts.indices._1[2u] == 3u,
        "all generated packed animation arrays publish through canonical pointers");
    Require(wide.assetIndex == 3u && wide.published && wide.identity == 3u &&
            wide.asset && wide.asset->numframes == 300u &&
            wide.asset->dataByteCount == 0u && wide.asset->dataByte != nullptr &&
            wide.asset->indices._2 && wide.asset->indices._2[0u] == 257u &&
            wide.asset->indices._2[1u] == 299u,
        "wide-frame indices use ushort storage and preserve zero-length presence allocation");
    Require(result.completedAssetCount == 4u &&
            result.registryAssetCount == 3u &&
            result.registryAliasCount == 4u &&
            result.registryDefinedAliasCount == 4u &&
            result.nextBodyIndex == 4u &&
            result.nextBodyType == ASSET_TYPE_GFXWORLD &&
            result.stoppedBeforeDifferentWorldAssetType &&
            result.semanticTrace.size() == 7u &&
            result.semanticTrace.back().kind == SemanticTraceEventKind::Boundary,
        "XAnimParts bodies and aliases return to traversal before GfxWorld");

    RetailFastfileCensus copied = result;
    Require(copied.worldXAnimParts[0u].asset->dataByte[0u] == 0xa1u &&
            copied.worldXAnimParts[1u].asset.get() ==
                copied.worldXAnimParts[0u].asset.get(),
        "canonical XAnimParts ownership remains stable when a census result is copied");

    RetailCensusLimits limits;
    limits.maxXAnimPayloadBytes = 32u;
    RetailFastfileCensusJob bounded;
    Require(bounded.BeginStreaming(RetailCensusMode::WorldAssetLoader, limits) ==
            RetailCensusError::None &&
            bounded.FeedSource(file, true) == RetailCensusError::None,
        "bounded XAnimParts fixture starts");
    while (bounded.Progress() == RetailCensusProgress::Running)
        (void)bounded.Step();
    RetailFastfileCensus unavailable;
    Require(bounded.Failure() == RetailCensusError::XAnimPayloadLimit &&
            !bounded.TakeResult(unavailable),
        "XAnimParts payload limits fail atomically before publication");

    RetailFastfileCensusJob badBone;
    const auto badBoneFile = BuildFile(
        BuildReusableWorldXAnimLoaderInflated(true));
    Require(badBone.BeginStreaming(RetailCensusMode::WorldAssetLoader) ==
            RetailCensusError::None &&
            badBone.FeedSource(badBoneFile, true) == RetailCensusError::None,
        "invalid XAnimParts bone-name fixture starts");
    while (badBone.Progress() == RetailCensusProgress::Running)
        (void)badBone.Step();
    Require(badBone.Failure() == RetailCensusError::XAnimScriptStringInvalid &&
            !badBone.TakeResult(unavailable),
        "out-of-range XAnimParts script strings cannot publish the asset");

    RetailFastfileCensusJob badAlias;
    const auto badAliasFile = BuildFile(
        BuildReusableWorldXAnimLoaderInflated(false, true));
    Require(badAlias.BeginStreaming(RetailCensusMode::WorldAssetLoader) ==
            RetailCensusError::None &&
            badAlias.FeedSource(badAliasFile, true) == RetailCensusError::None,
        "invalid XAnimParts pointer-alias fixture starts");
    while (badAlias.Progress() == RetailCensusProgress::Running)
        (void)badAlias.Step();
    Require(badAlias.Failure() == RetailCensusError::XAnimAliasInvalid &&
            !badAlias.TakeResult(unavailable),
        "undefined XAnimParts pointer aliases fail closed");
}

void TestReusableWorldWeaponDefLoader()
{
    using namespace kisak::fastfile;
    using namespace kisak::database;
    const auto file = BuildFile(BuildReusableWorldWeaponLoaderInflated());
    const RetailFastfileCensus result = Run(
        file, 7u, 2u, 3u, RetailCensusMode::WorldAssetLoader);
    Require(result.worldWeapons.size() == 2u,
        "the dispatcher retains one canonical WeaponDef and one pointer alias");
    const RetailPublishedWeaponDef &first = result.worldWeapons[0u];
    const RetailPublishedWeaponDef &alias = result.worldWeapons[1u];
    Require(first.assetIndex == 1u && first.published && first.asset &&
            first.storage && first.storage->strings[0u] &&
            *first.storage->strings[0u] == "web/weapon_full" &&
            first.asset->szInternalName ==
                first.storage->strings[0u]->c_str() &&
            first.serializedReference == 0xfffffffeu &&
            first.insertPointerBlock4Offset != UINT32_MAX &&
            first.payloadBytes == 142u && first.identity == 2u,
        "shared WeaponDef publishes only after its strings and knot arrays complete");
    Require(alias.assetIndex == 2u && alias.published && alias.pointerAlias &&
            alias.identity == first.identity &&
            alias.asset.get() == first.asset.get() &&
            alias.storage.get() == first.storage.get() &&
            alias.serializedReference ==
                (0x40000000u | (first.insertPointerBlock4Offset + 1u)),
        "Load_WeaponDefPtr resolves the shared insertion cell to canonical ownership");

    const WeaponDef &weapon = *first.asset;
    Require(weapon.playerAnimType == 7 && weapon.weapType == WEAPTYPE_PROJECTILE &&
            weapon.weapClass == WEAPCLASS_PISTOL &&
            weapon.iReticleCenterSize == 31 && weapon.autoAimRange == 123.5f &&
            weapon.altWeaponIndex == 19u &&
            weapon.projExplosion == WEAPPROJEXP_NONE &&
            weapon.lowAmmoWarningThreshold == 0.25f &&
            weapon.fAdsAimPitch == 1.5f &&
            weapon.iPositionReloadTransTime == 77 &&
            weapon.iUseHintStringIndex == 11 && weapon.minDamage == 55 &&
            weapon.adsDofStart == 4.5f,
        "WeaponDef scalar spans decode into the canonical gameplay structure");
    Require(weapon.hideTags[0u] == 0u &&
            std::string_view(weapon.szDisplayName) == "WEAPON_WEB_FULL" &&
            std::string_view(weapon.szAmmoName) == "web_ammo" &&
            std::string_view(weapon.accuracyGraphName[0u]) ==
                "web_accuracy_zero" &&
            std::string_view(weapon.accuracyGraphName[1u]) ==
                "web_accuracy_one" &&
            std::string_view(weapon.szScript) == "web_weapon_script",
        "direct XStrings preserve generated Load_WeaponDef traversal order");
    Require(weapon.accuracyGraphKnotCount[0u] == 2 &&
            weapon.accuracyGraphKnotCount[1u] == 1 &&
            weapon.originalAccuracyGraphKnotCount[0u] == 9 &&
            weapon.originalAccuracyGraphKnotCount[1u] == 8 &&
            weapon.accuracyGraphKnots[0u] &&
            weapon.accuracyGraphKnots[0u][1u][1u] == 3.0f &&
            weapon.originalAccuracyGraphKnots[0u][1u][0u] == 6.0f &&
            weapon.accuracyGraphKnots[1u][0u][1u] == 9.0f &&
            weapon.originalAccuracyGraphKnots[1u][0u][0u] == 10.0f,
        "both graph pairs use accuracyGraphKnotCount while retaining original counts");
    Require(result.completedAssetCount == 3u &&
            result.nextBodyIndex == 3u &&
            result.nextBodyType == ASSET_TYPE_GFXWORLD &&
            result.stoppedBeforeDifferentWorldAssetType &&
            result.semanticTrace.size() == 5u &&
            result.semanticTrace[2u].kind == SemanticTraceEventKind::AssetBegin &&
            result.semanticTrace[2u].assetType == ASSET_TYPE_WEAPON &&
            result.semanticTrace[3u].kind == SemanticTraceEventKind::AssetPublish,
        "WeaponDef publication returns to the reusable dispatcher before GfxWorld");

    RetailFastfileCensus copied = result;
    Require(copied.worldWeapons[0u].asset->accuracyGraphKnots[0u][1u][0u] ==
                2.0f &&
            copied.worldWeapons[1u].asset.get() ==
                copied.worldWeapons[0u].asset.get(),
        "canonical WeaponDef ownership remains stable when a census result is copied");

    const RetailFastfileCensus dependencies = Run(
        BuildFile(BuildWeaponCanonicalAliasDependenciesInflated()),
        7u, 2u, 3u, RetailCensusMode::WorldAssetLoader);
    Require(dependencies.worldXModels.size() == 1u &&
            dependencies.worldXModels[0u].published &&
            dependencies.worldXModels[0u].asset &&
            std::string_view(dependencies.worldXModels[0u].asset->name) ==
                ",web/dep_model" &&
            dependencies.worldFxEffects.size() == 1u &&
            dependencies.worldFxEffects[0u].published &&
            dependencies.worldFxEffects[0u].asset &&
            std::string_view(dependencies.worldFxEffects[0u].asset->name) ==
                "web/dep_fx" &&
            dependencies.worldFxEffects[0u].asset->elemDefCountLooping == 1 &&
            dependencies.worldFxEffects[0u].asset->elemDefs != nullptr &&
            dependencies.worldFxEffects[0u].materials.size() == 1u &&
            dependencies.worldFxEffects[0u].materials[0u].published &&
            dependencies.worldFxEffects[0u].materials[0u].asset &&
            std::string_view(
                dependencies.worldFxEffects[0u].materials[0u].asset->info.name) ==
                "web/dep_material",
        "existing XModel, FX, and Material loaders expose canonical child objects");
    Require(dependencies.worldWeapons.size() == 1u &&
            dependencies.worldWeapons[0u].published &&
            dependencies.worldWeapons[0u].asset &&
            dependencies.worldWeapons[0u].asset->gunXModel[0u] ==
                dependencies.worldXModels[0u].asset.get() &&
            dependencies.worldWeapons[0u].asset->viewFlashEffect ==
                dependencies.worldFxEffects[0u].asset.get() &&
            dependencies.worldWeapons[0u].asset->reticleCenter ==
                dependencies.worldFxEffects[0u].materials[0u].asset.get() &&
            std::string_view(
                dependencies.worldWeapons[0u].asset->szOverlayName) ==
                "web/dep_material" &&
            dependencies.completedAssetCount == 4u &&
            dependencies.nextBodyType == ASSET_TYPE_GFXWORLD,
        "WeaponDef alias handles resolve to canonical child pointer identity");

    SyntheticWeaponSoundLookup soundCatalog;
    const RetailFastfileCensus sounds = Run(
        BuildFile(BuildWeaponCanonicalAliasDependenciesInflated(true)),
        7u, 2u, 3u, RetailCensusMode::WorldAssetLoader,
        {LookupSyntheticWeaponSound, &soundCatalog});
    const RetailPublishedWeaponDef &soundWeapon = sounds.worldWeapons[0u];
    Require(soundWeapon.published && soundWeapon.asset && soundWeapon.storage &&
            soundWeapon.asset->pickupSound ==
                reinterpret_cast<snd_alias_list_t *>(soundCatalog.pickup.data()) &&
            soundWeapon.asset->projIgnitionSound ==
                soundWeapon.asset->pickupSound &&
            soundWeapon.asset->bounceSound &&
            soundWeapon.asset->bounceSound[0u] ==
                reinterpret_cast<snd_alias_list_t *>(soundCatalog.bounce.data()) &&
            soundWeapon.asset->bounceSound[1u] ==
                soundWeapon.asset->pickupSound &&
            soundWeapon.asset->bounceSound[2u] == nullptr &&
            soundWeapon.storage->soundNames[0u] &&
            *soundWeapon.storage->soundNames[0u] == "web/pickup" &&
            soundWeapon.storage->bounceSoundNames[0u] &&
            *soundWeapon.storage->bounceSoundNames[0u] == "web/bounce" &&
            soundCatalog.pickupLookups == 3u &&
            soundCatalog.bounceLookups == 1u,
        "native sound-name cells, aliases, bounce arrays, and DB lookup order resolve canonically");

    RetailFastfileCensusJob missingSoundLookup;
    const auto soundFile = BuildFile(
        BuildWeaponCanonicalAliasDependenciesInflated(true));
    Require(missingSoundLookup.BeginStreaming(
                RetailCensusMode::WorldAssetLoader) == RetailCensusError::None &&
            missingSoundLookup.FeedSource(soundFile, true) ==
                RetailCensusError::None,
        "missing sound lookup fixture starts");
    while (missingSoundLookup.Progress() == RetailCensusProgress::Running)
        (void)missingSoundLookup.Step();
    RetailFastfileCensus missingSoundResult;
    Require(missingSoundLookup.Failure() ==
                RetailCensusError::WeaponSoundLookupFailed &&
            !missingSoundLookup.TakeResult(missingSoundResult),
        "sound-name lookup failure preserves atomic WeaponDef publication");

    auto requireFailure = [](const std::vector<std::uint8_t> &malformedFile,
                             RetailCensusError expected,
                             const RetailCensusLimits &limits,
                             const char *message) {
        RetailFastfileCensusJob job;
        Require(job.BeginStreaming(
                    RetailCensusMode::WorldAssetLoader, limits) ==
                    RetailCensusError::None &&
                job.FeedSource(malformedFile, true) == RetailCensusError::None,
            "malformed WeaponDef fixture starts");
        while (job.Progress() == RetailCensusProgress::Running)
            (void)job.Step();
        RetailFastfileCensus unavailable;
        Require(job.Failure() == expected && !job.TakeResult(unavailable), message);
    };

    const RetailCensusLimits defaultLimits;
    requireFailure(
        BuildFile(BuildReusableWorldWeaponLoaderInflated(true)),
        RetailCensusError::WeaponScriptStringInvalid,
        defaultLimits,
        "out-of-range WeaponDef script strings fail before publication");
    requireFailure(
        BuildFile(BuildReusableWorldWeaponLoaderInflated(false, true)),
        RetailCensusError::WeaponDependencyUnsupported,
        defaultLimits,
        "unpublished canonical WeaponDef dependencies are never silently nulled");
    requireFailure(
        BuildFile(BuildReusableWorldWeaponLoaderInflated(false, false, true)),
        RetailCensusError::WeaponAliasInvalid,
        defaultLimits,
        "undefined WeaponDef insertion aliases fail closed");
    RetailCensusLimits payloadLimits;
    payloadLimits.maxWeaponPayloadBytes = 64u;
    requireFailure(
        file,
        RetailCensusError::WeaponPayloadLimit,
        payloadLimits,
        "bounded WeaponDef strings and knot arrays fail atomically");
}

void TestReusableWorldMaterialTechniqueLoader()
{
    using namespace kisak::fastfile;
    const auto result = Run(
        BuildFile(BuildReusableWorldXModelLoaderInflated(true)),
        7u, 2u, 3u, RetailCensusMode::WorldXModelLoader);
    const RetailWorldTechniqueSet &set = result.worldTechniqueSets.back();
    Require(set.assetIndex == 6u && set.published &&
        set.inlineTechniqueReferences == 2u && set.techniques.size() == 2u &&
        set.techniques[0].slot == 4u && set.techniques[0].completed &&
        set.techniques[0].name == "web/reusable_first" &&
        set.techniques[1].slot == 28u && set.techniques[1].completed &&
        set.techniques[1].name == "web/reusable_second" &&
        set.techniques[0].passCount == 1u &&
        set.techniques[1].passCount == 1u &&
        set.techniques[0].argumentCount == 1u &&
        set.techniques[1].argumentCount == 1u,
        "M37 completes both reusable MaterialTechnique dependencies");
    Require(result.completedAssetCount == 7u &&
        result.nextBodyIndex == 7u && result.nextBodyType == 16u &&
        result.stoppedBeforeDifferentWorldAssetType &&
        !result.stoppedBeforeWorldTechniqueDependency &&
        result.unsupportedOperation == nullptr,
        "M37 publishes the parent only after returning to the shared dispatcher");

    RetailFastfileCensusJob malformed;
    Require(malformed.BeginStreaming(RetailCensusMode::WorldXModelLoader) ==
        RetailCensusError::None, "malformed M37 fixture starts");
    const auto malformedFile = BuildFile(
        BuildReusableWorldXModelLoaderInflated(true, true));
    Require(malformed.FeedSource(malformedFile, true) ==
        RetailCensusError::None, "malformed M37 source is accepted");
    while (malformed.Progress() == RetailCensusProgress::Running)
        (void)malformed.Step();
    RetailFastfileCensus unavailable;
    Require(malformed.Failure() ==
            RetailCensusError::ShaderProgramSignatureInvalid &&
        !malformed.TakeResult(unavailable),
        "an invalid second dependency cannot publish the parent technique set");
}

void TestReusableWorldFxLoader()
{
    using namespace kisak::fastfile;
    const RetailFastfileCensus result = Run(
        BuildFile(BuildReusableWorldFxLoaderInflated()),
        7u, 2u, 3u, RetailCensusMode::WorldAssetLoader);
    Require(result.worldFxEffects.size() == 1u &&
        result.worldFxEffects[0].assetIndex == 1u &&
        result.worldFxEffects[0].name == "web/fx_mark" &&
        result.worldFxEffects[0].published &&
        result.worldFxEffects[0].elemDefs.size() == 1u &&
        result.worldFxEffects[0].elemDefs[0].elemType == 9u &&
        result.worldFxEffects[0].elemDefs[0].visualReferences.size() == 2u &&
        result.worldFxEffects[0].elemDefs[0].visualIdentities[0] != 0u &&
        result.worldFxEffects[0].elemDefs[0].visualIdentities[1] != 0u &&
        result.worldFxEffects[0].materials.size() == 2u &&
        result.worldFxEffects[0].materials[0].published &&
        result.worldFxEffects[0].materials[0].textures.size() == 1u &&
        result.worldFxEffects[0].materials[0].textures[0].resolved &&
        result.worldFxEffects[0].materials[0].images.size() == 1u &&
        result.worldFxEffects[0].materials[0].images[0].name ==
            ",web/fx_mark_builtin" &&
        result.worldFxEffects[0].materials[0].images[0].published &&
        result.worldFxEffects[0].materials[1].published &&
        result.completedAssetCount == 2u &&
        result.nextBodyIndex == 2u && result.nextBodyType == 16u &&
        result.stoppedBeforeDifferentWorldAssetType,
        "FX visuals reuse the checked material and image dependency path");

    std::uint32_t firstVisualAlias = 0u;
    Require(EncodeZoneAliasToken(
                {4u,
                 result.worldFxEffects[0u].elemDefs[0u]
                     .visualArrayBlock4Offset,
                 4u},
                firstVisualAlias),
        "FX regression encodes the first block-4 Material visual cell");
    const RetailFastfileCensus aliased = Run(
        BuildFile(BuildReusableWorldFxLoaderInflated(
            false, firstVisualAlias)),
        7u, 2u, 3u, RetailCensusMode::WorldAssetLoader);
    const RetailWorldFxElemDef &aliasedElem =
        aliased.worldFxEffects.at(0u).elemDefs.at(0u);
    Require(aliasedElem.visualReferences.size() == 2u &&
            aliasedElem.visualReferences[1u] == firstVisualAlias &&
            aliasedElem.visualIdentities[0u] != 0u &&
            aliasedElem.visualIdentities[1u] ==
                aliasedElem.visualIdentities[0u] &&
            aliased.worldFxEffects[0u].materials.size() == 1u &&
            aliased.worldFxEffects[0u].published,
        "normal block-4 Material visual aliases dereference the already-patched visual cell");

    RetailFastfileCensusJob malformed;
    Require(malformed.BeginStreaming(RetailCensusMode::WorldAssetLoader) ==
        RetailCensusError::None, "malformed FX fixture starts");
    const auto malformedFile = BuildFile(
        BuildReusableWorldFxLoaderInflated(true));
    Require(malformed.FeedSource(malformedFile, true) ==
        RetailCensusError::None, "malformed FX fixture is accepted");
    while (malformed.Progress() == RetailCensusProgress::Running)
        (void)malformed.Step();
    RetailFastfileCensus unavailable;
    Require(malformed.Failure() == RetailCensusError::FxMaterialUnsupported &&
        !malformed.TakeResult(unavailable),
        "a malformed nested FX material cannot publish its parent effect");
}

void TestPositiveIncrementalCensus()
{
    const auto result = Run(BuildFile());
    Require(result.version == 5u, "version reported");
    Require(result.xfileSize == 1378265u && result.externalSize == 950499u,
        "XFile progress values reported without reinterpretation");
    Require(result.declaredBlockBytes == 910932u, "nine blocks summed safely");
    Require(result.blockSizes[0] == 498816u && result.blockSizes[4] == 407412u &&
        result.blockSizes[7] == 4224u && result.blockSizes[8] == 480u,
        "nonempty blocks retained");
    Require(result.scriptStringCount == 3u && result.scriptStringBytes == 15u,
        "null and inline script strings traversed");
    Require(result.assetCount == 5u, "complete asset table counted");
    Require(result.typeCounts[5] == 2u && result.typeCounts[4] == 1u &&
        result.typeCounts[22] == 1u && result.typeCounts[32] == 1u,
        "type census is exact");
    Require(result.inlineAssetReferences == 3u && result.sharedAssetReferences == 0u &&
        result.aliasAssetReferences == 1u && result.nullAssetReferences == 1u,
        "asset reference classes counted");
    Require(result.firstBodyIndex == 0u && result.firstBodyType == 5u &&
        result.firstBodyReference == 0xffffffffu && !result.stoppedBeforeAssetBody,
        "leading technique-set body is entered explicitly");
    Require(result.inflatedPrefixBytes == 44u + 16u + 12u + 15u + 40u,
        "body boundary offset excludes body bytes");
    Require(std::string(kisak::fastfile::RetailAssetTypeName(5u)) == "techset",
        "asset type diagnostic is stable");
    Require(result.techniqueSetName == "web/synthetic_techset" &&
        result.firstTechniqueSlot == 4u && result.techniquePassCount == 1u,
        "leading technique set and first technique are traversed");
    Require(result.vertexStreamCount == 3u &&
        result.vertexStreamRoutingHash == 0x5bc9b27cu &&
        result.vertexDeclarationPrepared &&
        result.vertexShaderName == "web_synthetic_vs" &&
        result.vertexShaderProgramDwords == 101u &&
        result.vertexShaderProgramHash != 0u,
        "vertex declaration and shader metadata are retained");
    Require(result.vertexShaderInstructionCount == 15u &&
        result.vertexShaderConstantCount == 2u &&
        result.pixelShaderName == "web_synthetic_vs" &&
        result.pixelShaderProgramDwords == 50u &&
        result.pixelShaderInstructionCount == 6u &&
        result.pixelShaderConstantCount == 1u &&
        result.shaderArgumentCount == 3u && result.shaderArgumentHash != 0u &&
        result.techniqueName == "web_synthetic2d",
        "paired shader and argument contracts are decoded");
    Require(result.shaderCompatibilitySelected &&
        result.shaderSubstitutionId == "webgl2.vertcol_simple2d.v1" &&
        result.vertexGlslHash != 0u && result.fragmentGlslHash != 0u,
        "strict D3D9 contract selects explicit WebGL2 sources");
    Require(result.assetTableBlock4Offset == 28u &&
        result.techniqueSetBlock0Offset == 0u &&
        result.techniqueBlock4Offset == 92u &&
        result.vertexDeclarationBlock4Offset == 120u &&
        result.vertexShaderBlock4Offset == 220u &&
        result.vertexShaderProgramBlock4Offset == 256u &&
        result.pixelShaderBlock4Offset == 660u &&
        result.pixelShaderProgramBlock4Offset == 676u &&
        result.shaderArgumentsBlock4Offset == 876u &&
        result.block0HighWaterAtBoundary == 148u &&
        result.imageTextureInsertPointerBlock4Offset == 1676u &&
        result.block4CursorAtBoundary == 1688u,
        "logical block allocations match generated-loader alignment");
    Require(result.completedAssetCount == 3u && result.techniqueSetPublished &&
        !result.stoppedBeforeShaderCreation && result.unsupportedOperation == nullptr,
        "technique sets, image, and material publish atomically");
    Require(result.materialTechniqueSetName == "web/material_techset" &&
        result.materialName == "web_cursor" &&
        result.imageName == "synthetic_engine_asset" &&
        result.imagePath == "images/synthetic_engine_asset.iwi" &&
        result.registryAliasCount == 4u && result.registryDefinedAliasCount == 4u,
        "material and selected image boundary is retained");
}

void TestFailure(std::vector<std::uint8_t> inflated,
    kisak::fastfile::RetailCensusError expected,
    const char *message)
{
    using namespace kisak::fastfile;
    const auto file = BuildFile(std::move(inflated));
    RetailFastfileCensusJob job;
    Require(job.BeginStreaming() == RetailCensusError::None, "negative starts");
    Require(job.FeedSource(file, true) == RetailCensusError::None, "negative source accepted");
    for (std::uint32_t step = 0u; step < 100u &&
        job.Progress() == RetailCensusProgress::Running; ++step)
        (void)job.Step();
    Require(job.Progress() == RetailCensusProgress::Failed && job.Failure() == expected, message);
}

void TestMalformedPrefixRecords()
{
    auto invalidBlock = BuildInflated();
    invalidBlock[8u] = 1u;
    kisak::fastfile::RetailCensusLimits limits;
    limits.maxBlockBytes = 1u;
    const auto file = BuildFile(invalidBlock);
    kisak::fastfile::RetailFastfileCensusJob job;
    Require(job.BeginStreaming(limits) == kisak::fastfile::RetailCensusError::None,
        "tight block-limit census starts");
    Require(job.FeedSource(file, true) == kisak::fastfile::RetailCensusError::None,
        "tight block fixture accepted");
    (void)job.Step();
    Require(job.Failure() == kisak::fastfile::RetailCensusError::BlockSizeLimit,
        "block ceiling enforced");

    auto badStringToken = BuildInflated();
    badStringToken[60u + 4u] = 1u;
    badStringToken[60u + 5u] = 0u;
    badStringToken[60u + 6u] = 0u;
    badStringToken[60u + 7u] = 0u;
    TestFailure(std::move(badStringToken),
        kisak::fastfile::RetailCensusError::ScriptStringReferenceUnsupported,
        "normal script-string references rejected at bounded census boundary");

    auto badAssetType = BuildInflated();
    constexpr std::size_t assetTableOffset = 44u + 16u + 12u + 15u;
    badAssetType[assetTableOffset] = 33u;
    TestFailure(std::move(badAssetType),
        kisak::fastfile::RetailCensusError::AssetTypeInvalid,
        "out-of-range asset type rejected");
}

void TestTechniqueTraversalFailures()
{
    auto unsupportedTechniqueReference = BuildInflated();
    constexpr std::size_t TECHNIQUE_SET_OFFSET = 127u;
    SetU32(unsupportedTechniqueReference,
        TECHNIQUE_SET_OFFSET + 12u + 4u * 4u, 0x40000011u);
    TestFailure(std::move(unsupportedTechniqueReference),
        kisak::fastfile::RetailCensusError::TechniqueReferenceUnsupported,
        "normal technique reference is rejected before traversal can skip it");

    auto invalidShaderSignature = BuildInflated();
    constexpr std::size_t SHADER_PROGRAM_OFFSET = 458u;
    SetU32(invalidShaderSignature, SHADER_PROGRAM_OFFSET, 0x00000101u);
    TestFailure(std::move(invalidShaderSignature),
        kisak::fastfile::RetailCensusError::ShaderProgramSignatureInvalid,
        "non-D3D vertex program is rejected at the shader boundary");

    auto unmatchedContract = BuildInflated();
    constexpr std::size_t VERTEX_CONSTANT_NAME_OFFSET = 591u;
    unmatchedContract[VERTEX_CONSTANT_NAME_OFFSET] = 'x';
    TestFailure(std::move(unmatchedContract),
        kisak::fastfile::RetailCensusError::ShaderSubstitutionUnsupported,
        "a decoded but unmatched shader pair cannot publish the asset");

    auto invalidPixelSignature = BuildInflated();
    constexpr std::size_t PIXEL_PROGRAM_OFFSET = 878u;
    SetU32(invalidPixelSignature, PIXEL_PROGRAM_OFFSET, 0xfffe0101u);
    TestFailure(std::move(invalidPixelSignature),
        kisak::fastfile::RetailCensusError::ShaderContractInvalid,
        "wrong-stage pixel bytecode is rejected before publication");

    auto invalidArgument = BuildInflated();
    constexpr std::size_t ARGUMENT_OFFSET = 1078u;
    SetU32(invalidArgument, ARGUMENT_OFFSET, 9u);
    TestFailure(std::move(invalidArgument),
        kisak::fastfile::RetailCensusError::ShaderArgumentLayoutUnsupported,
        "unknown material arguments fail closed");

    auto blockOverflow = BuildInflated();
    constexpr std::size_t BLOCK4_SIZE_OFFSET = 8u + 4u * 4u;
    SetU32(blockOverflow, BLOCK4_SIZE_OFFSET, 271u);
    TestFailure(std::move(blockOverflow),
        kisak::fastfile::RetailCensusError::ZoneBlockOverflow,
        "shader program allocation cannot exceed declared block four");
}

void TestEnvelopeAndAtomicity()
{
    using namespace kisak::fastfile;
    auto file = BuildFile();
    file[0] = 'X';
    RetailFastfileCensusJob job;
    Require(job.BeginStreaming() == RetailCensusError::None, "bad magic job starts");
    Require(job.FeedSource(file, true) == RetailCensusError::None, "bad magic source accepted");
    (void)job.Step();
    Require(job.Failure() == RetailCensusError::InvalidMagic, "bad magic rejected");
    RetailFastfileCensus prior;
    prior.assetCount = 99u;
    Require(!job.TakeResult(prior) && prior.assetCount == 99u,
        "failure cannot partially replace caller result");

    RetailFastfileCensusJob budgetJob;
    Require(budgetJob.BeginStreaming() == RetailCensusError::None, "budget job starts");
    const auto report = budgetJob.Step({0u, 1u});
    Require(report.error == RetailCensusError::InvalidStepBudget,
        "zero work budget rejected deterministically");
}

void TestShaderCompatibilityDecoder()
{
    using namespace kisak::web;
    const auto vertexBytes = ShaderBytes(BuildShaderProgram(true));
    const auto pixelBytes = ShaderBytes(BuildShaderProgram(false));
    D3D9ShaderContract vertex;
    D3D9ShaderContract pixel;
    Require(DecodeD3D9Shader(vertexBytes, {}, vertex) == ShaderDecodeError::None &&
        DecodeD3D9Shader(pixelBytes, {}, pixel) == ShaderDecodeError::None,
        "bounded decoder accepts generated D3D9 contracts");
    Require(vertex.stage == ShaderStage::Vertex && vertex.instructionCount == 15u &&
        vertex.constants.size() == 2u && pixel.stage == ShaderStage::Pixel &&
        pixel.instructionCount == 6u && pixel.constants.size() == 1u,
        "decoder retains stage, instruction and CTAB metadata");
    WebGL2ShaderSubstitution substitution;
    Require(SelectWebGL2ShaderSubstitution(vertex, pixel, 0x5bc9b27cu, substitution) &&
        std::string(substitution.id) == "webgl2.vertcol_simple2d.v1" &&
        std::string(substitution.vertexSource).starts_with("#version 300 es") &&
        std::string(substitution.fragmentSource).starts_with("#version 300 es"),
        "structural pair selects owned WebGL2 GLSL contract");
    WebGL2ShaderSubstitution lookedUp;
    Require(LookupWebGL2ShaderSubstitution(substitution.id, lookedUp) &&
        lookedUp.vertexSourceHash == substitution.vertexSourceHash &&
        lookedUp.fragmentSourceHash == substitution.fragmentSourceHash &&
        std::string(lookedUp.vertexSource) == substitution.vertexSource &&
        std::string(lookedUp.fragmentSource) == substitution.fragmentSource,
        "stable ID resolves only to the compiled-in compatibility source");
    WebGL2ShaderSubstitution retainedLookup = lookedUp;
    Require(!LookupWebGL2ShaderSubstitution("webgl2.unregistered", retainedLookup) &&
        retainedLookup.id == lookedUp.id,
        "unknown shader IDs fail without replacing a prior registry result");
    WebGL2ShaderSubstitution prior = substitution;
    Require(!SelectWebGL2ShaderSubstitution(vertex, pixel, 0u, prior) &&
        prior.id == substitution.id,
        "routing mismatch fails without replacing a prior selection");

    auto truncated = vertexBytes;
    truncated.pop_back();
    D3D9ShaderContract unchanged;
    unchanged.programHash = 99u;
    Require(DecodeD3D9Shader(truncated, {}, unchanged) == ShaderDecodeError::InvalidArgument &&
        unchanged.programHash == 99u,
        "non-DWORD shader fails atomically");
    auto invalidCtab = vertexBytes;
    invalidCtab[12u] = 0u;
    Require(DecodeD3D9Shader(invalidCtab, {}, unchanged) ==
            ShaderDecodeError::InvalidConstantTable && unchanged.programHash == 99u,
        "malformed CTAB fails atomically");
}

void TestOwnedWorldSurfaceIfRequested(
    const char *path,
    const char *worldPath)
{
    if (!path || *path == '\0') return;
    using namespace kisak::fastfile;
    std::ifstream input(path, std::ios::binary);
    Require(input.good(), "owned world surface diagnostic opens fastfile");
    RetailFastfileCensusJob job;
    RetailCensusLimits limits;
    limits.maxFileBytes = 128u * 1024u * 1024u;
    limits.maxInflatedPrefixBytes = 128u * 1024u * 1024u;
    Require(job.BeginStreaming(RetailCensusMode::PrerequisiteZone, limits) ==
        RetailCensusError::None, "owned prerequisite-zone diagnostic starts");
    std::vector<std::uint8_t> chunk(RETAIL_CENSUS_MAX_STEP_BYTES);
    std::uint32_t steps = 0u;
    while (job.Progress() == RetailCensusProgress::Running && steps++ < 10000u)
    {
        if (job.NeedsSource())
        {
            input.read(reinterpret_cast<char *>(chunk.data()), chunk.size());
            const std::size_t count = static_cast<std::size_t>(input.gcount());
            Require(count != 0u, "owned world surface diagnostic receives source");
            const bool final = input.peek() == std::ifstream::traits_type::eof();
            const RetailCensusError feedError = job.FeedSource(
                std::span<const std::uint8_t>(chunk.data(), count), final);
            if (feedError != RetailCensusError::None)
                std::cerr << "owned world surface feed failed: "
                          << RetailCensusErrorString(feedError) << '\n';
            Require(feedError == RetailCensusError::None,
                "owned world surface source accepted");
        }
        (void)job.Step();
    }
    if (job.Progress() != RetailCensusProgress::Succeeded)
    {
        std::cerr << "owned world surface stopped at "
                  << RetailCensusStageString(job.Stage()) << ": "
                  << RetailCensusErrorString(job.Failure()) << " (asset "
                  << job.CurrentAssetIndex() << ", type "
                  << job.CurrentAssetType() << ")\n";
    }
    Require(job.Progress() == RetailCensusProgress::Succeeded,
        "owned world surface reaches dependency boundary");
    RetailFastfileCensus result;
    Require(job.TakeResult(result), "owned world surface result is available");
    if (result.firstGfxWorldAssetIndex == UINT32_MAX)
    {
        std::cerr << "owned non-world zone reached asset "
                  << result.nextBodyIndex << " type " << result.nextBodyType
                  << " (" << RetailAssetTypeName(result.nextBodyType) << ") after "
                  << result.completedAssetCount << " completed assets, reference 0x"
                  << std::hex << result.nextBodyReference << std::dec
                  << ", operation "
                  << (result.unsupportedOperation
                        ? result.unsupportedOperation : "complete") << '\n';
        if (!result.worldXModels.empty())
            std::cerr << "  final retained xmodel: "
                      << result.worldXModels.back().assetIndex << ' '
                      << result.worldXModels.back().name << '\n';
        std::cerr << "  retained top-level materials: "
                  << result.worldMaterials.size() << '\n';
        for (std::uint32_t type = 0u; type < result.typeCounts.size(); ++type)
        {
            if (result.typeCounts[type] == 0u) continue;
            std::cerr << "  type " << type << " (" << RetailAssetTypeName(type)
                      << "): " << result.typeCounts[type] << ", first asset "
                      << result.firstTypeIndices[type] << '\n';
        }
        if (!worldPath || *worldPath == '\0') return;

        Require(result.completedAssetCount == result.assetCount &&
                result.worldSoundAliasLists.size() == 1723u &&
                result.worldSoundAliasLists.front().assetIndex == 4778u,
            "common prerequisite traversal publishes the canonical sound run beginning at asset 4778");
        auto commonOwner = std::make_shared<RetailFastfileCensus>(
            std::move(result));
        RetailSoundAliasCatalog catalog;
        for (const RetailPublishedSoundAliasList &entry :
             commonOwner->worldSoundAliasLists)
        {
            Require(entry.published && entry.asset && entry.storage &&
                    entry.storage->aliasName,
                "common sound publication retains its canonical zone-owned graph");
            if (entry.pointerAlias || entry.databaseAlias) continue;
            const RetailSoundAliasCatalogError catalogError = catalog.Publish(
                *entry.storage->aliasName,
                entry.asset.get(),
                commonOwner);
            if (catalogError != RetailSoundAliasCatalogError::None)
            {
                snd_alias_list_t *prior = catalog.Find(
                    *entry.storage->aliasName);
                const auto priorEntry = std::find_if(
                    commonOwner->worldSoundAliasLists.begin(),
                    commonOwner->worldSoundAliasLists.end(),
                    [prior](const RetailPublishedSoundAliasList &candidate) {
                        return candidate.asset.get() == prior;
                    });
                std::cerr << "sound catalog publication failed at asset "
                          << entry.assetIndex << " name "
                          << *entry.storage->aliasName << ": "
                          << RetailSoundAliasCatalogErrorString(catalogError)
                          << " (first asset "
                          << (priorEntry ==
                                  commonOwner->worldSoundAliasLists.end()
                              ? UINT32_MAX : priorEntry->assetIndex)
                          << ", offsets "
                          << (priorEntry ==
                                  commonOwner->worldSoundAliasLists.end()
                              ? UINT32_MAX : priorEntry->nameBlock4Offset)
                          << '/' << entry.nameBlock4Offset
                          << ", refs 0x" << std::hex
                          << (priorEntry ==
                                  commonOwner->worldSoundAliasLists.end()
                              ? 0u : priorEntry->serializedReference)
                          << "/0x" << entry.serializedReference << std::dec
                          << ")"
                          << '\n';
            }
            Require(catalogError == RetailSoundAliasCatalogError::None,
                "common sound assets publish into the cross-zone lookup index");
        }
        Require(catalog.EntryCount() == 1716u,
            "cross-zone sound catalog indexes canonical common sound publications");

        std::ifstream worldInput(worldPath, std::ios::binary);
        Require(worldInput.good(),
            "Killhouse WeaponDef retry opens the world fastfile");
        RetailFastfileCensusJob worldJob;
        limits.loadGfxWorld = true;
        limits.maxImageResourceBytes = 64u * 1024u * 1024u;
        Require(worldJob.BeginStreaming(
                    RetailCensusMode::WorldAssetLoader,
                    limits,
                    catalog.Lookup()) ==
                    RetailCensusError::None,
            "Killhouse traversal starts with the common-zone lookup seam");
        steps = 0u;
        while (worldJob.Progress() == RetailCensusProgress::Running &&
               steps++ < 100000u)
        {
            if (worldJob.NeedsSource())
            {
                worldInput.read(
                    reinterpret_cast<char *>(chunk.data()), chunk.size());
                const std::size_t count =
                    static_cast<std::size_t>(worldInput.gcount());
                Require(count != 0u,
                    "Killhouse WeaponDef retry receives source bytes");
                const bool final =
                    worldInput.peek() == std::ifstream::traits_type::eof();
                Require(worldJob.FeedSource(
                            std::span<const std::uint8_t>(
                                chunk.data(), count),
                            final) == RetailCensusError::None,
                    "Killhouse WeaponDef retry accepts source bytes");
            }
            (void)worldJob.Step();
        }
        if (worldJob.Progress() != RetailCensusProgress::Succeeded)
        {
            std::cerr << "Killhouse retry stopped at "
                      << RetailCensusStageString(worldJob.Stage()) << ": "
                      << RetailCensusErrorString(worldJob.Failure())
                      << " (asset " << worldJob.CurrentAssetIndex()
                      << ", type " << worldJob.CurrentAssetType() << ")\n";
        }
        Require(worldJob.Progress() == RetailCensusProgress::Succeeded,
            "Killhouse traversal resolves WeaponDef 458 through common assets");
        RetailFastfileCensus worldResult;
        Require(worldJob.TakeResult(worldResult),
            "Killhouse WeaponDef retry publishes a result");
        const auto weaponIt = std::find_if(
            worldResult.worldWeapons.begin(),
            worldResult.worldWeapons.end(),
            [](const RetailPublishedWeaponDef &entry) {
                return entry.assetIndex == 458u;
            });
        Require(weaponIt != worldResult.worldWeapons.end() &&
                weaponIt->published && weaponIt->asset &&
                weaponIt->storage && weaponIt->storage->soundNames[0u] &&
                weaponIt->storage->soundNames[2u] &&
                weaponIt->storage->soundNames[7u] &&
                weaponIt->asset->pickupSound == catalog.Find(
                    *weaponIt->storage->soundNames[0u]) &&
                weaponIt->asset->ammoPickupSound == catalog.Find(
                    *weaponIt->storage->soundNames[2u]) &&
                *weaponIt->storage->soundNames[7u] ==
                    "weap_winch1200_fire_npc" &&
                weaponIt->asset->fireSound == catalog.Find("null"),
            "Killhouse WeaponDef 458 uses exact common sound pointers and the zone-owned native default");
        std::cerr << "common -> Killhouse publication: sounds="
                  << catalog.EntryCount() << " weapon="
                  << weaponIt->assetIndex << ':'
                  << weaponIt->asset->szInternalName << " pickup="
                  << *weaponIt->storage->soundNames[0u] << '\n';
        std::cerr << "Killhouse ordered boundary: completed="
                  << worldResult.completedAssetCount << " next="
                  << worldResult.nextBodyIndex << ':'
                  << worldResult.nextBodyType << " ("
                  << RetailAssetTypeName(worldResult.nextBodyType) << ")"
                  << " operation="
                  << (worldResult.unsupportedOperation
                          ? worldResult.unsupportedOperation : "complete")
                  << " canonical={techsets="
                  << worldResult.worldTechniqueSets.size()
                  << ",xmodels=" << worldResult.worldXModels.size()
                  << ",fx=" << worldResult.worldFxEffects.size()
                  << ",xanim=" << worldResult.worldXAnimParts.size()
                  << ",weapons=" << worldResult.worldWeapons.size()
                  << ",rawfiles=" << worldResult.worldRawFiles.size()
                  << ",comworlds=" << worldResult.worldComWorlds.size()
                  << ",images=" << worldResult.worldImages.size()
                  << ",lightdefs=" << worldResult.worldLightDefs.size()
                  << "}\n";
        Require(worldResult.worldComWorlds.size() == 1u &&
                worldResult.worldComWorlds.front().asset,
            "Killhouse traversal retains one canonical ComWorld");
        const RetailPublishedComWorld &comWorld =
            worldResult.worldComWorlds.front();
        std::cerr << "Killhouse ComWorld: name=" << comWorld.asset->name
                  << " lights=" << comWorld.asset->primaryLightCount
                  << " block0=" << comWorld.headerBlock0Offset
                  << " block4-name=" << comWorld.nameBlock4Offset
                  << " block4-lights=" << comWorld.primaryLightsBlock4Offset
                  << " boundary=" << comWorld.boundaryInflatedOffset
                  << " highwater0=" << worldResult.block0HighWaterAtBoundary
                  << " cursor4=" << worldResult.block4CursorAtBoundary
                  << " registry=" << worldResult.registryAssetCount << '/'
                  << worldResult.registryDefinedAliasCount << '\n';
        Require(!worldResult.worldLightDefs.empty(),
            "Killhouse traversal retains its pre-world LightDef run");
        const RetailPublishedLightDef &firstLightDef =
            worldResult.worldLightDefs.front();
        const RetailPublishedLightDef &lastLightDef =
            worldResult.worldLightDefs.back();
        std::cerr << "Killhouse LightDefs: first="
                  << firstLightDef.assetIndex << ':' << firstLightDef.asset->name
                  << " image=" << firstLightDef.attenuationImageIdentity
                  << " block0=" << firstLightDef.headerBlock0Offset
                  << " block4-name=" << firstLightDef.nameBlock4Offset
                  << " boundary=" << firstLightDef.boundaryInflatedOffset
                  << " last=" << lastLightDef.assetIndex << ':'
                  << lastLightDef.asset->name
                  << " image=" << lastLightDef.attenuationImageIdentity
                  << " block0=" << lastLightDef.headerBlock0Offset
                  << " block4-name=" << lastLightDef.nameBlock4Offset
                  << " boundary=" << lastLightDef.boundaryInflatedOffset
                  << " preworld-highwater0="
                  << worldResult.block0HighWaterAtBoundary
                  << " preworld-cursor4="
                  << worldResult.block4CursorAtBoundary
                  << " preworld-registry="
                  << worldResult.registryAssetCount << '/'
                  << worldResult.registryDefinedAliasCount
                  << " boundary-inflated="
                  << (worldResult.semanticTrace.empty()
                          ? UINT32_MAX
                          : worldResult.semanticTrace.back().inflatedOffset)
                  << " trace=" << worldResult.semanticTrace.size() << '\n';
        Require(worldResult.worldGfxWorlds.size() == 1u &&
                worldResult.worldGfxWorlds.front().published &&
                worldResult.worldGfxWorlds.front().asset,
            "Killhouse Gate 2 publishes one canonical GfxWorld");
        const RetailPublishedGfxWorld &gfxEntry =
            worldResult.worldGfxWorlds.front();
        const GfxWorld &gfx = *gfxEntry.asset;
        WebEngineGfxWorldSurfacePublication gfxSurface;
        Require(WebEngine_BuildGfxWorldSurface(gfx, gfxSurface) ==
                WebEngineGfxWorldSurfaceResult::Success,
            "Killhouse canonical GfxWorld exposes one bounded real renderer surface");
        std::cerr << "Killhouse GfxWorld: name=" << gfx.name
                  << " base=" << (gfx.baseName ? gfx.baseName : "")
                  << " planes=" << gfx.planeCount
                  << " nodes=" << gfx.nodeCount
                  << " cells=" << gfx.dpvsPlanes.cellCount
                  << " vertices=" << gfx.vertexCount
                  << " indices=" << gfx.indexCount
                  << " surfaces=" << gfx.surfaceCount
                  << " static-models=" << gfx.dpvs.smodelCount
                  << " lightmaps=" << gfx.lightmapCount
                  << " materials=" << gfx.materialMemoryCount
                  << " payload=" << gfxEntry.payloadBytes
                  << " boundary=" << gfxEntry.boundaryInflatedOffset
                  << " highwater0=" << gfxEntry.block0HighWaterAtPublication
                  << " highwater1=" << gfxEntry.block1HighWaterAtPublication
                  << " cursor4=" << gfxEntry.block4CursorAtPublication
                  << " registry=" << gfxEntry.registryAssetCountAtPublication
                  << '/' << gfxEntry.registryAliasCountAtPublication
                  << '/' << gfxEntry.registryDefinedAliasCountAtPublication
                  << " renderer-surface=" << gfxSurface.surfaceIndex
                  << ':' << gfxSurface.vertexCount
                  << '/' << gfxSurface.triangleCount
                  << " material="
                  << (gfxSurface.materialName ? gfxSurface.materialName : "")
                  << " material-identity="
                  << (gfxSurface.surfaceIndex <
                          gfxEntry.surfaceMaterialIdentities.size()
                      ? gfxEntry.surfaceMaterialIdentities[gfxSurface.surfaceIndex]
                      : 0u)
                  << '\n';
        const auto worldPublish = std::find_if(
            worldResult.semanticTrace.rbegin(),
            worldResult.semanticTrace.rend(),
            [](const kisak::database::SemanticTraceEntry &entry) {
                return entry.kind ==
                        kisak::database::SemanticTraceEventKind::AssetPublish &&
                    entry.assetType == ASSET_TYPE_GFXWORLD &&
                    entry.assetIndex == 772u;
            });
        Require(worldResult.completedAssetCount == 773u &&
                worldResult.nextBodyIndex == 773u &&
                worldResult.nextBodyType == 13u &&
                worldResult.worldTechniqueSets.size() == 218u &&
                worldResult.worldXModels.size() == 325u &&
                worldResult.worldFxEffects.size() == 60u &&
                worldResult.worldXAnimParts.size() == 146u &&
                worldResult.worldWeapons.size() == 10u &&
                worldResult.worldRawFiles.size() == 21u &&
                worldResult.worldComWorlds.size() == 1u &&
                firstLightDef.assetIndex == 705u &&
                lastLightDef.assetIndex == 705u &&
                std::string_view(firstLightDef.asset->name) ==
                    "light_point_linear" &&
                firstLightDef.asset->attenuation.image != nullptr &&
                !worldResult.semanticTrace.empty() &&
                worldResult.semanticTrace.back().kind ==
                    kisak::database::SemanticTraceEventKind::Boundary &&
                worldPublish != worldResult.semanticTrace.rend() &&
                std::all_of(
                    worldResult.worldLightDefs.begin(),
                    worldResult.worldLightDefs.end(),
                    [](const RetailPublishedLightDef &entry) {
                        return entry.published && entry.asset && entry.storage;
                    }) &&
                comWorld.published && comWorld.asset && comWorld.storage &&
                comWorld.assetIndex == 704u &&
                std::string_view(comWorld.asset->name) == "maps/killhouse.d3dbsp" &&
                comWorld.asset->primaryLightCount == 24u &&
                comWorld.asset->primaryLights != nullptr &&
                gfx.name && std::string_view(gfx.name) ==
                    "maps/killhouse.d3dbsp" &&
                gfx.vd.vertices && gfx.indices && gfx.dpvs.surfaces &&
                gfx.vd.worldVb == nullptr && gfx.vld.layerVb == nullptr &&
                gfxEntry.surfaceMaterialIdentities.size() ==
                    static_cast<std::size_t>(gfx.surfaceCount) &&
                gfxSurface.material ==
                    gfx.dpvs.surfaces[gfxSurface.surfaceIndex].material &&
                (gfx.reflectionProbeTextures == nullptr ||
                    std::all_of(
                        gfx.reflectionProbeTextures,
                        gfx.reflectionProbeTextures + gfx.reflectionProbeCount,
                        [](const GfxTexture &texture) {
                            return texture.basemap == nullptr;
                        })) &&
                (gfx.lightmapPrimaryTextures == nullptr ||
                    std::all_of(
                        gfx.lightmapPrimaryTextures,
                        gfx.lightmapPrimaryTextures + gfx.lightmapCount,
                        [](const GfxTexture &texture) {
                            return texture.basemap == nullptr;
                        })) &&
                (gfx.lightmapSecondaryTextures == nullptr ||
                    std::all_of(
                        gfx.lightmapSecondaryTextures,
                        gfx.lightmapSecondaryTextures + gfx.lightmapCount,
                        [](const GfxTexture &texture) {
                            return texture.basemap == nullptr;
                        })),
            "Killhouse ordered traversal atomically publishes GfxWorld 772");
        return;
    }
    Require(!result.worldXModels.empty(),
        "owned M35 diagnostic publishes a model collection");
    const RetailWorldXModel &model = result.worldXModels.front();
    Require(result.worldXModels.size() >= 2u,
        "owned M35 diagnostic preserves the M34 model prefix");
    const RetailWorldXModel &secondModel = result.worldXModels.at(1u);
    const RetailWorldXModel &finalModel = result.worldXModels.at(2u);
    const RetailWorldXModel &studioLightModel = result.worldXModels.at(3u);
    const RetailWorldXModel &dropRopeModel = result.worldXModels.at(4u);
    const RetailWorldXModel &nextModel = result.worldXModels.back();
    const auto &post = result.worldTechniqueSets.back();
    const auto firstDependencySetIt = std::find_if(
        result.worldTechniqueSets.begin(), result.worldTechniqueSets.end(),
        [](const RetailWorldTechniqueSet &entry) {
            return entry.assetIndex == 23u;
        });
    Require(firstDependencySetIt != result.worldTechniqueSets.end(),
        "owned M37 traversal retains asset 23");
    const RetailWorldTechniqueSet &firstDependencySet = *firstDependencySetIt;
    const auto priorPostIt = std::find_if(
        result.worldTechniqueSets.begin(), result.worldTechniqueSets.end(),
        [](const RetailWorldTechniqueSet &entry) {
            return entry.assetIndex == 20u;
        });
    Require(priorPostIt != result.worldTechniqueSets.end(),
        "owned reusable loader retains the prior technique-set run");
    const RetailWorldTechniqueSet &priorPost = *priorPostIt;
    std::cerr << "owned post-RawFile boundary: completed="
              << result.completedAssetCount
              << " registry=" << result.registryAssetCount
              << " next=" << result.nextBodyIndex << ':'
              << result.nextBodyType << ":0x" << std::hex
              << result.nextBodyReference << std::dec
              << " model=" << nextModel.assetIndex << ':'
              << nextModel.name << ':' << nextModel.identity << '\n';
    std::cout << "owned M39 XModel boundary: count=" << result.worldXModels.size()
              << " next-index=" << nextModel.assetIndex
              << " name=" << nextModel.name
              << " bones=" << static_cast<unsigned>(nextModel.numBones)
              << '/' << static_cast<unsigned>(nextModel.numRootBones)
              << " surfaces=" << static_cast<unsigned>(nextModel.surfaceCount)
              << '/' << nextModel.surfaces.size()
              << " vertices=" << nextModel.totalVertices
              << " triangles=" << nextModel.totalTriangles
              << " rigid-lists=" << nextModel.totalRigidVertLists
              << " payload=" << nextModel.surfacePayloadBytes
              << " material-handles=" << nextModel.materialReferences.size()
              << " surface-block4=" << nextModel.surfacesBlock4Offset
              << " handles-block4=" << nextModel.materialHandlesBlock4Offset
              << " lods=" << nextModel.lodCount
              << " collision-surfaces=" << nextModel.collisionSurfaceCount
              << " radius=" << nextModel.radius
              << " memory=" << nextModel.memoryUsage
              << " boundary=" << nextModel.boundaryInflatedOffset
              << " name-block4=" << nextModel.nameBlock4Offset
              << " skeleton=" << nextModel.skeletonPrefixTraversed
              << " stop=" << (result.unsupportedOperation
                    ? result.unsupportedOperation : "complete") << '\n';
    for (const RetailXModelMaterial &material : nextModel.materials)
    {
        std::cout << "  M38 material[" << material.handleIndex << "] name="
                  << material.name << " identity=" << material.identity
                  << " textures=" << material.textures.size()
                  << " texture-table=" << material.textureTableBlock4Offset
                  << " published=" << material.published << '\n';
        for (std::size_t textureIndex = 0u;
             textureIndex < material.textures.size(); ++textureIndex)
        {
            const RetailXModelMaterialTexture &texture =
                material.textures[textureIndex];
            ZoneSpan target;
            const bool decoded = DecodeZoneAliasToken(
                texture.imageReference, target);
            std::cout << "    texture[" << textureIndex << "] ref=0x"
                      << std::hex << texture.imageReference << std::dec
                      << " target=" << (decoded ? target.block : UINT32_MAX)
                      << ':' << (decoded ? target.offset : UINT32_MAX)
                      << " resolved=" << texture.resolved
                      << " identity=" << texture.imageIdentity << '\n';
        }
        for (const RetailXModelImage &image : material.images)
        {
            std::cout << "    image name=" << image.name
                      << " identity=" << image.identity
                      << " texture-ref=0x" << std::hex
                      << image.textureReference << std::dec
                      << " header0=" << image.headerBlock0Offset
                      << " name4=" << image.nameBlock4Offset
                      << " loaddef0=" << image.loadDefBlock0Offset
                      << " published=" << image.published << '\n';
        }
    }
    std::cout << "owned reusable-loader technique boundary: index=" << post.assetIndex
              << " name=" << post.name
              << " identity=" << post.identity
              << " published=" << post.published
              << " boundary=" << post.boundaryInflatedOffset
              << " refs=" << post.nullTechniqueReferences << ','
              << post.inlineTechniqueReferences << ','
              << post.sharedTechniqueReferences << ','
              << post.aliasTechniqueReferences
              << " registry=" << result.worldRegistryAliasCount << '/'
              << result.worldRegistryDefinedAliasCount
              << " next=" << result.nextBodyIndex << ':'
              << result.nextBodyType << ":0x" << std::hex
              << result.nextBodyReference << std::dec
              << " block0=" << result.block0HighWaterAtBoundary
              << " block4=" << result.block4CursorAtBoundary << '\n';
    std::cout << "owned M26 registry: model-identity=" << model.identity
              << " block0=" << result.block0HighWaterAtBoundary
              << " assets=" << result.registryAssetCount
              << " aliases=" << result.worldRegistryAliasCount
              << '/' << result.worldRegistryDefinedAliasCount << '\n';
    std::cout << "owned M34 publication: model-identity="
              << secondModel.identity
              << " published=" << secondModel.published
              << " materials=" << secondModel.materials.size()
              << " material-identities=";
    for (const std::uint32_t identity : secondModel.materialIdentities)
        std::cout << identity << ',';
    std::cout << " collision-triangles="
              << secondModel.collisionTriangleCount
              << " collision-payload=" << secondModel.collisionPayloadBytes
              << " bone-hash=0x" << std::hex << secondModel.boneInfoHash
              << std::dec
              << " completed=" << result.completedAssetCount << '\n';
    for (const RetailXModelMaterial &material : secondModel.materials)
    {
        std::cout << "  M34 material[" << material.handleIndex << "] name="
                  << material.name << " identity=" << material.identity
                  << " technique=" << material.techniqueSetIdentity
                  << " textures=" << static_cast<unsigned>(material.textureCount)
                  << " constants=" << static_cast<unsigned>(material.constantCount)
                  << " state-bits=" << static_cast<unsigned>(material.stateBitsCount)
                  << " images=" << material.images.size() << '\n';
        for (const RetailXModelImage &image : material.images)
            std::cout << "    image=" << image.name
                      << " identity=" << image.identity
                      << " published=" << image.published << '\n';
    }
    for (const RetailWorldXModel &entry : result.worldXModels)
    {
        std::cout << "  M38 model asset=" << entry.assetIndex
                  << " name=" << entry.name
                  << " identity=" << entry.identity
                  << " published=" << entry.published
                  << " materials=" << entry.materials.size()
                  << '/' << entry.resolvedMaterials.size()
                  << " images=" << entry.resolvedImages.size()
                  << " boundary=" << entry.boundaryInflatedOffset
                  << " name4=" << entry.nameBlock4Offset
                  << " surfaces4=" << entry.surfacesBlock4Offset
                  << " handles4=" << entry.materialHandlesBlock4Offset
                  << " collision=" << entry.collisionTriangleCount
                  << '/' << entry.collisionPayloadBytes
                  << " bone=0x" << std::hex << entry.boneInfoHash << std::dec
                  << " phys=" << entry.physPresetTraversed
                  << '/' << entry.physGeomsTraversed
                  << " geoms=" << entry.physGeomCount
                  << " brushes=" << entry.physGeomBrushCount
                  << " sides=" << entry.physGeomBrushSideCount
                  << " planes=" << entry.physGeomPlaneCount
                  << " edges=" << entry.physGeomEdgeCount
                  << " phys-bytes=" << entry.physGeomPayloadBytes << '\n';
        if (entry.physPreset.published)
        {
            std::cout << "    phys-preset=" << entry.physPreset.name
                      << " identity=" << entry.physPreset.identity
                      << " sound=" << entry.physPreset.soundAliasPrefix
                      << " type=" << entry.physPreset.type
                      << " mass=" << entry.physPreset.mass
                      << " bounce=" << entry.physPreset.bounce
                      << " friction=" << entry.physPreset.friction
                      << " bullet=" << entry.physPreset.bulletForceScale
                      << " explosive=" << entry.physPreset.explosiveForceScale
                      << " spread=" << entry.physPreset.piecesSpreadFraction
                      << " upward=" << entry.physPreset.piecesUpwardVelocity
                      << " cylinder="
                      << entry.physPreset.tempDefaultToCylinder
                      << " header0=" << entry.physPreset.headerBlock0Offset
                      << " name4=" << entry.physPreset.nameBlock4Offset
                      << " sound4="
                      << entry.physPreset.soundAliasPrefixBlock4Offset << '\n';
        }
        for (const RetailXModelMaterial &material : entry.materials)
        {
            std::cout << "    material=" << material.name
                      << " identity=" << material.identity
                      << " published=" << material.published
                      << " textures4=" << material.textureTableBlock4Offset
                      << " textures=" << material.textures.size() << '\n';
            for (const RetailXModelMaterialTexture &texture : material.textures)
                std::cout << "      texture-ref=0x" << std::hex
                          << texture.imageReference << std::dec
                          << " identity=" << texture.imageIdentity
                          << " resolved=" << texture.resolved << '\n';
            for (const RetailXModelImage &image : material.images)
            {
                std::cout << "      image=" << image.name
                          << " identity=" << image.identity
                          << " insert4="
                          << image.textureInsertPointerBlock4Offset
                          << " format=0x" << std::hex << image.format
                          << std::dec << " bytes=" << image.resourceBytes
                          << '\n';
            }
        }
    }
    for (const RetailWorldFxEffectDef &effect : result.worldFxEffects)
    {
        std::cout << "  FX asset=" << effect.assetIndex
                  << " name=" << effect.name
                  << " identity=" << effect.identity
                  << " elems=" << effect.elemDefs.size()
                  << " materials=" << effect.materials.size()
                  << " boundary=" << effect.boundaryInflatedOffset << '\n';
        for (std::size_t index = 0u; index < effect.elemDefs.size(); ++index)
        {
            const RetailWorldFxElemDef &elem = effect.elemDefs[index];
            std::cout << "    elem[" << index << "] type="
                      << static_cast<unsigned>(elem.elemType)
                      << " visuals=" << elem.visualReferences.size()
                      << " velocity=0x" << std::hex
                      << elem.velocitySamplesHash
                      << " visual=0x" << elem.visualSamplesHash
                      << std::dec << " trail=" << elem.trailVertexCount
                      << '/' << elem.trailIndexCount << '\n';
        }
    }
    const auto sandbagIt = std::find_if(
        result.worldXModels.begin(), result.worldXModels.end(),
        [](const RetailWorldXModel &entry) { return entry.assetIndex == 35u; });
    Require(result.worldFxEffects.size() == 11u,
        "owned traversal publishes every FX effect before asset 437");
    const RetailWorldFxEffectDef &splatFx = result.worldFxEffects[0u];
    const RetailWorldFxEffectDef &watermelonFx = result.worldFxEffects[1u];
    const RetailWorldFxEffectDef &winchesterMuzzleFx =
        result.worldFxEffects[2u];
    const std::size_t nestedBuiltinModels = static_cast<std::size_t>(
        std::count_if(
            result.worldXModels.begin(), result.worldXModels.end(),
            [](const RetailWorldXModel &entry) {
                return !entry.topLevelAsset && entry.published &&
                    entry.name.starts_with(',') && entry.lodCount == 0;
             }));
    Require(result.worldRawFiles.size() == 6u,
        "owned traversal retains all canonical RawFiles before asset 437");
    const RetailWorldRawFile &firstRawFile = result.worldRawFiles.front();
    const RetailWorldRawFile &finalRawFile = result.worldRawFiles.back();
    for (const RetailWorldRawFile &rawFile : result.worldRawFiles)
    {
        std::cout << "  RawFile asset=" << rawFile.assetIndex
                  << " name=" << rawFile.name
                  << " identity=" << rawFile.identity
                  << " length=" << rawFile.length
                  << " boundary=" << rawFile.boundaryInflatedOffset << '\n';
    }
    for (const RetailPublishedXAnimParts &entry : result.worldXAnimParts)
    {
        const XAnimParts *parts = entry.asset.get();
        std::cout << "  XAnimParts asset=" << entry.assetIndex
                  << " name=" << (parts && parts->name ? parts->name : "")
                  << " identity=" << entry.identity
                  << " frames=" << (parts ? parts->numframes : 0u)
                  << " bones=" << (parts ? parts->boneCount[9] : 0u)
                  << " notify=" << (parts ? parts->notifyCount : 0u)
                  << " indices=" << (parts ? parts->indexCount : 0u)
                  << " data=" << (parts ? parts->dataByteCount : 0u)
                  << '/' << (parts ? parts->dataShortCount : 0u)
                  << '/' << (parts ? parts->dataIntCount : 0u)
                  << " random=" << (parts ? parts->randomDataByteCount : 0u)
                  << '/' << (parts ? parts->randomDataShortCount : 0u)
                  << '/' << (parts ? parts->randomDataIntCount : 0u)
                  << " delta=" << (parts && parts->deltaPart)
                  << " payload=" << entry.payloadBytes
                  << " boundary=" << entry.boundaryInflatedOffset << '\n';
    }
    Require(sandbagIt != result.worldXModels.end() &&
        sandbagIt->name == "mil_sandbag_desert_single_flat" &&
        sandbagIt->published && sandbagIt->identity == 64u &&
        sandbagIt->physPresetTraversed && sandbagIt->physGeomsTraversed &&
        sandbagIt->physGeomCount != 0u &&
        sandbagIt->physGeomPayloadBytes != 0u &&
        result.completedAssetCount == 458u &&
        result.worldXModels.size() == 278u && nestedBuiltinModels == 4u &&
        result.worldXAnimParts.size() == 21u &&
        result.registryAssetCount == 1388u &&
        result.registryAliasCount == 1388u &&
        result.registryDefinedAliasCount == 1388u &&
        splatFx.assetIndex == 381u &&
        splatFx.name == "props/watermelon_splat" &&
        splatFx.identity == 1242u && splatFx.published &&
        splatFx.elemDefs.size() == 1u && splatFx.materials.size() == 4u &&
        splatFx.elemDefs[0u].elemType == 9u &&
        splatFx.elemDefs[0u].visualIdentities.size() == 4u &&
        watermelonFx.assetIndex == 382u &&
        watermelonFx.name == "props/watermelon" &&
        watermelonFx.identity == 1250u && watermelonFx.published &&
        watermelonFx.elemDefs.size() == 6u &&
        watermelonFx.materials.size() == 3u &&
        watermelonFx.elemDefs[5u].elemType == 5u &&
        watermelonFx.elemDefs[5u].visualIdentities.size() == 4u &&
        std::all_of(
            watermelonFx.elemDefs[5u].visualIdentities.begin(),
            watermelonFx.elemDefs[5u].visualIdentities.end(),
            [](std::uint32_t identity) { return identity != 0u; }) &&
        winchesterMuzzleFx.assetIndex == 423u &&
        winchesterMuzzleFx.name == "muzzleflashes/winch_flshview" &&
        winchesterMuzzleFx.identity == 1346u &&
        winchesterMuzzleFx.published &&
        winchesterMuzzleFx.elemDefs.size() == 11u &&
        winchesterMuzzleFx.materials.size() == 7u &&
        std::any_of(
            winchesterMuzzleFx.materials.begin(),
            winchesterMuzzleFx.materials.end(),
            [](const RetailWorldFxMaterial &material) {
                return material.published && !material.textures.empty();
            }) &&
        nextModel.assetIndex == 436u &&
        nextModel.name == "viewmodel_knife" &&
        nextModel.identity == 1367u && nextModel.published &&
        firstRawFile.assetIndex == 395u && firstRawFile.published &&
        firstRawFile.identity == 1290u && firstRawFile.asset &&
        firstRawFile.asset->name == firstRawFile.nameStorage->c_str() &&
        firstRawFile.asset->len == firstRawFile.length &&
        finalRawFile.assetIndex == 404u && finalRawFile.published &&
        finalRawFile.identity == 1318u && finalRawFile.asset &&
        finalRawFile.asset->name == finalRawFile.nameStorage->c_str() &&
        finalRawFile.asset->len == finalRawFile.length &&
        result.nextBodyIndex == 458u && result.nextBodyType == 23u &&
        result.nextBodyReference == 0xffffffffu &&
        result.stoppedBeforeDifferentWorldAssetType &&
        result.unsupportedOperation == nullptr,
        "owned dispatcher publishes XAnimParts assets 437-457 before WeaponDef 458");
    return;
    Require(model.published && model.identity == 19u &&
        model.rendererPayloadSelected && model.rendererPayloadAvailable &&
        model.materials.size() == 2u &&
        model.resolvedMaterials.size() == 2u &&
        model.resolvedImages.size() == 4u &&
        model.materials[0].name == "mc/mtl_street_light_02" &&
        model.materials[0].identity == 16u &&
        model.materials[1].name == "mc/mtl_street_light_bulb_02_off" &&
        model.materials[1].identity == 18u &&
        model.materialIdentities ==
            std::vector<std::uint32_t>{16u, 18u, 16u, 18u, 16u, 18u} &&
        model.collisionTriangleCount == 96u &&
        model.collisionPayloadBytes == 4696u &&
        model.boundaryInflatedOffset == 67723u,
        "owned M34 profile preserves the first XModel and technique-set run");
    Require(priorPost.assetIndex == 20u &&
        priorPost.name == ",mc_l_hsm_r0c0n0s0" &&
        priorPost.nullTechniqueReferences == 34u &&
        priorPost.identity == 27u && priorPost.published &&
        priorPost.boundaryInflatedOffset == 69063u &&
        result.worldPostXModelTechniqueSetAssetIndex == 13u &&
        result.worldPostXModelTechniqueSetPublished &&
        result.worldPostXModelTechniqueSetBodiesEntered == 18u &&
        result.worldPostXModelTechniqueSetCompletedCount == 18u,
        "owned reusable loader preserves the completed technique-set prefix");
    Require(secondModel.assetIndex == 21u &&
        secondModel.name == "com_steel_ladder" &&
        secondModel.headerTraversed && secondModel.skeletonPrefixTraversed &&
        secondModel.surfaceHeadersTraversed &&
        secondModel.surfaceDependenciesTraversed &&
        secondModel.materialHandlesTraversed &&
        !secondModel.stoppedBeforeMaterialDependency &&
        !secondModel.stoppedBeforeSurfaceArray && secondModel.published &&
        !secondModel.rendererPayloadSelected &&
        secondModel.rendererPayloadAvailable &&
        secondModel.identity == 32u &&
        secondModel.numBones == 1u && secondModel.numRootBones == 1u &&
        secondModel.surfaceCount == 3u && secondModel.lodCount == 3 &&
        secondModel.surfaces.size() == 3u &&
        secondModel.totalVertices == 750u &&
        secondModel.totalTriangles == 488u &&
        secondModel.totalRigidVertLists == 3u &&
        secondModel.surfacePayloadBytes == 28236u &&
        secondModel.materialReferences.size() == 3u &&
        secondModel.materialReferences == std::vector<std::uint32_t>{
            0xffffffffu, 0x40009af1u, 0x40009af1u} &&
        secondModel.materialIdentities ==
            std::vector<std::uint32_t>{31u, 31u, 31u} &&
        secondModel.materialsTraversed && secondModel.materials.size() == 1u &&
        secondModel.resolvedMaterials.size() == 1u &&
        secondModel.resolvedImages.size() == 3u &&
        secondModel.materials[0].name == "mc/mtl_steel_ladder" &&
        secondModel.materials[0].identity == 31u &&
        secondModel.materials[0].techniqueSetIdentity == 24u &&
        secondModel.materials[0].images.size() == 3u &&
        secondModel.collisionSurfacesTraversed &&
        secondModel.collisionSurfaces.size() == 1u &&
        secondModel.collisionTriangleCount == 296u &&
        secondModel.collisionPayloadBytes == 14252u &&
        secondModel.boneInfoTraversed &&
        secondModel.boneInfoHash == 0x604bd5f6u &&
        secondModel.physPresetTraversed && secondModel.physGeomsTraversed &&
        std::any_of(secondModel.surfaces.begin(), secondModel.surfaces.end(),
            [](const RetailXSurface &surface) {
                return surface.renderPayloadRetained;
            }) &&
        secondModel.collisionSurfaceCount == 1u &&
        secondModel.radius > 200.69f && secondModel.radius < 200.70f &&
        secondModel.memoryUsage == 24551u &&
        secondModel.boundaryInflatedOffset == 112348u &&
        secondModel.nameBlock4Offset == 38300u,
        "owned M34 profile publishes the complete second XModel");
    Require(result.worldXModels.size() == 6u &&
        finalModel.assetIndex == 22u &&
        finalModel.name == "com_steel_ladder_top" &&
        finalModel.headerTraversed && finalModel.skeletonPrefixTraversed &&
        finalModel.surfaceHeadersTraversed &&
        finalModel.surfaceDependenciesTraversed &&
        finalModel.materialHandlesTraversed && finalModel.materialsTraversed &&
        finalModel.collisionSurfacesTraversed && finalModel.boneInfoTraversed &&
        finalModel.physPresetTraversed && finalModel.physGeomsTraversed &&
        finalModel.published && finalModel.identity == 33u &&
        !finalModel.rendererPayloadSelected &&
        finalModel.rendererPayloadAvailable &&
        finalModel.numBones == 1u && finalModel.numRootBones == 1u &&
        finalModel.surfaceCount == 4u && finalModel.lodCount == 4 &&
        finalModel.surfaces.size() == 4u &&
        finalModel.totalVertices == 660u && finalModel.totalTriangles == 420u &&
        finalModel.totalRigidVertLists == 4u &&
        finalModel.surfacePayloadBytes == 25008u &&
        finalModel.materialReferences.size() == 4u &&
        finalModel.materialIdentities ==
            std::vector<std::uint32_t>{31u, 31u, 31u, 31u} &&
        finalModel.materials.empty() &&
        finalModel.resolvedMaterials.size() == 1u &&
        finalModel.resolvedImages.size() == 3u &&
        finalModel.collisionSurfaces.size() == 1u &&
        finalModel.collisionTriangleCount == 228u &&
        finalModel.collisionPayloadBytes == 10988u &&
        finalModel.boneInfoHash == 0x499d1eceu &&
        finalModel.memoryUsage == 21747u &&
        finalModel.boundaryInflatedOffset == 148660u &&
        finalModel.nameBlock4Offset == 54236u &&
        std::any_of(finalModel.surfaces.begin(), finalModel.surfaces.end(),
            [](const RetailXSurface &surface) {
                return surface.renderPayloadRetained;
            }) &&
        result.completedAssetCount == 35u &&
        result.registryAssetCount == 63u &&
        result.worldRegistryAliasCount == 65u &&
        result.worldRegistryDefinedAliasCount == 64u &&
        result.block0HighWaterAtBoundary == 352u &&
        result.block4CursorAtBoundary == 183881u &&
        result.nextBodyIndex == 35u && result.nextBodyType == 3u &&
        result.nextBodyReference == 0xffffffffu &&
        !result.stoppedBeforeDifferentWorldAssetType &&
        !result.stoppedBeforeWorldTechniqueDependency &&
        result.stoppedBeforeWorldXModelDependency &&
        std::string(result.unsupportedOperation) == "Load_PhysGeomList" &&
        firstDependencySet.assetIndex == 23u &&
        firstDependencySet.name == "sm2/mc_unlit" &&
        firstDependencySet.nullTechniqueReferences == 16u &&
        firstDependencySet.inlineTechniqueReferences == 2u &&
        firstDependencySet.sharedTechniqueReferences == 0u &&
        firstDependencySet.aliasTechniqueReferences == 16u &&
        firstDependencySet.published && firstDependencySet.identity == 34u &&
        firstDependencySet.boundaryInflatedOffset == 150864u &&
        firstDependencySet.techniques.size() == 2u &&
        firstDependencySet.techniques[0].slot == 4u &&
        firstDependencySet.techniques[0].name ==
            "vertcol_simple_fog_dtex" &&
        firstDependencySet.techniques[0].completed &&
        firstDependencySet.techniques[1].slot == 28u &&
        firstDependencySet.techniques[1].name ==
            "wireframe_solid_dtex" &&
        firstDependencySet.techniques[1].completed &&
        post.assetIndex == 32u &&
        post.name == "mc_effect_falloff_add_nofog" &&
        post.nullTechniqueReferences == 28u &&
        post.inlineTechniqueReferences == 1u &&
        post.sharedTechniqueReferences == 0u &&
        post.aliasTechniqueReferences == 5u &&
        post.published && post.identity == 43u &&
        post.boundaryInflatedOffset == 166717u &&
        nextModel.assetIndex == 35u &&
        nextModel.name == "mil_sandbag_desert_single_flat" &&
        !nextModel.published &&
        nextModel.physPresetTraversed && !nextModel.physGeomsTraversed &&
        nextModel.physPresetIdentity == 63u &&
        nextModel.physPreset.name == "sandbag" &&
        nextModel.physPreset.soundAliasPrefix.empty() &&
        nextModel.physPreset.type == 0 &&
        nextModel.physPreset.mass == 20.0f &&
        nextModel.physPreset.bounce == 0.01f &&
        nextModel.physPreset.friction == 0.3f &&
        nextModel.physPreset.headerBlock0Offset == 220u &&
        nextModel.physPreset.nameBlock4Offset == 183872u &&
        nextModel.physPreset.soundAliasPrefixBlock4Offset == 183880u &&
        nextModel.physPreset.traversed && nextModel.physPreset.published &&
        nextModel.boundaryInflatedOffset == 397206u,
        "owned M39 traversal publishes the PhysPreset and stops before PhysGeomList");
    Require(studioLightModel.assetIndex == 33u &&
        studioLightModel.name == "com_studio_light_on" &&
        studioLightModel.published && studioLightModel.identity == 54u &&
        studioLightModel.materialsTraversed &&
        studioLightModel.materials.size() == 5u &&
        studioLightModel.resolvedMaterials.size() == 5u &&
        studioLightModel.resolvedImages.size() == 5u &&
        studioLightModel.materials[1].name ==
            "mc/mtl_tripodstudiolight_on" &&
        studioLightModel.materials[1].textures.size() == 1u &&
        studioLightModel.materials[1].textures[0].resolved &&
        studioLightModel.materials[1].textures[0].imageIdentity == 46u &&
        studioLightModel.materials[2].images.size() == 1u &&
        studioLightModel.materials[2].images[0].name == "floodlight_beam" &&
        studioLightModel.materials[2].images[0].format == 0x16u &&
        studioLightModel.materials[2].images[0].resourceBytes == 0u &&
        studioLightModel.collisionTriangleCount == 424u &&
        studioLightModel.collisionPayloadBytes == 20572u &&
        studioLightModel.boneInfoHash == 0xd4bafd51u &&
        studioLightModel.boundaryInflatedOffset == 257898u &&
        dropRopeModel.assetIndex == 34u &&
        dropRopeModel.name == "com_drop_rope" &&
        dropRopeModel.published && dropRopeModel.identity == 59u &&
        dropRopeModel.materials.size() == 1u &&
        dropRopeModel.resolvedImages.size() == 3u &&
        dropRopeModel.collisionTriangleCount == 1186u &&
        dropRopeModel.collisionPayloadBytes == 56972u &&
        dropRopeModel.boundaryInflatedOffset == 374026u,
        "owned M38 publishes XModel assets 33 and 34 atomically");
    WebEngineXModelDrawList firstModelDrawList;
    WebEngineXModelDrawList secondModelDrawList;
    WebEngineXModelDrawList finalModelDrawList;
    Require(WebEngine_BuildXModelDrawList(
            model, firstModelDrawList) ==
            WebEngineXModelDrawListResult::Success &&
        !firstModelDrawList.renderer.draws.empty() &&
        WebEngine_BuildXModelDrawList(
            secondModel, secondModelDrawList) ==
            WebEngineXModelDrawListResult::Success &&
        !secondModelDrawList.renderer.draws.empty() &&
        WebEngine_BuildXModelDrawList(
            finalModel, finalModelDrawList) ==
            WebEngineXModelDrawListResult::Success &&
        !finalModelDrawList.renderer.draws.empty(),
        "all three owned retained XModels resolve materials and build selectable draw lists");
    std::cout << "owned selectable draw lists: first="
              << firstModelDrawList.renderer.draws.size()
              << " second=" << secondModelDrawList.renderer.draws.size()
              << " third=" << finalModelDrawList.renderer.draws.size()
              << '\n';
    for (const RetailXSurface &surface : secondModel.surfaces)
    {
        std::cout << "  M34 surface[" << surface.index << "] vertices="
                  << surface.vertCount << " triangles=" << surface.triCount
                  << " lists=" << surface.rigidVertLists.size()
                  << " vertex-hash=0x" << std::hex << surface.verticesHash
                  << " index-hash=0x" << surface.indicesHash << std::dec
                  << '\n';
    }
    std::cout << "  M34 material references:";
    for (const std::uint32_t reference : secondModel.materialReferences)
        std::cout << " 0x" << std::hex << reference << std::dec;
    std::cout << '\n';
    for (const RetailWorldTechniqueSet &entry : result.worldTechniqueSets)
    {
        if (entry.assetIndex <= model.assetIndex) continue;
        std::cout << "  M31 technique set: index=" << entry.assetIndex
                  << " name=" << entry.name
                  << " identity=" << entry.identity
                  << " boundary=" << entry.boundaryInflatedOffset
                  << " name-block4=" << entry.nameBlock4Offset << '\n';
    }
    const RetailXSurface &renderCandidate = model.surfaces.front();
    WebEngineConvertedXModelSurface converted;
    Require(renderCandidate.renderPayloadRetained &&
        renderCandidate.retainedPackedVertices.size() == 11776u &&
        renderCandidate.retainedPackedIndices.size() == 1512u,
        "owned M27 profile retains the exact bounded first XSurface payload");
    const WebEnginePackedXSurfaceView view{
        renderCandidate.retainedPackedVertices.data(),
        renderCandidate.retainedPackedVertices.size(),
        renderCandidate.vertCount,
        renderCandidate.retainedPackedIndices.data(),
        renderCandidate.retainedPackedIndices.size(),
        renderCandidate.triCount,
        model.materialIdentities.front(),
    };
    Require(WebEngine_ConvertPackedXModelSurface(view, converted) ==
        WebEngineXModelSurfaceResult::Success &&
        converted.rendererSurface.vertices.size() == 368u &&
        converted.rendererSurface.indices.size() == 756u &&
        converted.materialIdentity == 16u,
        "owned M27 profile converts the first retail surface through the renderer seam");
    WebEngineXModelMaterialImageBinding colorMap;
    Require(WebEngine_SelectXModelColorMap(
            model, converted.materialIdentity, colorMap) ==
            WebEngineXModelMaterialResult::Success &&
        colorMap.materialIdentity == converted.materialIdentity &&
        colorMap.semantic == WEB_ENGINE_TEXTURE_SEMANTIC_COLOR_MAP,
        "owned M28 profile resolves the rendered material's typed external color map");
    std::cout << "owned M27 render surface: vertices="
              << converted.rendererSurface.vertices.size()
              << " indices=" << converted.rendererSurface.indices.size()
              << " material=" << converted.materialIdentity
              << " axes=" << static_cast<unsigned>(converted.horizontalAxis)
              << ',' << static_cast<unsigned>(converted.verticalAxis) << '\n';
    std::cout << "owned M28 color map: material=" << colorMap.materialName
              << " image=" << colorMap.imageName
              << " path=" << colorMap.imagePath
              << " identities=" << colorMap.materialIdentity
              << ',' << colorMap.imageIdentity
              << " sampler=" << static_cast<unsigned>(colorMap.samplerState)
              << " semantic=" << static_cast<unsigned>(colorMap.semantic) << '\n';
    WebEngineXModelDrawList drawList;
    Require(WebEngine_BuildXModelDrawList(model, drawList) ==
            WebEngineXModelDrawListResult::Success,
        "owned M29 profile builds the bounded first-LOD draw list");
    std::cout << "owned M29 draw list: lod-surfaces="
              << drawList.firstLodSurfaceCount
              << " retained-draws=" << drawList.renderer.draws.size()
              << " textures=" << drawList.textures.size()
              << " vertices=" << drawList.renderer.vertices.size()
              << " indices=" << drawList.renderer.indices.size()
              << " axes=" << static_cast<unsigned>(drawList.horizontalAxis)
               << ',' << static_cast<unsigned>(drawList.verticalAxis) << '\n';
    std::cout << "owned M31 next body: index=" << result.nextBodyIndex
              << " type=" << result.nextBodyType
              << " (" << RetailAssetTypeName(result.nextBodyType) << ')'
              << " reference=0x" << std::hex << result.nextBodyReference
              << std::dec << '\n';
    for (std::size_t textureSlot = 0u;
         textureSlot < drawList.textures.size(); ++textureSlot)
    {
        const auto &binding = drawList.textures[textureSlot];
        std::cout << "  texture[" << textureSlot << "] material="
                  << binding.materialName << " image=" << binding.imageName
                  << " path=" << binding.imagePath
                  << " sampler=" << static_cast<unsigned>(binding.samplerState)
                  << '\n';
    }
    std::cout << "owned xmodel: index=" << model.assetIndex
              << " name=" << model.name
              << " surfaces=" << model.surfaces.size()
              << " vertices=" << model.totalVertices
              << " triangles=" << model.totalTriangles
              << " rigid-lists=" << model.totalRigidVertLists
              << " collision-nodes=" << model.totalCollisionNodes
              << " collision-leaves=" << model.totalCollisionLeaves
              << " payload=" << model.surfacePayloadBytes
              << " materials=" << model.materialReferences.size()
              << " inline-materials=" << model.materials.size()
              << " model-published=" << model.published
              << " collision-triangles=" << model.collisionTriangleCount
              << " collision-payload=" << model.collisionPayloadBytes
              << " boundary=" << model.boundaryInflatedOffset
              << " block4=" << result.block4CursorAtBoundary
              << " stop=" << (result.unsupportedOperation
                    ? result.unsupportedOperation : "complete") << '\n';
    for (const RetailXSurface &surface : model.surfaces)
    {
        std::cout << "  surface[" << surface.index << "] vertices="
                  << surface.vertCount << " triangles=" << surface.triCount
                  << " lists=" << surface.rigidVertLists.size()
                  << " vertex-hash=0x" << std::hex << surface.verticesHash
                  << " index-hash=0x" << surface.indicesHash << std::dec << '\n';
    }
    std::cout << "  material references:";
    for (const std::uint32_t reference : model.materialReferences)
        std::cout << " 0x" << std::hex << reference << std::dec;
    std::cout << '\n';
    for (const RetailXModelMaterial &material : model.materials)
    {
        std::cout << "  material[" << material.handleIndex << "] name="
                  << material.name << " identity=" << material.identity
                  << " textures=" << static_cast<unsigned>(material.textureCount)
                  << " constants=" << static_cast<unsigned>(material.constantCount)
                  << " state-bits=" << static_cast<unsigned>(material.stateBitsCount)
                  << " images=" << material.images.size() << '\n';
    }
}
} // namespace

int main(int argc, char **argv)
{
    TestSoundAliasCatalogLookupContract();
    TestPositiveIncrementalCensus();
    TestWorldAssetInventory();
    TestCanonicalLocalizeZoneLoader();
    TestCanonicalGfxWorldLoader();
    TestCanonicalClipMapLoader();
    TestCanonicalComWorldLoader();
    TestCanonicalLightDefLoader();
    TestWorldTechniqueSetPrefixBoundary();
    TestWorldXModelPrefixBoundary();
    TestWorldXSurfacePrefixBoundary();
    TestWorldXModelDependenciesBoundary();
    TestWorldPostXModelTechniqueSetBoundary();
    TestWorldSecondXModelPrefixBoundary();
    TestWorldSecondXSurfacePrefixBoundary();
    TestWorldSecondXModelDependenciesBoundary();
    TestWorldXModelCollectionBoundary();
    TestReusableWorldXModelLoader();
    TestReusableWorldRawFileLoader();
    TestReusableWorldXAnimPartsLoader();
    TestReusableWorldWeaponDefLoader();
    TestReusableWorldMaterialTechniqueLoader();
    TestReusableWorldFxLoader();
    TestMalformedPrefixRecords();
    TestTechniqueTraversalFailures();
    TestEnvelopeAndAtomicity();
    TestShaderCompatibilityDecoder();
    TestOwnedWorldSurfaceIfRequested(
        argc > 1 ? argv[1] : nullptr,
        argc > 2 ? argv[2] : nullptr);
    std::cout << "web retail fastfile census tests passed\n";
    return 0;
}
