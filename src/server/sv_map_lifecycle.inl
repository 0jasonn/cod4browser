// Canonical SP server initialization and SV_SpawnServer prefix used while the
// browser target converges through its first map database traversal. This file
// must stop at the real post-DB boundary and must not acquire browser state.

#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include <universal/q_shared.h>
#include <server/server.h>
#include <server/sv_public.h>
#include <server/sv_map_assets.h>
#include <server/sv_map_command.h>
#include <client/client.h>
#include <database/database.h>
#include <game/savememory.h>
#include <server/sv_game.h>
#include <qcommon/qcommon.h>
#include <qcommon/com_world_runtime.h>
#include <qcommon/threads.h>
#include <universal/dvar.h>
#include <universal/com_files.h>
#if defined(KISAK_WEB)
#include <database/db_registry_publication.h>
#endif

#include <cstring>

void __cdecl R_BeginRemoteScreenUpdate();
void __cdecl R_EndRemoteScreenUpdate();

const dvar_t *sv_clientFrameRateFix;
const dvar_t *sv_loadMyChanges;

client_t g_sv_clients[1];

void __cdecl SV_GetConfigstring(
    unsigned int index, char *buffer, int bufferSize)
{
    if (bufferSize < 1)
        Com_Error(ERR_DROP, "SV_GetConfigstring: bufferSize == %i", bufferSize);
    if (index >= ARRAY_COUNT(sv.configstrings))
        Com_Error(ERR_DROP, "SV_GetConfigstring: bad index %i", index);
    iassert(sv.configstrings[index]);
    I_strncpyz(buffer, SL_ConvertToString(sv.configstrings[index]), bufferSize);
}

unsigned int __cdecl SV_GetConfigstringConst(unsigned int index)
{
    iassert(index < MAX_CONFIGSTRINGS);
    iassert(sv.configstrings[index]);
    return sv.configstrings[index];
}

void __cdecl SV_InitReliableCommandsForClient(client_t *cl)
{
    Com_Memset(&cl->reliableCommands.header, 0,
        sizeof(serverCommandsHeader_t));
}

void __cdecl SV_AddReliableCommand(client_t *cl, int index, const char *cmd)
{
    const int length = static_cast<int>(std::strlen(cmd));
    if (length + 1 + cl->reliableCommands.header.rover > 0x2000)
    {
        SV_DumpServerCommands(cl);
        Com_Error(ERR_DROP, "Reliable command buffer overflow");
    }
    cl->reliableCommands.commands[index] =
        cl->reliableCommands.header.rover;
    char *destination =
        &cl->reliableCommands.buf[cl->reliableCommands.header.rover];
    for (int offset = 0; offset < length; ++offset)
        destination[offset] = cmd[offset] == '%' ? '.' : cmd[offset];
    destination[length] = '\0';
    cl->reliableCommands.header.rover += length + 1;
}

bool __cdecl SV_Loaded()
{
    return sv.state == SS_GAME;
}

void __cdecl SV_SetConfigstring(unsigned int index, const char *value)
{
    if (index >= MAX_CONFIGSTRINGS)
        Com_Error(ERR_DROP, "SV_SetConfigstring: bad index %i", index);
    if (!sv.configstrings[index])
    {
        iassert(!value);
        return;
    }
    if (!value) value = "";
    if (!std::strcmp(SL_ConvertToString(sv.configstrings[index]), value))
        return;

    SL_RemoveRefToString(sv.configstrings[index]);
    sv.configstrings[index] = index < 1114
        ? SL_GetString_(value, 0, MT_TYPE_CONFIG_STRING)
        : SL_GetLowercaseString_(value, 0, MT_TYPE_CONFIG_STRING);
    if (sv.state != SS_GAME || !svs.clients || svs.clients->state != 1)
        return;

    const int length = static_cast<int>(std::strlen(value));
    if (length < 1000)
    {
        SV_SendServerCommand(svs.clients, "cs %i %s", index, value);
        return;
    }
    for (int offset = 0; offset < length; offset += 999)
    {
        char segment[1120]{};
        I_strncpyz(segment, value + offset, 1000);
        const char *opcode = offset == 0 ? "bcs0"
            : (length - offset >= 1000 ? "bcs1" : "bcs2");
        SV_SendServerCommand(svs.clients, "%s %i %s", opcode, index,
            segment);
    }
}

void __cdecl SV_Startup()
{
    if (svs.initialized)
        Com_Error(ERR_FATAL, "SV_Startup() - already initialized");
    svs.clients = g_sv_clients;
    svs.numSnapshotEntities = MAX_GENTITIES;
    svs.initialized = 1;
    Dvar_SetBool(com_sv_running, true);
}

void __cdecl SV_ClearServer()
{
    if (svs.clients)
        Com_Memset(&svs.clients->reliableCommands, 0, 12);
    for (unsigned short *configstring = sv.configstrings;
         configstring < &sv.svEntities[0].worldSector;
         ++configstring)
    {
        if (*configstring)
            SL_RemoveRefToString(*configstring);
    }
    if (sv.emptyConfigString)
        SL_RemoveRefToString(sv.emptyConfigString);
    Com_Memset(&sv, 0, sizeof(server_t));
    com_inServerFrame = 0;
}

void __cdecl SV_StartMap(int randomSeed)
{
    (void)randomSeed;
    com_inServerFrame = 0;
    sv.state = SS_LOADING;
    com_time = 0;
    sv.skelTimeStamp = 0;
    Dvar_SetInt(cl_paused, 1);
}

void __cdecl SV_Settle()
{
    for (int frame = 0; frame < 5; ++frame)
    {
        sv.demo.forwardMsec -= 50;
        if (sv.demo.forwardMsec < 0)
            sv.demo.forwardMsec = 0;
        SV_PreFrame();
        SV_RunFrame(SV_FRAME_DO_ALL, 0);
    }
}

void __cdecl SV_Init()
{
    SV_RegisterMapCommands();
    sv_gameskill = Dvar_RegisterInt(
        "g_gameskill", 1, 0, 3, 0x64u, "Game skill level");
    sv_player_maxhealth = Dvar_RegisterInt(
        "g_player_maxhealth", 100, 10, 2000, 2u,
        "Maximum player health");
    sv_player_damageMultiplier = Dvar_RegisterFloat(
        "player_damageMultiplier", 1.0f, 0.0f, 1000.0f, 0, nullptr);
    player_healthEasy = Dvar_RegisterInt(
        "player_healthEasy", 500, 10, 2000, 2u,
        "Player health on easy mode");
    player_healthMedium = Dvar_RegisterInt(
        "player_healthMedium", 275, 10, 2000, 2u,
        "Player health in medium mode");
    player_healthHard = Dvar_RegisterInt(
        "player_healthHard", 165, 10, 2000, 2u,
        "Player health in challenging mode");
    player_healthFu = Dvar_RegisterInt(
        "player_healthFu", 115, 10, 2000, 2u,
        "Player health in veteran mode");
    sv_player_deathInvulnerableTime = Dvar_RegisterInt(
        "player_deathInvulnerableTime", 1000, 0, 0x7FFFFFFF,
        0x1082u,
        "Time player is invulnerable just before death");
    sv_mapname = Dvar_RegisterString(
        "mapname", "", 0x44u, "current map name");
    sv_lastSaveGame = Dvar_RegisterString(
        "sv_lastSaveGame", "", DVAR_ARCHIVE,
        "Last save game file name");
    sv_saveOnStartMap = Dvar_RegisterBool(
        "sv_saveOnStartMap", false, 0x1004u,
        "Save at the start of a level");
    sv_cheats = Dvar_RegisterBool(
        "sv_cheats", true, 0x48u, "Enable server cheats");
    replay_autosave = Dvar_RegisterInt(
        "replay_autosave", 30, 0, 0x7FFFFFFF, 0,
        "Use autosaves as part of demos");
    replay_asserts = Dvar_RegisterBool(
        "replay_asserts", true, 0,
        "Enable/Disable replay aborts due to inconsistency");
    nextmap = Dvar_RegisterString("nextmap", "", 0, "Next map to load");
    Dvar_RegisterInt(
        "g_reloading", 0, 0, 4, DVAR_ROM,
        "True if the game is currently reloading");
    sv_smp = Dvar_RegisterBool(
        "sv_smp", false, 0,
        "Enable server multithreading");
    sv_loadMyChanges = Dvar_RegisterBool(
        "sv_loadMyChanges", false, 0,
        "Load my changes fast file on devmap.");
    sv_clientFrameRateFix = Dvar_RegisterBool(
        "sv_clientFrameRateFix", true, 0x1004u,
        "Slow down server frame time to allow good client frame rate with server bound.");
}

void __cdecl SV_SpawnServer(const char *mapname, int savegame)
{
    Com_SyncThreads();
    (void)Sys_Milliseconds();
    Sys_BeginLoadThreadPriorities();
    DB_ResetZoneSize(0);
    CL_InitLoad(mapname);
    CL_MapLoading(mapname);
    R_BeginRemoteScreenUpdate();
    iassert(sv_gameskill);
    R_EndRemoteScreenUpdate();
    R_BeginRemoteScreenUpdate();
    R_EndRemoteScreenUpdate();
    CL_ShutdownAll(false);
    SaveMemory_CleanupSaveMemory();
    SaveMemory_ShutdownSaveSystem();
    Com_Printf(15, "------ Server Initialization ------\n");
    Com_Printf(15, "Server: %s\n", mapname);
    SV_ClearServer();
    const std::uint32_t seed = Sys_MillisecondsRaw();
    Com_Restart();
    if (!com_sv_running->current.enabled)
        SV_Startup();
    CL_StartLoading(mapname);
    if (mapname[0])
        SV_LoadLevelAssets(mapname);

    if (DB_HasXFileLoadFailure())
        return;

    // Continue in native order only after the complete map fastfile has
    // published. UI/load-screen presentation is platform-gated; server state,
    // config-string ownership, BSP-name construction, and collision loading
    // remain canonical engine work.
    R_BeginRemoteScreenUpdate();
    svs.nextSnapshotEntities = 0;
    Dvar_SetString(nextmap, "map_restart");
    iassert(!std::strstr(mapname, "\\"));
    Dvar_SetString(sv_mapname, mapname);

    sv.emptyConfigString = SL_GetString_("", 0, MT_TYPE_CONFIG_STRING);
    for (int index = 0; index < MAX_CONFIGSTRINGS; ++index)
    {
        iassert(!sv.configstrings[index]);
        sv.configstrings[index] = SL_GetString_("", 0, MT_TYPE_CONFIG_STRING);
    }

    char filename[160]{};
    Com_GetBspFilename(filename, sizeof(filename), mapname);
    CM_LoadMap(filename, &sv.checksum);
    Com_LoadWorld(filename);

    // Fastfile SP does not enter the loose-BSP sound-alias path here. The
    // canonical save-memory owner is the final ordinary pre-game step.
    SaveMemory_InitializeSaveSystem();
    SaveMemory_ClearDemoSave();

    SaveGame *save = nullptr;
    SV_InitGameProgs(seed, savegame, &save);
#if defined(KISAK_WEB)
    DB_DiagnosePublishedSoundCurves("spawn-server-after-game");
#endif
}
