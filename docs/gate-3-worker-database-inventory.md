# Gate 3 Worker/database execution inventory

This slice starts at the prior canonical stop:

```text
Com_InitXAssets
  -> DB_InitThread
  -> Sys_SpawnDatabaseThread(DB_Thread)
```

It establishes the browser host needed to cross that boundary without
Asyncify, Promises in qcommon/database, Emscripten pthreads, or the retail
census acting as a database.

## Native thread semantics

`DB_InitThread` only spawns `DB_Thread`. Native `Sys_SpawnDatabaseThread`
creates an auto-reset wake event plus two manual-reset completion events and a
resume event, creates the database OS thread, assigns its priority, and resumes
it. `DB_Thread` owns the database thread context and error slot, then loops:

```text
Sys_WaitStartDatabase()
DB_TryLoadXFile()
```

`DB_LoadXZone` is the main-thread request publisher. It copies at most eight
zone names/flags into `g_zoneInfo`, sets `g_loadingAssets`, resets completion
state, publishes `g_zoneInfoCount`, and signals the wake event. The database
thread snapshots and clears the count, processes requests sequentially through
`DB_TryLoadXFileInternal`, finishes reorder state, and signals completion.
Main-thread post-load publication remains ordered after that completion.

`g_load` has one database-thread owner. Hash and reorder critical sections
permit native main/render lookups while the load runs. Separate `Sys_GetValue`
slots, parse state, and file-handle slots are selected by database thread
identity.

Classification:

| Behavior | Classification | Browser treatment |
| --- | --- | --- |
| One owner for `g_load`, zone requests, and stream state | Semantically required | One logical DB context executes one request batch at a time. |
| Request publication before execution; completion before post-load publication | Required ordering/synchronization | Preserved synchronously by notify/execute/complete barriers. |
| Distinct database thread identity and value slots | Required ordering/context | `THREAD_CONTEXT_DATABASE` is entered around DB work; its slots are distinct. |
| Concurrent asset lookup while decoding | Native responsiveness capability | Not required before client/game bring-up; no concurrent consumers exist in this slice. |
| OS thread priority and alertable Win32 wait | Native performance/I/O detail | Not reproduced. The Worker already isolates engine work from the DOM thread. |
| Dedicated engine Worker | Genuine browser platform requirement | Owns Wasm, qcommon, DB, synchronous FS, and OffscreenCanvas/WebGL2. |
| A second Worker or Emscripten pthread | Not currently semantically required | Not enabled. Revisit only when concurrent consumers demonstrate a correctness need. |

The initial offline execution model is therefore synchronous inside the one
dedicated engine Worker. `Sys_SpawnDatabaseThread` records and initializes the
canonical DB entry point under logical database identity. `Sys_NotifyDatabase`
enters that identity, runs the pending batch, and returns after
`Sys_DatabaseCompleted`. This preserves state and publication order while
omitting native responsiveness concurrency.

## Worker ownership and filesystem

The DOM main thread owns only the launcher, picker/user gestures, validation,
IndexedDB manifest metadata, OPFS import staging, Worker lifecycle, and the
HTML canvas element. It transfers the canvas once and forwards resize messages
and engine events.

The engine Worker owns the Emscripten module, all C++ engine state, PMem,
command/qcommon state, DB state, synchronous file descriptors, and the
OffscreenCanvas WebGL2 context.

After validation, the main thread sends the manifest to the Worker as platform
mount configuration. The Worker walks the import directory and opens a
`FileSystemSyncAccessHandle` for every manifest file before starting engine
file consumers. The mount table is generic and case-folded; it does not
special-case `common.ff`, `killhouse.ff`, or any census asset. C/C++ sees only
logical paths and synchronous `open`, `size`, `seek`, `read`, and `close`.
Import UUIDs, OPFS handles, IndexedDB records, Blobs, and host paths never enter
Kisak structures or traces.

Each engine tab holds a shared Web Lock for the lifetime of its read-only sync
handles. Import replacement and removal broadcast a release request, unmount
their own Worker, and acquire the same lock exclusively before mutating OPFS.
This keeps the existing multi-tab atomic storage contract while proving that
no Worker retains a handle to a directory being collected.

The old cooperative VFS also runs against these mounted handles in the Worker,
but retains its deferred C++ completion pump so Gate 2 remains regression
infrastructure. No Promise crosses its C++ API.

## Canonical database state reached

The shared registry pool unit contains all native pool counts and exact 32-bit
entry strides. `DB_InitAssetPools` performs the native type-order initialization,
zeros the hash table, and builds the native asset-entry chain beginning at
`g_assetEntryPool + 16`; 32,752 entries are available and the first 16 remain
reserved exactly as in Kisak. Twenty-six pool/singleton identities are active
for the SP build. Singleton bodies remain owned by their native subsystems;
the Web prefix retains identities only and does not decode or publish them.

The executed path is:

```text
Com_InitXAssets
DB_InitThread
DB_Thread initialized
DB_LoadXAssets
DB_Init
asset-pool initialization
DB_LoadXZone
DB_TryLoadXFile
DB_TryLoadXFileInternal
DB_BuildOSPath
FS/platform open
zone header read (14 bytes)
zone header/framing validation
stop=DB_LoadXFile/streaming-inflate-closure
```

The browser test uses a freely generated synthetic `code_post_gfx.ff` through
the normal `zone/english/<zone>.ff` path. It validates the eight-byte IWff
magic, version 5, and zlib framing. The same path can consume a legally owned
import; no retail file is committed to the repository.

Win32 x86 and Wasm emit the identical normalized pool contract
`pools=26 free=32752 hash=3659fbb2`, alongside the existing identical
14-stage/3-startup/6-command/22-dvar/128-MiB startup prefix. The Worker browser
test separately asserts the ordered DB stages above, logical path, 14-byte
read, and framing result without exposing a host path or storage identity.

## Exact next blocker

The stop is immediately before integrating `DB_LoadXFile` and
`DB_LoadXFileInternal` beyond framing. Native `db_file_load.cpp` uses
`ReadFileEx`, `OVERLAPPED`, alertable `SleepEx`, Win32 handle sizing/closing,
then zlib authentication, XFile block sizes, PMem zone allocation, stream
initialization, generated asset loaders, delayed images, and final publication.

The next slice should make that translation unit consume the synchronous
platform file handle while retaining its double-buffer/order contract, then
compile `db_memory.cpp`, `db_stream.cpp`, `db_stream_load.cpp`, and the first
generated load closure needed for the XFile/asset-list prefix. It should stop
at the next missing canonical asset family rather than route through the
census. The current architecture already supplies the required Worker-hosted
synchronous filesystem boundary; no additional Worker is required yet.
