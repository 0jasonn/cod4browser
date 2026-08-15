#include <database/db_semantic_trace.h>

#include <array>
#include <cstddef>

namespace kisak::database
{
namespace
{

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

} // namespace kisak::database
