#include <database/db_string_ownership.h>
#include <script/scr_stringlist.h>
#include <universal/q_shared.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace
{
std::array<std::uint64_t, SL_MAX_STRING_INDEX> g_stringZoneOwners{};
std::array<std::uint16_t, SL_MAX_STRING_INDEX> g_defaultStringRefs{};
}

void DB_RegisterStringZoneOwnership(
    std::uint32_t stringValue, std::uint32_t zoneIndex)
{
    iassert(stringValue < SL_MAX_STRING_INDEX);
    iassert(zoneIndex < 64u);
    if (!stringValue) return;
    if (zoneIndex == 0u)
    {
        iassert(g_defaultStringRefs[stringValue] != UINT16_MAX);
        ++g_defaultStringRefs[stringValue];
    }
    g_stringZoneOwners[stringValue] |= std::uint64_t{1} << zoneIndex;
}

void DB_UnregisterDefaultStringOwnership(std::uint32_t stringValue)
{
    iassert(stringValue < SL_MAX_STRING_INDEX);
    if (!stringValue) return;
    iassert(g_defaultStringRefs[stringValue] > 0u);
    if (--g_defaultStringRefs[stringValue] == 0u)
        g_stringZoneOwners[stringValue] &= ~std::uint64_t{1};
    if (g_stringZoneOwners[stringValue] == 0u)
        SL_RemoveUser(stringValue, 4u);
}

void DB_ReleaseStringZoneOwnership(std::uint64_t releaseZoneMask)
{
    releaseZoneMask &= ~std::uint64_t{1};
    if (!releaseZoneMask) return;
    for (std::uint32_t stringValue = 1; stringValue < SL_MAX_STRING_INDEX;
         ++stringValue)
    {
        const std::uint64_t previous = g_stringZoneOwners[stringValue];
        if (!(previous & releaseZoneMask)) continue;
        g_stringZoneOwners[stringValue] = previous & ~releaseZoneMask;
        if (!g_stringZoneOwners[stringValue])
            SL_RemoveUser(stringValue, 4u);
    }
}

bool DB_HasRegisteredStringOwnership()
{
    for (std::uint32_t stringValue = 1; stringValue < SL_MAX_STRING_INDEX;
         ++stringValue)
    {
        if (g_stringZoneOwners[stringValue]) return true;
    }
    return false;
}

void DB_ResetStringOwnership()
{
    std::fill(g_stringZoneOwners.begin(), g_stringZoneOwners.end(), 0u);
    std::fill(g_defaultStringRefs.begin(), g_defaultStringRefs.end(), 0u);
}
