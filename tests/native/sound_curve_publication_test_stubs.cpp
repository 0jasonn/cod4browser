#include <universal/com_sndalias_curve.h>

namespace
{
SndCurve g_defaultCurve;
bool g_defaultInitialized;
}

SndCurve *__cdecl Com_GetDefaultSoundAliasVolumeFalloffCurve()
{
    if (!g_defaultInitialized)
    {
        Com_InitDefaultSoundAliasVolumeFalloffCurve(&g_defaultCurve);
        g_defaultInitialized = true;
    }
    return &g_defaultCurve;
}

void Com_ReportInvalidSoundAliasVolumeFalloffCurve(
    const snd_alias_t *, const SndCurve *)
{
}
