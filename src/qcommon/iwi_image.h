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
constexpr std::uint8_t FORMAT_RGB8 = 2u;
constexpr std::uint8_t FORMAT_DXT1 = 11u;
constexpr std::uint8_t FORMAT_DXT3 = 12u;
constexpr std::uint8_t FORMAT_DXT5 = 13u;
constexpr std::uint8_t FLAG_NO_PICMIP = 0x01u;
constexpr std::uint8_t FLAG_NO_MIPMAPS = 0x02u;
constexpr std::uint8_t FLAG_CUBEMAP = 0x04u;
constexpr std::uint8_t FLAG_STREAMING = 0x10u;
constexpr std::uint8_t FLAG_CLAMP_U = 0x40u;
constexpr std::uint8_t FLAG_CLAMP_V = 0x80u;
constexpr std::int32_t LOADDEF_FORMAT_A8R8G8B8 = 0x00000015;
constexpr std::int32_t LOADDEF_FORMAT_X8R8G8B8 = 0x00000016;
constexpr std::int32_t LOADDEF_FORMAT_L8 = 0x00000032;
constexpr std::int32_t LOADDEF_FORMAT_DXT1 = 0x31545844;
constexpr std::int32_t LOADDEF_FORMAT_DXT3 = 0x33545844;
constexpr std::int32_t LOADDEF_FORMAT_DXT5 = 0x35545844;

// The browser engine filesystem retains at most one complete 4 MiB member.
// Keep the portable texture boundary at the same limit so callers never need
// a second, less constrained path before handing bytes to the renderer.
constexpr std::size_t MAX_TEXTURE_MEMBER_BYTES = 4u * 1024u * 1024u;
// A compressed IWI member can legitimately expand beyond its archive byte
// limit. 2048x1024 RGBA8 weapon/world images require 8 MiB after decode.
constexpr std::size_t MAX_DECODED_RGBA8_BYTES = 16u * 1024u * 1024u;
// Retail lightmap load definitions include 2048x2048 L8 atlases, which expand
// to exactly 16 MiB of RGBA8 while retaining only a 4 MiB DB payload.
constexpr std::size_t MAX_LOADDEF_RGBA8_BYTES = 16u * 1024u * 1024u;

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

// Decodes one bounded two-dimensional IWI v6 image into RGBA8. Format 1 (named
// ARGB by the engine, serialized as BGRA) retains its original strict one-mip
// boundary. Format 2 (opaque BGR8), DXT1, DXT3, and DXT5 accept an exact 2D
// mip chain in COD4's smallest-to-largest file order and decode only the largest level. The
// streaming and U/V clamp policy bits do not alter that layout and are
// accepted. Cubemaps, volumes, unknown flags, malformed block layouts, and
// output above the shared recovery ceiling fail closed. Input and output may alias; output is
// replaced only after complete validation, allocation, and conversion.
Error DecodeRgba8(
    std::span<const std::uint8_t> bytes,
    Rgba8Image &image) noexcept;

// Decodes the largest two-dimensional mip from a canonical DB-owned
// GfxImageLoadDef payload. Unlike an IWI member, this payload has no file
// header and stores mip levels in native upload order (largest to smallest).
// Canonical L8 load definitions expand luminance into opaque RGBA8 and the
// output is bounded by MAX_LOADDEF_RGBA8_BYTES for retail lightmap atlases.
// The scalar arguments mirror GfxImageLoadDef so qcommon remains independent
// of renderer ABI declarations. The destination is replaced only on success.
Error DecodeLoadDefRgba8(
    std::int32_t format,
    std::uint8_t flags,
    std::uint16_t width,
    std::uint16_t height,
    std::uint16_t depth,
    std::span<const std::uint8_t> payload,
    Rgba8Image &image) noexcept;

} // namespace kisak::iwi
