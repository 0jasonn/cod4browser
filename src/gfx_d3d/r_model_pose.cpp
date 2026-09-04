#include <universal/q_shared.h>
#include "r_model_pose.h"
#include "r_model_pose_bounds.h"
#include <xanim/dobj_utils.h>
#include "r_dobj_skin.h"
#include <universal/profile.h>
#include "r_dpvs.h"

#ifdef KISAK_MP
#include <cgame_mp/cg_local_mp.h>
#elif KISAK_SP
#include <cgame/cg_pose.h>
#elif defined(KISAK_RADIANT)
#include <cgame/cg_pose.h>
#endif

// LWSS: this function basically determines the visibility (dormancy) of Entities in the worldspace. Bodies will disappear in the edges of your FOV if you fk it up. Mounted machine guns as well. Edit with care I reverted this file lol
DObjAnimMat *R_UpdateSceneEntBounds(
    GfxSceneEntity *sceneEnt,
    GfxSceneEntity **pLocalSceneEnt,
    const DObj_s **pObj,
    int waitForCullState)
{
    DObjAnimMat *mat; // [esp+E8h] [ebp-26Ch]
    int boneIndex; // [esp+ECh] [ebp-268h]
    uint32_t animPartBit; // [esp+F0h] [ebp-264h]
    int boneCount; // [esp+F4h] [ebp-260h]
    XBoneInfo *boneInfoArray[128]; // [esp+F8h] [ebp-25Ch] BYREF
    float4 minWorld; // [esp+300h] [ebp-54h]
    float4 maxWorld; // [esp+310h] [ebp-44h] BYREF
    DObjAnimMat *boneMatrix; // [esp+320h] [ebp-34h]
    int surfCount; // [esp+324h] [ebp-30h]
    int partBits[4]; // [esp+328h] [ebp-2Ch] BYREF
    const DObj_s *obj; // [esp+338h] [ebp-1Ch]
    GfxSceneEntity *localSceneEnt; // [esp+33Ch] [ebp-18h]
    uint32_t state; // [esp+340h] [ebp-14h]

    if (InterlockedCompareExchange((volatile LONG *)&sceneEnt->cull, 1, 0))
    {
        *pLocalSceneEnt = 0;
        if (waitForCullState)
        {
            do
            {
                state = sceneEnt->cull.state;
                iassert(state >= CULL_STATE_BOUNDED_PENDING);
            } while (state == CULL_STATE_BOUNDED_PENDING);
            if (state == CULL_STATE_DONE)
            {
                return 0;
            }
            else
            {
                localSceneEnt = sceneEnt;
                *pLocalSceneEnt = sceneEnt;
                obj = localSceneEnt->obj;
                *pObj = obj;
                iassert(obj);
                return I_dmaGetDObjSkel(obj);
            }
        }
        else
        {
            return 0;
        }
    }
    else
    {
        localSceneEnt = sceneEnt;
        *pLocalSceneEnt = sceneEnt;
        iassert(localSceneEnt->obj);
        obj = localSceneEnt->obj;
        *pObj = obj;
        iassert(obj);
        DObjGetSurfaceData(
            obj,
            localSceneEnt->placement.base.origin,
            localSceneEnt->placement.scale,
            localSceneEnt->cull.lods);
        if (useFastFile->current.enabled || !DObjBad(obj))
        {
            surfCount = DObjGetSurfaces(obj, partBits, localSceneEnt->cull.lods);
            if (surfCount && (boneMatrix = R_DObjCalcPose(localSceneEnt, obj, partBits)) != 0)
            {
                iassert(DObjSkelAreBonesUpToDate(obj, partBits));

                minWorld.v[0] = 131072.0;
                minWorld.v[1] = 131072.0;
                minWorld.v[2] = 131072.0;
                minWorld.v[3] = 0.0;

                maxWorld.v[0] = -131072.0;
                maxWorld.v[1] = -131072.0;
                maxWorld.v[2] = -131072.0;
                maxWorld.v[3] = 0.0;

                DObjGetBoneInfo(obj, boneInfoArray);
                boneCount = DObjNumBones(obj);
                animPartBit = 0x80000000;
                boneIndex = 0;

                while (boneIndex < boneCount)
                {
                    if ((animPartBit & partBits[boneIndex >> 5]) != 0)
                    {
                        mat = &boneMatrix[boneIndex];
                        iassert(!IS_NAN((mat->quat)[0]) && !IS_NAN((mat->quat)[1]) &&
                            !IS_NAN((mat->quat)[2]) && !IS_NAN((mat->quat)[3]));
                        iassert(!IS_NAN(mat->transWeight));
                        kisak::model_pose::AccumulateBoneBounds(*mat,
                            *boneInfoArray[boneIndex], scene.def.viewOffset,
                            minWorld.v, maxWorld.v);
                    }
                    ++boneIndex;
                    animPartBit = (animPartBit << 31) | (animPartBit >> 1);
                }

                localSceneEnt->cull.mins[0] = minWorld.v[0];
                localSceneEnt->cull.mins[1] = minWorld.v[1];
                localSceneEnt->cull.mins[2] = minWorld.v[2];

                localSceneEnt->cull.maxs[0] = maxWorld.v[0];
                localSceneEnt->cull.maxs[1] = maxWorld.v[1];
                localSceneEnt->cull.maxs[2] = maxWorld.v[2];

                iassert(localSceneEnt->cull.state == CULL_STATE_BOUNDED_PENDING);

                localSceneEnt->cull.state = CULL_STATE_BOUNDED;
                return boneMatrix;
            }
            else
            {
                R_SetNoDraw(sceneEnt);
                return 0;
            }
        }
        else
        {
            R_SetNoDraw(sceneEnt);
            return 0;
        }
    }
}

DObjAnimMat *__cdecl R_DObjCalcPose(const GfxSceneEntity *sceneEnt, const DObj_s *obj, int *partBits)
{
    DObjAnimMat *boneMatrix;
    int completePartBits[4];

    iassert(sceneEnt);
    iassert(obj);

    completePartBits[0] = partBits[0];
    completePartBits[1] = partBits[1];
    completePartBits[2] = partBits[2];
    completePartBits[3] = partBits[3];

    DObjLock((DObj_s*)obj);
    {
        PROF_SCOPED("R_DObjCalcPose");
        boneMatrix = CG_DObjCalcPose(sceneEnt->info.pose, obj, completePartBits);
    }
    DObjUnlock((DObj_s *)obj);

    return boneMatrix;
}

void __cdecl R_SetNoDraw(GfxSceneEntity *sceneEnt)
{
    if (sceneEnt->cull.state != 1)
        MyAssertHandler(
            ".\\r_model_pose.cpp",
            68,
            0,
            "%s\n\t(sceneEnt->cull.state) = %i",
            "(sceneEnt->cull.state == CULL_STATE_BOUNDED_PENDING)",
            sceneEnt->cull.state);
    sceneEnt->cull.state = 4;
}

void __cdecl R_UpdateGfxEntityBoundsCmd(GfxSceneEntity **data)
{
    const DObj_s *obj; // [esp+0h] [ebp-10h] BYREF
    GfxSceneEntity *localSceneEnt; // [esp+4h] [ebp-Ch] BYREF
    GfxSceneEntity *sceneEnt; // [esp+8h] [ebp-8h]
    GfxSceneEntity **pSceneEnt; // [esp+Ch] [ebp-4h]

    iassert( data );
    pSceneEnt = data;
    sceneEnt = *data;
    if (R_UpdateSceneEntBounds(sceneEnt, &localSceneEnt, &obj, 0))
    {
        iassert( localSceneEnt );
    }
}

