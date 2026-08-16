#pragma once

#include <cstdint>

// Canonical renderer-facing draw-surface key. This small ABI type is shared by
// database asset declarations without pulling Direct3D into portable loaders.
struct GfxDrawSurfFields // sizeof=0x8
{
    std::uint64_t objectId : 16;
    std::uint64_t reflectionProbeIndex : 8;
    std::uint64_t customIndex : 5;
    std::uint64_t materialSortedIndex : 11;
    std::uint64_t prepass : 2;
    std::uint64_t primaryLightIndex : 8;
    std::uint64_t surfType : 4;
    std::uint64_t primarySortKey : 6;
    std::uint64_t unused : 4;
};

inline constexpr std::uint64_t DRAWSURF_KEY_MASK = 0xFFFFFFFFE0000000ull;

union GfxDrawSurf // sizeof=0x8
{
    GfxDrawSurfFields fields;
    std::uint64_t packed;
    std::uint32_t packed_low;
};

static_assert(sizeof(GfxDrawSurfFields) == 8u);
static_assert(sizeof(GfxDrawSurf) == 8u);
