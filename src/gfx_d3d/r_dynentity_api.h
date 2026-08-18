#pragma once

#include <cstdint>
#include <qcommon/qcommon.h>

struct GfxBrushModel;
struct GfxScaledPlacement;
struct XModel;

GfxBrushModel *__cdecl R_GetBrushModel(std::uint32_t modelIndex);
void __cdecl R_LinkDynEnt(std::uint32_t dynEntId, DynEntityDrawType drawType,
    float *mins, float *maxs);
void __cdecl R_UnlinkDynEnt(std::uint32_t dynEntId, DynEntityDrawType drawType);
void __cdecl R_FilterXModelIntoScene(const XModel *model,
    const GfxScaledPlacement *placement, std::uint16_t renderFxFlags,
    std::uint16_t *cachedLightingHandle);
