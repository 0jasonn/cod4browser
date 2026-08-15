#pragma once

#include <database/db_asset_types.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

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

// Hash only the semantic fields that both the generated native loader and the
// browser loader can observe identically. The native registry uses addresses
// and its inflate layer reads ahead, so identity and inflatedOffset remain
// useful diagnostics but are deliberately excluded from this projection.
std::uint32_t SemanticTraceContractHash(
    std::span<const SemanticTraceEntry> entries) noexcept;

using SemanticTraceObserver = void (*)(
    const SemanticTraceEntry &entry, void *userData);

// The generated database loader is process-global and serialized already. Its
// observer follows the same lifetime: installing one is opt-in test/diagnostic
// behavior, and a null observer makes all native trace calls no-ops.
void SetSemanticTraceObserver(
    SemanticTraceObserver observer, void *userData = nullptr) noexcept;
void ClearSemanticTraceObserver() noexcept;
bool HasSemanticTraceObserver() noexcept;

void ResetNativeSemanticTraceContext() noexcept;
void EnterNativeSemanticTraceAsset(
    std::uint32_t assetIndex, XAssetType assetType) noexcept;
void LeaveNativeSemanticTraceAsset() noexcept;
void EmitNativeSemanticTrace(
    SemanticTraceEventKind kind,
    std::uint32_t identity,
    std::uint32_t inflatedOffset,
    std::uint32_t streamBlock,
    std::uint32_t streamOffset,
    std::uint32_t relatedBlock,
    std::uint32_t relatedOffset,
    std::string_view name = {}) noexcept;

} // namespace kisak::database
