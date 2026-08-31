#include <universal/q_shared.h>
#include "r_dpvs.h"

DpvsContext R_NativeDpvsContext();

void __cdecl R_AddCellStaticSurfacesInFrustumCmd(DpvsStaticCellCmd *data)
{
    g_smodelVisData = rgp.world->dpvs.smodelVisData[data->viewIndex];
    g_surfaceVisData = rgp.world->dpvs.surfaceVisData[data->viewIndex];
    auto context = R_NativeDpvsContext();
    context.R_AddCellStaticSurfacesInFrustum(data);
    if (context.drawWorld)
        context.R_AddCellCullGroupsInFrustum(data);
}
