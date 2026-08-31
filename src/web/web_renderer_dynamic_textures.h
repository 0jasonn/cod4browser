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

// Backend texture object names, in base/normal/detail/specular/secondary/primary
// order. The lightmaps always use sampler state 0x62.
struct WebRendererDynamicTextureSet
{
    std::array<std::uint32_t, 6> textures{};
    std::array<std::uint8_t, 4> samplers{};
    bool operator==(const WebRendererDynamicTextureSet &) const = default;
};

// One dynamic draw pass only. Nothing between Apply calls may change these
// six 2D bindings or their texture parameters. Compare the entire set: sampler
// parameters belong to texture objects, so aliases across units must retain
// their original last-write order. Per-unit skipping would be incorrect.
// The callback's fourth argument only suppresses a known texture binding;
// it must still reconcile object parameters in the original unit order.
class WebRendererDynamicTextures
{
public:
    template<typename BindTexture>
    void Apply(const WebRendererDynamicTextureSet &next, BindTexture bind)
    {
        if (valid_ && next == previous_) return;
        constexpr std::array<std::uint32_t, 6> units{0u, 1u, 4u, 5u, 2u, 9u};
        for (std::size_t i = 0; i < units.size(); ++i)
            bind(units[i], next.textures[i],
                i < next.samplers.size() ? next.samplers[i] : 0x62u,
                valid_ && previous_.textures[i] == next.textures[i]);
        previous_ = next;
        valid_ = true;
    }

private:
    WebRendererDynamicTextureSet previous_{};
    bool valid_ = false;
};
