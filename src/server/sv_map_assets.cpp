#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include <server/sv_map_assets.h>

#include <database/db_zone_loading.h>
#include <qcommon/cmd.h>
#include <qcommon/engine_lifecycle_trace.h>
#include <universal/dvar.h>

extern const dvar_t *sv_loadMyChanges;
int __cdecl CL_ControllerIndexFromClientNum(int clientIndex);

void SV_LoadLevelAssets(const char *mapname)
{
    XZoneInfo zoneInfo;

    zoneInfo.name = mapname;
    // SP PC retains the shared UI/startup allocation class while replacing a
    // previous map. This is the native Kisak request, not a web policy.
    zoneInfo.allocFlags = 8;
    zoneInfo.freeFlags = 8;
    EmitEngineLifecycleTrace(
        EngineLifecycleStage::MapZoneRequestConstructed,
        mapname,
        1,
        zoneInfo.allocFlags,
        zoneInfo.freeFlags,
        0);
    DB_LoadXAssets(&zoneInfo, 1, 0);
    if (sv_loadMyChanges->current.enabled)
    {
        Cbuf_ExecuteBuffer(
            0, CL_ControllerIndexFromClientNum(0), "loadzone mychanges\n");
    }
}
