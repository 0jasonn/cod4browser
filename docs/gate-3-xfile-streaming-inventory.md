# Gate 3 canonical XFile streaming inventory

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

The executed path reaches `DB_InitStreams` with all nine canonical block slots,
then publishes `first generated-loader entry` and stops at
`Load_XAssetListCustom/generated-loader-closure`. No census or
`WebRetailLoadContext` state is read.

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

`gate3_db_stream_trace_tests` compiles the same five shared DB translation
units for Win32 x86 and Emscripten. Both executions emit:

```text
gate3-db-stream produced=44 blocks=1024,0,0,0,2048,0,0,64,32 allocations=4 stop=generated-loader-closure
```

The browser Worker test separately verifies the real synchronous OPFS-style
descriptor boundary and normalized trace transport. Addresses, browser storage
identities, and host paths are absent from both traces.

## Exact next closure

The first deliberately unentered function is `Load_XAssetListCustom` in
`db_file_load.cpp`. Its first generated dependency is
`Load_ScriptStringList` in `db_load.cpp`. Compiling `db_load.cpp` currently
pulls the generated family graph and globals for XAnim, XModel, materials,
images, sound, collision, ComWorld, GfxWorld, FX, weapons, RawFile,
StringTable, renderer buffer finalization, and database publication.

The next slice should extract or compile that closure in native generated
order, beginning with `XAssetList` and `ScriptStringList`, then stop at the
first asset-family dependency that cannot link. It must continue to use
`g_streamPos`, `g_streamPosArray`, `g_streamZoneMem`, and PMem-owned blocks;
the census remains differential evidence only.

No configured legally owned retail root was available during this slice, so a
real-zone run was not claimed. The production path is generic
`zone/english/<name>.ff` and has no zone-name special case.
