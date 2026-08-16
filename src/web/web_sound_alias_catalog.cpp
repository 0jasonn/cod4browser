#include <web/web_sound_alias_catalog.h>

#include <algorithm>
#include <limits>

namespace kisak::fastfile
{
namespace
{
bool ValidName(std::string_view name) noexcept
{
    return !name.empty() && name.find('\0') == std::string_view::npos;
}

bool NamesEqual(std::string_view left, std::string_view right) noexcept
{
    const auto foldAscii = [](unsigned char value) noexcept {
        return value >= 'A' && value <= 'Z'
            ? static_cast<unsigned char>(value + ('a' - 'A'))
            : value;
    };
    return left.size() == right.size() &&
        std::equal(left.begin(), left.end(), right.begin(),
            [foldAscii](char lhs, char rhs) {
                return foldAscii(static_cast<unsigned char>(lhs)) ==
                    foldAscii(static_cast<unsigned char>(rhs));
            });
}
} // namespace

const char *RetailSoundAliasCatalogErrorString(
    RetailSoundAliasCatalogError error) noexcept
{
    switch (error)
    {
    case RetailSoundAliasCatalogError::None: return "success";
    case RetailSoundAliasCatalogError::InvalidArgument:
        return "sound alias catalog argument is invalid";
    case RetailSoundAliasCatalogError::EntryLimit:
        return "sound alias catalog entry count exceeds its limit";
    case RetailSoundAliasCatalogError::NameBytesLimit:
        return "sound alias catalog names exceed their byte limit";
    case RetailSoundAliasCatalogError::Duplicate:
        return "sound alias catalog entry is already published";
    case RetailSoundAliasCatalogError::AllocationFailed:
        return "sound alias catalog allocation failed";
    }
    return "unknown sound alias catalog error";
}

RetailSoundAliasCatalog::RetailSoundAliasCatalog(
    const RetailSoundAliasCatalogLimits &limits) noexcept
    : limits_(limits)
{
}

RetailSoundAliasCatalogError RetailSoundAliasCatalog::Publish(
    std::string_view name,
    snd_alias_list_t *asset,
    std::shared_ptr<const void> owner) noexcept
{
    if (!ValidName(name) || asset == nullptr || !owner ||
        limits_.maxEntries == 0u || limits_.maxTotalNameBytes == 0u)
    {
        return RetailSoundAliasCatalogError::InvalidArgument;
    }
    if (entries_.size() >= limits_.maxEntries)
        return RetailSoundAliasCatalogError::EntryLimit;
    if (name.size() > std::numeric_limits<std::uint32_t>::max() ||
        name.size() > limits_.maxTotalNameBytes - totalNameBytes_)
    {
        return RetailSoundAliasCatalogError::NameBytesLimit;
    }
    if (std::any_of(entries_.begin(), entries_.end(),
            [name](const Entry &entry) {
                return NamesEqual(entry.name, name);
            }))
    {
        return RetailSoundAliasCatalogError::Duplicate;
    }

    try
    {
        Entry entry;
        entry.name.assign(name.begin(), name.end());
        entry.asset = asset;
        entry.owner = std::move(owner);
        entries_.push_back(std::move(entry));
    }
    catch (...)
    {
        return RetailSoundAliasCatalogError::AllocationFailed;
    }
    totalNameBytes_ += static_cast<std::uint32_t>(name.size());
    return RetailSoundAliasCatalogError::None;
}

snd_alias_list_t *RetailSoundAliasCatalog::Find(
    std::string_view name) const noexcept
{
    if (!ValidName(name)) return nullptr;
    const auto found = std::find_if(entries_.begin(), entries_.end(),
        [name](const Entry &entry) {
            return NamesEqual(entry.name, name);
        });
    return found == entries_.end() ? nullptr : found->asset;
}

RetailSoundAliasLookup RetailSoundAliasCatalog::Lookup() noexcept
{
    return {LookupEntry, this};
}

std::uint32_t RetailSoundAliasCatalog::EntryCount() const noexcept
{
    return static_cast<std::uint32_t>(entries_.size());
}

std::uint32_t RetailSoundAliasCatalog::TotalNameBytes() const noexcept
{
    return totalNameBytes_;
}

void RetailSoundAliasCatalog::Reset() noexcept
{
    std::vector<Entry>().swap(entries_);
    totalNameBytes_ = 0u;
}

snd_alias_list_t *RetailSoundAliasCatalog::LookupEntry(
    std::string_view name, void *userData) noexcept
{
    if (userData == nullptr) return nullptr;
    return static_cast<RetailSoundAliasCatalog *>(userData)->Find(name);
}

} // namespace kisak::fastfile
