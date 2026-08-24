#include <qcommon/iwi_image.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Bytes = std::vector<uint8_t>;
using kisak::iwi::Error;
using kisak::iwi::Metadata;
using kisak::iwi::Rgba8Cube;
using kisak::iwi::Rgba8Image;

class TestFailure final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw TestFailure(std::string(message));
    }
}

void RequireError(Error actual, Error expected, std::string_view context)
{
    if (actual == expected)
    {
        return;
    }
    std::string message(context);
    message += ": expected ";
    message += kisak::iwi::ErrorString(expected);
    message += ", got ";
    message += kisak::iwi::ErrorString(actual);
    throw TestFailure(message);
}

void PatchU16(Bytes &bytes, std::size_t offset, uint16_t value)
{
    Require(offset <= bytes.size() && bytes.size() - offset >= 2, "u16 patch is in range");
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void PatchU32(Bytes &bytes, std::size_t offset, uint32_t value)
{
    Require(offset <= bytes.size() && bytes.size() - offset >= 4, "u32 patch is in range");
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

Bytes MakeIwi(
    uint8_t format = 1,
    uint8_t flags = 0x02,
    uint16_t width = 2,
    uint16_t height = 2,
    uint16_t depth = 1,
    std::size_t payloadBytes = 16)
{
    const std::size_t totalSize = kisak::iwi::HEADER_SIZE + payloadBytes;
    Require(totalSize <= UINT32_MAX, "synthetic IWI size fits its header");
    Bytes bytes(totalSize, 0xa5);
    bytes[0] = 'I';
    bytes[1] = 'W';
    bytes[2] = 'i';
    bytes[3] = kisak::iwi::COD4_VERSION;
    bytes[4] = format;
    bytes[5] = flags;
    PatchU16(bytes, 6, width);
    PatchU16(bytes, 8, height);
    PatchU16(bytes, 10, depth);
    for (std::size_t index = 0; index < 4; ++index)
    {
        PatchU32(bytes, 12 + index * 4, static_cast<uint32_t>(totalSize));
    }
    return bytes;
}

Bytes MakeDxtIwi(
    uint8_t format,
    uint16_t width,
    uint16_t height,
    const Bytes &payload,
    uint8_t flags = kisak::iwi::FLAG_NO_MIPMAPS)
{
    Bytes bytes = MakeIwi(format, flags, width, height, 1u, payload.size());
    std::copy(payload.begin(), payload.end(), bytes.begin() + kisak::iwi::HEADER_SIZE);
    return bytes;
}

Bytes MakeDxtColorBlock(
    uint16_t color0,
    uint16_t color1,
    uint32_t indices)
{
    Bytes block(8u, 0u);
    PatchU16(block, 0u, color0);
    PatchU16(block, 2u, color1);
    PatchU32(block, 4u, indices);
    return block;
}

void Append(Bytes &destination, const Bytes &source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

bool SameMetadata(const Metadata &left, const Metadata &right)
{
    return left.version == right.version &&
        left.format == right.format &&
        left.flags == right.flags &&
        left.width == right.width &&
        left.height == right.height &&
        left.depth == right.depth &&
        left.fileSizeForPicmip == right.fileSizeForPicmip &&
        left.mipCount == right.mipCount;
}

bool SameImage(const Rgba8Image &left, const Rgba8Image &right)
{
    return left.width == right.width &&
        left.height == right.height &&
        left.pixels == right.pixels;
}

Rgba8Image MakeSentinelImage()
{
    return Rgba8Image{17u, 19u, {0x10u, 0x20u, 0x30u, 0x40u, 0x50u}};
}

void RequireDecodeFailure(
    const Bytes &bytes,
    Error expectedError,
    std::string_view context)
{
    Rgba8Image image = MakeSentinelImage();
    const Rgba8Image expected = image;
    RequireError(kisak::iwi::DecodeRgba8(bytes, image), expectedError, context);
    Require(SameImage(image, expected),
        "failed RGBA8 decode leaves the complete destination unchanged");
}

void RequireMetadataInvariant(const Metadata &metadata, std::size_t inputSize)
{
    Require(metadata.version == kisak::iwi::COD4_VERSION, "successful parse has COD4 version");
    Require(metadata.format >= 1 && metadata.format <= 13, "successful parse has known format");
    Require(metadata.width >= 1 && metadata.width <= 0x7fff, "successful parse has valid width");
    Require(metadata.height >= 1 && metadata.height <= 0x7fff, "successful parse has valid height");
    Require(metadata.depth >= 1 && metadata.depth <= 0x7fff, "successful parse has valid depth");
    Require(metadata.fileSizeForPicmip[0] == inputSize, "successful parse covers full member");
    const bool picmipDisabled = (metadata.flags & 0x03u) != 0u ||
        metadata.width < 32u || metadata.height < 32u;
    const std::size_t lastSelectablePicmip = picmipDisabled ? 0u : 3u;
    for (std::size_t index = 0; index < metadata.fileSizeForPicmip.size(); ++index)
    {
        const uint32_t size = metadata.fileSizeForPicmip[index];
        Require(index > lastSelectablePicmip || size > kisak::iwi::HEADER_SIZE,
            "successful parse populates every selectable picmip prefix");
        Require(size == 0 || (size > kisak::iwi::HEADER_SIZE && size <= inputSize),
            "successful parse has a zero sentinel or nonempty bounded picmip prefix");
        if (index != 0)
        {
            Require(size <= metadata.fileSizeForPicmip[index - 1],
                "successful parse has nonincreasing picmip prefixes");
        }
    }
    Require(metadata.mipCount >= 1, "successful parse has at least one mip level");
}

void TestHappyPathAndSupportedFormats()
{
    const Bytes fixture = MakeIwi();
    Metadata metadata{};
    RequireError(kisak::iwi::Parse(fixture, metadata), Error::None, "parse valid IWI");
    Require(metadata.version == 6, "version parsed");
    Require(metadata.format == 1, "format parsed");
    Require(metadata.flags == 0x02, "flags parsed");
    Require(metadata.width == 2 && metadata.height == 2 && metadata.depth == 1,
        "dimensions parsed");
    Require(metadata.fileSizeForPicmip == std::array<uint32_t, 4>{44, 44, 44, 44},
        "picmip sizes parsed as little endian");
    Require(metadata.mipCount == 1, "no-mipmap flag produces one level");

    for (uint8_t format = 1; format <= 13; ++format)
    {
        const Bytes supported = MakeIwi(format);
        RequireError(kisak::iwi::Parse(supported, metadata), Error::None,
            "all engine image formats are accepted");
        Require(metadata.format == format, "supported format is retained");
    }

    const Bytes mipmapped = MakeIwi(11, 0, 8, 4, 2, 72);
    RequireError(kisak::iwi::Parse(mipmapped, metadata), Error::None,
        "parse mipmapped dimensions");
    Require(metadata.mipCount == 4, "mip count covers the largest dimension");

    const Bytes nonPowerOfTwo = MakeIwi(1, 0, 3, 1, 1, 16);
    RequireError(kisak::iwi::Parse(nonPowerOfTwo, metadata), Error::None,
        "parse non-power-of-two mip dimensions");
    Require(metadata.mipCount == 3,
        "mip count preserves the engine resolution-doubling semantics");
}

void TestTruncationAndAtomicFailure()
{
    const Bytes fixture = MakeIwi();
    Metadata metadata{};
    RequireError(kisak::iwi::Parse(fixture, metadata), Error::None, "seed metadata");
    const Metadata expected = metadata;

    for (std::size_t length = 0; length < kisak::iwi::HEADER_SIZE; ++length)
    {
        RequireError(
            kisak::iwi::Parse(std::span<const uint8_t>(fixture).first(length), metadata),
            Error::HeaderTruncated,
            "reject every truncated header length");
        Require(SameMetadata(metadata, expected), "truncated parse leaves metadata unchanged");
    }
}

void TestHeaderValidation()
{
    Metadata metadata{};

    Bytes invalidTag = MakeIwi();
    invalidTag[2] = 'I';
    RequireError(kisak::iwi::Parse(invalidTag, metadata), Error::InvalidTag, "invalid tag");

    Bytes invalidVersion = MakeIwi();
    invalidVersion[3] = kisak::iwi::COD4_VERSION - 1;
    RequireError(kisak::iwi::Parse(invalidVersion, metadata), Error::UnsupportedVersion,
        "unsupported version");

    for (const uint8_t format : {uint8_t{0}, uint8_t{14}, uint8_t{255}})
    {
        Bytes invalidFormat = MakeIwi();
        invalidFormat[4] = format;
        RequireError(kisak::iwi::Parse(invalidFormat, metadata), Error::UnsupportedFormat,
            "unsupported pixel format");
    }

    for (const std::size_t offset : {std::size_t{6}, std::size_t{8}, std::size_t{10}})
    {
        Bytes zeroDimension = MakeIwi();
        PatchU16(zeroDimension, offset, 0);
        RequireError(kisak::iwi::Parse(zeroDimension, metadata), Error::InvalidDimensions,
            "zero dimension");

        Bytes signedOverflow = MakeIwi();
        PatchU16(signedOverflow, offset, 0x8000);
        RequireError(kisak::iwi::Parse(signedOverflow, metadata), Error::InvalidDimensions,
            "dimension outside signed engine range");
    }
}

void TestFileSizeValidation()
{
    Metadata metadata{};
    const Bytes fixture = MakeIwi();

    Bytes wrongFullSize = fixture;
    PatchU32(wrongFullSize, 12, static_cast<uint32_t>(fixture.size() - 1));
    RequireError(kisak::iwi::Parse(wrongFullSize, metadata), Error::InvalidFileSize,
        "slot zero must equal the member size");

    Bytes negativeFullSize = fixture;
    PatchU32(negativeFullSize, 12, 0xffffffffu);
    RequireError(kisak::iwi::Parse(negativeFullSize, metadata), Error::InvalidFileSize,
        "negative signed full-size metadata is rejected");

    Bytes negativePicmip = fixture;
    PatchU32(negativePicmip, 16, 0x80000000u);
    RequireError(kisak::iwi::Parse(negativePicmip, metadata), Error::InvalidFileSize,
        "negative signed picmip metadata is rejected");

    Bytes shortPicmip = fixture;
    PatchU32(shortPicmip, 16, kisak::iwi::HEADER_SIZE - 1);
    RequireError(kisak::iwi::Parse(shortPicmip, metadata), Error::InvalidFileSize,
        "picmip prefix cannot end before the header");

    const Bytes selectableFixture = MakeIwi(1, 0, 32, 32, 1);

    Bytes emptyPicmip = selectableFixture;
    PatchU32(emptyPicmip, 16, kisak::iwi::HEADER_SIZE);
    RequireError(kisak::iwi::Parse(emptyPicmip, metadata), Error::InvalidFileSize,
        "picmip prefix must contain payload bytes");

    for (std::size_t index = 1; index < 4; ++index)
    {
        Bytes zeroPicmip = selectableFixture;
        PatchU32(zeroPicmip, 12 + index * 4, 0);
        RequireError(kisak::iwi::Parse(zeroPicmip, metadata), Error::InvalidFileSize,
            "every selectable picmip slot must be populated");
    }

    Bytes longPicmip = fixture;
    PatchU32(longPicmip, 20, static_cast<uint32_t>(fixture.size() + 1));
    RequireError(kisak::iwi::Parse(longPicmip, metadata), Error::InvalidFileSize,
        "picmip size is bounded by the member");

    Bytes unusedPicmips = fixture;
    PatchU32(unusedPicmips, 16, 0);
    PatchU32(unusedPicmips, 20, 0);
    PatchU32(unusedPicmips, 24, 0);
    RequireError(kisak::iwi::Parse(unusedPicmips, metadata), Error::None,
        "disabled picmip slots may use trailing zero sentinels");

    Bytes smallImagePicmips = MakeIwi(1, 0, 31, 32, 1);
    PatchU32(smallImagePicmips, 16, 0);
    PatchU32(smallImagePicmips, 20, 0);
    PatchU32(smallImagePicmips, 24, 0);
    RequireError(kisak::iwi::Parse(smallImagePicmips, metadata), Error::None,
        "small images force picmip slot zero and may leave trailing slots unused");

    Bytes descendingPicmips = selectableFixture;
    PatchU32(descendingPicmips, 16, 40);
    PatchU32(descendingPicmips, 20, 36);
    PatchU32(descendingPicmips, 24, 32);
    RequireError(kisak::iwi::Parse(descendingPicmips, metadata), Error::None,
        "picmip prefixes may shrink in slot order");
    Require(metadata.fileSizeForPicmip == std::array<uint32_t, 4>{44, 40, 36, 32},
        "ordered picmip prefixes are retained");

    Bytes increasingPicmips = descendingPicmips;
    PatchU32(increasingPicmips, 20, 41);
    const Metadata expected = metadata;
    RequireError(kisak::iwi::Parse(increasingPicmips, metadata), Error::InvalidFileSize,
        "higher picmip slots cannot require a longer prefix");
    Require(SameMetadata(metadata, expected),
        "invalid picmip ordering leaves metadata unchanged");
}

void TestRgba8DecodeAndSwizzle()
{
    Bytes fixture = MakeIwi(
        kisak::iwi::FORMAT_ARGB,
        kisak::iwi::FLAG_NO_MIPMAPS,
        2,
        2,
        1,
        16);
    const std::array<uint8_t, 16> bgra{
        0x30, 0x20, 0x10, 0x40,
        0x03, 0x02, 0x01, 0x04,
        0xff, 0x80, 0x00, 0x7f,
        0x55, 0xaa, 0xcc, 0x11,
    };
    std::copy(bgra.begin(), bgra.end(), fixture.begin() + kisak::iwi::HEADER_SIZE);

    Rgba8Image image = MakeSentinelImage();
    RequireError(kisak::iwi::DecodeRgba8(fixture, image), Error::None,
        "decode tightly packed format-1 IWI");
    Require(image.width == 2 && image.height == 2, "decoded dimensions are retained");
    Require(image.pixels == Bytes({
        0x10, 0x20, 0x30, 0x40,
        0x01, 0x02, 0x03, 0x04,
        0x00, 0x80, 0xff, 0x7f,
        0xcc, 0xaa, 0x55, 0x11,
    }), "serialized BGRA pixels are converted to renderer-ready RGBA8");

    Rgba8Image aliased{91u, 92u, fixture};
    const std::span<const uint8_t> aliasedInput(aliased.pixels);
    RequireError(kisak::iwi::DecodeRgba8(aliasedInput, aliased), Error::None,
        "decode when input aliases the destination pixel vector");
    Require(aliased.width == image.width && aliased.height == image.height &&
            aliased.pixels == image.pixels,
        "aliased decode commits only after conversion is complete");
}

void TestRgb8DecodeAndMipOrder()
{
    Bytes payload(3u + 12u + 48u, 0u);
    // Retail bitmap IWIs store mip levels from smallest to largest. Seed the
    // first texel of the final 4x4 level as BGR and ensure only that base level
    // is expanded for the renderer.
    payload[15u] = 0x10u;
    payload[16u] = 0x20u;
    payload[17u] = 0x30u;
    Bytes fixture = MakeIwi(
        kisak::iwi::FORMAT_RGB8, 0u, 4u, 4u, 1u, payload.size());
    std::copy(payload.begin(), payload.end(),
        fixture.begin() + kisak::iwi::HEADER_SIZE);

    Rgba8Image image = MakeSentinelImage();
    RequireError(kisak::iwi::DecodeRgba8(fixture, image), Error::None,
        "decode opaque BGR8 mip chain");
    Require(image.width == 4u && image.height == 4u &&
            image.pixels.size() == 4u * 4u * 4u,
        "BGR8 decode retains base dimensions");
    Require(image.pixels[0] == 0x30u && image.pixels[1] == 0x20u &&
            image.pixels[2] == 0x10u && image.pixels[3] == 0xffu,
        "BGR8 base texel is swizzled to opaque RGBA8");
}

void TestA8L8DecodeAndMipOrder()
{
    Bytes payload{
        0xffu, 0x00u, // 1x1 mip.
        0x10u, 0x20u,
        0x40u, 0x80u,
        0x7fu, 0xffu,
        0xffu, 0x00u,
    };
    Bytes fixture = MakeIwi(
        kisak::iwi::FORMAT_A8L8, 0u, 2u, 2u, 1u, payload.size());
    std::copy(payload.begin(), payload.end(),
        fixture.begin() + kisak::iwi::HEADER_SIZE);

    Rgba8Image image = MakeSentinelImage();
    RequireError(kisak::iwi::DecodeRgba8(fixture, image), Error::None,
        "decode A8L8 mip chain");
    Require(image.width == 2u && image.height == 2u &&
            image.pixels == Bytes({
                0x10u, 0x10u, 0x10u, 0x20u,
                0x40u, 0x40u, 0x40u, 0x80u,
                0x7fu, 0x7fu, 0x7fu, 0xffu,
                0xffu, 0xffu, 0xffu, 0x00u,
            }),
        "A8L8 decode uses the final base mip and preserves luminance and alpha");
}

void TestL8DecodeAndMipOrder()
{
    Bytes payload{
        0xffu, // 1x1 mip.
        0x00u, 0x40u, 0x80u, 0xffu,
    };
    Bytes fixture = MakeIwi(
        kisak::iwi::FORMAT_L8, 0u, 2u, 2u, 1u, payload.size());
    std::copy(payload.begin(), payload.end(),
        fixture.begin() + kisak::iwi::HEADER_SIZE);

    Rgba8Image image = MakeSentinelImage();
    RequireError(kisak::iwi::DecodeRgba8(fixture, image), Error::None,
        "decode L8 mip chain");
    Require(image.width == 2u && image.height == 2u &&
            image.pixels == Bytes({
                0x00u, 0x00u, 0x00u, 0xffu,
                0x40u, 0x40u, 0x40u, 0xffu,
                0x80u, 0x80u, 0x80u, 0xffu,
                0xffu, 0xffu, 0xffu, 0xffu,
            }),
        "L8 decode uses the final base mip and expands opaque luminance");
}

void TestDxt1Decode()
{
    const Bytes opaqueBlock = MakeDxtColorBlock(0xf800u, 0x07e0u, 0xe4e4e4e4u);
    Rgba8Image image = MakeSentinelImage();
    RequireError(kisak::iwi::DecodeRgba8(MakeDxtIwi(
        kisak::iwi::FORMAT_DXT1, 4u, 4u, opaqueBlock), image), Error::None,
        "decode DXT1 four-color block");
    const Bytes firstRow{
        255u, 0u, 0u, 255u,
        0u, 255u, 0u, 255u,
        170u, 85u, 0u, 255u,
        85u, 170u, 0u, 255u,
    };
    Require(image.width == 4u && image.height == 4u,
        "DXT1 retains dimensions");
    for (std::size_t row = 0u; row < 4u; ++row)
    {
        const auto decodedRow = std::span<const uint8_t>(image.pixels).subspan(
            row * firstRow.size(), firstRow.size());
        Require(std::equal(
            firstRow.begin(), firstRow.end(), decodedRow.begin()),
            "DXT1 expands RGB565 and two-bit selectors");
    }

    const Bytes transparentBlock = MakeDxtColorBlock(0x0000u, 0xffffu, 0xffffffffu);
    RequireError(kisak::iwi::DecodeRgba8(MakeDxtIwi(
        kisak::iwi::FORMAT_DXT1, 4u, 4u, transparentBlock), image), Error::None,
        "decode DXT1 transparent mode");
    for (std::size_t offset = 0u; offset < image.pixels.size(); offset += 4u)
    {
        Require(image.pixels[offset] == 0u && image.pixels[offset + 1u] == 0u &&
                image.pixels[offset + 2u] == 0u && image.pixels[offset + 3u] == 0u,
            "DXT1 selector three is transparent when color0 is not greater");
    }
}

void TestDxt3Decode()
{
    Bytes block{0x10u, 0x32u, 0x54u, 0x76u, 0x98u, 0xbau, 0xdcu, 0xfeu};
    Append(block, MakeDxtColorBlock(0xf800u, 0x07e0u, 0u));
    Rgba8Image image{};
    RequireError(kisak::iwi::DecodeRgba8(MakeDxtIwi(
        kisak::iwi::FORMAT_DXT3, 4u, 4u, block), image), Error::None,
        "decode DXT3 explicit alpha block");
    for (std::size_t pixel = 0u; pixel < 16u; ++pixel)
    {
        const std::size_t offset = pixel * 4u;
        Require(image.pixels[offset] == 255u && image.pixels[offset + 1u] == 0u &&
                image.pixels[offset + 2u] == 0u &&
                image.pixels[offset + 3u] == pixel * 17u,
            "DXT3 expands four-bit alpha in serialized pixel order");
    }
}

void TestDxt5Decode()
{
    Bytes block(8u, 0u);
    block[0] = 255u;
    block[1] = 0u;
    std::uint64_t alphaIndices = 0u;
    for (std::uint32_t pixel = 0u; pixel < 16u; ++pixel)
    {
        alphaIndices |= static_cast<std::uint64_t>(pixel & 7u) << (pixel * 3u);
    }
    for (std::size_t byte = 0u; byte < 6u; ++byte)
    {
        block[2u + byte] = static_cast<uint8_t>(alphaIndices >> (byte * 8u));
    }
    Append(block, MakeDxtColorBlock(0x001fu, 0x07e0u, 0u));
    Rgba8Image image{};
    RequireError(kisak::iwi::DecodeRgba8(MakeDxtIwi(
        kisak::iwi::FORMAT_DXT5, 4u, 4u, block), image), Error::None,
        "decode DXT5 interpolated alpha block");
    const std::array<uint8_t, 8> expectedAlpha{
        255u, 0u, 218u, 182u, 145u, 109u, 72u, 36u,
    };
    for (std::size_t pixel = 0u; pixel < 16u; ++pixel)
    {
        const std::size_t offset = pixel * 4u;
        Require(image.pixels[offset] == 0u && image.pixels[offset + 1u] == 0u &&
                image.pixels[offset + 2u] == 255u &&
                image.pixels[offset + 3u] == expectedAlpha[pixel & 7u],
            "DXT5 expands three-bit alpha selectors");
    }
}

void TestDxtMipOrderAndClipping()
{
    const Bytes red = MakeDxtColorBlock(0xf800u, 0x07e0u, 0u);
    const Bytes blue = MakeDxtColorBlock(0x001fu, 0x07e0u, 0u);
    Bytes mipChain;
    Append(mipChain, red);  // 1x1, level 3
    Append(mipChain, red);  // 2x1, level 2
    Append(mipChain, red);  // 4x2, level 1
    Append(mipChain, blue); // 8x4 base block 0
    Append(mipChain, blue); // 8x4 base block 1
    Rgba8Image image{};
    RequireError(kisak::iwi::DecodeRgba8(MakeDxtIwi(
        kisak::iwi::FORMAT_DXT1, 8u, 4u, mipChain, 0u), image), Error::None,
        "decode base image from COD4 smallest-to-largest mip order");
    Require(image.pixels.size() == 8u * 4u * 4u,
        "mip decode emits only the largest level");
    for (std::size_t offset = 0u; offset < image.pixels.size(); offset += 4u)
    {
        Require(image.pixels[offset] == 0u && image.pixels[offset + 1u] == 0u &&
                image.pixels[offset + 2u] == 255u && image.pixels[offset + 3u] == 255u,
            "mip decode reads the base blocks at the end of the payload");
    }

    Bytes oddBlocks;
    Append(oddBlocks, red);
    Append(oddBlocks, blue);
    RequireError(kisak::iwi::DecodeRgba8(MakeDxtIwi(
        kisak::iwi::FORMAT_DXT1, 5u, 3u, oddBlocks), image), Error::None,
        "decode clipped edge blocks for non-multiple-of-four dimensions");
    Require(image.width == 5u && image.height == 3u && image.pixels.size() == 60u,
        "clipped DXT output has an exact tight RGBA layout");
    for (std::uint32_t row = 0u; row < 3u; ++row)
    {
        const std::size_t redOffset = (static_cast<std::size_t>(row) * 5u + 3u) * 4u;
        const std::size_t blueOffset = (static_cast<std::size_t>(row) * 5u + 4u) * 4u;
        Require(image.pixels[redOffset] == 255u && image.pixels[redOffset + 2u] == 0u &&
                image.pixels[blueOffset] == 0u && image.pixels[blueOffset + 2u] == 255u,
            "edge clipping preserves pixels from both compressed blocks");
    }
}

void TestDxtValidationAndAtomicFailure()
{
    const Bytes block = MakeDxtColorBlock(0xf800u, 0x07e0u, 0u);
    Rgba8Image policyImage{};
    RequireError(kisak::iwi::DecodeRgba8(MakeDxtIwi(
        kisak::iwi::FORMAT_DXT1, 4u, 4u, block,
        kisak::iwi::FLAG_NO_MIPMAPS | kisak::iwi::FLAG_STREAMING |
            kisak::iwi::FLAG_CLAMP_U | kisak::iwi::FLAG_CLAMP_V),
        policyImage), Error::None,
        "accept DXT streaming and address-policy flags");
    RequireError(kisak::iwi::DecodeRgba8(MakeDxtIwi(
        kisak::iwi::FORMAT_DXT1, 4u, 4u, block,
        kisak::iwi::FLAG_NO_MIPMAPS |
            kisak::iwi::FLAG_LEGACY_NORMALS),
        policyImage), Error::None,
        "accept native legacy-normal metadata without changing layout");
    for (const uint8_t format : {
        kisak::iwi::FORMAT_DXT1,
        kisak::iwi::FORMAT_DXT3,
        kisak::iwi::FORMAT_DXT5,
    })
    {
        const std::size_t expected = format == kisak::iwi::FORMAT_DXT1 ? 8u : 16u;
        RequireDecodeFailure(MakeIwi(
            format, kisak::iwi::FLAG_NO_MIPMAPS, 4u, 4u, 1u, expected - 1u),
            Error::DecodeInvalidLayout,
            "reject truncated DXT block payload");
        RequireDecodeFailure(MakeIwi(
            format, kisak::iwi::FLAG_NO_MIPMAPS, 4u, 4u, 1u, expected + 1u),
            Error::DecodeInvalidLayout,
            "reject trailing DXT block payload");
    }
    RequireDecodeFailure(MakeDxtIwi(
        kisak::iwi::FORMAT_DXT1, 4u, 4u, block, kisak::iwi::FLAG_CUBEMAP),
        Error::DecodeUnsupportedFlags,
        "reject DXT cubemap at the bounded 2D renderer seam");
    RequireDecodeFailure(MakeIwi(
        kisak::iwi::FORMAT_DXT1,
        kisak::iwi::FLAG_NO_MIPMAPS,
        4096u,
        2048u,
        1u,
        8u),
        Error::DecodeOutputTooLarge,
        "reject DXT expansion above the RGBA recovery ceiling");

    Bytes largePayload(2048u * 1024u / 2u, 0u);
    Rgba8Image largeImage{};
    RequireError(kisak::iwi::DecodeRgba8(MakeDxtIwi(
        kisak::iwi::FORMAT_DXT1, 2048u, 1024u, largePayload), largeImage),
        Error::None, "decode a bounded retail-scale DXT image");
    Require(largeImage.pixels.size() == 2048u * 1024u * 4u,
        "compressed input may expand beyond the archive-member ceiling");
}

void TestCubemapDecodeAndNativeFaceOrder()
{
    constexpr std::array<std::uint16_t, 6> colors{
        0xf800u, 0x07e0u, 0x001fu, 0xffe0u, 0xf81fu, 0x07ffu};
    Bytes payload;
    for (const std::uint16_t color : colors)
        Append(payload, MakeDxtColorBlock(color, color, 0u));
    const Bytes iwi = MakeDxtIwi(kisak::iwi::FORMAT_DXT1,
        4u, 4u, payload, kisak::iwi::FLAG_CUBEMAP |
            kisak::iwi::FLAG_NO_MIPMAPS);
    Rgba8Cube cube{};
    RequireError(kisak::iwi::DecodeCubeRgba8(iwi, cube), Error::None,
        "decode IWI cubemap");
    Require(cube.edgeLength == 4u,
        "cubemap retains its canonical edge length");
    for (std::size_t face = 0u; face < cube.faces.size(); ++face)
    {
        Require(cube.faces[face].size() == 4u * 4u * 4u,
            "cubemap face expands to tight RGBA8");
        const Bytes expectedBlock = MakeDxtColorBlock(
            colors[face], colors[face], 0u);
        Bytes singlePayload(expectedBlock);
        Rgba8Image expected{};
        RequireError(kisak::iwi::DecodeRgba8(MakeDxtIwi(
            kisak::iwi::FORMAT_DXT1, 4u, 4u, singlePayload), expected),
            Error::None, "decode expected cubemap face");
        Require(cube.faces[face] == expected.pixels,
            "cubemap preserves native +X,-X,+Y,-Y,+Z,-Z face order");
    }
    Require(cube.mipFaces.empty(),
        "no-mipmap cubemap does not invent authored levels");

    Bytes mipPayload;
    for (std::size_t face = 0u; face < 6u; ++face)
        Append(mipPayload, MakeDxtColorBlock(0x001fu, 0x001fu, 0u));
    for (std::size_t face = 0u; face < 6u; ++face)
        Append(mipPayload, MakeDxtColorBlock(0x07e0u, 0x07e0u, 0u));
    for (const std::uint16_t color : colors)
        Append(mipPayload, MakeDxtColorBlock(color, color, 0u));
    Rgba8Cube mippedCube{};
    RequireError(kisak::iwi::DecodeCubeRgba8(MakeDxtIwi(
        kisak::iwi::FORMAT_DXT1, 4u, 4u, mipPayload,
        kisak::iwi::FLAG_CUBEMAP), mippedCube), Error::None,
        "decode complete smallest-to-largest IWI cubemap mip chain");
    Require(mippedCube.mipFaces.size() == 2u,
        "IWI cubemap retains every authored level after the base face");
    Require(mippedCube.mipFaces[0][0].size() == 2u * 2u * 4u &&
        mippedCube.mipFaces[0][0][1] > 240u &&
        mippedCube.mipFaces[1][0].size() == 4u &&
        mippedCube.mipFaces[1][0][2] > 240u,
        "IWI cubemap levels preserve their authored colors and dimensions");

    Rgba8Cube sentinel{};
    sentinel.edgeLength = 7u;
    sentinel.faces[0] = {1u, 2u, 3u};
    const Rgba8Cube before = sentinel;
    Bytes truncated = iwi;
    truncated.pop_back();
    for (std::size_t picmip = 0u; picmip < 4u; ++picmip)
        PatchU32(truncated, 12u + picmip * 4u,
            static_cast<std::uint32_t>(truncated.size()));
    RequireError(kisak::iwi::DecodeCubeRgba8(truncated, sentinel),
        Error::DecodeInvalidLayout, "reject truncated cubemap");
    Require(sentinel.edgeLength == before.edgeLength &&
        sentinel.faces == before.faces && sentinel.mipFaces == before.mipFaces,
        "failed cubemap decode leaves the destination unchanged");
}

void TestCanonicalLoadDefCubemapDecode()
{
    Bytes payload;
    constexpr std::array<std::uint16_t, 6> colors{
        0xf800u, 0x07e0u, 0x001fu, 0xffe0u, 0xf81fu, 0x07ffu};
    for (const std::uint16_t color : colors)
        Append(payload, MakeDxtColorBlock(color, color, 0u));
    Rgba8Cube cube{};
    RequireError(kisak::iwi::DecodeLoadDefCubeRgba8(
        kisak::iwi::LOADDEF_FORMAT_DXT1,
        kisak::iwi::FLAG_CUBEMAP | kisak::iwi::FLAG_NO_MIPMAPS,
        4u, 4u, 1u, payload, cube), Error::None,
        "decode canonical face-major load-definition cubemap");
    Require(cube.edgeLength == 4u && cube.faces.size() == 6u,
        "canonical cubemap retains all six faces");
    Require(cube.faces[0][0] > 240u && cube.faces[1][1] > 240u &&
        cube.faces[2][2] > 240u,
        "canonical cubemap decodes distinct RGB faces in native order");
    Require(cube.mipFaces.empty(),
        "no-mipmap load definition does not invent authored levels");

    Bytes mippedPayload;
    for (const std::uint16_t color : colors)
    {
        Append(mippedPayload, MakeDxtColorBlock(color, color, 0u));
        Append(mippedPayload, MakeDxtColorBlock(0x07e0u, 0x07e0u, 0u));
        Append(mippedPayload, MakeDxtColorBlock(0x001fu, 0x001fu, 0u));
    }
    Rgba8Cube mippedCube{};
    RequireError(kisak::iwi::DecodeLoadDefCubeRgba8(
        kisak::iwi::LOADDEF_FORMAT_DXT1,
        kisak::iwi::FLAG_CUBEMAP,
        4u, 4u, 1u, mippedPayload, mippedCube), Error::None,
        "decode complete face-major load-definition cubemap mip chain");
    Require(mippedCube.mipFaces.size() == 2u &&
        mippedCube.mipFaces[0][0].size() == 2u * 2u * 4u &&
        mippedCube.mipFaces[0][0][1] > 240u &&
        mippedCube.mipFaces[1][0].size() == 4u &&
        mippedCube.mipFaces[1][0][2] > 240u,
        "load-definition cubemap retains authored roughness levels");

    Rgba8Cube sentinel{};
    sentinel.edgeLength = 9u;
    sentinel.faces[5] = {9u};
    const Rgba8Cube before = sentinel;
    payload.pop_back();
    RequireError(kisak::iwi::DecodeLoadDefCubeRgba8(
        kisak::iwi::LOADDEF_FORMAT_DXT1,
        kisak::iwi::FLAG_CUBEMAP | kisak::iwi::FLAG_NO_MIPMAPS,
        4u, 4u, 1u, payload, sentinel), Error::DecodeOutputTooLarge,
        "reject uneven canonical cubemap payload");
    Require(sentinel.edgeLength == before.edgeLength &&
        sentinel.faces == before.faces && sentinel.mipFaces == before.mipFaces,
        "failed canonical cubemap decode is atomic");
}

void TestCanonicalLoadDefDecode()
{
    Bytes bgra{
        0x10u, 0x20u, 0x30u, 0x40u,
        0x50u, 0x60u, 0x70u, 0x80u,
    };
    Rgba8Image image{};
    RequireError(kisak::iwi::DecodeLoadDefRgba8(
        kisak::iwi::LOADDEF_FORMAT_A8R8G8B8,
        kisak::iwi::FLAG_NO_MIPMAPS,
        2u,
        1u,
        1u,
        bgra,
        image), Error::None, "decode canonical uncompressed load definition");
    Require(image.pixels == Bytes({
        0x30u, 0x20u, 0x10u, 0x40u,
        0x70u, 0x60u, 0x50u, 0x80u,
    }), "canonical A8R8G8B8 load definition is swizzled to RGBA8");

    RequireError(kisak::iwi::DecodeLoadDefRgba8(
        kisak::iwi::LOADDEF_FORMAT_X8R8G8B8,
        kisak::iwi::FLAG_NO_MIPMAPS,
        2u,
        1u,
        1u,
        bgra,
        image), Error::None, "decode canonical opaque BGRX load definition");
    Require(image.pixels == Bytes({
        0x30u, 0x20u, 0x10u, 0xffu,
        0x70u, 0x60u, 0x50u, 0xffu,
    }), "canonical X8R8G8B8 ignores the unused source alpha byte");

    const Bytes luminance{0x10u, 0x80u, 0xffu, 0x00u};
    RequireError(kisak::iwi::DecodeLoadDefRgba8(
        kisak::iwi::LOADDEF_FORMAT_L8,
        kisak::iwi::FLAG_NO_MIPMAPS,
        2u,
        2u,
        1u,
        luminance,
        image), Error::None, "decode canonical L8 lightmap load definition");
    Require(image.pixels == Bytes({
        0x10u, 0x10u, 0x10u, 0xffu,
        0x80u, 0x80u, 0x80u, 0xffu,
        0xffu, 0xffu, 0xffu, 0xffu,
        0x00u, 0x00u, 0x00u, 0xffu,
    }), "canonical L8 lightmap expands to opaque RGBA8");

    const Bytes luminanceAlpha{
        0x10u, 0x20u,
        0x40u, 0x80u,
        0x7fu, 0xffu,
        0xffu, 0x00u,
    };
    RequireError(kisak::iwi::DecodeLoadDefRgba8(
        kisak::iwi::LOADDEF_FORMAT_A8L8,
        kisak::iwi::FLAG_NO_MIPMAPS,
        2u,
        2u,
        1u,
        luminanceAlpha,
        image), Error::None, "decode canonical A8L8 load definition");
    Require(image.pixels == Bytes({
        0x10u, 0x10u, 0x10u, 0x20u,
        0x40u, 0x40u, 0x40u, 0x80u,
        0x7fu, 0x7fu, 0x7fu, 0xffu,
        0xffu, 0xffu, 0xffu, 0x00u,
    }), "canonical A8L8 expands luminance and preserves alpha");

    const Bytes red = MakeDxtColorBlock(0xf800u, 0x07e0u, 0u);
    const Bytes blue = MakeDxtColorBlock(0x001fu, 0x07e0u, 0u);
    Bytes nativeMipOrder;
    Append(nativeMipOrder, blue); // 4x4 base level first.
    Append(nativeMipOrder, red);  // 2x2 mip.
    Append(nativeMipOrder, red);  // 1x1 mip.
    RequireError(kisak::iwi::DecodeLoadDefRgba8(
        kisak::iwi::LOADDEF_FORMAT_DXT1,
        0u,
        4u,
        4u,
        1u,
        nativeMipOrder,
        image), Error::None, "decode canonical largest-to-smallest DXT load definition");
    Require(image.pixels.size() == 4u * 4u * 4u &&
            image.pixels[0] == 0u && image.pixels[1] == 0u &&
            image.pixels[2] == 255u && image.pixels[3] == 255u,
        "canonical load-definition decode uses the leading base mip");

    const Rgba8Image sentinel = MakeSentinelImage();
    image = sentinel;
    nativeMipOrder.pop_back();
    RequireError(kisak::iwi::DecodeLoadDefRgba8(
        kisak::iwi::LOADDEF_FORMAT_DXT1,
        0u,
        4u,
        4u,
        1u,
        nativeMipOrder,
        image), Error::DecodeInvalidLayout,
        "reject truncated canonical load-definition mip chain");
    Require(SameImage(image, sentinel),
        "failed canonical load-definition decode is atomic");
    RequireError(kisak::iwi::DecodeLoadDefRgba8(
        0x12345678,
        kisak::iwi::FLAG_NO_MIPMAPS,
        4u,
        4u,
        1u,
        red,
        image), Error::DecodeUnsupportedFormat,
        "reject an unsupported canonical load-definition format");
}

void TestRgba8DecodeRejectsUnsupportedSlice()
{
    for (const uint8_t flags : {
        uint8_t{0x00},
        uint8_t{0x01},
        uint8_t{0x03},
        uint8_t{0x04},
        uint8_t{0x08},
        uint8_t{0x06},
        uint8_t{0x22},
        uint8_t{0x40},
        uint8_t{0x80},
        uint8_t{0xff},
    })
    {
        RequireDecodeFailure(
            MakeIwi(kisak::iwi::FORMAT_ARGB, flags),
            Error::DecodeUnsupportedFlags,
            "reject flags outside the initial no-mipmap slice");
    }

    RequireDecodeFailure(
        MakeIwi(
            kisak::iwi::FORMAT_ARGB,
            kisak::iwi::FLAG_NO_MIPMAPS,
            2,
            2,
            2,
            32),
        Error::DecodeUnsupportedDimensions,
        "reject volume depth");

    Bytes policyArgb = MakeIwi(kisak::iwi::FORMAT_ARGB,
        kisak::iwi::FLAG_NO_MIPMAPS | kisak::iwi::FLAG_STREAMING |
            kisak::iwi::FLAG_CLAMP_U | kisak::iwi::FLAG_CLAMP_V);
    Rgba8Image policyImage{};
    RequireError(kisak::iwi::DecodeRgba8(policyArgb, policyImage),
        Error::None, "accept ARGB streaming and address-policy flags");

}

void TestRgba8DecodeLayoutAndLimits()
{
    RequireDecodeFailure(
        MakeIwi(
            kisak::iwi::FORMAT_ARGB,
            kisak::iwi::FLAG_NO_MIPMAPS,
            2,
            2,
            1,
            15),
        Error::DecodeInvalidLayout,
        "reject truncated tight pixel payload");
    RequireDecodeFailure(
        MakeIwi(
            kisak::iwi::FORMAT_ARGB,
            kisak::iwi::FLAG_NO_MIPMAPS,
            2,
            2,
            1,
            17),
        Error::DecodeInvalidLayout,
        "reject trailing pixel payload bytes");

    RequireDecodeFailure(
        MakeIwi(
            kisak::iwi::FORMAT_ARGB,
            kisak::iwi::FLAG_NO_MIPMAPS,
            4096,
            2048,
            1,
            4),
        Error::DecodeOutputTooLarge,
        "reject dimensions whose checked RGBA8 layout exceeds the bound");

    // Exercise a raw image immediately below the shared 8 MiB member ceiling.
    constexpr uint16_t BOUNDARY_WIDTH = 1024u;
    constexpr uint16_t BOUNDARY_HEIGHT = 2047u;
    constexpr std::size_t BOUNDARY_PAYLOAD =
        static_cast<std::size_t>(BOUNDARY_WIDTH) * BOUNDARY_HEIGHT * 4u;
    static_assert(
        BOUNDARY_PAYLOAD + kisak::iwi::HEADER_SIZE <
            kisak::iwi::MAX_TEXTURE_MEMBER_BYTES &&
        BOUNDARY_PAYLOAD + kisak::iwi::HEADER_SIZE >
            kisak::iwi::MAX_TEXTURE_MEMBER_BYTES - 4096u);

    const Bytes boundary = MakeIwi(
        kisak::iwi::FORMAT_ARGB,
        kisak::iwi::FLAG_NO_MIPMAPS,
        BOUNDARY_WIDTH,
        BOUNDARY_HEIGHT,
        1,
        BOUNDARY_PAYLOAD);
    Rgba8Image boundaryImage{};
    RequireError(kisak::iwi::DecodeRgba8(boundary, boundaryImage), Error::None,
        "accept a member exactly at the shared cache ceiling");
    Require(boundaryImage.width == BOUNDARY_WIDTH &&
            boundaryImage.height == BOUNDARY_HEIGHT &&
            boundaryImage.pixels.size() == BOUNDARY_PAYLOAD,
        "boundary decode returns the exact bounded RGBA8 allocation");

    RequireDecodeFailure(
        MakeIwi(
            kisak::iwi::FORMAT_ARGB,
            kisak::iwi::FLAG_NO_MIPMAPS,
            1025,
            BOUNDARY_HEIGHT,
            1,
            static_cast<std::size_t>(1025u) * BOUNDARY_HEIGHT * 4u),
        Error::DecodeOutputTooLarge,
        "reject an exact layout above the shared cache ceiling");
}

void TestRgba8DecodePropagatesParserErrors()
{
    Bytes invalidTag = MakeIwi();
    invalidTag[0] = 'X';
    RequireDecodeFailure(invalidTag, Error::InvalidTag,
        "decoder preserves invalid-tag parser error");

    Bytes invalidSize = MakeIwi();
    PatchU32(invalidSize, 12, static_cast<uint32_t>(invalidSize.size() - 1u));
    RequireDecodeFailure(invalidSize, Error::InvalidFileSize,
        "decoder preserves file-size parser error");

    const Bytes fixture = MakeIwi();
    for (std::size_t length = 0; length < kisak::iwi::HEADER_SIZE; ++length)
    {
        const Bytes truncated(
            fixture.begin(),
            fixture.begin() + static_cast<Bytes::difference_type>(length));
        RequireDecodeFailure(truncated, Error::HeaderTruncated,
            "decoder preserves truncated-header parser error");
    }
}

uint32_t NextRandom(uint32_t &state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void TestDeterministicMutationCorpus()
{
    const Bytes seed = MakeIwi();
    Metadata baseline{};
    RequireError(kisak::iwi::Parse(seed, baseline), Error::None, "mutation seed parses");

    for (std::size_t offset = 0; offset < kisak::iwi::HEADER_SIZE; ++offset)
    {
        for (const uint8_t mask : {uint8_t{0x01}, uint8_t{0x80}, uint8_t{0xff}})
        {
            Bytes mutated = seed;
            mutated[offset] ^= mask;
            Metadata metadata = baseline;
            const Error error = kisak::iwi::Parse(mutated, metadata);
            if (error == Error::None)
            {
                RequireMetadataInvariant(metadata, mutated.size());
            }
            else
            {
                Require(SameMetadata(metadata, baseline),
                    "failed header mutation leaves metadata unchanged");
            }
        }
    }

    uint32_t randomState = 0x91e10da5u;
    for (std::size_t iteration = 0; iteration < 10000; ++iteration)
    {
        const std::size_t length = NextRandom(randomState) % 97u;
        Bytes bytes(length);
        for (uint8_t &byte : bytes)
        {
            byte = static_cast<uint8_t>(NextRandom(randomState));
        }

        Metadata metadata = baseline;
        const Error error = kisak::iwi::Parse(bytes, metadata);
        if (error == Error::None)
        {
            RequireMetadataInvariant(metadata, bytes.size());
        }
        else
        {
            Require(SameMetadata(metadata, baseline),
                "failed deterministic fuzz input leaves metadata unchanged");
        }
    }
}

void TestErrorStrings()
{
    for (const Error error : {
        Error::None,
        Error::HeaderTruncated,
        Error::InvalidTag,
        Error::UnsupportedVersion,
        Error::UnsupportedFormat,
        Error::InvalidDimensions,
        Error::InvalidFileSize,
        Error::DecodeUnsupportedFormat,
        Error::DecodeUnsupportedFlags,
        Error::DecodeUnsupportedDimensions,
        Error::DecodeInvalidLayout,
        Error::DecodeOutputTooLarge,
        Error::DecodeAllocationFailed,
    })
    {
        Require(std::strlen(kisak::iwi::ErrorString(error)) > 0,
            "every parser error has a printable description");
    }
}

class Runner
{
public:
    void Run(const char *name, const std::function<void()> &test)
    {
        try
        {
            test();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        }
        catch (const std::exception &error)
        {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
        catch (...)
        {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": unknown exception\n";
        }
    }

    int Result() const
    {
        std::cout << passed_ << " passed, " << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

private:
    int passed_ = 0;
    int failed_ = 0;
};
} // namespace

int main()
{
    Runner runner;
    runner.Run("valid header and formats", TestHappyPathAndSupportedFormats);
    runner.Run("truncation and atomic failure", TestTruncationAndAtomicFailure);
    runner.Run("header validation", TestHeaderValidation);
    runner.Run("file-size validation", TestFileSizeValidation);
    runner.Run("RGBA8 decode and swizzle", TestRgba8DecodeAndSwizzle);
    runner.Run("RGB8 decode and mip order", TestRgb8DecodeAndMipOrder);
    runner.Run("A8L8 decode and mip order", TestA8L8DecodeAndMipOrder);
    runner.Run("L8 decode and mip order", TestL8DecodeAndMipOrder);
    runner.Run("DXT1 decode", TestDxt1Decode);
    runner.Run("DXT3 decode", TestDxt3Decode);
    runner.Run("DXT5 decode", TestDxt5Decode);
    runner.Run("DXT mip order and clipping", TestDxtMipOrderAndClipping);
    runner.Run("DXT validation and atomic failure", TestDxtValidationAndAtomicFailure);
    runner.Run("IWI cubemap decode and face order", TestCubemapDecodeAndNativeFaceOrder);
    runner.Run("canonical cubemap load-definition decode", TestCanonicalLoadDefCubemapDecode);
    runner.Run("canonical DB load-definition decode", TestCanonicalLoadDefDecode);
    runner.Run("RGBA8 unsupported slice", TestRgba8DecodeRejectsUnsupportedSlice);
    runner.Run("RGBA8 layout and limits", TestRgba8DecodeLayoutAndLimits);
    runner.Run("RGBA8 parser error propagation", TestRgba8DecodePropagatesParserErrors);
    runner.Run("deterministic mutation corpus", TestDeterministicMutationCorpus);
    runner.Run("error strings", TestErrorStrings);
    return runner.Result();
}
