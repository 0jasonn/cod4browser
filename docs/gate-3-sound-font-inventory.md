# Gate 3 SoundCurve, sound alias, LoadedSound and Font inventory

This checkpoint follows the native generated implementation in
`src/database/db_load.cpp` and normal owned `code_post_gfx.ff` traversal.

## SoundCurve

`SndCurve` is a 72-byte record containing `filename`, `knotCount`, and eight
two-float knots. `Load_SndCurvePtr` loads the pointer cell, pushes block 0,
handles null, `-1`, `-2`, insertion and prior aliases, then loads the body in
block 0. The filename uses the shared XString loader in block 4. Publication is
final and uses the real type-8 pool, asset-entry chain, hash and zone owner.

## Sound alias closure

`Load_snd_alias_list_ptr` owns the normal null/inline/insertion/alias root
forms. The 12-byte list body is in block 0; its name, optional 92-byte alias
array, strings and children are in block 4. Each alias preserves native order:

1. four XStrings;
2. optional inline/direct `SoundFile`;
3. streamed dir/name or canonical `LoadedSound` dependency;
4. canonical `SndCurve` pointer;
5. optional inline/direct 408-byte `SpeakerMap` and name.

The alias list is published only after all dependencies succeed. LoadedSound
uses the canonical 44-byte body, name XString, block-0 payload, `-1`/`-2`
payload insertion and real type-9 publication. The narrow
`DB_PlatformSetLoadedSoundData` seam copies transient stream payloads through
the sound driver's native ownership/resample operation before loading advances.

The native dispatcher has no `ASSET_TYPE_SNDDRIVER_GLOBALS` case. The shared
dispatcher therefore preserves its exact no-consumption behavior rather than
inventing a loader or publication.

## Font

`Font_s` is a 24-byte record. `Load_FontHandle` owns the canonical root forms,
loads the body in block 0, then in block 4 loads the font-name XString, two
existing canonical MaterialHandle dependencies and the optional 24-byte Glyph
array. Count overflow/truncation fails before final type-19 publication.

## Differential evidence

The Win32 x86 and Wasm fixtures exercise null, `-1`, `-2`, prior aliases,
insertion cells, inline strings, streamed SoundFile data, SpeakerMap,
LoadedSound payload insertion, Font glyph arrays, malformed pointers,
truncation, invalid counts, pool exhaustion, asset-entry exhaustion and
failure-before-publication. Their normalized trace text is identical.

## Retail evidence and next boundary

Normal owned `zone/english/code_post_gfx.ff` publishes:

- 11 sound-alias lists;
- 2 SndCurves;
- 9 LoadedSounds;
- 9 Fonts;
- the nested Materials, TechniqueSets and Images they reference.

The zone now publishes 1,240 assets and stops at asset 1224,
`ASSET_TYPE_FX`, block 4 offset 219848, root token `-1`. The next native call
is `Load_FxEffectDefHandle`. Its closure includes variable FxElemDef graphs,
samples, trails, recursive effect references, Materials and XModel. XModel is
not yet canonical in the web target, making FX the current major architectural
decision boundary.
