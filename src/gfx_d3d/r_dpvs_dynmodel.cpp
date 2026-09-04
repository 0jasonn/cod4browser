#include <universal/q_shared.h>
#include "r_dpvs.h"
#include "r_dpvs_core.h"
#include "r_dvars.h"
#include <DynEntity/DynEntity_client.h>


void __cdecl R_AddCellDynModelSurfacesInFrustumCmd(const DpvsDynamicCellCmd *data)
{
    if (!r_drawDynEnts->current.enabled) return;
    const unsigned count = DynEnt_GetEntityCount(DYNENT_COLL_CLIENT_MODEL);
    if (count != rgp.world->dpvsDyn.dynEntClientCount[0] ||
        !R_CullDynEntityCell(*rgp.world, 0u, data->cellIndex,
            DynEnt_GetClientModelPoseList(), nullptr,
            data->planes, data->planeCount,
            rgp.world->dpvsDyn.dynEntVisData[0][data->viewIndex]))
        Com_Error(ERR_DROP, "R_AddCellDynModelSurfacesInFrustumCmd: invalid canonical cell data");
}
