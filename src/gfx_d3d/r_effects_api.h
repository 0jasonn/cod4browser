#pragma once

#include <cstdint>
#include "r_scene_api.h"
#include "r_warning_types.h"

struct XModel;

void __cdecl R_GetAverageLightingAtPoint(const float *samplePos,
    std::uint8_t *outColor);
void R_WarnOncePerFrame(GfxWarningType warnType, ...);
XModel *__cdecl R_RegisterModel(const char *name);
