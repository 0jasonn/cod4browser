# Historical Gate 3 canonical FxEffectDef inventory

> **Historical, non-authoritative checkpoint.** This report describes the
> repository state when recorded. See the [current convergence inventory](../web-port-convergence.md).

This slice compiles the native generated FX ordering into the normal database
path. It does not use the Gate 2 census or introduce a browser FX type.

## Canonical closure

The implemented path is:

```text
Load_XAsset
  -> Load_FxEffectDefHandle
     -> Load_FxEffectDef
        -> Load_XString(name)
        -> Load_FxElemDefArray
           -> velocity and visual-state samples
           -> Load_FxElemDefVisuals
              -> MaterialHandle, sound XString, effect reference,
                 or XModelPtr according to elemType
           -> three FxEffectDefRef values
           -> optional FxTrailDef, vertices, and indices
     -> Load_FxEffectDefAsset
     -> canonical DB publication
```

The 32-bit ABI uses a 32-byte `FxEffectDef`, 252-byte `FxElemDef`, 96-byte
velocity sample, 48-byte visual sample, 28-byte trail definition, and 20-byte
trail vertex. Root handles preserve null, `-1`, `-2`, insertion-cell, and prior
alias behavior. The body is allocated in block 0; names, element arrays,
samples, visuals, effect names, and trails retain native block-4 order and
alignment. Publication happens only after the complete reached dependency
graph succeeds.

The serialized FX records formerly duplicated in `fxprimitives.h` now have one
canonical database-facing owner in `EffectsCore/fx_types.h`. Renderer runtime
state remains in `fxprimitives.h`.

Inline XModel visuals deliberately fail at the exact native
`Load_XModelPtr -> Load_XModel` boundary until that large canonical closure is
compiled. Null and prior-alias XModel cells retain native behavior. No fake
model or partial FX publication is produced.

## Differential evidence

The synthetic fixture covers a published FX with velocity/visual samples and a
trail, shared and inserted roots, a prior root alias, null optional data, an
inline sound XString, malformed aliases and counts, truncation, FX pool and
asset-entry exhaustion, and failure before publication at an inline XModel.
MSVC x86 and Wasm/Node produce the same normalized trace. The successful
inserted fixture publishes pool slot 0 and entry 16, changes the free chain
from 32,752 to 32,751, leaves 399 of 400 FX pool records, and finishes at block
0 offset 32 and block 4 offset 502. Failed publication leaves the hash and
insertion cell unchanged.

## Owned retail evidence

Installed Chrome traverses the legally owned
`zone/english/code_post_gfx.ff` without seeking or census handoff. Asset 1224
publishes `misc/missing_fx` as type 25 after naturally publishing one nested
Material and one nested GfxImage. The zone has then published 1,243 assets,
used asset entry 1,258 for the FX, and reduced the free-entry count to 31,509.

Traversal stops at asset 1225, type 26 `ASSET_TYPE_IMPACT_FX`, block 4 offset
220,880, on `Load_FxImpactTablePtr`. The native next closure is an eight-byte
`FxImpactTable`, its name XString, and a fixed array of 12 132-byte
`FxImpactEntry` records. Each entry loads 29 non-flesh and four flesh
`FxEffectDefHandle` values through the FX family implemented here.
