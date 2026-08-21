#pragma once

#include <sound/snd_alias_types.h>

bool Com_IsValidSoundAliasVolumeFalloffCurve(const SndCurve *curve);
SndCurve *Com_ResolveSoundAliasVolumeFalloffCurve(
    SndCurve *candidate, SndCurve *defaultCurve);
void __cdecl Com_InitDefaultSoundAliasVolumeFalloffCurve(SndCurve *sndCurve);

using SoundAliasCurveRepairReporter = void (*)(
    const snd_alias_t *alias, const SndCurve *originalCurve);
bool Com_RepairSoundAliasVolumeFalloffCurves(
    snd_alias_list_t *aliasList, SndCurve *defaultCurve,
    SoundAliasCurveRepairReporter reporter);

SndCurve *__cdecl Com_GetDefaultSoundAliasVolumeFalloffCurve();
void Com_ReportInvalidSoundAliasVolumeFalloffCurve(
    const snd_alias_t *alias, const SndCurve *originalCurve);
