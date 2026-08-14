#include <web/web_fastfile_zone_stream.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace kisak::fastfile
{
namespace
{

bool IsPowerOfTwo(std::uint32_t value) noexcept
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

ZoneLoadKind LoadKindForBlock(std::uint32_t block) noexcept
{
    if (block == 1u)
    {
        return ZoneLoadKind::ZeroFill;
    }
    if (block == 2u || block == 3u)
    {
        return ZoneLoadKind::Delayed;
    }
    return ZoneLoadKind::Immediate;
}

} // namespace

ZoneStreamError ZoneStreamMachine::Initialize(
    const std::array<std::uint32_t, ZONE_STREAM_BLOCK_COUNT> &declaredSizes,
    const ZoneStreamLimits &limits) noexcept
{
    if (limits.maxStackDepth > ZONE_STREAM_STACK_CAPACITY ||
        limits.maxDelayedSpans > ZONE_STREAM_DELAYED_CAPACITY)
    {
        return ZoneStreamError::InvalidArgument;
    }

    std::uint64_t totalArenaBytes = 0u;
    for (std::uint32_t size : declaredSizes)
    {
        totalArenaBytes += size;
        if (totalArenaBytes > limits.maxTotalArenaBytes)
        {
            return ZoneStreamError::ArenaSizeLimit;
        }
    }

    std::unique_ptr<ZoneSpan[]> delayed;
    if (limits.maxDelayedSpans != 0u)
    {
        delayed.reset(new (std::nothrow) ZoneSpan[limits.maxDelayedSpans]);
        if (!delayed)
        {
            return ZoneStreamError::AllocationFailed;
        }
    }

    declared_ = declaredSizes;
    cursor_ = {};
    highWater_ = {};
    limits_ = limits;
    delayed_ = std::move(delayed);
    totalArenaBytes_ = totalArenaBytes;
    delayedBytes_ = 0u;
    delayedBytesConsumed_ = 0u;
    activeBlock_ = 0u;
    stackDepth_ = 0u;
    delayedCount_ = 0u;
    delayedReplayIndex_ = 0u;
    delayedReplayOffset_ = 0u;
    delayedReplayStarted_ = false;
    initialized_ = true;
    return ZoneStreamError::None;
}

ZoneStreamError ZoneStreamMachine::Push(std::uint32_t block) noexcept
{
    if (!initialized_)
    {
        return ZoneStreamError::NotInitialized;
    }
    if (delayedReplayStarted_ || block >= ZONE_STREAM_BLOCK_COUNT)
    {
        return delayedReplayStarted_
            ? ZoneStreamError::ReplayState
            : ZoneStreamError::InvalidArgument;
    }
    if (stackDepth_ >= limits_.maxStackDepth)
    {
        return ZoneStreamError::StackDepthLimit;
    }

    frames_[stackDepth_] = {activeBlock_, block, cursor_[block]};
    ++stackDepth_;
    activeBlock_ = block;
    return ZoneStreamError::None;
}

ZoneStreamError ZoneStreamMachine::Pop() noexcept
{
    if (!initialized_)
    {
        return ZoneStreamError::NotInitialized;
    }
    if (delayedReplayStarted_)
    {
        return ZoneStreamError::ReplayState;
    }
    if (stackDepth_ == 0u)
    {
        return ZoneStreamError::StackUnderflow;
    }

    const Frame &frame = frames_[stackDepth_ - 1u];
    if (frame.targetBlock != activeBlock_)
    {
        return ZoneStreamError::UnbalancedStack;
    }
    if (activeBlock_ == 0u)
    {
        cursor_[0] = frame.targetEntry;
    }
    activeBlock_ = frame.returnBlock;
    --stackDepth_;
    return ZoneStreamError::None;
}

ZoneStreamError ZoneStreamMachine::ComputeSpan(
    std::uint32_t alignment,
    std::uint64_t length,
    ZoneSpan &span,
    std::uint32_t &newCursor) const noexcept
{
    if (!initialized_)
    {
        return ZoneStreamError::NotInitialized;
    }
    if (delayedReplayStarted_)
    {
        return ZoneStreamError::ReplayState;
    }
    if (!IsPowerOfTwo(alignment) ||
        length > std::numeric_limits<std::uint32_t>::max())
    {
        return ZoneStreamError::InvalidArgument;
    }

    const std::uint64_t mask = static_cast<std::uint64_t>(alignment) - 1u;
    const std::uint64_t current = cursor_[activeBlock_];
    const std::uint64_t aligned = (current + mask) & ~mask;
    if (aligned > declared_[activeBlock_] ||
        length > static_cast<std::uint64_t>(declared_[activeBlock_]) - aligned)
    {
        return ZoneStreamError::BlockOverflow;
    }

    span = {
        activeBlock_,
        static_cast<std::uint32_t>(aligned),
        static_cast<std::uint32_t>(length),
    };
    newCursor = static_cast<std::uint32_t>(aligned + length);
    return ZoneStreamError::None;
}

void ZoneStreamMachine::CommitSpan(
    const ZoneSpan &span,
    std::uint32_t newCursor) noexcept
{
    cursor_[span.block] = newCursor;
    highWater_[span.block] = std::max(highWater_[span.block], newCursor);
}

ZoneStreamError ZoneStreamMachine::PlanLoad(
    std::uint32_t alignment,
    std::uint64_t length,
    ZoneLoadPlan &plan) noexcept
{
    ZoneSpan span;
    std::uint32_t newCursor = 0u;
    if (const ZoneStreamError error = ComputeSpan(
            alignment, length, span, newCursor);
        error != ZoneStreamError::None)
    {
        return error;
    }

    const ZoneLoadKind kind = LoadKindForBlock(activeBlock_);
    if (kind == ZoneLoadKind::Delayed && length != 0u)
    {
        if (delayedCount_ >= limits_.maxDelayedSpans)
        {
            return ZoneStreamError::DelayedSpanLimit;
        }
        if (length > limits_.maxDelayedBytes - delayedBytes_)
        {
            return ZoneStreamError::DelayedByteLimit;
        }
    }

    if (kind == ZoneLoadKind::Delayed && length != 0u)
    {
        delayed_[delayedCount_] = span;
        ++delayedCount_;
        delayedBytes_ += length;
    }
    CommitSpan(span, newCursor);
    plan = {span, kind};
    return ZoneStreamError::None;
}

ZoneStreamError ZoneStreamMachine::Reserve(
    std::uint32_t alignment,
    std::uint64_t length,
    ZoneSpan &span) noexcept
{
    ZoneSpan replacement;
    std::uint32_t newCursor = 0u;
    if (const ZoneStreamError error = ComputeSpan(
            alignment, length, replacement, newCursor);
        error != ZoneStreamError::None)
    {
        return error;
    }

    CommitSpan(replacement, newCursor);
    span = replacement;
    return ZoneStreamError::None;
}

ZoneStreamError ZoneStreamMachine::BeginDelayedReplay() noexcept
{
    if (!initialized_)
    {
        return ZoneStreamError::NotInitialized;
    }
    if (delayedReplayStarted_)
    {
        return ZoneStreamError::ReplayState;
    }
    if (stackDepth_ != 0u || activeBlock_ != 0u)
    {
        return ZoneStreamError::UnbalancedStack;
    }

    delayedReplayStarted_ = true;
    delayedReplayIndex_ = 0u;
    delayedReplayOffset_ = 0u;
    delayedBytesConsumed_ = 0u;
    return ZoneStreamError::None;
}

bool ZoneStreamMachine::PeekDelayed(ZoneSpan &remaining) const noexcept
{
    if (!delayedReplayStarted_ || delayedReplayIndex_ >= delayedCount_)
    {
        return false;
    }

    const ZoneSpan &span = delayed_[delayedReplayIndex_];
    remaining = {
        span.block,
        span.offset + delayedReplayOffset_,
        span.length - delayedReplayOffset_,
    };
    return true;
}

ZoneStreamError ZoneStreamMachine::ConsumeDelayed(std::uint32_t length) noexcept
{
    if (!initialized_)
    {
        return ZoneStreamError::NotInitialized;
    }
    if (!delayedReplayStarted_)
    {
        return ZoneStreamError::ReplayState;
    }
    if (length == 0u || delayedReplayIndex_ >= delayedCount_)
    {
        return ZoneStreamError::DelayedConsumeInvalid;
    }

    const ZoneSpan &span = delayed_[delayedReplayIndex_];
    const std::uint32_t remaining = span.length - delayedReplayOffset_;
    if (length > remaining)
    {
        return ZoneStreamError::DelayedConsumeInvalid;
    }

    delayedReplayOffset_ += length;
    delayedBytesConsumed_ += length;
    if (delayedReplayOffset_ == span.length)
    {
        ++delayedReplayIndex_;
        delayedReplayOffset_ = 0u;
    }
    return ZoneStreamError::None;
}

ZoneStreamError ZoneStreamMachine::ValidateComplete() const noexcept
{
    if (!initialized_)
    {
        return ZoneStreamError::NotInitialized;
    }
    if (stackDepth_ != 0u || activeBlock_ != 0u)
    {
        return ZoneStreamError::UnbalancedStack;
    }
    if (delayedCount_ != 0u && !DelayedReplayComplete())
    {
        return ZoneStreamError::DelayedReplayIncomplete;
    }
    for (std::uint32_t block = 0u; block < ZONE_STREAM_BLOCK_COUNT; ++block)
    {
        const bool cursorMatches = block == 0u
            ? cursor_[block] == 0u
            : cursor_[block] == declared_[block];
        if (!cursorMatches || highWater_[block] != declared_[block])
        {
            return ZoneStreamError::BlockSizeMismatch;
        }
    }
    return ZoneStreamError::None;
}

bool ZoneStreamMachine::Initialized() const noexcept
{
    return initialized_;
}

bool ZoneStreamMachine::DelayedReplayStarted() const noexcept
{
    return delayedReplayStarted_;
}

bool ZoneStreamMachine::DelayedReplayComplete() const noexcept
{
    return delayedReplayStarted_ && delayedReplayIndex_ == delayedCount_;
}

std::uint32_t ZoneStreamMachine::ActiveBlock() const noexcept
{
    return activeBlock_;
}

std::uint32_t ZoneStreamMachine::StackDepth() const noexcept
{
    return stackDepth_;
}

std::uint64_t ZoneStreamMachine::TotalArenaBytes() const noexcept
{
    return totalArenaBytes_;
}

std::uint32_t ZoneStreamMachine::DeclaredSize(std::uint32_t block) const noexcept
{
    return block < ZONE_STREAM_BLOCK_COUNT ? declared_[block] : 0u;
}

std::uint32_t ZoneStreamMachine::Cursor(std::uint32_t block) const noexcept
{
    return block < ZONE_STREAM_BLOCK_COUNT ? cursor_[block] : 0u;
}

std::uint32_t ZoneStreamMachine::HighWater(std::uint32_t block) const noexcept
{
    return block < ZONE_STREAM_BLOCK_COUNT ? highWater_[block] : 0u;
}

std::uint32_t ZoneStreamMachine::DelayedSpanCount() const noexcept
{
    return delayedCount_;
}

std::uint64_t ZoneStreamMachine::DelayedBytes() const noexcept
{
    return delayedBytes_;
}

std::uint64_t ZoneStreamMachine::DelayedBytesConsumed() const noexcept
{
    return delayedBytesConsumed_;
}

bool ZoneStreamMachine::GetDelayedSpan(
    std::uint32_t index,
    ZoneSpan &span) const noexcept
{
    if (index >= delayedCount_)
    {
        return false;
    }
    span = delayed_[index];
    return true;
}

const char *ZoneStreamErrorString(ZoneStreamError error) noexcept
{
    switch (error)
    {
    case ZoneStreamError::None: return "success";
    case ZoneStreamError::NotInitialized: return "zone stream is not initialized";
    case ZoneStreamError::InvalidArgument: return "zone stream argument is invalid";
    case ZoneStreamError::ArenaSizeLimit: return "zone arenas exceed their cumulative limit";
    case ZoneStreamError::BlockOverflow: return "zone stream exceeds its declared block";
    case ZoneStreamError::StackDepthLimit: return "zone stream frame stack exceeds its limit";
    case ZoneStreamError::StackUnderflow: return "zone stream frame stack underflow";
    case ZoneStreamError::ReplayState: return "zone stream delayed replay state is invalid";
    case ZoneStreamError::DelayedSpanLimit: return "zone delayed span count exceeds its limit";
    case ZoneStreamError::DelayedByteLimit: return "zone delayed bytes exceed their limit";
    case ZoneStreamError::DelayedConsumeInvalid: return "zone delayed replay consumption is invalid";
    case ZoneStreamError::UnbalancedStack: return "zone stream frame stack is unbalanced";
    case ZoneStreamError::BlockSizeMismatch: return "zone stream does not fill its declared blocks";
    case ZoneStreamError::DelayedReplayIncomplete: return "zone delayed replay is incomplete";
    case ZoneStreamError::AllocationFailed: return "zone delayed queue allocation failed";
    }
    return "unknown zone stream error";
}

} // namespace kisak::fastfile
