#pragma once

#include <xanim/xmodel_types.h>

#include <cmath>
#include <cstddef>

int __cdecl XModelGetLodForDist(const XModel *model, float dist);

struct WebRendererLodRamp
{
    float scale = 1.0f;
    float bias = 0.0f;
};

struct WebRendererLodParms
{
    float origin[3]{};
    WebRendererLodRamp ramp[2]{};
    bool valid = false;
};

inline bool WebRenderer_BuildLodParms(
    const float viewOrigin[3],
    float tanHalfFovY,
    float rigidScale,
    float rigidBias,
    float skinnedScale,
    float skinnedBias,
    WebRendererLodParms &parms) noexcept
{
    if (!viewOrigin || !std::isfinite(viewOrigin[0]) ||
        !std::isfinite(viewOrigin[1]) || !std::isfinite(viewOrigin[2]) ||
        !std::isfinite(tanHalfFovY) || tanHalfFovY <= 0.0f ||
        !std::isfinite(rigidScale) || rigidScale < 0.0f ||
        !std::isfinite(rigidBias) || !std::isfinite(skinnedScale) ||
        skinnedScale < 0.0f || !std::isfinite(skinnedBias))
    {
        parms = {};
        return false;
    }

    // Exact R_UpdateLodParms conversion from the authored 640x480 FOV basis.
    constexpr float LOD_FOV_SCALE = 2.118673086166382f;
    const float invFovScale = tanHalfFovY * LOD_FOV_SCALE;
    for (std::size_t component = 0u; component < 3u; ++component)
        parms.origin[component] = viewOrigin[component];
    parms.ramp[0].scale = rigidScale * invFovScale;
    parms.ramp[0].bias = rigidBias * invFovScale;
    parms.ramp[1].scale = skinnedScale * invFovScale;
    parms.ramp[1].bias = skinnedBias * invFovScale;
    parms.valid = true;
    return true;
}

inline int WebRenderer_SelectModelLod(
    const XModel *model,
    const float modelOrigin[3],
    float modelScale,
    const WebRendererLodParms &parms) noexcept
{
    if (!model || !modelOrigin || !parms.valid || model->numLods <= 0 ||
        model->numLods > MAX_LODS || model->lodRampType > 1u ||
        !std::isfinite(modelOrigin[0]) || !std::isfinite(modelOrigin[1]) ||
        !std::isfinite(modelOrigin[2]) || !std::isfinite(modelScale) ||
        modelScale <= 0.0f)
    {
        return -1;
    }

    const float dx = parms.origin[0] - modelOrigin[0];
    const float dy = parms.origin[1] - modelOrigin[1];
    const float dz = parms.origin[2] - modelOrigin[2];
    const float baseDistance = std::sqrt(dx * dx + dy * dy + dz * dz) /
        modelScale;
    const WebRendererLodRamp &ramp = parms.ramp[model->lodRampType];
    const float adjustedDistance = baseDistance * ramp.scale + ramp.bias;
    if (!std::isfinite(adjustedDistance))
        return -1;
    return XModelGetLodForDist(model, adjustedDistance);
}

inline int WebRenderer_SelectStaticModelLod(
    const XModel *model,
    const float modelOrigin[3],
    float modelScale,
    float cullDistance,
    const WebRendererLodParms &parms) noexcept
{
    if (!model || !modelOrigin || !parms.valid || model->numLods <= 0 ||
        model->numLods > MAX_LODS ||
        !std::isfinite(modelOrigin[0]) || !std::isfinite(modelOrigin[1]) ||
        !std::isfinite(modelOrigin[2]) || !std::isfinite(modelScale) ||
        modelScale <= 0.0f || !std::isfinite(cullDistance))
    {
        return -1;
    }

    // R_AddAllStaticModelSurfaces applies the rigid ramp before placement
    // scale, and performs the authored smodel cull-distance check first.
    const float dx = parms.origin[0] - modelOrigin[0];
    const float dy = parms.origin[1] - modelOrigin[1];
    const float dz = parms.origin[2] - modelOrigin[2];
    const float originDistance =
        std::sqrt(dx * dx + dy * dy + dz * dz) * parms.ramp[0].scale +
        parms.ramp[0].bias;
    if (!std::isfinite(originDistance) || originDistance >= cullDistance)
        return -1;
    return XModelGetLodForDist(model, originDistance / modelScale);
}
