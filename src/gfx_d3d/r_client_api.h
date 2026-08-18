#pragma once

struct Material;
struct Font_s;
struct MemoryFile;
struct refdef_s;

void __cdecl R_ClearFogs();
void __cdecl R_LoadWorld(char *name, int *checksum, int savegame);
void __cdecl R_RenderScene(const refdef_s *refdef);
void R_AddCmdSetViewportValues(int x, int y, int width, int height);
void __cdecl R_AddCmdDrawStretchPic(float x, float y, float w, float h,
    float s0, float t0, float s1, float t1, const float *color,
    Material *material);
void __cdecl R_AddCmdDrawStretchPicFlipST(float x, float y, float w, float h,
    float s0, float t0, float s1, float t1, const float *color,
    Material *material);
void __cdecl R_AddCmdDrawStretchPicRotateXY(float x, float y, float w,
    float h, float s0, float t0, float s1, float t1, float angle,
    const float *color, Material *material);
void __cdecl R_AddCmdDrawStretchPicRotateST(float x, float y, float w,
    float h, float centerS, float centerT, float radiusST, float scaleFinalS,
    float scaleFinalT, float angle, const float *color, Material *material);
void __cdecl R_AddCmdProjectionSet2D();
void __cdecl R_AddCmdProjectionSet3D();
void __cdecl R_AddCmdDrawQuadPic(const float (*verts)[2], const float *color,
    Material *material);
void __cdecl R_AddCmdDrawTextSubtitle(const char *text, int maxChars,
    Font_s *font, float x, float y, float xScale, float yScale,
    float rotation, const float *color, int style, const float *glowColor,
    bool cinematic);
void __cdecl R_EndRegistration();
void __cdecl R_ArchiveFogState(MemoryFile *memFile);
