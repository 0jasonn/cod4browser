#pragma once

#include <database/db_asset_types.h>

#include <cstdint>
#include <span>
#include <string>

namespace kisak::database
{

enum class SemanticTraceEventKind : std::uint8_t
{
    AssetBegin = 0,
    AssetPublish,
    Dependency,
    AliasReserve,
    AliasPublish,
    Boundary,
    Failure,
};

// Stable semantic event shared by native and web database paths. Addresses and
// backend handles are intentionally excluded; logical stream coordinates and
// registry identities remain comparable across processes and platforms.
struct SemanticTraceEntry
{
    SemanticTraceEventKind kind = SemanticTraceEventKind::AssetBegin;
    XAssetType assetType = ASSET_TYPE_XMODELPIECES;
    std::uint32_t assetIndex = 0u;
    std::uint32_t identity = 0u;
    std::uint32_t inflatedOffset = 0u;
    std::uint32_t streamBlock = 0u;
    std::uint32_t streamOffset = 0u;
    std::uint32_t relatedBlock = 0u;
    std::uint32_t relatedOffset = 0u;
    std::string name;

    bool operator==(const SemanticTraceEntry &) const noexcept = default;
};

const char *SemanticTraceEventKindString(
    SemanticTraceEventKind kind) noexcept;

std::uint32_t SemanticTraceHash(
    std::span<const SemanticTraceEntry> entries) noexcept;

} // namespace kisak::database
