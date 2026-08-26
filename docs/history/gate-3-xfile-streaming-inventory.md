# Gate 3 canonical XFile streaming inventory

> **Historical, non-authoritative checkpoint.** This report describes the
> repository state when recorded. See the [current convergence inventory](../web-port-convergence.md).

This slice continues immediately after the prior IWff magic/version/framing
probe. The repository implementation, rather than the temporary retail census,
defines the execution order.

## Native call and ownership order

The names requested during inventory do not all exist in this Kisak baseline.
There is no `DB_ReadXFile`, `DB_ReadXFileUncompressed`, `DB_AllocXBlocks`,
`DB_FreeXBlocks`, `DB_InflateInit`, `DB_Inflate`, or `DB_InflateEnd` symbol.
Their actual equivalents and order are:

```text
DB_LoadXFile
  -> zero/initialize the single-owner g_load state
  -> install the caller-owned 512 KiB compression buffer
DB_LoadXFileInternal
  -> DB_ReadXFileStage
     -> DB_ReadData
        -> ReadFileEx(first alternating 256 KiB half)
  -> DB_WaitXFileStage
     -> alertable SleepEx; publish completed input to z_stream.avail_in
  -> DB_ReadXFileStage(second alternating half)
  -> consume IWff magic and version 5
  -> DB_AuthLoad_InflateInit
     -> inflateInit_
  -> DB_LoadXFileData(sizeof(XFile))
     -> DB_AuthLoad_Inflate
        -> inflate
     -> when input is empty: DB_WaitXFileStage, then DB_ReadXFileStage
     -> wrap next_in only at the 512 KiB buffer end
  -> DB_AllocXZoneMemory(XFile::blockSize[9])
     -> DB_MemAlloc in fixed block order
        -> PMem_Alloc using g_block_mem_type and the zone allocation end
  -> DB_InitStreams
     -> g_streamZoneMem, g_streamPos, g_streamPosIndex
     -> g_streamPosArray[9], delay state, and stream-position stack
  -> Load_XAssetListCustom
     -> DB_LoadXFileData(sizeof(XAssetList))
     -> DB_PushStreamPos(4)
     -> Load_ScriptStringList
```

`DB_CancelLoadXFile` drains an outstanding half, calls
`DB_AuthLoad_InflateEnd`/`inflateEnd`, and closes the file. Zone blocks are not
owned by that cleanup; their lifetime remains the named PMem zone allocation.
Native unload later clears the `XZoneMemory` and releases that PMem scope.

The synchronous Worker filesystem replaces only `ReadFileEx`, completion wait,
file sizing, and close. A short final read is now represented explicitly rather
than credited as a complete 256 KiB read. The two-half buffer, refill order,
zlib stream, partial-output loop, cursor wrap, XFile structure, block order,
PMem ownership, and stream globals remain in the shared Kisak translation
units.

## Executed browser path

Production Wasm now directly compiles:

- `db_auth.cpp`
- `db_file_load.cpp`
- `db_memory.cpp`
- `db_stream.cpp`
- `db_stream_load.cpp`

The executed path reaches `DB_InitStreams` with all nine canonical block slots.
The following generated-prefix and first-publication slice is inventoried in
`gate-3-generated-loader-inventory.md`. No census or
`WebRetailLoadContext` state is read by either path.

The default freely generated fixture produces the 44-byte `XFile`, reports
block sizes `498816,0,0,0,407412,0,0,4224,480`, allocates the four non-empty
blocks through PMem in that order (910,932 payload bytes), and initializes
stream block 0 at logical offset 0. A second valid fixture places the first
deflate output beyond 256 KiB of compressed input and proves alternating
refill/wrap behavior without whole-file decompression.

Malformed coverage distinguishes a valid early zlib end, truncated compressed
input, corrupt zlib data, allocation exhaustion, and `size + 15` arithmetic
overflow. The complete block request is preflighted before any PMem cursor
moves, and all failure paths end inflate and close the descriptor. The direct
test then runs a valid request after failure to prove the EOF, inflate, and load
descriptor state reset. The canonical 44-byte `XFile` always contains exactly
nine block sizes; it has no separately encoded block-count field to validate.

## Differential evidence

The original streaming checkpoint compiled the same five DB translation units
for Win32 x86 and Emscripten and emitted a 44-byte-envelope trace. The current
test has advanced to the generated closure and both executions now emit:

```text
gate3-db-stream produced=134 strings=1 assets=1 type=31 entry=16 pool=0 free=32752->32751 zone=1 offsets=0,0,0,0,68,0,0,0,0 stop=next-family-closure
```

The browser Worker test separately verifies the real synchronous OPFS-style
descriptor boundary and normalized trace transport. Addresses, browser storage
identities, and host paths are absent from both traces.

## Superseded closure

`Load_XAssetListCustom`, `Load_ScriptStringList`, `Load_XAssetArrayCustom`,
`Load_XAsset`, the RawFile family, and first canonical DB publication now run.
See `gate-3-generated-loader-inventory.md` for the exact shared extraction,
trace, and next `PhysPreset` closure.

No configured legally owned retail root was available during this slice, so a
real-zone run was not claimed. The production path is generic
`zone/english/<name>.ff` and has no zone-name special case.
