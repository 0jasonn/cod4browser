#pragma once

#include <array>
#include <cstdint>

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
                i < next.samplers.size() ? next.samplers[i] : 0x62u);
        previous_ = next;
        valid_ = true;
    }

private:
    WebRendererDynamicTextureSet previous_{};
    bool valid_ = false;
};
