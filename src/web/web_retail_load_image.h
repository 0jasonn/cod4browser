#pragma once

#include <web/web_retail_load_context.h>

#include <cstdint>
#include <memory>

struct GfxImage;

namespace kisak::fastfile
{

enum class RetailImageLoadProgress : std::uint8_t
{
    Idle = 0,
    Running,
    Complete,
};

// Reusable database-side transcription of Load_GfxImagePtr/Load_GfxImage.
// It publishes canonical metadata only. Texture decoding and GPU upload remain
// renderer/backend responsibilities.
class RetailImageLoadFamily
{
public:
    struct State;

    RetailImageLoadFamily() noexcept;
    ~RetailImageLoadFamily();
    RetailImageLoadFamily(const RetailImageLoadFamily &) = delete;
    RetailImageLoadFamily &operator=(const RetailImageLoadFamily &) = delete;

    RetailCensusError Begin(
        RetailLoadContext &context,
        std::uint32_t ownerAssetIndex,
        std::uint32_t serializedReference,
        const ZoneSpan &pointerCell) noexcept;
    RetailCensusError Step(RetailLoadContext &context) noexcept;
    RetailImageLoadProgress Progress() const noexcept;
    GfxImage *Asset(RetailLoadContext &context) const noexcept;
    std::uint32_t Identity(RetailLoadContext &context) const noexcept;
    void Reset() noexcept;

private:
    std::unique_ptr<State> state_;
};

} // namespace kisak::fastfile
