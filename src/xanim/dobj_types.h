#pragma once

struct DObjAnimMat
{
    float quat[4];
    float trans[3];
    float transWeight;
};

static_assert(sizeof(DObjAnimMat) == 32u);
