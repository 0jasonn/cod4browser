#include <EffectsCore/fx_emission_math.h>

#include <cassert>

namespace
{
void TestMsvcCompatibility()
{
    constexpr double oldScale = 0.000030517578125;
    constexpr std::int32_t values[] = {0, 1, 16384, 32767};
    for (const std::int32_t value : values)
    {
        assert(FX_NormalizeRandFraction(value, 32767) ==
            static_cast<double>(value) * oldScale);
    }
}

void TestWideCrtRangeAndSpacing()
{
    constexpr std::int32_t maximum = 0x7fffffff;
    constexpr double base = 4.0;
    constexpr double amplitude = 16.0;
    constexpr std::int32_t values[] = {0, 1, 0x40000000, maximum};
    for (const std::int32_t value : values)
    {
        const double fraction = FX_NormalizeRandFraction(value, maximum);
        assert(fraction >= 0.0 && fraction < 1.0);
        const double spacing = FX_EmissionDistanceWithVariance(
            base, amplitude, value, maximum);
        assert(spacing >= base && spacing < base + amplitude);
    }
    assert(FX_EmissionDistanceWithVariance(
        base, amplitude, -1, maximum) == base);
    assert(FX_EmissionDistanceWithVariance(
        base, amplitude, 11, 10) < base + amplitude);
}
}

int main()
{
    TestMsvcCompatibility();
    TestWideCrtRangeAndSpacing();
    return 0;
}
