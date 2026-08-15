#include <database/db_semantic_trace.h>

#include <array>
#include <cstddef>

namespace kisak::database
{
namespace
{

struct NativeSemanticTraceState
{
    SemanticTraceObserver observer = nullptr;
    void *userData = nullptr;
    std::uint32_t assetIndex = 0u;
    XAssetType assetType = ASSET_TYPE_XMODELPIECES;
    bool assetActive = false;
};

NativeSemanticTraceState g_nativeSemanticTrace;

void HashByte(std::uint32_t &hash, std::uint8_t value) noexcept
{
    hash ^= value;
    hash *= 16777619u;
}

void HashU32(std::uint32_t &hash, std::uint32_t value) noexcept
{
    for (std::uint32_t shift = 0u; shift < 32u; shift += 8u)
        HashByte(hash, static_cast<std::uint8_t>(value >> shift));
}

} // namespace

const char *SemanticTraceEventKindString(
    SemanticTraceEventKind kind) noexcept
{
    switch (kind)
    {
    case SemanticTraceEventKind::AssetBegin: return "asset-begin";
    case SemanticTraceEventKind::AssetPublish: return "asset-publish";
    case SemanticTraceEventKind::Dependency: return "dependency";
    case SemanticTraceEventKind::AliasReserve: return "alias-reserve";
    case SemanticTraceEventKind::AliasPublish: return "alias-publish";
    case SemanticTraceEventKind::Boundary: return "boundary";
    case SemanticTraceEventKind::Failure: return "failure";
    }
    return "unknown";
}

std::uint32_t SemanticTraceHash(
    std::span<const SemanticTraceEntry> entries) noexcept
{
    std::uint32_t hash = 2166136261u;
    for (const SemanticTraceEntry &entry : entries)
    {
        HashByte(hash, static_cast<std::uint8_t>(entry.kind));
        HashU32(hash, static_cast<std::uint32_t>(entry.assetType));
        HashU32(hash, entry.assetIndex);
        HashU32(hash, entry.identity);
        HashU32(hash, entry.inflatedOffset);
        HashU32(hash, entry.streamBlock);
        HashU32(hash, entry.streamOffset);
        HashU32(hash, entry.relatedBlock);
        HashU32(hash, entry.relatedOffset);
        for (const unsigned char byte : entry.name) HashByte(hash, byte);
        HashByte(hash, 0u);
    }
    return hash;
}

std::uint32_t SemanticTraceContractHash(
    std::span<const SemanticTraceEntry> entries) noexcept
{
    std::uint32_t hash = 2166136261u;
    for (const SemanticTraceEntry &entry : entries)
    {
        HashByte(hash, static_cast<std::uint8_t>(entry.kind));
        HashU32(hash, static_cast<std::uint32_t>(entry.assetType));
        HashU32(hash, entry.assetIndex);
        HashU32(hash, entry.streamBlock);
        HashU32(hash, entry.streamOffset);
        HashU32(hash, entry.relatedBlock);
        HashU32(hash, entry.relatedOffset);
        for (const unsigned char byte : entry.name) HashByte(hash, byte);
        HashByte(hash, 0u);
    }
    return hash;
}

void SetSemanticTraceObserver(
    SemanticTraceObserver observer, void *userData) noexcept
{
    g_nativeSemanticTrace.observer = observer;
    g_nativeSemanticTrace.userData = userData;
}

void ClearSemanticTraceObserver() noexcept
{
    g_nativeSemanticTrace = {};
}

bool HasSemanticTraceObserver() noexcept
{
    return g_nativeSemanticTrace.observer != nullptr;
}

void ResetNativeSemanticTraceContext() noexcept
{
    g_nativeSemanticTrace.assetIndex = 0u;
    g_nativeSemanticTrace.assetType = ASSET_TYPE_XMODELPIECES;
    g_nativeSemanticTrace.assetActive = false;
}

void EnterNativeSemanticTraceAsset(
    std::uint32_t assetIndex, XAssetType assetType) noexcept
{
    if (!HasSemanticTraceObserver()) return;
    g_nativeSemanticTrace.assetIndex = assetIndex;
    g_nativeSemanticTrace.assetType = assetType;
    g_nativeSemanticTrace.assetActive = true;
}

void LeaveNativeSemanticTraceAsset() noexcept
{
    g_nativeSemanticTrace.assetActive = false;
}

void EmitNativeSemanticTrace(
    SemanticTraceEventKind kind,
    std::uint32_t identity,
    std::uint32_t inflatedOffset,
    std::uint32_t streamBlock,
    std::uint32_t streamOffset,
    std::uint32_t relatedBlock,
    std::uint32_t relatedOffset,
    std::string_view name) noexcept
{
    if (!g_nativeSemanticTrace.observer ||
        !g_nativeSemanticTrace.assetActive)
    {
        return;
    }
    try
    {
        SemanticTraceEntry entry;
        entry.kind = kind;
        entry.assetType = g_nativeSemanticTrace.assetType;
        entry.assetIndex = g_nativeSemanticTrace.assetIndex;
        entry.identity = identity;
        entry.inflatedOffset = inflatedOffset;
        entry.streamBlock = streamBlock;
        entry.streamOffset = streamOffset;
        entry.relatedBlock = relatedBlock;
        entry.relatedOffset = relatedOffset;
        entry.name.assign(name);
        g_nativeSemanticTrace.observer(entry, g_nativeSemanticTrace.userData);
    }
    catch (...)
    {
        // Diagnostics must never change database loader behavior.
    }
}

} // namespace kisak::database
