#pragma once

#include <algorithm>
#include <cstdint>

// Shared R_SetTexFilter policy. The result retains Kisak's packed sampler
// filter state: anisotropy in bits 0..7, min/mag/mip filters at 8/12/16.
inline std::uint32_t R_DecodeSamplerFilter(std::uint8_t samplerState,
    int mipMode, int minAniso, int maxAniso, bool disableFiltering,
    bool anisotropicMin = true, bool anisotropicMag = true)
{
    constexpr unsigned mipFilters[4][3] = {
        {0, 1, 2}, {0, 2, 2}, {0, 1, 1}, {0, 0, 0},
    };
    maxAniso = (std::max)(maxAniso, 1);
    minAniso = std::clamp(minAniso, 1, maxAniso);
    const unsigned filter = samplerState & 7u;
    const unsigned authoredMip = (samplerState >> 3u) & 3u;
    const unsigned mip = mipFilters[disableFiltering ? 3 : std::clamp(mipMode, 0, 3)]
        [(std::min)(authoredMip, 2u)];
    if (disableFiltering || filter < 2u || filter > 4u)
        return (mip << 16u) | 0x1101u;
    const int anisotropy = filter == 2u && !mip ? 1 :
        std::clamp(filter == 4u ? 4 : (filter == 3u ? 2 : 1), minAniso, maxAniso);
    const bool anisotropic = filter == 2u ? anisotropy > 1 : maxAniso > 1;
    return (mip << 16u) | static_cast<unsigned>(anisotropy) |
        (anisotropic && anisotropicMin ? 0x300u : 0x200u) |
        (anisotropic && anisotropicMag ? 0x3000u : 0x2000u);
}
