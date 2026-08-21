#pragma once

#include <cstdint>

#include <cstdlib>

// MSVC's rand() contract uses a 15-bit inclusive maximum. Dividing by
// RAND_MAX + 1 preserves the historical raw * (1 / 32768) expression while
// keeping every other CRT's valid output in [0, 1).
constexpr double FX_NormalizeRandFraction(
    std::int32_t rawValue, std::int32_t randMaximum) noexcept
{
    if (rawValue < 0 || randMaximum <= 0)
        return 0.0;
    if (rawValue > randMaximum)
        rawValue = randMaximum;
    return static_cast<double>(rawValue) /
        (static_cast<double>(randMaximum) + 1.0);
}

constexpr double FX_CrtRandFraction(std::int32_t rawValue) noexcept
{
    return FX_NormalizeRandFraction(rawValue, RAND_MAX);
}

constexpr double FX_EmissionDistanceWithVariance(
    double baseDistance, double amplitude,
    std::int32_t rawValue, std::int32_t randMaximum) noexcept
{
    return baseDistance + amplitude *
        FX_NormalizeRandFraction(rawValue, randMaximum);
}
