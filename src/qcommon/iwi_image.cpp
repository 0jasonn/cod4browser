#include <qcommon/iwi_image.h>

#include <cstddef>
#include <limits>

namespace kisak::iwi
{
namespace
{
constexpr std::uint8_t TAG[] = {'I', 'W', 'i'};
constexpr std::uint8_t MIN_FORMAT = 1u;
constexpr std::uint8_t MAX_FORMAT = 13u;
constexpr std::uint8_t FLAG_NO_PICMIP = 0x01u;
constexpr std::uint16_t MIN_PICMIP_DIMENSION = 32u;
constexpr std::size_t ARGB_BYTES_PER_PIXEL = 4u;

std::uint16_t ReadLe16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1u]) << 8u));
}

std::uint32_t ReadLe32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
        (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
        (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

std::uint32_t CountMipmaps(
    std::uint8_t flags,
    std::uint16_t width,
    std::uint16_t height,
    std::uint16_t depth) noexcept
{
    // Preserve Image_CountMipmaps from r_image.cpp. A no-mipmap image has one
    // level; otherwise count powers of two until every dimension is covered.
    if ((flags & FLAG_NO_MIPMAPS) != 0u)
    {
        return 1u;
    }

    std::uint32_t count = 1u;
    for (std::uint32_t resolution = 1u;
         resolution < width || resolution < height || resolution < depth;
         resolution *= 2u)
    {
        ++count;
    }
    return count;
}

bool CheckedMultiply(
    std::size_t left,
    std::size_t right,
    std::size_t &product) noexcept
{
    if (left != 0u && right > std::numeric_limits<std::size_t>::max() / left)
    {
        return false;
    }
    product = left * right;
    return true;
}

bool CheckedAdd(
    std::size_t left,
    std::size_t right,
    std::size_t &sum) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left)
    {
        return false;
    }
    sum = left + right;
    return true;
}
} // namespace

const char *ErrorString(Error error) noexcept
{
    switch (error)
    {
    case Error::None: return "no error";
    case Error::HeaderTruncated: return "IWI header is truncated";
    case Error::InvalidTag: return "IWI tag is invalid";
    case Error::UnsupportedVersion: return "IWI version is unsupported";
    case Error::UnsupportedFormat: return "IWI pixel format is unsupported";
    case Error::InvalidDimensions: return "IWI dimensions are invalid";
    case Error::InvalidFileSize: return "IWI file-size metadata is invalid";
    case Error::DecodeUnsupportedFormat:
        return "IWI format is unsupported by the RGBA8 decoder";
    case Error::DecodeUnsupportedFlags:
        return "IWI flags are unsupported by the RGBA8 decoder";
    case Error::DecodeUnsupportedDimensions:
        return "IWI dimensions are unsupported by the RGBA8 decoder";
    case Error::DecodeInvalidLayout:
        return "IWI pixel payload layout is invalid";
    case Error::DecodeOutputTooLarge:
        return "IWI texture exceeds the RGBA8 decoder limit";
    case Error::DecodeAllocationFailed:
        return "IWI RGBA8 pixel allocation failed";
    }
    return "unknown IWI error";
}

Error Parse(std::span<const std::uint8_t> bytes, Metadata &metadata) noexcept
{
    if (bytes.size() < HEADER_SIZE)
    {
        return Error::HeaderTruncated;
    }
    if (bytes[0] != TAG[0] || bytes[1] != TAG[1] || bytes[2] != TAG[2])
    {
        return Error::InvalidTag;
    }

    Metadata parsed{};
    parsed.version = bytes[3];
    if (parsed.version != COD4_VERSION)
    {
        return Error::UnsupportedVersion;
    }

    parsed.format = bytes[4];
    if (parsed.format < MIN_FORMAT || parsed.format > MAX_FORMAT)
    {
        return Error::UnsupportedFormat;
    }
    parsed.flags = bytes[5];
    parsed.width = ReadLe16(bytes, 6u);
    parsed.height = ReadLe16(bytes, 8u);
    parsed.depth = ReadLe16(bytes, 10u);

    constexpr auto MAX_SIGNED_DIMENSION =
        static_cast<std::uint16_t>(std::numeric_limits<std::int16_t>::max());
    if (parsed.width == 0u || parsed.height == 0u || parsed.depth == 0u ||
        parsed.width > MAX_SIGNED_DIMENSION ||
        parsed.height > MAX_SIGNED_DIMENSION ||
        parsed.depth > MAX_SIGNED_DIMENSION)
    {
        return Error::InvalidDimensions;
    }

    for (std::size_t index = 0; index < parsed.fileSizeForPicmip.size(); ++index)
    {
        parsed.fileSizeForPicmip[index] = ReadLe32(bytes, 12u + index * 4u);
    }
    // The native loader stores these values in signed ints and subtracts
    // HEADER_SIZE before allocating and reading. It disables picmip when flag
    // bit 0 or 1 is set, or for a small 2D extent; otherwise the semantic can select
    // any slot from 0 through 3. Every selectable prefix must therefore hold
    // payload bytes, while zero remains valid only as an unused trailing slot.
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) ||
        parsed.fileSizeForPicmip[0] != bytes.size())
    {
        return Error::InvalidFileSize;
    }
    const bool picmipDisabled =
        (parsed.flags & (FLAG_NO_PICMIP | FLAG_NO_MIPMAPS)) != 0u ||
        parsed.width < MIN_PICMIP_DIMENSION ||
        parsed.height < MIN_PICMIP_DIMENSION;
    const std::size_t lastSelectablePicmip = picmipDisabled ? 0u : 3u;
    for (std::size_t index = 0; index < parsed.fileSizeForPicmip.size(); ++index)
    {
        const std::uint32_t size = parsed.fileSizeForPicmip[index];
        if ((index <= lastSelectablePicmip && size == 0u) ||
            (size != 0u && (size <= HEADER_SIZE || size > bytes.size())) ||
            (index != 0u && size > parsed.fileSizeForPicmip[index - 1u]))
        {
            return Error::InvalidFileSize;
        }
    }

    parsed.mipCount = CountMipmaps(
        parsed.flags, parsed.width, parsed.height, parsed.depth);
    metadata = parsed;
    return Error::None;
}

Error DecodeRgba8(
    std::span<const std::uint8_t> bytes,
    Rgba8Image &image) noexcept
{
    Metadata metadata{};
    const Error parseError = Parse(bytes, metadata);
    if (parseError != Error::None)
    {
        return parseError;
    }

    if (metadata.format != FORMAT_ARGB)
    {
        return Error::DecodeUnsupportedFormat;
    }
    if (metadata.flags != FLAG_NO_MIPMAPS)
    {
        return Error::DecodeUnsupportedFlags;
    }
    if (metadata.depth != 1u || metadata.mipCount != 1u)
    {
        return Error::DecodeUnsupportedDimensions;
    }

    std::size_t pixelCount = 0u;
    std::size_t pixelBytes = 0u;
    std::size_t expectedMemberBytes = 0u;
    if (!CheckedMultiply(metadata.width, metadata.height, pixelCount) ||
        !CheckedMultiply(pixelCount, ARGB_BYTES_PER_PIXEL, pixelBytes) ||
        !CheckedAdd(HEADER_SIZE, pixelBytes, expectedMemberBytes))
    {
        return Error::DecodeOutputTooLarge;
    }
    if (bytes.size() > MAX_TEXTURE_MEMBER_BYTES ||
        expectedMemberBytes > MAX_TEXTURE_MEMBER_BYTES)
    {
        return Error::DecodeOutputTooLarge;
    }
    if (bytes.size() != expectedMemberBytes)
    {
        return Error::DecodeInvalidLayout;
    }

    std::vector<std::uint8_t> rgba;
    try
    {
        rgba.resize(pixelBytes);
    }
    catch (...)
    {
        return Error::DecodeAllocationFailed;
    }

    const std::span<const std::uint8_t> bgra = bytes.subspan(HEADER_SIZE);
    for (std::size_t offset = 0u; offset < pixelBytes; offset += ARGB_BYTES_PER_PIXEL)
    {
        rgba[offset] = bgra[offset + 2u];
        rgba[offset + 1u] = bgra[offset + 1u];
        rgba[offset + 2u] = bgra[offset];
        rgba[offset + 3u] = bgra[offset + 3u];
    }

    image.width = metadata.width;
    image.height = metadata.height;
    image.pixels.swap(rgba);
    return Error::None;
}

} // namespace kisak::iwi
