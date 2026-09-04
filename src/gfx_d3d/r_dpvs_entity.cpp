#include <universal/q_shared.h>
#include "r_dpvs.h"
#include "r_model_pose.h"
#include "r_dobj_skin.h"
#include <cgame/cg_local.h>



void __cdecl R_AddEntitySurfacesInFrustumCmd(uint16_t *data)
{
    int v1; // [esp+4h] [ebp-28h]
    const DpvsPlane *plane; // [esp+Ch] [ebp-20h]
    int itr; // [esp+10h] [ebp-1Ch]
    DObjAnimMat *boneMatrix; // [esp+14h] [ebp-18h]
    const DObj_s *obj; // [esp+1Ch] [ebp-10h] BYREF
    GfxSceneEntity *localSceneEnt; // [esp+20h] [ebp-Ch] BYREF
    const DpvsPlane *planes; // [esp+24h] [ebp-8h]
    GfxSceneEntity *sceneEnt; // [esp+28h] [ebp-4h]

    sceneEnt = *(GfxSceneEntity **)data;
    boneMatrix = R_UpdateSceneEntBounds(sceneEnt, &localSceneEnt, &obj, 1);
    if (boneMatrix)
    {
        iassert( localSceneEnt );
        planes = (const DpvsPlane *)*((uint32_t *)data + 1);
        itr = 0;
        plane = planes;
        while (itr < data[4])
        {
            //if (*(float *)((char *)localSceneEnt->cull.mins + v2->side[0]) * v2->coeffs[0]
            //    + v2->coeffs[3]
            //    + *(float *)((char *)localSceneEnt->cull.mins + v2->side[1]) * v2->coeffs[1]
            //    + *(float *)((char *)localSceneEnt->cull.mins + v2->side[2]) * v2->coeffs[2] <= 0.0)
            if (R_DpvsPlaneMaxSignedDistToBox(plane, localSceneEnt->cull.mins) <= 0.0)
            {
                v1 = 1;
                goto LABEL_13;
            }
            ++itr;
            ++plane;
        }
        v1 = 0;
    LABEL_13:
        if (!v1
            && R_BoundsInCell(
                (mnode_t *)rgp.world->dpvsPlanes.nodes,
                data[5],
                localSceneEnt->cull.mins,
                localSceneEnt->cull.maxs))
        {
#ifndef KISAK_RADIANT
            CG_CullIn(localSceneEnt->info.pose);
#endif
            R_SkinSceneDObj(sceneEnt, localSceneEnt, obj, boneMatrix, 0);
            iassert( localSceneEnt->entnum != gfxCfg.entnumNone );
            *(_BYTE *)(localSceneEnt->entnum + *((uint32_t *)data + 3)) = 1;
        }
        else
        {
#ifndef KISAK_RADIANT
            CG_UsedDObjCalcPose(localSceneEnt->info.pose);
#endif
        }
    }
    else if (localSceneEnt)
    {
#ifndef KISAK_RADIANT
        CG_UsedDObjCalcPose(localSceneEnt->info.pose);
#endif
    }
}

bool __cdecl R_BoundsInCell(mnode_t *node, int findCellIndex, const float *mins, const float *maxs)
{
    bool inside = false;
    if (node != reinterpret_cast<mnode_t *>(rgp.world->dpvsPlanes.nodes) ||
        !R_QueryBoundsInCell(*rgp.world, findCellIndex, mins, maxs, inside))
        Com_Error(ERR_DROP, "R_BoundsInCell: invalid canonical BSP or bounds");
    return inside;
}
