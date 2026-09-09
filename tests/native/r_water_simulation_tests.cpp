#include <universal/q_shared.h>
#include <gfx_d3d/r_water.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    assert(false);
}

namespace
{
void TestCanonicalZeroSpectrumProducesZeroL8Field()
{
    std::array<complex_s, 16> h0{};
    std::array<float, 16> wTerm{};
    std::array<std::uint8_t, 16> pixels{};
    pixels.fill(0xffu);
    water_t water{};
    water.H0 = h0.data();
    water.wTerm = wTerm.data();
    water.M = 4;
    water.N = 4;
    assert(R_GenerateWaterPixelsR8(
        &water, 3.25f, pixels.data(), pixels.size()));
    assert(std::all_of(pixels.begin(), pixels.end(),
        [](std::uint8_t value) { return value == 0u; }));
}

void TestAnimatedCanonicalSpectrumAndCapacity()
{
    // Synthetic Fourier modes only; no retail water or texture data.
    std::array<complex_s, 16> h0{};
    std::array<float, 16> wTerm{};
    h0[1] = {8.0f, 4.0f};
    h0[4] = {2.0f, 1.0f};
    wTerm[1] = 1.0f;
    wTerm[4] = 0.5f;
    const auto originalH0 = h0;
    const auto originalWTerm = wTerm;
    water_t water{};
    water.H0 = h0.data();
    water.wTerm = wTerm.data();
    water.M = water.N = 4;
    water.writable.floatTime = 7.0f;
    alignas(GfxColor) std::array<std::uint8_t, 16> first{}, second{}, repeated{};
    assert(R_GenerateWaterPixelsR8(&water, 0.0f, first.data(), first.size()));
    assert(R_GenerateWaterPixelsR8(&water, 1.0f, second.data(), second.size()));
    const auto nonzero = [](std::uint8_t value) { return value != 0; };
    assert(std::any_of(first.begin(), first.end(), nonzero));
    assert(std::any_of(second.begin(), second.end(), nonzero));
    assert(first != second);
    // Revisit both times with the same water identity after shared FFT scratch
    // was overwritten; the result must depend only on spectrum and time.
    assert(R_GenerateWaterPixelsR8(&water, 0.0f, repeated.data(), repeated.size()));
    assert(repeated == first);
    assert(R_GenerateWaterPixelsR8(&water, 1.0f, repeated.data(), repeated.size()));
    assert(repeated == second);
    repeated.fill(0xabu);
    assert(!R_GenerateWaterPixelsR8(&water, 1.0f, repeated.data(), repeated.size() - 1));
    assert(std::all_of(repeated.begin(), repeated.end(),
        [](std::uint8_t value) { return value == 0xabu; }));
    assert(R_GenerateWaterPixelsR8(&water, 1.0f, repeated.data(), repeated.size()));
    assert(repeated == second);
    assert(water.H0 == h0.data() && water.wTerm == wTerm.data());
    assert(water.writable.floatTime == 7.0f && wTerm == originalWTerm);
    for (std::size_t i = 0; i < h0.size(); ++i)
        assert(h0[i].real == originalH0[i].real && h0[i].imag == originalH0[i].imag);
}

void TestInvalidCanonicalGridIsRejectedWithoutWriting()
{
    std::array<complex_s, 15> h0{};
    std::array<float, 15> wTerm{};
    std::array<std::uint8_t, 16> pixels{};
    pixels.fill(0xabu);
    water_t water{};
    water.H0 = h0.data();
    water.wTerm = wTerm.data();
    water.M = 3;
    water.N = 5;
    assert(!R_GenerateWaterPixelsR8(
        &water, 0.0f, pixels.data(), pixels.size()));
    assert(std::all_of(pixels.begin(), pixels.end(),
        [](std::uint8_t value) { return value == 0xabu; }));
}
} // namespace

int main()
{
    TestCanonicalZeroSpectrumProducesZeroL8Field();
    TestAnimatedCanonicalSpectrumAndCapacity();
    TestInvalidCanonicalGridIsRejectedWithoutWriting();
    return 0;
}
