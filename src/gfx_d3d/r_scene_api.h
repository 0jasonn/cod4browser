#pragma once

#include <cstdint>
#include "gfx_particle_cloud_types.h"

struct GfxScaledPlacement;
struct Material;
struct XModel;

std::uint32_t __cdecl R_GetLocalClientNum();
GfxParticleCloud *__cdecl R_AddParticleCloudToScene(Material *material);
void __cdecl R_AddOmniLightToScene(const float *org, float radius,
    float r, float g, float b);
void __cdecl R_AddSpotLightToScene(const float *org, const float *dir,
    float radius, float r, float g, float b);
void __cdecl R_FilterXModelIntoScene(const XModel *model,
    const GfxScaledPlacement *placement, std::uint16_t renderFxFlags,
    std::uint16_t *cachedLightingHandle);
