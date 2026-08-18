#pragma once

#include "r_debug_types.h"
#include "r_gfx.h"
#include "r_warning_types.h"

struct GfxDebugPoly // sizeof=0x18
{
    float color[4];
    int firstVert;
    int vertCount;
};

struct GfxDebugPlume // sizeof=0x28
{
    float origin[3];
    float color[4];
    int score;
    int startTime;
    int duration;
};

struct DebugGlobals // sizeof=0x54
{                                       // ...
    float (*verts)[3];
    int vertCount;
    int vertLimit;
    GfxDebugPoly* polys;
    int polyCount;
    int polyLimit;
    trDebugString_t* strings;
    int stringCount;
    int stringLimit;
    trDebugString_t* externStrings;
    int externStringCount;
    int externMaxStringCount;
    trDebugLine_t* lines;
    int lineCount;
    int lineLimit;
    trDebugLine_t* externLines;
    int externLineCount;
    int externMaxLineCount;
    GfxDebugPlume* plumes;              // ...
    int plumeCount;                     // ...
    int plumeLimit;                     // ...
};

void __cdecl TRACK_r_debug();
void __cdecl R_AddDebugPolygon(DebugGlobals *debugGlobalsEntry, const float *color, int pointCount, float (*points)[3]);
void __cdecl R_AddDebugLine(DebugGlobals *debugGlobalsEntry, const float *start, const float *end, const float *color);
void __cdecl R_AddDebugBox(DebugGlobals *debugGlobalsEntry, const float *mins, const float *maxs, const float *color);
void __cdecl R_AddDebugString(
    DebugGlobals *debugGlobalsEntry,
    const float *origin,
    const float *color,
    float scale,
    const char *string);
void __cdecl R_AddScaledDebugString(
    DebugGlobals *debugGlobalsEntry,
    const GfxViewParms *viewParms,
    const float *origin,
    const float *color,
    const char *string);
void __cdecl R_InitDebugEntry(DebugGlobals *debugGlobalsEntry);
void __cdecl R_InitDebug();
void __cdecl R_ShutdownDebugEntry(DebugGlobals *debugGlobalsEntry);
void __cdecl R_TransferDebugGlobals(DebugGlobals *debugGlobalsEntry);
void __cdecl R_ShutdownDebug();
void __cdecl R_CopyDebugStrings(
    trDebugString_t *clStrings,
    int clStringCnt,
    trDebugString_t *svStrings,
    int svStringCnt,
    int maxStringCount);
void __cdecl R_CopyDebugLines(
    trDebugLine_t *clLines,
    int clLineCnt,
    trDebugLine_t *svLines,
    int svLineCnt,
    int maxLineCount);

// r_debug_alloc
void __cdecl R_DebugAlloc(void **memPtr, int size, const char *name);
void __cdecl R_DebugFree(void **dataPtr);


// r_warn
void R_WarnOncePerFrame(GfxWarningType warnType, ...);
double __cdecl R_UpdateFrameRate();
void __cdecl R_WarnInitDvars();

extern DebugGlobals debugGlobals;
