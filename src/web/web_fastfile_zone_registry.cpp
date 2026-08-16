#include <web/web_fastfile_zone_registry.h>

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace kisak::fastfile
{
namespace
{
constexpr std::uint32_t INLINE_POINTER = 0xffffffffu;
constexpr std::uint32_t INLINE_SHARED_POINTER = 0xfffffffeu;
constexpr std::uint32_t POINTER_OFFSET_MASK = 0x0fffffffu;

std::uint64_t AssetSourceKey(
    std::uint32_t type,
    std::uint32_t sourceIndex) noexcept
{
    return (static_cast<std::uint64_t>(type) << 32u) | sourceIndex;
}

std::uint64_t AliasSpanKey(const ZoneSpan &slot) noexcept
{
    return (static_cast<std::uint64_t>(slot.block) << 32u) | slot.offset;
}

std::uint64_t AssetNameHash(
    std::uint32_t type,
    std::string_view name) noexcept
{
    std::uint64_t value = 1469598103934665603ull;
    for (std::uint32_t shift = 0u; shift < 32u; shift += 8u)
    {
        value ^= (type >> shift) & 0xffu;
        value *= 1099511628211ull;
    }
    for (const unsigned char byte : name)
    {
        value ^= byte;
        value *= 1099511628211ull;
    }
    return value;
}

bool SpanIsValidAlias(
    const ZoneSpan &slot,
    const std::array<std::uint32_t, ZONE_STREAM_BLOCK_COUNT> &declared) noexcept
{
    return slot.block < ZONE_STREAM_BLOCK_COUNT && slot.length == 4u &&
        (slot.offset & 3u) == 0u && slot.offset <= declared[slot.block] &&
        slot.length <= declared[slot.block] - slot.offset &&
        slot.offset <= POINTER_OFFSET_MASK;
}
} // namespace

bool DecodeZoneAliasToken(
    std::uint32_t token,
    ZoneSpan &slot) noexcept
{
    if (token == 0u || token == INLINE_POINTER ||
        token == INLINE_SHARED_POINTER)
    {
        return false;
    }
    const std::uint32_t packed = token - 1u;
    const std::uint32_t block = packed >> 28u;
    if (block >= ZONE_STREAM_BLOCK_COUNT)
    {
        return false;
    }
    slot = {block, packed & POINTER_OFFSET_MASK, 4u};
    return true;
}

bool EncodeZoneAliasToken(
    const ZoneSpan &slot,
    std::uint32_t &token) noexcept
{
    if (slot.block >= ZONE_STREAM_BLOCK_COUNT || slot.length != 4u ||
        (slot.offset & 3u) != 0u || slot.offset > POINTER_OFFSET_MASK)
    {
        return false;
    }
    token = (slot.block << 28u) | (slot.offset + 1u);
    return token != INLINE_POINTER && token != INLINE_SHARED_POINTER;
}

ZoneRegistryError ZoneAssetRegistry::Initialize(
    const std::array<std::uint32_t, ZONE_STREAM_BLOCK_COUNT> &declaredSizes,
    const ZoneRegistryLimits &limits) noexcept
{
    if (limits.maxAssets == 0u || limits.maxAliases == 0u ||
        limits.maxNameBytes == 0u)
    {
        return ZoneRegistryError::InvalidArgument;
    }

    std::vector<ZoneRegisteredAsset>().swap(assets_);
    std::vector<AliasEntry>().swap(aliases_);
    std::unordered_map<std::uint32_t, std::size_t>().swap(assetByIdentity_);
    std::unordered_map<std::uint64_t, std::size_t>().swap(assetBySource_);
    std::unordered_multimap<std::uint64_t, std::size_t>().swap(assetByNameHash_);
    std::unordered_map<std::uint64_t, std::size_t>().swap(aliasBySpan_);
    declared_ = declaredSizes;
    limits_ = limits;
    totalNameBytes_ = 0u;
    nextIdentity_ = 1u;
    initialized_ = true;
    return ZoneRegistryError::None;
}

ZoneRegistryError ZoneAssetRegistry::RegisterAsset(
    std::uint32_t type,
    std::uint32_t sourceIndex,
    std::string_view name,
    std::uint32_t &identity) noexcept
{
    if (!initialized_)
    {
        return ZoneRegistryError::NotInitialized;
    }
    if (type == 0u || nextIdentity_ == 0u)
    {
        return ZoneRegistryError::InvalidArgument;
    }
    if (assets_.size() >= limits_.maxAssets)
    {
        return ZoneRegistryError::AssetLimit;
    }
    if (name.size() > limits_.maxNameBytes - totalNameBytes_)
    {
        return ZoneRegistryError::NameBytesLimit;
    }
    const std::uint64_t sourceKey = AssetSourceKey(type, sourceIndex);
    if (assetBySource_.contains(sourceKey))
    {
        return ZoneRegistryError::AssetDuplicate;
    }

    bool assetAppended = false;
    bool sourceIndexed = false;
    bool identityIndexed = false;
    try
    {
        ZoneRegisteredAsset asset;
        asset.identity = nextIdentity_;
        asset.type = type;
        asset.sourceIndex = sourceIndex;
        asset.name.assign(name.begin(), name.end());
        assets_.push_back(std::move(asset));
        assetAppended = true;
        const std::size_t index = assets_.size() - 1u;
        const auto [sourceIt, sourceInserted] =
            assetBySource_.emplace(sourceKey, index);
        if (!sourceInserted)
        {
            assets_.pop_back();
            return ZoneRegistryError::AssetDuplicate;
        }
        sourceIndexed = true;
        const auto [identityIt, identityInserted] =
            assetByIdentity_.emplace(nextIdentity_, index);
        if (!identityInserted)
        {
            assetBySource_.erase(sourceIt);
            assets_.pop_back();
            return ZoneRegistryError::InvalidArgument;
        }
        identityIndexed = true;
        assetByNameHash_.emplace(
            AssetNameHash(type, assets_.back().name), index);
    }
    catch (const std::bad_alloc &)
    {
        if (identityIndexed) assetByIdentity_.erase(nextIdentity_);
        if (sourceIndexed) assetBySource_.erase(sourceKey);
        if (assetAppended) assets_.pop_back();
        return ZoneRegistryError::AllocationFailed;
    }
    catch (...)
    {
        if (identityIndexed) assetByIdentity_.erase(nextIdentity_);
        if (sourceIndexed) assetBySource_.erase(sourceKey);
        if (assetAppended) assets_.pop_back();
        return ZoneRegistryError::AllocationFailed;
    }

    identity = nextIdentity_;
    ++nextIdentity_;
    totalNameBytes_ += static_cast<std::uint32_t>(name.size());
    return ZoneRegistryError::None;
}

ZoneRegistryError ZoneAssetRegistry::ReserveAlias(
    const ZoneSpan &slot,
    std::uint32_t expectedType) noexcept
{
    if (!initialized_)
    {
        return ZoneRegistryError::NotInitialized;
    }
    if (expectedType == 0u || !SpanIsValidAlias(slot, declared_))
    {
        return ZoneRegistryError::AliasInvalid;
    }
    if (aliases_.size() >= limits_.maxAliases)
    {
        return ZoneRegistryError::AliasLimit;
    }
    const std::uint64_t spanKey = AliasSpanKey(slot);
    if (aliasBySpan_.contains(spanKey))
    {
        return ZoneRegistryError::AliasDuplicate;
    }

    try
    {
        aliases_.push_back({slot, expectedType, 0u});
        try
        {
            const auto [it, inserted] =
                aliasBySpan_.emplace(spanKey, aliases_.size() - 1u);
            if (!inserted)
            {
                aliases_.pop_back();
                return ZoneRegistryError::AliasDuplicate;
            }
        }
        catch (...)
        {
            aliases_.pop_back();
            throw;
        }
    }
    catch (const std::bad_alloc &)
    {
        return ZoneRegistryError::AllocationFailed;
    }
    catch (...)
    {
        return ZoneRegistryError::AllocationFailed;
    }
    return ZoneRegistryError::None;
}

ZoneRegistryError ZoneAssetRegistry::PublishAlias(
    const ZoneSpan &slot,
    std::uint32_t identity) noexcept
{
    if (!initialized_)
    {
        return ZoneRegistryError::NotInitialized;
    }
    AliasEntry *alias = FindAlias(slot);
    if (!alias)
    {
        return ZoneRegistryError::AliasInvalid;
    }
    if (alias->identity != 0u)
    {
        return ZoneRegistryError::AliasDuplicate;
    }
    const ZoneRegisteredAsset *asset = FindAsset(identity);
    if (!asset)
    {
        return ZoneRegistryError::AssetUndefined;
    }
    if (asset->type != alias->expectedType)
    {
        return ZoneRegistryError::AssetTypeMismatch;
    }
    alias->identity = identity;
    return ZoneRegistryError::None;
}

ZoneRegistryError ZoneAssetRegistry::ResolveAlias(
    std::uint32_t token,
    std::uint32_t expectedType,
    std::uint32_t &identity) const noexcept
{
    if (!initialized_)
    {
        return ZoneRegistryError::NotInitialized;
    }
    ZoneSpan slot;
    if (!DecodeZoneAliasToken(token, slot) ||
        !SpanIsValidAlias(slot, declared_))
    {
        return ZoneRegistryError::AliasInvalid;
    }
    std::uint32_t canonicalToken = 0u;
    if (!EncodeZoneAliasToken(slot, canonicalToken) || canonicalToken != token)
    {
        return ZoneRegistryError::AliasInvalid;
    }
    const AliasEntry *alias = FindAlias(slot);
    if (!alias)
    {
        return ZoneRegistryError::AliasInvalid;
    }
    if (alias->identity == 0u)
    {
        return ZoneRegistryError::AliasUndefined;
    }
    const ZoneRegisteredAsset *asset = FindAsset(alias->identity);
    if (!asset)
    {
        return ZoneRegistryError::AssetUndefined;
    }
    if (asset->type != expectedType || alias->expectedType != expectedType)
    {
        return ZoneRegistryError::AssetTypeMismatch;
    }
    identity = asset->identity;
    return ZoneRegistryError::None;
}

const ZoneRegisteredAsset *ZoneAssetRegistry::FindAsset(
    std::uint32_t identity) const noexcept
{
    const auto found = assetByIdentity_.find(identity);
    return found == assetByIdentity_.end() || found->second >= assets_.size()
        ? nullptr : &assets_[found->second];
}

const ZoneRegisteredAsset *ZoneAssetRegistry::FindAsset(
    std::uint32_t type,
    std::string_view name) const noexcept
{
    const auto [begin, end] = assetByNameHash_.equal_range(
        AssetNameHash(type, name));
    for (auto candidate = begin; candidate != end; ++candidate)
    {
        if (candidate->second >= assets_.size()) continue;
        const ZoneRegisteredAsset &asset = assets_[candidate->second];
        if (asset.type == type && asset.name == name) return &asset;
    }
    return nullptr;
}

bool ZoneAssetRegistry::Initialized() const noexcept
{
    return initialized_;
}

std::uint32_t ZoneAssetRegistry::AssetCount() const noexcept
{
    return static_cast<std::uint32_t>(assets_.size());
}

std::uint32_t ZoneAssetRegistry::AliasCount() const noexcept
{
    return static_cast<std::uint32_t>(aliases_.size());
}

std::uint32_t ZoneAssetRegistry::DefinedAliasCount() const noexcept
{
    return static_cast<std::uint32_t>(std::count_if(
        aliases_.begin(), aliases_.end(),
        [](const AliasEntry &alias) { return alias.identity != 0u; }));
}

std::uint32_t ZoneAssetRegistry::TotalNameBytes() const noexcept
{
    return totalNameBytes_;
}

void ZoneAssetRegistry::UnloadAll() noexcept
{
    std::vector<ZoneRegisteredAsset>().swap(assets_);
    std::vector<AliasEntry>().swap(aliases_);
    std::unordered_map<std::uint32_t, std::size_t>().swap(assetByIdentity_);
    std::unordered_map<std::uint64_t, std::size_t>().swap(assetBySource_);
    std::unordered_multimap<std::uint64_t, std::size_t>().swap(assetByNameHash_);
    std::unordered_map<std::uint64_t, std::size_t>().swap(aliasBySpan_);
    totalNameBytes_ = 0u;
    nextIdentity_ = 1u;
}

void ZoneAssetRegistry::Reset() noexcept
{
    UnloadAll();
    declared_ = {};
    limits_ = {};
    initialized_ = false;
}

ZoneAssetRegistry::AliasEntry *ZoneAssetRegistry::FindAlias(
    const ZoneSpan &slot) noexcept
{
    const auto found = aliasBySpan_.find(AliasSpanKey(slot));
    return found == aliasBySpan_.end() || found->second >= aliases_.size()
        ? nullptr : &aliases_[found->second];
}

const ZoneAssetRegistry::AliasEntry *ZoneAssetRegistry::FindAlias(
    const ZoneSpan &slot) const noexcept
{
    const auto found = aliasBySpan_.find(AliasSpanKey(slot));
    return found == aliasBySpan_.end() || found->second >= aliases_.size()
        ? nullptr : &aliases_[found->second];
}

const char *ZoneRegistryErrorString(ZoneRegistryError error) noexcept
{
    switch (error)
    {
    case ZoneRegistryError::None: return "success";
    case ZoneRegistryError::NotInitialized: return "zone registry is not initialized";
    case ZoneRegistryError::InvalidArgument: return "zone registry argument is invalid";
    case ZoneRegistryError::AssetLimit: return "zone registry asset count exceeds its limit";
    case ZoneRegistryError::AliasLimit: return "zone registry alias count exceeds its limit";
    case ZoneRegistryError::NameBytesLimit: return "zone registry names exceed their byte limit";
    case ZoneRegistryError::AssetDuplicate: return "zone registry asset is already registered";
    case ZoneRegistryError::AssetUndefined: return "zone registry asset is undefined";
    case ZoneRegistryError::AssetTypeMismatch: return "zone registry asset type is incompatible";
    case ZoneRegistryError::AliasInvalid: return "zone registry alias is invalid";
    case ZoneRegistryError::AliasUndefined: return "zone registry alias is undefined";
    case ZoneRegistryError::AliasDuplicate: return "zone registry alias is already defined";
    case ZoneRegistryError::AllocationFailed: return "zone registry allocation failed";
    }
    return "unknown zone registry error";
}

} // namespace kisak::fastfile
