#pragma once

#include <web/web_retail_load_context.h>
#include <web/web_retail_load_image.h>

#include <cstdint>
#include <memory>

namespace kisak::fastfile
{

enum class RetailLightDefLoadProgress : std::uint8_t
{
    Idle = 0,
    Running,
    Complete,
};

// Resumable transcription of native Load_GfxLightDefPtr,
// Load_GfxLightDef, and Load_GfxLightImage. Renderer light behavior is outside
// this database family.
class RetailLightDefLoadFamily
{
public:
    struct State;

    RetailLightDefLoadFamily() noexcept;
    ~RetailLightDefLoadFamily();
    RetailLightDefLoadFamily(const RetailLightDefLoadFamily &) = delete;
    RetailLightDefLoadFamily &operator=(const RetailLightDefLoadFamily &) = delete;

    RetailCensusError Begin(
        RetailLoadContext &context,
        std::uint32_t assetIndex,
        std::uint32_t serializedReference) noexcept;
    RetailCensusError Step(RetailLoadContext &context) noexcept;
    RetailLightDefLoadProgress Progress() const noexcept;
    void Reset() noexcept;

private:
    std::unique_ptr<State> state_;
    RetailImageLoadFamily imageLoader_;
};

} // namespace kisak::fastfile
