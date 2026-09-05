#pragma once

#include <cstdint>
#include "gfx_particle_cloud_types.h"

struct GfxScaledPlacement;
struct Material;
struct XModel;

// Presentation metadata from cgame; native D3D strips this unsupported bit.
// Pickup identity must not be inferred from shared weapon material names.
constexpr std::uint32_t GFX_RENDERFX_PICKUP = 0x1000u;

std::uint32_t __cdecl R_GetLocalClientNum();
GfxParticleCloud *__cdecl R_AddParticleCloudToScene(Material *material);
void __cdecl R_AddOmniLightToScene(const float *org, float radius,
    float r, float g, float b);
void __cdecl R_AddSpotLightToScene(const float *org, const float *dir,
    float radius, float r, float g, float b);
void __cdecl R_FilterXModelIntoScene(const XModel *model,
    const GfxScaledPlacement *placement, std::uint16_t renderFxFlags,
    std::uint16_t *cachedLightingHandle);
