#pragma once

struct Material;

void __cdecl R_SyncRenderThread();
void __cdecl Material_Sort();
Material *__cdecl Material_RegisterHandle(const char *name, int imageTrack);
