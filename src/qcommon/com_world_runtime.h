#pragma once

struct ComWorld;

extern ComWorld comWorld;

void __cdecl Com_LoadWorld(char *name);
void __cdecl Com_LoadWorld_FastFile(const char *name);
void __cdecl Com_ShutdownWorld();
void __cdecl Com_UnloadWorld();
