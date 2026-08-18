#include "com_checksum.h"

int __cdecl Com_BlockChecksumKey32(const std::uint8_t *data,
    std::uint32_t length, std::uint32_t initialCrc)
{
    std::uint32_t crc = ~initialCrc;
    for (const std::uint8_t *cursor = data; cursor != data + length; ++cursor)
    {
        std::uint32_t value = crc ^ *cursor;
        for (int bit = 0; bit < 8; ++bit)
            value = (-306674912 * (value & 1u)) ^ (value >> 1);
        crc = value;
    }
    return ~crc;
}
