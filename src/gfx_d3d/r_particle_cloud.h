#pragma once

#include <cassert>
#include <cmath>

// Shared R_CreateParticleCloudBuffer cell placement. Keep the intermediate
// arithmetic in double, matching the native rand()/32767 expressions before
// their assignment to GfxPosTexVertex floats.
inline void R_CreateParticleCloudCellPosition(
    int x, int y, int z, const double random[3], float position[3]) noexcept
{
    position[0] = static_cast<float>((random[0] + x) * 0.25 - 1.0);
    position[1] = static_cast<float>((random[1] + y) * 0.25 - 1.0);
    position[2] = static_cast<float>((random[2] + z) * 0.125 - 1.0);
}

// Shared RB_CreateParticleCloud2dAxis policy. Preserve the native signed
// threshold and minimum projected length, including negative-view fallback.
inline void R_CreateParticleCloud2dAxis(const float radius[2],
    const float viewUp[3], float viewAxis[2][2])
{
    if (viewUp[0] >= 0.001f || viewUp[1] >= 0.001f)
    {
        viewAxis[0][0] = -1.0f * viewUp[1];
        viewAxis[0][1] = -1.0f * viewUp[0];
        viewAxis[1][0] = viewUp[0];
        viewAxis[1][1] = viewUp[1];
        const float length = std::sqrt(viewAxis[0][1] * viewAxis[0][1] +
            viewAxis[0][0] * viewAxis[0][0]);
        assert(length > 0.0f);
        viewAxis[0][0] *= radius[0] / length;
        viewAxis[0][1] *= radius[0] / length;
        if (radius[0] > static_cast<double>(length))
        {
            viewAxis[1][0] *= radius[0] / length;
            viewAxis[1][1] *= radius[0] / length;
        }
    }
    else
    {
        viewAxis[0][0] = radius[0];
        viewAxis[0][1] = 0.0f;
        viewAxis[1][0] = 0.0f;
        viewAxis[1][1] = radius[1];
    }
}
