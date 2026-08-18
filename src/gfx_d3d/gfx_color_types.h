#pragma once

#include <cstdint>

union GfxColor
{
    operator std::uint32_t() const { return packed; }
    GfxColor() : packed(0) {}
    GfxColor(int value) : packed(static_cast<std::uint32_t>(value)) {}
    GfxColor(std::uint32_t value) : packed(value) {}
    std::uint32_t packed;
    std::uint8_t array[4];
};

static_assert(sizeof(GfxColor) == 4u);
