#include <web/web_retail_load_lightdef.h>

#include <gfx_d3d/gfx_light_types.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <new>
#include <string>
#include <utility>

namespace kisak::fastfile
{
namespace
{
constexpr std::uint32_t INLINE_POINTER = UINT32_MAX;
constexpr std::uint32_t SHARED_POINTER = UINT32_MAX - 1u;
constexpr std::uint32_t ASSET_TYPE_LIGHT_DEF_VALUE = 17u;
constexpr std::uint32_t LIGHT_DEF_BYTES = 16u;

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
} // namespace

struct CanonicalLightDefStorage
{
    std::shared_ptr<std::string> name;
    std::shared_ptr<GfxImage> attenuationImage;
};

struct RetailLightDefLoadFamily::State
{
    enum class Phase : std::uint8_t
    {
        Header,
        Name,
        Image,
        Publish,
    };

    RetailLightDefLoadProgress progress = RetailLightDefLoadProgress::Idle;
    Phase phase = Phase::Header;
    std::size_t resultIndex = 0u;
    ZoneSpan tableAlias{};
    ZoneSpan insertAlias{};
    ZoneSpan headerSpan{};
    ZoneSpan stringSpan{};
    std::uint32_t nameToken = 0u;
    std::uint32_t imageToken = 0u;
    std::uint32_t plannedStringBytes = 0u;
    bool hasInsertAlias = false;
    bool block4Pushed = false;
    bool stringPlanned = false;
    bool imageStarted = false;
};

namespace
{
RetailPublishedLightDef &Entry(
    RetailLoadContext &context,
    const RetailLightDefLoadFamily::State &state) noexcept
{
    return context.Ownership().worldLightDefs[state.resultIndex];
}

RetailCensusError LoadName(
    RetailLoadContext &context,
    RetailLightDefLoadFamily::State &state,
    RetailPublishedLightDef &entry) noexcept
{
    CanonicalLightDefStorage &storage = *entry.storage;
    storage.name.reset();
    entry.nameBlock4Offset = UINT32_MAX;
    if (state.nameToken == 0u || state.nameToken == SHARED_POINTER)
        return RetailCensusError::LightDefNameInvalid;
    if (state.nameToken != INLINE_POINTER)
    {
        if (!context.ResolveXString(
                state.nameToken, storage.name, entry.nameBlock4Offset) ||
            !storage.name || storage.name->empty())
            return RetailCensusError::LightDefNameInvalid;
        if (storage.name->size() >=
            context.LoaderLimits().maxLightDefNameBytes)
            return RetailCensusError::LightDefNameTooLong;
        return RetailCensusError::None;
    }

    if (!state.stringPlanned)
    {
        const auto tail = context.InflatedTail();
        const std::size_t limit =
            context.LoaderLimits().maxLightDefNameBytes;
        const std::size_t scanBytes = tail.size() <= limit
            ? tail.size() : limit + 1u;
        const auto terminator = std::find(
            tail.begin(), tail.begin() + scanBytes, 0u);
        if (terminator == tail.begin() + scanBytes)
        {
            if (tail.size() > limit)
                return RetailCensusError::LightDefNameTooLong;
            context.BlockForInflatedInput();
            return RetailCensusError::None;
        }
        const std::size_t length =
            static_cast<std::size_t>(terminator - tail.begin()) + 1u;
        if (length <= 1u) return RetailCensusError::LightDefNameInvalid;
        if (length > limit || length > UINT32_MAX)
            return RetailCensusError::LightDefNameTooLong;
        state.plannedStringBytes = static_cast<std::uint32_t>(length);
        ZoneLoadKind kind = ZoneLoadKind::Immediate;
        if (const RetailCensusError error = context.PlanStream(
                1u, state.plannedStringBytes, &state.stringSpan, &kind);
            error != RetailCensusError::None)
            return error;
        if (kind != ZoneLoadKind::Immediate)
            return RetailCensusError::ZoneStreamInvalid;
        state.stringPlanned = true;
    }
    if (context.VisitRecord(state.plannedStringBytes) !=
        RetailLoadVisit::Complete)
        return RetailCensusError::None;
    const auto tail = context.InflatedTail();
    if (tail.size() < state.plannedStringBytes)
    {
        context.BlockForInflatedInput();
        return RetailCensusError::None;
    }
    try
    {
        storage.name = std::make_shared<std::string>(
            reinterpret_cast<const char *>(tail.data()),
            state.plannedStringBytes - 1u);
    }
    catch (...) { return RetailCensusError::AllocationFailed; }
    entry.nameBlock4Offset = state.stringSpan.offset;
    if (const RetailCensusError error = context.RememberXString(
            state.nameToken, state.stringSpan, storage.name);
        error != RetailCensusError::None)
        return error;
    context.ConsumeRecord(state.plannedStringBytes);
    state.stringPlanned = false;
    return RetailCensusError::None;
}
} // namespace

RetailLightDefLoadFamily::RetailLightDefLoadFamily() noexcept = default;
RetailLightDefLoadFamily::~RetailLightDefLoadFamily() = default;

RetailCensusError RetailLightDefLoadFamily::Begin(
    RetailLoadContext &context,
    std::uint32_t assetIndex,
    std::uint32_t serializedReference) noexcept
{
    if (state_ && state_->progress == RetailLightDefLoadProgress::Running)
        return RetailCensusError::InvalidArgument;
    RetailFastfileCensus &result = context.Ownership();
    if (result.worldLightDefs.size() >=
        context.LoaderLimits().maxLightDefs)
        return RetailCensusError::LightDefCollectionLimit;
    std::unique_ptr<State> next(new (std::nothrow) State());
    if (!next) return RetailCensusError::AllocationFailed;
    try { result.worldLightDefs.emplace_back(); }
    catch (...) { return RetailCensusError::AllocationFailed; }
    next->resultIndex = result.worldLightDefs.size() - 1u;
    RetailPublishedLightDef &entry = result.worldLightDefs.back();
    entry.assetIndex = assetIndex;
    entry.serializedReference = serializedReference;

    if (serializedReference == 0u)
    {
        entry.nullRoot = true;
        entry.boundaryInflatedOffset =
            static_cast<std::uint32_t>(context.InflatedCursor());
        next->progress = RetailLightDefLoadProgress::Complete;
        state_ = std::move(next);
        return RetailCensusError::None;
    }
    if (serializedReference != INLINE_POINTER &&
        serializedReference != SHARED_POINTER)
    {
        if (context.ResolveAssetAlias(serializedReference,
                ASSET_TYPE_LIGHT_DEF_VALUE, entry.identity) !=
            ZoneRegistryError::None)
            return RetailCensusError::LightDefAliasInvalid;
        const auto found = std::find_if(
            result.worldLightDefs.begin(), result.worldLightDefs.end() - 1,
            [&](const RetailPublishedLightDef &candidate) {
                return candidate.published &&
                    candidate.identity == entry.identity &&
                    candidate.asset && candidate.storage;
            });
        if (found == result.worldLightDefs.end() - 1 ||
            context.FindCanonicalAsset(
                ASSET_TYPE_LIGHT_DEF_VALUE, entry.identity) !=
                found->asset.get())
            return RetailCensusError::LightDefAliasInvalid;
        entry = *found;
        entry.assetIndex = assetIndex;
        entry.serializedReference = serializedReference;
        entry.pointerAlias = true;
        entry.boundaryInflatedOffset =
            static_cast<std::uint32_t>(context.InflatedCursor());
        next->progress = RetailLightDefLoadProgress::Complete;
        state_ = std::move(next);
        return RetailCensusError::None;
    }

    std::uint32_t tableOffset = 0u;
    if (!CheckedTableAliasOffset(result, assetIndex, tableOffset))
        return RetailCensusError::LightDefAliasInvalid;
    next->tableAlias = {4u, tableOffset, 4u};
    if (const RetailCensusError error = RegistryError(
            context.Assets().ReserveAlias(
                next->tableAlias, ASSET_TYPE_LIGHT_DEF_VALUE));
        error != RetailCensusError::None)
        return error;
    if (const RetailCensusError error = context.PushStream(0u);
        error != RetailCensusError::None)
        return error;
    if (const RetailCensusError error = context.PlanStream(
            4u, LIGHT_DEF_BYTES, &next->headerSpan, nullptr);
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
                    next->insertAlias, ASSET_TYPE_LIGHT_DEF_VALUE));
            error != RetailCensusError::None)
            return error;
        entry.insertPointerBlock4Offset = next->insertAlias.offset;
    }
    try
    {
        entry.storage = std::make_shared<CanonicalLightDefStorage>();
        entry.asset = std::make_shared<GfxLightDef>();
    }
    catch (...) { return RetailCensusError::AllocationFailed; }
    std::memset(entry.asset.get(), 0, sizeof(GfxLightDef));
    if (const RetailCensusError error = context.Trace(
            kisak::database::SemanticTraceEventKind::AssetBegin,
            ASSET_TYPE_LIGHT_DEF_VALUE, assetIndex, 0u,
            static_cast<std::uint32_t>(context.InflatedCursor()),
            next->headerSpan, {}, next->tableAlias);
        error != RetailCensusError::None)
        return error;
    next->progress = RetailLightDefLoadProgress::Running;
    state_ = std::move(next);
    return RetailCensusError::None;
}

RetailCensusError RetailLightDefLoadFamily::Step(
    RetailLoadContext &context) noexcept
{
    if (!state_ || state_->progress != RetailLightDefLoadProgress::Running)
        return RetailCensusError::InvalidArgument;
    State &state = *state_;
    RetailPublishedLightDef &entry = Entry(context, state);
    CanonicalLightDefStorage &storage = *entry.storage;
    GfxLightDef &lightDef = *entry.asset;

    if (state.phase == State::Phase::Header)
    {
        if (context.VisitRecord(LIGHT_DEF_BYTES) != RetailLoadVisit::Complete)
            return RetailCensusError::None;
        const auto tail = context.InflatedTail();
        if (tail.size() < LIGHT_DEF_BYTES)
        {
            context.BlockForInflatedInput();
            return RetailCensusError::None;
        }
        const std::uint8_t *record = tail.data();
        state.nameToken = ReadU32(record);
        state.imageToken = ReadU32(record + 4u);
        lightDef.attenuation.samplerState = record[8u];
        lightDef.attenuation.padding[0u] = record[9u];
        lightDef.attenuation.padding[1u] = record[10u];
        lightDef.attenuation.padding[2u] = record[11u];
        lightDef.lmapLookupStart = ReadS32(record + 12u);
        context.ConsumeRecord(LIGHT_DEF_BYTES);
        if (const RetailCensusError error = context.PushStream(4u);
            error != RetailCensusError::None)
            return error;
        state.block4Pushed = true;
        state.phase = State::Phase::Name;
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::Name)
    {
        if (const RetailCensusError error = LoadName(context, state, entry);
            error != RetailCensusError::None)
            return error;
        if (!storage.name) return RetailCensusError::None;
        lightDef.name = storage.name->c_str();
        state.phase = State::Phase::Image;
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::Image)
    {
        if (!state.imageStarted)
        {
            imageLoader_.Reset();
            const ZoneSpan imageCell{
                0u, state.headerSpan.offset + 4u, 4u};
            if (const RetailCensusError error = imageLoader_.Begin(
                    context, entry.assetIndex, state.imageToken, imageCell);
                error != RetailCensusError::None)
                return error;
            state.imageStarted = true;
        }
        if (imageLoader_.Progress() == RetailImageLoadProgress::Running)
        {
            if (const RetailCensusError error = imageLoader_.Step(context);
                error != RetailCensusError::None)
                return error;
            if (imageLoader_.Progress() == RetailImageLoadProgress::Running)
                return RetailCensusError::None;
        }
        lightDef.attenuation.image = imageLoader_.Asset(context);
        entry.attenuationImageIdentity = imageLoader_.Identity(context);
        if (lightDef.attenuation.image)
        {
            const auto found = std::find_if(
                context.Ownership().worldImages.begin(),
                context.Ownership().worldImages.end(),
                [&](const RetailPublishedGfxImage &candidate) {
                    return candidate.published &&
                        candidate.identity == entry.attenuationImageIdentity &&
                        candidate.asset.get() == lightDef.attenuation.image;
                });
            if (found == context.Ownership().worldImages.end())
                return RetailCensusError::LightDefImageInvalid;
            storage.attenuationImage = found->asset;
        }
        state.phase = State::Phase::Publish;
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
                    ASSET_TYPE_LIGHT_DEF_VALUE, entry.assetIndex,
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
                ASSET_TYPE_LIGHT_DEF_VALUE, entry.assetIndex, entry.identity,
                entry.boundaryInflatedOffset, state.headerSpan,
                *storage.name, state.tableAlias);
            error != RetailCensusError::None)
            return error;
        state.progress = RetailLightDefLoadProgress::Complete;
        return RetailCensusError::None;
    }
    return RetailCensusError::LightDefLayoutUnsupported;
}

RetailLightDefLoadProgress RetailLightDefLoadFamily::Progress() const noexcept
{
    return state_ ? state_->progress : RetailLightDefLoadProgress::Idle;
}

void RetailLightDefLoadFamily::Reset() noexcept
{
    state_.reset();
    imageLoader_.Reset();
}

} // namespace kisak::fastfile
