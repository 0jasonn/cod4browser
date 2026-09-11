#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Browser callback admission and elapsed simulation time are different clocks.
// Keep fractional admission phase without replaying it as simulation time.
struct WebFrameTiming
{
    int Advance(std::uint32_t now, int maximumFps,
        std::uint32_t resetTime = 0u) noexcept
    {
        // Com_ResetFrametime excludes map loading from the native frame clock.
        // Preserve elapsed time after that reset, including real gameplay stalls.
        if (resetTime != canonicalResetTime)
        {
            *this = {};
            canonicalResetTime = resetTime;
            initialized = true;
            previous = resetTime;
        }
        if (!initialized)
        {
            initialized = true;
            previous = now;
            return 16;
        }
        const auto elapsed = std::min(now - previous, 5000u);
        previous = now;
        simulationElapsed = std::min(simulationElapsed + elapsed, 5000u);
        admissionRemainder += elapsed;
        // Preserve the existing uncapped 125 Hz safety policy.
        const double period = maximumFps > 0
            ? std::max(1.0, 1000.0 / maximumFps) : 8.0;
        if (!simulationElapsed || admissionRemainder + 1e-9 < std::floor(period))
            return 0;
        // The platform clock is integer milliseconds. Permit its sub-millisecond
        // rounding at admission, but carry that debt into the next period.
        admissionRemainder -= period;
        if (admissionRemainder >= period)
            admissionRemainder = std::fmod(admissionRemainder, period);
        const int result = static_cast<int>(simulationElapsed);
        simulationElapsed = 0u;
        return result;
    }

    bool initialized = false;
    std::uint32_t canonicalResetTime = 0u;
    std::uint32_t previous = 0u;
    std::uint32_t simulationElapsed = 0u;
    double admissionRemainder = 0.0;
};
