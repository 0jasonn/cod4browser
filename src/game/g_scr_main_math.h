#pragma once

#include <cmath>
#include <cstdlib>

inline float GScr_ParseFloatValue(const char *value) noexcept
{
    return static_cast<float>(std::atof(value));
}

inline float GScr_FloorValue(float value) noexcept
{
    return std::floor(value);
}

inline float GScr_CeilValue(float value) noexcept
{
    return std::ceil(value);
}

inline int GScr_FxTriggerTime(float seconds) noexcept
{
    // MSVC long double is double; Wasm's is wider. Convert the value instead
    // of aliasing its storage so negative times can prewarm authored effects.
    return static_cast<int>(std::floor(seconds * 1000.0f + 0.5f));
}
