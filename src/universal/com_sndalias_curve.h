#pragma once

#include <sound/snd_alias_types.h>

bool Com_IsValidSoundAliasVolumeFalloffCurve(const SndCurve *curve);
SndCurve *Com_ResolveSoundAliasVolumeFalloffCurve(
    SndCurve *candidate, SndCurve *defaultCurve);
void __cdecl Com_InitDefaultSoundAliasVolumeFalloffCurve(SndCurve *sndCurve);
