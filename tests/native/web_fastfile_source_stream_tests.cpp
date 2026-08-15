#include <web/web_fastfile_source_stream.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

namespace
{
using kisak::fastfile::BoundedSourceStream;
using kisak::fastfile::SourceStreamError;
using kisak::fastfile::SourceStreamLimits;

void Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void RequireError(
    SourceStreamError actual,
    SourceStreamError expected,
    std::string_view message)
{
    if (actual != expected)
    {
        std::cerr << "FAIL: " << message << ": expected "
                  << kisak::fastfile::SourceStreamErrorString(expected)
                  << ", got "
                  << kisak::fastfile::SourceStreamErrorString(actual) << '\n';
        std::exit(1);
    }
}

bool SameBytes(
    std::span<const std::uint8_t> left,
    std::span<const std::uint8_t> right)
{
    return left.size() == right.size() &&
        std::equal(left.begin(), left.end(), right.begin());
}

void TestFeedBackpressureAndConsumption()
{
    BoundedSourceStream source;
    RequireError(source.Initialize({8u, 4u}), SourceStreamError::None,
        "bounded source initializes");
    Require(source.NeedsSource() && !source.FinalReceived(),
        "empty initialized source requests its first chunk");

    const std::array<std::uint8_t, 4> first = {1u, 2u, 3u, 4u};
    RequireError(source.Feed(first, false), SourceStreamError::None,
        "first bounded chunk is accepted");
    Require(!source.NeedsSource() && source.AvailableBytes() == 4u &&
            source.FeedCount() == 1u && source.TotalBytesReceived() == 4u,
        "feed publishes exact source metrics");
    Require(SameBytes(
            source.Peek(2u),
            std::span<const std::uint8_t>(first).first(2u)),
        "peek exposes only the requested prefix");

    const std::array<std::uint8_t, 1> next = {5u};
    RequireError(source.Feed(next, false), SourceStreamError::Backpressure,
        "unread bytes apply source backpressure");
    Require(source.AvailableBytes() == 4u && source.FeedCount() == 1u,
        "backpressure failure leaves source state unchanged");

    RequireError(source.Consume(2u), SourceStreamError::None,
        "partial source consumption succeeds");
    Require(SameBytes(
            source.Peek(),
            std::span<const std::uint8_t>(first).subspan(2u)),
        "partial consumption retains the unread suffix");
    RequireError(source.Consume(2u), SourceStreamError::None,
        "remaining source bytes are consumed");
    Require(source.NeedsSource() && source.TotalBytesConsumed() == 4u,
        "drained non-final chunk requests another source chunk");

    RequireError(source.Feed(next, true), SourceStreamError::None,
        "final nonempty chunk is accepted");
    Require(source.FinalReceived() && !source.Complete(),
        "final source remains incomplete while bytes are unread");
    RequireError(source.Consume(1u), SourceStreamError::None,
        "final byte is consumed");
    Require(source.Complete() && !source.NeedsSource() &&
            source.TotalBytesReceived() == 5u &&
            source.TotalBytesConsumed() == 5u,
        "explicit EOF completes only after the final chunk drains");
    RequireError(source.Feed({}, true), SourceStreamError::AlreadyFinal,
        "bytes cannot be fed after EOF");
}

void TestLimitsFinalMarkerAndReset()
{
    BoundedSourceStream source;
    RequireError(source.Initialize({5u, 3u}), SourceStreamError::None,
        "limit fixture initializes");
    const std::array<std::uint8_t, 4> tooLarge = {1u, 2u, 3u, 4u};
    RequireError(source.Feed(tooLarge, false), SourceStreamError::ChunkTooLarge,
        "one oversized source chunk is rejected atomically");
    Require(source.NeedsSource() && source.TotalBytesReceived() == 0u,
        "chunk-limit failure consumes no source state");

    const std::array<std::uint8_t, 3> first = {1u, 2u, 3u};
    RequireError(source.Feed(first, false), SourceStreamError::None,
        "exact maximum chunk is accepted");
    RequireError(source.Consume(3u), SourceStreamError::None,
        "exact maximum chunk drains");
    const std::array<std::uint8_t, 3> totalOverflow = {4u, 5u, 6u};
    RequireError(source.Feed(totalOverflow, true),
        SourceStreamError::TotalSizeLimit,
        "cumulative source limit rejects an otherwise bounded chunk");
    Require(source.TotalBytesReceived() == 3u && source.NeedsSource(),
        "total-limit failure leaves the source resumable");
    const std::array<std::uint8_t, 2> final = {4u, 5u};
    RequireError(source.Feed(final, false), SourceStreamError::None,
        "remaining exact total bytes are accepted");
    RequireError(source.Consume(2u), SourceStreamError::None,
        "remaining exact total bytes drain");
    RequireError(source.Feed({}, true), SourceStreamError::None,
        "empty final marker terminates a previously drained chunk");
    Require(source.Complete() && source.FeedCount() == 3u,
        "empty EOF marker participates in deterministic feed metrics");

    source.Reset();
    Require(!source.Initialized() && source.AvailableBytes() == 0u &&
            source.TotalBytesReceived() == 0u && source.FeedCount() == 0u,
        "reset releases source state and counters");
    RequireError(source.Feed(final, true), SourceStreamError::NotInitialized,
        "reset source rejects later feed until reinitialized");
}

void TestInvalidOperationsAndStrings()
{
    BoundedSourceStream source;
    RequireError(source.Initialize({0u, 1u}), SourceStreamError::InvalidArgument,
        "zero total source limit is invalid");
    RequireError(source.Initialize({4u, 0u}), SourceStreamError::InvalidArgument,
        "zero source chunk limit is invalid");
    RequireError(source.Initialize({4u, 5u}), SourceStreamError::InvalidArgument,
        "chunk limit cannot exceed cumulative source limit");
    RequireError(source.Initialize({4u, 4u}), SourceStreamError::None,
        "valid source reinitializes");
    RequireError(source.Feed({}, false), SourceStreamError::InvalidArgument,
        "empty non-final feed cannot fake progress");
    RequireError(source.Consume(0u), SourceStreamError::ConsumeInvalid,
        "zero consumption is rejected");
    RequireError(source.Consume(1u), SourceStreamError::ConsumeInvalid,
        "consumption cannot exceed available bytes");

    for (SourceStreamError error : {
            SourceStreamError::None,
            SourceStreamError::NotInitialized,
            SourceStreamError::InvalidArgument,
            SourceStreamError::ChunkTooLarge,
            SourceStreamError::TotalSizeLimit,
            SourceStreamError::Backpressure,
            SourceStreamError::AlreadyFinal,
            SourceStreamError::ConsumeInvalid,
            SourceStreamError::AllocationFailed})
    {
        Require(std::string_view(
                kisak::fastfile::SourceStreamErrorString(error)) !=
                "unknown source stream error",
            "every source-stream error has stable text");
    }
}
} // namespace

int main()
{
    TestFeedBackpressureAndConsumption();
    TestLimitsFinalMarkerAndReset();
    TestInvalidOperationsAndStrings();
    std::cout << "web_fastfile_source_stream_tests: PASS\n";
    return 0;
}
