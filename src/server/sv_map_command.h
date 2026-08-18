#pragma once

void SV_Map_f();
void SV_RegisterMapCommands();

int __cdecl ExtractMapStringFromSaveGame(
    const char *filename, char *mapname);
void __cdecl ShowLoadErrorsSummary(
    const char *mapName, unsigned int count);
