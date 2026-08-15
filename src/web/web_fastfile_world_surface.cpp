#include <web/web_fastfile_world_surface.h>
#include <web/web_fastfile_source_stream.h>
#include <web/web_fastfile_zone_registry.h>
#include <web/web_fastfile_zone_stream.h>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace kisak::fastfile
{
namespace
{
constexpr std::array<std::uint8_t, 8> UNSIGNED_MAGIC = {
    'I', 'W', 'f', 'f', 'u', '1', '0', '0',
};
constexpr std::array<std::uint8_t, 8> AUTHENTICATED_MAGIC = {
    'I', 'W', 'f', 'f', '0', '1', '0', '0',
};
constexpr std::uint32_t PREFIX_SIZE = 12u;
constexpr std::uint32_t XFILE_SIZE = 44u;
constexpr std::uint32_t XASSET_LIST_SIZE = 16u;
constexpr std::uint32_t XASSET_SIZE = 8u;
constexpr std::uint32_t MATERIAL_MEMORY_SIZE = 8u;
constexpr std::uint32_t ASSET_TYPE_MATERIAL = 0x04u;
constexpr std::uint32_t ASSET_TYPE_GFXWORLD = 0x10u;
constexpr std::uint32_t INLINE_POINTER = 0xffffffffu;
constexpr std::uint32_t INLINE_SHARED_POINTER = 0xfffffffeu;
constexpr std::uint32_t POINTER_OFFSET_MASK = 0x0fffffffu;

constexpr std::uint32_t WORLD_INDEX_COUNT_OFFSET = 0x10u;
constexpr std::uint32_t WORLD_INDICES_OFFSET = 0x14u;
constexpr std::uint32_t WORLD_SURFACE_COUNT_OFFSET = 0x18u;
constexpr std::uint32_t WORLD_VERTEX_COUNT_OFFSET = 0x30u;
constexpr std::uint32_t WORLD_VERTICES_OFFSET = 0x34u;
constexpr std::uint32_t WORLD_MATERIAL_MEMORY_COUNT_OFFSET = 0x174u;
constexpr std::uint32_t WORLD_MATERIAL_MEMORY_OFFSET = 0x178u;
constexpr std::uint32_t WORLD_STATIC_SURFACE_COUNT_OFFSET = 0x248u;
constexpr std::uint32_t WORLD_SURFACES_OFFSET = 0x294u;

std::uint16_t ReadU16(const std::uint8_t *bytes) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[0]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[1]) << 8u));
}

std::uint32_t ReadU32(const std::uint8_t *bytes) noexcept
{
    return static_cast<std::uint32_t>(bytes[0]) |
        static_cast<std::uint32_t>(bytes[1]) << 8u |
        static_cast<std::uint32_t>(bytes[2]) << 16u |
        static_cast<std::uint32_t>(bytes[3]) << 24u;
}

std::int32_t ReadS32(const std::uint8_t *bytes) noexcept
{
    return std::bit_cast<std::int32_t>(ReadU32(bytes));
}

float ReadF32(const std::uint8_t *bytes) noexcept
{
    return std::bit_cast<float>(ReadU32(bytes));
}

bool Matches(
    std::span<const std::uint8_t> bytes,
    const std::array<std::uint8_t, 8> &expected) noexcept
{
    return bytes.size() >= expected.size() &&
        std::equal(expected.begin(), expected.end(), bytes.begin());
}

static_assert(ZONE_BLOCK_COUNT == ZONE_STREAM_BLOCK_COUNT);

Error MapZoneStreamError(ZoneStreamError error) noexcept
{
    switch (error)
    {
    case ZoneStreamError::None: return Error::None;
    case ZoneStreamError::ArenaSizeLimit: return Error::TotalBlockSizeLimit;
    case ZoneStreamError::BlockOverflow: return Error::ZoneBlockOverflow;
    case ZoneStreamError::StackDepthLimit: return Error::StackDepthLimit;
    case ZoneStreamError::DelayedSpanLimit: return Error::DelayedSpanLimit;
    case ZoneStreamError::DelayedByteLimit: return Error::DelayedByteLimit;
    case ZoneStreamError::AllocationFailed: return Error::AllocationFailed;
    case ZoneStreamError::BlockSizeMismatch:
        return Error::ZoneBlockSizeMismatch;
    case ZoneStreamError::NotInitialized:
    case ZoneStreamError::InvalidArgument:
    case ZoneStreamError::StackUnderflow:
    case ZoneStreamError::ReplayState:
    case ZoneStreamError::DelayedConsumeInvalid:
    case ZoneStreamError::UnbalancedStack:
    case ZoneStreamError::DelayedReplayIncomplete:
        return Error::InvalidArgument;
    }
    return Error::InvalidArgument;
}

Error MapSourceStreamError(SourceStreamError error) noexcept
{
    switch (error)
    {
    case SourceStreamError::None: return Error::None;
    case SourceStreamError::ChunkTooLarge: return Error::SourceChunkTooLarge;
    case SourceStreamError::TotalSizeLimit: return Error::FileTooLarge;
    case SourceStreamError::Backpressure: return Error::SourceBackpressure;
    case SourceStreamError::AlreadyFinal: return Error::SourceAlreadyFinal;
    case SourceStreamError::AllocationFailed: return Error::AllocationFailed;
    case SourceStreamError::NotInitialized:
    case SourceStreamError::InvalidArgument:
    case SourceStreamError::ConsumeInvalid:
        return Error::InvalidArgument;
    }
    return Error::InvalidArgument;
}

Error MapZoneRegistryError(ZoneRegistryError error) noexcept
{
    switch (error)
    {
    case ZoneRegistryError::None: return Error::None;
    case ZoneRegistryError::AssetLimit: return Error::AssetCountLimit;
    case ZoneRegistryError::AliasLimit: return Error::AliasLimit;
    case ZoneRegistryError::NameBytesLimit: return Error::StringBytesLimit;
    case ZoneRegistryError::AliasUndefined: return Error::MaterialAliasUndefined;
    case ZoneRegistryError::AliasDuplicate: return Error::MaterialAliasDuplicate;
    case ZoneRegistryError::AliasInvalid:
    case ZoneRegistryError::AssetUndefined:
    case ZoneRegistryError::AssetTypeMismatch:
        return Error::MaterialAliasInvalid;
    case ZoneRegistryError::AllocationFailed: return Error::AllocationFailed;
    case ZoneRegistryError::NotInitialized:
    case ZoneRegistryError::InvalidArgument:
    case ZoneRegistryError::AssetDuplicate:
        return Error::InvalidArgument;
    }
    return Error::InvalidArgument;
}

bool ByteIsInRange(
    std::uint32_t byte,
    std::uint32_t begin,
    std::uint32_t size) noexcept
{
    return byte >= begin && byte - begin < size;
}

bool WorldByteIsSupported(std::uint32_t byte) noexcept
{
    return ByteIsInRange(byte, WORLD_INDEX_COUNT_OFFSET, 8u) ||
        ByteIsInRange(byte, WORLD_SURFACE_COUNT_OFFSET, 4u) ||
        ByteIsInRange(byte, WORLD_VERTEX_COUNT_OFFSET, 8u) ||
        ByteIsInRange(byte, WORLD_MATERIAL_MEMORY_COUNT_OFFSET, 8u) ||
        ByteIsInRange(byte, WORLD_STATIC_SURFACE_COUNT_OFFSET, 4u) ||
        ByteIsInRange(byte, WORLD_SURFACES_OFFSET, 4u);
}

bool VertexIsFinite(const WebEngineWorldVertex &vertex) noexcept
{
    for (float component : vertex.xyz)
    {
        if (!std::isfinite(component))
        {
            return false;
        }
    }
    if (!std::isfinite(vertex.binormalSign))
    {
        return false;
    }
    for (float component : vertex.textureCoordinate)
    {
        if (!std::isfinite(component))
        {
            return false;
        }
    }
    for (float component : vertex.lightmapCoordinate)
    {
        if (!std::isfinite(component))
        {
            return false;
        }
    }
    return true;
}

WebEngineWorldVertex DecodeVertex(const std::uint8_t *bytes) noexcept
{
    WebEngineWorldVertex vertex{};
    vertex.xyz[0] = ReadF32(bytes + 0u);
    vertex.xyz[1] = ReadF32(bytes + 4u);
    vertex.xyz[2] = ReadF32(bytes + 8u);
    vertex.binormalSign = ReadF32(bytes + 12u);
    vertex.color = ReadU32(bytes + 16u);
    vertex.textureCoordinate[0] = ReadF32(bytes + 20u);
    vertex.textureCoordinate[1] = ReadF32(bytes + 24u);
    vertex.lightmapCoordinate[0] = ReadF32(bytes + 28u);
    vertex.lightmapCoordinate[1] = ReadF32(bytes + 32u);
    vertex.normal = ReadU32(bytes + 36u);
    vertex.tangent = ReadU32(bytes + 40u);
    return vertex;
}

Error ValidateLimits(const Limits &limits) noexcept
{
    if (limits.maxFileBytes < PREFIX_SIZE ||
        limits.maxSourceChunkBytes == 0u ||
        limits.maxSourceChunkBytes > MAX_STEP_BYTES ||
        limits.maxInflatedBytes == 0u ||
        limits.maxBlockBytes == 0u || limits.maxTotalBlockBytes == 0u ||
        limits.maxAssets == 0u || limits.maxStackDepth == 0u ||
        limits.maxStackDepth > ZONE_STREAM_STACK_CAPACITY ||
        limits.maxDelayedSpans == 0u || limits.maxDelayedBytes == 0u ||
        limits.maxDelayedSpans > ZONE_STREAM_DELAYED_CAPACITY ||
        limits.maxTotalStringBytes == 0u || limits.maxAliases == 0u ||
        limits.maxWorldVertices == 0u || limits.maxWorldIndices == 0u ||
        limits.maxSelectedVertices == 0u || limits.maxSelectedIndices == 0u ||
        limits.maxMaterialNameBytes == 0u)
    {
        return Error::InvalidArgument;
    }
    return Error::None;
}

Error ValidatePrefix(std::span<const std::uint8_t> prefix) noexcept
{
    if ((prefix.data() == nullptr && !prefix.empty()) ||
        prefix.size() < PREFIX_SIZE)
    {
        return Error::PrefixTruncated;
    }
    const std::span<const std::uint8_t> magic = prefix.first(8u);
    if (Matches(magic, AUTHENTICATED_MAGIC))
    {
        return Error::AuthenticatedUnsupported;
    }
    if (!Matches(magic, UNSIGNED_MAGIC))
    {
        return Error::InvalidMagic;
    }
    if (ReadU32(prefix.data() + 8u) != VERSION)
    {
        return Error::UnsupportedVersion;
    }
    return Error::None;
}

Error ValidateFileEnvelope(
    std::span<const std::uint8_t> fileBytes,
    const Limits &limits) noexcept
{
    if ((fileBytes.data() == nullptr && !fileBytes.empty()))
    {
        return Error::InvalidArgument;
    }
    if (const Error error = ValidateLimits(limits); error != Error::None)
    {
        return error;
    }
    if (fileBytes.size() > limits.maxFileBytes ||
        fileBytes.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return Error::FileTooLarge;
    }
    if (fileBytes.size() < PREFIX_SIZE)
    {
        return Error::PrefixTruncated;
    }
    if (const Error error = ValidatePrefix(fileBytes.first(PREFIX_SIZE));
        error != Error::None)
    {
        return error;
    }
    if (fileBytes.size() == PREFIX_SIZE)
    {
        return Error::InflateTruncated;
    }
    return Error::None;
}

// Stable 64 KiB pages let zlib append one bounded window without reserving the
// complete configured ceiling or copying all prior output during vector growth.
// Traversal uses checked offsets and bridges page boundaries explicitly.
class ChunkedInflatedBytes
{
public:
    static constexpr std::size_t CHUNK_BYTES = MAX_STEP_BYTES;

    std::size_t size() const noexcept
    {
        return size_;
    }

    std::span<std::uint8_t> AppendWindow(std::size_t maximumBytes)
    {
        const std::size_t chunkIndex = size_ / CHUNK_BYTES;
        const std::size_t chunkOffset = size_ % CHUNK_BYTES;
        if (chunkIndex == chunks_.size())
        {
            chunks_.push_back(
                std::unique_ptr<std::uint8_t[]>(new std::uint8_t[CHUNK_BYTES]));
        }
        const std::size_t available = CHUNK_BYTES - chunkOffset;
        return {chunks_[chunkIndex].get() + chunkOffset,
            std::min(maximumBytes, available)};
    }

    void Commit(std::size_t produced) noexcept
    {
        size_ += produced;
    }

    std::uint8_t operator[](std::size_t offset) const noexcept
    {
        return chunks_[offset / CHUNK_BYTES][offset % CHUNK_BYTES];
    }

    void Copy(std::size_t offset, std::span<std::uint8_t> destination) const noexcept
    {
        std::size_t copied = 0u;
        while (copied < destination.size())
        {
            const std::size_t source = offset + copied;
            const std::size_t chunkOffset = source % CHUNK_BYTES;
            const std::size_t count = std::min(
                destination.size() - copied,
                CHUNK_BYTES - chunkOffset);
            std::memcpy(
                destination.data() + copied,
                chunks_[source / CHUNK_BYTES].get() + chunkOffset,
                count);
            copied += count;
        }
    }

    void Clear() noexcept
    {
        chunks_.clear();
        size_ = 0u;
    }

private:
    std::vector<std::unique_ptr<std::uint8_t[]>> chunks_;
    std::size_t size_ = 0u;
};
} // namespace

struct WorldSurfaceExtractionJob::Impl
{
    enum class Phase : std::uint8_t
    {
        Inflate,
        XFile,
        AssetList,
        AssetTable,
        Material,
        MaterialName,
        World,
        Indices,
        MaterialMemory,
        Vertices,
        Surface,
        SelectedVertices,
        SelectedIndices,
        Finish,
        Complete,
    };

    BoundedSourceStream source;
    std::vector<std::uint8_t> ownedSource;
    std::size_t ownedSourceOffset = 0u;
    std::array<std::uint8_t, PREFIX_SIZE> prefix{};
    std::uint32_t prefixBytes = 0u;
    ChunkedInflatedBytes inflated;
    std::array<std::uint8_t, GFX_WORLD_WIRE_SIZE> recordScratch{};
    std::array<std::uint8_t, GFX_WORLD_VERTEX_WIRE_SIZE> vertexScratch{};
    std::array<std::uint8_t, sizeof(std::uint16_t)> indexScratch{};
    Limits limits;
    z_stream stream{};
    bool streamInitialized = false;
    bool inflateStreamEnded = false;
    Phase phase = Phase::Inflate;
    std::size_t readerPosition = 0u;
    std::size_t recordStart = 0u;
    std::size_t recordSize = 0u;
    std::size_t recordVisited = 0u;
    bool recordPrepared = false;
    bool arenaPrepared = false;
    bool materialArenaPrepared = false;
    ZoneStreamMachine arenas;
    ZoneAssetRegistry registry;
    ExtractedWorldSurface replacement;
    std::uint32_t declaredSize = 0u;
    std::uint32_t indexCount = 0u;
    std::uint32_t vertexCount = 0u;
    std::size_t serializedIndicesOffset = 0u;
    std::size_t serializedVerticesOffset = 0u;
    ZoneSpan materialAliasSlot{};
    std::uint32_t materialMemoryIdentity = 0u;
    std::uint32_t totalStringBytes = 0u;
    std::string materialNameScratch;
    SurfaceMetadata metadata;
    std::uint32_t selectedIndexCount = 0u;
    std::uint32_t selectedVertexCursor = 0u;
    std::uint32_t selectedIndexCursor = 0u;
    std::size_t selectionVisited = 0u;
    bool assetTableBlockScopeOpen = false;
    bool rootBlockScopeOpen = false;
    bool materialBlockScopeOpen = false;
    bool materialChildBlockScopeOpen = false;
    bool worldChildBlockScopeOpen = false;

    ~Impl()
    {
        EndInflate();
    }

    void EndInflate() noexcept
    {
        if (streamInitialized)
        {
            inflateEnd(&stream);
            streamInitialized = false;
            stream = {};
        }
    }

    Error BeginInflate() noexcept
    {
        stream = {};
        const int result = inflateInit(&stream);
        if (result != Z_OK)
        {
            return result == Z_MEM_ERROR
                ? Error::AllocationFailed
                : Error::InflateInit;
        }
        streamInitialized = true;
        return Error::None;
    }

    bool HasOwnedSourcePending() const noexcept
    {
        return ownedSourceOffset < ownedSource.size();
    }

    Error FeedOwnedSource() noexcept
    {
        if (!source.NeedsSource() || !HasOwnedSourcePending())
        {
            return Error::None;
        }
        const std::size_t remaining = ownedSource.size() - ownedSourceOffset;
        const std::size_t count = std::min<std::size_t>(
            remaining, limits.maxSourceChunkBytes);
        const bool final = count == remaining;
        const Error error = MapSourceStreamError(source.Feed(
            std::span<const std::uint8_t>(ownedSource)
                .subspan(ownedSourceOffset, count),
            final));
        if (error == Error::None)
        {
            ownedSourceOffset += count;
        }
        return error;
    }

    Error PrepareRecord(std::uint64_t size, Error truncatedError) noexcept
    {
        if (recordPrepared)
        {
            return Error::None;
        }
        if (size > std::numeric_limits<std::size_t>::max() ||
            readerPosition > inflated.size() ||
            size > inflated.size() - readerPosition)
        {
            return truncatedError;
        }
        recordStart = readerPosition;
        recordSize = static_cast<std::size_t>(size);
        recordVisited = 0u;
        recordPrepared = true;
        return Error::None;
    }

    const std::uint8_t *RecordBytes() noexcept
    {
        return recordScratch.data();
    }

    enum class VisitResult : std::uint8_t
    {
        Pending,
        Complete,
        Error,
    };

    VisitResult VisitRecord(
        const StepBudget &budget,
        StepReport &report,
        Error &error) noexcept
    {
        if (!recordPrepared || report.recordsProcessedThisStep >= budget.maxRecords)
        {
            return VisitResult::Pending;
        }
        const std::uint32_t byteBudget =
            budget.maxBytes - report.traversedBytesThisStep;
        if (byteBudget == 0u)
        {
            return VisitResult::Pending;
        }
        const std::size_t count = std::min<std::size_t>(
            recordSize - recordVisited,
            byteBudget);
        const std::size_t visitOffset = recordVisited;
        const std::size_t begin = recordStart + visitOffset;
        const bool staged = recordSize <= recordScratch.size();
        if (staged)
        {
            inflated.Copy(
                begin,
                std::span<std::uint8_t>(recordScratch)
                    .subspan(visitOffset, count));
        }
        recordVisited += count;
        report.traversedBytesThisStep += static_cast<std::uint32_t>(count);
        if (phase == Phase::World)
        {
            for (std::size_t offset = 0u; offset < count; ++offset)
            {
                const std::uint32_t fieldByte = static_cast<std::uint32_t>(
                    visitOffset + offset);
                if (!WorldByteIsSupported(fieldByte) &&
                    recordScratch[visitOffset + offset] != 0u)
                {
                    error = Error::UnsupportedWorldField;
                    return VisitResult::Error;
                }
            }
        }
        else if (phase == Phase::Material)
        {
            for (std::size_t offset = 0u; offset < count; ++offset)
            {
                if (visitOffset + offset >= 4u &&
                    recordScratch[visitOffset + offset] != 0u)
                {
                    error = Error::MaterialLayoutUnsupported;
                    return VisitResult::Error;
                }
            }
        }
        if (recordVisited != recordSize)
        {
            return VisitResult::Pending;
        }
        readerPosition += recordSize;
        recordPrepared = false;
        recordVisited = 0u;
        ++report.recordsProcessedThisStep;
        return VisitResult::Complete;
    }

    Error AllocateForRecord(
        std::uint32_t block,
        std::uint32_t alignment,
        std::uint64_t size,
        std::uint32_t *offset = nullptr) noexcept
    {
        if (arenas.ActiveBlock() != block)
        {
            return Error::InvalidArgument;
        }
        ZoneLoadPlan plan;
        const Error error = MapZoneStreamError(
            arenas.PlanLoad(alignment, size, plan));
        if (error != Error::None)
        {
            return error;
        }
        if (plan.kind != ZoneLoadKind::Immediate || plan.span.block != block)
        {
            return Error::UnsupportedBlock;
        }
        if (offset)
        {
            *offset = plan.span.offset;
        }
        return Error::None;
    }

    Error ReserveArena(
        std::uint32_t block,
        std::uint32_t alignment,
        std::uint64_t size,
        std::uint32_t *offset = nullptr) noexcept
    {
        if (arenas.ActiveBlock() != block)
        {
            return Error::InvalidArgument;
        }
        ZoneSpan span;
        const Error error = MapZoneStreamError(
            arenas.Reserve(alignment, size, span));
        if (error == Error::None && offset)
        {
            *offset = span.offset;
        }
        return error;
    }

    Error StepInflate(const StepBudget &budget, StepReport &report) noexcept
    {
        if (const Error error = FeedOwnedSource(); error != Error::None)
        {
            return error;
        }

        while (prefixBytes < PREFIX_SIZE)
        {
            const std::uint32_t sourceBudget =
                budget.maxBytes - report.sourceBytesConsumedThisStep;
            if (sourceBudget == 0u)
            {
                return Error::None;
            }
            const std::uint32_t needed = PREFIX_SIZE - prefixBytes;
            const std::span<const std::uint8_t> input = source.Peek(
                std::min(sourceBudget, needed));
            if (input.empty())
            {
                if (source.FinalReceived())
                {
                    return Error::PrefixTruncated;
                }
                report.needsSource = !HasOwnedSourcePending();
                return Error::None;
            }
            std::memcpy(prefix.data() + prefixBytes, input.data(), input.size());
            const std::uint32_t consumed =
                static_cast<std::uint32_t>(input.size());
            if (const Error error = MapSourceStreamError(source.Consume(consumed));
                error != Error::None)
            {
                return error;
            }
            prefixBytes += consumed;
            report.sourceBytesConsumedThisStep += consumed;
        }

        if (!streamInitialized && !inflateStreamEnded)
        {
            if (const Error error = ValidatePrefix(prefix); error != Error::None)
            {
                return error;
            }
            if (const Error error = BeginInflate(); error != Error::None)
            {
                return error;
            }
        }

        if (inflateStreamEnded)
        {
            if (source.AvailableBytes() != 0u)
            {
                return Error::InflateTrailingData;
            }
            if (source.FinalReceived())
            {
                phase = Phase::XFile;
            }
            else
            {
                report.needsSource = !HasOwnedSourcePending();
            }
            return Error::None;
        }

        const std::uint32_t sourceBudget =
            budget.maxBytes - report.sourceBytesConsumedThisStep;
        if (sourceBudget == 0u)
        {
            return Error::None;
        }
        const std::span<const std::uint8_t> input = source.Peek(sourceBudget);
        if (input.empty() && !source.FinalReceived())
        {
            report.needsSource = !HasOwnedSourcePending();
            return Error::None;
        }
        const std::size_t inputCapacity = std::min<std::size_t>(
            sourceBudget,
            input.size());
        const bool atOutputCeiling =
            inflated.size() == limits.maxInflatedBytes;
        std::array<std::uint8_t, 1> overflowGuard{};
        std::span<std::uint8_t> output;
        if (atOutputCeiling)
        {
            // zlib can still need arbitrarily many output-free control/trailer
            // bytes after producing exactly the allowed output. A one-byte
            // guard distinguishes that valid drain from an attempted byte
            // beyond the configured ceiling.
            output = overflowGuard;
        }
        else
        {
            const std::size_t requestedOutputCapacity = std::min<std::size_t>(
                budget.maxBytes,
                limits.maxInflatedBytes - inflated.size());
            output = inflated.AppendWindow(requestedOutputCapacity);
        }
        const std::size_t outputCapacity = output.size();

        stream.next_in = inputCapacity == 0u
            ? Z_NULL
            : const_cast<Bytef *>(reinterpret_cast<const Bytef *>(
                input.data()));
        stream.avail_in = static_cast<uInt>(inputCapacity);
        stream.next_out = reinterpret_cast<Bytef *>(output.data());
        stream.avail_out = static_cast<uInt>(outputCapacity);
        const uInt inputBefore = stream.avail_in;
        const uInt outputBefore = stream.avail_out;
        const int result = inflate(&stream, Z_NO_FLUSH);
        const std::uint32_t consumed = inputBefore - stream.avail_in;
        const std::uint32_t produced = outputBefore - stream.avail_out;
        if (consumed != 0u)
        {
            if (const Error error = MapSourceStreamError(source.Consume(consumed));
                error != Error::None)
            {
                return error;
            }
        }
        report.compressedBytesConsumedThisStep = consumed;
        report.sourceBytesConsumedThisStep += consumed;
        if (atOutputCeiling)
        {
            if (produced != 0u)
            {
                return Error::InflatedSizeLimit;
            }
        }
        else
        {
            inflated.Commit(produced);
            report.inflatedBytesProducedThisStep = produced;
        }

        if (result == Z_STREAM_END)
        {
            EndInflate();
            inflateStreamEnded = true;
            if (source.AvailableBytes() != 0u)
            {
                return Error::InflateTrailingData;
            }
            if (source.FinalReceived())
            {
                phase = Phase::XFile;
            }
            else
            {
                report.needsSource = !HasOwnedSourcePending();
            }
            return Error::None;
        }
        if (result == Z_MEM_ERROR)
        {
            return Error::AllocationFailed;
        }
        if (result != Z_OK)
        {
            if (result == Z_BUF_ERROR && source.AvailableBytes() == 0u)
            {
                if (source.FinalReceived())
                {
                    return Error::InflateTruncated;
                }
                report.needsSource = !HasOwnedSourcePending();
                return Error::None;
            }
            return Error::InflateData;
        }
        if (consumed == 0u && produced == 0u)
        {
            if (source.AvailableBytes() == 0u)
            {
                if (source.FinalReceived())
                {
                    return Error::InflateTruncated;
                }
                report.needsSource = !HasOwnedSourcePending();
                return Error::None;
            }
            return Error::InflateData;
        }
        return Error::None;
    }

    Error StepTraverse(const StepBudget &budget, StepReport &report);
};

Error WorldSurfaceExtractionJob::Impl::StepTraverse(
    const StepBudget &budget,
    StepReport &report)
{
    while (report.traversedBytesThisStep < budget.maxBytes &&
        report.recordsProcessedThisStep < budget.maxRecords)
    {
        Error error = Error::None;
        switch (phase)
        {
        case Phase::Inflate:
            return Error::InvalidArgument;

        case Phase::XFile:
        {
            error = PrepareRecord(XFILE_SIZE, Error::RecordTruncated);
            if (error != Error::None)
            {
                return error;
            }
            const VisitResult visited = VisitRecord(budget, report, error);
            if (visited == VisitResult::Error)
            {
                return error;
            }
            if (visited == VisitResult::Pending)
            {
                return Error::None;
            }
            const std::uint8_t *xfile = RecordBytes();
            declaredSize = ReadU32(xfile);
            if (ReadU32(xfile + 4u) != 0u)
            {
                return Error::ExternalDataUnsupported;
            }
            std::uint64_t totalBlockBytes = 0u;
            std::array<std::uint32_t, ZONE_STREAM_BLOCK_COUNT> blockSizes{};
            for (std::uint32_t block = 0u; block < ZONE_BLOCK_COUNT; ++block)
            {
                const std::uint32_t size = ReadU32(xfile + 8u + block * 4u);
                if (size > limits.maxBlockBytes ||
                    size > static_cast<std::uint64_t>(POINTER_OFFSET_MASK) + 1u)
                {
                    return Error::BlockSizeLimit;
                }
                if (block != 0u && block != 4u && size != 0u)
                {
                    return Error::UnsupportedBlock;
                }
                totalBlockBytes += size;
                if (totalBlockBytes > limits.maxTotalBlockBytes)
                {
                    return Error::TotalBlockSizeLimit;
                }
                blockSizes[block] = size;
                replacement.blockSizes[block] = size;
            }
            if (declaredSize > limits.maxTotalBlockBytes)
            {
                return Error::TotalBlockSizeLimit;
            }
            const ZoneStreamLimits streamLimits{
                limits.maxTotalBlockBytes,
                limits.maxStackDepth,
                limits.maxDelayedSpans,
                limits.maxDelayedBytes,
            };
            error = MapZoneStreamError(
                arenas.Initialize(blockSizes, streamLimits));
            if (error != Error::None)
            {
                return error;
            }
            const ZoneRegistryLimits registryLimits{
                limits.maxAssets,
                limits.maxAliases,
                limits.maxTotalStringBytes,
            };
            error = MapZoneRegistryError(
                registry.Initialize(blockSizes, registryLimits));
            if (error != Error::None)
            {
                return error;
            }
            phase = Phase::AssetList;
            break;
        }

        case Phase::AssetList:
        {
            error = PrepareRecord(XASSET_LIST_SIZE, Error::RecordTruncated);
            if (error != Error::None)
            {
                return error;
            }
            const VisitResult visited = VisitRecord(budget, report, error);
            if (visited == VisitResult::Pending)
            {
                return Error::None;
            }
            const std::uint8_t *assetList = RecordBytes();
            if (ReadS32(assetList) != 0 || ReadU32(assetList + 4u) != 0u)
            {
                return Error::ScriptStringsUnsupported;
            }
            const std::int32_t assetCount = ReadS32(assetList + 8u);
            if (assetCount > 0 &&
                static_cast<std::uint32_t>(assetCount) > limits.maxAssets)
            {
                return Error::AssetCountLimit;
            }
            if (assetCount != 2)
            {
                return Error::AssetCountUnsupported;
            }
            if (ReadU32(assetList + 12u) == 0u)
            {
                return Error::MissingAssetArray;
            }
            replacement.sourceAssetCount = 2u;
            replacement.materialAssetIndex = 0u;
            replacement.worldAssetIndex = 1u;
            error = MapZoneStreamError(arenas.Push(4u));
            if (error != Error::None)
            {
                return error;
            }
            assetTableBlockScopeOpen = true;
            phase = Phase::AssetTable;
            break;
        }

        case Phase::AssetTable:
        {
            if (!arenaPrepared)
            {
                error = AllocateForRecord(4u, 4u, 2u * XASSET_SIZE);
                if (error != Error::None)
                {
                    return error;
                }
                arenaPrepared = true;
            }
            error = PrepareRecord(2u * XASSET_SIZE, Error::RecordTruncated);
            if (error != Error::None)
            {
                return error;
            }
            const VisitResult visited = VisitRecord(budget, report, error);
            if (visited == VisitResult::Pending)
            {
                return Error::None;
            }
            arenaPrepared = false;
            const std::uint8_t *assets = RecordBytes();
            if (ReadU32(assets) == ASSET_TYPE_GFXWORLD &&
                ReadU32(assets + XASSET_SIZE) == ASSET_TYPE_MATERIAL)
            {
                // A later material cannot define the alias needed by a world
                // dispatched first. Reject before consuming either body.
                return Error::MaterialAliasUndefined;
            }
            if (ReadU32(assets) != ASSET_TYPE_MATERIAL ||
                ReadU32(assets + XASSET_SIZE) != ASSET_TYPE_GFXWORLD)
            {
                return Error::AssetTypeUnsupported;
            }
            if (ReadU32(assets + 4u) != INLINE_SHARED_POINTER ||
                ReadU32(assets + XASSET_SIZE + 4u) != INLINE_POINTER)
            {
                return Error::AssetReferenceUnsupported;
            }
            phase = Phase::Material;
            break;
        }

        case Phase::World:
        {
            if (!arenaPrepared)
            {
                error = MapZoneStreamError(arenas.Push(0u));
                if (error != Error::None)
                {
                    return error;
                }
                rootBlockScopeOpen = true;
                error = AllocateForRecord(0u, 4u, GFX_WORLD_WIRE_SIZE);
                if (error != Error::None)
                {
                    return error;
                }
                arenaPrepared = true;
            }
            error = PrepareRecord(GFX_WORLD_WIRE_SIZE, Error::RecordTruncated);
            if (error != Error::None)
            {
                return error;
            }
            const VisitResult visited = VisitRecord(budget, report, error);
            if (visited == VisitResult::Error)
            {
                return error;
            }
            if (visited == VisitResult::Pending)
            {
                return Error::None;
            }
            arenaPrepared = false;
            const std::uint8_t *world = RecordBytes();
            const std::int32_t signedIndexCount =
                ReadS32(world + WORLD_INDEX_COUNT_OFFSET);
            const std::int32_t signedSurfaceCount =
                ReadS32(world + WORLD_SURFACE_COUNT_OFFSET);
            vertexCount = ReadU32(world + WORLD_VERTEX_COUNT_OFFSET);
            const std::int32_t materialMemoryCount =
                ReadS32(world + WORLD_MATERIAL_MEMORY_COUNT_OFFSET);
            const std::uint32_t staticSurfaceCount =
                ReadU32(world + WORLD_STATIC_SURFACE_COUNT_OFFSET);
            if (signedIndexCount <= 0 || signedSurfaceCount != 1 ||
                vertexCount == 0u || materialMemoryCount != 1 ||
                staticSurfaceCount != 1u)
            {
                return Error::InvalidWorldCount;
            }
            indexCount = static_cast<std::uint32_t>(signedIndexCount);
            if (indexCount > limits.maxWorldIndices ||
                vertexCount > limits.maxWorldVertices)
            {
                return Error::WorldCountLimit;
            }
            if (ReadU32(world + WORLD_INDICES_OFFSET) == 0u ||
                ReadU32(world + WORLD_VERTICES_OFFSET) == 0u ||
                ReadU32(world + WORLD_MATERIAL_MEMORY_OFFSET) == 0u ||
                ReadU32(world + WORLD_SURFACES_OFFSET) == 0u)
            {
                return Error::MissingWorldArray;
            }
            error = MapZoneStreamError(arenas.Push(4u));
            if (error != Error::None)
            {
                return error;
            }
            worldChildBlockScopeOpen = true;
            phase = Phase::Indices;
            break;
        }

        case Phase::Indices:
        {
            const std::uint64_t size = static_cast<std::uint64_t>(indexCount) * 2u;
            if (!arenaPrepared)
            {
                error = AllocateForRecord(4u, 2u, size);
                if (error != Error::None)
                {
                    return error;
                }
                serializedIndicesOffset = readerPosition;
                arenaPrepared = true;
            }
            error = PrepareRecord(size, Error::RecordTruncated);
            if (error != Error::None)
            {
                return error;
            }
            const VisitResult visited = VisitRecord(budget, report, error);
            if (visited == VisitResult::Pending)
            {
                return Error::None;
            }
            arenaPrepared = false;
            phase = Phase::MaterialMemory;
            break;
        }

        case Phase::MaterialMemory:
        {
            if (!arenaPrepared)
            {
                error = AllocateForRecord(4u, 4u, MATERIAL_MEMORY_SIZE);
                if (error != Error::None)
                {
                    return error;
                }
                arenaPrepared = true;
            }
            error = PrepareRecord(MATERIAL_MEMORY_SIZE, Error::RecordTruncated);
            if (error != Error::None)
            {
                return error;
            }
            const VisitResult visited = VisitRecord(budget, report, error);
            if (visited == VisitResult::Pending)
            {
                return Error::None;
            }
            arenaPrepared = false;
            const std::uint8_t *memory = RecordBytes();
            if (ReadS32(memory + 4u) < 0)
            {
                return Error::MaterialMemoryUnsupported;
            }
            const std::uint32_t materialAlias = ReadU32(memory);
            std::uint32_t resolvedIdentity = 0u;
            error = MapZoneRegistryError(registry.ResolveAlias(
                materialAlias, ASSET_TYPE_MATERIAL, resolvedIdentity));
            if (error != Error::None)
            {
                return error;
            }
            materialMemoryIdentity = resolvedIdentity;
            phase = Phase::Vertices;
            break;
        }

        case Phase::Material:
        {
            if (!materialArenaPrepared)
            {
                if (limits.maxAliases < 1u)
                {
                    return Error::AliasLimit;
                }
                error = MapZoneStreamError(arenas.Push(0u));
                if (error != Error::None)
                {
                    return error;
                }
                materialBlockScopeOpen = true;
                error = AllocateForRecord(0u, 4u, MATERIAL_WIRE_SIZE);
                if (error != Error::None)
                {
                    return error;
                }
                error = MapZoneStreamError(arenas.Push(4u));
                if (error != Error::None)
                {
                    return error;
                }
                std::uint32_t aliasSlotOffset = 0u;
                error = ReserveArena(4u, 4u, 4u, &aliasSlotOffset);
                if (error != Error::None)
                {
                    return error;
                }
                materialAliasSlot = {4u, aliasSlotOffset, 4u};
                error = MapZoneRegistryError(registry.ReserveAlias(
                    materialAliasSlot, ASSET_TYPE_MATERIAL));
                if (error != Error::None)
                {
                    return error;
                }
                error = MapZoneStreamError(arenas.Pop());
                if (error != Error::None)
                {
                    return error;
                }
                materialArenaPrepared = true;
                ++report.recordsProcessedThisStep;
                if (report.recordsProcessedThisStep >= budget.maxRecords)
                {
                    return Error::None;
                }
            }
            error = PrepareRecord(MATERIAL_WIRE_SIZE, Error::RecordTruncated);
            if (error != Error::None)
            {
                return error;
            }
            const VisitResult visited = VisitRecord(budget, report, error);
            if (visited == VisitResult::Error)
            {
                return error;
            }
            if (visited == VisitResult::Pending)
            {
                return Error::None;
            }
            const std::uint8_t *material = RecordBytes();
            if (ReadU32(material) != INLINE_POINTER)
            {
                return Error::MaterialLayoutUnsupported;
            }
            error = MapZoneStreamError(arenas.Push(4u));
            if (error != Error::None)
            {
                return error;
            }
            materialChildBlockScopeOpen = true;
            phase = Phase::MaterialName;
            break;
        }

        case Phase::MaterialName:
        {
            if (readerPosition >= inflated.size())
            {
                return Error::MaterialNameTruncated;
            }
            const std::uint8_t character = inflated[readerPosition++];
            ++report.traversedBytesThisStep;
            ++report.recordsProcessedThisStep;
            if (totalStringBytes >= limits.maxTotalStringBytes)
            {
                return Error::StringBytesLimit;
            }
            ++totalStringBytes;
            error = AllocateForRecord(4u, 1u, 1u);
            if (error != Error::None)
            {
                return error;
            }
            if (character == 0u)
            {
                if (materialNameScratch.empty())
                {
                    return Error::MaterialNameInvalid;
                }
                error = MapZoneRegistryError(registry.RegisterAsset(
                    ASSET_TYPE_MATERIAL,
                    replacement.materialAssetIndex,
                    materialNameScratch,
                    replacement.materialIdentity));
                if (error != Error::None)
                {
                    return error;
                }
                error = MapZoneRegistryError(registry.PublishAlias(
                    materialAliasSlot, replacement.materialIdentity));
                if (error != Error::None)
                {
                    return error;
                }
                error = MapZoneStreamError(arenas.Pop());
                if (error != Error::None)
                {
                    return error;
                }
                materialChildBlockScopeOpen = false;
                error = MapZoneStreamError(arenas.Pop());
                if (error != Error::None)
                {
                    return error;
                }
                materialBlockScopeOpen = false;
                phase = Phase::World;
                break;
            }
            if (materialNameScratch.size() >= limits.maxMaterialNameBytes)
            {
                return Error::MaterialNameTooLong;
            }
            if (character < 0x20u || character > 0x7eu)
            {
                return Error::MaterialNameInvalid;
            }
            materialNameScratch.push_back(static_cast<char>(character));
            break;
        }

        case Phase::Vertices:
        {
            const std::uint64_t size =
                static_cast<std::uint64_t>(vertexCount) *
                GFX_WORLD_VERTEX_WIRE_SIZE;
            if (!arenaPrepared)
            {
                error = AllocateForRecord(4u, 4u, size);
                if (error != Error::None)
                {
                    return error;
                }
                serializedVerticesOffset = readerPosition;
                arenaPrepared = true;
            }
            error = PrepareRecord(size, Error::RecordTruncated);
            if (error != Error::None)
            {
                return error;
            }
            const VisitResult visited = VisitRecord(budget, report, error);
            if (visited == VisitResult::Pending)
            {
                return Error::None;
            }
            arenaPrepared = false;
            phase = Phase::Surface;
            break;
        }

        case Phase::Surface:
        {
            if (!arenaPrepared)
            {
                error = AllocateForRecord(4u, 4u, GFX_SURFACE_WIRE_SIZE);
                if (error != Error::None)
                {
                    return error;
                }
                arenaPrepared = true;
            }
            error = PrepareRecord(GFX_SURFACE_WIRE_SIZE, Error::RecordTruncated);
            if (error != Error::None)
            {
                return error;
            }
            const VisitResult visited = VisitRecord(budget, report, error);
            if (visited == VisitResult::Pending)
            {
                return Error::None;
            }
            arenaPrepared = false;
            const std::uint8_t *surface = RecordBytes();
            const std::int32_t vertexLayerData = ReadS32(surface);
            const std::int32_t firstVertexSigned = ReadS32(surface + 4u);
            const std::uint16_t selectedVertexCount = ReadU16(surface + 8u);
            const std::uint16_t triangleCount = ReadU16(surface + 10u);
            const std::int32_t baseIndexSigned = ReadS32(surface + 12u);
            if (vertexLayerData != 0)
            {
                return Error::LayeredSurfaceUnsupported;
            }
            if (selectedVertexCount == 0u || triangleCount == 0u)
            {
                return Error::EmptySurface;
            }
            selectedIndexCount = static_cast<std::uint32_t>(triangleCount) * 3u;
            if (selectedVertexCount > limits.maxSelectedVertices ||
                selectedIndexCount > limits.maxSelectedIndices ||
                selectedVertexCount > WEB_RENDERER_MAX_SURFACE_VERTICES ||
                selectedIndexCount > WEB_RENDERER_MAX_SURFACE_INDICES)
            {
                return Error::SurfaceOutputTooLarge;
            }
            if (firstVertexSigned < 0 || baseIndexSigned < 0)
            {
                return Error::SurfaceRangeInvalid;
            }
            const std::uint32_t firstVertex =
                static_cast<std::uint32_t>(firstVertexSigned);
            const std::uint32_t baseIndex =
                static_cast<std::uint32_t>(baseIndexSigned);
            if (firstVertex > vertexCount ||
                selectedVertexCount > vertexCount - firstVertex ||
                baseIndex > indexCount ||
                selectedIndexCount > indexCount - baseIndex)
            {
                return Error::SurfaceRangeInvalid;
            }

            const std::uint32_t materialAlias = ReadU32(surface + 16u);
            std::uint32_t resolvedIdentity = 0u;
            error = MapZoneRegistryError(registry.ResolveAlias(
                materialAlias, ASSET_TYPE_MATERIAL, resolvedIdentity));
            if (error != Error::None)
            {
                return error;
            }
            if (resolvedIdentity != replacement.materialIdentity ||
                materialMemoryIdentity != resolvedIdentity)
            {
                return Error::MaterialAliasInvalid;
            }

            metadata.sourceFirstVertex = firstVertex;
            metadata.sourceBaseIndex = baseIndex;
            metadata.vertexCount = selectedVertexCount;
            metadata.triangleCount = triangleCount;
            metadata.lightmapIndex = surface[20u];
            metadata.reflectionProbeIndex = surface[21u];
            metadata.primaryLightIndex = surface[22u];
            metadata.flags = surface[23u];
            for (std::uint32_t axis = 0u; axis < 3u; ++axis)
            {
                metadata.bounds[0][axis] = ReadF32(surface + 24u + axis * 4u);
                metadata.bounds[1][axis] = ReadF32(surface + 36u + axis * 4u);
                if (!std::isfinite(metadata.bounds[0][axis]) ||
                    !std::isfinite(metadata.bounds[1][axis]))
                {
                    return Error::NonFiniteBounds;
                }
                if (metadata.bounds[0][axis] > metadata.bounds[1][axis])
                {
                    return Error::InvalidBounds;
                }
            }
            replacement.vertices.reserve(selectedVertexCount);
            replacement.indices.reserve(selectedIndexCount);
            phase = Phase::SelectedVertices;
            break;
        }

        case Phase::SelectedVertices:
        {
            if (selectedVertexCursor >= metadata.vertexCount)
            {
                selectionVisited = 0u;
                phase = Phase::SelectedIndices;
                break;
            }
            const std::uint32_t available =
                budget.maxBytes - report.traversedBytesThisStep;
            const std::size_t sourceVertex =
                static_cast<std::size_t>(metadata.sourceFirstVertex) +
                selectedVertexCursor;
            const std::size_t offset = serializedVerticesOffset +
                sourceVertex * GFX_WORLD_VERTEX_WIRE_SIZE;
            const std::size_t count = std::min<std::size_t>(
                GFX_WORLD_VERTEX_WIRE_SIZE - selectionVisited,
                available);
            inflated.Copy(
                offset + selectionVisited,
                std::span<std::uint8_t>(vertexScratch)
                    .subspan(selectionVisited, count));
            selectionVisited += count;
            report.traversedBytesThisStep += static_cast<std::uint32_t>(count);
            if (selectionVisited != GFX_WORLD_VERTEX_WIRE_SIZE)
            {
                return Error::None;
            }
            WebEngineWorldVertex vertex = DecodeVertex(vertexScratch.data());
            if (!VertexIsFinite(vertex))
            {
                return Error::NonFiniteVertex;
            }
            replacement.vertices.push_back(vertex);
            ++selectedVertexCursor;
            selectionVisited = 0u;
            ++report.recordsProcessedThisStep;
            break;
        }

        case Phase::SelectedIndices:
        {
            if (selectedIndexCursor >= selectedIndexCount)
            {
                selectionVisited = 0u;
                phase = Phase::Finish;
                break;
            }
            const std::uint32_t available =
                budget.maxBytes - report.traversedBytesThisStep;
            const std::size_t sourceIndex =
                static_cast<std::size_t>(metadata.sourceBaseIndex) +
                selectedIndexCursor;
            const std::size_t offset = serializedIndicesOffset +
                sourceIndex * sizeof(std::uint16_t);
            const std::size_t count = std::min<std::size_t>(
                sizeof(std::uint16_t) - selectionVisited,
                available);
            inflated.Copy(
                offset + selectionVisited,
                std::span<std::uint8_t>(indexScratch)
                    .subspan(selectionVisited, count));
            selectionVisited += count;
            report.traversedBytesThisStep += static_cast<std::uint32_t>(count);
            if (selectionVisited != sizeof(std::uint16_t))
            {
                return Error::None;
            }
            const std::uint16_t index = ReadU16(indexScratch.data());
            if (index >= metadata.vertexCount)
            {
                return Error::IndexOutOfRange;
            }
            replacement.indices.push_back(index);
            ++selectedIndexCursor;
            selectionVisited = 0u;
            ++report.recordsProcessedThisStep;
            break;
        }

        case Phase::Finish:
        {
            if (materialBlockScopeOpen || materialChildBlockScopeOpen ||
                !worldChildBlockScopeOpen || !rootBlockScopeOpen ||
                !assetTableBlockScopeOpen)
            {
                return Error::InvalidArgument;
            }
            error = MapZoneStreamError(arenas.Pop());
            if (error != Error::None)
            {
                return error;
            }
            worldChildBlockScopeOpen = false;
            error = MapZoneStreamError(arenas.Pop());
            if (error != Error::None)
            {
                return error;
            }
            rootBlockScopeOpen = false;
            error = MapZoneStreamError(arenas.Pop());
            if (error != Error::None)
            {
                return error;
            }
            assetTableBlockScopeOpen = false;
            if (readerPosition != inflated.size())
            {
                return Error::TrailingData;
            }
            error = MapZoneStreamError(arenas.BeginDelayedReplay());
            if (error != Error::None)
            {
                return error;
            }
            error = MapZoneStreamError(arenas.ValidateComplete());
            if (error != Error::None)
            {
                return error;
            }

            error = MapZoneRegistryError(registry.RegisterAsset(
                ASSET_TYPE_GFXWORLD,
                replacement.worldAssetIndex,
                {},
                replacement.worldIdentity));
            if (error != Error::None)
            {
                return error;
            }
            if (registry.AssetCount() != replacement.sourceAssetCount ||
                registry.AliasCount() != 1u ||
                registry.DefinedAliasCount() != 1u)
            {
                return Error::InvalidArgument;
            }
            const ZoneRegisteredAsset *material =
                registry.FindAsset(replacement.materialIdentity);
            if (!material || material->type != ASSET_TYPE_MATERIAL ||
                material->sourceIndex != replacement.materialAssetIndex)
            {
                return Error::MaterialAliasInvalid;
            }

            replacement.fastfileVersion = VERSION;
            replacement.compressedBytes = static_cast<std::uint32_t>(
                source.TotalBytesReceived() - PREFIX_SIZE);
            replacement.inflatedBytes = static_cast<std::uint32_t>(inflated.size());
            replacement.declaredZoneBytes = declaredSize;
            replacement.sourceWorldVertexCount = vertexCount;
            replacement.sourceWorldIndexCount = indexCount;
            replacement.sourceWorldSurfaceCount = 1u;
            replacement.sourceSurfaceIndex = 0u;
            replacement.registeredAssetCount = registry.AssetCount();
            replacement.materialName = material->name;
            replacement.metadata = metadata;
            ++report.recordsProcessedThisStep;
            phase = Phase::Complete;
            return Error::None;
        }

        case Phase::Complete:
            return Error::None;

        default:
            return Error::None;
        }
    }
    return Error::None;
}

WebEngineWorldSurfaceView ExtractedWorldSurface::View() const noexcept
{
    return {
        {
            vertices.data(),
            static_cast<std::uint32_t>(vertices.size()),
            indices.data(),
            static_cast<std::uint32_t>(indices.size()),
        },
        {
            0,
            0,
            static_cast<std::uint16_t>(vertices.size()),
            metadata.triangleCount,
            0,
        },
        WebEngineWorldVertexFormat::Base,
    };
}

const char *MaterialReferenceKindString(MaterialReferenceKind kind) noexcept
{
    switch (kind)
    {
    case MaterialReferenceKind::AliasToInlineShared:
        return "alias-to-inline-shared";
    }
    return "unknown";
}

const char *JobStageString(JobStage stage) noexcept
{
    switch (stage)
    {
    case JobStage::NotStarted: return "not-started";
    case JobStage::Inflate: return "inflate";
    case JobStage::Traverse: return "traverse";
    case JobStage::Complete: return "complete";
    case JobStage::Failed: return "failed";
    }
    return "unknown";
}

WorldSurfaceExtractionJob::WorldSurfaceExtractionJob() noexcept = default;
WorldSurfaceExtractionJob::~WorldSurfaceExtractionJob() = default;

WorldSurfaceExtractionJob::WorldSurfaceExtractionJob(
    WorldSurfaceExtractionJob &&other) noexcept
    : impl_(std::move(other.impl_)),
      progress_(other.progress_),
      stage_(other.stage_),
      failure_(other.failure_),
      resultAvailable_(other.resultAvailable_)
{
    other.progress_ = JobProgress::NotStarted;
    other.stage_ = JobStage::NotStarted;
    other.failure_ = Error::None;
    other.resultAvailable_ = false;
}

WorldSurfaceExtractionJob &WorldSurfaceExtractionJob::operator=(
    WorldSurfaceExtractionJob &&other) noexcept
{
    if (this != &other)
    {
        Reset();
        impl_ = std::move(other.impl_);
        progress_ = other.progress_;
        stage_ = other.stage_;
        failure_ = other.failure_;
        resultAvailable_ = other.resultAvailable_;
        other.progress_ = JobProgress::NotStarted;
        other.stage_ = JobStage::NotStarted;
        other.failure_ = Error::None;
        other.resultAvailable_ = false;
    }
    return *this;
}

Error WorldSurfaceExtractionJob::Begin(
    std::vector<std::uint8_t> &&fileBytes,
    const Limits &limits) noexcept
{
    Reset();
    const Error validation = ValidateFileEnvelope(fileBytes, limits);
    if (validation != Error::None)
    {
        progress_ = JobProgress::Failed;
        stage_ = JobStage::Failed;
        failure_ = validation;
        return validation;
    }
    const Error beginError = BeginStreaming(limits);
    if (beginError != Error::None)
    {
        return beginError;
    }
    try
    {
        impl_->ownedSource = std::move(fileBytes);
        return Error::None;
    }
    catch (...)
    {
        Reset();
        progress_ = JobProgress::Failed;
        stage_ = JobStage::Failed;
        failure_ = Error::AllocationFailed;
        return failure_;
    }
}

Error WorldSurfaceExtractionJob::BeginStreaming(const Limits &limits) noexcept
{
    Reset();
    if (const Error validation = ValidateLimits(limits);
        validation != Error::None)
    {
        progress_ = JobProgress::Failed;
        stage_ = JobStage::Failed;
        failure_ = validation;
        return validation;
    }
    try
    {
        auto replacement = std::make_unique<Impl>();
        replacement->limits = limits;
        const SourceStreamLimits sourceLimits{
            limits.maxFileBytes,
            std::min(limits.maxSourceChunkBytes, limits.maxFileBytes),
        };
        if (const Error error = MapSourceStreamError(
                replacement->source.Initialize(sourceLimits));
            error != Error::None)
        {
            progress_ = JobProgress::Failed;
            stage_ = JobStage::Failed;
            failure_ = error;
            return error;
        }
        impl_ = std::move(replacement);
        progress_ = JobProgress::Running;
        stage_ = JobStage::Inflate;
        return Error::None;
    }
    catch (...)
    {
        impl_.reset();
        progress_ = JobProgress::Failed;
        stage_ = JobStage::Failed;
        failure_ = Error::AllocationFailed;
        return failure_;
    }
}

Error WorldSurfaceExtractionJob::FeedSource(
    std::span<const std::uint8_t> bytes,
    bool final) noexcept
{
    if (!impl_ || progress_ != JobProgress::Running ||
        impl_->phase != Impl::Phase::Inflate ||
        impl_->HasOwnedSourcePending())
    {
        return Error::SourceNotReady;
    }
    return MapSourceStreamError(impl_->source.Feed(bytes, final));
}

StepReport WorldSurfaceExtractionJob::Step(const StepBudget &budget) noexcept
{
    StepReport report;
    const JobStage workStage = stage_;
    report.progress = progress_;
    report.stage = progress_ == JobProgress::Running ? workStage : stage_;
    report.error = failure_;
    if (progress_ != JobProgress::Running || !impl_)
    {
        return report;
    }
    if (budget.maxBytes == 0u || budget.maxBytes > MAX_STEP_BYTES ||
        budget.maxRecords == 0u || budget.maxRecords > MAX_STEP_RECORDS)
    {
        impl_->EndInflate();
        progress_ = JobProgress::Failed;
        stage_ = JobStage::Failed;
        failure_ = Error::InvalidStepBudget;
        report.progress = progress_;
        report.stage = stage_;
        report.error = failure_;
        return report;
    }

    try
    {
        Error error = Error::None;
        if (impl_->phase == Impl::Phase::Inflate)
        {
            error = impl_->StepInflate(budget, report);
            if (error == Error::None && impl_->phase != Impl::Phase::Inflate)
            {
                stage_ = JobStage::Traverse;
            }
        }
        else
        {
            error = impl_->StepTraverse(budget, report);
        }
        if (error != Error::None)
        {
            impl_->EndInflate();
            progress_ = JobProgress::Failed;
            stage_ = JobStage::Failed;
            failure_ = error;
        }
        else if (impl_->phase == Impl::Phase::Complete)
        {
            progress_ = JobProgress::Succeeded;
            stage_ = JobStage::Complete;
            resultAvailable_ = true;
            std::vector<std::uint8_t>().swap(impl_->ownedSource);
            impl_->ownedSourceOffset = 0u;
            std::string().swap(impl_->materialNameScratch);
            impl_->inflated.Clear();
        }
    }
    catch (...)
    {
        impl_->EndInflate();
        progress_ = JobProgress::Failed;
        stage_ = JobStage::Failed;
        failure_ = Error::AllocationFailed;
    }

    report.progress = progress_;
    report.stage = progress_ == JobProgress::Running ? workStage : stage_;
    report.error = failure_;
    report.needsSource = NeedsSource();
    return report;
}

JobProgress WorldSurfaceExtractionJob::Progress() const noexcept
{
    return progress_;
}

JobStage WorldSurfaceExtractionJob::Stage() const noexcept
{
    return stage_;
}

Error WorldSurfaceExtractionJob::Failure() const noexcept
{
    return failure_;
}

bool WorldSurfaceExtractionJob::NeedsSource() const noexcept
{
    return impl_ && progress_ == JobProgress::Running &&
        impl_->phase == Impl::Phase::Inflate &&
        impl_->source.NeedsSource() && !impl_->HasOwnedSourcePending();
}

bool WorldSurfaceExtractionJob::SourceFinalReceived() const noexcept
{
    return impl_ && impl_->source.FinalReceived();
}

std::uint32_t WorldSurfaceExtractionJob::SourceFeedCount() const noexcept
{
    return impl_ ? impl_->source.FeedCount() : 0u;
}

std::uint64_t WorldSurfaceExtractionJob::SourceBytesReceived() const noexcept
{
    return impl_ ? impl_->source.TotalBytesReceived() : 0u;
}

std::uint64_t WorldSurfaceExtractionJob::SourceBytesConsumed() const noexcept
{
    return impl_ ? impl_->source.TotalBytesConsumed() : 0u;
}

bool WorldSurfaceExtractionJob::TakeResult(
    ExtractedWorldSurface &destination) noexcept
{
    if (progress_ != JobProgress::Succeeded || !resultAvailable_ || !impl_)
    {
        return false;
    }
    destination = std::move(impl_->replacement);
    resultAvailable_ = false;
    return true;
}

void WorldSurfaceExtractionJob::Reset() noexcept
{
    impl_.reset();
    progress_ = JobProgress::NotStarted;
    stage_ = JobStage::NotStarted;
    failure_ = Error::None;
    resultAvailable_ = false;
}

Error ExtractWorldSurface(
    std::span<const std::uint8_t> fileBytes,
    const Limits &limits,
    ExtractedWorldSurface &destination) noexcept
{
    if (const Error validation = ValidateFileEnvelope(fileBytes, limits);
        validation != Error::None)
    {
        return validation;
    }
    try
    {
        std::vector<std::uint8_t> owned(fileBytes.begin(), fileBytes.end());
        WorldSurfaceExtractionJob job;
        if (const Error beginError = job.Begin(std::move(owned), limits);
            beginError != Error::None)
        {
            return beginError;
        }
        while (job.Progress() == JobProgress::Running)
        {
            (void)job.Step();
        }
        if (job.Progress() != JobProgress::Succeeded)
        {
            return job.Failure();
        }
        return job.TakeResult(destination) ? Error::None : Error::InvalidArgument;
    }
    catch (...)
    {
        return Error::AllocationFailed;
    }
}

const char *ErrorString(Error error) noexcept
{
    switch (error)
    {
    case Error::None: return "success";
    case Error::InvalidArgument: return "invalid fastfile world-surface argument";
    case Error::InvalidStepBudget:
        return "fastfile extraction step budget is invalid";
    case Error::SourceChunkTooLarge:
        return "fastfile source chunk exceeds its bounded size";
    case Error::SourceBackpressure:
        return "fastfile source still has unread bytes";
    case Error::SourceAlreadyFinal:
        return "fastfile source has already received its final marker";
    case Error::SourceNotReady:
        return "fastfile extraction is not ready for another source chunk";
    case Error::FileTooLarge: return "fastfile exceeds the bounded input size";
    case Error::PrefixTruncated: return "fastfile prefix is truncated";
    case Error::InvalidMagic: return "fastfile magic is invalid";
    case Error::AuthenticatedUnsupported:
        return "authenticated fastfiles are not supported by the synthetic slice";
    case Error::UnsupportedVersion: return "fastfile version is unsupported";
    case Error::InflateInit: return "fastfile zlib initialization failed";
    case Error::InflateData: return "fastfile zlib payload is invalid";
    case Error::InflateTruncated: return "fastfile zlib payload is truncated";
    case Error::InflateTrailingData: return "fastfile has data after the zlib stream";
    case Error::InflatedSizeLimit: return "fastfile inflated payload exceeds its limit";
    case Error::RecordTruncated: return "fastfile record stream is truncated";
    case Error::ExternalDataUnsupported:
        return "fastfile external data is unsupported";
    case Error::BlockSizeLimit: return "fastfile zone block exceeds its limit";
    case Error::TotalBlockSizeLimit:
        return "fastfile zone blocks exceed their cumulative limit";
    case Error::UnsupportedBlock:
        return "fastfile uses a zone block outside the narrow geometry slice";
    case Error::ScriptStringsUnsupported:
        return "fastfile script strings are outside the narrow geometry slice";
    case Error::AssetCountUnsupported:
        return "fastfile must contain exactly two assets in the narrow slice";
    case Error::AssetCountLimit:
        return "fastfile asset count exceeds its limit";
    case Error::MissingAssetArray: return "fastfile asset array is missing";
    case Error::AssetTypeUnsupported:
        return "fastfile assets are not the supported Material-to-GfxWorld prefix";
    case Error::AssetReferenceUnsupported:
        return "fastfile top-level asset reference is unsupported";
    case Error::UnsupportedWorldField:
        return "fastfile GfxWorld uses a field outside the narrow geometry slice";
    case Error::InvalidWorldCount: return "fastfile GfxWorld counts are invalid";
    case Error::WorldCountLimit: return "fastfile GfxWorld counts exceed their limits";
    case Error::MissingWorldArray:
        return "fastfile GfxWorld required array marker is missing";
    case Error::MaterialMemoryUnsupported:
        return "fastfile material memory record is unsupported";
    case Error::MaterialReferenceUnsupported:
        return "fastfile material reference is unsupported";
    case Error::MaterialLayoutUnsupported:
        return "fastfile material uses dependencies outside the narrow slice";
    case Error::MaterialNameTruncated: return "fastfile material name is truncated";
    case Error::MaterialNameTooLong: return "fastfile material name exceeds its limit";
    case Error::StringBytesLimit:
        return "fastfile strings exceed their cumulative byte limit";
    case Error::MaterialNameInvalid: return "fastfile material name is invalid";
    case Error::StackDepthLimit:
        return "fastfile stream stack exceeds its depth limit";
    case Error::DelayedSpanLimit:
        return "fastfile delayed stream exceeds its span limit";
    case Error::DelayedByteLimit:
        return "fastfile delayed stream exceeds its byte limit";
    case Error::AliasLimit: return "fastfile aliases exceed their limit";
    case Error::MaterialAliasUndefined:
        return "fastfile material alias is used before it is defined";
    case Error::MaterialAliasDuplicate:
        return "fastfile material alias is defined more than once";
    case Error::ZoneBlockOverflow:
        return "fastfile logical stream exceeds its declared zone block";
    case Error::ZoneBlockSizeMismatch:
        return "fastfile logical stream does not fill its declared zone blocks";
    case Error::LayeredSurfaceUnsupported:
        return "fastfile layered world surface is unsupported";
    case Error::EmptySurface: return "fastfile world surface is empty";
    case Error::SurfaceOutputTooLarge:
        return "fastfile world surface exceeds the bounded renderer slice";
    case Error::SurfaceRangeInvalid:
        return "fastfile world-surface range is outside its world arrays";
    case Error::MaterialAliasInvalid:
        return "fastfile surface material alias is invalid";
    case Error::NonFiniteBounds:
        return "fastfile world surface contains non-finite bounds";
    case Error::InvalidBounds: return "fastfile world surface bounds are inverted";
    case Error::NonFiniteVertex:
        return "fastfile world surface contains a non-finite selected vertex";
    case Error::IndexOutOfRange:
        return "fastfile world surface contains an out-of-range local index";
    case Error::TrailingData: return "fastfile logical record stream has trailing data";
    case Error::AllocationFailed: return "fastfile world-surface allocation failed";
    }
    return "unknown fastfile world-surface error";
}

} // namespace kisak::fastfile
