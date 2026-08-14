#include <web/web_fastfile_zone_stream.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace
{

using kisak::fastfile::ZoneLoadKind;
using kisak::fastfile::ZoneLoadPlan;
using kisak::fastfile::ZoneSpan;
using kisak::fastfile::ZoneStreamError;
using kisak::fastfile::ZoneStreamLimits;
using kisak::fastfile::ZoneStreamMachine;

[[noreturn]] void Fail(const std::string &message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, const std::string &message)
{
    if (!condition)
    {
        Fail(message);
    }
}

void RequireError(
    ZoneStreamError actual,
    ZoneStreamError expected,
    const std::string &message)
{
    if (actual != expected)
    {
        Fail(message + ": expected " +
            kisak::fastfile::ZoneStreamErrorString(expected) + ", got " +
            kisak::fastfile::ZoneStreamErrorString(actual));
    }
}

std::array<std::uint32_t, kisak::fastfile::ZONE_STREAM_BLOCK_COUNT>
Declared(std::uint32_t block, std::uint32_t size)
{
    std::array<std::uint32_t, kisak::fastfile::ZONE_STREAM_BLOCK_COUNT> result{};
    result[block] = size;
    return result;
}

void TestInitializationAndAtomicFailure()
{
    ZoneStreamMachine stream;
    ZoneLoadPlan untouched{{8u, 9u, 10u}, ZoneLoadKind::ZeroFill};
    const ZoneLoadPlan original = untouched;
    RequireError(stream.PlanLoad(1u, 1u, untouched),
        ZoneStreamError::NotInitialized,
        "loads require initialization");
    Require(untouched == original,
        "failed load before initialization leaves output unchanged");

    std::array<std::uint32_t, kisak::fastfile::ZONE_STREAM_BLOCK_COUNT> sizes{};
    for (std::uint32_t block = 0u; block < sizes.size(); ++block)
    {
        sizes[block] = block + 1u;
    }
    ZoneStreamLimits limits;
    limits.maxTotalArenaBytes = 45u;
    RequireError(stream.Initialize(sizes, limits), ZoneStreamError::None,
        "nine declared blocks initialize");
    Require(stream.Initialized(), "initialized accessor is true");
    Require(stream.TotalArenaBytes() == 45u,
        "total arena accessor reports the checked sum");
    Require(stream.ActiveBlock() == 0u && stream.StackDepth() == 0u,
        "initial active block and stack match the native loader");
    for (std::uint32_t block = 0u; block < sizes.size(); ++block)
    {
        Require(stream.DeclaredSize(block) == sizes[block],
            "every declared block size is retained");
        Require(stream.Cursor(block) == 0u && stream.HighWater(block) == 0u,
            "new logical block cursors begin at zero");
    }

    RequireError(stream.Push(4u), ZoneStreamError::None,
        "state is made observable before failed reinitialization");
    const std::uint32_t activeBefore = stream.ActiveBlock();
    const std::uint32_t depthBefore = stream.StackDepth();
    ZoneStreamLimits tooSmall = limits;
    tooSmall.maxTotalArenaBytes = 44u;
    RequireError(stream.Initialize(sizes, tooSmall),
        ZoneStreamError::ArenaSizeLimit,
        "total arena ceiling is enforced");
    Require(stream.ActiveBlock() == activeBefore &&
            stream.StackDepth() == depthBefore &&
            stream.TotalArenaBytes() == 45u,
        "failed initialization is atomic");

    ZoneStreamLimits invalid = limits;
    invalid.maxStackDepth =
        kisak::fastfile::ZONE_STREAM_STACK_CAPACITY + 1u;
    RequireError(stream.Initialize(sizes, invalid),
        ZoneStreamError::InvalidArgument,
        "stack limit cannot exceed the native hard capacity");
    Require(stream.ActiveBlock() == activeBefore &&
            stream.StackDepth() == depthBefore,
        "invalid stack limit leaves prior state unchanged");
    invalid = limits;
    invalid.maxDelayedSpans =
        kisak::fastfile::ZONE_STREAM_DELAYED_CAPACITY + 1u;
    RequireError(stream.Initialize(sizes, invalid),
        ZoneStreamError::InvalidArgument,
        "delayed limit cannot exceed the native hard capacity");
    Require(stream.ActiveBlock() == activeBefore &&
            stream.StackDepth() == depthBefore,
        "invalid delayed limit leaves prior state unchanged");
}

void TestBlockZeroRewindAndHighWater()
{
    ZoneStreamMachine stream;
    RequireError(stream.Initialize(Declared(0u, 12u)), ZoneStreamError::None,
        "block-zero fixture initializes");
    RequireError(stream.Push(0u), ZoneStreamError::None,
        "outer temporary frame opens");

    ZoneLoadPlan outer;
    RequireError(stream.PlanLoad(4u, 5u, outer), ZoneStreamError::None,
        "outer temporary record plans");
    Require(outer == ZoneLoadPlan{{0u, 0u, 5u}, ZoneLoadKind::Immediate},
        "block zero consumes input immediately");

    RequireError(stream.Push(0u), ZoneStreamError::None,
        "nested temporary frame opens at the outer cursor");
    ZoneLoadPlan nested;
    RequireError(stream.PlanLoad(8u, 4u, nested), ZoneStreamError::None,
        "nested temporary record plans");
    Require(nested.span == ZoneSpan{0u, 8u, 4u},
        "alignment advances the logical cursor before the nested record");
    Require(stream.Cursor(0u) == 12u && stream.HighWater(0u) == 12u,
        "nested allocation establishes the temporary peak");
    RequireError(stream.Pop(), ZoneStreamError::None,
        "nested temporary frame closes");
    Require(stream.Cursor(0u) == 5u && stream.HighWater(0u) == 12u,
        "block-zero pop rewinds its cursor but preserves high water");

    ZoneLoadPlan reused;
    RequireError(stream.PlanLoad(2u, 3u, reused), ZoneStreamError::None,
        "outer temporary storage can be reused after nested rewind");
    Require(reused.span == ZoneSpan{0u, 6u, 3u},
        "reused temporary span follows checked alignment");
    Require(stream.Cursor(0u) == 9u && stream.HighWater(0u) == 12u,
        "reuse below the peak does not reduce high water");
    RequireError(stream.Pop(), ZoneStreamError::None,
        "outer temporary frame closes");
    Require(stream.Cursor(0u) == 0u && stream.HighWater(0u) == 12u,
        "outer pop returns block zero to its entry cursor");
    RequireError(stream.ValidateComplete(), ZoneStreamError::None,
        "exact block-zero high water validates after terminal rewind");
}

void TestPersistentImmediateBlocks()
{
    std::array<std::uint32_t, kisak::fastfile::ZONE_STREAM_BLOCK_COUNT> sizes{};
    for (std::uint32_t block = 4u; block < sizes.size(); ++block)
    {
        sizes[block] = 4u;
    }
    ZoneStreamMachine stream;
    RequireError(stream.Initialize(sizes), ZoneStreamError::None,
        "persistent immediate fixture initializes");

    for (std::uint32_t block = 4u; block < sizes.size(); ++block)
    {
        RequireError(stream.Push(block), ZoneStreamError::None,
            "persistent immediate frame opens");
        ZoneLoadPlan plan;
        RequireError(stream.PlanLoad(1u, 4u, plan), ZoneStreamError::None,
            "persistent immediate span plans");
        Require(plan == ZoneLoadPlan{{block, 0u, 4u}, ZoneLoadKind::Immediate},
            "blocks four through eight consume bytes immediately");
        RequireError(stream.Pop(), ZoneStreamError::None,
            "persistent immediate frame closes");
        Require(stream.Cursor(block) == 4u && stream.HighWater(block) == 4u,
            "nonzero block pop preserves its advanced cursor");
        Require(stream.ActiveBlock() == 0u,
            "persistent frame returns to its prior active block");
    }
    RequireError(stream.ValidateComplete(), ZoneStreamError::None,
        "persistent immediate blocks validate at exact declarations");
}

void TestZeroFillReserveAndAlignment()
{
    std::array<std::uint32_t, kisak::fastfile::ZONE_STREAM_BLOCK_COUNT> sizes{};
    sizes[1] = 10u;
    sizes[4] = 5u;
    sizes[2] = 4u;
    ZoneStreamMachine stream;
    RequireError(stream.Initialize(sizes), ZoneStreamError::None,
        "zero-fill and alignment fixture initializes");

    RequireError(stream.Push(1u), ZoneStreamError::None,
        "runtime block opens");
    ZoneLoadPlan zero;
    RequireError(stream.PlanLoad(4u, 3u, zero), ZoneStreamError::None,
        "runtime load plans");
    Require(zero == ZoneLoadPlan{{1u, 0u, 3u}, ZoneLoadKind::ZeroFill},
        "block one synthesizes zeroes without serialized input");
    ZoneSpan reserved{7u, 7u, 7u};
    RequireError(stream.Reserve(8u, 2u, reserved), ZoneStreamError::None,
        "input-free reservation succeeds in any logical block");
    Require(reserved == ZoneSpan{1u, 8u, 2u},
        "reservation includes logical alignment but no load plan");
    Require(stream.DelayedSpanCount() == 0u && stream.DelayedBytes() == 0u,
        "zero-fill and Reserve do not create delayed serialized reads");
    RequireError(stream.Pop(), ZoneStreamError::None,
        "runtime block closes persistently");

    RequireError(stream.Push(4u), ZoneStreamError::None,
        "virtual block opens");
    ZoneLoadPlan first;
    ZoneLoadPlan second;
    RequireError(stream.PlanLoad(1u, 1u, first), ZoneStreamError::None,
        "first immediate byte plans");
    RequireError(stream.PlanLoad(4u, 1u, second), ZoneStreamError::None,
        "aligned immediate byte plans");
    Require(first.span == ZoneSpan{4u, 0u, 1u} &&
            second.span == ZoneSpan{4u, 4u, 1u},
        "three alignment bytes exist only in logical arena offsets");
    Require(first.span.length + second.span.length == 2u,
        "alignment gap does not appear in serialized span lengths");
    RequireError(stream.Pop(), ZoneStreamError::None,
        "virtual block closes persistently");

    RequireError(stream.Push(2u), ZoneStreamError::None,
        "delayed block opens for reservation check");
    ZoneSpan delayedReservation;
    RequireError(stream.Reserve(1u, 4u, delayedReservation),
        ZoneStreamError::None,
        "Reserve in a delayed block remains input-free");
    Require(stream.DelayedSpanCount() == 0u,
        "Reserve never enqueues delayed data");
    RequireError(stream.Pop(), ZoneStreamError::None,
        "delayed reservation block closes");
    RequireError(stream.ValidateComplete(), ZoneStreamError::None,
        "zero-fill, reservation, and alignment use exact logical extents");
}

void TestDelayedFifoReplay()
{
    std::array<std::uint32_t, kisak::fastfile::ZONE_STREAM_BLOCK_COUNT> sizes{};
    sizes[2] = 6u;
    sizes[3] = 3u;
    sizes[4] = 2u;
    ZoneStreamLimits limits;
    limits.maxDelayedSpans = 3u;
    limits.maxDelayedBytes = 7u;
    ZoneStreamMachine stream;
    RequireError(stream.Initialize(sizes, limits), ZoneStreamError::None,
        "delayed FIFO fixture initializes");

    RequireError(stream.Push(2u), ZoneStreamError::None,
        "first delayed block opens");
    ZoneLoadPlan first;
    RequireError(stream.PlanLoad(1u, 2u, first), ZoneStreamError::None,
        "first delayed span plans");
    Require(first == ZoneLoadPlan{{2u, 0u, 2u}, ZoneLoadKind::Delayed},
        "block two load is delayed");
    RequireError(stream.Pop(), ZoneStreamError::None,
        "first delayed block closes persistently");

    RequireError(stream.Push(4u), ZoneStreamError::None,
        "immediate block interleaves delayed plans");
    ZoneLoadPlan immediate;
    RequireError(stream.PlanLoad(1u, 2u, immediate), ZoneStreamError::None,
        "interleaved immediate plan succeeds");
    Require(immediate.kind == ZoneLoadKind::Immediate,
        "interleaved immediate span is not queued");
    RequireError(stream.Pop(), ZoneStreamError::None,
        "interleaved immediate block closes");

    RequireError(stream.Push(3u), ZoneStreamError::None,
        "second delayed block opens");
    ZoneLoadPlan second;
    RequireError(stream.PlanLoad(1u, 3u, second), ZoneStreamError::None,
        "second delayed span plans");
    RequireError(stream.Pop(), ZoneStreamError::None,
        "second delayed block closes");

    RequireError(stream.Push(2u), ZoneStreamError::None,
        "first delayed block reopens persistently");
    ZoneLoadPlan third;
    RequireError(stream.PlanLoad(4u, 2u, third), ZoneStreamError::None,
        "third delayed span plans after a logical alignment gap");
    Require(third.span == ZoneSpan{2u, 4u, 2u},
        "delayed alignment advances its arena without a queued padding span");
    RequireError(stream.Pop(), ZoneStreamError::None,
        "reopened delayed block closes");

    const std::array<ZoneSpan, 3> expected = {{
        {2u, 0u, 2u},
        {3u, 0u, 3u},
        {2u, 4u, 2u},
    }};
    Require(stream.DelayedSpanCount() == expected.size() &&
            stream.DelayedBytes() == 7u,
        "delayed queue tracks bounded cumulative records and bytes");
    for (std::uint32_t index = 0u; index < expected.size(); ++index)
    {
        ZoneSpan stored{8u, 8u, 8u};
        Require(stream.GetDelayedSpan(index, stored) && stored == expected[index],
            "delayed queue retains checked original triples in visit order");
    }

    RequireError(stream.ValidateComplete(),
        ZoneStreamError::DelayedReplayIncomplete,
        "queued spans must replay before completion");
    RequireError(stream.BeginDelayedReplay(), ZoneStreamError::None,
        "delayed replay begins after traversal frames close");
    ZoneSpan remaining;
    Require(stream.PeekDelayed(remaining) && remaining == expected[0],
        "FIFO replay begins with the first queued triple");
    RequireError(stream.ConsumeDelayed(1u), ZoneStreamError::None,
        "delayed replay supports a partial per-frame consumption");
    Require(stream.PeekDelayed(remaining) &&
            remaining == ZoneSpan{2u, 1u, 1u},
        "peek exposes the unconsumed suffix without mutating the stored triple");
    RequireError(stream.ConsumeDelayed(1u), ZoneStreamError::None,
        "first delayed record completes");
    Require(stream.PeekDelayed(remaining) && remaining == expected[1],
        "FIFO replay advances to the second block without regrouping");
    RequireError(stream.ConsumeDelayed(3u), ZoneStreamError::None,
        "second delayed record completes");
    Require(stream.PeekDelayed(remaining) && remaining == expected[2],
        "FIFO replay advances to the final original record");
    RequireError(stream.ConsumeDelayed(2u), ZoneStreamError::None,
        "final delayed record completes");
    Require(!stream.PeekDelayed(remaining) && stream.DelayedReplayComplete() &&
            stream.DelayedBytesConsumed() == 7u,
        "replay terminates only after every delayed byte is consumed");
    RequireError(stream.ValidateComplete(), ZoneStreamError::None,
        "immediate and FIFO-delayed extents validate together");
}

void TestLimitsAndOperationAtomicity()
{
    {
        ZoneStreamLimits limits;
        limits.maxStackDepth = 2u;
        ZoneStreamMachine stream;
        RequireError(stream.Initialize({}, limits),
            ZoneStreamError::None,
            "bounded stack fixture initializes");
        RequireError(stream.Push(4u), ZoneStreamError::None,
            "first bounded frame opens");
        RequireError(stream.Push(0u), ZoneStreamError::None,
            "second bounded frame opens");
        RequireError(stream.Push(1u), ZoneStreamError::StackDepthLimit,
            "configured frame depth is enforced");
        Require(stream.StackDepth() == 2u && stream.ActiveBlock() == 0u,
            "failed push leaves stack and active block unchanged");
        RequireError(stream.Pop(), ZoneStreamError::None,
            "bounded stack unwinds second frame");
        RequireError(stream.Pop(), ZoneStreamError::None,
            "bounded stack unwinds first frame");
        RequireError(stream.Pop(), ZoneStreamError::StackUnderflow,
            "pop below zero is rejected atomically");
        Require(stream.StackDepth() == 0u && stream.ActiveBlock() == 0u,
            "failed pop leaves empty stack state unchanged");
    }

    {
        ZoneStreamLimits limits;
        limits.maxDelayedSpans = 1u;
        limits.maxDelayedBytes = 3u;
        ZoneStreamMachine stream;
        RequireError(stream.Initialize(Declared(2u, 5u), limits),
            ZoneStreamError::None,
            "delayed span-limit fixture initializes");
        RequireError(stream.Push(2u), ZoneStreamError::None,
            "delayed span-limit block opens");
        ZoneLoadPlan accepted;
        RequireError(stream.PlanLoad(1u, 2u, accepted), ZoneStreamError::None,
            "first bounded delayed span plans");
        ZoneLoadPlan unchanged{{8u, 9u, 10u}, ZoneLoadKind::ZeroFill};
        const ZoneLoadPlan before = unchanged;
        RequireError(stream.PlanLoad(1u, 1u, unchanged),
            ZoneStreamError::DelayedSpanLimit,
            "configured delayed span count is enforced");
        Require(unchanged == before && stream.Cursor(2u) == 2u &&
                stream.HighWater(2u) == 2u &&
                stream.DelayedSpanCount() == 1u && stream.DelayedBytes() == 2u,
            "failed delayed-span plan is atomic");
    }

    {
        ZoneStreamLimits limits;
        limits.maxDelayedSpans = 2u;
        limits.maxDelayedBytes = 3u;
        ZoneStreamMachine stream;
        RequireError(stream.Initialize(Declared(3u, 5u), limits),
            ZoneStreamError::None,
            "delayed byte-limit fixture initializes");
        RequireError(stream.Push(3u), ZoneStreamError::None,
            "delayed byte-limit block opens");
        ZoneLoadPlan accepted;
        RequireError(stream.PlanLoad(1u, 2u, accepted), ZoneStreamError::None,
            "first delayed bytes fit");
        ZoneLoadPlan unchanged{{8u, 9u, 10u}, ZoneLoadKind::ZeroFill};
        const ZoneLoadPlan before = unchanged;
        RequireError(stream.PlanLoad(1u, 2u, unchanged),
            ZoneStreamError::DelayedByteLimit,
            "configured delayed byte total is enforced");
        Require(unchanged == before && stream.Cursor(3u) == 2u &&
                stream.DelayedSpanCount() == 1u && stream.DelayedBytes() == 2u,
            "failed delayed-byte plan is atomic");
    }

    {
        ZoneStreamMachine stream;
        RequireError(stream.Initialize(Declared(4u, 4u)), ZoneStreamError::None,
            "span failure fixture initializes");
        RequireError(stream.Push(4u), ZoneStreamError::None,
            "span failure block opens");
        ZoneLoadPlan unchanged{{8u, 9u, 10u}, ZoneLoadKind::ZeroFill};
        const ZoneLoadPlan before = unchanged;
        RequireError(stream.PlanLoad(3u, 1u, unchanged),
            ZoneStreamError::InvalidArgument,
            "non-power-of-two alignment is rejected");
        Require(unchanged == before && stream.Cursor(4u) == 0u,
            "invalid alignment changes neither output nor cursor");
        RequireError(stream.PlanLoad(1u, 5u, unchanged),
            ZoneStreamError::BlockOverflow,
            "span beyond a declared block is rejected");
        Require(unchanged == before && stream.Cursor(4u) == 0u &&
                stream.HighWater(4u) == 0u,
            "overflow changes neither output nor arena state");

        ZoneLoadPlan prefix;
        RequireError(stream.PlanLoad(1u, 1u, prefix), ZoneStreamError::None,
            "one byte creates an alignment-overflow precondition");

        ZoneSpan reserve{7u, 7u, 7u};
        const ZoneSpan reserveBefore = reserve;
        RequireError(stream.Reserve(8u, 1u, reserve),
            ZoneStreamError::BlockOverflow,
            "aligned reservation beyond a declared block is rejected");
        Require(reserve == reserveBefore && stream.Cursor(4u) == 1u &&
                stream.HighWater(4u) == 1u,
            "failed Reserve leaves output and cursor unchanged");
    }
}

void TestNativeHardDefaults()
{
    const ZoneStreamLimits defaults;
    Require(defaults.maxStackDepth == 64u &&
            defaults.maxStackDepth ==
                kisak::fastfile::ZONE_STREAM_STACK_CAPACITY,
        "default frame depth matches the native 64-entry stack");
    Require(defaults.maxDelayedSpans == 4096u &&
            defaults.maxDelayedSpans ==
                kisak::fastfile::ZONE_STREAM_DELAYED_CAPACITY,
        "default delayed span count matches the native 4096-entry queue");

    ZoneStreamMachine stack;
    RequireError(stack.Initialize({}), ZoneStreamError::None,
        "hard stack-capacity fixture initializes");
    for (std::uint32_t depth = 0u;
         depth < kisak::fastfile::ZONE_STREAM_STACK_CAPACITY;
         ++depth)
    {
        RequireError(stack.Push(0u), ZoneStreamError::None,
            "every native stack entry is usable");
    }
    RequireError(stack.Push(0u), ZoneStreamError::StackDepthLimit,
        "sixty-fifth native stack entry is rejected");
    Require(stack.StackDepth() == kisak::fastfile::ZONE_STREAM_STACK_CAPACITY,
        "hard stack-limit failure is atomic");
    for (std::uint32_t depth = 0u;
         depth < kisak::fastfile::ZONE_STREAM_STACK_CAPACITY;
         ++depth)
    {
        RequireError(stack.Pop(), ZoneStreamError::None,
            "native-capacity stack unwinds");
    }

    ZoneStreamMachine delayed;
    RequireError(delayed.Initialize(Declared(
            2u, kisak::fastfile::ZONE_STREAM_DELAYED_CAPACITY + 1u)),
        ZoneStreamError::None,
        "hard delayed-capacity fixture initializes");
    RequireError(delayed.Push(2u), ZoneStreamError::None,
        "hard delayed-capacity block opens");
    ZoneLoadPlan plan;
    for (std::uint32_t index = 0u;
         index < kisak::fastfile::ZONE_STREAM_DELAYED_CAPACITY;
         ++index)
    {
        RequireError(delayed.PlanLoad(1u, 1u, plan), ZoneStreamError::None,
            "every native delayed queue entry is usable");
    }
    const std::uint32_t cursorBefore = delayed.Cursor(2u);
    RequireError(delayed.PlanLoad(1u, 1u, plan),
        ZoneStreamError::DelayedSpanLimit,
        "4097th native delayed queue entry is rejected");
    Require(delayed.Cursor(2u) == cursorBefore &&
            delayed.DelayedSpanCount() ==
                kisak::fastfile::ZONE_STREAM_DELAYED_CAPACITY,
        "hard delayed-limit failure is atomic");
}

void TestReplayStateAndCompletionFailures()
{
    ZoneStreamMachine open;
    RequireError(open.Initialize({}), ZoneStreamError::None,
        "open-frame replay fixture initializes");
    RequireError(open.Push(4u), ZoneStreamError::None,
        "frame opens before replay");
    RequireError(open.BeginDelayedReplay(), ZoneStreamError::UnbalancedStack,
        "replay cannot begin while loader frames remain open");
    Require(!open.DelayedReplayStarted() && open.StackDepth() == 1u,
        "failed replay transition is atomic");

    ZoneStreamMachine incomplete;
    RequireError(incomplete.Initialize(Declared(4u, 2u)),
        ZoneStreamError::None,
        "incomplete block fixture initializes");
    RequireError(incomplete.ValidateComplete(),
        ZoneStreamError::BlockSizeMismatch,
        "unused declared arena tail is rejected");

    ZoneStreamMachine replay;
    RequireError(replay.Initialize(Declared(2u, 1u)), ZoneStreamError::None,
        "replay-state fixture initializes");
    RequireError(replay.Push(2u), ZoneStreamError::None,
        "replay-state delayed block opens");
    ZoneLoadPlan delayed;
    RequireError(replay.PlanLoad(1u, 1u, delayed), ZoneStreamError::None,
        "replay-state delayed span plans");
    RequireError(replay.Pop(), ZoneStreamError::None,
        "replay-state delayed block closes");
    RequireError(replay.BeginDelayedReplay(), ZoneStreamError::None,
        "replay state becomes sealed");

    const std::uint32_t cursorBefore = replay.Cursor(2u);
    RequireError(replay.Push(4u), ZoneStreamError::ReplayState,
        "push is forbidden after traversal is sealed");
    ZoneLoadPlan unchanged{{8u, 9u, 10u}, ZoneLoadKind::ZeroFill};
    const ZoneLoadPlan before = unchanged;
    RequireError(replay.PlanLoad(1u, 1u, unchanged),
        ZoneStreamError::ReplayState,
        "new load is forbidden during delayed replay");
    Require(unchanged == before && replay.Cursor(2u) == cursorBefore,
        "failed post-replay load is atomic");
    RequireError(replay.ConsumeDelayed(2u),
        ZoneStreamError::DelayedConsumeInvalid,
        "replay cannot consume beyond the current triple");
    Require(replay.DelayedBytesConsumed() == 0u,
        "failed delayed consumption is atomic");
    RequireError(replay.ConsumeDelayed(1u), ZoneStreamError::None,
        "exact delayed suffix consumption succeeds");
    RequireError(replay.ConsumeDelayed(1u),
        ZoneStreamError::DelayedConsumeInvalid,
        "replay cannot consume after FIFO completion");
    RequireError(replay.ValidateComplete(), ZoneStreamError::None,
        "sealed and fully replayed stream validates");
}

void TestErrorStrings()
{
    constexpr std::array<ZoneStreamError, 16> errors = {
        ZoneStreamError::None,
        ZoneStreamError::NotInitialized,
        ZoneStreamError::InvalidArgument,
        ZoneStreamError::ArenaSizeLimit,
        ZoneStreamError::BlockOverflow,
        ZoneStreamError::StackDepthLimit,
        ZoneStreamError::StackUnderflow,
        ZoneStreamError::ReplayState,
        ZoneStreamError::DelayedSpanLimit,
        ZoneStreamError::DelayedByteLimit,
        ZoneStreamError::DelayedConsumeInvalid,
        ZoneStreamError::UnbalancedStack,
        ZoneStreamError::BlockSizeMismatch,
        ZoneStreamError::DelayedReplayIncomplete,
        ZoneStreamError::AllocationFailed,
        static_cast<ZoneStreamError>(0xffu),
    };
    for (ZoneStreamError error : errors)
    {
        Require(std::strlen(kisak::fastfile::ZoneStreamErrorString(error)) != 0u,
            "every stream-machine result has printable text");
    }
}

} // namespace

int main()
{
    TestInitializationAndAtomicFailure();
    TestBlockZeroRewindAndHighWater();
    TestPersistentImmediateBlocks();
    TestZeroFillReserveAndAlignment();
    TestDelayedFifoReplay();
    TestLimitsAndOperationAtomicity();
    TestNativeHardDefaults();
    TestReplayStateAndCompletionFailures();
    TestErrorStrings();
    std::cout << "web_fastfile_zone_stream_tests: PASS\n";
    return 0;
}
