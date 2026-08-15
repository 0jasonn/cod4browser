#include <web/web_retail_fastfile_census.h>
#include <web/web_shader_compatibility.h>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
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
    bool invalidBoneString = false)
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
    model[6u] = 2u;
    SetU32(model, 8u, unsupportedBoneNames ? 0x40000001u : 0xffffffffu);
    SetU32(model, 24u, 0xffffffffu);
    SetU32(model, 28u, 0xffffffffu);
    SetU32(model, 32u, 0xffffffffu);
    SetU32(model, 36u, 0xffffffffu);
    SetF32(model, 40u, 800.0f);
    PutU16At(model, 44u, 2u);
    SetU32(model, 48u, 0x80000000u);
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
    bool invalidCollisionTree = false)
{
    std::vector<std::uint8_t> bytes = BuildWorldXModelPrefixInflated();
    for (std::uint32_t index = 0u; index < 2u; ++index)
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
    for (std::uint32_t index = 0u; index < 2u; ++index)
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
    PutU32(bytes, 0xffffffffu);
    PutU32(bytes, 0x40000001u);
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
    const RetailWorldXModel &model = result.worldFirstXModel;
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

    const auto unsupported = Run(
        BuildFile(BuildWorldXModelPrefixInflated(false, true)), 7u, 2u, 3u,
        RetailCensusMode::WorldXModelPrefix);
    Require(unsupported.worldFirstXModel.headerTraversed &&
        !unsupported.worldFirstXModel.skeletonPrefixTraversed &&
        std::string(unsupported.unsupportedOperation) == "Load_ScriptStringArray",
        "unsupported bone dependency stops after fixed header");

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
    const RetailWorldXModel &model = result.worldFirstXModel;
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
        result.block4CursorAtBoundary == 1684u,
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
    Require(job.BeginStreaming(RetailCensusMode::WorldXSurfacePrefix) ==
        RetailCensusError::None, "owned world surface diagnostic starts");
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
    const RetailWorldXModel &model = result.worldFirstXModel;
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
              << " boundary=" << model.boundaryInflatedOffset
              << " block4=" << result.block4CursorAtBoundary
              << " stop=" << result.unsupportedOperation << '\n';
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
}
} // namespace

int main(int argc, char **argv)
{
    TestPositiveIncrementalCensus();
    TestWorldAssetInventory();
    TestWorldTechniqueSetPrefixBoundary();
    TestWorldXModelPrefixBoundary();
    TestWorldXSurfacePrefixBoundary();
    TestMalformedPrefixRecords();
    TestTechniqueTraversalFailures();
    TestEnvelopeAndAtomicity();
    TestShaderCompatibilityDecoder();
    TestOwnedWorldSurfaceIfRequested(argc > 1 ? argv[1] : nullptr);
    std::cout << "web retail fastfile census tests passed\n";
    return 0;
}
