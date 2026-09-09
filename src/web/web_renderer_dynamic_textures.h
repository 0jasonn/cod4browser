#pragma once

#include <array>
#include <cstdint>

// Frame-local texture parameter memo. Bindings still happen in their original
// order, including aliases across units. Clear before rendering: uploads and
// context recovery outside the frame may have changed/reused object names.
class WebRendererTextureParameters
{
public:
    void Reset() noexcept { entries_ = {}; }
    bool NeedsUpdate(std::uint32_t texture, std::uint8_t sampler, bool mipmaps) noexcept
    {
        const std::uint16_t state = sampler | (mipmaps ? 0x100u : 0u);
        auto &entry = entries_[texture % entries_.size()];
        if (entry.texture == texture && entry.state == state) return false;
        entry = {texture, state};
        return true;
    }

private:
    struct Entry { std::uint32_t texture = 0u; std::uint16_t state = UINT16_MAX; };
    // ponytail: fixed direct mapping; collisions only repeat GL state writes.
    // Increase this bounded table if measured collision cost becomes material.
    std::array<Entry, 256> entries_{};
};

struct WebRendererPassTexture
{
    std::uint32_t unit = 0u;
    std::uint32_t texture = 0u;
    std::uint8_t sampler = 0u;
    bool mipmaps = true;
    bool enabled = true;
    bool operator==(const WebRendererPassTexture &) const = default;
};

// One draw pass only. Nothing between Apply calls may change these
// 2D bindings or their texture parameters without Reset. Each unit occurs
// once, in its original draw-family order. Compare the entire set: sampler
// parameters belong to texture objects, so aliases across units must retain
// their original last-write order. Per-unit skipping would be incorrect.
// The callback's last argument only suppresses a known texture binding;
// it must still reconcile object parameters in the original unit order.
template<std::size_t Count>
class WebRendererPassTextures
{
public:
    void Reset() noexcept { valid_ = false; }

    // A/B builds compile out cache reads, equality checks and state copies.
    template<bool Reuse = true, typename BindTexture>
    void Apply(const std::array<WebRendererPassTexture, Count> &next, BindTexture bind)
    {
        if constexpr (Reuse)
            if (valid_ && next == previous_) return;
        for (std::size_t i = 0; i < next.size(); ++i)
            if (next[i].enabled)
                bind(next[i].unit, next[i].texture, next[i].sampler, next[i].mipmaps,
                    Reuse && valid_ && previous_[i].enabled && previous_[i].unit == next[i].unit &&
                        previous_[i].texture == next[i].texture);
        if constexpr (Reuse)
        {
            previous_ = next;
            valid_ = true;
        }
    }

private:
    std::array<WebRendererPassTexture, Count> previous_{};
    bool valid_ = false;
};
