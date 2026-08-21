#include <web/web_renderer_dobj_scene.h>

#include <universal/q_shared.h>
#include <xanim/xmodel.h>

#include <cmath>

namespace
{
bool Finite3(const float value[3]) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) &&
        std::isfinite(value[2]);
}
} // namespace

int WebRenderer_SelectDObjLod(
    const XModel *model, const float poseOrigin[3],
    const float viewOrigin[3]) noexcept
{
    // R_GetBaseLodDist is owned by native renderer state and is not available
    // at this portable frontend seam. The canonical XModel thresholds remain
    // authoritative once the active view and pose origins are supplied.
    if (!model || !poseOrigin || !viewOrigin || !Finite3(poseOrigin) ||
        !Finite3(viewOrigin) || model->numLods <= 0 ||
        model->numLods > MAX_LODS)
    {
        return 0;
    }
    const float dx = viewOrigin[0] - poseOrigin[0];
    const float dy = viewOrigin[1] - poseOrigin[1];
    const float dz = viewOrigin[2] - poseOrigin[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(distance)) return 0;
    const int lod = XModelGetLodForDist(model, distance);
    return lod >= 0 ? lod : static_cast<int>(model->numLods - 1u);
}
