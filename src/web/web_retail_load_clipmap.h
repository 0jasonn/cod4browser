#pragma once

#include <web/web_retail_load_context.h>

#include <cstdint>
#include <memory>

namespace kisak::fastfile
{

struct CanonicalClipMapStorage;

enum class RetailClipMapLoadProgress : std::uint8_t
{
    Idle = 0,
    Running,
    Complete,
};

// Resumable, family-owned transcription of generated Load_clipMap_ptr and
// Load_clipMap_t. Top-level XAsset dispatch remains in the census job.
class RetailClipMapLoadFamily
{
public:
    struct State;

    RetailClipMapLoadFamily() noexcept;
    ~RetailClipMapLoadFamily();
    RetailClipMapLoadFamily(const RetailClipMapLoadFamily &) = delete;
    RetailClipMapLoadFamily &operator=(const RetailClipMapLoadFamily &) = delete;

    RetailCensusError Begin(
        RetailLoadContext &context,
        std::uint32_t assetIndex,
        std::uint32_t assetType,
        std::uint32_t serializedReference) noexcept;
    RetailCensusError Step(RetailLoadContext &context) noexcept;
    RetailClipMapLoadProgress Progress() const noexcept;
    void Reset() noexcept;

private:
    std::unique_ptr<State> state_;
};

} // namespace kisak::fastfile
