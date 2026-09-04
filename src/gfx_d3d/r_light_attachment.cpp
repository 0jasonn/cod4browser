#include <universal/q_shared.h>
#include "r_light.h"
#include <EffectsCore/fx_system.h>

// Shared native FX/DObj identity query; the renderer owns only its draw result.
bool __cdecl R_SpotLightIsAttachedToDobj(const DObj_s *obj)
{
    iassert(obj);
    FxSystem *system = FX_GetSystem(0);
    iassert(system);
    if (!system->activeSpotLightEffectCount)
        return false;
    if (system->activeSpotLightBoltDobj == -1)
        return false;
    DObj_s *attachedDobj = Com_GetClientDObj(system->activeSpotLightBoltDobj, 0);
    return attachedDobj && attachedDobj == obj;
}
