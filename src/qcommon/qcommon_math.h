#pragma once

#if defined(_WIN32) && (defined(_M_IX86) || defined(_M_X64))
#include <xmmintrin.h>
#include <intrin.h>
#else
#include <cmath>
#endif

// The original sites use banker's rounding (nearest, ties to even) through
// x87 fistp or SSE cvtss. Plain integer casts still truncate and must not be
// replaced with this helper.
inline int SnapFloatToInt(float value)
{
#if defined(KISAK_PURE) && defined(_WIN32) && defined(_M_IX86)
    int result;
    __asm fld value;
    __asm fistp result;
    return result;
#elif defined(_WIN32) && (defined(_M_IX86) || defined(_M_X64))
    const int result = _mm_cvtss_si32(_mm_set_ss(value));
#if defined(_DEBUG) && defined(_M_IX86)
    int x87Result;
    __asm fld value;
    __asm fistp x87Result;
    if (result != x87Result)
    {
        __debugbreak();
    }
#endif
    return result;
#else
    // nearbyint follows the active IEEE-754 rounding mode without changing
    // it. Kisak uses the default round-to-nearest, ties-to-even mode.
    return static_cast<int>(std::nearbyint(value));
#endif
}

inline float SnapFloat(float value)
{
    return static_cast<float>(SnapFloatToInt(value));
}
