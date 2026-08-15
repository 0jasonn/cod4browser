#include <web/web_asset_probe.h>

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
using Header = std::array<std::uint8_t, 14>;

Header ValidHeader()
{
    return {
        'I', 'W', 'f', 'f', 'u', '1', '0', '0',
        5u, 0u, 0u, 0u,
        0x78u, 0xdau,
    };
}

void Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(message));
    }
}

void RequireResult(
    std::int32_t actual,
    WebAssetProbeResult expected,
    std::string_view message)
{
    Require(actual == static_cast<std::int32_t>(expected), message);
}

void TestFastfileHeader()
{
    Header header = ValidHeader();
    RequireResult(
        KisakWeb_ProbeFastfileHeader(header.data(), header.size(), header.size()),
        WebAssetProbeResult::Success,
        "retail unsigned v5 zlib framing is accepted");

    header[13] = 0x01u;
    RequireResult(
        KisakWeb_ProbeFastfileHeader(header.data(), header.size(), 1024u),
        WebAssetProbeResult::Success,
        "a second valid zlib compression-level header is accepted");

    RequireResult(
        KisakWeb_ProbeFastfileHeader(nullptr, header.size(), header.size()),
        WebAssetProbeResult::InvalidArgument,
        "null probe memory is rejected");
    RequireResult(
        KisakWeb_ProbeFastfileHeader(header.data(), header.size() - 1u, header.size()),
        WebAssetProbeResult::InvalidArgument,
        "inconsistent bounded head length is rejected");
    RequireResult(
        KisakWeb_ProbeFastfileHeader(header.data(), header.size() - 1u, header.size() - 1u),
        WebAssetProbeResult::FastfileHeader,
        "a physically truncated fastfile header is rejected");
}

void TestFastfileHeaderFailures()
{
    Header header = ValidHeader();
    header[4] = '0';
    RequireResult(
        KisakWeb_ProbeFastfileHeader(header.data(), header.size(), header.size()),
        WebAssetProbeResult::FastfileAuthenticated,
        "authenticated framing remains explicitly unsupported");

    header = ValidHeader();
    header[0] = 'X';
    RequireResult(
        KisakWeb_ProbeFastfileHeader(header.data(), header.size(), header.size()),
        WebAssetProbeResult::FastfileHeader,
        "unknown fastfile magic is rejected");

    header = ValidHeader();
    header[8] = 6u;
    RequireResult(
        KisakWeb_ProbeFastfileHeader(header.data(), header.size(), header.size()),
        WebAssetProbeResult::FastfileVersion,
        "non-COD4 fastfile version is rejected");

    for (const std::array<std::uint8_t, 2> invalid : {
            std::array<std::uint8_t, 2>{0x00u, 0x00u},
            std::array<std::uint8_t, 2>{0x78u, 0x20u},
            std::array<std::uint8_t, 2>{0x78u, 0x00u},
        })
    {
        header = ValidHeader();
        header[12] = invalid[0];
        header[13] = invalid[1];
        RequireResult(
            KisakWeb_ProbeFastfileHeader(header.data(), header.size(), header.size()),
            WebAssetProbeResult::FastfileCompression,
            "invalid or preset-dictionary zlib header is rejected");
    }
}
} // namespace

int main()
{
    try
    {
        TestFastfileHeader();
        TestFastfileHeaderFailures();
        std::cout << "2 passed, 0 failed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
