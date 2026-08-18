#pragma once

#include <cstdint>

struct PackedTexCoords
{
    PackedTexCoords() : packed(0) {}
    PackedTexCoords(std::uint32_t value) : packed(value) {}
    std::uint32_t packed;
};

static_assert(sizeof(PackedTexCoords) == 4u);
