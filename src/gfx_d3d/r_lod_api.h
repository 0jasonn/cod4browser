#pragma once

#include <xanim/xmodel_types.h>

double __cdecl R_GetBaseLodDist(const float *origin);
double __cdecl R_GetAdjustedLodDist(float dist, XModelLodRampType lodRampType);
