#include <web/web_retail_fastfile_census.h>

#include <web/web_fastfile_source_stream.h>
#include <web/web_fastfile_zone_stream.h>
#include <web/web_shader_compatibility.h>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace kisak::fastfile
{
namespace
{
constexpr std::array<std::uint8_t, 8> UNSIGNED_MAGIC = {'I','W','f','f','u','1','0','0'};
constexpr std::array<std::uint8_t, 8> AUTHENTICATED_MAGIC = {'I','W','f','f','0','1','0','0'};
constexpr std::uint32_t PREFIX_BYTES = 12u;
constexpr std::uint32_t XFILE_BYTES = 44u;
constexpr std::uint32_t ASSET_LIST_BYTES = 16u;
constexpr std::uint32_t ASSET_BYTES = 8u;
constexpr std::uint32_t INLINE_POINTER = 0xffffffffu;
constexpr std::uint32_t SHARED_POINTER = 0xfffffffeu;
constexpr std::uint32_t TECHNIQUE_SET_BYTES = 148u;
constexpr std::uint32_t TECHNIQUE_HEADER_BYTES = 8u;
constexpr std::uint32_t MATERIAL_PASS_BYTES = 20u;
constexpr std::uint32_t VERTEX_DECLARATION_BYTES = 100u;
constexpr std::uint32_t VERTEX_SHADER_BYTES = 16u;
constexpr std::uint32_t PIXEL_SHADER_BYTES = 16u;
constexpr std::uint32_t MATERIAL_ARGUMENT_BYTES = 8u;

constexpr std::array<const char *, RETAIL_CENSUS_ASSET_TYPE_COUNT> ASSET_NAMES = {{
    "xmodelpieces", "physpreset", "xanim", "xmodel", "material", "techset",
    "image", "sound", "sndcurve", "loaded_sound", "col_map_sp", "col_map_mp",
    "com_map", "game_map_sp", "game_map_mp", "map_ents", "gfx_map", "lightdef",
    "ui_map", "font", "menufile", "menu", "localize", "weapon",
    "snddriverglobals", "fx", "impactfx", "aitype", "mptype", "character",
    "xmodelalias", "rawfile", "stringtable",
}};

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

RetailCensusError MapSourceError(SourceStreamError error) noexcept
{
    switch (error)
    {
    case SourceStreamError::None: return RetailCensusError::None;
    case SourceStreamError::ChunkTooLarge: return RetailCensusError::SourceChunkTooLarge;
    case SourceStreamError::TotalSizeLimit: return RetailCensusError::FileTooLarge;
    case SourceStreamError::Backpressure: return RetailCensusError::SourceBackpressure;
    case SourceStreamError::AlreadyFinal: return RetailCensusError::SourceAlreadyFinal;
    case SourceStreamError::AllocationFailed: return RetailCensusError::AllocationFailed;
    default: return RetailCensusError::InvalidArgument;
    }
}

RetailCensusError MapZoneError(ZoneStreamError error) noexcept
{
    switch (error)
    {
    case ZoneStreamError::None: return RetailCensusError::None;
    case ZoneStreamError::BlockOverflow: return RetailCensusError::ZoneBlockOverflow;
    case ZoneStreamError::AllocationFailed: return RetailCensusError::AllocationFailed;
    default: return RetailCensusError::ZoneStreamInvalid;
    }
}

std::uint32_t Fnv1a32(std::span<const std::uint8_t> bytes) noexcept
{
    std::uint32_t value = 2166136261u;
    for (const std::uint8_t byte : bytes)
    {
        value ^= byte;
        value *= 16777619u;
    }
    return value;
}

bool ValidLimits(const RetailCensusLimits &limits) noexcept
{
    return limits.maxFileBytes >= PREFIX_BYTES &&
        limits.maxSourceChunkBytes != 0u &&
        limits.maxSourceChunkBytes <= RETAIL_CENSUS_MAX_STEP_BYTES &&
        limits.maxInflatedPrefixBytes >= XFILE_BYTES + ASSET_LIST_BYTES &&
        limits.maxBlockBytes != 0u && limits.maxTotalBlockBytes != 0u &&
        limits.maxScriptStrings != 0u && limits.maxScriptStringBytes != 0u &&
        limits.maxTotalScriptStringBytes != 0u && limits.maxAssets != 0u &&
        limits.maxTechniqueNameBytes != 0u && limits.maxTechniquePasses != 0u &&
        limits.maxShaderNameBytes != 0u && limits.maxShaderProgramDwords != 0u;
}
} // namespace

const char *RetailAssetTypeName(std::uint32_t type) noexcept
{
    return type < ASSET_NAMES.size() ? ASSET_NAMES[type] : "invalid";
}

const char *RetailCensusErrorString(RetailCensusError error) noexcept
{
    switch (error)
    {
    case RetailCensusError::None: return "none";
    case RetailCensusError::InvalidArgument: return "invalid argument";
    case RetailCensusError::InvalidStepBudget: return "invalid step budget";
    case RetailCensusError::SourceChunkTooLarge: return "source chunk exceeds limit";
    case RetailCensusError::SourceBackpressure: return "source backpressure";
    case RetailCensusError::SourceAlreadyFinal: return "source already final";
    case RetailCensusError::FileTooLarge: return "fastfile exceeds source limit";
    case RetailCensusError::PrefixTruncated: return "fastfile prefix truncated";
    case RetailCensusError::InvalidMagic: return "invalid fastfile magic";
    case RetailCensusError::AuthenticatedUnsupported: return "authenticated fastfile unsupported";
    case RetailCensusError::UnsupportedVersion: return "unsupported fastfile version";
    case RetailCensusError::InflateInit: return "zlib initialization failed";
    case RetailCensusError::InflateData: return "invalid zlib stream";
    case RetailCensusError::InflateTruncated: return "zlib prefix truncated";
    case RetailCensusError::InflatedPrefixLimit: return "inflated prefix exceeds limit";
    case RetailCensusError::RecordTruncated: return "fastfile prefix record truncated";
    case RetailCensusError::BlockSizeLimit: return "zone block exceeds limit";
    case RetailCensusError::TotalBlockSizeLimit: return "zone block total exceeds limit";
    case RetailCensusError::ScriptStringCountInvalid: return "invalid script string count";
    case RetailCensusError::ScriptStringCountLimit: return "script string count exceeds limit";
    case RetailCensusError::ScriptStringArrayInvalid: return "invalid script string array token";
    case RetailCensusError::ScriptStringReferenceUnsupported: return "unsupported script string reference";
    case RetailCensusError::ScriptStringTooLong: return "script string exceeds limit";
    case RetailCensusError::ScriptStringBytesLimit: return "script string bytes exceed limit";
    case RetailCensusError::AssetCountInvalid: return "invalid asset count";
    case RetailCensusError::AssetCountLimit: return "asset count exceeds limit";
    case RetailCensusError::AssetArrayInvalid: return "invalid asset array token";
    case RetailCensusError::AssetTypeInvalid: return "invalid asset type";
    case RetailCensusError::ZoneStreamInvalid: return "invalid logical zone stream state";
    case RetailCensusError::ZoneBlockOverflow: return "logical zone block overflow";
    case RetailCensusError::FirstAssetUnsupported: return "first asset is not an inline technique set";
    case RetailCensusError::TechniqueSetLayoutUnsupported: return "unsupported technique-set layout";
    case RetailCensusError::TechniqueSetNameInvalid: return "invalid technique-set name";
    case RetailCensusError::TechniqueSetNameTooLong: return "technique-set name exceeds limit";
    case RetailCensusError::TechniqueReferenceUnsupported: return "unsupported technique reference";
    case RetailCensusError::TechniqueLayoutUnsupported: return "unsupported technique layout";
    case RetailCensusError::TechniquePassCountLimit: return "technique pass count exceeds limit";
    case RetailCensusError::MaterialPassUnsupported: return "unsupported material pass layout";
    case RetailCensusError::VertexDeclarationUnsupported: return "unsupported vertex declaration";
    case RetailCensusError::VertexShaderLayoutUnsupported: return "unsupported vertex shader layout";
    case RetailCensusError::VertexShaderNameInvalid: return "invalid vertex shader name";
    case RetailCensusError::VertexShaderNameTooLong: return "vertex shader name exceeds limit";
    case RetailCensusError::ShaderProgramSizeInvalid: return "invalid vertex shader program size";
    case RetailCensusError::ShaderProgramSizeLimit: return "vertex shader program exceeds limit";
    case RetailCensusError::ShaderProgramSignatureInvalid: return "invalid Direct3D vertex shader signature";
    case RetailCensusError::PixelShaderLayoutUnsupported: return "unsupported pixel shader layout";
    case RetailCensusError::ShaderContractInvalid: return "invalid Direct3D shader contract";
    case RetailCensusError::ShaderSubstitutionUnsupported: return "no WebGL2 shader substitution matches";
    case RetailCensusError::ShaderArgumentLayoutUnsupported: return "unsupported shader argument layout";
    case RetailCensusError::TechniqueNameInvalid: return "invalid technique name";
    case RetailCensusError::AllocationFailed: return "allocation failed";
    }
    return "unknown retail census error";
}

const char *RetailCensusStageString(RetailCensusStage stage) noexcept
{
    switch (stage)
    {
    case RetailCensusStage::NotStarted: return "not-started";
    case RetailCensusStage::Prefix: return "prefix";
    case RetailCensusStage::Inflate: return "inflate";
    case RetailCensusStage::XFile: return "xfile";
    case RetailCensusStage::AssetList: return "asset-list";
    case RetailCensusStage::ScriptStringPointers: return "script-string-pointers";
    case RetailCensusStage::ScriptStrings: return "script-strings";
    case RetailCensusStage::AssetTable: return "asset-table";
    case RetailCensusStage::TechniqueSet: return "technique-set";
    case RetailCensusStage::TechniqueSetName: return "technique-set-name";
    case RetailCensusStage::Technique: return "technique";
    case RetailCensusStage::MaterialPasses: return "material-passes";
    case RetailCensusStage::VertexDeclaration: return "vertex-declaration";
    case RetailCensusStage::VertexShader: return "vertex-shader";
    case RetailCensusStage::VertexShaderName: return "vertex-shader-name";
    case RetailCensusStage::VertexShaderProgram: return "vertex-shader-program";
    case RetailCensusStage::PixelShader: return "pixel-shader";
    case RetailCensusStage::PixelShaderProgram: return "pixel-shader-program";
    case RetailCensusStage::ShaderArguments: return "shader-arguments";
    case RetailCensusStage::TechniqueName: return "technique-name";
    case RetailCensusStage::AssetBoundary: return "asset-boundary";
    case RetailCensusStage::Failed: return "failed";
    }
    return "unknown";
}

struct RetailFastfileCensusJob::Impl
{
    BoundedSourceStream source;
    RetailCensusLimits limits{};
    std::array<std::uint8_t, PREFIX_BYTES> prefix{};
    std::uint32_t prefixCount = 0u;
    std::vector<std::uint8_t> inflated;
    z_stream stream{};
    bool streamInitialized = false;
    bool streamEnded = false;
    std::size_t cursor = 0u;
    std::size_t assetTableOffset = 0u;
    std::vector<std::uint32_t> scriptTokens;
    std::uint32_t scriptIndex = 0u;
    std::uint32_t assetIndex = 0u;
    std::size_t recordVisited = 0u;
    ZoneStreamMachine arenas;
    bool arenasInitialized = false;
    bool scriptScopesOpen = false;
    bool assetScopeOpen = false;
    std::array<std::uint32_t, 34> techniqueTokens{};
    std::uint32_t materialPassBytes = 0u;
    std::uint32_t vertexShaderProgramBytes = 0u;
    std::uint32_t pixelShaderProgramBytes = 0u;
    std::uint32_t materialArgumentBytes = 0u;
    std::uint32_t pixelShaderToken = 0u;
    std::uint32_t materialArgumentToken = 0u;
    std::uint32_t vertexShaderNameBlock4Offset = 0u;
    kisak::web::D3D9ShaderContract vertexContract;
    kisak::web::D3D9ShaderContract pixelContract;
    RetailFastfileCensus result{};

    ~Impl()
    {
        if (streamInitialized) inflateEnd(&stream);
    }

    bool Available(std::size_t count) const noexcept
    {
        return cursor <= inflated.size() && count <= inflated.size() - cursor;
    }

    RetailCensusError Push(std::uint32_t block) noexcept
    {
        return MapZoneError(arenas.Push(block));
    }

    RetailCensusError Pop() noexcept
    {
        return MapZoneError(arenas.Pop());
    }

    RetailCensusError Plan(
        std::uint32_t alignment,
        std::uint64_t length,
        ZoneSpan *span = nullptr) noexcept
    {
        ZoneLoadPlan plan;
        const RetailCensusError error = MapZoneError(
            arenas.PlanLoad(alignment, length, plan));
        if (error != RetailCensusError::None) return error;
        if (plan.kind != ZoneLoadKind::Immediate)
            return RetailCensusError::ZoneStreamInvalid;
        if (span) *span = plan.span;
        return RetailCensusError::None;
    }

    RetailCensusError ValidatePrefix() noexcept
    {
        if (std::equal(AUTHENTICATED_MAGIC.begin(), AUTHENTICATED_MAGIC.end(), prefix.begin()))
            return RetailCensusError::AuthenticatedUnsupported;
        if (!std::equal(UNSIGNED_MAGIC.begin(), UNSIGNED_MAGIC.end(), prefix.begin()))
            return RetailCensusError::InvalidMagic;
        result.version = ReadU32(prefix.data() + 8u);
        return result.version == 5u
            ? RetailCensusError::None
            : RetailCensusError::UnsupportedVersion;
    }

    RetailCensusError Parse(
        RetailCensusStepBudget budget,
        RetailCensusStepReport &report,
        RetailCensusStage &stage,
        bool &blocked,
        bool &complete) noexcept
    {
        blocked = false;
        complete = false;
        auto visitRecord = [&](std::size_t bytes) noexcept -> int {
            if (!Available(bytes))
            {
                blocked = true;
                return -1;
            }
            const std::size_t byteBudget = budget.maxBytes - report.traversedBytes;
            const std::size_t count = std::min(bytes - recordVisited, byteBudget);
            recordVisited += count;
            report.traversedBytes += static_cast<std::uint32_t>(count);
            if (recordVisited != bytes) return 0;
            recordVisited = 0u;
            return 1;
        };
        while (report.recordsProcessed < budget.maxRecords &&
            report.traversedBytes < budget.maxBytes)
        {
            if (stage == RetailCensusStage::XFile)
            {
                const int visit = visitRecord(XFILE_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                result.xfileSize = ReadU32(record);
                result.externalSize = ReadU32(record + 4u);
                std::uint64_t total = 0u;
                for (std::size_t index = 0u; index < result.blockSizes.size(); ++index)
                {
                    const std::uint32_t size = ReadU32(record + 8u + index * 4u);
                    if (size > limits.maxBlockBytes) return RetailCensusError::BlockSizeLimit;
                    result.blockSizes[index] = size;
                    total += size;
                    if (total > limits.maxTotalBlockBytes) return RetailCensusError::TotalBlockSizeLimit;
                }
                result.declaredBlockBytes = total;
                if (const RetailCensusError error = MapZoneError(arenas.Initialize(
                        result.blockSizes,
                        {limits.maxTotalBlockBytes, 64u, 4096u, limits.maxTotalBlockBytes}));
                    error != RetailCensusError::None)
                    return error;
                arenasInitialized = true;
                cursor += XFILE_BYTES;
                ++report.recordsProcessed;
                stage = RetailCensusStage::AssetList;
                continue;
            }
            if (stage == RetailCensusStage::AssetList)
            {
                const int visit = visitRecord(ASSET_LIST_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::int32_t scriptCount = ReadS32(record);
                const std::uint32_t scriptToken = ReadU32(record + 4u);
                const std::int32_t assetCount = ReadS32(record + 8u);
                const std::uint32_t assetToken = ReadU32(record + 12u);
                if (scriptCount < 0) return RetailCensusError::ScriptStringCountInvalid;
                if (static_cast<std::uint32_t>(scriptCount) > limits.maxScriptStrings)
                    return RetailCensusError::ScriptStringCountLimit;
                if ((scriptCount == 0 && scriptToken != 0u) ||
                    (scriptCount != 0 && scriptToken != INLINE_POINTER))
                    return RetailCensusError::ScriptStringArrayInvalid;
                if (assetCount <= 0) return RetailCensusError::AssetCountInvalid;
                if (static_cast<std::uint32_t>(assetCount) > limits.maxAssets)
                    return RetailCensusError::AssetCountLimit;
                if (assetToken != INLINE_POINTER) return RetailCensusError::AssetArrayInvalid;
                result.scriptStringCount = static_cast<std::uint32_t>(scriptCount);
                result.assetCount = static_cast<std::uint32_t>(assetCount);
                cursor += ASSET_LIST_BYTES;
                ++report.recordsProcessed;
                stage = RetailCensusStage::ScriptStringPointers;
                continue;
            }
            if (stage == RetailCensusStage::ScriptStringPointers)
            {
                const std::uint64_t bytes64 = static_cast<std::uint64_t>(result.scriptStringCount) * 4u;
                if (bytes64 > std::numeric_limits<std::size_t>::max())
                    return RetailCensusError::ScriptStringCountLimit;
                const std::size_t bytes = static_cast<std::size_t>(bytes64);
                if (!scriptScopesOpen)
                {
                    if (!arenasInitialized) return RetailCensusError::ZoneStreamInvalid;
                    if (const RetailCensusError error = Push(4u);
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Push(4u);
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Plan(4u, bytes64);
                        error != RetailCensusError::None) return error;
                    scriptScopesOpen = true;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    scriptTokens.resize(result.scriptStringCount);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                for (std::uint32_t index = 0u; index < result.scriptStringCount; ++index)
                    scriptTokens[index] = ReadU32(inflated.data() + cursor + index * 4u);
                cursor += bytes;
                ++report.recordsProcessed;
                stage = RetailCensusStage::ScriptStrings;
                continue;
            }
            if (stage == RetailCensusStage::ScriptStrings)
            {
                if (scriptIndex == result.scriptStringCount)
                {
                    if (!scriptScopesOpen) return RetailCensusError::ZoneStreamInvalid;
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    if (const RetailCensusError error = Pop();
                        error != RetailCensusError::None) return error;
                    scriptScopesOpen = false;
                    assetTableOffset = cursor;
                    const std::uint64_t tableBytes =
                        static_cast<std::uint64_t>(result.assetCount) * ASSET_BYTES;
                    if (tableBytes > limits.maxInflatedPrefixBytes ||
                        cursor > limits.maxInflatedPrefixBytes - static_cast<std::size_t>(tableBytes))
                        return RetailCensusError::InflatedPrefixLimit;
                    if (const RetailCensusError error = Push(4u);
                        error != RetailCensusError::None) return error;
                    ZoneSpan tableSpan;
                    if (const RetailCensusError error = Plan(4u, tableBytes, &tableSpan);
                        error != RetailCensusError::None) return error;
                    result.assetTableBlock4Offset = tableSpan.offset;
                    assetScopeOpen = true;
                    stage = RetailCensusStage::AssetTable;
                    continue;
                }
                const std::uint32_t token = scriptTokens[scriptIndex];
                if (token == 0u)
                {
                    ++scriptIndex;
                    ++report.recordsProcessed;
                    continue;
                }
                if (token != INLINE_POINTER)
                    return RetailCensusError::ScriptStringReferenceUnsupported;
                const auto begin = inflated.begin() + static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    const std::size_t available = inflated.size() - cursor;
                    if (available >= limits.maxScriptStringBytes)
                        return RetailCensusError::ScriptStringTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes = static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes > limits.maxScriptStringBytes)
                    return RetailCensusError::ScriptStringTooLong;
                if (bytes > limits.maxTotalScriptStringBytes ||
                    result.scriptStringBytes > limits.maxTotalScriptStringBytes - bytes)
                    return RetailCensusError::ScriptStringBytesLimit;
                if (recordVisited == 0u)
                {
                    if (const RetailCensusError error = Plan(1u, bytes);
                        error != RetailCensusError::None) return error;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                result.scriptStringBytes += static_cast<std::uint32_t>(bytes);
                cursor += bytes;
                ++scriptIndex;
                ++report.recordsProcessed;
                continue;
            }
            if (stage == RetailCensusStage::AssetTable)
            {
                const std::size_t tableBytes = static_cast<std::size_t>(result.assetCount) * ASSET_BYTES;
                if (assetIndex == 0u && inflated.size() - assetTableOffset < tableBytes)
                {
                    blocked = true;
                    return RetailCensusError::None;
                }
                if (assetIndex == result.assetCount)
                {
                    result.firstBodyIndex = 0u;
                    result.inflatedPrefixBytes = static_cast<std::uint32_t>(
                        assetTableOffset + tableBytes);
                    if (!assetScopeOpen || result.firstBodyType != 5u ||
                        result.firstBodyReference != INLINE_POINTER)
                        return RetailCensusError::FirstAssetUnsupported;
                    if (const RetailCensusError error = Push(0u);
                        error != RetailCensusError::None) return error;
                    ZoneSpan span;
                    if (const RetailCensusError error = Plan(
                            4u, TECHNIQUE_SET_BYTES, &span);
                        error != RetailCensusError::None) return error;
                    result.techniqueSetBlock0Offset = span.offset;
                    stage = RetailCensusStage::TechniqueSet;
                    continue;
                }
                const std::uint8_t *record = inflated.data() +
                    assetTableOffset + static_cast<std::size_t>(assetIndex) * ASSET_BYTES;
                const int visit = visitRecord(ASSET_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::int32_t signedType = ReadS32(record);
                if (signedType < 0 || static_cast<std::uint32_t>(signedType) >= RETAIL_CENSUS_ASSET_TYPE_COUNT)
                    return RetailCensusError::AssetTypeInvalid;
                const std::uint32_t type = static_cast<std::uint32_t>(signedType);
                const std::uint32_t reference = ReadU32(record + 4u);
                if (assetIndex == 0u)
                {
                    result.firstBodyType = type;
                    result.firstBodyReference = reference;
                }
                ++result.typeCounts[type];
                if (reference == 0u) ++result.nullAssetReferences;
                else if (reference == INLINE_POINTER) ++result.inlineAssetReferences;
                else if (reference == SHARED_POINTER) ++result.sharedAssetReferences;
                else ++result.aliasAssetReferences;
                ++assetIndex;
                cursor += ASSET_BYTES;
                ++report.recordsProcessed;
                continue;
            }
            if (stage == RetailCensusStage::TechniqueSet)
            {
                const int visit = visitRecord(TECHNIQUE_SET_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::uint32_t nameToken = ReadU32(record);
                if (nameToken != INLINE_POINTER || record[5] != 0u ||
                    record[6] != 0u || record[7] != 0u || ReadU32(record + 8u) != 0u)
                    return RetailCensusError::TechniqueSetLayoutUnsupported;
                for (std::uint32_t index = 0u; index < techniqueTokens.size(); ++index)
                    techniqueTokens[index] = ReadU32(record + 12u + index * 4u);
                cursor += TECHNIQUE_SET_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Push(4u);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::TechniqueSetName;
                continue;
            }
            if (stage == RetailCensusStage::TechniqueSetName)
            {
                const auto begin = inflated.begin() + static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxTechniqueNameBytes)
                        return RetailCensusError::TechniqueSetNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes = static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes > limits.maxTechniqueNameBytes)
                    return RetailCensusError::TechniqueSetNameTooLong;
                if (recordVisited == 0u)
                {
                    if (const RetailCensusError error = Plan(1u, bytes);
                        error != RetailCensusError::None) return error;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                if (bytes <= 1u) return RetailCensusError::TechniqueSetNameInvalid;
                try
                {
                    result.techniqueSetName.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor), bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += bytes;
                ++report.recordsProcessed;
                std::uint32_t populated = 0u;
                for (std::uint32_t index = 0u; index < techniqueTokens.size(); ++index)
                {
                    const std::uint32_t token = techniqueTokens[index];
                    if (token == 0u) continue;
                    if (token != INLINE_POINTER)
                        return RetailCensusError::TechniqueReferenceUnsupported;
                    if (populated++ == 0u) result.firstTechniqueSlot = index;
                }
                if (populated != 1u) return RetailCensusError::TechniqueReferenceUnsupported;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, TECHNIQUE_HEADER_BYTES, &span);
                    error != RetailCensusError::None) return error;
                result.techniqueBlock4Offset = span.offset;
                stage = RetailCensusStage::Technique;
                continue;
            }
            if (stage == RetailCensusStage::Technique)
            {
                const int visit = visitRecord(TECHNIQUE_HEADER_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::uint32_t techniqueNameToken = ReadU32(record);
                const std::uint32_t passCount =
                    static_cast<std::uint32_t>(record[6]) |
                    static_cast<std::uint32_t>(record[7]) << 8u;
                if (techniqueNameToken != INLINE_POINTER || passCount == 0u)
                    return RetailCensusError::TechniqueLayoutUnsupported;
                if (passCount > limits.maxTechniquePasses)
                    return RetailCensusError::TechniquePassCountLimit;
                result.techniquePassCount = passCount;
                materialPassBytes = passCount * MATERIAL_PASS_BYTES;
                cursor += TECHNIQUE_HEADER_BYTES;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Plan(1u, materialPassBytes);
                    error != RetailCensusError::None) return error;
                stage = RetailCensusStage::MaterialPasses;
                continue;
            }
            if (stage == RetailCensusStage::MaterialPasses)
            {
                const int visit = visitRecord(materialPassBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                pixelShaderToken = ReadU32(record + 8u);
                materialArgumentToken = ReadU32(record + 16u);
                const std::uint32_t argumentCount = static_cast<std::uint32_t>(record[12]) +
                    static_cast<std::uint32_t>(record[13]) +
                    static_cast<std::uint32_t>(record[14]);
                if (result.techniquePassCount != 1u ||
                    ReadU32(record) != INLINE_POINTER ||
                    ReadU32(record + 4u) != INLINE_POINTER ||
                    pixelShaderToken != INLINE_POINTER ||
                    materialArgumentToken != INLINE_POINTER ||
                    record[12] > 64u || record[13] > 64u || record[14] > 64u)
                    return RetailCensusError::MaterialPassUnsupported;
                if (argumentCount == 0u || argumentCount > UINT32_MAX / MATERIAL_ARGUMENT_BYTES)
                    return RetailCensusError::ShaderArgumentLayoutUnsupported;
                result.shaderArgumentCount = argumentCount;
                materialArgumentBytes = argumentCount * MATERIAL_ARGUMENT_BYTES;
                cursor += materialPassBytes;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, VERTEX_DECLARATION_BYTES, &span);
                    error != RetailCensusError::None) return error;
                result.vertexDeclarationBlock4Offset = span.offset;
                stage = RetailCensusStage::VertexDeclaration;
                continue;
            }
            if (stage == RetailCensusStage::VertexDeclaration)
            {
                const int visit = visitRecord(VERTEX_DECLARATION_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                if (record[0] == 0u || record[0] > 16u || record[1] > 1u ||
                    record[2] != 0u || record[3] != 0u ||
                    std::any_of(record + 36u, record + 100u,
                        [](std::uint8_t byte) { return byte != 0u; }))
                    return RetailCensusError::VertexDeclarationUnsupported;
                result.vertexStreamCount = record[0];
                std::copy(record + 4u, record + 36u,
                    result.vertexStreamRouting.begin());
                result.vertexStreamRoutingHash = Fnv1a32(
                    std::span<const std::uint8_t>(record + 4u, 32u));
                result.vertexDeclarationPrepared = true;
                cursor += VERTEX_DECLARATION_BYTES;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, VERTEX_SHADER_BYTES, &span);
                    error != RetailCensusError::None) return error;
                result.vertexShaderBlock4Offset = span.offset;
                stage = RetailCensusStage::VertexShader;
                continue;
            }
            if (stage == RetailCensusStage::VertexShader)
            {
                const int visit = visitRecord(VERTEX_SHADER_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::uint32_t vertexShaderNameToken = ReadU32(record);
                const std::uint32_t runtimeShader = ReadU32(record + 4u);
                const std::uint32_t programToken = ReadU32(record + 8u);
                const std::uint32_t programDwords = ReadU32(record + 12u);
                if (vertexShaderNameToken != INLINE_POINTER || runtimeShader != 0u ||
                    programToken != INLINE_POINTER)
                    return RetailCensusError::VertexShaderLayoutUnsupported;
                if (programDwords == 0u) return RetailCensusError::ShaderProgramSizeInvalid;
                if (programDwords > limits.maxShaderProgramDwords ||
                    programDwords > UINT32_MAX / 4u)
                    return RetailCensusError::ShaderProgramSizeLimit;
                result.vertexShaderProgramDwords = programDwords;
                vertexShaderProgramBytes = programDwords * 4u;
                cursor += VERTEX_SHADER_BYTES;
                ++report.recordsProcessed;
                stage = RetailCensusStage::VertexShaderName;
                continue;
            }
            if (stage == RetailCensusStage::VertexShaderName)
            {
                const auto begin = inflated.begin() + static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxShaderNameBytes)
                        return RetailCensusError::VertexShaderNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes = static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes > limits.maxShaderNameBytes)
                    return RetailCensusError::VertexShaderNameTooLong;
                ZoneSpan nameSpan;
                if (recordVisited == 0u)
                {
                    if (const RetailCensusError error = Plan(1u, bytes, &nameSpan);
                        error != RetailCensusError::None) return error;
                    vertexShaderNameBlock4Offset = nameSpan.offset;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                if (bytes <= 1u) return RetailCensusError::VertexShaderNameInvalid;
                try
                {
                    result.vertexShaderName.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor), bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += bytes;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, vertexShaderProgramBytes, &span);
                    error != RetailCensusError::None) return error;
                result.vertexShaderProgramBlock4Offset = span.offset;
                stage = RetailCensusStage::VertexShaderProgram;
                continue;
            }
            if (stage == RetailCensusStage::VertexShaderProgram)
            {
                const int visit = visitRecord(vertexShaderProgramBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const auto program = std::span<const std::uint8_t>(
                    record, vertexShaderProgramBytes);
                const kisak::web::ShaderDecodeError decodeError =
                    kisak::web::DecodeD3D9Shader(program,
                        {limits.maxShaderProgramDwords, 4096u, 64u,
                            limits.maxShaderNameBytes}, vertexContract);
                if (decodeError != kisak::web::ShaderDecodeError::None)
                    return decodeError == kisak::web::ShaderDecodeError::InvalidVersion
                        ? RetailCensusError::ShaderProgramSignatureInvalid
                        : RetailCensusError::ShaderContractInvalid;
                if (vertexContract.stage != kisak::web::ShaderStage::Vertex)
                    return RetailCensusError::ShaderProgramSignatureInvalid;
                result.vertexShaderProgramHash = vertexContract.programHash;
                result.vertexShaderInstructionCount = vertexContract.instructionCount;
                result.vertexShaderConstantCount = static_cast<std::uint32_t>(
                    vertexContract.constants.size());
                cursor += vertexShaderProgramBytes;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(4u, PIXEL_SHADER_BYTES, &span);
                    error != RetailCensusError::None) return error;
                result.pixelShaderBlock4Offset = span.offset;
                stage = RetailCensusStage::PixelShader;
                continue;
            }
            if (stage == RetailCensusStage::PixelShader)
            {
                const int visit = visitRecord(PIXEL_SHADER_BYTES);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const std::uint32_t nameToken = ReadU32(record);
                const std::uint32_t runtimeShader = ReadU32(record + 4u);
                const std::uint32_t programToken = ReadU32(record + 8u);
                const std::uint32_t programDwords = ReadU32(record + 12u);
                const std::uint32_t expectedNameToken = 0x40000001u +
                    vertexShaderNameBlock4Offset;
                if (pixelShaderToken != INLINE_POINTER || nameToken != expectedNameToken ||
                    runtimeShader != 0u || programToken != INLINE_POINTER)
                    return RetailCensusError::PixelShaderLayoutUnsupported;
                if (programDwords == 0u) return RetailCensusError::ShaderProgramSizeInvalid;
                if (programDwords > limits.maxShaderProgramDwords ||
                    programDwords > UINT32_MAX / 4u)
                    return RetailCensusError::ShaderProgramSizeLimit;
                result.pixelShaderName = result.vertexShaderName;
                result.pixelShaderProgramDwords = programDwords;
                pixelShaderProgramBytes = programDwords * 4u;
                cursor += PIXEL_SHADER_BYTES;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, pixelShaderProgramBytes, &span);
                    error != RetailCensusError::None) return error;
                result.pixelShaderProgramBlock4Offset = span.offset;
                stage = RetailCensusStage::PixelShaderProgram;
                continue;
            }
            if (stage == RetailCensusStage::PixelShaderProgram)
            {
                const int visit = visitRecord(pixelShaderProgramBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                const kisak::web::ShaderDecodeError decodeError =
                    kisak::web::DecodeD3D9Shader(
                        std::span<const std::uint8_t>(record, pixelShaderProgramBytes),
                        {limits.maxShaderProgramDwords, 4096u, 64u,
                            limits.maxShaderNameBytes}, pixelContract);
                if (decodeError != kisak::web::ShaderDecodeError::None ||
                    pixelContract.stage != kisak::web::ShaderStage::Pixel)
                    return RetailCensusError::ShaderContractInvalid;
                result.pixelShaderProgramHash = pixelContract.programHash;
                result.pixelShaderInstructionCount = pixelContract.instructionCount;
                result.pixelShaderConstantCount = static_cast<std::uint32_t>(
                    pixelContract.constants.size());
                kisak::web::WebGL2ShaderSubstitution substitution;
                if (!kisak::web::SelectWebGL2ShaderSubstitution(
                        vertexContract, pixelContract,
                        result.vertexStreamRoutingHash, substitution))
                    return RetailCensusError::ShaderSubstitutionUnsupported;
                try { result.shaderSubstitutionId = substitution.id; }
                catch (...) { return RetailCensusError::AllocationFailed; }
                result.vertexGlslHash = substitution.vertexSourceHash;
                result.fragmentGlslHash = substitution.fragmentSourceHash;
                result.shaderCompatibilitySelected = true;
                cursor += pixelShaderProgramBytes;
                ++report.recordsProcessed;
                ZoneSpan span;
                if (const RetailCensusError error = Plan(
                        4u, materialArgumentBytes, &span);
                    error != RetailCensusError::None) return error;
                result.shaderArgumentsBlock4Offset = span.offset;
                stage = RetailCensusStage::ShaderArguments;
                continue;
            }
            if (stage == RetailCensusStage::ShaderArguments)
            {
                const int visit = visitRecord(materialArgumentBytes);
                if (visit <= 0) return RetailCensusError::None;
                const std::uint8_t *record = inflated.data() + cursor;
                for (std::uint32_t index = 0u; index < result.shaderArgumentCount; ++index)
                {
                    const std::uint16_t type = static_cast<std::uint16_t>(record[index * 8u]) |
                        static_cast<std::uint16_t>(
                            static_cast<std::uint16_t>(record[index * 8u + 1u]) << 8u);
                    if (type != 2u && type != 3u)
                        return RetailCensusError::ShaderArgumentLayoutUnsupported;
                }
                result.shaderArgumentHash = Fnv1a32(
                    std::span<const std::uint8_t>(record, materialArgumentBytes));
                cursor += materialArgumentBytes;
                ++report.recordsProcessed;
                stage = RetailCensusStage::TechniqueName;
                continue;
            }
            if (stage == RetailCensusStage::TechniqueName)
            {
                const auto begin = inflated.begin() + static_cast<std::ptrdiff_t>(cursor);
                const auto terminator = std::find(begin, inflated.end(), 0u);
                if (terminator == inflated.end())
                {
                    if (inflated.size() - cursor >= limits.maxTechniqueNameBytes)
                        return RetailCensusError::TechniqueSetNameTooLong;
                    blocked = true;
                    return RetailCensusError::None;
                }
                const std::size_t bytes = static_cast<std::size_t>(terminator - begin) + 1u;
                if (bytes <= 1u) return RetailCensusError::TechniqueNameInvalid;
                if (bytes > limits.maxTechniqueNameBytes)
                    return RetailCensusError::TechniqueSetNameTooLong;
                if (recordVisited == 0u)
                {
                    if (const RetailCensusError error = Plan(1u, bytes);
                        error != RetailCensusError::None) return error;
                }
                const int visit = visitRecord(bytes);
                if (visit <= 0) return RetailCensusError::None;
                try
                {
                    result.techniqueName.assign(
                        reinterpret_cast<const char *>(inflated.data() + cursor), bytes - 1u);
                }
                catch (...) { return RetailCensusError::AllocationFailed; }
                cursor += bytes;
                ++report.recordsProcessed;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                if (const RetailCensusError error = Pop();
                    error != RetailCensusError::None) return error;
                result.block0HighWaterAtBoundary = arenas.HighWater(0u);
                result.block4CursorAtBoundary = arenas.Cursor(4u);
                result.completedAssetCount = 1u;
                result.techniqueSetPublished = true;
                result.stoppedBeforeAssetBody = false;
                result.stoppedBeforeShaderCreation = false;
                result.unsupportedOperation = nullptr;
                stage = RetailCensusStage::AssetBoundary;
                complete = true;
                return RetailCensusError::None;
            }
            return RetailCensusError::InvalidArgument;
        }
        return RetailCensusError::None;
    }
};

RetailFastfileCensusJob::RetailFastfileCensusJob() noexcept = default;
RetailFastfileCensusJob::~RetailFastfileCensusJob() = default;
RetailFastfileCensusJob::RetailFastfileCensusJob(RetailFastfileCensusJob &&) noexcept = default;
RetailFastfileCensusJob &RetailFastfileCensusJob::operator=(RetailFastfileCensusJob &&) noexcept = default;

RetailCensusError RetailFastfileCensusJob::BeginStreaming(const RetailCensusLimits &limits) noexcept
{
    Reset();
    if (!ValidLimits(limits)) return RetailCensusError::InvalidArgument;
    try { impl_ = std::make_unique<Impl>(); }
    catch (...) { return RetailCensusError::AllocationFailed; }
    impl_->limits = limits;
    const SourceStreamError sourceError = impl_->source.Initialize({
        limits.maxFileBytes, limits.maxSourceChunkBytes});
    if (sourceError != SourceStreamError::None)
    {
        impl_.reset();
        return MapSourceError(sourceError);
    }
    try { impl_->inflated.reserve(limits.maxInflatedPrefixBytes); }
    catch (...) { impl_.reset(); return RetailCensusError::AllocationFailed; }
    progress_ = RetailCensusProgress::Running;
    stage_ = RetailCensusStage::Prefix;
    return RetailCensusError::None;
}

RetailCensusError RetailFastfileCensusJob::FeedSource(
    std::span<const std::uint8_t> bytes, bool final) noexcept
{
    if (!impl_ || progress_ != RetailCensusProgress::Running)
        return RetailCensusError::InvalidArgument;
    return MapSourceError(impl_->source.Feed(bytes, final));
}

RetailCensusStepReport RetailFastfileCensusJob::Step(
    const RetailCensusStepBudget &budget) noexcept
{
    RetailCensusStepReport report{progress_, stage_, failure_};
    if (!impl_ || progress_ != RetailCensusProgress::Running) return report;
    auto fail = [&](RetailCensusError error) {
        if (impl_->streamInitialized)
        {
            inflateEnd(&impl_->stream);
            impl_->streamInitialized = false;
        }
        failure_ = error;
        progress_ = RetailCensusProgress::Failed;
        stage_ = RetailCensusStage::Failed;
        report.progress = progress_;
        report.stage = stage_;
        report.error = error;
    };
    if (budget.maxBytes == 0u || budget.maxBytes > RETAIL_CENSUS_MAX_STEP_BYTES ||
        budget.maxRecords == 0u || budget.maxRecords > RETAIL_CENSUS_MAX_STEP_RECORDS)
    {
        fail(RetailCensusError::InvalidStepBudget);
        return report;
    }

    while (report.sourceBytesConsumed < budget.maxBytes)
    {
        if (impl_->prefixCount < PREFIX_BYTES)
        {
            const std::uint32_t remaining = PREFIX_BYTES - impl_->prefixCount;
            const auto input = impl_->source.Peek(std::min(
                remaining, budget.maxBytes - report.sourceBytesConsumed));
            if (input.empty())
            {
                if (impl_->source.FinalReceived()) fail(RetailCensusError::PrefixTruncated);
                else report.needsSource = true;
                break;
            }
            std::memcpy(impl_->prefix.data() + impl_->prefixCount, input.data(), input.size());
            const auto consumed = static_cast<std::uint32_t>(input.size());
            if (const auto error = MapSourceError(impl_->source.Consume(consumed));
                error != RetailCensusError::None) { fail(error); break; }
            impl_->prefixCount += consumed;
            report.sourceBytesConsumed += consumed;
            if (impl_->prefixCount != PREFIX_BYTES) continue;
            if (const auto error = impl_->ValidatePrefix(); error != RetailCensusError::None)
            { fail(error); break; }
            impl_->stream = {};
            const int initResult = inflateInit(&impl_->stream);
            if (initResult != Z_OK)
            {
                fail(initResult == Z_MEM_ERROR
                    ? RetailCensusError::AllocationFailed
                    : RetailCensusError::InflateInit);
                break;
            }
            impl_->streamInitialized = true;
            stage_ = RetailCensusStage::XFile;
        }

        bool blocked = false;
        bool complete = false;
        if (const auto error = impl_->Parse(budget, report, stage_, blocked, complete);
            error != RetailCensusError::None)
        { fail(error); break; }
        if (complete)
        {
            impl_->result.sourceBytesConsumed = impl_->source.TotalBytesConsumed();
            impl_->result.sourceFeedCount = impl_->source.FeedCount();
            if (impl_->streamInitialized)
            {
                inflateEnd(&impl_->stream);
                impl_->streamInitialized = false;
            }
            progress_ = RetailCensusProgress::Succeeded;
            resultAvailable_ = true;
            report.progress = progress_;
            report.stage = stage_;
            break;
        }
        if (!blocked || report.inflatedBytesProduced >= budget.maxBytes ||
            impl_->inflated.size() == impl_->limits.maxInflatedPrefixBytes)
        {
            if (blocked && impl_->inflated.size() == impl_->limits.maxInflatedPrefixBytes)
                fail(RetailCensusError::InflatedPrefixLimit);
            break;
        }
        const auto input = impl_->source.Peek(
            budget.maxBytes - report.sourceBytesConsumed);
        if (input.empty())
        {
            if (impl_->source.FinalReceived()) fail(RetailCensusError::InflateTruncated);
            else report.needsSource = true;
            break;
        }
        const std::uint32_t outputCapacity = std::min<std::uint32_t>(
            budget.maxBytes - report.inflatedBytesProduced,
            impl_->limits.maxInflatedPrefixBytes -
                static_cast<std::uint32_t>(impl_->inflated.size()));
        if (outputCapacity == 0u) { fail(RetailCensusError::InflatedPrefixLimit); break; }
        const std::size_t oldSize = impl_->inflated.size();
        try { impl_->inflated.resize(oldSize + outputCapacity); }
        catch (...) { fail(RetailCensusError::AllocationFailed); break; }
        impl_->stream.next_in = const_cast<Bytef *>(
            reinterpret_cast<const Bytef *>(input.data()));
        impl_->stream.avail_in = static_cast<uInt>(input.size());
        impl_->stream.next_out = reinterpret_cast<Bytef *>(impl_->inflated.data() + oldSize);
        impl_->stream.avail_out = outputCapacity;
        const uInt inputBefore = impl_->stream.avail_in;
        const uInt outputBefore = impl_->stream.avail_out;
        const int inflateResult = inflate(&impl_->stream, Z_NO_FLUSH);
        const std::uint32_t consumed = inputBefore - impl_->stream.avail_in;
        const std::uint32_t produced = outputBefore - impl_->stream.avail_out;
        impl_->inflated.resize(oldSize + produced);
        if (consumed != 0u)
        {
            if (const auto error = MapSourceError(impl_->source.Consume(consumed));
                error != RetailCensusError::None) { fail(error); break; }
        }
        report.sourceBytesConsumed += consumed;
        report.inflatedBytesProduced += produced;
        if (inflateResult == Z_STREAM_END)
        {
            inflateEnd(&impl_->stream);
            impl_->streamInitialized = false;
            impl_->streamEnded = true;
        }
        else if (inflateResult == Z_MEM_ERROR) { fail(RetailCensusError::AllocationFailed); break; }
        else if (inflateResult != Z_OK && inflateResult != Z_BUF_ERROR)
        { fail(RetailCensusError::InflateData); break; }
        if (consumed == 0u && produced == 0u)
        {
            if (impl_->source.FinalReceived()) fail(RetailCensusError::InflateTruncated);
            else report.needsSource = true;
            break;
        }
        if (impl_->streamEnded && blocked)
        {
            bool parseBlocked = false;
            bool parseComplete = false;
            if (const auto error = impl_->Parse(
                    budget, report, stage_, parseBlocked, parseComplete);
                error != RetailCensusError::None) fail(error);
            else if (parseComplete)
            {
                impl_->result.sourceBytesConsumed = impl_->source.TotalBytesConsumed();
                impl_->result.sourceFeedCount = impl_->source.FeedCount();
                progress_ = RetailCensusProgress::Succeeded;
                resultAvailable_ = true;
            }
            else if (parseBlocked) fail(RetailCensusError::RecordTruncated);
            break;
        }
    }
    report.progress = progress_;
    report.stage = stage_;
    report.error = failure_;
    return report;
}

RetailCensusProgress RetailFastfileCensusJob::Progress() const noexcept { return progress_; }
RetailCensusStage RetailFastfileCensusJob::Stage() const noexcept { return stage_; }
RetailCensusError RetailFastfileCensusJob::Failure() const noexcept { return failure_; }
bool RetailFastfileCensusJob::NeedsSource() const noexcept
{
    return impl_ && progress_ == RetailCensusProgress::Running && impl_->source.NeedsSource();
}
std::uint64_t RetailFastfileCensusJob::SourceBytesReceived() const noexcept
{
    return impl_ ? impl_->source.TotalBytesReceived() : 0u;
}
bool RetailFastfileCensusJob::TakeResult(RetailFastfileCensus &destination) noexcept
{
    if (!impl_ || !resultAvailable_ || progress_ != RetailCensusProgress::Succeeded)
        return false;
    destination = impl_->result;
    resultAvailable_ = false;
    return true;
}
void RetailFastfileCensusJob::Reset() noexcept
{
    impl_.reset();
    progress_ = RetailCensusProgress::NotStarted;
    stage_ = RetailCensusStage::NotStarted;
    failure_ = RetailCensusError::None;
    resultAvailable_ = false;
}

} // namespace kisak::fastfile
