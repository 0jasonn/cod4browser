#pragma once

// Native packed normal/tangent arithmetic, shared with portable draw adapters.
inline void Q_UnpackUnitVec(const unsigned char value[4], float out[3])
{
    const float scale = (value[3] - -192.0f) / 32385.0f;
    out[0] = (value[0] - 127.0f) * scale;
    out[1] = (value[1] - 127.0f) * scale;
    out[2] = (value[2] - 127.0f) * scale;
}

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

// Canonical row-major quaternion expansion. Callers validate unit length.
inline void Q_UnitQuatToAxis(const float *quat, float axis[3][3]) noexcept
{
    const float scaledX = *quat + *quat;
    const float xx = scaledX * *quat;
    const float xy = scaledX * quat[1];
    const float xz = scaledX * quat[2];
    const float xw = scaledX * quat[3];
    const float scaledY = quat[1] + quat[1];
    const float yy = scaledY * quat[1];
    const float yz = scaledY * quat[2];
    const float yw = scaledY * quat[3];
    const float scaledZ = quat[2] + quat[2];
    const float zw = scaledZ * quat[3];
    const float zz = scaledZ * quat[2];

    (axis)[0][0] = 1.0 - (yy + zz);
    (axis)[0][1] = xy + zw;
    (axis)[0][2] = xz - yw;
    (axis)[1][0] = xy - zw;
    (axis)[1][1] = 1.0 - (xx + zz);
    (axis)[1][2] = yz + xw;
    (axis)[2][0] = xz + yw;
    (axis)[2][1] = yz - xw;
    (axis)[2][2] = 1.0 - (xx + yy);
}
