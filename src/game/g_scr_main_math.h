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
