#pragma once

#include <web/web_fastfile_zone_stream.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kisak::fastfile
{

struct ZoneRegistryLimits
{
    std::uint32_t maxAssets = 2u;
    std::uint32_t maxAliases = 1u;
    std::uint32_t maxTotalNameBytes = 256u;
};

struct ZoneRegisteredAsset
{
    std::uint32_t identity = 0u;
    std::uint32_t type = 0u;
    std::uint32_t sourceIndex = 0u;
    std::string name;
};

enum class ZoneRegistryError : std::uint8_t
{
    None = 0,
    NotInitialized,
    InvalidArgument,
    AssetLimit,
    AliasLimit,
    NameBytesLimit,
    AssetDuplicate,
    AssetUndefined,
    AssetTypeMismatch,
    AliasInvalid,
    AliasUndefined,
    AliasDuplicate,
    AllocationFailed,
};

const char *ZoneRegistryErrorString(ZoneRegistryError error) noexcept;

bool DecodeZoneAliasToken(
    std::uint32_t token,
    ZoneSpan &slot) noexcept;
bool EncodeZoneAliasToken(
    const ZoneSpan &slot,
    std::uint32_t &token) noexcept;

// Owns stable job-local asset identities and the lifecycle of serialized alias
// cells. Alias entries are keyed by checked logical spans, never native or Wasm
// pointers. UnloadAll releases all names/entries while retaining configuration;
// Reset additionally returns the registry to its uninitialized state.
class ZoneAssetRegistry
{
public:
    ZoneRegistryError Initialize(
        const std::array<std::uint32_t, ZONE_STREAM_BLOCK_COUNT> &declaredSizes,
        const ZoneRegistryLimits &limits = {}) noexcept;

    ZoneRegistryError RegisterAsset(
        std::uint32_t type,
        std::uint32_t sourceIndex,
        std::string_view name,
        std::uint32_t &identity) noexcept;
    ZoneRegistryError ReserveAlias(
        const ZoneSpan &slot,
        std::uint32_t expectedType) noexcept;
    ZoneRegistryError PublishAlias(
        const ZoneSpan &slot,
        std::uint32_t identity) noexcept;
    ZoneRegistryError ResolveAlias(
        std::uint32_t token,
        std::uint32_t expectedType,
        std::uint32_t &identity) const noexcept;

    const ZoneRegisteredAsset *FindAsset(std::uint32_t identity) const noexcept;

    bool Initialized() const noexcept;
    std::uint32_t AssetCount() const noexcept;
    std::uint32_t AliasCount() const noexcept;
    std::uint32_t DefinedAliasCount() const noexcept;
    std::uint32_t TotalNameBytes() const noexcept;

    void UnloadAll() noexcept;
    void Reset() noexcept;

private:
    struct AliasEntry
    {
        ZoneSpan slot{};
        std::uint32_t expectedType = 0u;
        std::uint32_t identity = 0u;
    };

    AliasEntry *FindAlias(const ZoneSpan &slot) noexcept;
    const AliasEntry *FindAlias(const ZoneSpan &slot) const noexcept;

    std::array<std::uint32_t, ZONE_STREAM_BLOCK_COUNT> declared_{};
    std::vector<ZoneRegisteredAsset> assets_;
    std::vector<AliasEntry> aliases_;
    ZoneRegistryLimits limits_{};
    std::uint32_t totalNameBytes_ = 0u;
    std::uint32_t nextIdentity_ = 1u;
    bool initialized_ = false;
};

} // namespace kisak::fastfile
