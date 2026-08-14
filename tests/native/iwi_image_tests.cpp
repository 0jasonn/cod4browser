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

void TestRgba8DecodeRejectsUnsupportedSlice()
{
    RequireDecodeFailure(
        MakeIwi(2, kisak::iwi::FLAG_NO_MIPMAPS),
        Error::DecodeUnsupportedFormat,
        "reject non-ARGB format");

    for (const uint8_t flags : {
        uint8_t{0x00},
        uint8_t{0x01},
        uint8_t{0x03},
        uint8_t{0x04},
        uint8_t{0x08},
        uint8_t{0x12},
        uint8_t{0x22},
        uint8_t{0x42},
        uint8_t{0x82},
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
            2048,
            2048,
            1,
            4),
        Error::DecodeOutputTooLarge,
        "reject dimensions whose checked RGBA8 layout exceeds the bound");

    // 579 * 1811 * 4 + 28 is exactly the shared 4 MiB member ceiling.
    constexpr uint16_t BOUNDARY_WIDTH = 579u;
    constexpr uint16_t BOUNDARY_HEIGHT = 1811u;
    constexpr std::size_t BOUNDARY_PAYLOAD =
        static_cast<std::size_t>(BOUNDARY_WIDTH) * BOUNDARY_HEIGHT * 4u;
    static_assert(
        BOUNDARY_PAYLOAD + kisak::iwi::HEADER_SIZE ==
        kisak::iwi::MAX_TEXTURE_MEMBER_BYTES);

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
            580,
            BOUNDARY_HEIGHT,
            1,
            static_cast<std::size_t>(580u) * BOUNDARY_HEIGHT * 4u),
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
        const Bytes truncated(fixture.begin(), fixture.begin() + length);
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
    runner.Run("RGBA8 unsupported slice", TestRgba8DecodeRejectsUnsupportedSlice);
    runner.Run("RGBA8 layout and limits", TestRgba8DecodeLayoutAndLimits);
    runner.Run("RGBA8 parser error propagation", TestRgba8DecodePropagatesParserErrors);
    runner.Run("deterministic mutation corpus", TestDeterministicMutationCorpus);
    runner.Run("error strings", TestErrorStrings);
    return runner.Result();
}
