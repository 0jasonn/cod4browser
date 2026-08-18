#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include <universal/q_shared.h>
#include <server/server.h>

#include <game/savememory.h>

server_t sv;
serverStatic_t svs;

int com_time;
int com_inServerFrame;

const dvar_t *sv_lastSaveGame;
const dvar_t *sv_smp;
const dvar_t *sv_player_damageMultiplier;
const dvar_t *sv_player_maxhealth;
const dvar_t *sv_saveOnStartMap;
const dvar_t *sv_gameskill;
const dvar_t *sv_mapname;
#ifdef KISAK_XBOX
const dvar_t *sv_saveDeviceAvailable;
#endif
const dvar_t *sv_cheats;
const dvar_t *player_healthEasy;
const dvar_t *player_healthHard;
const dvar_t *sv_player_deathInvulnerableTime;
const dvar_t *runForTime;
const dvar_t *sv_saveGameSuccess;
#ifdef KISAK_XBOX
const dvar_t *sv_saveGameAvailable;
const dvar_t *sv_saveGameNotReadable;
#endif
const dvar_t *replay_autosave;
const dvar_t *player_healthMedium;
const dvar_t *player_healthFu;
const dvar_t *replay_asserts;

PendingSaveList pendingSaveGlob;
