#pragma once

#include <cstdint>

// Shared checksum boundary used by BSP, archive, and save-memory owners.
int __cdecl Com_BlockChecksumKey32(
    const std::uint8_t *data,
    std::uint32_t length,
    std::uint32_t initialCrc);
