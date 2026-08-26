# Historical Gate 3 DB registry convergence and PhysPreset inventory

## Ownership result

The production Wasm target now compiles `src/database/db_registry.cpp` with a
browser-SP lifecycle slice. The following functions are no longer duplicated
in `db_runtime_prefix.cpp`:

- `DB_BuildOSPath`
- `DB_TryLoadXFileInternal`
- `DB_TryLoadXFile`
- `DB_Thread`
- `DB_LoadXZone`
- `DB_LoadZone_f`
- `DB_InitThread`
- `DB_LoadXAssets`

The canonical owner preserves initial request order, alloc flags, loading
flags, logical database-thread identity, zone index/record ownership, PMem
scope, file open/failure order, XFile initialization, generated entry, and
synchronous completion. `db_runtime_prefix.cpp` shrank from lifecycle plus
trace plus browser start ownership to trace/failure plus browser start only.

## Dependency classification

| Closure | Classification | Current disposition |
| --- | --- | --- |
| Request queue, flags, zones, PMem, XFile calls | portable shared Kisak | Compiled under `db_registry.cpp`. |
| `CreateFileA`, `GetFileSize`, `CloseHandle`, install/language path | native Sys/platform and filesystem | Replaced below the canonical owner by synchronous `db_file_platform.h` operations. |
| DB OS-thread infinite wait loop | thread/synchronization | Worker-owned synchronous execution retains a separate logical DB context and Sys wake/notify/completion ordering; no pthread or Asyncify. |
| Mod directory, reorder CSV, lost-device waits | browser-SP gated feature | Not needed by the first offline startup load; remains native-owned and fail-closed. |
| Renderer screen update, archive/unarchive, unload/free resources | genuinely missing engine subsystems | Initial no-unload load is compiled; these branches return with renderer/resource convergence. |
| Debug-zone update command | gated feature | The browser command accepts one explicit zone and enters the same `DB_LoadXAssets` request path. |

`db_file_load.cpp` no longer includes `src/web/web_database_filesystem.h`.
The platform implementation is the only DB-side unit that sees the Worker
filesystem adapter. Canonical DB sees build-path, open, read, size, close, and
opaque engine file operations.

## PhysPreset generated closure

The shared extracted generated loader now follows the native order:

```text
Load_XAsset
  -> Load_XAssetHeader(ASSET_TYPE_PHYSPRESET)
  -> Load_PhysPresetPtr
     -> block 0, alignment 3
     -> optional block-4 insertion cell for -2
     -> Load_PhysPreset (44 bytes)
        -> block 4 name XString
        -> block 4 sndAliasPrefix XString
     -> Load_PhysPresetAsset
     -> DB_AddXAsset
     -> DB_LinkXAssetEntry
```

`physics/phys_preset.h` is the canonical 44-byte IW3 structure shared by xanim
and generated DB loading. No retail/web substitute exists. Null, `-1`, `-2`,
prior alias, inline and direct XString, alignment, insertion-cell, and
final-only publication behavior match the generated `db_load.cpp` source.

The primary fixture publishes `physics/gate3` with type 7, mass 12.5, and a
direct `sndAliasPrefix` pointer to its inline name. A second asset resolves the
first asset through the `-2` insertion cell at block-4 offset 16. The result is:

| Observation | Result |
| --- | --- |
| PhysPreset body | block 0 offset 0, size 44 |
| Inline name | block 4 offset 20 |
| Prior alias cell | block 4 offset 16 |
| Asset entry | 16 |
| PhysPreset pool | slot 0 of 64 |
| Free asset entries | 32,752 -> 32,751 |
| PhysPreset pool free slots | 64 -> 63 |
| Zone | 1 |

MSVC x86 and Emscripten/Node compile the same generated/publication units and
emit the identical normalized line:

```text
gate3-db-stream rawfile=published physpreset=published insert=-2 alias=block4:16 direct-xstring=block4:20 entry=16 pool=0 free=32752->32751 zone=1 stop=next-family-closure
```

Additional cases cover `-1`, null, malformed alias, unterminated/truncated and
active-block-exhausting strings, excessive script-string and asset counts
suite, PhysPreset pool exhaustion, global entry exhaustion, and failure before
publication/hash insertion.

## Owned retail traversal

The normal launcher/Worker mount was run against the locally owned English
`code_post_gfx.ff`; all other test-profile files remained freely generated.
There was no seek, rewind, census bridge, registry reconstruction, or
zone-specific parser branch.

The canonical path read the retail XFile, inflated its normal block table,
loaded 107 script strings, read the 1,639-entry XAsset array, and stopped at:

| Observation | Result |
| --- | --- |
| Asset index | 0 |
| Asset type | 5, `ASSET_TYPE_TECHNIQUE_SET` |
| Pointer | `-1` inline/shared |
| Generated function | `Load_XAssetHeader` |
| Unsupported dependency | `Load_MaterialTechniqueSetPtr` family |
| Canonical name | unavailable before the unsupported body loader runs |
| Active stream | block 4, offset 14,884, immediately after the XAsset array |

The next slice should inventory and implement the canonical
MaterialTechniqueSet generated closure, then rerun this same retail path. Do
not continue into additional families in the current slice.

## Remaining temporary prefix ownership

`db_runtime_prefix.cpp` still owns only normalized event emission, trace
snapshot mutation, bounded failure/stream observations, and
`KisakWeb_StartCanonicalDbRuntimeCheck`. The EM_JS trace emitter and exported
start hook remain temporary because browser event delivery is not yet behind a
separate trace adapter. They contain no DB lifecycle, asset decode,
publication, Promise, OPFS handle, census, or registry behavior.
