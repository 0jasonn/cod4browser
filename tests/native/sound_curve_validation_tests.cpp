#include <universal/com_sndalias_curve.h>

#include <cassert>
#include <cmath>
#include <cstring>

namespace
{
int reportedRepairCount;
const SndCurve *reportedOriginal;

void ReportRepair(const snd_alias_t *, const SndCurve *originalCurve)
{
    ++reportedRepairCount;
    reportedOriginal = originalCurve;
}

float EvaluateLinear(const SndCurve &curve, float fraction)
{
    for (int index = 1; index < curve.knotCount; ++index)
    {
        if (curve.knots[index][0] >= fraction)
        {
            const float span = curve.knots[index][0]
                - curve.knots[index - 1][0];
            const float local = (fraction - curve.knots[index - 1][0]) / span;
            return curve.knots[index - 1][1]
                + local * (curve.knots[index][1]
                    - curve.knots[index - 1][1]);
        }
    }
    return curve.knots[curve.knotCount - 1][1];
}
}

int main()
{
    SndCurve defaultCurve{};
    Com_InitDefaultSoundAliasVolumeFalloffCurve(&defaultCurve);
    assert(Com_IsValidSoundAliasVolumeFalloffCurve(&defaultCurve));
    assert(defaultCurve.knotCount == 2);
    assert(EvaluateLinear(defaultCurve, 0.0f) == 1.0f);
    assert(EvaluateLinear(defaultCurve, 0.5f) == 0.5f);
    assert(EvaluateLinear(defaultCurve, 1.0f) == 0.0f);

    SndCurve validCurve{};
    validCurve.filename = "retail_curve";
    validCurve.knotCount = 3;
    validCurve.knots[0][0] = 0.0f;
    validCurve.knots[0][1] = 1.0f;
    validCurve.knots[1][0] = 0.25f;
    validCurve.knots[1][1] = 0.75f;
    validCurve.knots[2][0] = 1.0f;
    validCurve.knots[2][1] = 0.0f;
    assert(Com_IsValidSoundAliasVolumeFalloffCurve(&validCurve));
    assert(Com_ResolveSoundAliasVolumeFalloffCurve(
        &validCurve, &defaultCurve) == &validCurve);
    assert(std::strcmp(validCurve.filename, "retail_curve") == 0);
    assert(std::fabs(EvaluateLinear(validCurve, 0.125f) - 0.875f) < 0.00001f);

    SndCurve zeroKnots{};
    SndCurve oneKnot{};
    oneKnot.knotCount = 1;
    oneKnot.knots[0][0] = 0.0f;
    oneKnot.knots[0][1] = 1.0f;
    assert(!Com_IsValidSoundAliasVolumeFalloffCurve(nullptr));
    assert(!Com_IsValidSoundAliasVolumeFalloffCurve(&zeroKnots));
    assert(!Com_IsValidSoundAliasVolumeFalloffCurve(&oneKnot));
    assert(Com_ResolveSoundAliasVolumeFalloffCurve(
        nullptr, &defaultCurve) == &defaultCurve);
    assert(Com_ResolveSoundAliasVolumeFalloffCurve(
        &zeroKnots, &defaultCurve) == &defaultCurve);
    assert(Com_ResolveSoundAliasVolumeFalloffCurve(
        &oneKnot, &defaultCurve) == &defaultCurve);

    SndCurve badEndpoints = validCurve;
    badEndpoints.knots[2][0] = 0.99f;
    assert(!Com_IsValidSoundAliasVolumeFalloffCurve(&badEndpoints));
    SndCurve badStart = validCurve;
    badStart.knots[0][0] = 0.01f;
    assert(!Com_IsValidSoundAliasVolumeFalloffCurve(&badStart));
    SndCurve badNan = validCurve;
    badNan.knots[1][1] = NAN;
    assert(!Com_IsValidSoundAliasVolumeFalloffCurve(&badNan));

    // A broken fallback owner must fail closed rather than returning an
    // invalid candidate.  This also covers shutdown/re-init zeroing of the
    // default storage before it is re-established by the owner.
    SndCurve resetDefault{};
    assert(Com_ResolveSoundAliasVolumeFalloffCurve(
        &zeroKnots, &resetDefault) == nullptr);
    Com_InitDefaultSoundAliasVolumeFalloffCurve(&resetDefault);
    assert(Com_ResolveSoundAliasVolumeFalloffCurve(
        &oneKnot, &resetDefault) == &resetDefault);

    snd_alias_t aliases[2]{};
    aliases[0].aliasName = "valid_alias";
    aliases[0].volumeFalloffCurve = &validCurve;
    aliases[1].aliasName = "malformed_alias";
    aliases[1].volumeFalloffCurve = &zeroKnots;
    snd_alias_list_t aliasList{"sound/test", aliases, 2};
    reportedRepairCount = 0;
    reportedOriginal = nullptr;
    assert(Com_RepairSoundAliasVolumeFalloffCurves(
        &aliasList, &defaultCurve, ReportRepair));
    assert(aliases[0].volumeFalloffCurve == &validCurve);
    assert(aliases[1].volumeFalloffCurve == &defaultCurve);
    assert(reportedRepairCount == 1 && reportedOriginal == &zeroKnots);

    return 0;
}
