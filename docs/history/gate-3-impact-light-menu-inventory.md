# Historical Gate 3 Impact, Light, and Menu inventory

This checkpoint follows the canonical FX closure. Normal DB execution remains
independent of the Gate 2 census and renderer oracle.

## Canonical generated closures

`db_generated_fx_impact.cpp` mirrors `Load_FxImpactTablePtr`, the eight-byte
body, fixed 12-entry `FxImpactEntry` array, and all 396 embedded
`FxEffectDefHandle` loads. Root null, `-1`, `-2`, insertion, prior aliases,
block transitions, child-before-parent ordering, and final type-26 publication
use the shared DB stream and registry state.

`db_generated_light.cpp` mirrors `Load_GfxLightDefPtr`, the 16-byte body,
name XString, embedded eight-byte `GfxLightImage`, canonical `GfxImagePtr`
dependency, and final type-17 publication.

The dependency-heavy UI closure is split between
`db_generated_menu.cpp` and `db_generated_menu_expression.cpp`. It covers:

- `MenuList` and `menuDef_t` root null, `-1`, `-2`, insertion, and prior alias
  behavior;
- block-0 root bodies and block-4 names, pointer arrays, windows, recursive key
  handlers, items, list/edit/multi type data, Materials, and sounds;
- expression pointer arrays, entries, operands, and string operands;
- Menu publication before its owning MenuList and native reparenting of every
  item to the published Menu identity.

The unchanged database-facing UI ABI now lives in lightweight
`ui/ui_asset_types.h`. Generated DB units no longer include the unrelated
`ui_shared.h -> bg_local.h` gameplay/parser graph. This is a shared engine type
boundary, not a browser representation.

## Differential evidence

The same synthetic fastfiles execute in MSVC x86 and Emscripten/Node. The
normalized outputs match exactly. Coverage includes inline/shared/insertion
and prior aliases, direct and inline XStrings, nested FX and GfxImage
dependencies, menu/item/key/type-data/expression ordering, invalid aliases,
negative counts, truncation, pool exhaustion, asset-entry exhaustion, and
failure before parent publication. The rich menu fixture ends at block-0
offset 296 and block-4 offset 880, publishes Menu entry 16 before MenuList
entry 17, and consumes two free entries (32,752 to 32,750).

## Retail evidence and next boundary

Installed Chrome traverses the legally owned
`zone/english/code_post_gfx.ff` through normal open, inflate, stream, generated
dispatch, and DB publication. It publishes 1,555 records:

| Type | Family | Publications |
| ---: | --- | ---: |
| 4 | Material | 72 |
| 5 | MaterialTechniqueSet | 86 |
| 6 | GfxImage | 24 |
| 7 | Sound alias | 11 |
| 8 | SndCurve | 2 |
| 9 | LoadedSound | 9 |
| 17 | GfxLightDef | 2 |
| 19 | Font | 9 |
| 20 | MenuList | 2 |
| 21 | Menu | 14 |
| 22 | LocalizeEntry | 1,248 |
| 25 | FxEffectDef | 1 |
| 26 | FxImpactTable | 1 |
| 31 | RawFile | 74 |

Traversal stops without seeking at asset 1506, type 23
`ASSET_TYPE_WEAPON`, block 4 offset 373196. The exact next call is
`Load_WeaponDefPtr`. Native `Load_WeaponDef` consumes a 2,168-byte body and
depends on XModel pointer arrays, 33 animation-name XStrings, 40 script
strings, FX, Materials, dozens of sound-name cells, a 29-entry bounce-sound
array, projectile dependencies, and four accuracy-graph arrays. Because the
canonical Gate 3 XModel generated closure is still missing, WeaponDef is a
major dependency slice and the appropriate architecture-review boundary.
