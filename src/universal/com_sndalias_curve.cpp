#include "com_sndalias_curve.h"

#include <cmath>

void __cdecl Com_InitDefaultSoundAliasVolumeFalloffCurve(SndCurve *sndCurve)
{
    sndCurve->filename = "";
    sndCurve->knots[0][0] = 0.0f;
    sndCurve->knots[0][1] = 1.0f;
    sndCurve->knots[1][0] = 1.0f;
    sndCurve->knots[1][1] = 0.0f;
    sndCurve->knotCount = 2;
}

bool Com_IsValidSoundAliasVolumeFalloffCurve(const SndCurve *curve)
{
    if (!curve || curve->knotCount < 2 || curve->knotCount > 8)
        return false;

    for (int index = 0; index < curve->knotCount; ++index)
    {
        const float x = curve->knots[index][0];
        const float y = curve->knots[index][1];
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f || x > 1.0f
            || y < 0.0f || y > 1.0f)
        {
            return false;
        }
        if (index > 0 && x <= curve->knots[index - 1][0])
            return false;
    }

    // GraphGetValueFromFraction evaluates fraction 0 against the first
    // segment, so a first x above zero would trip its adjusted-fraction
    // assertion. Keep the DB boundary no stricter than those runtime x
    // requirements; the load-object parser separately normalizes its
    // traditional (0,1)/(1,0) y endpoints.
    return curve->knots[0][0] == 0.0f
        && curve->knots[curve->knotCount - 1][0] == 1.0f;
}

SndCurve *Com_ResolveSoundAliasVolumeFalloffCurve(
    SndCurve *candidate, SndCurve *defaultCurve)
{
    if (Com_IsValidSoundAliasVolumeFalloffCurve(candidate))
        return candidate;
    return Com_IsValidSoundAliasVolumeFalloffCurve(defaultCurve)
        ? defaultCurve : nullptr;
}
