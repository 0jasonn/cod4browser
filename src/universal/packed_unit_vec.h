#pragma once

#include <cstdint>

// Database-facing packed vector ABI. Kept independent of com_math so asset
// declarations can be shared with browser code without importing native math
// globals or platform calling-convention helpers.
union PackedUnitVec
{
    operator std::uint32_t() const noexcept
    {
        return packed;
    }

    operator int() const noexcept
    {
        return static_cast<int>(packed);
    }

    std::uint32_t packed;
    std::uint8_t array[4];
};

static_assert(sizeof(PackedUnitVec) == 4u);
