#pragma once

#include <gfx_d3d/gfx_light_types.h>
#include <gfx_d3d/gfx_draw_surf_types.h>
#include <algorithm>
#include <cmath>

// Shared native scene-light construction and selection. GfxLight remains the
// canonical frame payload; the platform backend owns only its GPU resources.
namespace kisak::dynamic_lights
{
// Non-BSP light passes inherit material fields, not camera light/probe state.
// Object IDs remain native draw-buffer coordinates supplied by each caller.
inline GfxDrawSurf ReceiverDrawSurf(GfxDrawSurf material,
    unsigned surfType, unsigned objectId, bool depthHack = false) noexcept
{
    material.fields.objectId = objectId;
    material.fields.surfType = surfType;
    material.fields.primarySortKey -= depthHack;
    return material;
}

// R_ReverseSortDrawSurfs compares the canonical packed key after complementing
// only primarySortKey. This makes larger material sort bands draw first while
// preserving the native surf-type/material/object ordering inside each band.
inline constexpr std::uint64_t ReceiverDrawSortKey(
    std::uint64_t packed) noexcept
{
    constexpr std::uint64_t primarySortMask = 0x3full << 54u;
    return (packed & ~primarySortMask) |
        (((~(packed >> 54u)) & 0x3full) << 54u);
}

struct ScissorRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Native R_BoxInPlanes uses a strict negative corner test. A box touching
// a plane from outside is rejected, unlike the sphere receiver predicate.
inline bool BoxInPlanes(const float planes[6][4], const float mins[3], const float maxs[3])
{
    for (unsigned p = 0; p < 6; ++p)
    {
        const float *plane = planes[p];
        bool inside = false;
        for (unsigned corner = 0; corner < 8; ++corner)
        {
            const float x = corner & 4 ? maxs[0] : mins[0];
            const float y = corner & 2 ? maxs[1] : mins[1];
            const float z = corner & 1 ? maxs[2] : mins[2];
            inside |= plane[3] + z * plane[2] + y * plane[1] + x * plane[0] < 0.0f;
        }
        if (!inside) return false;
    }
    return true;
}

inline bool BoxInSphere(const float origin[3], float radiusSquared,
    const float mins[3], const float maxs[3])
{
    float distanceSquared = 0.0f;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const float delta = mins[axis] - origin[axis];
        if (delta <= 0.0f)
        {
            const float other = origin[axis] - maxs[axis];
            if (other > 0.0f) distanceSquared = other * other + distanceSquared;
        }
        else distanceSquared = delta * delta + distanceSquared;
    }
    return radiusSquared >= distanceSquared;
}

inline bool SphereInPlanes(const float planes[6][4], const float center[3], float radius)
{
    for (unsigned p = 0; p < 6; ++p)
        if (planes[p][0] * center[0] + planes[p][1] * center[1] +
            planes[p][2] * center[2] + planes[p][3] - radius > 0.0f) return false;
    return true;
}

inline bool SpheresIntersect(const float a[3], float radiusA, const float b[3], float radiusB)
{
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    const float radius = radiusA + radiusB;
    const float distanceSquared = x * x + y * y + z * z;
    return !(distanceSquared > radius * radius);
}

inline void SpotCrossDirs(const GfxLight &light, float crossDirs[2][3])
{
    unsigned bestAxis = 0;
    for (unsigned axis = 1; axis < 3; ++axis)
        if (std::fabs(light.dir[bestAxis]) > std::fabs(light.dir[axis])) bestAxis = axis;
    const auto crossNormalize = [](const float a[3], const float b[3], float out[3]) {
        out[0] = a[1] * b[2] - a[2] * b[1];
        out[1] = a[2] * b[0] - a[0] * b[2];
        out[2] = a[0] * b[1] - a[1] * b[0];
        const float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
        const float scale = length > 0.0f ? 1.0f / length : 1.0f;
        for (unsigned axis = 0; axis < 3; ++axis) out[axis] *= scale;
    };
    std::fill_n(crossDirs[0], 3, 0.0f);
    crossDirs[0][bestAxis] = 1.0f;
    crossNormalize(light.dir, crossDirs[0], crossDirs[1]);
    crossNormalize(crossDirs[1], light.dir, crossDirs[0]);
}

inline void SpotPlanes(const GfxLight &light, float nearPlaneOffset, float planes[6][4])
{
    float crossDirs[2][3];
    SpotCrossDirs(light, crossDirs);
    const float sine = std::sqrt(1.0f - light.cosHalfFovOuter * light.cosHalfFovOuter);
    for (unsigned p = 0; p < 6; ++p)
    {
        float origin[3];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            origin[axis] = light.origin[axis];
            if (p == 0 || p == 5)
            {
                planes[p][axis] = p == 0 ? light.dir[axis] : -light.dir[axis];
                origin[axis] += (p == 0 ? nearPlaneOffset : light.radius) * -light.dir[axis];
            }
            else
                planes[p][axis] = (p <= 2 ? light.cosHalfFovOuter : -light.cosHalfFovOuter) *
                    crossDirs[(p - 1) % 2][axis] + -sine * -light.dir[axis];
        }
        planes[p][3] = -(planes[p][0] * origin[0] + planes[p][1] * origin[1] + planes[p][2] * origin[2]);
    }
}

inline bool ReceiverPlanes(const GfxLight &light, float nearPlaneOffset, float planes[6][4])
{
    if ((light.type != 2 && light.type != 3) || !std::isfinite(light.radius) || light.radius <= 0.0f)
        return false;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!std::isfinite(light.origin[axis])) return false;
    if (light.type == 2)
    {
        if (!std::isfinite(nearPlaneOffset) || nearPlaneOffset < 0.0f ||
            !std::isfinite(light.cosHalfFovOuter) || light.cosHalfFovOuter < 0.0f ||
            light.cosHalfFovOuter > 1.0f) return false;
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!std::isfinite(light.dir[axis])) return false;
        SpotPlanes(light, nearPlaneOffset, planes);
    }
    return true;
}

inline void SetOmni(GfxLight &light, GfxLightDef *definition,
    const float origin[3], float radius, const float color[3])
{
    light = {};
    light.def = definition;
    light.type = 3;
    std::copy_n(origin, 3, light.origin);
    std::copy_n(color, 3, light.color);
    light.radius = radius;
    light.spotShadowIndex = UINT32_MAX;
}

inline float SetSpot(GfxLight &light, GfxLightDef *definition,
    const float origin[3], const float direction[3], float radius,
    const float color[3], float startRadius, float endRadius,
    float innerFraction, float brightness, bool shadows)
{
    const float outer = std::atan((endRadius - startRadius) / radius);
    const float inner = outer * innerFraction;
    const float offset = startRadius / std::tan(outer);
    SetOmni(light, definition, origin, radius + offset, color);
    light.type = 2;
    for (int axis = 0; axis < 3; ++axis)
    {
        light.dir[axis] = -direction[axis];
        light.origin[axis] += offset * light.dir[axis];
        light.color[axis] *= brightness;
    }
    light.exponent = 1;
    light.cosHalfFovInner = std::cos(inner);
    light.cosHalfFovOuter = std::cos(outer);
    light.canUseShadowMap = shadows;
    return offset;
}

inline bool ImportanceGreaterEqual(const GfxLight *a, const GfxLight *b,
    const float viewOrigin[3])
{
    if (a->type != b->type) return a->type == 2;
    float distanceA = 0, distanceB = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float da = viewOrigin[axis] - a->origin[axis];
        const float db = viewOrigin[axis] - b->origin[axis];
        distanceA += da * da;
        distanceB += db * db;
    }
    return b->radius * b->radius * distanceA <= a->radius * a->radius * distanceB;
}

// Preserve native partition order, including ties; sorting all lights would
// change the order of additive material passes unnecessarily.
inline void MostImportant(const GfxLight **lights, int count, int keep,
    const float viewOrigin[3])
{
    if (keep <= 0 || count <= keep) return;
    for (;;)
    {
        int bottom = 0, top = count;
        const GfxLight *pivot = lights[0];
        for (;;)
        {
            do { ++bottom; }
            while (bottom < top && ImportanceGreaterEqual(lights[bottom], pivot, viewOrigin));
            if (bottom > top) break;
            do { --top; }
            while (top >= bottom && ImportanceGreaterEqual(pivot, lights[top], viewOrigin));
            if (bottom > top) break;
            std::swap(lights[bottom], lights[top]);
        }
        if (bottom == count)
        {
            std::swap(lights[0], lights[top]);
            --bottom;
        }
        if (bottom == keep) return;
        if (bottom >= keep) count = bottom;
        else
        {
            lights += bottom;
            count -= bottom;
            keep -= bottom;
        }
    }
}

// Native R_DrawPointLitSurfs bounds each destination-alpha clear and light
// draw to the projected tangent rectangle of the light sphere. The matrix is
// the canonical row-major GfxViewParms view-projection matrix and y remains in
// the engine's top-left viewport convention.
inline bool ComputeScissorRect(const GfxLight &light,
    const float viewOrigin[3], const float viewAxis[3][3],
    const float viewProjection[4][4], int viewportX, int viewportY,
    int viewportWidth, int viewportHeight, ScissorRect &result) noexcept
{
    if (!(light.radius > 0.0f) || viewportWidth <= 0 || viewportHeight <= 0)
        return false;
    float offset[3]{};
    float offsetDistanceSq = 0.0f;
    for (int component = 0; component < 3; ++component)
    {
        if (!std::isfinite(light.origin[component]) ||
            !std::isfinite(viewOrigin[component]))
            return false;
        offset[component] = light.origin[component] - viewOrigin[component];
        offsetDistanceSq += offset[component] * offset[component];
    }
    float bounds[4]{1.0f, -1.0f, 1.0f, -1.0f};
    const float tangentDistanceSq =
        offsetDistanceSq - light.radius * light.radius;
    if (tangentDistanceSq > 1.0f)
    {
        const float tangentDistance = std::sqrt(tangentDistanceSq);
        const float perpendicularDistance = offsetDistanceSq / tangentDistance;
        float sign = 1.0f;
        for (int plane = 0; plane < 4; ++plane, sign = -sign)
        {
            const int screenAxis = plane >> 1;
            const float *perpendicular = viewAxis[2 - screenAxis];
            float tangent[3]{
                sign * (perpendicular[1] * offset[2] -
                    perpendicular[2] * offset[1]),
                sign * (perpendicular[2] * offset[0] -
                    perpendicular[0] * offset[2]),
                sign * (perpendicular[0] * offset[1] -
                    perpendicular[1] * offset[0])};
            const float tangentLength = std::sqrt(tangent[0] * tangent[0] +
                tangent[1] * tangent[1] + tangent[2] * tangent[2]);
            if (!(tangentLength > 0.000001f) ||
                !std::isfinite(tangentLength))
                continue;
            for (float &component : tangent) component /= tangentLength;
            float edgeDirection[3]{};
            float edgeLengthSq = 0.0f;
            for (int component = 0; component < 3; ++component)
            {
                edgeDirection[component] = viewOrigin[component] -
                    perpendicularDistance * tangent[component] -
                    light.origin[component];
                edgeLengthSq += edgeDirection[component] *
                    edgeDirection[component];
            }
            const float edgeLength = std::sqrt(edgeLengthSq);
            if (!(edgeLength > 0.000001f) || !std::isfinite(edgeLength))
                continue;
            float edgePoint[3]{};
            for (int component = 0; component < 3; ++component)
                edgePoint[component] = light.origin[component] +
                    light.radius * edgeDirection[component] / edgeLength;
            const float w = edgePoint[0] * viewProjection[0][3] +
                edgePoint[1] * viewProjection[1][3] +
                edgePoint[2] * viewProjection[2][3] +
                viewProjection[3][3];
            if (!(w > 0.0f) || !std::isfinite(w)) continue;
            const float projected =
                (edgePoint[0] * viewProjection[0][screenAxis] +
                 edgePoint[1] * viewProjection[1][screenAxis] +
                 edgePoint[2] * viewProjection[2][screenAxis] +
                 viewProjection[3][screenAxis]) / w;
            if (std::isfinite(projected))
                bounds[plane] = std::clamp(projected, -1.0f, 1.0f);
        }
    }
    const float halfWidth = static_cast<float>(viewportWidth >> 1);
    const float halfHeight = static_cast<float>(viewportHeight >> 1);
    result.x = viewportX + static_cast<int>(
        std::floor((bounds[1] + 1.0f) * halfWidth));
    result.y = viewportY + static_cast<int>(
        std::floor((1.0f - bounds[2]) * halfHeight));
    const int right = viewportX + static_cast<int>(
        std::ceil((bounds[0] + 1.0f) * halfWidth));
    const int bottom = viewportY + static_cast<int>(
        std::ceil((1.0f - bounds[3]) * halfHeight));
    result.width = right > result.x ? right - result.x : 0;
    result.height = bottom > result.y ? bottom - result.y : 0;
    return result.width > 0 && result.height > 0;
}
}
