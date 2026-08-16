#pragma once

#include <database/db_semantic_trace.h>
#include <web/web_fastfile_zone_registry.h>
#include <web/web_retail_fastfile_census.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace kisak::fastfile
{

enum class RetailLoadVisit : std::uint8_t
{
    Blocked = 0,
    Partial,
    Complete,
};

// The deliberately small seam shared by extracted generated-loader families.
// The census job continues to own inflation and top-level orchestration; a
// family receives only the stream/registry/lifetime services which generated
// Load_* code genuinely shares across asset types.
class RetailLoadContext
{
public:
    virtual ~RetailLoadContext() = default;

    virtual ZoneStreamMachine &Streams() noexcept = 0;
    virtual ZoneAssetRegistry &Assets() noexcept = 0;
    virtual RetailFastfileCensus &Ownership() noexcept = 0;
    virtual const RetailCensusLimits &LoaderLimits() const noexcept = 0;

    virtual std::size_t InflatedCursor() const noexcept = 0;
    virtual std::span<const std::uint8_t> InflatedTail() const noexcept = 0;
    virtual RetailLoadVisit VisitRecord(std::size_t bytes) noexcept = 0;
    virtual void ConsumeRecord(std::size_t bytes) noexcept = 0;
    virtual void BlockForInflatedInput() noexcept = 0;

    virtual RetailCensusError PushStream(std::uint32_t block) noexcept = 0;
    virtual RetailCensusError PopStream() noexcept = 0;
    virtual RetailCensusError PlanStream(
        std::uint32_t alignment,
        std::uint64_t length,
        ZoneSpan *span = nullptr,
        ZoneLoadKind *kind = nullptr) noexcept = 0;

    virtual RetailCensusError Trace(
        kisak::database::SemanticTraceEventKind kind,
        std::uint32_t assetType,
        std::uint32_t assetIndex,
        std::uint32_t identity,
        std::uint32_t inflatedOffset,
        const ZoneSpan &span,
        std::string_view name = {},
        const ZoneSpan &related = {}) noexcept = 0;

    virtual ZoneRegistryError ResolveAssetAlias(
        std::uint32_t token,
        std::uint32_t expectedType,
        std::uint32_t &identity) const noexcept = 0;
    virtual void *FindCanonicalAsset(
        std::uint32_t type,
        std::uint32_t identity) noexcept = 0;
    virtual bool ValidPointerToken(
        std::uint32_t token,
        std::uint32_t alignment = 1u) const noexcept = 0;
    virtual bool TranslatePointerToken(
        std::uint32_t token,
        std::uint32_t alignment,
        ZoneSpan &target) const noexcept = 0;
    virtual RetailCensusError BeginXModelDependency(
        std::uint32_t ownerAssetIndex,
        std::uint32_t token,
        const ZoneSpan &pointerCell) noexcept = 0;
    virtual XModel *TakeXModelDependency() noexcept = 0;

    virtual bool ResolveXString(
        std::uint32_t token,
        std::shared_ptr<std::string> &value,
        std::uint32_t &block4Offset) noexcept = 0;
    virtual RetailCensusError RememberXString(
        std::uint32_t serializedToken,
        const ZoneSpan &span,
        const std::shared_ptr<std::string> &value) noexcept = 0;
};

} // namespace kisak::fastfile
