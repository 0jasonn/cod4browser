#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include <server/sv_map_command.h>

#include <game/save_error.h>
#include <qcommon/cmd.h>
#include <qcommon/engine_lifecycle_trace.h>
#include <qcommon/qcommon.h>
#include <qcommon/system.h>
#include <universal/com_files.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>

#include <cstring>
#include <climits>

extern const dvar_t *sv_cheats;

void __cdecl CL_ShutdownDemo();
void __cdecl SV_SpawnServer(const char *mapname, int savegame);

unsigned int SV_GetMapRandomSeed()
{
    // Shared developer control for repeatable fresh-map profiling. Save/demo
    // restoration still replaces this seed through the canonical game path.
    const dvar_t *const seed = Dvar_RegisterInt("sv_mapSeed", -1, -1, INT_MAX,
        DVAR_CHEAT, "Fresh-map random seed; -1 uses the system clock");
    return seed->current.integer < 0 ? Sys_MillisecondsRaw()
        : static_cast<unsigned int>(seed->current.integer);
}

void SV_Map_f()
{
    char *hasSVG;
    bool savegame;
    char mapname[64];
    char filename[72];

    EmitEngineLifecycleTrace(EngineLifecycleStage::MapCommandAccepted);
    com_errorPrintsCount = 0;
    SV_Cmd_ArgvBuffer(1, mapname, sizeof(mapname));
    I_strlwr(mapname);
    hasSVG = std::strstr(mapname, ".svg");
    savegame = hasSVG != nullptr;

    if (hasSVG)
    {
#if defined(KISAK_RUNTIME_MAP_DB_BOUNDARY)
        Com_Error(
            ERR_DROP,
            "Savegame map commands are not available before the save-system runtime is linked");
#else
        I_strncpyz(filename, mapname, 64);
        if (!static_cast<unsigned char>(
                ExtractMapStringFromSaveGame(filename, mapname)))
        {
            G_SaveError(
                ERR_DROP,
                SAVE_ERROR_MISSING_DEVICE,
                "Unable to extract map string name from save");
        }
#endif
    }
    Dvar_SetBool(sv_cheats, true);
    CL_ShutdownDemo();
    FS_ConvertPath(mapname);
    EmitEngineLifecycleTrace(
        EngineLifecycleStage::MapNameSelected, mapname);
    EmitEngineLifecycleTrace(
        EngineLifecycleStage::MapSpawnBegin, mapname);
    EmitEngineLifecycleTrace(
        EngineLifecycleStage::MapLoadingBegin, mapname);
    SV_SpawnServer(mapname, savegame);
#if !defined(KISAK_RUNTIME_MAP_DB_BOUNDARY)
    ShowLoadErrorsSummary(mapname, com_errorPrintsCount);
#endif
}

namespace
{
cmd_function_s s_spMapClientCommand;
cmd_function_s s_spMapServerCommand;
cmd_function_s s_mapClientCommand;
cmd_function_s s_mapServerCommand;
cmd_function_s s_devMapClientCommand;
cmd_function_s s_devMapServerCommand;
cmd_function_s s_spDevMapClientCommand;
cmd_function_s s_spDevMapServerCommand;
} // namespace

void SV_RegisterMapCommands()
{
    Cmd_AddCommandInternal("spmap", Cbuf_AddServerText_f, &s_spMapClientCommand);
    Cmd_AddServerCommandInternal("spmap", SV_Map_f, &s_spMapServerCommand);
    Cmd_AddCommandInternal("map", Cbuf_AddServerText_f, &s_mapClientCommand);
    Cmd_AddServerCommandInternal("map", SV_Map_f, &s_mapServerCommand);
    Cmd_AddCommandInternal("devmap", Cbuf_AddServerText_f, &s_devMapClientCommand);
    Cmd_AddServerCommandInternal("devmap", SV_Map_f, &s_devMapServerCommand);
    Cmd_AddCommandInternal(
        "spdevmap", Cbuf_AddServerText_f, &s_spDevMapClientCommand);
    Cmd_AddServerCommandInternal(
        "spdevmap", SV_Map_f, &s_spDevMapServerCommand);
    Cmd_SetAutoComplete("map", "maps", "d3dbsp");
    Cmd_SetAutoComplete("spmap", "maps", "d3dbsp");
    Cmd_SetAutoComplete("devmap", "maps", "d3dbsp");
    Cmd_SetAutoComplete("spdevmap", "maps", "d3dbsp");
}
