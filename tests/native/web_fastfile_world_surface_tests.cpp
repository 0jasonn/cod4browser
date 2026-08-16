#include <web/web_fastfile_world_surface.h>
#include "zlib_test_support.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using kisak::fastfile::Error;
using kisak::fastfile::ExtractedWorldSurface;
using kisak::fastfile::JobProgress;
using kisak::fastfile::JobStage;
using kisak::fastfile::Limits;
using kisak::fastfile::StepBudget;
using kisak::fastfile::StepReport;
using kisak::fastfile::WorldSurfaceExtractionJob;

constexpr std::size_t PREFIX_SIZE = 12u;
constexpr std::size_t XFILE_SIZE = 44u;
constexpr std::size_t XASSET_LIST_SIZE = 16u;
constexpr std::size_t XASSET_SIZE = 8u;
constexpr std::size_t MATERIAL_MEMORY_SIZE = 8u;
constexpr std::uint32_t ASSET_TYPE_MATERIAL = 0x04u;
constexpr std::uint32_t ASSET_TYPE_GFXWORLD = 0x10u;
constexpr std::uint32_t INLINE_POINTER = 0xffffffffu;
constexpr std::uint32_t INLINE_SHARED_POINTER = 0xfffffffeu;

constexpr std::size_t WORLD_INDEX_COUNT_OFFSET = 0x10u;
constexpr std::size_t WORLD_INDICES_OFFSET = 0x14u;
constexpr std::size_t WORLD_SURFACE_COUNT_OFFSET = 0x18u;
constexpr std::size_t WORLD_VERTEX_COUNT_OFFSET = 0x30u;
constexpr std::size_t WORLD_VERTICES_OFFSET = 0x34u;
constexpr std::size_t WORLD_MATERIAL_MEMORY_COUNT_OFFSET = 0x174u;
constexpr std::size_t WORLD_MATERIAL_MEMORY_OFFSET = 0x178u;
constexpr std::size_t WORLD_STATIC_SURFACE_COUNT_OFFSET = 0x248u;
constexpr std::size_t WORLD_SURFACES_OFFSET = 0x294u;

class TestFailure final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw TestFailure(std::string(message));
    }
}

void RequireResult(Error actual, Error expected, std::string_view context)
{
    if (actual == expected)
    {
        return;
    }
    std::string message(context);
    message += ": expected ";
    message += kisak::fastfile::ErrorString(expected);
    message += ", got ";
    message += kisak::fastfile::ErrorString(actual);
    throw TestFailure(message);
}

void RequireConverterResult(
    WebEngineWorldSurfaceResult actual,
    WebEngineWorldSurfaceResult expected,
    std::string_view context)
{
    if (actual == expected)
    {
        return;
    }
    std::string message(context);
    message += ": expected ";
    message += WebEngine_WorldSurfaceResultString(expected);
    message += ", got ";
    message += WebEngine_WorldSurfaceResultString(actual);
    throw TestFailure(message);
}

void RequireNear(float actual, float expected, std::string_view context)
{
    if (std::fabs(actual - expected) <= 0.000001f)
    {
        return;
    }
    std::string message(context);
    message += ": expected ";
    message += std::to_string(expected);
    message += ", got ";
    message += std::to_string(actual);
    throw TestFailure(message);
}

void PutU16(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint16_t value)
{
    Require(offset <= bytes.size() && bytes.size() - offset >= 2u, "PutU16 range");
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

void PutU32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value)
{
    Require(offset <= bytes.size() && bytes.size() - offset >= 4u, "PutU32 range");
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    bytes[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
    bytes[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
}

void PutS32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::int32_t value)
{
    PutU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void PutF32(std::vector<std::uint8_t> &bytes, std::size_t offset, float value)
{
    PutU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

std::uint32_t ReadU32(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    Require(offset <= bytes.size() && bytes.size() - offset >= 4u, "ReadU32 range");
    return static_cast<std::uint32_t>(bytes[offset]) |
        static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u |
        static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u |
        static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u;
}

std::uint32_t PackColor(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha)
{
    return static_cast<std::uint32_t>(blue) |
        static_cast<std::uint32_t>(green) << 8u |
        static_cast<std::uint32_t>(red) << 16u |
        static_cast<std::uint32_t>(alpha) << 24u;
}

std::uint32_t Align(std::uint32_t cursor, std::uint32_t alignment)
{
    return (cursor + alignment - 1u) & ~(alignment - 1u);
}

std::uint32_t Allocate(
    std::uint32_t &cursor,
    std::uint32_t alignment,
    std::uint32_t size)
{
    cursor = Align(cursor, alignment);
    const std::uint32_t offset = cursor;
    cursor += size;
    return offset;
}

std::size_t Append(std::vector<std::uint8_t> &bytes, std::size_t count)
{
    const std::size_t offset = bytes.size();
    bytes.resize(offset + count, 0u);
    return offset;
}

void WriteVertex(
    std::vector<std::uint8_t> &bytes,
    std::size_t offset,
    float x,
    float y,
    float z,
    std::uint32_t color,
    float textureU,
    float textureV,
    float lightmapU = 0.0f,
    float lightmapV = 0.0f,
    float binormalSign = 1.0f,
    std::uint32_t normal = 0x7f7f7fffu,
    std::uint32_t tangent = 0x7f7f7fffu)
{
    PutF32(bytes, offset + 0u, x);
    PutF32(bytes, offset + 4u, y);
    PutF32(bytes, offset + 8u, z);
    PutF32(bytes, offset + 12u, binormalSign);
    PutU32(bytes, offset + 16u, color);
    PutF32(bytes, offset + 20u, textureU);
    PutF32(bytes, offset + 24u, textureV);
    PutF32(bytes, offset + 28u, lightmapU);
    PutF32(bytes, offset + 32u, lightmapV);
    PutU32(bytes, offset + 36u, normal);
    PutU32(bytes, offset + 40u, tangent);
}

std::vector<std::uint8_t> Compress(std::span<const std::uint8_t> inflated)
{
    uLongf capacity = KisakTestCompressBound(
        static_cast<uLong>(inflated.size()));
    std::vector<std::uint8_t> compressed(capacity);
    const int result = compress2(
        reinterpret_cast<Bytef *>(compressed.data()),
        &capacity,
        reinterpret_cast<const Bytef *>(inflated.data()),
        static_cast<uLong>(inflated.size()),
        Z_BEST_COMPRESSION);
    Require(result == Z_OK, "synthetic fixture compression succeeds");
    compressed.resize(capacity);
    return compressed;
}

void AppendStoredDeflateBlock(
    std::vector<std::uint8_t> &payload,
    bool final,
    std::span<const std::uint8_t> bytes)
{
    Require(bytes.size() <= std::numeric_limits<std::uint16_t>::max(),
        "stored deflate fixture block fits its wire length");
    const std::uint16_t length = static_cast<std::uint16_t>(bytes.size());
    const std::uint16_t complement = static_cast<std::uint16_t>(~length);
    // BTYPE=00 starts a stored block and pads the rest of this header byte.
    payload.push_back(final ? 0x01u : 0x00u);
    payload.push_back(static_cast<std::uint8_t>(length));
    payload.push_back(static_cast<std::uint8_t>(length >> 8u));
    payload.push_back(static_cast<std::uint8_t>(complement));
    payload.push_back(static_cast<std::uint8_t>(complement >> 8u));
    payload.insert(payload.end(), bytes.begin(), bytes.end());
}

std::vector<std::uint8_t> BuildStoredControlTailFastfile(
    std::span<const std::uint8_t> inflated,
    bool appendOutputBeyondCeiling)
{
    std::vector<std::uint8_t> payload = {0x78u, 0x01u};
    AppendStoredDeflateBlock(payload, false, inflated);

    constexpr std::size_t EMPTY_BLOCK_WIRE_BYTES = 5u;
    constexpr std::size_t OUTPUT_FREE_TAIL_BYTES =
        2u * kisak::fastfile::MAX_STEP_BYTES + 257u;
    constexpr std::size_t EMPTY_BLOCK_COUNT =
        (OUTPUT_FREE_TAIL_BYTES + EMPTY_BLOCK_WIRE_BYTES - 1u) /
        EMPTY_BLOCK_WIRE_BYTES;
    const std::span<const std::uint8_t> empty;
    for (std::size_t block = 0u; block < EMPTY_BLOCK_COUNT; ++block)
    {
        AppendStoredDeflateBlock(payload, false, empty);
    }

    const std::array<std::uint8_t, 1> extra = {0x5au};
    AppendStoredDeflateBlock(
        payload,
        true,
        appendOutputBeyondCeiling
            ? std::span<const std::uint8_t>(extra)
            : empty);

    uLong checksum = adler32(0L, Z_NULL, 0);
    checksum = adler32(
        checksum,
        reinterpret_cast<const Bytef *>(inflated.data()),
        static_cast<uInt>(inflated.size()));
    if (appendOutputBeyondCeiling)
    {
        checksum = adler32(checksum, extra.data(), static_cast<uInt>(extra.size()));
    }
    payload.push_back(static_cast<std::uint8_t>(checksum >> 24u));
    payload.push_back(static_cast<std::uint8_t>(checksum >> 16u));
    payload.push_back(static_cast<std::uint8_t>(checksum >> 8u));
    payload.push_back(static_cast<std::uint8_t>(checksum));

    std::vector<std::uint8_t> file = {
        'I', 'W', 'f', 'f', 'u', '1', '0', '0',
        5u, 0u, 0u, 0u,
    };
    file.insert(file.end(), payload.begin(), payload.end());
    return file;
}

struct SyntheticFastfile
{
    std::string materialName;
    std::vector<std::uint8_t> inflated;
    std::vector<std::uint8_t> file;

    std::size_t xfileOffset = 0u;
    std::size_t assetListOffset = 0u;
    std::size_t assetTableOffset = 0u;
    std::size_t materialAssetOffset = 0u;
    std::size_t worldAssetOffset = 0u;
    std::size_t materialOffset = 0u;
    std::size_t materialNameOffset = 0u;
    std::size_t worldOffset = 0u;
    std::size_t indicesOffset = 0u;
    std::size_t materialMemoryOffset = 0u;
    std::size_t verticesOffset = 0u;
    std::size_t surfaceOffset = 0u;

    std::uint32_t block0Size = 0u;
    std::uint32_t block4Size = 0u;
    std::uint32_t aliasSlotOffset = 0u;
    std::uint32_t worldVertexCount = 6u;

    explicit SyntheticFastfile(
        std::string name = "web/synthetic",
        std::uint32_t sourceVertexCount = 6u)
        : materialName(std::move(name)), worldVertexCount(sourceVertexCount)
    {
        Require(worldVertexCount >= 6u, "synthetic fixture retains six guard vertices");
        Build();
    }

    void Build()
    {
        inflated.clear();
        xfileOffset = Append(inflated, XFILE_SIZE);
        assetListOffset = Append(inflated, XASSET_LIST_SIZE);
        assetTableOffset = Append(inflated, 2u * XASSET_SIZE);
        materialAssetOffset = assetTableOffset;
        worldAssetOffset = assetTableOffset + XASSET_SIZE;
        materialOffset = Append(inflated, kisak::fastfile::MATERIAL_WIRE_SIZE);
        materialNameOffset = Append(inflated, materialName.size() + 1u);
        worldOffset = Append(inflated, kisak::fastfile::GFX_WORLD_WIRE_SIZE);
        indicesOffset = Append(inflated, 12u * sizeof(std::uint16_t));
        materialMemoryOffset = Append(inflated, MATERIAL_MEMORY_SIZE);
        verticesOffset = Append(
            inflated,
            static_cast<std::size_t>(worldVertexCount) *
                kisak::fastfile::GFX_WORLD_VERTEX_WIRE_SIZE);
        surfaceOffset = Append(inflated, kisak::fastfile::GFX_SURFACE_WIRE_SIZE);

        // Both top-level asset bodies use rewindable block 0 loader frames.
        // The later world deliberately reuses the material's logical bytes, so
        // the declared extent is the larger high-water mark, not their sum.
        std::uint32_t materialBlock0Cursor = 0u;
        (void)Allocate(materialBlock0Cursor, 4u,
            kisak::fastfile::MATERIAL_WIRE_SIZE);
        std::uint32_t worldBlock0Cursor = 0u;
        (void)Allocate(worldBlock0Cursor, 4u,
            kisak::fastfile::GFX_WORLD_WIRE_SIZE);
        block0Size = std::max(materialBlock0Cursor, worldBlock0Cursor);

        std::uint32_t block4Cursor = 0u;
        (void)Allocate(block4Cursor, 4u, 2u * XASSET_SIZE);
        aliasSlotOffset = Allocate(block4Cursor, 4u, sizeof(std::uint32_t));
        (void)Allocate(
            block4Cursor,
            1u,
            static_cast<std::uint32_t>(materialName.size() + 1u));
        (void)Allocate(block4Cursor, 2u, 12u * sizeof(std::uint16_t));
        (void)Allocate(block4Cursor, 4u, MATERIAL_MEMORY_SIZE);
        (void)Allocate(
            block4Cursor,
            4u,
            worldVertexCount * kisak::fastfile::GFX_WORLD_VERTEX_WIRE_SIZE);
        (void)Allocate(
            block4Cursor, 4u, kisak::fastfile::GFX_SURFACE_WIRE_SIZE);
        block4Size = block4Cursor;

        const std::uint32_t declaredZoneBytes = block0Size + block4Size;
        PutU32(inflated, xfileOffset + 0u, declaredZoneBytes);
        PutU32(inflated, xfileOffset + 4u, 0u);
        PutU32(inflated, xfileOffset + 8u + 0u * 4u, block0Size);
        PutU32(inflated, xfileOffset + 8u + 4u * 4u, block4Size);

        PutS32(inflated, assetListOffset + 0u, 0);
        PutU32(inflated, assetListOffset + 4u, 0u);
        PutS32(inflated, assetListOffset + 8u, 2);
        PutU32(inflated, assetListOffset + 12u, INLINE_POINTER);

        PutU32(inflated, materialAssetOffset + 0u, ASSET_TYPE_MATERIAL);
        PutU32(inflated, materialAssetOffset + 4u, INLINE_SHARED_POINTER);
        PutU32(inflated, worldAssetOffset + 0u, ASSET_TYPE_GFXWORLD);
        PutU32(inflated, worldAssetOffset + 4u, INLINE_POINTER);

        PutU32(inflated, materialOffset, INLINE_POINTER);
        std::copy(
            materialName.begin(),
            materialName.end(),
            inflated.begin() + static_cast<std::ptrdiff_t>(materialNameOffset));

        PutS32(inflated, worldOffset + WORLD_INDEX_COUNT_OFFSET, 12);
        PutU32(inflated, worldOffset + WORLD_INDICES_OFFSET, INLINE_POINTER);
        PutS32(inflated, worldOffset + WORLD_SURFACE_COUNT_OFFSET, 1);
        PutU32(inflated, worldOffset + WORLD_VERTEX_COUNT_OFFSET, worldVertexCount);
        PutU32(inflated, worldOffset + WORLD_VERTICES_OFFSET, INLINE_POINTER);
        PutS32(inflated, worldOffset + WORLD_MATERIAL_MEMORY_COUNT_OFFSET, 1);
        PutU32(
            inflated,
            worldOffset + WORLD_MATERIAL_MEMORY_OFFSET,
            INLINE_POINTER);
        PutU32(inflated, worldOffset + WORLD_STATIC_SURFACE_COUNT_OFFSET, 1u);
        PutU32(inflated, worldOffset + WORLD_SURFACES_OFFSET, INLINE_POINTER);

        const std::array<std::uint16_t, 12> indices = {
            0xffffu, 0xffffu, 0xffffu,
            0u, 1u, 2u, 2u, 3u, 0u,
            0xffffu, 0xffffu, 0xffffu,
        };
        for (std::size_t index = 0u; index < indices.size(); ++index)
        {
            PutU16(inflated, indicesOffset + index * 2u, indices[index]);
        }

        const std::uint32_t materialAlias =
            (4u << 28u) | (aliasSlotOffset + 1u);
        PutU32(inflated, materialMemoryOffset, materialAlias);
        PutS32(inflated, materialMemoryOffset + 4u, 4242);

        const float poison = std::numeric_limits<float>::quiet_NaN();
        WriteVertex(
            inflated,
            verticesOffset + 0u * kisak::fastfile::GFX_WORLD_VERTEX_WIRE_SIZE,
            poison, poison, poison, PackColor(1, 2, 3, 4), -7.0f, -8.0f,
            -9.0f, -10.0f);
        WriteVertex(
            inflated,
            verticesOffset + 1u * kisak::fastfile::GFX_WORLD_VERTEX_WIRE_SIZE,
            32.0f, 80.0f, 24.0f, PackColor(224, 96, 32, 255), 0.0f, 0.0f);
        WriteVertex(
            inflated,
            verticesOffset + 2u * kisak::fastfile::GFX_WORLD_VERTEX_WIRE_SIZE,
            32.0f, 16.0f, 24.0f, PackColor(48, 176, 80, 255), 0.0f, 1.0f,
            0.0f, 1.0f);
        WriteVertex(
            inflated,
            verticesOffset + 3u * kisak::fastfile::GFX_WORLD_VERTEX_WIRE_SIZE,
            96.0f, 16.0f, 24.0f, PackColor(64, 112, 232, 255), 1.0f, 1.0f,
            1.0f, 1.0f);
        WriteVertex(
            inflated,
            verticesOffset + 4u * kisak::fastfile::GFX_WORLD_VERTEX_WIRE_SIZE,
            96.0f, 80.0f, 24.0f, PackColor(240, 208, 72, 255), 1.0f, 0.0f,
            1.0f, 0.0f);
        WriteVertex(
            inflated,
            verticesOffset + 5u * kisak::fastfile::GFX_WORLD_VERTEX_WIRE_SIZE,
            4096.0f, 4096.0f, 4096.0f, PackColor(5, 6, 7, 8), 7.0f, 8.0f,
            9.0f, 10.0f, -1.0f, 0x11121314u, 0x15161718u);

        PutS32(inflated, surfaceOffset + 0u, 0);
        PutS32(inflated, surfaceOffset + 4u, 1);
        PutU16(inflated, surfaceOffset + 8u, 4u);
        PutU16(inflated, surfaceOffset + 10u, 2u);
        PutS32(inflated, surfaceOffset + 12u, 3);
        PutU32(
            inflated,
            surfaceOffset + 16u,
            materialAlias);
        inflated[surfaceOffset + 20u] = 7u;
        inflated[surfaceOffset + 21u] = 11u;
        inflated[surfaceOffset + 22u] = 13u;
        inflated[surfaceOffset + 23u] = 0x5au;
        const std::array<float, 3> mins = {32.0f, 16.0f, 24.0f};
        const std::array<float, 3> maxs = {96.0f, 80.0f, 24.0f};
        for (std::size_t axis = 0u; axis < 3u; ++axis)
        {
            PutF32(inflated, surfaceOffset + 24u + axis * 4u, mins[axis]);
            PutF32(inflated, surfaceOffset + 36u + axis * 4u, maxs[axis]);
        }
        Repack();
    }

    void Repack()
    {
        const std::vector<std::uint8_t> compressed = Compress(inflated);
        file.assign({
            'I', 'W', 'f', 'f', 'u', '1', '0', '0',
            5u, 0u, 0u, 0u,
        });
        file.insert(file.end(), compressed.begin(), compressed.end());
    }

    std::size_t VertexOffset(std::size_t index, std::size_t field = 0u) const
    {
        return verticesOffset +
            index * kisak::fastfile::GFX_WORLD_VERTEX_WIRE_SIZE + field;
    }

    std::size_t IndexOffset(std::size_t index) const
    {
        return indicesOffset + index * sizeof(std::uint16_t);
    }
};

ExtractedWorldSurface ExtractGolden(const SyntheticFastfile &fixture)
{
    ExtractedWorldSurface extracted;
    RequireResult(
        kisak::fastfile::ExtractWorldSurface(fixture.file, {}, extracted),
        Error::None,
        "extract golden synthetic fastfile");
    return extracted;
}

bool SameVertex(const WebEngineWorldVertex &left, const WebEngineWorldVertex &right)
{
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}

bool SameExtracted(
    const ExtractedWorldSurface &left,
    const ExtractedWorldSurface &right)
{
    if (left.fastfileVersion != right.fastfileVersion ||
        left.compressedBytes != right.compressedBytes ||
        left.inflatedBytes != right.inflatedBytes ||
        left.declaredZoneBytes != right.declaredZoneBytes ||
        left.blockSizes != right.blockSizes ||
        left.sourceAssetCount != right.sourceAssetCount ||
        left.materialAssetIndex != right.materialAssetIndex ||
        left.worldAssetIndex != right.worldAssetIndex ||
        left.materialIdentity != right.materialIdentity ||
        left.worldIdentity != right.worldIdentity ||
        left.registeredAssetCount != right.registeredAssetCount ||
        left.sourceWorldVertexCount != right.sourceWorldVertexCount ||
        left.sourceWorldIndexCount != right.sourceWorldIndexCount ||
        left.sourceWorldSurfaceCount != right.sourceWorldSurfaceCount ||
        left.sourceSurfaceIndex != right.sourceSurfaceIndex ||
        left.materialName != right.materialName ||
        left.indices != right.indices ||
        left.vertices.size() != right.vertices.size())
    {
        return false;
    }
    const auto &a = left.metadata;
    const auto &b = right.metadata;
    if (a.sourceFirstVertex != b.sourceFirstVertex ||
        a.sourceBaseIndex != b.sourceBaseIndex ||
        a.vertexCount != b.vertexCount ||
        a.triangleCount != b.triangleCount ||
        a.lightmapIndex != b.lightmapIndex ||
        a.reflectionProbeIndex != b.reflectionProbeIndex ||
        a.primaryLightIndex != b.primaryLightIndex ||
        a.flags != b.flags ||
        a.materialReference != b.materialReference)
    {
        return false;
    }
    for (std::size_t bound = 0u; bound < 2u; ++bound)
    {
        for (std::size_t axis = 0u; axis < 3u; ++axis)
        {
            if (a.bounds[bound][axis] != b.bounds[bound][axis])
            {
                return false;
            }
        }
    }
    for (std::size_t index = 0u; index < left.vertices.size(); ++index)
    {
        if (!SameVertex(left.vertices[index], right.vertices[index]))
        {
            return false;
        }
    }
    return true;
}

struct IncrementalRun
{
    Error error = Error::None;
    ExtractedWorldSurface output;
    std::vector<StepReport> reports;
};

IncrementalRun RunIncremental(
    std::vector<std::uint8_t> file,
    const StepBudget &budget,
    const Limits &limits = {})
{
    IncrementalRun run;
    WorldSurfaceExtractionJob job;
    run.error = job.Begin(std::move(file), limits);
    if (run.error != Error::None)
    {
        Require(job.Progress() == JobProgress::Failed,
            "failed Begin has terminal progress");
        Require(job.Stage() == JobStage::Failed,
            "failed Begin has terminal stage");
        Require(job.Failure() == run.error,
            "failed Begin retains its deterministic error");
        const StepReport terminal = job.Step(budget);
        Require(terminal.progress == JobProgress::Failed &&
            terminal.stage == JobStage::Failed && terminal.error == run.error &&
            terminal.compressedBytesConsumedThisStep == 0u &&
            terminal.inflatedBytesProducedThisStep == 0u &&
            terminal.traversedBytesThisStep == 0u &&
            terminal.recordsProcessedThisStep == 0u,
            "failed Begin remains zero-work terminal under Step");
        return run;
    }

    Require(job.Progress() == JobProgress::Running,
        "successful Begin starts a running job");
    Require(job.Stage() == JobStage::Inflate,
        "successful Begin starts in inflate stage");
    constexpr std::size_t MAX_STEPS = 1u << 22u;
    for (std::size_t step = 0u;
        job.Progress() == JobProgress::Running && step < MAX_STEPS;
        ++step)
    {
        StepReport report = job.Step(budget);
        Require(report.compressedBytesConsumedThisStep <= budget.maxBytes,
            "step compressed-input work stays within budget");
        Require(report.inflatedBytesProducedThisStep <= budget.maxBytes,
            "step inflated-output work stays within budget");
        Require(report.traversedBytesThisStep <= budget.maxBytes,
            "step traversal work stays within budget");
        Require(report.recordsProcessedThisStep <= budget.maxRecords,
            "step record work stays within budget");
        run.reports.push_back(report);
    }
    Require(job.Progress() != JobProgress::Running,
        "incremental job terminates within its bounded iteration guard");

    run.error = job.Failure();
    if (job.Progress() == JobProgress::Succeeded)
    {
        Require(run.error == Error::None,
            "successful job retains no failure");
        Require(job.Stage() == JobStage::Complete,
            "successful job has complete stage");
        Require(job.TakeResult(run.output),
            "successful job transfers its result once");
        ExtractedWorldSurface sentinel = ExtractGolden(SyntheticFastfile{});
        const ExtractedWorldSurface snapshot = sentinel;
        Require(!job.TakeResult(sentinel),
            "successful job refuses a second result transfer");
        Require(SameExtracted(sentinel, snapshot),
            "refused second result transfer is atomic");
    }
    else
    {
        Require(job.Progress() == JobProgress::Failed,
            "non-successful incremental job is failed");
        Require(run.error != Error::None,
            "failed incremental job retains an error");
    }

    const StepReport terminal = job.Step(budget);
    Require(terminal.progress == job.Progress() && terminal.stage == job.Stage() &&
        terminal.error == job.Failure(),
        "terminal Step is status-idempotent");
    Require(terminal.compressedBytesConsumedThisStep == 0u &&
        terminal.inflatedBytesProducedThisStep == 0u &&
        terminal.traversedBytesThisStep == 0u &&
        terminal.recordsProcessedThisStep == 0u,
        "terminal Step reports zero work");
    return run;
}

void RequireFailureAtomic(
    const SyntheticFastfile &fixture,
    Error expected,
    std::string_view context,
    const Limits &limits = {})
{
    const SyntheticFastfile goldenFixture;
    const ExtractedWorldSurface sentinel = ExtractGolden(goldenFixture);
    ExtractedWorldSurface destination = sentinel;
    RequireResult(
        kisak::fastfile::ExtractWorldSurface(fixture.file, limits, destination),
        expected,
        context);
    Require(SameExtracted(destination, sentinel),
        std::string(context) + ": destination remains unchanged");
}

WebEngineWorldProjection2D GoldenProjection()
{
    return {
        {1.0f / 64.0f, 0.0f, 0.0f, -1.0f},
        {0.0f, 1.0f / 64.0f, 0.0f, -0.75f},
    };
}

void TestGoldenExtractionAndConversion()
{
    const SyntheticFastfile fixture;
    const ExtractedWorldSurface extracted = ExtractGolden(fixture);

    Require(extracted.fastfileVersion == 5u, "v5 fastfile version retained");
    Require(extracted.compressedBytes == fixture.file.size() - PREFIX_SIZE,
        "compressed payload byte count retained");
    Require(extracted.inflatedBytes == fixture.inflated.size(),
        "inflated payload byte count retained");
    Require(extracted.declaredZoneBytes == fixture.block0Size + fixture.block4Size,
        "declared zone bytes retained");
    Require(extracted.blockSizes[0] == fixture.block0Size,
        "block zero allocation size retained");
    Require(extracted.blockSizes[4] == fixture.block4Size,
        "block four allocation size retained");
    Require(fixture.inflated.size() == 1246u,
        "two-asset fixture has its exact decompressed record size");
    Require(fixture.block0Size == 732u && fixture.block4Size == 380u,
        "two-asset fixture has exact rewindable and persistent arena extents");
    Require(fixture.aliasSlotOffset == 16u,
        "top-level shared material reserves the first post-table alias slot");
    Require(ReadU32(fixture.inflated, fixture.materialMemoryOffset) == 0x40000011u &&
            ReadU32(fixture.inflated, fixture.surfaceOffset + 16u) == 0x40000011u,
        "world material handles share the exact top-level material alias token");
    Require(extracted.declaredZoneBytes == 1112u,
        "two-asset fixture retains its exact declared zone bytes");
    for (std::size_t block = 0u; block < extracted.blockSizes.size(); ++block)
    {
        if (block != 0u && block != 4u)
        {
            Require(extracted.blockSizes[block] == 0u,
                "unused block remains empty");
        }
    }
    Require(extracted.sourceWorldVertexCount == 6u, "source vertex count retained");
    Require(extracted.sourceAssetCount == 2u, "two top-level assets retained");
    Require(extracted.materialAssetIndex == 0u,
        "material top-level table index retained");
    Require(extracted.worldAssetIndex == 1u,
        "world top-level table index retained");
    Require(extracted.materialIdentity == 1u,
        "stable job-local material identity retained after block-zero reuse");
    Require(extracted.worldIdentity == 2u,
        "stable job-local world identity follows the registered material");
    Require(extracted.registeredAssetCount == 2u,
        "registry owns exactly the accepted top-level asset envelope");
    Require(extracted.sourceWorldIndexCount == 12u, "source index count retained");
    Require(extracted.sourceWorldSurfaceCount == 1u, "source surface count retained");
    Require(extracted.sourceSurfaceIndex == 0u, "source surface index retained");
    Require(extracted.metadata.sourceFirstVertex == 1u,
        "nonzero source firstVertex retained");
    Require(extracted.metadata.sourceBaseIndex == 3u,
        "nonzero source baseIndex retained");
    Require(extracted.metadata.vertexCount == 4u, "selected vertex count retained");
    Require(extracted.metadata.triangleCount == 2u, "triangle count retained");
    Require(extracted.metadata.lightmapIndex == 7u, "lightmap index retained");
    Require(extracted.metadata.reflectionProbeIndex == 11u,
        "reflection-probe index retained");
    Require(extracted.metadata.primaryLightIndex == 13u,
        "primary-light index retained");
    Require(extracted.metadata.flags == 0x5au, "surface flags retained");
    RequireNear(extracted.metadata.bounds[0][0], 32.0f, "minimum X retained");
    RequireNear(extracted.metadata.bounds[1][1], 80.0f, "maximum Y retained");
    Require(extracted.materialName == fixture.materialName, "material name retained");
    Require(extracted.metadata.materialReference ==
            kisak::fastfile::MaterialReferenceKind::AliasToInlineShared,
        "material dependency is classified");
    Require(extracted.vertices.size() == 4u, "only selected vertices are owned");
    Require(extracted.indices ==
            std::vector<std::uint16_t>({0u, 1u, 2u, 2u, 3u, 0u}),
        "only selected local indices are owned");

    RequireNear(extracted.vertices[0].xyz[0], 32.0f, "first selected X decoded LE");
    RequireNear(extracted.vertices[0].xyz[1], 80.0f, "first selected Y decoded LE");
    Require(extracted.vertices[0].color == PackColor(224, 96, 32, 255),
        "packed native color decoded LE");
    RequireNear(extracted.vertices[2].textureCoordinate[0], 1.0f,
        "texture coordinate decoded");
    Require(extracted.vertices[3].normal == 0x7f7f7fffu,
        "packed normal decoded");

    const WebEngineWorldSurfaceView view = extracted.View();
    Require(view.world.vertices == extracted.vertices.data(),
        "view points at owned vertices");
    Require(view.world.indices == extracted.indices.data(),
        "view points at owned indices");
    Require(view.world.vertexCount == 4u && view.world.indexCount == 6u,
        "view publishes selected counts");
    Require(view.surface.firstVertex == 0 && view.surface.baseIndex == 0,
        "view normalizes shared-array offsets");
    Require(view.surface.vertexCount == 4u && view.surface.triangleCount == 2u,
        "view preserves selected range size");
    Require(view.vertexFormat == WebEngineWorldVertexFormat::Base,
        "view maps deliberately to base-world vertices");

    WebEngineConvertedWorldSurface converted;
    RequireConverterResult(
        WebEngine_ConvertWorldSurface(view, GoldenProjection(), converted),
        WebEngineWorldSurfaceResult::Success,
        "feed extracted owner through existing converter");
    Require(converted.indices == extracted.indices,
        "converter receives the extracted local indices");
    const std::array<std::array<float, 2>, 4> positions = {{
        {-0.5f, 0.5f}, {-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f},
    }};
    for (std::size_t index = 0u; index < positions.size(); ++index)
    {
        RequireNear(converted.vertices[index].position[0], positions[index][0],
            "converted clip X matches current runtime proof");
        RequireNear(converted.vertices[index].position[1], positions[index][1],
            "converted clip Y matches current runtime proof");
    }
    RequireNear(converted.vertices[0].color[0], 224.0f / 255.0f,
        "converted red matches native packed color");
    RequireNear(converted.vertices[0].color[1], 96.0f / 255.0f,
        "converted green matches native packed color");
    RequireNear(converted.vertices[0].color[2], 32.0f / 255.0f,
        "converted blue matches native packed color");
}

void TestPrefixAndVersionFraming()
{
    const SyntheticFastfile golden;
    for (std::size_t size = 0u; size < PREFIX_SIZE; ++size)
    {
        SyntheticFastfile truncated;
        truncated.file.resize(size);
        RequireFailureAtomic(truncated, Error::PrefixTruncated,
            "every incomplete 12-byte prefix is rejected");
    }

    SyntheticFastfile invalidMagic;
    invalidMagic.file[0] ^= 0x20u;
    RequireFailureAtomic(invalidMagic, Error::InvalidMagic, "invalid magic");

    SyntheticFastfile authenticated;
    const std::array<std::uint8_t, 8> authenticatedMagic = {
        'I', 'W', 'f', 'f', '0', '1', '0', '0',
    };
    std::copy(authenticatedMagic.begin(), authenticatedMagic.end(), authenticated.file.begin());
    RequireFailureAtomic(authenticated, Error::AuthenticatedUnsupported,
        "authenticated framing is distinguished from corruption");

    for (std::uint32_t version : {0u, 4u, 6u, 0x05000000u})
    {
        SyntheticFastfile unsupported;
        PutU32(unsupported.file, 8u, version);
        RequireFailureAtomic(unsupported, Error::UnsupportedVersion,
            "only little-endian fastfile version five is accepted");
    }

    Limits exact;
    exact.maxFileBytes = static_cast<std::uint32_t>(golden.file.size());
    ExtractedWorldSurface extracted;
    RequireResult(kisak::fastfile::ExtractWorldSurface(golden.file, exact, extracted),
        Error::None, "exact file-size limit is accepted");
    exact.maxFileBytes -= 1u;
    RequireFailureAtomic(golden, Error::FileTooLarge, "file-size limit plus one", exact);

    for (auto member : {
            &Limits::maxInflatedBytes,
            &Limits::maxBlockBytes,
            &Limits::maxTotalBlockBytes,
            &Limits::maxAssets,
            &Limits::maxStackDepth,
            &Limits::maxDelayedSpans,
            &Limits::maxDelayedBytes,
            &Limits::maxTotalStringBytes,
            &Limits::maxAliases,
            &Limits::maxWorldVertices,
            &Limits::maxWorldIndices,
            &Limits::maxSelectedVertices,
            &Limits::maxSelectedIndices,
            &Limits::maxMaterialNameBytes})
    {
        Limits invalid;
        invalid.*member = 0u;
        RequireFailureAtomic(golden, Error::InvalidArgument,
            "zero parser limit is rejected", invalid);
    }
    Limits shortMaximum;
    shortMaximum.maxFileBytes = static_cast<std::uint32_t>(PREFIX_SIZE - 1u);
    RequireFailureAtomic(golden, Error::InvalidArgument,
        "file limit cannot be shorter than framing", shortMaximum);
}

void TestZlibEnvelopeAndInflatedLimits()
{
    const SyntheticFastfile golden;

    SyntheticFastfile truncated = golden;
    truncated.file.pop_back();
    RequireFailureAtomic(truncated, Error::InflateTruncated,
        "truncated zlib stream");

    SyntheticFastfile invalid = golden;
    invalid.file.resize(PREFIX_SIZE);
    invalid.file.insert(invalid.file.end(), {0xffu, 0xffu, 0xffu, 0xffu});
    RequireFailureAtomic(invalid, Error::InflateData, "invalid zlib stream");

    SyntheticFastfile zlibTrailing = golden;
    zlibTrailing.file.push_back(0xa5u);
    RequireFailureAtomic(zlibTrailing, Error::InflateTrailingData,
        "bytes after zlib stream");

    SyntheticFastfile emptyPayload = golden;
    emptyPayload.file.resize(PREFIX_SIZE);
    RequireFailureAtomic(emptyPayload, Error::InflateTruncated,
        "missing compressed payload");

    Limits exact;
    exact.maxInflatedBytes = static_cast<std::uint32_t>(golden.inflated.size());
    ExtractedWorldSurface extracted;
    RequireResult(kisak::fastfile::ExtractWorldSurface(golden.file, exact, extracted),
        Error::None, "exact inflated-size ceiling is accepted");
    exact.maxInflatedBytes -= 1u;
    RequireFailureAtomic(golden, Error::InflatedSizeLimit,
        "inflated-size ceiling plus one", exact);

    SyntheticFastfile logicalTrailing = golden;
    logicalTrailing.inflated.push_back(0x5au);
    logicalTrailing.Repack();
    RequireFailureAtomic(logicalTrailing, Error::TrailingData,
        "trailing logical record byte inside zlib stream");
}

void TestRecordTruncation()
{
    const SyntheticFastfile golden;
    const std::array<std::size_t, 10> terminalSizes = {
        XFILE_SIZE - 1u,
        golden.assetListOffset + XASSET_LIST_SIZE - 1u,
        golden.materialAssetOffset + XASSET_SIZE - 1u,
        golden.worldAssetOffset,
        golden.worldAssetOffset + XASSET_SIZE - 1u,
        golden.materialOffset + kisak::fastfile::MATERIAL_WIRE_SIZE - 1u,
        golden.worldOffset + kisak::fastfile::GFX_WORLD_WIRE_SIZE - 1u,
        golden.indicesOffset + 12u * sizeof(std::uint16_t) - 1u,
        golden.materialMemoryOffset + MATERIAL_MEMORY_SIZE - 1u,
        golden.verticesOffset + 6u * kisak::fastfile::GFX_WORLD_VERTEX_WIRE_SIZE - 1u,
    };
    for (std::size_t size : terminalSizes)
    {
        SyntheticFastfile fixture = golden;
        fixture.inflated.resize(size);
        fixture.Repack();
        RequireFailureAtomic(fixture, Error::RecordTruncated,
            "one-byte truncation at every fixed/array record");
    }

    SyntheticFastfile nameTruncated = golden;
    nameTruncated.inflated.resize(
        nameTruncated.materialNameOffset + nameTruncated.materialName.size());
    nameTruncated.Repack();
    RequireFailureAtomic(nameTruncated, Error::MaterialNameTruncated,
        "material name terminator is required");

    SyntheticFastfile surfaceTruncated = golden;
    surfaceTruncated.inflated.resize(
        surfaceTruncated.surfaceOffset + kisak::fastfile::GFX_SURFACE_WIRE_SIZE - 1u);
    surfaceTruncated.Repack();
    RequireFailureAtomic(surfaceTruncated, Error::RecordTruncated,
        "surface record truncation");
}

void TestBlockLimitsAndAlignment()
{
    const SyntheticFastfile golden;
    const std::uint32_t total = golden.block0Size + golden.block4Size;

    Limits exact;
    exact.maxBlockBytes = std::max(golden.block0Size, golden.block4Size);
    exact.maxTotalBlockBytes = total;
    ExtractedWorldSurface extracted;
    RequireResult(kisak::fastfile::ExtractWorldSurface(golden.file, exact, extracted),
        Error::None, "exact block ceilings are accepted");

    Limits blockLimit = exact;
    blockLimit.maxBlockBytes -= 1u;
    RequireFailureAtomic(golden, Error::BlockSizeLimit,
        "per-block ceiling plus one", blockLimit);
    Limits totalLimit = exact;
    totalLimit.maxTotalBlockBytes -= 1u;
    RequireFailureAtomic(golden, Error::TotalBlockSizeLimit,
        "cumulative block ceiling plus one", totalLimit);

    SyntheticFastfile declaredTooLarge = golden;
    PutU32(declaredTooLarge.inflated, declaredTooLarge.xfileOffset, total + 1u);
    declaredTooLarge.Repack();
    RequireFailureAtomic(declaredTooLarge, Error::TotalBlockSizeLimit,
        "declared zone size has an independent ceiling", exact);

    SyntheticFastfile unsupportedBlock = golden;
    PutU32(unsupportedBlock.inflated, unsupportedBlock.xfileOffset + 8u + 7u * 4u, 1u);
    unsupportedBlock.Repack();
    RequireFailureAtomic(unsupportedBlock, Error::UnsupportedBlock,
        "vertex arena is not silently admitted to narrow slice");

    for (std::size_t block : {0u, 4u})
    {
        SyntheticFastfile shortBlock = golden;
        const std::size_t offset = shortBlock.xfileOffset + 8u + block * 4u;
        PutU32(shortBlock.inflated, offset, ReadU32(shortBlock.inflated, offset) - 1u);
        shortBlock.Repack();
        RequireFailureAtomic(shortBlock, Error::ZoneBlockOverflow,
            "logical allocation cannot exceed declared block");

        SyntheticFastfile longBlock = golden;
        PutU32(longBlock.inflated, offset, ReadU32(longBlock.inflated, offset) + 1u);
        longBlock.Repack();
        RequireFailureAtomic(longBlock, Error::ZoneBlockSizeMismatch,
            "unconsumed block tail is rejected");
    }

    for (std::string name : {"a", "ab", "abc", "abcd", "abcde", "abcdef"})
    {
        const SyntheticFastfile aligned(std::move(name));
        const ExtractedWorldSurface result = ExtractGolden(aligned);
        Require(result.blockSizes[4] == aligned.block4Size,
            "material-name length exercises four-byte vertex alignment");
        Require(result.materialName == aligned.materialName,
            "alignment padding is not included in material name");
    }
}

void TestEnvelopeAndDependencyRejections()
{
    const SyntheticFastfile golden;
    struct Mutation
    {
        const char *name;
        Error error;
        std::function<void(SyntheticFastfile &)> apply;
    };
    const std::vector<Mutation> mutations = {
        {"external data", Error::ExternalDataUnsupported, [](SyntheticFastfile &f) {
            PutU32(f.inflated, f.xfileOffset + 4u, 1u);
        }},
        {"script string count", Error::ScriptStringsUnsupported, [](SyntheticFastfile &f) {
            PutS32(f.inflated, f.assetListOffset, 1);
        }},
        {"script string pointer", Error::ScriptStringsUnsupported, [](SyntheticFastfile &f) {
            PutU32(f.inflated, f.assetListOffset + 4u, INLINE_POINTER);
        }},
        {"zero assets", Error::AssetCountUnsupported, [](SyntheticFastfile &f) {
            PutS32(f.inflated, f.assetListOffset + 8u, 0);
        }},
        {"one asset", Error::AssetCountUnsupported, [](SyntheticFastfile &f) {
            PutS32(f.inflated, f.assetListOffset + 8u, 1);
        }},
        {"negative asset count", Error::AssetCountUnsupported, [](SyntheticFastfile &f) {
            PutS32(f.inflated, f.assetListOffset + 8u, -1);
        }},
        {"third asset", Error::AssetCountLimit, [](SyntheticFastfile &f) {
            PutS32(f.inflated, f.assetListOffset + 8u, 3);
        }},
        {"missing asset array", Error::MissingAssetArray, [](SyntheticFastfile &f) {
            PutU32(f.inflated, f.assetListOffset + 12u, 0u);
        }},
        {"unsupported preceding asset type", Error::AssetTypeUnsupported, [](SyntheticFastfile &f) {
            PutU32(f.inflated, f.materialAssetOffset, ASSET_TYPE_MATERIAL - 1u);
        }},
        {"wrong second asset type", Error::AssetTypeUnsupported, [](SyntheticFastfile &f) {
            PutU32(f.inflated, f.worldAssetOffset, ASSET_TYPE_GFXWORLD - 1u);
        }},
        {"world before material alias definition", Error::MaterialAliasUndefined,
            [](SyntheticFastfile &f) {
                PutU32(f.inflated, f.materialAssetOffset, ASSET_TYPE_GFXWORLD);
                PutU32(f.inflated, f.worldAssetOffset, ASSET_TYPE_MATERIAL);
            }},
        {"null material reference", Error::AssetReferenceUnsupported,
            [](SyntheticFastfile &f) {
                PutU32(f.inflated, f.materialAssetOffset + 4u, 0u);
            }},
        {"non-shared top-level material", Error::AssetReferenceUnsupported,
            [](SyntheticFastfile &f) {
                PutU32(f.inflated, f.materialAssetOffset + 4u, INLINE_POINTER);
            }},
        {"aliased top-level material", Error::AssetReferenceUnsupported,
            [](SyntheticFastfile &f) {
                PutU32(f.inflated, f.materialAssetOffset + 4u,
                    (4u << 28u) | (f.aliasSlotOffset + 1u));
            }},
        {"null world reference", Error::AssetReferenceUnsupported, [](SyntheticFastfile &f) {
            PutU32(f.inflated, f.worldAssetOffset + 4u, 0u);
        }},
        {"shared world reference", Error::AssetReferenceUnsupported, [](SyntheticFastfile &f) {
            PutU32(f.inflated, f.worldAssetOffset + 4u, INLINE_SHARED_POINTER);
        }},
        {"unsupported world field", Error::UnsupportedWorldField, [](SyntheticFastfile &f) {
            f.inflated[f.worldOffset + 0x1cu] = 1u;
        }},
        {"material memory null", Error::MaterialAliasInvalid, [](SyntheticFastfile &f) {
            PutU32(f.inflated, f.materialMemoryOffset, 0u);
        }},
        {"material memory inline", Error::MaterialAliasInvalid, [](SyntheticFastfile &f) {
            PutU32(f.inflated, f.materialMemoryOffset, INLINE_POINTER);
        }},
        {"material memory inline-shared", Error::MaterialAliasInvalid,
            [](SyntheticFastfile &f) {
                PutU32(f.inflated, f.materialMemoryOffset, INLINE_SHARED_POINTER);
            }},
        {"negative material memory", Error::MaterialMemoryUnsupported, [](SyntheticFastfile &f) {
            PutS32(f.inflated, f.materialMemoryOffset + 4u, -1);
        }},
        {"material name reference null", Error::MaterialLayoutUnsupported, [](SyntheticFastfile &f) {
            PutU32(f.inflated, f.materialOffset, 0u);
        }},
        {"material technique dependency", Error::MaterialLayoutUnsupported,
            [](SyntheticFastfile &f) {
                PutU32(f.inflated, f.materialOffset + 64u, INLINE_POINTER);
        }},
    };
    for (const Mutation &mutation : mutations)
    {
        SyntheticFastfile fixture = golden;
        mutation.apply(fixture);
        fixture.Repack();
        RequireFailureAtomic(fixture, mutation.error, mutation.name);
    }

    Limits exactAssets;
    exactAssets.maxAssets = 2u;
    ExtractedWorldSurface extracted;
    RequireResult(kisak::fastfile::ExtractWorldSurface(
            golden.file, exactAssets, extracted),
        Error::None, "exact supported-prefix asset limit is accepted");
    exactAssets.maxAssets = 1u;
    RequireFailureAtomic(golden, Error::AssetCountLimit,
        "two-asset prefix exceeds a one-asset limit", exactAssets);

    for (std::size_t markerOffset : {
            WORLD_INDICES_OFFSET,
            WORLD_VERTICES_OFFSET,
            WORLD_MATERIAL_MEMORY_OFFSET,
            WORLD_SURFACES_OFFSET})
    {
        SyntheticFastfile missing = golden;
        PutU32(missing.inflated, missing.worldOffset + markerOffset, 0u);
        missing.Repack();
        RequireFailureAtomic(missing, Error::MissingWorldArray,
            "every required GfxWorld array marker is checked");
    }
}

void TestWorldCountsAndLimits()
{
    const SyntheticFastfile golden;
    struct CountMutation
    {
        std::size_t offset;
        std::int32_t value;
    };
    const std::array<CountMutation, 8> invalid = {{
        {WORLD_INDEX_COUNT_OFFSET, 0},
        {WORLD_INDEX_COUNT_OFFSET, -1},
        {WORLD_SURFACE_COUNT_OFFSET, 0},
        {WORLD_SURFACE_COUNT_OFFSET, 2},
        {WORLD_VERTEX_COUNT_OFFSET, 0},
        {WORLD_MATERIAL_MEMORY_COUNT_OFFSET, 0},
        {WORLD_MATERIAL_MEMORY_COUNT_OFFSET, 2},
        {WORLD_STATIC_SURFACE_COUNT_OFFSET, 2},
    }};
    for (const CountMutation &mutation : invalid)
    {
        SyntheticFastfile fixture = golden;
        PutS32(fixture.inflated, fixture.worldOffset + mutation.offset, mutation.value);
        fixture.Repack();
        RequireFailureAtomic(fixture, Error::InvalidWorldCount,
            "invalid signed or fixed world count");
    }

    Limits exact;
    exact.maxWorldVertices = 6u;
    exact.maxWorldIndices = 12u;
    ExtractedWorldSurface extracted;
    RequireResult(kisak::fastfile::ExtractWorldSurface(golden.file, exact, extracted),
        Error::None, "exact world count limits are accepted");
    exact.maxWorldVertices = 5u;
    RequireFailureAtomic(golden, Error::WorldCountLimit,
        "world vertex limit plus one", exact);
    exact.maxWorldVertices = 6u;
    exact.maxWorldIndices = 11u;
    RequireFailureAtomic(golden, Error::WorldCountLimit,
        "world index limit plus one", exact);

    Limits selectedExact;
    selectedExact.maxSelectedVertices = 4u;
    selectedExact.maxSelectedIndices = 6u;
    RequireResult(kisak::fastfile::ExtractWorldSurface(
            golden.file, selectedExact, extracted),
        Error::None, "exact selected count limits are accepted");
    selectedExact.maxSelectedVertices = 3u;
    RequireFailureAtomic(golden, Error::SurfaceOutputTooLarge,
        "selected vertex limit plus one", selectedExact);
    selectedExact.maxSelectedVertices = 4u;
    selectedExact.maxSelectedIndices = 5u;
    RequireFailureAtomic(golden, Error::SurfaceOutputTooLarge,
        "selected index limit plus one", selectedExact);

    SyntheticFastfile rendererVertexLimit = golden;
    PutU16(rendererVertexLimit.inflated, rendererVertexLimit.surfaceOffset + 8u,
        static_cast<std::uint16_t>(WEB_RENDERER_MAX_SURFACE_VERTICES + 1u));
    rendererVertexLimit.Repack();
    RequireFailureAtomic(rendererVertexLimit, Error::SurfaceOutputTooLarge,
        "renderer vertex cap is enforced independently");

    SyntheticFastfile rendererIndexLimit = golden;
    PutU16(rendererIndexLimit.inflated, rendererIndexLimit.surfaceOffset + 10u,
        static_cast<std::uint16_t>(WEB_RENDERER_MAX_SURFACE_INDICES / 3u + 1u));
    rendererIndexLimit.Repack();
    RequireFailureAtomic(rendererIndexLimit, Error::SurfaceOutputTooLarge,
        "renderer index cap is enforced independently");
}

void TestMaterialNamesAndAliases()
{
    const SyntheticFastfile golden;
    Limits exact;
    exact.maxMaterialNameBytes = static_cast<std::uint32_t>(golden.materialName.size());
    ExtractedWorldSurface extracted;
    RequireResult(kisak::fastfile::ExtractWorldSurface(golden.file, exact, extracted),
        Error::None, "exact material-name limit is accepted");
    exact.maxMaterialNameBytes -= 1u;
    RequireFailureAtomic(golden, Error::MaterialNameTooLong,
        "material-name limit plus one", exact);

    Limits exactTotalString;
    exactTotalString.maxTotalStringBytes = static_cast<std::uint32_t>(
        golden.materialName.size() + 1u);
    RequireResult(kisak::fastfile::ExtractWorldSurface(
            golden.file, exactTotalString, extracted),
        Error::None, "exact cumulative string-byte limit includes the terminator");
    exactTotalString.maxTotalStringBytes -= 1u;
    RequireFailureAtomic(golden, Error::StringBytesLimit,
        "material terminator exceeds the cumulative string-byte limit",
        exactTotalString);

    SyntheticFastfile empty;
    empty.inflated[empty.materialNameOffset] = 0u;
    empty.Repack();
    RequireFailureAtomic(empty, Error::MaterialNameInvalid,
        "empty material name");

    constexpr std::array<std::uint8_t, 5> invalidNameBytes = {
        0x01u, 0x1fu, 0x7fu, 0x80u, 0xffu,
    };
    for (std::uint8_t invalid : invalidNameBytes)
    {
        SyntheticFastfile fixture = golden;
        fixture.inflated[fixture.materialNameOffset + 1u] = invalid;
        fixture.Repack();
        RequireFailureAtomic(fixture, Error::MaterialNameInvalid,
            "material names are printable ASCII");
    }

    const std::array<std::uint32_t, 9> invalidAliases = {
        0u,
        INLINE_POINTER,
        INLINE_SHARED_POINTER,
        (3u << 28u) | (golden.aliasSlotOffset + 1u),
        (4u << 28u) | golden.aliasSlotOffset,
        (4u << 28u) | (golden.aliasSlotOffset + 2u),
        (4u << 28u) | (golden.aliasSlotOffset + 5u),
        (4u << 28u) | (golden.block4Size + 1u),
        (9u << 28u) | (golden.aliasSlotOffset + 1u),
    };
    for (std::size_t tokenOffset : {
            golden.materialMemoryOffset,
            golden.surfaceOffset + 16u})
    {
        for (std::uint32_t alias : invalidAliases)
        {
            SyntheticFastfile fixture = golden;
            PutU32(fixture.inflated, tokenOffset, alias);
            fixture.Repack();
            RequireFailureAtomic(fixture, Error::MaterialAliasInvalid,
                "both world handles must target the exact defined material alias");
        }
    }
}

void TestSurfaceRangesAndUnsupportedLayering()
{
    const SyntheticFastfile golden;

    SyntheticFastfile layered = golden;
    PutS32(layered.inflated, layered.surfaceOffset, 4);
    layered.Repack();
    RequireFailureAtomic(layered, Error::LayeredSurfaceUnsupported,
        "layered surface data is explicit future work");

    for (std::size_t countOffset : {8u, 10u})
    {
        SyntheticFastfile empty = golden;
        PutU16(empty.inflated, empty.surfaceOffset + countOffset, 0u);
        empty.Repack();
        RequireFailureAtomic(empty, Error::EmptySurface,
            "zero vertex/triangle surface is rejected");
    }

    const std::array<std::pair<std::int32_t, std::int32_t>, 7> invalidRanges = {{
        {-1, 3},
        {1, -1},
        {3, 3},
        {6, 3},
        {7, 3},
        {1, 7},
        {1, std::numeric_limits<std::int32_t>::max()},
    }};
    for (const auto &[firstVertex, baseIndex] : invalidRanges)
    {
        SyntheticFastfile fixture = golden;
        PutS32(fixture.inflated, fixture.surfaceOffset + 4u, firstVertex);
        PutS32(fixture.inflated, fixture.surfaceOffset + 12u, baseIndex);
        fixture.Repack();
        RequireFailureAtomic(fixture, Error::SurfaceRangeInvalid,
            "signed and subtraction-safe surface ranges");
    }

    SyntheticFastfile exactVertexEnd = golden;
    PutS32(exactVertexEnd.inflated, exactVertexEnd.surfaceOffset + 4u, 2);
    exactVertexEnd.Repack();
    ExtractedWorldSurface extracted;
    RequireResult(kisak::fastfile::ExtractWorldSurface(
            exactVertexEnd.file, {}, extracted),
        Error::None, "selected vertices may end exactly at world end");
    RequireNear(extracted.vertices.back().xyz[0], 4096.0f,
        "exact-end selection includes final serialized vertex");

    SyntheticFastfile exactIndexEnd = golden;
    PutS32(exactIndexEnd.inflated, exactIndexEnd.surfaceOffset + 12u, 6);
    PutU16(exactIndexEnd.inflated, exactIndexEnd.IndexOffset(9u), 0u);
    PutU16(exactIndexEnd.inflated, exactIndexEnd.IndexOffset(10u), 1u);
    PutU16(exactIndexEnd.inflated, exactIndexEnd.IndexOffset(11u), 2u);
    exactIndexEnd.Repack();
    RequireResult(kisak::fastfile::ExtractWorldSurface(
            exactIndexEnd.file, {}, extracted),
        Error::None, "selected indices may end exactly at world end");
    Require(extracted.metadata.sourceBaseIndex == 6u,
        "exact-end base index retained");
}

void TestBoundsValidation()
{
    const SyntheticFastfile golden;
    const std::array<std::size_t, 6> boundFields = {
        24u, 28u, 32u, 36u, 40u, 44u,
    };
    for (std::size_t field : boundFields)
    {
        SyntheticFastfile fixture = golden;
        PutF32(fixture.inflated, fixture.surfaceOffset + field,
            field % 8u == 0u
                ? std::numeric_limits<float>::quiet_NaN()
                : std::numeric_limits<float>::infinity());
        fixture.Repack();
        RequireFailureAtomic(fixture, Error::NonFiniteBounds,
            "all six non-finite bound components are rejected");
    }

    for (std::size_t axis = 0u; axis < 3u; ++axis)
    {
        SyntheticFastfile inverted = golden;
        PutF32(inverted.inflated, inverted.surfaceOffset + 24u + axis * 4u, 1000.0f);
        inverted.Repack();
        RequireFailureAtomic(inverted, Error::InvalidBounds,
            "every inverted bounds axis is rejected");
    }
}

void TestVertexAndIndexValidation()
{
    const SyntheticFastfile golden;
    // Vertex zero contains NaNs and the index guards contain 0xffff, proving
    // that unselected shared-array records are deliberately ignored.
    (void)ExtractGolden(golden);

    const std::array<std::size_t, 8> floatFields = {
        0u, 4u, 8u, 12u, 20u, 24u, 28u, 32u,
    };
    for (std::size_t field : floatFields)
    {
        SyntheticFastfile fixture = golden;
        PutF32(fixture.inflated, fixture.VertexOffset(1u, field),
            field % 8u == 0u
                ? std::numeric_limits<float>::quiet_NaN()
                : -std::numeric_limits<float>::infinity());
        fixture.Repack();
        RequireFailureAtomic(fixture, Error::NonFiniteVertex,
            "every selected floating vertex field is finite");
    }

    for (std::size_t selected = 3u; selected < 9u; ++selected)
    {
        SyntheticFastfile fixture = golden;
        PutU16(fixture.inflated, fixture.IndexOffset(selected), 4u);
        fixture.Repack();
        RequireFailureAtomic(fixture, Error::IndexOutOfRange,
            "each selected local index is checked against selected vertices");
    }
}

void TestAtomicityAndSourceIndependence()
{
    SyntheticFastfile fixture;
    ExtractedWorldSurface extracted = ExtractGolden(fixture);
    const ExtractedWorldSurface snapshot = extracted;

    std::fill(fixture.file.begin(), fixture.file.end(), 0u);
    std::fill(fixture.inflated.begin(), fixture.inflated.end(), 0u);
    Require(SameExtracted(extracted, snapshot),
        "extracted data does not alias compressed or inflated source storage");

    WebEngineConvertedWorldSurface converted;
    RequireConverterResult(
        WebEngine_ConvertWorldSurface(
            extracted.View(), GoldenProjection(), converted),
        WebEngineWorldSurfaceResult::Success,
        "source-independent extracted owner still converts");

    ExtractedWorldSurface moved = std::move(extracted);
    RequireConverterResult(
        WebEngine_ConvertWorldSurface(moved.View(), GoldenProjection(), converted),
        WebEngineWorldSurfaceResult::Success,
        "View reconstructs pointers after owner move");

    SyntheticFastfile invalid;
    invalid.file[0] = 'X';
    ExtractedWorldSurface destination = snapshot;
    RequireResult(kisak::fastfile::ExtractWorldSurface(invalid.file, {}, destination),
        Error::InvalidMagic, "invalid replacement fails");
    Require(SameExtracted(destination, snapshot),
        "invalid replacement is atomic after source owner move proof");
}

void TestIncrementalBudgetsAndOwnership()
{
    const SyntheticFastfile fixture;
    const ExtractedWorldSurface expected = ExtractGolden(fixture);
    const std::array<StepBudget, 5> budgets = {{
        {1u, 1u},
        {7u, 1u},
        {43u, 2u},
        {1024u, 3u},
        {kisak::fastfile::MAX_STEP_BYTES,
            kisak::fastfile::MAX_STEP_RECORDS},
    }};
    for (const StepBudget &budget : budgets)
    {
        const IncrementalRun run = RunIncremental(fixture.file, budget);
        RequireResult(run.error, Error::None,
            "golden incremental extraction across budget partition");
        Require(SameExtracted(run.output, expected),
            "budget partition does not change extracted output");
        Require(run.reports.size() > 1u,
            "incremental extraction crosses at least one Step boundary");
        bool sawInflate = false;
        bool sawTraverse = false;
        for (const StepReport &report : run.reports)
        {
            sawInflate = sawInflate || report.stage == JobStage::Inflate;
            sawTraverse = sawTraverse || report.stage == JobStage::Traverse;
        }
        Require(sawInflate,
            "step reports retain the stage that performed inflate work");
        Require(sawTraverse || budget.maxRecords ==
                kisak::fastfile::MAX_STEP_RECORDS,
            "record-bounded traversal reports its in-progress stage");
        Require(run.reports.back().stage == JobStage::Complete,
            "final successful step reports the complete stage");
    }

    WorldSurfaceExtractionJob job;
    std::vector<std::uint8_t> owned = fixture.file;
    RequireResult(job.Begin(std::move(owned), {}), Error::None,
        "owned incremental source begins");
    owned.assign(4096u, 0xa5u);
    ExtractedWorldSurface sentinel = expected;
    const ExtractedWorldSurface snapshot = sentinel;
    Require(!job.TakeResult(sentinel),
        "running job does not publish partial output");
    Require(SameExtracted(sentinel, snapshot),
        "running job leaves destination atomic");
    (void)job.Step({17u, 1u});
    WorldSurfaceExtractionJob moved(std::move(job));
    Require(job.Progress() == JobProgress::NotStarted,
        "moved-from job returns to a safe idle state");
    while (moved.Progress() == JobProgress::Running)
    {
        (void)moved.Step({17u, 1u});
    }
    ExtractedWorldSurface movedOutput;
    Require(moved.Progress() == JobProgress::Succeeded &&
        moved.TakeResult(movedOutput) && SameExtracted(movedOutput, expected),
        "job move preserves owned source and exact parser state");
}

void TestIncrementalBudgetFailuresAndErrorEquivalence()
{
    const SyntheticFastfile golden;
    const std::array<StepBudget, 4> invalidBudgets = {{
        {0u, 1u},
        {kisak::fastfile::MAX_STEP_BYTES + 1u, 1u},
        {1u, 0u},
        {1u, kisak::fastfile::MAX_STEP_RECORDS + 1u},
    }};
    for (const StepBudget &budget : invalidBudgets)
    {
        const IncrementalRun run = RunIncremental(golden.file, budget);
        RequireResult(run.error, Error::InvalidStepBudget,
            "invalid Step budget is a deterministic terminal failure");
        Require(run.reports.size() == 1u,
            "invalid Step budget fails on its first call");
        const StepReport &report = run.reports.front();
        Require(report.compressedBytesConsumedThisStep == 0u &&
            report.inflatedBytesProducedThisStep == 0u &&
            report.traversedBytesThisStep == 0u &&
            report.recordsProcessedThisStep == 0u,
            "invalid Step budget consumes no work");
    }

    const auto requireMeteredRecordFailure = [](
        SyntheticFastfile fixture,
        std::size_t mutationOffset,
        Error expected,
        std::string_view context)
    {
        fixture.inflated[mutationOffset] = 1u;
        fixture.Repack();
        constexpr StepBudget budget{17u, 1u};
        const IncrementalRun run = RunIncremental(fixture.file, budget);
        RequireResult(run.error, expected, context);
        Require(!run.reports.empty(),
            "malformed fixed record produces a terminal Step report");
        const StepReport &failure = run.reports.back();
        Require(failure.progress == JobProgress::Failed &&
            failure.traversedBytesThisStep != 0u &&
            failure.traversedBytesThisStep <= budget.maxBytes,
            "malformed fixed-record staging is charged before early failure");
    };
    requireMeteredRecordFailure(
        golden,
        golden.worldOffset + 0x1cu,
        Error::UnsupportedWorldField,
        "unsupported world byte retains metered traversal work");
    requireMeteredRecordFailure(
        golden,
        golden.materialOffset + 4u,
        Error::MaterialLayoutUnsupported,
        "unsupported material byte retains metered traversal work");

    std::vector<SyntheticFastfile> malformed;
    SyntheticFastfile badMagic = golden;
    badMagic.file[0] ^= 0x20u;
    malformed.push_back(std::move(badMagic));
    SyntheticFastfile truncatedZlib = golden;
    truncatedZlib.file.pop_back();
    malformed.push_back(std::move(truncatedZlib));
    SyntheticFastfile unsupportedWorld = golden;
    unsupportedWorld.inflated[unsupportedWorld.worldOffset + 0x1cu] = 1u;
    unsupportedWorld.Repack();
    malformed.push_back(std::move(unsupportedWorld));
    SyntheticFastfile badIndex = golden;
    PutU16(badIndex.inflated, badIndex.IndexOffset(3u), 4u);
    badIndex.Repack();
    malformed.push_back(std::move(badIndex));
    SyntheticFastfile trailing = golden;
    trailing.inflated.push_back(0x5au);
    trailing.Repack();
    malformed.push_back(std::move(trailing));

    for (const SyntheticFastfile &fixture : malformed)
    {
        ExtractedWorldSurface syncDestination = ExtractGolden(golden);
        const Error synchronous = kisak::fastfile::ExtractWorldSurface(
            fixture.file, {}, syncDestination);
        const IncrementalRun tiny = RunIncremental(
            fixture.file, {7u, 1u});
        const IncrementalRun wide = RunIncremental(
            fixture.file,
            {kisak::fastfile::MAX_STEP_BYTES,
                kisak::fastfile::MAX_STEP_RECORDS});
        Require(synchronous != Error::None,
            "malformed equivalence fixture fails synchronously");
        RequireResult(tiny.error, synchronous,
            "tiny-budget job preserves synchronous first error");
        RequireResult(wide.error, synchronous,
            "wide-budget job preserves synchronous first error");
    }
}

void TestIncrementalInflateDrainAndBlockZeroHighWater()
{
    const SyntheticFastfile highlyCompressible(
        "mc/high_ratio_incremental_world", 2000u);
    Require(highlyCompressible.inflated.size() >
        kisak::fastfile::MAX_STEP_BYTES,
        "high-ratio fixture expands beyond one output window");
    Require(highlyCompressible.file.size() <
        kisak::fastfile::MAX_STEP_BYTES,
        "high-ratio fixture compressed input fits one source window");
    const IncrementalRun run = RunIncremental(
        highlyCompressible.file,
        {kisak::fastfile::MAX_STEP_BYTES,
            kisak::fastfile::MAX_STEP_RECORDS});
    RequireResult(run.error, Error::None,
        "zlib drains pending output after consuming its final input byte");
    std::uint32_t inflateSteps = 0u;
    std::uint64_t consumedBytes = 0u;
    std::uint64_t producedBytes = 0u;
    for (const StepReport &report : run.reports)
    {
        if (report.stage == JobStage::Inflate)
        {
            ++inflateSteps;
            consumedBytes += report.compressedBytesConsumedThisStep;
            producedBytes += report.inflatedBytesProducedThisStep;
        }
    }
    Require(inflateSteps >= 2u,
        "high-ratio stream drains output across multiple bounded Steps");
    Require(consumedBytes == highlyCompressible.file.size() - PREFIX_SIZE &&
        producedBytes == highlyCompressible.inflated.size(),
        "bounded inflate accounts for exact final input and output totals");
    Require(run.output.sourceWorldVertexCount == 2000u,
        "drained high-ratio world retains its source count");

    const SyntheticFastfile golden;
    Limits exactOutput;
    exactOutput.maxInflatedBytes = static_cast<std::uint32_t>(
        golden.inflated.size());
    const std::vector<std::uint8_t> outputFreeTail =
        BuildStoredControlTailFastfile(golden.inflated, false);
    Require(outputFreeTail.size() - PREFIX_SIZE >
        2u * kisak::fastfile::MAX_STEP_BYTES,
        "exact-output fixture retains more than two input windows of controls");
    const IncrementalRun exactDrain = RunIncremental(
        outputFreeTail,
        {kisak::fastfile::MAX_STEP_BYTES,
            kisak::fastfile::MAX_STEP_RECORDS},
        exactOutput);
    RequireResult(exactDrain.error, Error::None,
        "exact output ceiling permits later output-free deflate and trailer input");
    ExtractedWorldSurface expectedControlTail = ExtractGolden(golden);
    expectedControlTail.compressedBytes = static_cast<std::uint32_t>(
        outputFreeTail.size() - PREFIX_SIZE);
    Require(SameExtracted(exactDrain.output, expectedControlTail),
        "output-free control tail does not change extracted output");
    std::uint64_t acceptedOutput = 0u;
    std::uint32_t atCeilingDrainSteps = 0u;
    for (const StepReport &report : exactDrain.reports)
    {
        acceptedOutput += report.inflatedBytesProducedThisStep;
        if (report.stage == JobStage::Inflate &&
            acceptedOutput == golden.inflated.size() &&
            report.compressedBytesConsumedThisStep != 0u &&
            report.inflatedBytesProducedThisStep == 0u)
        {
            ++atCeilingDrainSteps;
        }
    }
    Require(atCeilingDrainSteps >= 2u,
        "exact-output stream consumes bounded output-free input across later Steps");

    const IncrementalRun overflowAfterControls = RunIncremental(
        BuildStoredControlTailFastfile(golden.inflated, true),
        {kisak::fastfile::MAX_STEP_BYTES,
            kisak::fastfile::MAX_STEP_RECORDS},
        exactOutput);
    RequireResult(overflowAfterControls.error, Error::InflatedSizeLimit,
        "one attempted byte after output-free controls exceeds the exact ceiling");

    // The top-level Material first reaches 80 bytes in block 0 and rewinds.
    // The following GfxWorld reuses those bytes and reaches 732 before it also
    // rewinds. The exact declaration therefore proves max high-water rather
    // than terminal-cursor or incorrectly cumulative 812-byte accounting.
    const IncrementalRun peak = RunIncremental(golden.file, {31u, 2u});
    RequireResult(peak.error, Error::None,
        "rewindable block zero accepts its exact high-water declaration");
    SyntheticFastfile shortPeak = golden;
    PutU32(shortPeak.inflated, shortPeak.xfileOffset + 8u,
        golden.block0Size - 1u);
    shortPeak.Repack();
    RequireResult(RunIncremental(shortPeak.file, {31u, 2u}).error,
        Error::ZoneBlockOverflow,
        "block-zero declaration below reused-world peak overflows");
    SyntheticFastfile longPeak = golden;
    PutU32(longPeak.inflated, longPeak.xfileOffset + 8u,
        golden.block0Size + 1u);
    longPeak.Repack();
    RequireResult(RunIncremental(longPeak.file, {31u, 2u}).error,
        Error::ZoneBlockSizeMismatch,
        "block-zero declaration above exact peak is rejected");
}

void TestResumableSourceStreaming()
{
    const SyntheticFastfile fixture;
    const ExtractedWorldSurface expected = ExtractGolden(fixture);
    constexpr StepBudget budget{5u, 2u};

    Limits limits;
    limits.maxFileBytes = static_cast<std::uint32_t>(fixture.file.size());
    limits.maxSourceChunkBytes = 7u;
    WorldSurfaceExtractionJob job;
    RequireResult(job.BeginStreaming(limits), Error::None,
        "streaming extraction begins without a complete source allocation");
    Require(job.NeedsSource() && !job.SourceFinalReceived(),
        "new streaming job advertises initial source starvation");
    const StepReport starved = job.Step(budget);
    Require(starved.progress == JobProgress::Running && starved.needsSource &&
            starved.sourceBytesConsumedThisStep == 0u &&
            starved.compressedBytesConsumedThisStep == 0u &&
            starved.inflatedBytesProducedThisStep == 0u,
        "source starvation is a zero-work resumable state");

    std::size_t sourceOffset = 0u;
    std::size_t stepGuard = 0u;
    std::uint32_t dataFeeds = 0u;
    while (sourceOffset < fixture.file.size())
    {
        Require(job.NeedsSource(),
            "producer feeds only after the decoder applies backpressure");
        const std::size_t count = std::min<std::size_t>(
            limits.maxSourceChunkBytes, fixture.file.size() - sourceOffset);
        std::vector<std::uint8_t> chunk(
            fixture.file.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
            fixture.file.begin() + static_cast<std::ptrdiff_t>(sourceOffset + count));
        RequireResult(job.FeedSource(chunk, false), Error::None,
            "one bounded non-final source chunk is accepted");
        ++dataFeeds;
        sourceOffset += count;
        std::fill(chunk.begin(), chunk.end(), 0xa5u);

        while (job.Progress() == JobProgress::Running && !job.NeedsSource())
        {
            const StepReport report = job.Step(budget);
            Require(report.sourceBytesConsumedThisStep <= budget.maxBytes &&
                    report.compressedBytesConsumedThisStep <= budget.maxBytes &&
                    report.inflatedBytesProducedThisStep <= budget.maxBytes &&
                    report.traversedBytesThisStep <= budget.maxBytes &&
                    report.recordsProcessedThisStep <= budget.maxRecords,
                "streaming work remains independently bounded per Step");
            Require(++stepGuard < (1u << 22u),
                "streaming extraction remains within its iteration guard");
        }
    }
    Require(job.Progress() == JobProgress::Running && job.NeedsSource(),
        "zlib completion waits for an explicit final marker");
    RequireResult(job.FeedSource({}, true), Error::None,
        "empty final feed closes a fully delivered compressed stream");
    while (job.Progress() == JobProgress::Running)
    {
        (void)job.Step(budget);
        Require(++stepGuard < (1u << 22u),
            "finalized streaming extraction terminates within its guard");
    }
    Require(job.Progress() == JobProgress::Succeeded,
        "arbitrarily chunked source reaches successful traversal");
    Require(job.SourceFinalReceived() &&
            job.SourceBytesReceived() == fixture.file.size() &&
            job.SourceBytesConsumed() == fixture.file.size() &&
            job.SourceFeedCount() == dataFeeds + 1u,
        "source lifecycle retains exact feed and byte accounting");
    ExtractedWorldSurface streamed;
    Require(job.TakeResult(streamed) && SameExtracted(streamed, expected),
        "streamed source produces the exact synchronous output");
    RequireResult(job.FeedSource({}, true), Error::SourceNotReady,
        "completed job refuses source mutation");

    for (std::size_t split = 1u; split <= PREFIX_SIZE; ++split)
    {
        WorldSurfaceExtractionJob splitJob;
        RequireResult(splitJob.BeginStreaming(), Error::None,
            "split-prefix streaming job begins");
        RequireResult(splitJob.FeedSource(
                std::span<const std::uint8_t>(fixture.file).first(split), false),
            Error::None, "first split-prefix fragment is accepted");
        while (!splitJob.NeedsSource() &&
            splitJob.Progress() == JobProgress::Running)
        {
            (void)splitJob.Step({3u, 1u});
        }
        Require(splitJob.Progress() == JobProgress::Running &&
                splitJob.NeedsSource(),
            "every prefix boundary pauses cleanly for its continuation");
        RequireResult(splitJob.FeedSource(
                std::span<const std::uint8_t>(fixture.file).subspan(split), true),
            Error::None, "remaining split-prefix source is accepted as final");
        while (splitJob.Progress() == JobProgress::Running)
        {
            (void)splitJob.Step({17u, 2u});
        }
        ExtractedWorldSurface splitOutput;
        Require(splitJob.Progress() == JobProgress::Succeeded &&
                splitJob.TakeResult(splitOutput) &&
                SameExtracted(splitOutput, expected),
            "prefix partition does not alter extraction output");
    }
}

void TestStreamingSourceFailures()
{
    const SyntheticFastfile fixture;
    WorldSurfaceExtractionJob idle;
    RequireResult(idle.FeedSource({}, true), Error::SourceNotReady,
        "idle job refuses source feeds");

    Limits smallChunks;
    smallChunks.maxSourceChunkBytes = 8u;
    WorldSurfaceExtractionJob pressure;
    RequireResult(pressure.BeginStreaming(smallChunks), Error::None,
        "backpressure test stream begins");
    const std::array<std::uint8_t, 4> first = {'I', 'W', 'f', 'f'};
    RequireResult(pressure.FeedSource(first, false), Error::None,
        "initial source fragment is accepted");
    const std::uint64_t receivedBefore = pressure.SourceBytesReceived();
    const std::uint32_t feedsBefore = pressure.SourceFeedCount();
    RequireResult(pressure.FeedSource(first, false), Error::SourceBackpressure,
        "unread source chunk applies backpressure");
    Require(pressure.SourceBytesReceived() == receivedBefore &&
            pressure.SourceFeedCount() == feedsBefore,
        "backpressure failure is source-state atomic");
    while (!pressure.NeedsSource())
    {
        (void)pressure.Step({1u, 1u});
    }
    const std::array<std::uint8_t, 9> oversized{};
    RequireResult(pressure.FeedSource(oversized, false),
        Error::SourceChunkTooLarge,
        "source chunk limit is enforced before allocation");
    Require(pressure.SourceBytesReceived() == receivedBefore &&
            pressure.SourceFeedCount() == feedsBefore,
        "oversized feed failure is source-state atomic");

    WorldSurfaceExtractionJob finalOnce;
    RequireResult(finalOnce.BeginStreaming(), Error::None,
        "final-marker test stream begins");
    RequireResult(finalOnce.FeedSource(
            std::span<const std::uint8_t>(fixture.file).first(PREFIX_SIZE), true),
        Error::None, "first final marker is accepted");
    RequireResult(finalOnce.FeedSource({}, true), Error::SourceAlreadyFinal,
        "a second final marker is rejected atomically");

    Limits exactPrefixLimit;
    exactPrefixLimit.maxFileBytes = PREFIX_SIZE;
    exactPrefixLimit.maxSourceChunkBytes = PREFIX_SIZE;
    WorldSurfaceExtractionJob totalLimit;
    RequireResult(totalLimit.BeginStreaming(exactPrefixLimit), Error::None,
        "total source-limit stream begins");
    RequireResult(totalLimit.FeedSource(
            std::span<const std::uint8_t>(fixture.file).first(PREFIX_SIZE), false),
        Error::None, "exact total source limit is accepted");
    (void)totalLimit.Step();
    Require(totalLimit.NeedsSource(),
        "exact prefix without payload remains resumably starved");
    const std::array<std::uint8_t, 1> oneByte = {0u};
    RequireResult(totalLimit.FeedSource(oneByte, true), Error::FileTooLarge,
        "source total limit rejects the first excess byte");

    const auto requireStreamingFailure = [](
        std::span<const std::uint8_t> bytes,
        Error expected,
        std::string_view context)
    {
        WorldSurfaceExtractionJob failed;
        RequireResult(failed.BeginStreaming(), Error::None,
            "malformed streaming job begins");
        RequireResult(failed.FeedSource(bytes, true), Error::None,
            "malformed final source is accepted by the source seam");
        std::size_t guard = 0u;
        while (failed.Progress() == JobProgress::Running)
        {
            (void)failed.Step({31u, 2u});
            Require(++guard < (1u << 20u),
                "malformed streaming job terminates within its guard");
        }
        RequireResult(failed.Failure(), expected, context);
    };

    requireStreamingFailure(
        std::span<const std::uint8_t>(fixture.file).first(PREFIX_SIZE - 1u),
        Error::PrefixTruncated,
        "final marker before a complete prefix is deterministic");
    requireStreamingFailure(
        std::span<const std::uint8_t>(fixture.file).first(fixture.file.size() - 1u),
        Error::InflateTruncated,
        "final marker before a complete zlib stream is deterministic");
    std::vector<std::uint8_t> trailing = fixture.file;
    trailing.push_back(0x5au);
    requireStreamingFailure(trailing, Error::InflateTrailingData,
        "bytes after streamed zlib completion are rejected");
}

void TestResultStrings()
{
    const std::array<Error, 56> errors = {
        Error::None,
        Error::InvalidArgument,
        Error::InvalidStepBudget,
        Error::SourceChunkTooLarge,
        Error::SourceBackpressure,
        Error::SourceAlreadyFinal,
        Error::SourceNotReady,
        Error::FileTooLarge,
        Error::PrefixTruncated,
        Error::InvalidMagic,
        Error::AuthenticatedUnsupported,
        Error::UnsupportedVersion,
        Error::InflateInit,
        Error::InflateData,
        Error::InflateTruncated,
        Error::InflateTrailingData,
        Error::InflatedSizeLimit,
        Error::RecordTruncated,
        Error::ExternalDataUnsupported,
        Error::BlockSizeLimit,
        Error::TotalBlockSizeLimit,
        Error::UnsupportedBlock,
        Error::ScriptStringsUnsupported,
        Error::AssetCountUnsupported,
        Error::AssetCountLimit,
        Error::MissingAssetArray,
        Error::AssetTypeUnsupported,
        Error::AssetReferenceUnsupported,
        Error::UnsupportedWorldField,
        Error::InvalidWorldCount,
        Error::WorldCountLimit,
        Error::MissingWorldArray,
        Error::MaterialMemoryUnsupported,
        Error::MaterialReferenceUnsupported,
        Error::MaterialLayoutUnsupported,
        Error::MaterialNameTruncated,
        Error::MaterialNameTooLong,
        Error::StringBytesLimit,
        Error::MaterialNameInvalid,
        Error::StackDepthLimit,
        Error::DelayedSpanLimit,
        Error::DelayedByteLimit,
        Error::AliasLimit,
        Error::MaterialAliasUndefined,
        Error::MaterialAliasDuplicate,
        Error::ZoneBlockOverflow,
        Error::ZoneBlockSizeMismatch,
        Error::LayeredSurfaceUnsupported,
        Error::EmptySurface,
        Error::SurfaceOutputTooLarge,
        Error::SurfaceRangeInvalid,
        Error::MaterialAliasInvalid,
        Error::NonFiniteBounds,
        Error::InvalidBounds,
        Error::NonFiniteVertex,
        Error::IndexOutOfRange,
        // TrailingData and AllocationFailed are checked below to keep the
        // explicit array size compiler-verified if the enum changes.
    };
    for (Error error : errors)
    {
        Require(std::strlen(kisak::fastfile::ErrorString(error)) > 0u,
            "every fastfile parser result has a printable string");
    }
    Require(std::strlen(kisak::fastfile::ErrorString(Error::TrailingData)) > 0u,
        "trailing-data result is printable");
    Require(std::strlen(kisak::fastfile::ErrorString(Error::AllocationFailed)) > 0u,
        "allocation result is printable");
    Require(std::string_view(kisak::fastfile::ErrorString(
            static_cast<Error>(0xffffffffu))) ==
            "unknown fastfile world-surface error",
        "unknown parser result has a stable fallback");
    Require(std::string_view(kisak::fastfile::MaterialReferenceKindString(
            kisak::fastfile::MaterialReferenceKind::AliasToInlineShared)) ==
            "alias-to-inline-shared",
        "material reference kind is printable");
    Require(std::string_view(kisak::fastfile::MaterialReferenceKindString(
            static_cast<kisak::fastfile::MaterialReferenceKind>(0xffu))) ==
            "unknown",
        "unknown material reference kind has a fallback");
    for (JobStage stage : {
            JobStage::NotStarted,
            JobStage::Inflate,
            JobStage::Traverse,
            JobStage::Complete,
            JobStage::Failed})
    {
        Require(std::strlen(kisak::fastfile::JobStageString(stage)) > 0u,
            "every incremental job stage has a printable string");
    }
    Require(std::string_view(kisak::fastfile::JobStageString(
            static_cast<JobStage>(0xffu))) == "unknown",
        "unknown incremental job stage has a stable fallback");
}

class Runner
{
public:
    void Run(const char *name, const std::function<void()> &test)
    {
        try
        {
            test();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        }
        catch (const std::exception &error)
        {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
        catch (...)
        {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": unknown exception\n";
        }
    }

    int Result() const
    {
        std::cout << passed_ << " passed, " << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

private:
    int passed_ = 0;
    int failed_ = 0;
};
} // namespace

int main()
{
    Runner runner;
    runner.Run("golden extraction and conversion", TestGoldenExtractionAndConversion);
    runner.Run("prefix and version framing", TestPrefixAndVersionFraming);
    runner.Run("zlib envelope and inflated limits", TestZlibEnvelopeAndInflatedLimits);
    runner.Run("record truncation", TestRecordTruncation);
    runner.Run("block limits and alignment", TestBlockLimitsAndAlignment);
    runner.Run("envelope and dependency rejections", TestEnvelopeAndDependencyRejections);
    runner.Run("world counts and limits", TestWorldCountsAndLimits);
    runner.Run("material names and aliases", TestMaterialNamesAndAliases);
    runner.Run("surface ranges and layering", TestSurfaceRangesAndUnsupportedLayering);
    runner.Run("bounds validation", TestBoundsValidation);
    runner.Run("vertex and index validation", TestVertexAndIndexValidation);
    runner.Run("atomicity and source independence", TestAtomicityAndSourceIndependence);
    runner.Run("incremental budgets and ownership", TestIncrementalBudgetsAndOwnership);
    runner.Run("incremental failure equivalence",
        TestIncrementalBudgetFailuresAndErrorEquivalence);
    runner.Run("incremental inflate drain and block-zero high-water",
        TestIncrementalInflateDrainAndBlockZeroHighWater);
    runner.Run("resumable source streaming", TestResumableSourceStreaming);
    runner.Run("streaming source failures", TestStreamingSourceFailures);
    runner.Run("result strings", TestResultStrings);
    return runner.Result();
}
