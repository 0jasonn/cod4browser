# Gate 3 generated loader and first publication inventory

> **Historical, non-authoritative checkpoint.** This report describes the
> repository state when recorded. See the [current convergence inventory](../web-port-convergence.md).

This slice starts at the previously proven `DB_InitStreams` boundary. It does
not call the retail census, `WebRetailLoadContext`, or the Gate 2 registry.

## Exact executed order

The shared Win32 x86 and Wasm path is now:

```text
DB_InitStreams
  -> Load_XAssetListCustom
     -> DB_LoadXFileData(sizeof(XAssetList))
     -> DB_PushStreamPos(4)
     -> Load_ScriptStringList
        -> Load_Stream(sizeof(ScriptStringList))
        -> DB_PushStreamPos(4)
        -> AllocLoad_FxElemVisStateSample
        -> Load_TempStringArray
           -> Load_TempString
           -> AllocLoad_raw_byte
           -> Load_TempStringCustom
           -> SL_GetString(..., 4)
        -> DB_PopStreamPos
     -> DB_PopStreamPos
  -> DB_PushStreamPos(4)
  -> AllocLoad_FxElemVisStateSample
  -> Load_XAssetArrayCustom
     -> Load_Stream(count * sizeof(XAsset))
     -> Load_XAsset
        -> Load_Stream(sizeof(XAsset))
        -> Load_XAssetHeader
        -> Load_RawFilePtr
           -> DB_PushStreamPos(0)
           -> DB_AllocStreamPos(3)
           -> DB_InsertPointer for -2
           -> Load_RawFile
              -> Load_Stream(sizeof(RawFile))
              -> DB_PushStreamPos(4)
              -> Load_XString
              -> Load_ConstCharArray
              -> DB_PopStreamPos
           -> Load_RawFileAsset
              -> DB_AddXAsset
              -> DB_LinkXAssetEntry
           -> DB_PopStreamPos
  -> DB_PopStreamPos(4)
```

`src/database/db_generated_loaders.cpp` is a narrow extraction from the
generated `db_load.cpp` closure. It keeps the canonical function names and
valid-input operation order and is compiled unchanged by both the Emscripten
production target and the Win32 x86/Wasm differential targets. It contains
only the root/list/string helpers and the RawFile branch required by this
slice. A valid but uncompiled family fails explicitly at
`Load_XAssetHeader/unsupported family closure`; it never returns success and
there are no dummy, weak, or no-op family loaders. The complete generated
`db_load.cpp` remains authoritative and is still used by ordinary native game
builds.

Portable checks precede potentially overflowing count arithmetic and reject a
read outside the active canonical block. They do not alter legal stream
ordering. The allocator, stream stack, offset conversion, insertion cells,
and script-string interning remain the Kisak functions rather than a new
translation layer.

## Generated global state

The prefix mutates the same generated variables as native Kisak:

| State | Use in this closure |
| --- | --- |
| `varXAssetList` | Points at the file-local root `XAssetList` before its 16-byte read. |
| `varScriptStringList` | Points at `varXAssetList->stringList` before the generated string-list call. |
| `varTempString`, `varConstChar`, `varXString` | Track the current serialized string cell and inline character payload. |
| `varXAsset` | Points at the allocated block-4 array, then advances in serialized order. |
| `varXAssetHeader` | Points at the current asset's four-byte header cell before dispatch. |
| `varRawFilePtr`, `varRawFile` | Track the header cell and block-0 RawFile body. |
| `varXAssetType` | Exists in the monolithic generated unit but is not read or written by `Load_XAssetListCustom`, `Load_ScriptStringList`, `Load_XAssetArrayCustom`, or `Load_XAsset`. It was therefore not added to the extracted closure. |
| `g_streamPos`, `g_streamPosArray`, `g_streamPosIndex`, `g_streamZoneMem` | Remain the sole stream truth. Push/pop saves and restores active block cursors exactly as the generated path expects. |

Generated `var*` variables intentionally retain the last object selected, as
the native generated code does. Stream state, rather than those globals, is
stack-restored by `DB_PushStreamPos`/`DB_PopStreamPos`.

## ScriptStringList proof

Three freely generated fixtures establish the progression:

1. Zero script strings and zero assets returns successfully with all nine
   final offsets zero.
2. `gate3_alpha`, `gate3_beta` and zero assets registers both strings in order;
   block 4 ends at offset 31.
3. `gate3_script_identity` followed by one RawFile registers the string and
   then enters the asset dispatcher.

The list preserves the serialized eight-byte root, block-4 pointer table,
four-byte alignment, `-1` inline string form, offset conversion, and
`SL_GetString` user 4 registration. The production target uses the canonical
`scr_stringlist.cpp`; the direct differential executable supplies only a
deterministic test host for the same API.

## Canonical first DB publication

`src/database/db_registry_publication.cpp` extracts the smallest first-asset
publication closure from `db_registry.cpp`: canonical name hashing,
`DB_AddXAsset`, file-local `DB_LinkXAssetEntry`, RawFile pool allocation and
clone, the 32,768-entry free chain, hash insertion, zone ownership, lookup,
and duplicate/override ordering. It consumes the existing storage from
`db_registry_pools.cpp`; it does not own another registry.

For `tests/gate3_first.txt` (`RawFile`, type 31, payload `first`):

| Observation | Result |
| --- | --- |
| Pointer form | `inline-insert/-2` |
| Asset entry | index 16 |
| RawFile pool | index 0 of 1,024 |
| Free asset entries | 32,752 -> 32,751 |
| Hash | canonical case-insensitive slash-normalized `DB_HashForName` result |
| Zone | index 1 |
| Lookup | `DB_FindXAssetHeader(ASSET_TYPE_RAWFILE, name)` returns the published pool object |
| Final stream offsets | `0,0,0,0,68,0,0,0,0` |

Block 0 returns to offset zero after the temporary inline body scope is
popped. Publication remains valid because the body is cloned into the RawFile
pool before that pop. Same-zone duplicate rejection occurs before allocation;
pool and entry exhaustion cannot insert a hash record or emit publication end.

## Differential and failure evidence

The Win32 x86 and Emscripten executables consume the same legal fixtures and
emit the same normalized line:

```text
gate3-db-stream produced=134 strings=1 assets=1 type=31 entry=16 pool=0 free=32752->32751 zone=1 offsets=0,0,0,0,68,0,0,0,0 stop=next-family-closure
```

Browser coverage additionally rejects a truncated `XAssetList`, excessive
script-string count, truncated script string, excessive asset count, invalid
asset type, malformed alias, invalid prior offset, and a RawFile truncated
before publication. The direct shared test forces RawFile pool exhaustion and
asset-entry exhaustion. In every case publication end and hash lookup remain
absent for the failed asset, cleanup runs, and a later valid load can reuse the
global descriptor.

## Exact next generated-family closure

The first uncompiled branch in canonical `Load_XAssetHeader` order is type 1:

```text
ASSET_TYPE_PHYSPRESET
  -> Load_PhysPresetPtr
  -> Load_PhysPreset (44-byte body, block-4 name and sndAliasPrefix)
  -> Load_PhysPresetAsset
  -> DB_AddXAsset
```

That slice requires the canonical `PhysPreset` ABI, its generated globals and
string children, pool clone/name handlers, and the corresponding registry
publication behavior. It should be added to this shared extraction before
moving to `XAnimParts`; full `db_load.cpp` still references the complete
XAnim/XModel/material/image/sound/collision/world/UI/weapon/FX/StringTable
graph and is intentionally not linked wholesale.

No configured legally owned retail root or retail `.ff` fixture was available,
so no real-zone result is claimed. The generic logical path remains
`zone/english/<zone>.ff`; no `code_post_gfx`, `common`, or `killhouse` branch
was added.
