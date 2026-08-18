// Fastfile-owned ComWorld runtime split from com_bsp/com_bsp_load_obj while
// loose BSP parsing remains outside the browser target.

#include <qcommon/com_world_runtime.h>

#include <database/database.h>
#include <qcommon/com_world_types.h>
#include <qcommon/engine_lifecycle_trace.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <universal/q_shared.h>

ComWorld comWorld{};

void __cdecl Com_LoadWorld(char *name)
{
    if (!IsFastFileLoad())
    {
        Com_Error(
            ERR_DROP,
            "Loose BSP common-world loading is unavailable in the fastfile runtime");
        return;
    }
    Com_LoadWorld_FastFile(name);
}

void __cdecl Com_LoadWorld_FastFile(const char *name)
{
    EmitEngineLifecycleTrace(EngineLifecycleStage::CommonWorldLoadBegin, name);
    if (DB_FindXAssetHeader(ASSET_TYPE_COMWORLD, name).comWorld != &comWorld)
        MyAssertHandler(".\\qcommon\\com_bsp_load_obj.cpp", 640, 0, "%s", "asset == &comWorld");
    iassert(comWorld.isInUse);
    EmitEngineLifecycleTrace(EngineLifecycleStage::CommonWorldLoadComplete, name);
}

void __cdecl Com_ShutdownWorld()
{
    comWorld.isInUse = 0;
}

void __cdecl Com_UnloadWorld()
{
    iassert(IsFastFileLoad());
    if (comWorld.isInUse)
        Sys_Error("Cannot unload world while it is in use");
}
