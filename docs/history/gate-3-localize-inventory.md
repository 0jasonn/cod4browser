# Historical Gate 3 canonical LocalizeEntry inventory

This inventory records the exact KisakCOD generated closure used by the normal
database path. The authoritative source is `src/database/db_load.cpp`, not the
Gate 2 retail census.

## Native closure and ABI

```text
Load_XAsset
  -> Load_XAssetHeader (ASSET_TYPE_LOCALIZE_ENTRY / 22)
     -> Load_LocalizeEntryPtr
        -> Load_LocalizeEntry
           -> Load_XString(value)
           -> Load_XString(name)
        -> Load_LocalizeEntryAsset
           -> DB_AddXAsset
```

`LocalizeEntry` is an eight-byte 32-bit ABI record:

```text
offset 0  const char *value
offset 4  const char *name
```

The root pointer cell is read in the caller's current block. The loader pushes
block 0 and handles null without allocation. `-1` and `-2` allocate the body
with `AllocLoad_FxElemVisStateSample` (four-byte alignment); `-2` additionally
reserves a four-byte insertion cell in block 4. Any other non-null token is a
prior alias resolved by `DB_ConvertOffsetToAlias`.

The eight-byte body is read in block 0. `Load_LocalizeEntry` then pushes block
4 and loads `value` followed by `name` through the already shared canonical
`Load_XString` implementation. An XString is null, `-1` inline, or a direct
block/offset pointer converted by `DB_ConvertOffsetToPointer`. XString has no
`-2` form and LocalizeEntry has no serialized count field. Interior references
are valid when their encoded block offset points inside an earlier owned
string.

Only after both strings succeed does `Load_LocalizeEntryAsset` publish through
the canonical LocalizeEntry pool, asset-entry free chain, name/type hash, and
loading-zone ownership. The canonical identity is `name`; `value` may be null,
but an absent name fails before hash insertion. A successful `-2` root patches
its insertion cell after publication. Failed string or publication paths leave
that cell and the hash bucket unchanged.

## Synthetic differential fixture

The valid two-asset fixture uses a `-2` inline root followed by a prior root
alias. It publishes one LocalizeEntry at entry 16, pool slot 0, changes the
free-entry count from 32,752 to 32,751, ends at block 0 offset 8 and block 4
offset 51, and patches the insertion cell at block 4 offset 16. Native x86 and
Wasm also cover a shared `-1` root, null root/value, inline strings, an interior
direct name reference, malformed root and XString tokens, null required name,
truncation, an unterminated block-sized string, pool exhaustion, entry
exhaustion, and failure-before-publication.

## Owned retail result

The normal canonical path through the legally owned
`zone/english/code_post_gfx.ff` publishes 1,116 LocalizeEntry assets at asset
indices 5 through 1,120. They occupy LocalizeEntry pool slots 0 through 1,115
and asset entries 22 through 1,137. Traversal then publishes five images and 74
RawFiles before reaching the next unsupported asset:

```text
asset index       1200
asset type        8 / ASSET_TYPE_SOUND_CURVE
root token        -1 / inline shared
stream position   block 4, offset 176300
furthest call     Load_XAssetHeader
next native call  Load_SndCurvePtr
name available    no; the body has not been loaded
```

No seek, rewind, census handoff, or zone-specific behavior participates in
this traversal.
