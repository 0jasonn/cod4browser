#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kisak::fastfile
{

constexpr std::uint32_t SOURCE_STREAM_DEFAULT_CHUNK_BYTES = 64u * 1024u;

struct SourceStreamLimits
{
    std::uint32_t maxTotalBytes = 4u * 1024u * 1024u;
    std::uint32_t maxChunkBytes = SOURCE_STREAM_DEFAULT_CHUNK_BYTES;
};

enum class SourceStreamError : std::uint8_t
{
    None = 0,
    NotInitialized,
    InvalidArgument,
    ChunkTooLarge,
    TotalSizeLimit,
    Backpressure,
    AlreadyFinal,
    ConsumeInvalid,
    AllocationFailed,
};

const char *SourceStreamErrorString(SourceStreamError error) noexcept;

// A bounded, single-chunk queue between an asynchronous source and a decoder.
// Feed is atomic, applies backpressure while unread bytes remain, and requires
// an explicit final marker. Consumed chunks are released before the next feed,
// so a decoder never needs to retain the complete source allocation.
class BoundedSourceStream
{
public:
    SourceStreamError Initialize(const SourceStreamLimits &limits = {}) noexcept;
    SourceStreamError Feed(
        std::span<const std::uint8_t> bytes,
        bool final) noexcept;
    SourceStreamError Consume(std::uint32_t bytes) noexcept;

    std::span<const std::uint8_t> Peek(
        std::uint32_t maximumBytes = UINT32_MAX) const noexcept;

    bool Initialized() const noexcept;
    bool NeedsSource() const noexcept;
    bool FinalReceived() const noexcept;
    bool Complete() const noexcept;
    std::uint32_t AvailableBytes() const noexcept;
    std::uint32_t MaxChunkBytes() const noexcept;
    std::uint64_t TotalBytesReceived() const noexcept;
    std::uint64_t TotalBytesConsumed() const noexcept;
    std::uint32_t FeedCount() const noexcept;

    void Reset() noexcept;

private:
    std::vector<std::uint8_t> chunk_;
    SourceStreamLimits limits_{};
    std::size_t cursor_ = 0u;
    std::uint64_t totalReceived_ = 0u;
    std::uint64_t totalConsumed_ = 0u;
    std::uint32_t feedCount_ = 0u;
    bool initialized_ = false;
    bool finalReceived_ = false;
};

} // namespace kisak::fastfile
