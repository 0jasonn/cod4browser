#include <web/web_retail_load_image.h>

#include <gfx_d3d/gfx_image_types.h>

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
constexpr std::uint32_t ASSET_TYPE_IMAGE_VALUE = 6u;
constexpr std::uint32_t GFX_IMAGE_BYTES = 36u;
constexpr std::uint32_t GFX_IMAGE_LOAD_DEF_BYTES = 16u;

std::uint16_t ReadU16(const std::uint8_t *value) noexcept
{
    return static_cast<std::uint16_t>(value[0]) |
        static_cast<std::uint16_t>(value[1]) << 8u;
}

std::uint32_t ReadU32(const std::uint8_t *value) noexcept
{
    return static_cast<std::uint32_t>(value[0]) |
        static_cast<std::uint32_t>(value[1]) << 8u |
        static_cast<std::uint32_t>(value[2]) << 16u |
        static_cast<std::uint32_t>(value[3]) << 24u;
}

std::int16_t ReadS16(const std::uint8_t *value) noexcept
{
    return std::bit_cast<std::int16_t>(ReadU16(value));
}

std::int32_t ReadS32(const std::uint8_t *value) noexcept
{
    return std::bit_cast<std::int32_t>(ReadU32(value));
}

bool SupportedImageFormat(std::uint32_t format) noexcept
{
    return format == 0x00000015u || format == 0x00000016u ||
        format == 0x0000001cu || format == 0x00000032u ||
        format == 0x00000033u || format == 0x0000004bu ||
        format == 0x00000050u || format == 0x00000072u ||
        format == 0x31545844u || format == 0x33545844u ||
        format == 0x35545844u;
}

RetailCensusError RegistryError(ZoneRegistryError error) noexcept
{
    if (error == ZoneRegistryError::None) return RetailCensusError::None;
    return error == ZoneRegistryError::AllocationFailed
        ? RetailCensusError::AllocationFailed
        : RetailCensusError::AssetRegistryInvalid;
}
} // namespace

struct RetailImageLoadFamily::State
{
    enum class Phase : std::uint8_t
    {
        Header,
        Name,
        LoadDef,
        Resource,
        Publish,
    };

    RetailImageLoadProgress progress = RetailImageLoadProgress::Idle;
    Phase phase = Phase::Header;
    std::size_t resultIndex = 0u;
    ZoneSpan pointerCell{};
    ZoneSpan insertCell{};
    ZoneSpan headerSpan{};
    ZoneSpan loadDefSpan{};
    ZoneSpan stringSpan{};
    std::uint32_t nameToken = 0u;
    std::uint32_t textureToken = 0u;
    std::uint32_t plannedStringBytes = 0u;
    std::uint32_t resourceBytes = 0u;
    bool hasInsertCell = false;
    bool block0Pushed = false;
    bool block4Pushed = false;
    bool textureBlock0Pushed = false;
    bool stringPlanned = false;
};

namespace
{
RetailPublishedGfxImage &Entry(
    RetailLoadContext &context,
    const RetailImageLoadFamily::State &state) noexcept
{
    return context.Ownership().worldImages[state.resultIndex];
}

RetailCensusError LoadName(
    RetailLoadContext &context,
    RetailImageLoadFamily::State &state,
    RetailPublishedGfxImage &entry) noexcept
{
    entry.canonicalName.reset();
    entry.nameBlock4Offset = UINT32_MAX;
    if (state.nameToken == 0u || state.nameToken == SHARED_POINTER)
        return RetailCensusError::ImageNameInvalid;
    if (state.nameToken != INLINE_POINTER)
    {
        if (!context.ResolveXString(
                state.nameToken, entry.canonicalName,
                entry.nameBlock4Offset) || !entry.canonicalName)
            return RetailCensusError::ImageNameInvalid;
        if (entry.canonicalName->empty())
            return RetailCensusError::ImageNameInvalid;
        if (entry.canonicalName->size() >=
            context.LoaderLimits().maxImageNameBytes)
            return RetailCensusError::ImageNameTooLong;
        entry.name = *entry.canonicalName;
        return RetailCensusError::None;
    }

    if (!state.stringPlanned)
    {
        const auto tail = context.InflatedTail();
        const std::size_t limit = context.LoaderLimits().maxImageNameBytes;
        const std::size_t scanBytes = tail.size() <= limit
            ? tail.size() : limit + 1u;
        const auto terminator = std::find(
            tail.begin(), tail.begin() + scanBytes, 0u);
        if (terminator == tail.begin() + scanBytes)
        {
            if (tail.size() > limit)
                return RetailCensusError::ImageNameTooLong;
            context.BlockForInflatedInput();
            return RetailCensusError::None;
        }
        const std::size_t length =
            static_cast<std::size_t>(terminator - tail.begin()) + 1u;
        if (length <= 1u) return RetailCensusError::ImageNameInvalid;
        if (length > limit || length > UINT32_MAX)
            return RetailCensusError::ImageNameTooLong;
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
        entry.canonicalName = std::make_shared<std::string>(
            reinterpret_cast<const char *>(tail.data()),
            state.plannedStringBytes - 1u);
        entry.name = *entry.canonicalName;
    }
    catch (...) { return RetailCensusError::AllocationFailed; }
    entry.nameBlock4Offset = state.stringSpan.offset;
    if (const RetailCensusError error = context.RememberXString(
            state.nameToken, state.stringSpan, entry.canonicalName);
        error != RetailCensusError::None)
        return error;
    context.ConsumeRecord(state.plannedStringBytes);
    state.stringPlanned = false;
    return RetailCensusError::None;
}
} // namespace

RetailImageLoadFamily::RetailImageLoadFamily() noexcept = default;
RetailImageLoadFamily::~RetailImageLoadFamily() = default;

RetailCensusError RetailImageLoadFamily::Begin(
    RetailLoadContext &context,
    std::uint32_t ownerAssetIndex,
    std::uint32_t serializedReference,
    const ZoneSpan &pointerCell) noexcept
{
    if (state_ && state_->progress == RetailImageLoadProgress::Running)
        return RetailCensusError::InvalidArgument;
    std::unique_ptr<State> next(new (std::nothrow) State());
    if (!next) return RetailCensusError::AllocationFailed;
    try { context.Ownership().worldImages.emplace_back(); }
    catch (...) { return RetailCensusError::AllocationFailed; }
    next->resultIndex = context.Ownership().worldImages.size() - 1u;
    next->pointerCell = pointerCell;
    RetailPublishedGfxImage &entry = Entry(context, *next);
    entry.ownerAssetIndex = ownerAssetIndex;
    entry.serializedReference = serializedReference;

    if (serializedReference == 0u)
    {
        entry.nullRoot = true;
        entry.boundaryInflatedOffset =
            static_cast<std::uint32_t>(context.InflatedCursor());
        next->progress = RetailImageLoadProgress::Complete;
        state_ = std::move(next);
        return RetailCensusError::None;
    }
    if (serializedReference != INLINE_POINTER &&
        serializedReference != SHARED_POINTER)
    {
        if (context.ResolveAssetAlias(serializedReference,
                ASSET_TYPE_IMAGE_VALUE, entry.identity) !=
            ZoneRegistryError::None)
            return RetailCensusError::LightDefImageInvalid;
        GfxImage *asset = static_cast<GfxImage *>(context.FindCanonicalAsset(
            ASSET_TYPE_IMAGE_VALUE, entry.identity));
        const auto found = std::find_if(
            context.Ownership().worldImages.begin(),
            context.Ownership().worldImages.end() - 1,
            [&](const RetailPublishedGfxImage &candidate) {
                return candidate.published &&
                    candidate.identity == entry.identity &&
                    candidate.asset.get() == asset;
            });
        if (!asset || found == context.Ownership().worldImages.end() - 1)
            return RetailCensusError::LightDefImageInvalid;
        entry = *found;
        entry.ownerAssetIndex = ownerAssetIndex;
        entry.serializedReference = serializedReference;
        entry.pointerAlias = true;
        entry.boundaryInflatedOffset =
            static_cast<std::uint32_t>(context.InflatedCursor());
        next->progress = RetailImageLoadProgress::Complete;
        state_ = std::move(next);
        return RetailCensusError::None;
    }

    if (const RetailCensusError error = RegistryError(
            context.Assets().ReserveAlias(pointerCell, ASSET_TYPE_IMAGE_VALUE));
        error != RetailCensusError::None)
        return error;
    next->hasInsertCell = serializedReference == SHARED_POINTER;
    if (next->hasInsertCell)
    {
        if (const RetailCensusError error = context.PlanStream(
                4u, 4u, &next->insertCell, nullptr);
            error != RetailCensusError::None)
            return error;
        if (const RetailCensusError error = RegistryError(
                context.Assets().ReserveAlias(
                    next->insertCell, ASSET_TYPE_IMAGE_VALUE));
            error != RetailCensusError::None)
            return error;
        entry.assetInsertPointerBlock4Offset = next->insertCell.offset;
    }
    if (const RetailCensusError error = context.PushStream(0u);
        error != RetailCensusError::None)
        return error;
    next->block0Pushed = true;
    if (const RetailCensusError error = context.PlanStream(
            4u, GFX_IMAGE_BYTES, &next->headerSpan, nullptr);
        error != RetailCensusError::None)
        return error;
    entry.headerBlock0Offset = next->headerSpan.offset;
    try { entry.asset = std::make_shared<GfxImage>(); }
    catch (...) { return RetailCensusError::AllocationFailed; }
    std::memset(entry.asset.get(), 0, sizeof(GfxImage));
    next->progress = RetailImageLoadProgress::Running;
    state_ = std::move(next);
    return RetailCensusError::None;
}

RetailCensusError RetailImageLoadFamily::Step(
    RetailLoadContext &context) noexcept
{
    if (!state_ || state_->progress != RetailImageLoadProgress::Running)
        return RetailCensusError::InvalidArgument;
    State &state = *state_;
    RetailPublishedGfxImage &entry = Entry(context, state);
    GfxImage &image = *entry.asset;

    if (state.phase == State::Phase::Header)
    {
        if (context.VisitRecord(GFX_IMAGE_BYTES) != RetailLoadVisit::Complete)
            return RetailCensusError::None;
        const auto tail = context.InflatedTail();
        if (tail.size() < GFX_IMAGE_BYTES)
        {
            context.BlockForInflatedInput();
            return RetailCensusError::None;
        }
        const std::uint8_t *record = tail.data();
        entry.mapType = ReadU32(record);
        state.textureToken = ReadU32(record + 4u);
        entry.textureReference = state.textureToken;
        entry.nameReference = ReadU32(record + 32u);
        entry.width = ReadU16(record + 24u);
        entry.height = ReadU16(record + 26u);
        entry.depth = ReadU16(record + 28u);
        const bool emptyBuiltin = entry.mapType == MAPTYPE_NONE &&
            state.textureToken == 0u && entry.width == 0u &&
            entry.height == 0u && entry.depth == 0u;
        const bool bounded2d = entry.mapType == MAPTYPE_2D &&
            state.textureToken != 0u && entry.width != 0u &&
            entry.height != 0u && entry.depth == 1u;
        const bool bounded3d = entry.mapType == MAPTYPE_3D &&
            state.textureToken != 0u && entry.width != 0u &&
            entry.height != 0u && entry.depth != 0u;
        const bool boundedCube = entry.mapType == MAPTYPE_CUBE &&
            state.textureToken != 0u && entry.width != 0u &&
            entry.width == entry.height && entry.depth == 1u;
        if ((!emptyBuiltin && !bounded2d && !bounded3d && !boundedCube) ||
            record[10u] > 1u ||
            entry.nameReference == 0u ||
            entry.nameReference == SHARED_POINTER)
            return RetailCensusError::ImageLayoutUnsupported;
        if (state.textureToken != 0u &&
            state.textureToken != INLINE_POINTER &&
            state.textureToken != SHARED_POINTER &&
            !context.ValidPointerToken(state.textureToken, 4u))
            return RetailCensusError::ImageLayoutUnsupported;

        image.mapType = static_cast<MapType>(entry.mapType);
        image.texture.basemap = nullptr;
        image.picmip.platform[0] = record[8u];
        image.picmip.platform[1] = record[9u];
        image.noPicmip = record[10u] != 0u;
        image.semantic = record[11u];
        image.track = record[12u];
        image.cardMemory.platform[0] = ReadS32(record + 16u);
        image.cardMemory.platform[1] = ReadS32(record + 20u);
        image.width = entry.width;
        image.height = entry.height;
        image.depth = entry.depth;
        image.category = record[30u];
        image.delayLoadPixels = record[31u] != 0u;
        context.ConsumeRecord(GFX_IMAGE_BYTES);
        if (const RetailCensusError error = context.PushStream(4u);
            error != RetailCensusError::None)
            return error;
        state.block4Pushed = true;
        state.nameToken = entry.nameReference;
        state.phase = State::Phase::Name;
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::Name)
    {
        if (const RetailCensusError error = LoadName(context, state, entry);
            error != RetailCensusError::None)
            return error;
        if (!entry.canonicalName) return RetailCensusError::None;
        if (entry.mapType == MAPTYPE_NONE &&
            !entry.canonicalName->starts_with(','))
            return RetailCensusError::ImageNameInvalid;
        image.name = entry.canonicalName->c_str();
        if (state.textureToken == INLINE_POINTER ||
            state.textureToken == SHARED_POINTER)
        {
            if (state.textureToken == SHARED_POINTER)
            {
                ZoneSpan insert;
                if (const RetailCensusError error = context.PlanStream(
                        4u, 4u, &insert, nullptr);
                    error != RetailCensusError::None)
                    return error;
                entry.textureInsertPointerBlock4Offset = insert.offset;
            }
            if (const RetailCensusError error = context.PushStream(0u);
                error != RetailCensusError::None)
                return error;
            state.textureBlock0Pushed = true;
            if (const RetailCensusError error = context.PlanStream(
                    4u, GFX_IMAGE_LOAD_DEF_BYTES,
                    &state.loadDefSpan, nullptr);
                error != RetailCensusError::None)
                return error;
            entry.loadDefBlock0Offset = state.loadDefSpan.offset;
            state.phase = State::Phase::LoadDef;
            return RetailCensusError::None;
        }
        if (const RetailCensusError error = context.PopStream();
            error != RetailCensusError::None)
            return error;
        state.block4Pushed = false;
        if (const RetailCensusError error = context.PopStream();
            error != RetailCensusError::None)
            return error;
        state.block0Pushed = false;
        state.phase = State::Phase::Publish;
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::LoadDef)
    {
        if (context.VisitRecord(GFX_IMAGE_LOAD_DEF_BYTES) !=
            RetailLoadVisit::Complete)
            return RetailCensusError::None;
        const auto tail = context.InflatedTail();
        if (tail.size() < GFX_IMAGE_LOAD_DEF_BYTES)
        {
            context.BlockForInflatedInput();
            return RetailCensusError::None;
        }
        const std::uint8_t *record = tail.data();
        const std::int32_t resourceBytes = ReadS32(record + 12u);
        if (!SupportedImageFormat(ReadU32(record + 8u)) ||
            ReadS16(record + 2u) != entry.width ||
            ReadS16(record + 4u) != entry.height ||
            ReadS16(record + 6u) != entry.depth || resourceBytes < 0)
            return resourceBytes < 0
                ? RetailCensusError::ImageResourceSizeInvalid
                : RetailCensusError::ImageLayoutUnsupported;
        if (static_cast<std::uint32_t>(resourceBytes) >
            context.LoaderLimits().maxImageResourceBytes)
            return RetailCensusError::ImageResourceSizeLimit;
        entry.format = ReadU32(record + 8u);
        entry.resourceBytes = static_cast<std::uint32_t>(resourceBytes);
        entry.loadDefTraversed = true;
        state.resourceBytes = entry.resourceBytes;
        context.ConsumeRecord(GFX_IMAGE_LOAD_DEF_BYTES);
        if (const RetailCensusError error = context.PlanStream(
                1u, state.resourceBytes, nullptr, nullptr);
            error != RetailCensusError::None)
            return error;
        state.phase = State::Phase::Resource;
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::Resource)
    {
        if (context.VisitRecord(state.resourceBytes) != RetailLoadVisit::Complete)
            return RetailCensusError::None;
        context.ConsumeRecord(state.resourceBytes);
        if (const RetailCensusError error = context.PopStream();
            error != RetailCensusError::None)
            return error;
        state.textureBlock0Pushed = false;
        if (const RetailCensusError error = context.PopStream();
            error != RetailCensusError::None)
            return error;
        state.block4Pushed = false;
        if (const RetailCensusError error = context.PopStream();
            error != RetailCensusError::None)
            return error;
        state.block0Pushed = false;
        state.phase = State::Phase::Publish;
        return RetailCensusError::None;
    }

    if (state.phase == State::Phase::Publish)
    {
        const std::uint32_t source =
            (state.pointerCell.block << 28u) | state.pointerCell.offset;
        if (const RetailCensusError error = RegistryError(
                context.Assets().RegisterAsset(
                    ASSET_TYPE_IMAGE_VALUE, source,
                    *entry.canonicalName, entry.identity));
            error != RetailCensusError::None)
            return error;
        if (const RetailCensusError error = RegistryError(
                context.Assets().PublishAlias(
                    state.pointerCell, entry.identity));
            error != RetailCensusError::None)
            return error;
        if (state.hasInsertCell)
            if (const RetailCensusError error = RegistryError(
                    context.Assets().PublishAlias(
                        state.insertCell, entry.identity));
                error != RetailCensusError::None)
                return error;
        entry.boundaryInflatedOffset =
            static_cast<std::uint32_t>(context.InflatedCursor());
        entry.published = true;
        state.progress = RetailImageLoadProgress::Complete;
        return RetailCensusError::None;
    }
    return RetailCensusError::ImageLayoutUnsupported;
}

RetailImageLoadProgress RetailImageLoadFamily::Progress() const noexcept
{
    return state_ ? state_->progress : RetailImageLoadProgress::Idle;
}

GfxImage *RetailImageLoadFamily::Asset(
    RetailLoadContext &context) const noexcept
{
    if (!state_) return nullptr;
    return Entry(context, *state_).asset.get();
}

std::uint32_t RetailImageLoadFamily::Identity(
    RetailLoadContext &context) const noexcept
{
    return state_ ? Entry(context, *state_).identity : 0u;
}

void RetailImageLoadFamily::Reset() noexcept
{
    state_.reset();
}

} // namespace kisak::fastfile
