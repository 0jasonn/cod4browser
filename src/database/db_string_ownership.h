#pragma once

#include <cstdint>

void DB_RegisterStringZoneOwnership(
    std::uint32_t stringValue, std::uint32_t zoneIndex);
void DB_UnregisterDefaultStringOwnership(std::uint32_t stringValue);
void DB_ReleaseStringZoneOwnership(std::uint64_t releaseZoneMask);
bool DB_HasRegisteredStringOwnership();
void DB_ResetStringOwnership();
