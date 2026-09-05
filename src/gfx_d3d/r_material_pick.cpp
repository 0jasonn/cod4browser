#include <qcommon/qcommon.h>
#include <universal/com_math.h>
#include <universal/surfaceflags.h>
#include "r_cgame_api.h"

// Shared collision-backed material HUD query; no graphics API dependency.
int __cdecl R_PickMaterial(
    int traceMask,
    const float *org,
    const float *dir,
    char *name,
    char *surfaceFlags,
    char *contents,
    uint32_t charLimit)
{
    int v8; // ecx
    float end[3]; // [esp+5Ch] [ebp-48h] BYREF
    trace_t trace; // [esp+68h] [ebp-3Ch] BYREF
    int contentsLen; // [esp+94h] [ebp-10h]
    int index; // [esp+98h] [ebp-Ch]
    int surfaceFlagsLen; // [esp+9Ch] [ebp-8h]
    int i; // [esp+A0h] [ebp-4h]

    if (!charLimit)
        return 0;
    Vec3Mad(org, 262144.0, dir, end);
    CM_BoxTrace(&trace, org, end, vec3_origin, vec3_origin, 0, traceMask);
    if (trace.startsolid || trace.allsolid || trace.fraction == 1.0 || !trace.material)
        return 0;
    I_strncpyz(name, trace.material, MAX_QPATH);
    *surfaceFlags = 0;
    surfaceFlags[charLimit - 1] = 0;
    surfaceFlagsLen = 0;
    *contents = 0;
    contents[charLimit - 1] = 0;
    contentsLen = 0;
    v8 = (trace.surfaceFlags & 0x1F00000) >> 20;
    index = (uint8_t)v8;
    if ((_BYTE)v8 && index < 29)
        strncpy(surfaceFlags, infoParms[index - 1].name, charLimit);
    else
        strncpy(surfaceFlags, "^1default^7", charLimit);
    if (surfaceFlags[charLimit - 1])
        return 0;
    surfaceFlagsLen = strlen(surfaceFlags);
    if ((trace.contents & 1) != 0)
        strncpy(contents, "solid", charLimit);
    else
        strncpy(contents, "^3nonsolid^7", charLimit);
    if (contents[charLimit - 1])
        return 0;
    contentsLen = strlen(contents);
    for (i = 28; infoParms[i].name; ++i)
    {
        if ((trace.surfaceFlags & infoParms[i].surfaceFlags) != 0)
        {
            if (surfaceFlagsLen >= charLimit - 1) return 0;
            surfaceFlags[surfaceFlagsLen++] = 32;
            strncpy(
                &surfaceFlags[surfaceFlagsLen],
                infoParms[i].name,
                charLimit - surfaceFlagsLen);
            if (surfaceFlags[charLimit - 1])
                return 0;
            surfaceFlagsLen += strlen(&surfaceFlags[surfaceFlagsLen]);
        }
        if ((trace.contents & infoParms[i].contents) != 0)
        {
            if (contentsLen >= charLimit - 1) return 0;
            contents[contentsLen++] = 32;
            strncpy(&contents[contentsLen], infoParms[i].name, charLimit - contentsLen);
            if (contents[charLimit - 1])
                return 0;
            contentsLen += strlen(&contents[contentsLen]);
        }
    }
    return 1;
}

