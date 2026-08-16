#pragma once

#include <cstdint>

struct cplane_s // IW3 serialized size: 0x14
{
    float normal[3];
    float dist;
    std::uint8_t type;
    std::uint8_t signbits;
    std::uint8_t pad[2];
};

static_assert(sizeof(cplane_s) == 0x14);
