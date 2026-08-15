#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace kisak::fastfile
{

inline constexpr std::uint32_t RETAIL_CENSUS_ASSET_TYPE_COUNT = 33u;
inline constexpr std::uint32_t RETAIL_CENSUS_MAX_STEP_BYTES = 64u * 1024u;
inline constexpr std::uint32_t RETAIL_CENSUS_MAX_STEP_RECORDS = 64u;

struct RetailCensusLimits
{
    std::uint32_t maxFileBytes = 16u * 1024u * 1024u;
    std::uint32_t maxSourceChunkBytes = RETAIL_CENSUS_MAX_STEP_BYTES;
    std::uint32_t maxInflatedPrefixBytes = 256u * 1024u;
    std::uint32_t maxBlockBytes = 512u * 1024u * 1024u;
    std::uint64_t maxTotalBlockBytes = 1536ull * 1024ull * 1024ull;
    std::uint32_t maxScriptStrings = 4096u;
    std::uint32_t maxScriptStringBytes = 4096u;
    std::uint32_t maxTotalScriptStringBytes = 256u * 1024u;
    std::uint32_t maxAssets = 16384u;
    std::uint32_t maxTechniqueNameBytes = 255u;
    std::uint32_t maxTechniquePasses = 16u;
    std::uint32_t maxShaderNameBytes = 255u;
    std::uint32_t maxShaderProgramDwords = 16384u;
};

enum class RetailCensusError : std::uint8_t
{
    None = 0,
    InvalidArgument,
    InvalidStepBudget,
    SourceChunkTooLarge,
    SourceBackpressure,
    SourceAlreadyFinal,
    FileTooLarge,
    PrefixTruncated,
    InvalidMagic,
    AuthenticatedUnsupported,
    UnsupportedVersion,
    InflateInit,
    InflateData,
    InflateTruncated,
    InflatedPrefixLimit,
    RecordTruncated,
    BlockSizeLimit,
    TotalBlockSizeLimit,
    ScriptStringCountInvalid,
    ScriptStringCountLimit,
    ScriptStringArrayInvalid,
    ScriptStringReferenceUnsupported,
    ScriptStringTooLong,
    ScriptStringBytesLimit,
    AssetCountInvalid,
    AssetCountLimit,
    AssetArrayInvalid,
    AssetTypeInvalid,
    ZoneStreamInvalid,
    ZoneBlockOverflow,
    FirstAssetUnsupported,
    TechniqueSetLayoutUnsupported,
    TechniqueSetNameInvalid,
    TechniqueSetNameTooLong,
    TechniqueReferenceUnsupported,
    TechniqueLayoutUnsupported,
    TechniquePassCountLimit,
    MaterialPassUnsupported,
    VertexDeclarationUnsupported,
    VertexShaderLayoutUnsupported,
    VertexShaderNameInvalid,
    VertexShaderNameTooLong,
    ShaderProgramSizeInvalid,
    ShaderProgramSizeLimit,
    ShaderProgramSignatureInvalid,
    PixelShaderLayoutUnsupported,
    ShaderContractInvalid,
    ShaderSubstitutionUnsupported,
    ShaderArgumentLayoutUnsupported,
    TechniqueNameInvalid,
    AllocationFailed,
};

const char *RetailCensusErrorString(RetailCensusError error) noexcept;
const char *RetailAssetTypeName(std::uint32_t type) noexcept;

enum class RetailCensusProgress : std::uint8_t
{
    NotStarted = 0,
    Running,
    Succeeded,
    Failed,
};

enum class RetailCensusStage : std::uint8_t
{
    NotStarted = 0,
    Prefix,
    Inflate,
    XFile,
    AssetList,
    ScriptStringPointers,
    ScriptStrings,
    AssetTable,
    TechniqueSet,
    TechniqueSetName,
    Technique,
    MaterialPasses,
    VertexDeclaration,
    VertexShader,
    VertexShaderName,
    VertexShaderProgram,
    PixelShader,
    PixelShaderProgram,
    ShaderArguments,
    TechniqueName,
    AssetBoundary,
    Failed,
};

const char *RetailCensusStageString(RetailCensusStage stage) noexcept;

struct RetailCensusStepBudget
{
    std::uint32_t maxBytes = RETAIL_CENSUS_MAX_STEP_BYTES;
    std::uint32_t maxRecords = RETAIL_CENSUS_MAX_STEP_RECORDS;
};

struct RetailCensusStepReport
{
    RetailCensusProgress progress = RetailCensusProgress::NotStarted;
    RetailCensusStage stage = RetailCensusStage::NotStarted;
    RetailCensusError error = RetailCensusError::None;
    std::uint32_t sourceBytesConsumed = 0u;
    std::uint32_t inflatedBytesProduced = 0u;
    std::uint32_t traversedBytes = 0u;
    std::uint32_t recordsProcessed = 0u;
    bool needsSource = false;
};

struct RetailFastfileCensus
{
    std::uint32_t version = 0u;
    std::uint32_t xfileSize = 0u;
    std::uint32_t externalSize = 0u;
    std::array<std::uint32_t, 9> blockSizes{};
    std::uint64_t declaredBlockBytes = 0u;
    std::uint32_t scriptStringCount = 0u;
    std::uint32_t scriptStringBytes = 0u;
    std::uint32_t assetCount = 0u;
    std::array<std::uint32_t, RETAIL_CENSUS_ASSET_TYPE_COUNT> typeCounts{};
    std::uint32_t inlineAssetReferences = 0u;
    std::uint32_t sharedAssetReferences = 0u;
    std::uint32_t aliasAssetReferences = 0u;
    std::uint32_t nullAssetReferences = 0u;
    std::uint32_t firstBodyIndex = 0u;
    std::uint32_t firstBodyType = 0u;
    std::uint32_t firstBodyReference = 0u;
    std::uint32_t inflatedPrefixBytes = 0u;
    std::uint64_t sourceBytesConsumed = 0u;
    std::uint32_t sourceFeedCount = 0u;
    bool stoppedBeforeAssetBody = false;
    std::string techniqueSetName;
    std::string vertexShaderName;
    std::string pixelShaderName;
    std::string techniqueName;
    std::uint32_t firstTechniqueSlot = 0u;
    std::uint32_t techniquePassCount = 0u;
    std::uint32_t vertexStreamCount = 0u;
    std::array<std::uint8_t, 32> vertexStreamRouting{};
    std::uint32_t vertexStreamRoutingHash = 0u;
    std::uint32_t vertexShaderProgramDwords = 0u;
    std::uint32_t vertexShaderProgramHash = 0u;
    std::uint32_t vertexShaderInstructionCount = 0u;
    std::uint32_t vertexShaderConstantCount = 0u;
    std::uint32_t pixelShaderProgramDwords = 0u;
    std::uint32_t pixelShaderProgramHash = 0u;
    std::uint32_t pixelShaderInstructionCount = 0u;
    std::uint32_t pixelShaderConstantCount = 0u;
    std::uint32_t shaderArgumentCount = 0u;
    std::uint32_t shaderArgumentHash = 0u;
    std::string shaderSubstitutionId;
    std::uint32_t vertexGlslHash = 0u;
    std::uint32_t fragmentGlslHash = 0u;
    std::uint32_t assetTableBlock4Offset = 0u;
    std::uint32_t techniqueSetBlock0Offset = 0u;
    std::uint32_t techniqueBlock4Offset = 0u;
    std::uint32_t vertexDeclarationBlock4Offset = 0u;
    std::uint32_t vertexShaderBlock4Offset = 0u;
    std::uint32_t vertexShaderProgramBlock4Offset = 0u;
    std::uint32_t pixelShaderBlock4Offset = 0u;
    std::uint32_t pixelShaderProgramBlock4Offset = 0u;
    std::uint32_t shaderArgumentsBlock4Offset = 0u;
    std::uint32_t block0HighWaterAtBoundary = 0u;
    std::uint32_t block4CursorAtBoundary = 0u;
    std::uint32_t completedAssetCount = 0u;
    bool techniqueSetPublished = false;
    bool vertexDeclarationPrepared = false;
    bool stoppedBeforeShaderCreation = false;
    bool shaderCompatibilitySelected = false;
    const char *unsupportedOperation = nullptr;
};

// A deliberately narrow retail reader. It validates the unsigned v5/zlib
// envelope, XFile, ScriptStringList and complete XAsset table, then follows the
// generated loader for asset zero through its technique set, first technique,
// first pass, both shader contracts, arguments, and technique name. Native D3D9
// creation is replaced by an explicit WebGL2 compatibility record; asset zero
// is published only after the complete generated-loader path validates.
class RetailFastfileCensusJob
{
public:
    RetailFastfileCensusJob() noexcept;
    ~RetailFastfileCensusJob();
    RetailFastfileCensusJob(RetailFastfileCensusJob &&) noexcept;
    RetailFastfileCensusJob &operator=(RetailFastfileCensusJob &&) noexcept;
    RetailFastfileCensusJob(const RetailFastfileCensusJob &) = delete;
    RetailFastfileCensusJob &operator=(const RetailFastfileCensusJob &) = delete;

    RetailCensusError BeginStreaming(
        const RetailCensusLimits &limits = {}) noexcept;
    RetailCensusError FeedSource(
        std::span<const std::uint8_t> bytes,
        bool final) noexcept;
    RetailCensusStepReport Step(
        const RetailCensusStepBudget &budget = {}) noexcept;
    RetailCensusProgress Progress() const noexcept;
    RetailCensusStage Stage() const noexcept;
    RetailCensusError Failure() const noexcept;
    bool NeedsSource() const noexcept;
    std::uint64_t SourceBytesReceived() const noexcept;
    bool TakeResult(RetailFastfileCensus &destination) noexcept;
    void Reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    RetailCensusProgress progress_ = RetailCensusProgress::NotStarted;
    RetailCensusStage stage_ = RetailCensusStage::NotStarted;
    RetailCensusError failure_ = RetailCensusError::None;
    bool resultAvailable_ = false;
};

} // namespace kisak::fastfile
