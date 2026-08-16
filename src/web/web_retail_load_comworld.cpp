#include <web/web_retail_load_comworld.h>

#include <qcommon/com_world_types.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace kisak::fastfile
{
namespace
{
constexpr std::uint32_t INLINE_POINTER = UINT32_MAX;
constexpr std::uint32_t SHARED_POINTER = UINT32_MAX - 1u;
constexpr std::uint32_t COMWORLD_BYTES = 16u;
constexpr std::uint32_t COM_PRIMARY_LIGHT_BYTES = 68u;

std::uint32_t ReadU32(const std::uint8_t *value) noexcept
{
    return static_cast<std::uint32_t>(value[0]) |
        static_cast<std::uint32_t>(value[1]) << 8u |
        static_cast<std::uint32_t>(value[2]) << 16u |
        static_cast<std::uint32_t>(value[3]) << 24u;
}

std::int32_t ReadS32(const std::uint8_t *value) noexcept
{
    return std::bit_cast<std::int32_t>(ReadU32(value));
}

float ReadF32(const std::uint8_t *value) noexcept
{
    return std::bit_cast<float>(ReadU32(value));
}

RetailCensusError RegistryError(ZoneRegistryError error) noexcept
{
    if (error == ZoneRegistryError::None) return RetailCensusError::None;
    return error == ZoneRegistryError::AllocationFailed
        ? RetailCensusError::AllocationFailed
        : RetailCensusError::AssetRegistryInvalid;
}

bool CheckedTableAliasOffset(
    const RetailFastfileCensus &result,
    std::uint32_t assetIndex,
    std::uint32_t &offset) noexcept
{
    const std::uint64_t value =
        static_cast<std::uint64_t>(result.assetTableBlock4Offset) +
        static_cast<std::uint64_t>(assetIndex) * 8u + 4u;
    if (value > UINT32_MAX) return false;
    offset = static_cast<std::uint32_t>(value);
    return true;
}

bool CheckedLightBytes(
    std::uint32_t count,
    const RetailCensusLimits &limits,
    std::uint32_t &bytes) noexcept
{
    if (count > limits.maxComWorldPrimaryLights) return false;
    const std::uint64_t total =
        static_cast<std::uint64_t>(count) * COM_PRIMARY_LIGHT_BYTES;
    if (total > UINT32_MAX || total > limits.maxComWorldPayloadBytes)
        return false;
    bytes = static_cast<std::uint32_t>(total);
    return true;
}

void DecodeLight(const std::uint8_t *source, ComPrimaryLight &light) noexcept
{
    light = {};
    light.type = source[0u];
    light.canUseShadowMap = source[1u];
    light.exponent = source[2u];
    light.unused = source[3u];
    for (std::size_t index = 0u; index < 3u; ++index)
    {
        light.color[index] = ReadF32(source + 4u + index * 4u);
        light.dir[index] = ReadF32(source + 16u + index * 4u);
        light.origin[index] = ReadF32(source + 28u + index * 4u);
    }
    light.radius = ReadF32(source + 40u);
    light.cosHalfFovOuter = ReadF32(source + 44u);
    light.cosHalfFovInner = ReadF32(source + 48u);
    light.cosHalfFovExpanded = ReadF32(source + 52u);
    light.rotationLimit = ReadF32(source + 56u);
    light.translationLimit = ReadF32(source + 60u);
}
} // namespace

struct CanonicalComWorldStorage
{
    std::shared_ptr<std::string> name;
    std::vector<ComPrimaryLight> primaryLights;
    std::shared_ptr<ComPrimaryLight> emptyPrimaryLights;
    std::vector<std::shared_ptr<std::string>> lightDefNames;
};

struct RetailComWorldLoadFamily::State
{
    enum class Phase : std::uint8_t
    {
        Header,
        Name,
        PrimaryLights,
        LightNames,
        Publish,
    };

    RetailComWorldLoadProgress progress = RetailComWorldLoadProgress::Idle;
    Phase phase = Phase::Header;
    std::size_t resultIndex = 0u;
    std::size_t lightIndex = 0u;
    ZoneSpan tableAlias{};
    ZoneSpan insertAlias{};
    ZoneSpan headerSpan{};
    ZoneSpan primaryLightsSpan{};
    ZoneSpan stringSpan{};
    ZoneLoadKind plannedKind = ZoneLoadKind::Immediate;
    std::uint32_t nameToken = 0u;
    std::uint32_t primaryLightsToken = 0u;
    std::uint32_t primaryLightCount = 0u;
    std::uint32_t primaryLightBytes = 0u;
    std::uint32_t plannedStringBytes = 0u;
    std::uint32_t totalStringBytes = 0u;
    bool hasInsertAlias = false;
    bool block4Pushed = false;
    bool lightsPlanned = false;
    bool stringPlanned = false;
    std::vector<std::uint32_t> lightNameTokens;
};

namespace
{
RetailPublishedComWorld &Entry(
    RetailLoadContext &context,
    const RetailComWorldLoadFamily::State &state) noexcept
{
    return context.Ownership().worldComWorlds[state.resultIndex];
}

RetailCensusError AddPayload(
    RetailLoadContext &context,
    RetailPublishedComWorld &entry,
    std::uint32_t bytes) noexcept
{
    const std::uint64_t total =
        static_cast<std::uint64_t>(entry.payloadBytes) + bytes;
    if (total > UINT32_MAX ||
        total > context.LoaderLimits().maxComWorldPayloadBytes)
        return RetailCensusError::ComWorldPayloadLimit;
    entry.payloadBytes = static_cast<std::uint32_t>(total);
    return RetailCensusError::None;
}

RetailCensusError LoadXString(
    RetailLoadContext &context,
    RetailComWorldLoadFamily::State &state,
    std::uint32_t token,
    std::uint32_t perStringLimit,
    RetailCensusError invalidError,
    RetailCensusError tooLongError,
    std::shared_ptr<std::string> &output,
    std::uint32_t &block4Offset) noexcept
{
    output.reset();
    block4Offset = UINT32_MAX;
    if (token == 0u) return RetailCensusError::None;
    if (token != INLINE_POINTER)
    {
        if (!context.ResolveXString(token, output, block4Offset) || !output)
            return invalidError;
        if (output->size() >= perStringLimit) return tooLongError;
        return RetailCensusError::None;
    }

    if (!state.stringPlanned)
    {
        const auto tail = context.InflatedTail();
        const std::size_t stringLimit =
            static_cast<std::size_t>(perStringLimit);
        const std::size_t scanBytes = tail.size() <= stringLimit
            ? tail.size() : stringLimit + 1u;
        const auto terminator = std::find(tail.begin(), tail.begin() + scanBytes, 0u);
        if (terminator == tail.begin() + scanBytes)
        {
            if (tail.size() > perStringLimit) return tooLongError;
            context.BlockForInflatedInput();
            return RetailCensusError::None;
        }
        const std::size_t length =
            static_cast<std::size_t>(terminator - tail.begin()) + 1u;
        if (length > perStringLimit || length > UINT32_MAX) return tooLongError;
        state.plannedStringBytes = static_cast<std::uint32_t>(length);
        if (const RetailCensusError error = context.PlanStream(
                1u, state.plannedStringBytes, &state.stringSpan,
                &state.plannedKind);
            error != RetailCensusError::None)
            return error;
        if (state.plannedKind != ZoneLoadKind::Immediate)
            return RetailCensusError::ZoneStreamInvalid;
        state.stringPlanned = true;
    }

    if (context.VisitRecord(state.plannedStringBytes) != RetailLoadVisit::Complete)
        return RetailCensusError::None;
    const auto tail = context.InflatedTail();
    if (tail.size() < state.plannedStringBytes)
    {
        context.BlockForInflatedInput();
        return RetailCensusError::None;
    }
    try
    {
        output = std::make_shared<std::string>(
            reinterpret_cast<const char *>(tail.data()),
            state.plannedStringBytes - 1u);
    }
    catch (...) { return RetailCensusError::AllocationFailed; }
    if (const RetailCensusError error = context.RememberXString(
            token, state.stringSpan, output);
        error != RetailCensusError::None)
        return error;
    const std::uint64_t total =
        static_cast<std::uint64_t>(state.totalStringBytes) +
        state.plannedStringBytes;
    if (total > context.LoaderLimits().maxComWorldStringBytes ||
        total > UINT32_MAX)
        return RetailCensusError::ComWorldStringBytesLimit;
    state.totalStringBytes = static_cast<std::uint32_t>(total);
    block4Offset = state.stringSpan.offset;
    context.ConsumeRecord(state.plannedStringBytes);
    state.stringPlanned = false;
    state.plannedStringBytes = 0u;
    return AddPayload(context, Entry(context, state),
        static_cast<std::uint32_t>(output->size() + 1u));
}
} // namespace

RetailComWorldLoadFamily::RetailComWorldLoadFamily() noexcept = default;
RetailComWorldLoadFamily::~RetailComWorldLoadFamily() = default;

RetailCensusError RetailComWorldLoadFamily::Begin(
    RetailLoadContext &context,
    std::uint32_t assetIndex,
    std::uint32_t serializedReference) noexcept
{
    if (state_ && state_->progress == RetailComWorldLoadProgress::Running)
        return RetailCensusError::InvalidArgument;
    RetailFastfileCensus &result = context.Ownership();
    if (result.worldComWorlds.size() >= context.LoaderLimits().maxComWorlds)
        return RetailCensusError::ComWorldCollectionLimit;

    std::unique_ptr<State> next(new (std::nothrow) State());
    if (!next) return RetailCensusError::AllocationFailed;
    try { result.worldComWorlds.emplace_back(); }
    catch (...) { return RetailCensusError::AllocationFailed; }
    next->resultIndex = result.worldComWorlds.size() - 1u;
    RetailPublishedComWorld &entry = result.worldComWorlds.back();
    entry.assetIndex = assetIndex;
    entry.serializedReference = serializedReference;

    if (serializedReference == 0u)
    {
        entry.nullRoot = true;
        entry.boundaryInflatedOffset =
            static_cast<std::uint32_t>(context.InflatedCursor());
        next->progress = RetailComWorldLoadProgress::Complete;
        state_ = std::move(next);
        return RetailCensusError::None;
    }

    if (serializedReference != INLINE_POINTER &&
        serializedReference != SHARED_POINTER)
    {
        if (context.ResolveAssetAlias(serializedReference, ASSET_TYPE_COMWORLD,
                entry.identity) != ZoneRegistryError::None)
            return RetailCensusError::ComWorldAliasInvalid;
        const auto found = std::find_if(
            result.worldComWorlds.begin(), result.worldComWorlds.end() - 1,
            [&](const RetailPublishedComWorld &candidate) {
                return candidate.published && candidate.identity == entry.identity &&
                    candidate.asset && candidate.storage;
            });
        if (found == result.worldComWorlds.end() - 1 ||
            context.FindCanonicalAsset(ASSET_TYPE_COMWORLD, entry.identity) !=
                found->asset.get())
            return RetailCensusError::ComWorldAliasInvalid;
        entry.asset = found->asset;
        entry.storage = found->storage;
        entry.headerBlock0Offset = found->headerBlock0Offset;
        entry.nameBlock4Offset = found->nameBlock4Offset;
        entry.primaryLightsBlock4Offset = found->primaryLightsBlock4Offset;
        entry.lightDefNameBlock4Offsets = found->lightDefNameBlock4Offsets;
        entry.payloadBytes = found->payloadBytes;
        entry.pointerAlias = true;
        entry.published = true;
        entry.boundaryInflatedOffset =
            static_cast<std::uint32_t>(context.InflatedCursor());
        next->progress = RetailComWorldLoadProgress::Complete;
        state_ = std::move(next);
        return RetailCensusError::None;
    }

    std::uint32_t tableOffset = 0u;
    if (!CheckedTableAliasOffset(result, assetIndex, tableOffset))
        return RetailCensusError::ComWorldAliasInvalid;
    next->tableAlias = {4u, tableOffset, 4u};
    if (const RetailCensusError error = RegistryError(
            context.Assets().ReserveAlias(
                next->tableAlias, ASSET_TYPE_COMWORLD));
        error != RetailCensusError::None)
        return error;
    if (const RetailCensusError error = context.PushStream(0u);
        error != RetailCensusError::None)
        return error;
    if (const RetailCensusError error = context.PlanStream(
            4u, COMWORLD_BYTES, &next->headerSpan, nullptr);
        error != RetailCensusError::None)
        return error;
    entry.headerBlock0Offset = next->headerSpan.offset;
    next->hasInsertAlias = serializedReference == SHARED_POINTER;
    if (next->hasInsertAlias)
    {
        if (const RetailCensusError error = context.PushStream(4u);
            error != RetailCensusError::None)
            return error;
        if (const RetailCensusError error = context.PlanStream(
                4u, 4u, &next->insertAlias, nullptr);
            error != RetailCensusError::None)
            return error;
        if (const RetailCensusError error = context.PopStream();
            error != RetailCensusError::None)
            return error;
        if (const RetailCensusError error = RegistryError(
                context.Assets().ReserveAlias(
                    next->insertAlias, ASSET_TYPE_COMWORLD));
            error != RetailCensusError::None)
            return error;
        entry.insertPointerBlock4Offset = next->insertAlias.offset;
    }
    try
    {
        entry.storage = std::make_shared<CanonicalComWorldStorage>();
        entry.asset = std::make_shared<ComWorld>();
    }
    catch (...) { return RetailCensusError::AllocationFailed; }
    *entry.asset = {};
    if (const RetailCensusError error = context.Trace(
            kisak::database::SemanticTraceEventKind::AssetBegin,
            ASSET_TYPE_COMWORLD, assetIndex, 0u,
            static_cast<std::uint32_t>(context.InflatedCursor()),
            next->headerSpan, {}, next->tableAlias);
        error != RetailCensusError::None)
        return error;
    next->progress = RetailComWorldLoadProgress::Running;
    state_ = std::move(next);
    return RetailCensusError::None;
}

RetailCensusError RetailComWorldLoadFamily::Step(
    RetailLoadContext &context) noexcept
{
    if (!state_ || state_->progress != RetailComWorldLoadProgress::Running)
        return RetailCensusError::InvalidArgument;
    State &state = *state_;
    RetailPublishedComWorld &entry = Entry(context, state);
    CanonicalComWorldStorage &storage = *entry.storage;
    ComWorld &world = *entry.asset;

    if (state.phase == State::Phase::Header)
    {
        if (context.VisitRecord(COMWORLD_BYTES) != RetailLoadVisit::Complete)
            return RetailCensusError::None;
        const auto tail = context.InflatedTail();
        if (tail.size() < COMWORLD_BYTES)
        {
            context.BlockForInflatedInput();
            return RetailCensusError::None;
        }
        const std::uint8_t *record = tail.data();
        state.nameToken = ReadU32(record);
        world.isInUse = ReadS32(record + 4u);
        state.primaryLightCount = ReadU32(record + 8u);
        state.primaryLightsToken = ReadU32(record + 12u);
        if (!CheckedLightBytes(state.primaryLightCount,
                context.LoaderLimits(), state.primaryLightBytes))
            return RetailCensusError::ComWorldLightCountInvalid;
        world.primaryLightCount = state.primaryLightCount;
        context.ConsumeRecord(COMWORLD_BYTES);
        if (const RetailCensusError error = context.PushStream(4u);
            error != RetailCensusError::None)
            return error;
        state.block4Pushed = true;
        state.phase = State::Phase::Name;
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::Name)
    {
        const RetailCensusError error = LoadXString(
            context, state, state.nameToken,
            context.LoaderLimits().maxComWorldNameBytes,
            RetailCensusError::ComWorldNameInvalid,
            RetailCensusError::ComWorldNameTooLong,
            storage.name, entry.nameBlock4Offset);
        if (error != RetailCensusError::None) return error;
        if (state.stringPlanned || (state.nameToken == INLINE_POINTER &&
                !storage.name))
            return RetailCensusError::None;
        if (!storage.name || storage.name->empty())
            return RetailCensusError::ComWorldNameInvalid;
        world.name = storage.name->c_str();
        state.phase = State::Phase::PrimaryLights;
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::PrimaryLights)
    {
        if (state.primaryLightsToken == 0u)
        {
            world.primaryLights = nullptr;
            state.phase = State::Phase::Publish;
            return RetailCensusError::None;
        }
        if (!state.lightsPlanned)
        {
            if (const RetailCensusError error = context.PlanStream(
                    4u, state.primaryLightBytes, &state.primaryLightsSpan,
                    &state.plannedKind);
                error != RetailCensusError::None)
                return error;
            if (state.plannedKind != ZoneLoadKind::Immediate)
                return RetailCensusError::ZoneStreamInvalid;
            entry.primaryLightsBlock4Offset = state.primaryLightsSpan.offset;
            if (const RetailCensusError error = AddPayload(
                    context, entry, state.primaryLightBytes);
                error != RetailCensusError::None)
                return error;
            state.lightsPlanned = true;
        }
        if (context.VisitRecord(state.primaryLightBytes) !=
            RetailLoadVisit::Complete)
            return RetailCensusError::None;
        const auto tail = context.InflatedTail();
        if (tail.size() < state.primaryLightBytes)
        {
            context.BlockForInflatedInput();
            return RetailCensusError::None;
        }
        try
        {
            storage.primaryLights.resize(state.primaryLightCount);
            if (state.primaryLightCount == 0u)
                storage.emptyPrimaryLights =
                    std::make_shared<ComPrimaryLight>();
            storage.lightDefNames.resize(state.primaryLightCount);
            state.lightNameTokens.resize(state.primaryLightCount);
            entry.lightDefNameBlock4Offsets.assign(
                state.primaryLightCount, UINT32_MAX);
        }
        catch (...) { return RetailCensusError::AllocationFailed; }
        for (std::size_t index = 0u; index < state.primaryLightCount; ++index)
        {
            const std::uint8_t *record =
                tail.data() + index * COM_PRIMARY_LIGHT_BYTES;
            DecodeLight(record, storage.primaryLights[index]);
            state.lightNameTokens[index] = ReadU32(record + 64u);
        }
        context.ConsumeRecord(state.primaryLightBytes);
        world.primaryLights = storage.primaryLights.empty()
            ? storage.emptyPrimaryLights.get() : storage.primaryLights.data();
        state.phase = State::Phase::LightNames;
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::LightNames)
    {
        if (state.lightIndex == state.primaryLightCount)
        {
            state.phase = State::Phase::Publish;
            return RetailCensusError::None;
        }
        const std::size_t index = state.lightIndex;
        const RetailCensusError error = LoadXString(
            context, state, state.lightNameTokens[index],
            context.LoaderLimits().maxComWorldLightDefNameBytes,
            RetailCensusError::ComWorldLightNameInvalid,
            RetailCensusError::ComWorldLightNameTooLong,
            storage.lightDefNames[index],
            entry.lightDefNameBlock4Offsets[index]);
        if (error != RetailCensusError::None) return error;
        if (state.stringPlanned ||
            (state.lightNameTokens[index] == INLINE_POINTER &&
             !storage.lightDefNames[index]))
            return RetailCensusError::None;
        storage.primaryLights[index].defName = storage.lightDefNames[index]
            ? storage.lightDefNames[index]->c_str() : nullptr;
        ++state.lightIndex;
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::Publish)
    {
        if (state.block4Pushed)
        {
            if (const RetailCensusError error = context.PopStream();
                error != RetailCensusError::None)
                return error;
            state.block4Pushed = false;
        }
        if (const RetailCensusError error = context.PopStream();
            error != RetailCensusError::None)
            return error;
        if (const RetailCensusError error = RegistryError(
                context.Assets().RegisterAsset(
                    ASSET_TYPE_COMWORLD, entry.assetIndex,
                    *storage.name, entry.identity));
            error != RetailCensusError::None)
            return error;
        if (const RetailCensusError error = RegistryError(
                context.Assets().PublishAlias(
                    state.tableAlias, entry.identity));
            error != RetailCensusError::None)
            return error;
        if (state.hasInsertAlias)
            if (const RetailCensusError error = RegistryError(
                    context.Assets().PublishAlias(
                        state.insertAlias, entry.identity));
                error != RetailCensusError::None)
                return error;
        entry.boundaryInflatedOffset =
            static_cast<std::uint32_t>(context.InflatedCursor());
        entry.published = true;
        if (const RetailCensusError error = context.Trace(
                kisak::database::SemanticTraceEventKind::AssetPublish,
                ASSET_TYPE_COMWORLD, entry.assetIndex, entry.identity,
                entry.boundaryInflatedOffset, state.headerSpan,
                *storage.name, state.tableAlias);
            error != RetailCensusError::None)
            return error;
        state.progress = RetailComWorldLoadProgress::Complete;
        return RetailCensusError::None;
    }
    return RetailCensusError::ComWorldLayoutUnsupported;
}

RetailComWorldLoadProgress RetailComWorldLoadFamily::Progress() const noexcept
{
    return state_ ? state_->progress : RetailComWorldLoadProgress::Idle;
}

void RetailComWorldLoadFamily::Reset() noexcept
{
    state_.reset();
}

} // namespace kisak::fastfile
