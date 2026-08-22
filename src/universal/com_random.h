#pragma once

#include <cstdint>

// COD's original MSVC runtime supplied a 15-bit rand(). Map wider platform
// rand() results back to the same 32,768 buckets before converting to float.
constexpr float Q_RandomToUnitFloat(
    std::uint32_t sample, std::uint32_t maximum) noexcept
{
    if (maximum == 0u) return 0.0f;
    const std::uint64_t bucket =
        static_cast<std::uint64_t>(sample) * 32768u /
        (static_cast<std::uint64_t>(maximum) + 1u);
    return static_cast<float>(bucket) * (1.0f / 32768.0f);
}
