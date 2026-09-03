#pragma once

#include <gfx_d3d/gfx_color_types.h>
#include <gfx_d3d/r_font.h>

// The native text renderer submits quads through the platform backend.
int R_TextSceneTime();
int R_TextCursorTime();
extern GfxColor color_table[8];
void __cdecl RB_LookupColor(uint8_t c, GfxColor *color);
void __cdecl R_TextDrawQuad(
    const Material *material,
    float x,
    float y,
    float w,
    float h,
    float s0,
    float t0,
    float s1,
    float t1,
    float sinAngle,
    float cosAngle,
    uint32_t color);
void __cdecl DrawText2D(
    const char *text,
    float x,
    float y,
    Font_s *font,
    float xScale,
    float yScale,
    float sinAngle,
    float cosAngle,
    GfxColor color,
    int maxLength,
    std::int16_t renderFlags,
    int cursorPos,
    char cursorLetter,
    float padding,
    GfxColor glowForcedColor,
    int fxBirthTime,
    int fxLetterTime,
    int fxDecayStartTime,
    int fxDecayDuration,
    const Material *fxMaterial,
    const Material *fxMaterialGlow);
double __cdecl RB_DrawHudIcon(
    const char *text,
    float x,
    float y,
    float sinAngle,
    float cosAngle,
    Font_s *font,
    float xScale,
    float yScale,
    uint32_t color);
void __cdecl RB_DrawCursor(
    const Material *material,
    uint8_t cursor,
    float x,
    float y,
    float sinAngle,
    float cosAngle,
    Font_s *font,
    float xScale,
    float yScale,
    uint32_t color);
void __cdecl RotateXY(
    float cosAngle,
    float sinAngle,
    float pivotX,
    float pivotY,
    float x,
    float y,
    float *outX,
    float *outY);
double __cdecl GetMonospaceWidth(Font_s *font, char renderFlags);
void __cdecl GlowColor(GfxColor *result, GfxColor baseColor, GfxColor forcedGlowColor, char renderFlags);
char __cdecl SetupPulseFXVars(
    const char *text,
    int maxLength,
    char renderFlags,
    int fxBirthTime,
    int fxLetterTime,
    int fxDecayStartTime,
    int fxDecayDuration,
    bool *resultDrawRandChar,
    int *resultRandSeed,
    int *resultMaxLength,
    bool *resultDecaying,
    int *resultdecayTimeElapsed);
void __cdecl GetDecayingLetterInfo(
    uint32_t letter,
    Font_s *font,
    int *randSeed,
    int decayTimeElapsed,
    int fxBirthTime,
    int fxDecayDuration,
    uint8_t alpha,
    bool *resultSkipDrawing,
    uint8_t *resultAlpha,
    uint32_t *resultLetter,
    bool *resultDrawExtraFxChar);
void __cdecl DrawTextFxExtraCharacter(
    const Material *material,
    int charIndex,
    float x,
    float y,
    float w,
    float h,
    float sinAngle,
    float cosAngle,
    uint32_t color);
uint8_t __cdecl ModulateByteColors(uint8_t colorA, uint8_t colorB);

inline int R_TextStyleFlags(int style)
{
    return style == 3 ? 4 : style == 6 ? 12 : style == 128 ? 1 : 0;
}
