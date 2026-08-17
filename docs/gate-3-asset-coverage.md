# Gate 3 canonical asset coverage

This is the current ownership map for normal canonical DB execution. Gate 2
remains an explicitly started oracle and does not supply assets to this path.

## Canonical DB path

| Family | Status | Normal DB ownership |
| --- | --- | --- |
| RawFile | yes | Generated body/pointer loader and real pool/hash publication |
| PhysPreset | yes | Canonical 44-byte ABI, XStrings, pointer aliases and publication |
| MaterialTechniqueSet | yes | Complete reached technique/pass/shader closure and publication |
| GfxImage | yes | Body, load definition/payload, pointer aliases and publication |
| Material | yes | Info, TechniqueSet, texture/image/water, constants/state and publication |
| water_t | yes, nested | Canonical body, H0/wTerm arrays and image dependency; not an XAsset type |
| LocalizeEntry | yes | Canonical eight-byte body, XStrings, aliases and publication |
| SndCurve | yes | Canonical 72-byte body, filename XString, aliases and publication |
| Sound alias list | yes | Alias arrays, streamed/loaded references, SndCurve and SpeakerMap dependencies |
| LoadedSound | yes | Canonical metadata/payload stream, aliases and publication |
| Font | yes | Canonical body, Material dependencies, glyph array and publication |

The shared prefix also owns XAssetList, ScriptStringList, XString, stream block,
PMem, zone, pool, free-chain, hash and insertion-cell behavior used by these
families.

## Remaining Gate 2/census-only asset families

These families have useful bounded oracle coverage or canonical-shaped records
in the explicit Gate 2 path, but do not yet execute through the normal generated
DB dispatcher and real DB publication path:

- XAnimParts
- XModel and its surface/collision/physics graph
- clipMap/clipMapPVS
- ComWorld
- GfxLightDef
- MenuList
- WeaponDef
- FxEffectDef
- FxImpactTable
- StringTable
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

## Next retail blocker

Owned `code_post_gfx.ff` now publishes 1,240 assets and reaches asset 1224,
type 25 `ASSET_TYPE_FX`, at block 4 offset 219848. The next generated closure
begins with `Load_FxEffectDefHandle`. It expands into variable 252-byte element
graphs, velocity/visual samples, trails, recursive effect references,
Materials and the not-yet-compiled XModel closure, so it is the current major
architectural boundary.
