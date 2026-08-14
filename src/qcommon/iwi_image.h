#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kisak::iwi
{

constexpr std::uint32_t HEADER_SIZE = 28u;
constexpr std::uint8_t COD4_VERSION = 6u;
constexpr std::uint8_t FORMAT_ARGB = 1u;
constexpr std::uint8_t FLAG_NO_MIPMAPS = 0x02u;

// The browser engine filesystem retains at most one complete 4 MiB member.
// Keep the portable texture boundary at the same limit so callers never need
// a second, less constrained path before handing bytes to the renderer.
constexpr std::size_t MAX_TEXTURE_MEMBER_BYTES = 4u * 1024u * 1024u;

enum class Error : std::uint32_t
{
    None = 0,
    HeaderTruncated,
    InvalidTag,
    UnsupportedVersion,
    UnsupportedFormat,
    InvalidDimensions,
    InvalidFileSize,
    DecodeUnsupportedFormat,
    DecodeUnsupportedFlags,
    DecodeUnsupportedDimensions,
    DecodeInvalidLayout,
    DecodeOutputTooLarge,
    DecodeAllocationFailed,
};

const char *ErrorString(Error error) noexcept;

struct Metadata
{
    std::uint8_t version = 0;
    std::uint8_t format = 0;
    std::uint8_t flags = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t depth = 0;
    std::array<std::uint32_t, 4> fileSizeForPicmip{};
    std::uint32_t mipCount = 0;
};

struct Rgba8Image
{
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<std::uint8_t> pixels;
};

// Parses the 28-byte CoD4 IWI header from a complete decoded member. The
// input remains owned by the caller and output is replaced only on success.
Error Parse(std::span<const std::uint8_t> bytes, Metadata &metadata) noexcept;

// Decodes the deliberately narrow first renderer texture slice from a complete
// IWI v6 member. Only format 1 (named ARGB by the engine, serialized as BGRA),
// flags == FLAG_NO_MIPMAPS, a two-dimensional depth of one, and one exact
// tightly packed mip are accepted. The returned pixels are tightly packed
// RGBA8 in row-major order. Input and output may alias; output is replaced only
// after the complete image has been validated, allocated, and converted.
Error DecodeRgba8(
    std::span<const std::uint8_t> bytes,
    Rgba8Image &image) noexcept;

} // namespace kisak::iwi
