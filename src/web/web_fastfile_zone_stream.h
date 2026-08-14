#pragma once

#include <array>
#include <cstdint>
#include <memory>

namespace kisak::fastfile
{

constexpr std::uint32_t ZONE_STREAM_BLOCK_COUNT = 9u;
constexpr std::uint32_t ZONE_STREAM_STACK_CAPACITY = 64u;
constexpr std::uint32_t ZONE_STREAM_DELAYED_CAPACITY = 4096u;

enum class ZoneLoadKind : std::uint8_t
{
    Immediate = 0,
    ZeroFill,
    Delayed,
};

struct ZoneSpan
{
    std::uint32_t block = 0u;
    std::uint32_t offset = 0u;
    std::uint32_t length = 0u;

    bool operator==(const ZoneSpan &) const noexcept = default;
};

struct ZoneLoadPlan
{
    ZoneSpan span{};
    ZoneLoadKind kind = ZoneLoadKind::Immediate;

    bool operator==(const ZoneLoadPlan &) const noexcept = default;
};

struct ZoneStreamLimits
{
    std::uint64_t maxTotalArenaBytes = 8u * 1024u * 1024u;
    std::uint32_t maxStackDepth = ZONE_STREAM_STACK_CAPACITY;
    std::uint32_t maxDelayedSpans = ZONE_STREAM_DELAYED_CAPACITY;
    std::uint64_t maxDelayedBytes = 8u * 1024u * 1024u;
};

enum class ZoneStreamError : std::uint8_t
{
    None = 0,
    NotInitialized,
    InvalidArgument,
    ArenaSizeLimit,
    BlockOverflow,
    StackDepthLimit,
    StackUnderflow,
    ReplayState,
    DelayedSpanLimit,
    DelayedByteLimit,
    DelayedConsumeInvalid,
    UnbalancedStack,
    BlockSizeMismatch,
    DelayedReplayIncomplete,
    AllocationFailed,
};

const char *ZoneStreamErrorString(ZoneStreamError error) noexcept;

// Models the nine logical arenas used by the generated fastfile loader. It
// deliberately does not own inflated input or arena storage: PlanLoad tells a
// caller whether bytes are needed now, synthesized as zeroes, or deferred.
// Reserve advances only the logical arena and never represents serialized
// input. All offsets are checked before any state or output parameter changes.
class ZoneStreamMachine
{
public:
    ZoneStreamError Initialize(
        const std::array<std::uint32_t, ZONE_STREAM_BLOCK_COUNT> &declaredSizes,
        const ZoneStreamLimits &limits = {}) noexcept;

    ZoneStreamError Push(std::uint32_t block) noexcept;
    ZoneStreamError Pop() noexcept;

    ZoneStreamError PlanLoad(
        std::uint32_t alignment,
        std::uint64_t length,
        ZoneLoadPlan &plan) noexcept;
    ZoneStreamError Reserve(
        std::uint32_t alignment,
        std::uint64_t length,
        ZoneSpan &span) noexcept;

    // Delayed spans retain their original checked {block, offset, length}
    // records. PeekDelayed returns the unconsumed suffix of the current FIFO
    // entry so replay may observe the same per-frame byte ceilings as traversal.
    ZoneStreamError BeginDelayedReplay() noexcept;
    bool PeekDelayed(ZoneSpan &remaining) const noexcept;
    ZoneStreamError ConsumeDelayed(std::uint32_t length) noexcept;

    // Requires balanced loader frames, active block zero, exact declared
    // high-water use for every block, persistent cursors at their declarations,
    // a rewound block-zero cursor, and complete delayed replay when applicable.
    ZoneStreamError ValidateComplete() const noexcept;

    bool Initialized() const noexcept;
    bool DelayedReplayStarted() const noexcept;
    bool DelayedReplayComplete() const noexcept;
    std::uint32_t ActiveBlock() const noexcept;
    std::uint32_t StackDepth() const noexcept;
    std::uint64_t TotalArenaBytes() const noexcept;
    std::uint32_t DeclaredSize(std::uint32_t block) const noexcept;
    std::uint32_t Cursor(std::uint32_t block) const noexcept;
    std::uint32_t HighWater(std::uint32_t block) const noexcept;
    std::uint32_t DelayedSpanCount() const noexcept;
    std::uint64_t DelayedBytes() const noexcept;
    std::uint64_t DelayedBytesConsumed() const noexcept;
    bool GetDelayedSpan(std::uint32_t index, ZoneSpan &span) const noexcept;

private:
    struct Frame
    {
        std::uint32_t returnBlock = 0u;
        std::uint32_t targetBlock = 0u;
        std::uint32_t targetEntry = 0u;
    };

    ZoneStreamError ComputeSpan(
        std::uint32_t alignment,
        std::uint64_t length,
        ZoneSpan &span,
        std::uint32_t &newCursor) const noexcept;
    void CommitSpan(const ZoneSpan &span, std::uint32_t newCursor) noexcept;

    std::array<std::uint32_t, ZONE_STREAM_BLOCK_COUNT> declared_{};
    std::array<std::uint32_t, ZONE_STREAM_BLOCK_COUNT> cursor_{};
    std::array<std::uint32_t, ZONE_STREAM_BLOCK_COUNT> highWater_{};
    std::array<Frame, ZONE_STREAM_STACK_CAPACITY> frames_{};
    std::unique_ptr<ZoneSpan[]> delayed_;
    ZoneStreamLimits limits_{};
    std::uint64_t totalArenaBytes_ = 0u;
    std::uint64_t delayedBytes_ = 0u;
    std::uint64_t delayedBytesConsumed_ = 0u;
    std::uint32_t activeBlock_ = 0u;
    std::uint32_t stackDepth_ = 0u;
    std::uint32_t delayedCount_ = 0u;
    std::uint32_t delayedReplayIndex_ = 0u;
    std::uint32_t delayedReplayOffset_ = 0u;
    bool initialized_ = false;
    bool delayedReplayStarted_ = false;
};

} // namespace kisak::fastfile
