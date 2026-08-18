# Gate 3 canonical asset coverage

This is the current ownership map for normal canonical DB execution. Gate 2
remains an explicitly started oracle and does not supply assets to this path.

## Canonical DB path

| Family | Status | Normal DB ownership |
| --- | --- | --- |
| RawFile | yes | Generated body/pointer loader and real pool/hash publication |
| PhysPreset | yes | Canonical 44-byte ABI, XStrings, pointer aliases and publication |
| XModel | yes | Canonical skeleton, surface/vertex/index, collision, Material, PhysPreset and physical-geometry closure |
| XAnimParts | yes | Canonical notifies, ScriptStrings, packed arrays, dynamic indices and delta translation/quaternion closure |
| WeaponDef | yes | Canonical 2,168-byte body, XModel/Material/FX/sound dependencies, ScriptStrings and accuracy arrays |
| MaterialTechniqueSet | yes | Complete reached technique/pass/shader closure and publication |
| GfxImage | yes | Body, load definition/payload, pointer aliases and publication |
| Material | yes | Info, TechniqueSet, texture/image/water, constants/state and publication |
| water_t | yes, nested | Canonical body, H0/wTerm arrays and image dependency; not an XAsset type |
| LocalizeEntry | yes | Canonical eight-byte body, XStrings, aliases and publication |
| SndCurve | yes | Canonical 72-byte body, filename XString, aliases and publication |
| Sound alias list | yes | Alias arrays, streamed/loaded references, SndCurve and SpeakerMap dependencies |
| LoadedSound | yes | Canonical metadata/payload stream, aliases and publication |
| Font | yes | Canonical body, Material dependencies, glyph array and publication |
| FxEffectDef | yes, reached closure | Canonical elements, samples, visuals, references, trails and publication; inline XModel bodies remain fail-closed |
| FxImpactTable | yes | Fixed 12-entry table, 396 FxEffectDef handles, aliases and dependency-ordered publication |
| GfxLightDef | yes | Canonical 16-byte body, embedded light image, GfxImage dependency and publication |
| MenuList | yes | Canonical list body, Menu pointer array, root aliases and final publication |
| Menu | yes, nested and top-level | Window/item/key/type-data/expression closure, Material/sound dependencies, item-parent reparenting and publication |
| StringTable | yes | Canonical special root-pointer rules, name/value XStrings and row/column table publication |

The shared prefix also owns XAssetList, ScriptStringList, XString, stream block,
PMem, zone, pool, free-chain, hash and insertion-cell behavior used by these
families.

## Remaining Gate 2/census-only asset families

These families have useful bounded oracle coverage or canonical-shaped records
in the explicit Gate 2 path, but do not yet execute through the normal generated
DB dispatcher and real DB publication path:

- clipMap/clipMapPVS
- ComWorld
- GfxWorld

Material, TechniqueSet, GfxImage, PhysPreset, RawFile and LocalizeEntry may
still appear inside Gate 2 oracle results, but they are no longer census-only.

## Temporary scaffolding still present

- `db_runtime_prefix.cpp` contains the browser entry hook plus normalized
  trace/failure seams; it owns no generated asset behavior and remains
  shrink-only.
- `common_gate3_prefix.inl` remains the frozen qcommon bootstrap extraction.
- `web_retail_fastfile_census.*`, its resumable family loaders, registry and
  retained Killhouse ownership remain an explicitly started Gate 2 oracle.
- Renderer-disabled `Load_Texture`, shader/material post-load, and
  `Load_PicmipWater` adapters preserve native signatures until the canonical
  renderer frontend/backend path owns those operations.
- `DB_PlatformSetLoadedSoundData` retains zone-owned LoadedSound bytes until a
  browser audio backend owns the native copy/resample boundary.
- The launcher, Worker mount, synchronous filesystem adapter and WebGL backend
  remain legitimate browser platform boundaries.

## Current retail boundary

Owned `code_post_gfx.ff` completes all 1,639 assets through the ordinary
generated dispatcher. It produces 1,698 real publications through entry 1,713,
leaves 31,054 free entries, and ends at asset 1,638, type 31 RawFile
`code_post_gfx`. All allocated stream blocks are consumed exactly:
`[0,0,0,0,407412,0,0,4224,480]`. There is no unsupported generated family in
this zone. This is an architecture-review stop; the next work item is to resume
the normal startup zone request order and inventory the first genuine family
boundary reached there, without preselecting or seeking to a later asset.
