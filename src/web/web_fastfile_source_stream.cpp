#include <web/web_fastfile_source_stream.h>

#include <algorithm>
#include <limits>
#include <new>

namespace kisak::fastfile
{

SourceStreamError BoundedSourceStream::Initialize(
    const SourceStreamLimits &limits) noexcept
{
    if (limits.maxTotalBytes == 0u || limits.maxChunkBytes == 0u ||
        limits.maxChunkBytes > limits.maxTotalBytes)
    {
        return SourceStreamError::InvalidArgument;
    }

    std::vector<std::uint8_t>().swap(chunk_);
    limits_ = limits;
    cursor_ = 0u;
    totalReceived_ = 0u;
    totalConsumed_ = 0u;
    feedCount_ = 0u;
    finalReceived_ = false;
    initialized_ = true;
    return SourceStreamError::None;
}

SourceStreamError BoundedSourceStream::Feed(
    std::span<const std::uint8_t> bytes,
    bool final) noexcept
{
    if (!initialized_)
    {
        return SourceStreamError::NotInitialized;
    }
    if ((bytes.data() == nullptr && !bytes.empty()) ||
        (bytes.empty() && !final))
    {
        return SourceStreamError::InvalidArgument;
    }
    if (finalReceived_)
    {
        return SourceStreamError::AlreadyFinal;
    }
    if (AvailableBytes() != 0u)
    {
        return SourceStreamError::Backpressure;
    }
    if (bytes.size() > limits_.maxChunkBytes ||
        bytes.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return SourceStreamError::ChunkTooLarge;
    }
    if (bytes.size() > limits_.maxTotalBytes - totalReceived_)
    {
        return SourceStreamError::TotalSizeLimit;
    }

    try
    {
        std::vector<std::uint8_t> replacement(bytes.begin(), bytes.end());
        chunk_.swap(replacement);
    }
    catch (const std::bad_alloc &)
    {
        return SourceStreamError::AllocationFailed;
    }
    catch (...)
    {
        return SourceStreamError::AllocationFailed;
    }

    cursor_ = 0u;
    totalReceived_ += bytes.size();
    ++feedCount_;
    finalReceived_ = final;
    return SourceStreamError::None;
}

SourceStreamError BoundedSourceStream::Consume(std::uint32_t bytes) noexcept
{
    if (!initialized_)
    {
        return SourceStreamError::NotInitialized;
    }
    if (bytes == 0u || bytes > AvailableBytes())
    {
        return SourceStreamError::ConsumeInvalid;
    }

    cursor_ += bytes;
    totalConsumed_ += bytes;
    if (cursor_ == chunk_.size())
    {
        std::vector<std::uint8_t>().swap(chunk_);
        cursor_ = 0u;
    }
    return SourceStreamError::None;
}

std::span<const std::uint8_t> BoundedSourceStream::Peek(
    std::uint32_t maximumBytes) const noexcept
{
    if (!initialized_ || AvailableBytes() == 0u)
    {
        return {};
    }
    const std::size_t count = std::min<std::size_t>(
        AvailableBytes(), maximumBytes);
    return std::span<const std::uint8_t>(chunk_).subspan(cursor_, count);
}

bool BoundedSourceStream::Initialized() const noexcept
{
    return initialized_;
}

bool BoundedSourceStream::NeedsSource() const noexcept
{
    return initialized_ && AvailableBytes() == 0u && !finalReceived_;
}

bool BoundedSourceStream::FinalReceived() const noexcept
{
    return finalReceived_;
}

bool BoundedSourceStream::Complete() const noexcept
{
    return initialized_ && finalReceived_ && AvailableBytes() == 0u;
}

std::uint32_t BoundedSourceStream::AvailableBytes() const noexcept
{
    if (cursor_ > chunk_.size() ||
        chunk_.size() - cursor_ > std::numeric_limits<std::uint32_t>::max())
    {
        return 0u;
    }
    return static_cast<std::uint32_t>(chunk_.size() - cursor_);
}

std::uint32_t BoundedSourceStream::MaxChunkBytes() const noexcept
{
    return initialized_ ? limits_.maxChunkBytes : 0u;
}

std::uint64_t BoundedSourceStream::TotalBytesReceived() const noexcept
{
    return totalReceived_;
}

std::uint64_t BoundedSourceStream::TotalBytesConsumed() const noexcept
{
    return totalConsumed_;
}

std::uint32_t BoundedSourceStream::FeedCount() const noexcept
{
    return feedCount_;
}

void BoundedSourceStream::Reset() noexcept
{
    std::vector<std::uint8_t>().swap(chunk_);
    limits_ = {};
    cursor_ = 0u;
    totalReceived_ = 0u;
    totalConsumed_ = 0u;
    feedCount_ = 0u;
    initialized_ = false;
    finalReceived_ = false;
}

const char *SourceStreamErrorString(SourceStreamError error) noexcept
{
    switch (error)
    {
    case SourceStreamError::None: return "success";
    case SourceStreamError::NotInitialized: return "source stream is not initialized";
    case SourceStreamError::InvalidArgument: return "source stream argument is invalid";
    case SourceStreamError::ChunkTooLarge: return "source chunk exceeds its limit";
    case SourceStreamError::TotalSizeLimit: return "source stream exceeds its total limit";
    case SourceStreamError::Backpressure: return "source stream still has unread bytes";
    case SourceStreamError::AlreadyFinal: return "source stream is already final";
    case SourceStreamError::ConsumeInvalid: return "source stream consumption is invalid";
    case SourceStreamError::AllocationFailed: return "source stream allocation failed";
    }
    return "unknown source stream error";
}

} // namespace kisak::fastfile
