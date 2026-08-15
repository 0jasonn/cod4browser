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
        limits.maxTotalNameBytes == 0u)
    {
        return ZoneRegistryError::InvalidArgument;
    }

    std::vector<ZoneRegisteredAsset>().swap(assets_);
    std::vector<AliasEntry>().swap(aliases_);
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
    if (name.size() > limits_.maxTotalNameBytes - totalNameBytes_)
    {
        return ZoneRegistryError::NameBytesLimit;
    }
    if (std::any_of(assets_.begin(), assets_.end(),
            [type, sourceIndex](const ZoneRegisteredAsset &asset) {
                return asset.type == type && asset.sourceIndex == sourceIndex;
            }))
    {
        return ZoneRegistryError::AssetDuplicate;
    }

    try
    {
        ZoneRegisteredAsset asset;
        asset.identity = nextIdentity_;
        asset.type = type;
        asset.sourceIndex = sourceIndex;
        asset.name.assign(name.begin(), name.end());
        assets_.push_back(std::move(asset));
    }
    catch (const std::bad_alloc &)
    {
        return ZoneRegistryError::AllocationFailed;
    }
    catch (...)
    {
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
    if (FindAlias(slot) != nullptr)
    {
        return ZoneRegistryError::AliasDuplicate;
    }

    try
    {
        aliases_.push_back({slot, expectedType, 0u});
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
    const auto found = std::find_if(
        assets_.begin(), assets_.end(),
        [identity](const ZoneRegisteredAsset &asset) {
            return asset.identity == identity;
        });
    return found == assets_.end() ? nullptr : &*found;
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
    const auto found = std::find_if(
        aliases_.begin(), aliases_.end(),
        [&slot](const AliasEntry &entry) { return entry.slot == slot; });
    return found == aliases_.end() ? nullptr : &*found;
}

const ZoneAssetRegistry::AliasEntry *ZoneAssetRegistry::FindAlias(
    const ZoneSpan &slot) const noexcept
{
    const auto found = std::find_if(
        aliases_.begin(), aliases_.end(),
        [&slot](const AliasEntry &entry) { return entry.slot == slot; });
    return found == aliases_.end() ? nullptr : &*found;
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
