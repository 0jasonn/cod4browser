#include <web/web_retail_fastfile_census.h>
#include <web/web_fastfile_zone_registry.h>
#include <web/web_engine_xmodel_surface.h>
#include <web/web_engine_xmodel_material.h>
#include <web/web_engine_xmodel_draw_list.h>
#include <web/web_shader_compatibility.h>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
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

void CollectSemanticTrace(
    const kisak::database::SemanticTraceEntry &entry, void *userData)
{
    static_cast<std::vector<kisak::database::SemanticTraceEntry> *>(userData)
        ->push_back(entry);
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
    uLongf compressedSize = compressBound(static_cast<uLong>(inflated.size()));
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
    bool invalidMaterial = false);

std::vector<std::uint8_t> BuildReusableWorldRawFileLoaderInflated()
{
    std::vector<std::uint8_t> bytes =
        BuildReusableWorldFxLoaderInflated(false);
    constexpr std::size_t assetTableOffset = 60u;
    constexpr std::size_t rawFileAssetIndex = 2u;
    SetU32(bytes, 52u, 4u);
    const std::array<std::uint8_t, 8> rawFileEntry = {{
        31u, 0u, 0u, 0u, 0xffu, 0xffu, 0xffu, 0xffu,
    }};
    bytes.insert(
        bytes.begin() + static_cast<std::ptrdiff_t>(
            assetTableOffset + rawFileAssetIndex * 8u),
        rawFileEntry.begin(), rawFileEntry.end());

    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 4u);
    PutU32(bytes, 1u);
    AppendString(bytes, "scripts/web_rawfile.gsc");
    const std::array<std::uint8_t, 5> payload = {{'t', 'e', 's', 't', 0u}};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

std::vector<std::uint8_t> BuildReusableWorldFxLoaderInflated(
    bool invalidMaterial)
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
    PutU32(bytes, 512u);
    PutU32(bytes, 0u);
    PutU32(bytes, 0u);
    PutU32(bytes, 1u);
    PutU32(bytes, 0u);
    PutU32(bytes, 0xffffffffu);
    AppendString(bytes, "web/fx_mark");

    std::vector<std::uint8_t> elem(252u, 0u);
    elem[176u] = 9u;
    elem[177u] = 1u;
    SetU32(elem, 188u, 0xffffffffu);
    bytes.insert(bytes.end(), elem.begin(), elem.end());
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0xffffffffu);
    for (std::uint32_t index = 0u; index < 2u; ++index)
    {
        std::vector<std::uint8_t> material(80u, 0u);
        SetU32(material, 0u, 0xffffffffu);
        if (invalidMaterial && index == 1u) material[58u] = 1u;
        bytes.insert(bytes.end(), material.begin(), material.end());
        AppendString(bytes, index == 0u
            ? ",web/fx_mark_world"
            : ",web/fx_mark_model");
    }
    return bytes;
}

kisak::fastfile::RetailFastfileCensus Run(
    const std::vector<std::uint8_t> &file,
    std::size_t chunkBytes = 7u,
    std::uint32_t stepRecords = 2u,
    std::uint32_t stepBytes = 3u,
    kisak::fastfile::RetailCensusMode mode =
        kisak::fastfile::RetailCensusMode::CodePostGfxMaterial)
{
    using namespace kisak::fastfile;
    RetailFastfileCensusJob job;
    Require(job.BeginStreaming(mode) == RetailCensusError::None, "census starts");
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
        result.worldFxEffects[0].materials[1].published &&
        result.completedAssetCount == 2u &&
        result.nextBodyIndex == 2u && result.nextBodyType == 16u &&
        result.stoppedBeforeDifferentWorldAssetType,
        "the reusable dispatcher publishes an FX mark-visual family atomically");

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

void TestOwnedWorldSurfaceIfRequested(const char *path)
{
    if (!path || *path == '\0') return;
    using namespace kisak::fastfile;
    std::ifstream input(path, std::ios::binary);
    Require(input.good(), "owned world surface diagnostic opens fastfile");
    RetailFastfileCensusJob job;
    Require(job.BeginStreaming(RetailCensusMode::WorldAssetLoader) ==
        RetailCensusError::None, "owned reusable XModel-loader diagnostic starts");
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
            Require(job.FeedSource(
                std::span<const std::uint8_t>(chunk.data(), count), final) ==
                RetailCensusError::None, "owned world surface source accepted");
        }
        (void)job.Step();
    }
    if (job.Progress() != RetailCensusProgress::Succeeded)
    {
        std::cerr << "owned world surface stopped at "
                  << RetailCensusStageString(job.Stage()) << ": "
                  << RetailCensusErrorString(job.Failure()) << '\n';
    }
    Require(job.Progress() == RetailCensusProgress::Succeeded,
        "owned world surface reaches dependency boundary");
    RetailFastfileCensus result;
    Require(job.TakeResult(result), "owned world surface result is available");
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
    Require(result.worldFxEffects.size() == 2u,
        "owned traversal publishes both leading FX effects");
    const RetailWorldFxEffectDef &splatFx = result.worldFxEffects[0u];
    const RetailWorldFxEffectDef &watermelonFx = result.worldFxEffects[1u];
    const std::size_t nestedBuiltinModels = static_cast<std::size_t>(
        std::count_if(
            result.worldXModels.begin(), result.worldXModels.end(),
            [](const RetailWorldXModel &entry) {
                return !entry.topLevelAsset && entry.published &&
                    entry.name.starts_with(',') && entry.lodCount == 0;
             }));
    Require(result.worldRawFiles.size() == 1u,
        "owned traversal retains the first canonical RawFile");
    const RetailWorldRawFile &firstRawFile = result.worldRawFiles.front();
    std::cout << "  RawFile asset=" << firstRawFile.assetIndex
              << " name=" << firstRawFile.name
              << " identity=" << firstRawFile.identity
              << " length=" << firstRawFile.length
              << " boundary=" << firstRawFile.boundaryInflatedOffset << '\n';
    Require(sandbagIt != result.worldXModels.end() &&
        sandbagIt->name == "mil_sandbag_desert_single_flat" &&
        sandbagIt->published && sandbagIt->identity == 64u &&
        sandbagIt->physPresetTraversed && sandbagIt->physGeomsTraversed &&
        sandbagIt->physGeomCount != 0u &&
        sandbagIt->physGeomPayloadBytes != 0u &&
        result.completedAssetCount == 396u &&
        result.worldXModels.size() == 269u && nestedBuiltinModels == 4u &&
        result.registryAssetCount == 1290u &&
        result.registryAliasCount == 1290u &&
        result.registryDefinedAliasCount == 1290u &&
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
        nextModel.assetIndex == 394u &&
        nextModel.name == "com_drop_rope_obj" &&
        nextModel.published &&
        firstRawFile.assetIndex == 395u && firstRawFile.published &&
        firstRawFile.identity == 1290u && firstRawFile.asset &&
        firstRawFile.asset->name == firstRawFile.nameStorage->c_str() &&
        firstRawFile.asset->len == firstRawFile.length &&
        result.nextBodyIndex == 396u && result.nextBodyType == 31u &&
        result.stoppedAfterCanonicalRawFile &&
        result.unsupportedOperation == nullptr,
        "owned FX-family traversal publishes RawFile 395 and stops at 396");
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
    TestPositiveIncrementalCensus();
    TestWorldAssetInventory();
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
    TestReusableWorldMaterialTechniqueLoader();
    TestReusableWorldFxLoader();
    TestMalformedPrefixRecords();
    TestTechniqueTraversalFailures();
    TestEnvelopeAndAtomicity();
    TestShaderCompatibilityDecoder();
    TestOwnedWorldSurfaceIfRequested(argc > 1 ? argv[1] : nullptr);
    std::cout << "web retail fastfile census tests passed\n";
    return 0;
}
