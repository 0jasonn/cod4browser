#pragma once

#include <cstddef>
#include <cstdint>

enum CinematicEnum : int;

// Renderer/frontend cinematic lifecycle used outside the native D3D backend.
bool R_Cinematic_IsNextReady();
bool R_Cinematic_IsPending();
bool R_Cinematic_IsStarted();
bool R_Cinematic_IsFinished();
bool R_Cinematic_GetPlaybackInfo(char *name, std::size_t nameSize,
    std::uint32_t *timeInMsec);
void __cdecl R_Cinematic_StartPlayback(char *name, unsigned int flags, float volume);
void __cdecl R_Cinematic_StartNextPlayback();
void __cdecl R_Cinematic_SyncNow();
void __cdecl R_Cinematic_DrawStretchPic_Letterboxed();
void __cdecl R_Cinematic_SetPaused(CinematicEnum paused);
void R_Cinematic_SetNextPlayback(const char *name, unsigned int flags);
void R_Cinematic_StopPlayback();
void R_Cinematic_UnsetNextPlayback();
