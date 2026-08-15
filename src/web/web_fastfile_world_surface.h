#pragma once

#include <web/web_engine_world_surface.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace kisak::fastfile
{

constexpr std::uint32_t VERSION = 5u;
constexpr std::uint32_t ZONE_BLOCK_COUNT = 9u;
constexpr std::uint32_t GFX_WORLD_WIRE_SIZE = 732u;
constexpr std::uint32_t GFX_WORLD_VERTEX_WIRE_SIZE = 44u;
constexpr std::uint32_t GFX_SURFACE_WIRE_SIZE = 48u;
constexpr std::uint32_t MATERIAL_WIRE_SIZE = 80u;
constexpr std::uint32_t MAX_STEP_BYTES = 64u * 1024u;
constexpr std::uint32_t MAX_STEP_RECORDS = 64u;

// These are deliberately provisional ceilings for the strict synthetic-only
// slice. A general zone loader still needs measured per-zone budgets and a
// broader generated traversal before it may consume user-owned fastfiles.
struct Limits
{
    std::uint32_t maxFileBytes = 4u * 1024u * 1024u;
    std::uint32_t maxSourceChunkBytes = MAX_STEP_BYTES;
    std::uint32_t maxInflatedBytes = 8u * 1024u * 1024u;
    std::uint32_t maxBlockBytes = 8u * 1024u * 1024u;
    std::uint32_t maxTotalBlockBytes = 8u * 1024u * 1024u;
    std::uint32_t maxAssets = 2u;
    std::uint32_t maxStackDepth = 64u;
    std::uint32_t maxDelayedSpans = 4096u;
    std::uint32_t maxDelayedBytes = 8u * 1024u * 1024u;
    std::uint32_t maxTotalStringBytes = 256u;
    std::uint32_t maxAliases = 1u;
    std::uint32_t maxWorldVertices = 65536u;
    std::uint32_t maxWorldIndices = 262144u;
    std::uint32_t maxSelectedVertices = WEB_RENDERER_MAX_SURFACE_VERTICES;
    std::uint32_t maxSelectedIndices = WEB_RENDERER_MAX_SURFACE_INDICES;
    std::uint32_t maxMaterialNameBytes = 255u;
};

enum class MaterialReferenceKind : std::uint8_t
{
    AliasToInlineShared = 0,
};

const char *MaterialReferenceKindString(MaterialReferenceKind kind) noexcept;

struct SurfaceMetadata
{
    std::uint32_t sourceFirstVertex = 0u;
    std::uint32_t sourceBaseIndex = 0u;
    std::uint16_t vertexCount = 0u;
    std::uint16_t triangleCount = 0u;
    std::uint8_t lightmapIndex = 0u;
    std::uint8_t reflectionProbeIndex = 0u;
    std::uint8_t primaryLightIndex = 0u;
    std::uint8_t flags = 0u;
    float bounds[2][3]{};
    MaterialReferenceKind materialReference =
        MaterialReferenceKind::AliasToInlineShared;
};

struct ExtractedWorldSurface
{
    std::uint32_t fastfileVersion = 0u;
    std::uint32_t compressedBytes = 0u;
    std::uint32_t inflatedBytes = 0u;
    std::uint32_t declaredZoneBytes = 0u;
    std::array<std::uint32_t, ZONE_BLOCK_COUNT> blockSizes{};
    std::uint32_t sourceAssetCount = 0u;
    std::uint32_t materialAssetIndex = 0u;
    std::uint32_t worldAssetIndex = 0u;
    // Stable job-local identity resolved through the serialized block-4 alias.
    // This is deliberately not a serialized or native pointer value.
    std::uint32_t materialIdentity = 0u;
    std::uint32_t worldIdentity = 0u;
    std::uint32_t registeredAssetCount = 0u;
    std::uint32_t sourceWorldVertexCount = 0u;
    std::uint32_t sourceWorldIndexCount = 0u;
    std::uint32_t sourceWorldSurfaceCount = 0u;
    std::uint32_t sourceSurfaceIndex = 0u;
    SurfaceMetadata metadata{};
    std::string materialName;
    std::vector<WebEngineWorldVertex> vertices;
    std::vector<std::uint16_t> indices;

    // The extracted vectors contain only the selected surface slice. The view
    // is normalized to zero-based ranges while metadata preserves the source
    // GfxWorld firstVertex/baseIndex values.
    WebEngineWorldSurfaceView View() const noexcept;
};

enum class Error : std::uint32_t
{
    None = 0,
    InvalidArgument,
    InvalidStepBudget,
    SourceChunkTooLarge,
    SourceBackpressure,
    SourceAlreadyFinal,
    SourceNotReady,
    FileTooLarge,
    PrefixTruncated,
    InvalidMagic,
    AuthenticatedUnsupported,
    UnsupportedVersion,
    InflateInit,
    InflateData,
    InflateTruncated,
    InflateTrailingData,
    InflatedSizeLimit,
    RecordTruncated,
    ExternalDataUnsupported,
    BlockSizeLimit,
    TotalBlockSizeLimit,
    UnsupportedBlock,
    ScriptStringsUnsupported,
    AssetCountUnsupported,
    AssetCountLimit,
    MissingAssetArray,
    AssetTypeUnsupported,
    AssetReferenceUnsupported,
    UnsupportedWorldField,
    InvalidWorldCount,
    WorldCountLimit,
    MissingWorldArray,
    MaterialMemoryUnsupported,
    MaterialReferenceUnsupported,
    MaterialLayoutUnsupported,
    MaterialNameTruncated,
    MaterialNameTooLong,
    StringBytesLimit,
    MaterialNameInvalid,
    StackDepthLimit,
    DelayedSpanLimit,
    DelayedByteLimit,
    AliasLimit,
    MaterialAliasUndefined,
    MaterialAliasDuplicate,
    ZoneBlockOverflow,
    ZoneBlockSizeMismatch,
    LayeredSurfaceUnsupported,
    EmptySurface,
    SurfaceOutputTooLarge,
    SurfaceRangeInvalid,
    MaterialAliasInvalid,
    NonFiniteBounds,
    InvalidBounds,
    NonFiniteVertex,
    IndexOutOfRange,
    TrailingData,
    AllocationFailed,
};

const char *ErrorString(Error error) noexcept;

enum class JobProgress : std::uint8_t
{
    NotStarted = 0,
    Running,
    Succeeded,
    Failed,
};

enum class JobStage : std::uint8_t
{
    NotStarted = 0,
    Inflate,
    Traverse,
    Complete,
    Failed,
};

const char *JobStageString(JobStage stage) noexcept;

struct StepBudget
{
    // Both the compressed input window and inflated output window are capped
    // independently by maxBytes, matching the browser filesystem decoder's
    // existing 64 KiB convention. Traversal visits at most maxBytes of the
    // inflated stream and completes at most maxRecords semantic records.
    std::uint32_t maxBytes = MAX_STEP_BYTES;
    std::uint32_t maxRecords = MAX_STEP_RECORDS;
};

struct StepReport
{
    JobProgress progress = JobProgress::NotStarted;
    JobStage stage = JobStage::NotStarted;
    Error error = Error::None;
    std::uint32_t compressedBytesConsumedThisStep = 0u;
    std::uint32_t inflatedBytesProducedThisStep = 0u;
    std::uint32_t traversedBytesThisStep = 0u;
    std::uint32_t recordsProcessedThisStep = 0u;
    std::uint32_t sourceBytesConsumedThisStep = 0u;
    bool needsSource = false;
};

// Incremental owner for the same deliberately strict extraction implemented
// by ExtractWorldSurface. BeginStreaming creates a backpressured source that
// accepts one bounded chunk at a time and can remain Running while NeedsSource
// is true. FeedSource copies each chunk, so caller memory may expire immediately.
// The vector-taking Begin overload is a compatibility owner that feeds the same
// source seam automatically. A zero or over-limit Step consumes no work and
// terminates with InvalidStepBudget. It never writes caller output. TakeResult
// is a one-shot atomic move available only after Succeeded.
class WorldSurfaceExtractionJob
{
public:
    WorldSurfaceExtractionJob() noexcept;
    ~WorldSurfaceExtractionJob();
    WorldSurfaceExtractionJob(WorldSurfaceExtractionJob &&) noexcept;
    WorldSurfaceExtractionJob &operator=(WorldSurfaceExtractionJob &&) noexcept;
    WorldSurfaceExtractionJob(const WorldSurfaceExtractionJob &) = delete;
    WorldSurfaceExtractionJob &operator=(const WorldSurfaceExtractionJob &) = delete;

    Error Begin(
        std::vector<std::uint8_t> &&fileBytes,
        const Limits &limits = {}) noexcept;
    Error BeginStreaming(const Limits &limits = {}) noexcept;
    Error FeedSource(
        std::span<const std::uint8_t> bytes,
        bool final) noexcept;
    StepReport Step(const StepBudget &budget = {}) noexcept;
    JobProgress Progress() const noexcept;
    JobStage Stage() const noexcept;
    Error Failure() const noexcept;
    bool NeedsSource() const noexcept;
    bool SourceFinalReceived() const noexcept;
    std::uint32_t SourceFeedCount() const noexcept;
    std::uint64_t SourceBytesReceived() const noexcept;
    std::uint64_t SourceBytesConsumed() const noexcept;
    bool TakeResult(ExtractedWorldSurface &destination) noexcept;
    void Reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    JobProgress progress_ = JobProgress::NotStarted;
    JobStage stage_ = JobStage::NotStarted;
    Error failure_ = Error::None;
    bool resultAvailable_ = false;
};

// Decodes the deliberately strict first fastfile slice. It accepts only an
// unauthenticated IWffu100 v5 file containing no script strings and exactly two
// top-level records: a minimal inline-shared Material followed by one inline
// GfxWorld whose MaterialMemory and selected surface resolve that material.
// It is not a general fastfile scanner and intentionally rejects retail zones
// whose earlier assets would require the complete generated loader traversal.
// Successful output owns only the selected geometry and replaces destination
// atomically; every failure leaves destination unchanged.
Error ExtractWorldSurface(
    std::span<const std::uint8_t> fileBytes,
    const Limits &limits,
    ExtractedWorldSurface &destination) noexcept;

} // namespace kisak::fastfile
