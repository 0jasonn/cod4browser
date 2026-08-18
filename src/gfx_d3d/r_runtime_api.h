#pragma once

struct Font_s;
struct GfxConfiguration;
struct Material;
struct vidConfig_t;

void __cdecl R_BeginRegistration(vidConfig_t *vidConfigOut);
void __cdecl R_ConfigureRenderer(const GfxConfiguration *config);
void __cdecl R_Shutdown(int destroyWindow);
void R_ShutdownDirect3D();
void __cdecl R_SyncRenderThread();
Font_s *__cdecl R_RegisterFont(const char *name, int imageTrack);
void __cdecl R_AddCmdDrawStretchPic(float x, float y, float w, float h,
    float s0, float t0, float s1, float t1, const float *color,
    Material *material);
void __cdecl R_AddCmdDrawText(const char *text, int maxChars, Font_s *font,
    float x, float y, float xScale, float yScale, float rotation,
    const float *color, int style);
void __cdecl R_AddCmdDrawTextWithCursor(const char *text, int maxChars,
    Font_s *font, float x, float y, float xScale, float yScale,
    float rotation, const float *color, int style, int cursorPos,
    char cursor);
void __cdecl R_AddCmdDrawTextWithEffects(const char *text, int maxChars,
    Font_s *font, float x, float y, float xScale, float yScale,
    float rotation, const float *color, int style, const float *glowColor,
    Material *fxMaterial, Material *fxMaterialGlow, int fxBirthTime,
    int fxLetterTime, int fxDecayStartTime, int fxDecayDuration);
