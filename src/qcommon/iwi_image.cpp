#include <qcommon/iwi_image.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace kisak::iwi
{
Error DecodeWaveletPayloadRgba8(
    std::uint8_t format,
    std::uint16_t width,
    std::uint16_t height,
    std::span<const std::uint8_t> payload,
    std::vector<std::uint8_t> &rgba,
    std::uint32_t firstMip) noexcept;

namespace
{
constexpr std::uint8_t TAG[] = {'I', 'W', 'i'};
constexpr std::uint8_t MIN_FORMAT = 1u;
constexpr std::uint8_t MAX_FORMAT = 13u;
constexpr std::uint16_t MIN_PICMIP_DIMENSION = 32u;
constexpr std::size_t ARGB_BYTES_PER_PIXEL = 4u;
constexpr std::size_t RGBA_BYTES_PER_PIXEL = 4u;
constexpr std::size_t MAX_LOADDEF_MIP_CHAIN_RGBA8_BYTES =
    24u * 1024u * 1024u;

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

std::uint32_t Count2dStoredMipmaps(
    std::uint8_t flags,
    std::uint16_t width,
    std::uint16_t height) noexcept
{
    if ((flags & FLAG_NO_MIPMAPS) != 0u) return 1u;
    std::uint32_t count = 1u;
    std::uint32_t currentWidth = width;
    std::uint32_t currentHeight = height;
    while (currentWidth > 1u || currentHeight > 1u)
    {
        currentWidth = std::max(currentWidth >> 1u, 1u);
        currentHeight = std::max(currentHeight >> 1u, 1u);
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

std::uint8_t Expand5(std::uint16_t value) noexcept
{
    return static_cast<std::uint8_t>((value << 3u) | (value >> 2u));
}

std::uint8_t Expand6(std::uint16_t value) noexcept
{
    return static_cast<std::uint8_t>((value << 2u) | (value >> 4u));
}

struct Rgba
{
    std::uint8_t red = 0u;
    std::uint8_t green = 0u;
    std::uint8_t blue = 0u;
    std::uint8_t alpha = 255u;
};

Rgba Decode565(std::uint16_t packed) noexcept
{
    return {
        Expand5(static_cast<std::uint16_t>((packed >> 11u) & 0x1fu)),
        Expand6(static_cast<std::uint16_t>((packed >> 5u) & 0x3fu)),
        Expand5(static_cast<std::uint16_t>(packed & 0x1fu)),
        255u,
    };
}

std::uint8_t Interpolate(
    std::uint8_t left,
    std::uint8_t right,
    std::uint32_t leftWeight,
    std::uint32_t rightWeight,
    std::uint32_t divisor) noexcept
{
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(left) * leftWeight +
         static_cast<std::uint32_t>(right) * rightWeight) / divisor);
}

void BuildColorTable(
    std::span<const std::uint8_t> block,
    bool allowDxt1Transparency,
    std::array<Rgba, 4> &colors) noexcept
{
    const std::uint16_t color0 = ReadLe16(block, 0u);
    const std::uint16_t color1 = ReadLe16(block, 2u);
    colors[0] = Decode565(color0);
    colors[1] = Decode565(color1);
    if (!allowDxt1Transparency || color0 > color1)
    {
        colors[2] = {
            Interpolate(colors[0].red, colors[1].red, 2u, 1u, 3u),
            Interpolate(colors[0].green, colors[1].green, 2u, 1u, 3u),
            Interpolate(colors[0].blue, colors[1].blue, 2u, 1u, 3u),
            255u,
        };
        colors[3] = {
            Interpolate(colors[0].red, colors[1].red, 1u, 2u, 3u),
            Interpolate(colors[0].green, colors[1].green, 1u, 2u, 3u),
            Interpolate(colors[0].blue, colors[1].blue, 1u, 2u, 3u),
            255u,
        };
    }
    else
    {
        colors[2] = {
            Interpolate(colors[0].red, colors[1].red, 1u, 1u, 2u),
            Interpolate(colors[0].green, colors[1].green, 1u, 1u, 2u),
            Interpolate(colors[0].blue, colors[1].blue, 1u, 1u, 2u),
            255u,
        };
        colors[3] = {0u, 0u, 0u, 0u};
    }
}

void BuildDxt5AlphaTable(
    std::span<const std::uint8_t> block,
    std::array<std::uint8_t, 8> &alpha) noexcept
{
    alpha[0] = block[0];
    alpha[1] = block[1];
    if (alpha[0] > alpha[1])
    {
        for (std::uint32_t index = 1u; index <= 6u; ++index)
        {
            alpha[index + 1u] = Interpolate(
                alpha[0], alpha[1], 7u - index, index, 7u);
        }
    }
    else
    {
        for (std::uint32_t index = 1u; index <= 4u; ++index)
        {
            alpha[index + 1u] = Interpolate(
                alpha[0], alpha[1], 5u - index, index, 5u);
        }
        alpha[6] = 0u;
        alpha[7] = 255u;
    }
}

std::uint64_t ReadLe48(std::span<const std::uint8_t> bytes) noexcept
{
    std::uint64_t value = 0u;
    for (std::size_t index = 0u; index < 6u; ++index)
    {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8u);
    }
    return value;
}

bool DxtLevelBytes(
    std::uint32_t width,
    std::uint32_t height,
    std::size_t bytesPerBlock,
    std::size_t &bytes) noexcept
{
    const std::size_t blockWidth = (static_cast<std::size_t>(width) + 3u) / 4u;
    const std::size_t blockHeight = (static_cast<std::size_t>(height) + 3u) / 4u;
    std::size_t blockCount = 0u;
    return CheckedMultiply(blockWidth, blockHeight, blockCount) &&
        CheckedMultiply(blockCount, bytesPerBlock, bytes);
}

void DecodeDxtBlock(
    std::span<const std::uint8_t> block,
    std::uint8_t format,
    std::uint32_t blockX,
    std::uint32_t blockY,
    std::uint32_t width,
    std::uint32_t height,
    std::span<std::uint8_t> rgba) noexcept
{
    const std::size_t colorOffset = format == FORMAT_DXT1 ? 0u : 8u;
    std::array<Rgba, 4> colors{};
    BuildColorTable(
        block.subspan(colorOffset, 8u),
        format == FORMAT_DXT1,
        colors);
    const std::uint32_t colorIndices = ReadLe32(block, colorOffset + 4u);

    std::array<std::uint8_t, 8> dxt5Alpha{};
    std::uint64_t dxt5Indices = 0u;
    if (format == FORMAT_DXT5)
    {
        BuildDxt5AlphaTable(block.first(8u), dxt5Alpha);
        dxt5Indices = ReadLe48(block.subspan(2u, 6u));
    }

    for (std::uint32_t localY = 0u; localY < 4u; ++localY)
    {
        const std::uint32_t y = blockY * 4u + localY;
        if (y >= height) continue;
        for (std::uint32_t localX = 0u; localX < 4u; ++localX)
        {
            const std::uint32_t x = blockX * 4u + localX;
            if (x >= width) continue;
            const std::uint32_t pixel = localY * 4u + localX;
            Rgba color = colors[(colorIndices >> (pixel * 2u)) & 3u];
            if (format == FORMAT_DXT3)
            {
                const std::uint8_t packed = block[pixel / 2u];
                const std::uint8_t nibble = static_cast<std::uint8_t>(
                    (packed >> ((pixel & 1u) * 4u)) & 0x0fu);
                color.alpha = static_cast<std::uint8_t>(nibble * 17u);
            }
            else if (format == FORMAT_DXT5)
            {
                color.alpha = dxt5Alpha[
                    (dxt5Indices >> (pixel * 3u)) & 7u];
            }
            const std::size_t destination =
                (static_cast<std::size_t>(y) * width + x) * RGBA_BYTES_PER_PIXEL;
            rgba[destination] = color.red;
            rgba[destination + 1u] = color.green;
            rgba[destination + 2u] = color.blue;
            rgba[destination + 3u] = color.alpha;
        }
    }
}

Error DecodeIwiCubeFaceLevel(
    std::uint8_t format,
    std::uint32_t edgeLength,
    std::span<const std::uint8_t> source,
    std::vector<std::uint8_t> &pixels) noexcept
{
    const bool rgb8 = format == FORMAT_RGB8;
    const bool compressed = format == FORMAT_DXT1 ||
        format == FORMAT_DXT3 || format == FORMAT_DXT5;
    const std::size_t bytesPerBlock = format == FORMAT_DXT1 ? 8u : 16u;
    std::size_t pixelCount = 0u;
    std::size_t pixelBytes = 0u;
    if (!CheckedMultiply(edgeLength, edgeLength, pixelCount) ||
        !CheckedMultiply(pixelCount, RGBA_BYTES_PER_PIXEL, pixelBytes))
        return Error::DecodeOutputTooLarge;
    try
    {
        pixels.resize(pixelBytes);
    }
    catch (...)
    {
        return Error::DecodeAllocationFailed;
    }
    if (rgb8)
    {
        for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel)
        {
            const std::size_t sourceOffset = pixel * 3u;
            const std::size_t targetOffset = pixel * 4u;
            pixels[targetOffset] = source[sourceOffset + 2u];
            pixels[targetOffset + 1u] = source[sourceOffset + 1u];
            pixels[targetOffset + 2u] = source[sourceOffset];
            pixels[targetOffset + 3u] = 255u;
        }
    }
    else if (!compressed)
    {
        for (std::size_t offset = 0u; offset < pixelBytes; offset += 4u)
        {
            pixels[offset] = source[offset + 2u];
            pixels[offset + 1u] = source[offset + 1u];
            pixels[offset + 2u] = source[offset];
            pixels[offset + 3u] = source[offset + 3u];
        }
    }
    else
    {
        const std::uint32_t blocksWide = (edgeLength + 3u) / 4u;
        const std::uint32_t blocksHigh = (edgeLength + 3u) / 4u;
        std::size_t sourceOffset = 0u;
        for (std::uint32_t blockY = 0u; blockY < blocksHigh; ++blockY)
        {
            for (std::uint32_t blockX = 0u; blockX < blocksWide; ++blockX)
            {
                DecodeDxtBlock(source.subspan(sourceOffset, bytesPerBlock),
                    format, blockX, blockY, edgeLength, edgeLength, pixels);
                sourceOffset += bytesPerBlock;
            }
        }
    }
    return Error::None;
}

Error LoadDefLevelBytes(
    std::int32_t format,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t &bytes) noexcept
{
    if (format == LOADDEF_FORMAT_DXT1)
        return DxtLevelBytes(width, height, 8u, bytes)
            ? Error::None : Error::DecodeOutputTooLarge;
    if (format == LOADDEF_FORMAT_DXT3 || format == LOADDEF_FORMAT_DXT5)
        return DxtLevelBytes(width, height, 16u, bytes)
            ? Error::None : Error::DecodeOutputTooLarge;
    std::size_t pixels = 0u;
    const std::size_t bytesPerPixel = format == LOADDEF_FORMAT_L8 ? 1u :
        (format == LOADDEF_FORMAT_A8L8 ? 2u : 4u);
    if (format != LOADDEF_FORMAT_A8R8G8B8 &&
        format != LOADDEF_FORMAT_X8R8G8B8 &&
        format != LOADDEF_FORMAT_L8 && format != LOADDEF_FORMAT_A8L8)
        return Error::DecodeUnsupportedFormat;
    return CheckedMultiply(width, height, pixels) &&
            CheckedMultiply(pixels, bytesPerPixel, bytes)
        ? Error::None : Error::DecodeOutputTooLarge;
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

static Error ProcessRgba8(
    std::span<const std::uint8_t> bytes,
    Rgba8Image *image,
    Rgba8Layout *layout,
    std::uint32_t firstMip) noexcept
{
    Metadata metadata{};
    const Error parseError = Parse(bytes, metadata);
    if (parseError != Error::None)
    {
        return parseError;
    }

    // Image_LoadFromFile also exempts small images from quality reduction.
    firstMip = (metadata.flags & (FLAG_NO_PICMIP | FLAG_NO_MIPMAPS)) ||
        metadata.width < 32u || metadata.height < 32u ? 0u :
        std::min(firstMip, Count2dStoredMipmaps(metadata.flags, metadata.width, metadata.height) - 1u);
    const auto originalWidth = metadata.width;
    const auto originalHeight = metadata.height;

    const bool rgb8 = metadata.format == FORMAT_RGB8;
    const bool a8l8 = metadata.format == FORMAT_A8L8;
    const bool l8 = metadata.format == FORMAT_L8;
    const bool wavelet = metadata.format >= FORMAT_WAVELET_ARGB &&
        metadata.format <= FORMAT_WAVELET_A8;
    const bool compressed = metadata.format == FORMAT_DXT1 ||
        metadata.format == FORMAT_DXT3 || metadata.format == FORMAT_DXT5;
    if (metadata.format != FORMAT_ARGB && !rgb8 && !a8l8 && !l8 &&
        !wavelet && !compressed)
    {
        return Error::DecodeUnsupportedFormat;
    }
    constexpr std::uint8_t policyFlags =
        FLAG_STREAMING | FLAG_CLAMP_U | FLAG_CLAMP_V;
    const std::uint8_t compressedPolicyFlags = static_cast<std::uint8_t>(
        policyFlags | FLAG_LEGACY_NORMALS);
    if ((metadata.format == FORMAT_ARGB &&
            ((metadata.flags & FLAG_NO_MIPMAPS) == 0u ||
             (metadata.flags & static_cast<std::uint8_t>(
                 ~(FLAG_NO_MIPMAPS | policyFlags))) != 0u)) ||
        ((rgb8 || a8l8 || l8 || wavelet) &&
            (metadata.flags & static_cast<std::uint8_t>(
                ~(FLAG_NO_PICMIP | FLAG_NO_MIPMAPS | policyFlags))) != 0u) ||
        (compressed && (metadata.flags & static_cast<std::uint8_t>(
            ~(FLAG_NO_PICMIP | FLAG_NO_MIPMAPS |
                compressedPolicyFlags))) != 0u))
    {
        return Error::DecodeUnsupportedFlags;
    }
    if (metadata.depth != 1u ||
        (metadata.format == FORMAT_ARGB && metadata.mipCount != 1u))
    {
        return Error::DecodeUnsupportedDimensions;
    }

    std::size_t pixelCount = 0u;
    std::size_t pixelBytes = 0u;
    std::size_t expectedMemberBytes = 0u;
    if (!CheckedMultiply(metadata.width, metadata.height, pixelCount) ||
        !CheckedMultiply(pixelCount, RGBA_BYTES_PER_PIXEL, pixelBytes))
    {
        return Error::DecodeOutputTooLarge;
    }
    if (bytes.size() > MAX_TEXTURE_MEMBER_BYTES ||
        pixelBytes > MAX_DECODED_RGBA8_BYTES)
    {
        return Error::DecodeOutputTooLarge;
    }
    std::size_t baseLevelOffset = HEADER_SIZE;
    if (wavelet)
    {
        if (bytes.size() <= HEADER_SIZE)
            return Error::DecodeInvalidLayout;
    }
    else if (!compressed && !rgb8 && !a8l8 && !l8)
    {
        if (!CheckedAdd(HEADER_SIZE, pixelBytes, expectedMemberBytes) ||
            bytes.size() != expectedMemberBytes)
        {
            return Error::DecodeInvalidLayout;
        }
    }
    else if (rgb8 || a8l8 || l8)
    {
        const std::uint32_t storedMipCount = Count2dStoredMipmaps(
            metadata.flags, metadata.width, metadata.height);
        std::size_t payloadBytes = 0u;
        std::size_t baseLevelBytes = 0u;
        for (std::uint32_t mip = storedMipCount; mip > 0u; --mip)
        {
            const std::uint32_t level = mip - 1u;
            const std::uint32_t width = std::max<std::uint32_t>(
                static_cast<std::uint32_t>(metadata.width) >> level, 1u);
            const std::uint32_t height = std::max<std::uint32_t>(
                static_cast<std::uint32_t>(metadata.height) >> level, 1u);
            std::size_t levelPixels = 0u;
            std::size_t levelBytes = 0u;
            if (!CheckedMultiply(width, height, levelPixels) ||
                !CheckedMultiply(levelPixels,
                    rgb8 ? 3u : (a8l8 ? 2u : 1u), levelBytes) ||
                !CheckedAdd(payloadBytes, levelBytes, payloadBytes))
            {
                return Error::DecodeOutputTooLarge;
            }
            if (level == firstMip) {
                baseLevelBytes = levelBytes;
                baseLevelOffset = HEADER_SIZE + payloadBytes - levelBytes;
            }
        }
        if (!CheckedAdd(HEADER_SIZE, payloadBytes, expectedMemberBytes) ||
            bytes.size() != expectedMemberBytes || baseLevelBytes > payloadBytes)
        {
            return Error::DecodeInvalidLayout;
        }
    }
    else
    {
        const std::size_t bytesPerBlock = metadata.format == FORMAT_DXT1 ? 8u : 16u;
        const std::uint32_t storedMipCount = Count2dStoredMipmaps(
            metadata.flags, metadata.width, metadata.height);
        std::size_t payloadBytes = 0u;
        std::size_t baseLevelBytes = 0u;
        for (std::uint32_t mip = storedMipCount; mip > 0u; --mip)
        {
            const std::uint32_t level = mip - 1u;
            const std::uint32_t width = std::max<std::uint32_t>(
                static_cast<std::uint32_t>(metadata.width) >> level, 1u);
            const std::uint32_t height = std::max<std::uint32_t>(
                static_cast<std::uint32_t>(metadata.height) >> level, 1u);
            std::size_t levelBytes = 0u;
            if (!DxtLevelBytes(width, height, bytesPerBlock, levelBytes) ||
                !CheckedAdd(payloadBytes, levelBytes, payloadBytes))
            {
                return Error::DecodeOutputTooLarge;
            }
            if (level == firstMip) {
                baseLevelBytes = levelBytes;
                baseLevelOffset = HEADER_SIZE + payloadBytes - levelBytes;
            }
        }
        if (!CheckedAdd(HEADER_SIZE, payloadBytes, expectedMemberBytes) ||
            bytes.size() != expectedMemberBytes || baseLevelBytes > payloadBytes)
        {
            return Error::DecodeInvalidLayout;
        }
    }

    if (baseLevelOffset > bytes.size())
    {
        return Error::DecodeInvalidLayout;
    }
    metadata.width = std::max<std::uint16_t>(originalWidth >> firstMip, 1u);
    metadata.height = std::max<std::uint16_t>(originalHeight >> firstMip, 1u);
    pixelCount = static_cast<std::size_t>(metadata.width) * metadata.height;
    pixelBytes = pixelCount * RGBA_BYTES_PER_PIXEL;
    if (layout)
    {
        *layout = {
            metadata.width,
            metadata.height,
            1u,
            pixelBytes,
            pixelBytes,
        };
    }
    if (!image) return Error::None;

    std::vector<std::uint8_t> rgba;
    if (wavelet)
    {
        const Error waveletError = DecodeWaveletPayloadRgba8(
            metadata.format, originalWidth, originalHeight,
            bytes.subspan(HEADER_SIZE), rgba, firstMip);
        if (waveletError != Error::None) return waveletError;
        image->width = metadata.width;
        image->height = metadata.height;
        image->pixels.swap(rgba);
        return Error::None;
    }
    try
    {
        rgba.resize(pixelBytes);
    }
    catch (...)
    {
        return Error::DecodeAllocationFailed;
    }

    if (rgb8)
    {
        const std::span<const std::uint8_t> bgr = bytes.subspan(
            baseLevelOffset, pixelCount * 3u);
        for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel)
        {
            const std::size_t sourceOffset = pixel * 3u;
            const std::size_t targetOffset = pixel * RGBA_BYTES_PER_PIXEL;
            rgba[targetOffset] = bgr[sourceOffset + 2u];
            rgba[targetOffset + 1u] = bgr[sourceOffset + 1u];
            rgba[targetOffset + 2u] = bgr[sourceOffset];
            rgba[targetOffset + 3u] = 255u;
        }
    }
    else if (a8l8)
    {
        const std::span<const std::uint8_t> luminanceAlpha = bytes.subspan(
            baseLevelOffset, pixelCount * 2u);
        for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel)
        {
            const std::size_t sourceOffset = pixel * 2u;
            const std::size_t targetOffset = pixel * RGBA_BYTES_PER_PIXEL;
            const std::uint8_t luminance = luminanceAlpha[sourceOffset];
            rgba[targetOffset] = luminance;
            rgba[targetOffset + 1u] = luminance;
            rgba[targetOffset + 2u] = luminance;
            rgba[targetOffset + 3u] = luminanceAlpha[sourceOffset + 1u];
        }
    }
    else if (l8)
    {
        const std::span<const std::uint8_t> luminance = bytes.subspan(
            baseLevelOffset, pixelCount);
        for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel)
        {
            const std::size_t targetOffset = pixel * RGBA_BYTES_PER_PIXEL;
            rgba[targetOffset] = luminance[pixel];
            rgba[targetOffset + 1u] = luminance[pixel];
            rgba[targetOffset + 2u] = luminance[pixel];
            rgba[targetOffset + 3u] = 255u;
        }
    }
    else if (!compressed)
    {
        const std::span<const std::uint8_t> bgra = bytes.subspan(baseLevelOffset);
        for (std::size_t offset = 0u; offset < pixelBytes; offset += ARGB_BYTES_PER_PIXEL)
        {
            rgba[offset] = bgra[offset + 2u];
            rgba[offset + 1u] = bgra[offset + 1u];
            rgba[offset + 2u] = bgra[offset];
            rgba[offset + 3u] = bgra[offset + 3u];
        }
    }
    else
    {
        const std::size_t bytesPerBlock = metadata.format == FORMAT_DXT1 ? 8u : 16u;
        const std::uint32_t blocksWide =
            (static_cast<std::uint32_t>(metadata.width) + 3u) / 4u;
        const std::uint32_t blocksHigh =
            (static_cast<std::uint32_t>(metadata.height) + 3u) / 4u;
        std::size_t sourceOffset = baseLevelOffset;
        for (std::uint32_t blockY = 0u; blockY < blocksHigh; ++blockY)
        {
            for (std::uint32_t blockX = 0u; blockX < blocksWide; ++blockX)
            {
                DecodeDxtBlock(
                    bytes.subspan(sourceOffset, bytesPerBlock),
                    metadata.format,
                    blockX,
                    blockY,
                    metadata.width,
                    metadata.height,
                    rgba);
                sourceOffset += bytesPerBlock;
            }
        }
    }

    image->width = metadata.width;
    image->height = metadata.height;
    image->pixels.swap(rgba);
    return Error::None;
}

Error DecodeRgba8(
    std::span<const std::uint8_t> bytes,
    Rgba8Image &image,
    std::uint32_t firstMip) noexcept
{
    return ProcessRgba8(bytes, &image, nullptr, firstMip);
}

Error InspectRgba8(
    std::span<const std::uint8_t> bytes,
    Rgba8Layout &layout,
    std::uint32_t firstMip) noexcept
{
    return ProcessRgba8(bytes, nullptr, &layout, firstMip);
}

Error DecodeCubeRgba8(
    std::span<const std::uint8_t> bytes,
    Rgba8Cube &cube) noexcept
{
    Metadata metadata{};
    const Error parseError = Parse(bytes, metadata);
    if (parseError != Error::None) return parseError;

    const bool rgb8 = metadata.format == FORMAT_RGB8;
    const bool compressed = metadata.format == FORMAT_DXT1 ||
        metadata.format == FORMAT_DXT3 || metadata.format == FORMAT_DXT5;
    if (metadata.format != FORMAT_ARGB && !rgb8 && !compressed)
        return Error::DecodeUnsupportedFormat;
    constexpr std::uint8_t policyFlags =
        FLAG_STREAMING | FLAG_CLAMP_U | FLAG_CLAMP_V;
    const std::uint8_t allowedFlags = FLAG_CUBEMAP | FLAG_NO_PICMIP |
        FLAG_NO_MIPMAPS | policyFlags;
    if ((metadata.flags & FLAG_CUBEMAP) == 0u ||
        (metadata.flags & static_cast<std::uint8_t>(~allowedFlags)) != 0u)
        return Error::DecodeUnsupportedFlags;
    if (metadata.depth != 1u || metadata.width != metadata.height)
        return Error::DecodeUnsupportedDimensions;

    const std::size_t bytesPerBlock = metadata.format == FORMAT_DXT1
        ? 8u : 16u;
    const std::uint32_t mipCount = Count2dStoredMipmaps(
        metadata.flags, metadata.width, metadata.height);
    struct LevelLayout
    {
        std::uint32_t level = 0u;
        std::uint32_t edgeLength = 0u;
        std::size_t faceBytes = 0u;
        std::size_t offset = 0u;
    };
    std::vector<LevelLayout> layouts;
    try
    {
        layouts.reserve(mipCount);
    }
    catch (...)
    {
        return Error::DecodeAllocationFailed;
    }
    std::size_t payloadBytes = 0u;
    std::size_t totalPixelBytes = 0u;
    for (std::uint32_t mip = mipCount; mip > 0u; --mip)
    {
        const std::uint32_t level = mip - 1u;
        const std::uint32_t edgeLength = std::max<std::uint32_t>(
            static_cast<std::uint32_t>(metadata.width) >> level, 1u);
        std::size_t levelFaceBytes = 0u;
        if (compressed)
        {
            if (!DxtLevelBytes(edgeLength, edgeLength, bytesPerBlock,
                    levelFaceBytes))
                return Error::DecodeOutputTooLarge;
        }
        else
        {
            std::size_t levelPixels = 0u;
            if (!CheckedMultiply(edgeLength, edgeLength, levelPixels) ||
                !CheckedMultiply(levelPixels, rgb8 ? 3u : 4u,
                    levelFaceBytes))
                return Error::DecodeOutputTooLarge;
        }
        std::size_t levelBytes = 0u;
        std::size_t rgbaFaceBytes = 0u;
        std::size_t rgbaLevelBytes = 0u;
        if (!CheckedMultiply(edgeLength, edgeLength, rgbaFaceBytes) ||
            !CheckedMultiply(rgbaFaceBytes, RGBA_BYTES_PER_PIXEL,
                rgbaFaceBytes) ||
            !CheckedMultiply(rgbaFaceBytes, 6u, rgbaLevelBytes) ||
            !CheckedAdd(totalPixelBytes, rgbaLevelBytes, totalPixelBytes))
            return Error::DecodeOutputTooLarge;
        const std::size_t levelOffset = HEADER_SIZE + payloadBytes;
        if (!CheckedMultiply(levelFaceBytes, 6u, levelBytes) ||
            !CheckedAdd(payloadBytes, levelBytes, payloadBytes))
            return Error::DecodeOutputTooLarge;
        layouts.push_back({level, edgeLength, levelFaceBytes, levelOffset});
    }
    std::size_t expectedMemberBytes = 0u;
    if (!CheckedAdd(HEADER_SIZE, payloadBytes, expectedMemberBytes) ||
        expectedMemberBytes != bytes.size())
        return Error::DecodeInvalidLayout;
    if (totalPixelBytes > MAX_CUBE_RGBA8_BYTES ||
        bytes.size() > MAX_TEXTURE_MEMBER_BYTES)
        return Error::DecodeOutputTooLarge;

    Rgba8Cube decoded{};
    decoded.edgeLength = metadata.width;
    try
    {
        decoded.mipFaces.resize(mipCount - 1u);
    }
    catch (...)
    {
        return Error::DecodeAllocationFailed;
    }
    for (const LevelLayout &layout : layouts)
    {
        auto &targetFaces = layout.level == 0u
            ? decoded.faces : decoded.mipFaces[layout.level - 1u];
        for (std::size_t faceIndex = 0u; faceIndex < targetFaces.size();
             ++faceIndex)
        {
            const std::size_t sourceStart =
                layout.offset + faceIndex * layout.faceBytes;
            if (sourceStart > bytes.size() ||
                layout.faceBytes > bytes.size() - sourceStart)
                return Error::DecodeInvalidLayout;
            const Error decodeError = DecodeIwiCubeFaceLevel(
                metadata.format, layout.edgeLength,
                bytes.subspan(sourceStart, layout.faceBytes),
                targetFaces[faceIndex]);
            if (decodeError != Error::None) return decodeError;
        }
    }
    cube = std::move(decoded);
    return Error::None;
}

static Error ProcessLoadDefRgba8(
    std::int32_t format,
    std::uint8_t flags,
    std::uint16_t width,
    std::uint16_t height,
    std::uint16_t depth,
    std::span<const std::uint8_t> payload,
    Rgba8Image *image,
    Rgba8Layout *layout,
    std::uint32_t firstMip = 0u) noexcept
{
    std::uint8_t iwiFormat = 0u;
    std::size_t bytesPerBlock = 0u;
    bool compressed = false;
    bool luminance = false;
    bool luminanceAlpha = false;
    bool opaqueBgra = false;
    switch (format)
    {
    case LOADDEF_FORMAT_A8R8G8B8:
        iwiFormat = FORMAT_ARGB;
        break;
    case LOADDEF_FORMAT_X8R8G8B8:
        iwiFormat = FORMAT_ARGB;
        opaqueBgra = true;
        break;
    case LOADDEF_FORMAT_L8:
        luminance = true;
        break;
    case LOADDEF_FORMAT_A8L8:
        luminanceAlpha = true;
        break;
    case LOADDEF_FORMAT_DXT1:
        iwiFormat = FORMAT_DXT1;
        bytesPerBlock = 8u;
        compressed = true;
        break;
    case LOADDEF_FORMAT_DXT3:
        iwiFormat = FORMAT_DXT3;
        bytesPerBlock = 16u;
        compressed = true;
        break;
    case LOADDEF_FORMAT_DXT5:
        iwiFormat = FORMAT_DXT5;
        bytesPerBlock = 16u;
        compressed = true;
        break;
    default:
        return Error::DecodeUnsupportedFormat;
    }
    if (width == 0u || height == 0u || depth != 1u)
        return Error::DecodeUnsupportedDimensions;

    std::size_t pixelCount = 0u;
    std::size_t pixelBytes = 0u;
    if (!CheckedMultiply(width, height, pixelCount) ||
        !CheckedMultiply(pixelCount, RGBA_BYTES_PER_PIXEL, pixelBytes) ||
        pixelBytes > MAX_LOADDEF_RGBA8_BYTES)
    {
        return Error::DecodeOutputTooLarge;
    }

    std::uint32_t mipCount = CountMipmaps(flags, width, height, depth);
    firstMip = (flags & FLAG_NO_PICMIP) ? 0u : std::min(firstMip, mipCount - 1u);
    std::size_t skippedBytes = 0u, skippedRgbaBytes = 0u;
    std::size_t expectedBytes = 0u;
    std::size_t baseLevelBytes = 0u;
    std::size_t totalRgbaBytes = 0u;
    for (std::uint32_t mip = 0u; mip < mipCount; ++mip)
    {
        const std::uint32_t mipWidth = std::max<std::uint32_t>(width >> mip, 1u);
        const std::uint32_t mipHeight = std::max<std::uint32_t>(height >> mip, 1u);
        std::size_t levelBytes = 0u;
        std::size_t levelPixels = 0u;
        std::size_t levelRgbaBytes = 0u;
        if (compressed)
        {
            if (!DxtLevelBytes(mipWidth, mipHeight, bytesPerBlock, levelBytes))
                return Error::DecodeOutputTooLarge;
        }
        else
        {
            std::size_t mipPixels = 0u;
            if (!CheckedMultiply(mipWidth, mipHeight, mipPixels) ||
                !CheckedMultiply(
                    mipPixels,
                    luminance ? 1u :
                        (luminanceAlpha ? 2u : ARGB_BYTES_PER_PIXEL),
                    levelBytes))
            {
                return Error::DecodeOutputTooLarge;
            }
        }
        if (mip == 0u) baseLevelBytes = levelBytes;
        if (!CheckedMultiply(mipWidth, mipHeight, levelPixels) ||
            !CheckedMultiply(levelPixels, RGBA_BYTES_PER_PIXEL,
                levelRgbaBytes) ||
            !CheckedAdd(totalRgbaBytes, levelRgbaBytes, totalRgbaBytes) ||
            !CheckedAdd(expectedBytes, levelBytes, expectedBytes))
            return Error::DecodeOutputTooLarge;
        if (mip < firstMip) {
            skippedBytes = expectedBytes;
            skippedRgbaBytes = totalRgbaBytes;
        }
    }
    if (payload.size() != expectedBytes || baseLevelBytes > payload.size())
        return Error::DecodeInvalidLayout;
    if (totalRgbaBytes > MAX_LOADDEF_MIP_CHAIN_RGBA8_BYTES)
        return Error::DecodeOutputTooLarge;
    if (firstMip) {
        payload = payload.subspan(skippedBytes);
        width = std::max<std::uint16_t>(width >> firstMip, 1u);
        height = std::max<std::uint16_t>(height >> firstMip, 1u);
        mipCount -= firstMip;
        pixelCount = static_cast<std::size_t>(width) * height;
        pixelBytes = pixelCount * RGBA_BYTES_PER_PIXEL;
        totalRgbaBytes -= skippedRgbaBytes;
        const Error sizeError = LoadDefLevelBytes(format, width, height, baseLevelBytes);
        if (sizeError != Error::None) return sizeError;
    }
    if (layout)
    {
        *layout = {
            width,
            height,
            mipCount,
            pixelBytes,
            totalRgbaBytes,
        };
    }
    if (!image) return Error::None;

    std::vector<std::uint8_t> rgba;
    try
    {
        rgba.resize(pixelBytes);
    }
    catch (...)
    {
        return Error::DecodeAllocationFailed;
    }
    if (luminance)
    {
        const std::span<const std::uint8_t> l8 = payload.first(baseLevelBytes);
        for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel)
        {
            const std::size_t offset = pixel * RGBA_BYTES_PER_PIXEL;
            rgba[offset] = l8[pixel];
            rgba[offset + 1u] = l8[pixel];
            rgba[offset + 2u] = l8[pixel];
            rgba[offset + 3u] = 255u;
        }
    }
    else if (luminanceAlpha)
    {
        const std::span<const std::uint8_t> a8l8 = payload.first(baseLevelBytes);
        for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel)
        {
            const std::size_t sourceOffset = pixel * 2u;
            const std::size_t targetOffset = pixel * RGBA_BYTES_PER_PIXEL;
            const std::uint8_t luminanceValue = a8l8[sourceOffset];
            rgba[targetOffset] = luminanceValue;
            rgba[targetOffset + 1u] = luminanceValue;
            rgba[targetOffset + 2u] = luminanceValue;
            rgba[targetOffset + 3u] = a8l8[sourceOffset + 1u];
        }
    }
    else if (!compressed)
    {
        const std::span<const std::uint8_t> bgra = payload.first(baseLevelBytes);
        for (std::size_t offset = 0u; offset < pixelBytes;
             offset += ARGB_BYTES_PER_PIXEL)
        {
            rgba[offset] = bgra[offset + 2u];
            rgba[offset + 1u] = bgra[offset + 1u];
            rgba[offset + 2u] = bgra[offset];
            rgba[offset + 3u] = opaqueBgra ? 255u : bgra[offset + 3u];
        }
    }
    else
    {
        const std::uint32_t blocksWide =
            (static_cast<std::uint32_t>(width) + 3u) / 4u;
        const std::uint32_t blocksHigh =
            (static_cast<std::uint32_t>(height) + 3u) / 4u;
        std::size_t sourceOffset = 0u;
        for (std::uint32_t blockY = 0u; blockY < blocksHigh; ++blockY)
        {
            for (std::uint32_t blockX = 0u; blockX < blocksWide; ++blockX)
            {
                DecodeDxtBlock(
                    payload.subspan(sourceOffset, bytesPerBlock),
                    iwiFormat,
                    blockX,
                    blockY,
                    width,
                    height,
                    rgba);
                sourceOffset += bytesPerBlock;
            }
        }
    }

    Rgba8Image decoded{};
    decoded.width = width;
    decoded.height = height;
    decoded.pixels.swap(rgba);
    if (mipCount > 1u)
    {
        try
        {
            decoded.mipPixels.reserve(mipCount - 1u);
        }
        catch (...)
        {
            return Error::DecodeAllocationFailed;
        }
        std::size_t sourceOffset = baseLevelBytes;
        for (std::uint32_t mip = 1u; mip < mipCount; ++mip)
        {
            const std::uint32_t mipWidth = std::max<std::uint32_t>(
                static_cast<std::uint32_t>(width) >> mip, 1u);
            const std::uint32_t mipHeight = std::max<std::uint32_t>(
                static_cast<std::uint32_t>(height) >> mip, 1u);
            std::size_t levelBytes = 0u;
            const Error sizeError = LoadDefLevelBytes(
                format, mipWidth, mipHeight, levelBytes);
            if (sizeError != Error::None) return sizeError;
            Rgba8Image level{};
            const Error levelError = ProcessLoadDefRgba8(
                format,
                static_cast<std::uint8_t>(flags | FLAG_NO_MIPMAPS),
                static_cast<std::uint16_t>(mipWidth),
                static_cast<std::uint16_t>(mipHeight), depth,
                payload.subspan(sourceOffset, levelBytes), &level, nullptr);
            if (levelError != Error::None) return levelError;
            decoded.mipPixels.push_back(std::move(level.pixels));
            sourceOffset += levelBytes;
        }
        if (sourceOffset != payload.size())
            return Error::DecodeInvalidLayout;
    }
    *image = std::move(decoded);
    return Error::None;
}

Error DecodeLoadDefRgba8(
    std::int32_t format,
    std::uint8_t flags,
    std::uint16_t width,
    std::uint16_t height,
    std::uint16_t depth,
    std::span<const std::uint8_t> payload,
    Rgba8Image &image,
    std::uint32_t firstMip) noexcept
{
    return ProcessLoadDefRgba8(
        format, flags, width, height, depth, payload, &image, nullptr, firstMip);
}

Error InspectLoadDefRgba8(
    std::int32_t format,
    std::uint8_t flags,
    std::uint16_t width,
    std::uint16_t height,
    std::uint16_t depth,
    std::span<const std::uint8_t> payload,
    Rgba8Layout &layout,
    std::uint32_t firstMip) noexcept
{
    return ProcessLoadDefRgba8(
        format, flags, width, height, depth, payload, nullptr, &layout, firstMip);
}

Error DecodeLoadDefCubeRgba8(
    std::int32_t format,
    std::uint8_t flags,
    std::uint16_t width,
    std::uint16_t height,
    std::uint16_t depth,
    std::span<const std::uint8_t> payload,
    Rgba8Cube &cube) noexcept
{
    if (width == 0u || width != height || depth != 1u)
        return Error::DecodeUnsupportedDimensions;
    if (payload.size() % 6u != 0u)
        return Error::DecodeOutputTooLarge;
    const std::size_t facePayloadBytes = payload.size() / 6u;
    if (facePayloadBytes == 0u) return Error::DecodeInvalidLayout;

    const std::uint32_t mipCount = CountMipmaps(flags, width, height, depth);
    std::vector<std::size_t> levelBytes;
    try
    {
        levelBytes.resize(mipCount);
    }
    catch (...)
    {
        return Error::DecodeAllocationFailed;
    }
    std::size_t expectedFaceBytes = 0u;
    std::size_t totalRgbaBytes = 0u;
    for (std::uint32_t mip = 0u; mip < mipCount; ++mip)
    {
        const std::uint32_t edgeLength = std::max<std::uint32_t>(
            static_cast<std::uint32_t>(width) >> mip, 1u);
        const Error sizeError = LoadDefLevelBytes(
            format, edgeLength, edgeLength, levelBytes[mip]);
        if (sizeError != Error::None) return sizeError;
        std::size_t rgbaFaceBytes = 0u;
        std::size_t rgbaLevelBytes = 0u;
        if (!CheckedAdd(expectedFaceBytes, levelBytes[mip],
                expectedFaceBytes) ||
            !CheckedMultiply(edgeLength, edgeLength, rgbaFaceBytes) ||
            !CheckedMultiply(rgbaFaceBytes, RGBA_BYTES_PER_PIXEL,
                rgbaFaceBytes) ||
            !CheckedMultiply(rgbaFaceBytes, 6u, rgbaLevelBytes) ||
            !CheckedAdd(totalRgbaBytes, rgbaLevelBytes, totalRgbaBytes))
            return Error::DecodeOutputTooLarge;
    }
    if (expectedFaceBytes != facePayloadBytes)
        return Error::DecodeInvalidLayout;
    if (totalRgbaBytes > MAX_CUBE_RGBA8_BYTES)
        return Error::DecodeOutputTooLarge;

    Rgba8Cube decoded{};
    decoded.edgeLength = width;
    try
    {
        decoded.mipFaces.resize(mipCount - 1u);
    }
    catch (...)
    {
        return Error::DecodeAllocationFailed;
    }
    for (std::size_t faceIndex = 0u; faceIndex < decoded.faces.size();
         ++faceIndex)
    {
        std::size_t faceOffset = faceIndex * facePayloadBytes;
        for (std::uint32_t mip = 0u; mip < mipCount; ++mip)
        {
            const std::uint32_t edgeLength = std::max<std::uint32_t>(
                static_cast<std::uint32_t>(width) >> mip, 1u);
            Rgba8Image face{};
            const Error error = DecodeLoadDefRgba8(
                format, static_cast<std::uint8_t>(flags | FLAG_NO_MIPMAPS),
                static_cast<std::uint16_t>(edgeLength),
                static_cast<std::uint16_t>(edgeLength), depth,
                payload.subspan(faceOffset, levelBytes[mip]), face);
            if (error != Error::None) return error;
            std::size_t expectedRgbaBytes = 0u;
            if (!CheckedMultiply(edgeLength, edgeLength,
                    expectedRgbaBytes) ||
                !CheckedMultiply(expectedRgbaBytes, RGBA_BYTES_PER_PIXEL,
                    expectedRgbaBytes) ||
                face.width != edgeLength || face.height != edgeLength ||
                face.pixels.size() != expectedRgbaBytes)
                return Error::DecodeInvalidLayout;
            auto &targetFaces = mip == 0u
                ? decoded.faces : decoded.mipFaces[mip - 1u];
            targetFaces[faceIndex] = std::move(face.pixels);
            faceOffset += levelBytes[mip];
        }
    }
    cube = std::move(decoded);
    return Error::None;
}

} // namespace kisak::iwi
