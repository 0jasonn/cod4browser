#pragma once

#include <universal/q_shared.h>
#include <universal/com_math.h>
#include <xanim/dobj.h>
#include <xanim/xmodel_types.h>

namespace kisak::model_pose
{
inline void AccumulateBoneBounds(const DObjAnimMat &pose, const XBoneInfo &bone,
    const float viewOffset[3], float mins[3], float maxs[3]) noexcept
{
    DObjSkelMat matrix{};
    ConvertQuatToSkelMat(&pose, &matrix);
    for (unsigned output = 0; output < 3; ++output)
    {
        float lower = matrix.origin[output] + viewOffset[output];
        float upper = lower;
        for (unsigned input = 0; input < 3; ++input)
        {
            const float coefficient = matrix.axis[input][output];
            lower = (coefficient >= 0.0f ? bone.bounds[0][input] : bone.bounds[1][input]) *
                coefficient + lower;
            upper = (coefficient >= 0.0f ? bone.bounds[1][input] : bone.bounds[0][input]) *
                coefficient + upper;
        }
        if (lower < mins[output]) mins[output] = lower;
        if (upper > maxs[output]) maxs[output] = upper;
    }
}
}
