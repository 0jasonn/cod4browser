#include <web/web_renderer_dobj_scene.h>

int WebRenderer_SelectDObjLod(
    const XModel *model, const float poseOrigin[3],
    const WebRendererLodParms *lodParms) noexcept
{
    return lodParms
        ? WebRenderer_SelectModelLod(model, poseOrigin, 1.0f, *lodParms)
        : -1;
}
