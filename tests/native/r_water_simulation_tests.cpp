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
    TestInvalidCanonicalGridIsRejectedWithoutWriting();
    return 0;
}
