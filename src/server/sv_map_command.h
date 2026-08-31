#pragma once

void SV_Map_f();
void SV_RegisterMapCommands();
unsigned int SV_GetMapRandomSeed();

int __cdecl ExtractMapStringFromSaveGame(
    const char *filename, char *mapname);
void __cdecl ShowLoadErrorsSummary(
    const char *mapName, unsigned int count);
