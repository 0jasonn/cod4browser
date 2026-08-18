#pragma once

struct SaveGame;
struct playerState_s;
struct ComPrimaryLight;
struct dvar_s;

extern const dvar_s *replay_time;

float __cdecl CG_GetViewZoomScale();
const ComPrimaryLight *__cdecl Com_GetPrimaryLight(unsigned int primaryLightIndex);
void __cdecl CG_SetDebugOrigin(float *origin);
void __cdecl CG_SetDebugAngles(const float *angles);
void __cdecl CG_SaveEntities(SaveGame *save);
void __cdecl CG_LoadEntities(SaveGame *save);
void CG_SaveViewModelAnimTrees(SaveGame *save);
void CG_LoadViewModelAnimTrees(SaveGame *save, const playerState_s *ps);
void __cdecl CG_FreeWeapons(int localClientNum);
void __cdecl CG_SetServerCommandSequence(int reliableSent);
