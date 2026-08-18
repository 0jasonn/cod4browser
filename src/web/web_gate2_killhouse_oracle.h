#pragma once

#include <cstdint>

// Frozen, address-independent observations from the independent Gate 2
// Killhouse loader. The normal database path never uses these values to choose
// a zone, asset, allocation, or loader action; they are regression evidence
// for a canonically published GfxWorld only.
namespace kisak::web::gate2_killhouse_oracle
{
inline constexpr const char *name = "maps/killhouse.d3dbsp";
inline constexpr const char *baseName = "killhouse";
inline constexpr std::int32_t planeCount = 5712;
inline constexpr std::int32_t nodeCount = 5074;
inline constexpr std::int32_t cellCount = 3;
inline constexpr std::uint32_t vertexCount = 448962u;
inline constexpr std::int32_t indexCount = 829539;
inline constexpr std::int32_t surfaceCount = 8694;
inline constexpr std::uint32_t staticModelCount = 12255u;
inline constexpr std::int32_t lightmapCount = 3;
inline constexpr std::int32_t materialMemoryCount = 170;
inline constexpr std::uint32_t inflatedOffset = 86162172u;
inline constexpr std::uint32_t assetIndex = 772u;
inline constexpr std::uint32_t assetCount = 1684u;
inline constexpr std::uint32_t registryAssetCount = 2371u;
inline constexpr std::uint32_t registryAliasCount = 2479u;
inline constexpr std::uint32_t surfaceIndex = 6077u;
inline constexpr std::uint32_t surfaceVertexCount = 2009u;
inline constexpr std::uint32_t surfaceTriangleCount = 128u;
inline constexpr const char *surfaceMaterial = "wc/decal_porterjustice8";
}
