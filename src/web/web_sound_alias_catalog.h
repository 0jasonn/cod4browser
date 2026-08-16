#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct snd_alias_list_t;

namespace kisak::fastfile
{

// Mirrors DB_FindXAssetHeader(ASSET_TYPE_SOUND, name) without making the
// bounded fastfile operation own an independent database. Exact names resolve
// to indexed zone objects; a miss resolves to the indexed canonical `null`
// sound when that prerequisite asset has been published.
using RetailSoundAliasLookupFunction = snd_alias_list_t *(*)(
    std::string_view name, void *userData) noexcept;

struct RetailSoundAliasLookup
{
    RetailSoundAliasLookupFunction function = nullptr;
    void *userData = nullptr;
};

enum class RetailSoundAliasCatalogError : std::uint8_t
{
    None = 0,
    InvalidArgument,
    EntryLimit,
    NameBytesLimit,
    Duplicate,
    AllocationFailed,
};

const char *RetailSoundAliasCatalogErrorString(
    RetailSoundAliasCatalogError error) noexcept;

struct RetailSoundAliasCatalogLimits
{
    std::uint32_t maxEntries = 16384u;
    std::uint32_t maxTotalNameBytes = 2u * 1024u * 1024u;
};

// A cross-zone ownership seam for canonical sound assets. The opaque owner is
// retained so entries can be backed directly by native/common-zone ownership;
// no snd_alias_list_t is synthesized or copied here.
class RetailSoundAliasCatalog
{
public:
    RetailSoundAliasCatalog(
        const RetailSoundAliasCatalogLimits &limits = {}) noexcept;

    RetailSoundAliasCatalogError Publish(
        std::string_view name,
        snd_alias_list_t *asset,
        std::shared_ptr<const void> owner) noexcept;
    snd_alias_list_t *Find(std::string_view name) const noexcept;
    RetailSoundAliasLookup Lookup() noexcept;

    std::uint32_t EntryCount() const noexcept;
    std::uint32_t TotalNameBytes() const noexcept;
    void Reset() noexcept;

private:
    struct Entry
    {
        std::string name;
        snd_alias_list_t *asset = nullptr;
        std::shared_ptr<const void> owner;
    };

    static snd_alias_list_t *LookupEntry(
        std::string_view name, void *userData) noexcept;

    RetailSoundAliasCatalogLimits limits_{};
    std::vector<Entry> entries_;
    std::uint32_t totalNameBytes_ = 0u;
};

} // namespace kisak::fastfile
