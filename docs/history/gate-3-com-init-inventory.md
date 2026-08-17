# Gate 3 `Com_Init` inventory

This inventory records the second executable Gate 3 runtime slice. The browser
target now executes canonical qcommon command behavior and the native physical
memory/database-initializing prefix. It does not claim that the Kisak database
thread, filesystem, asset pools, or zone loader are running.

## Executed boundary

`KisakCOD-web` compiles `src/qcommon/common.cpp` with the temporary
`KISAK_GATE3_COM_INIT_PREFIX` link envelope. The envelope remains a strict
incremental boundary: it excludes later client, server, filesystem, database,
D3D, sound, and Win32 references, and it contains no Promise, callback, polling,
Asyncify, or retail-census path.

The executed canonical prefix is:

```text
Com_Init
  -> setjmp(Sys_GetValue(2))
  -> Com_Init_Try_Block_Function
     -> Com_ParseCommandLine
     -> SL_Init
     -> Swap_Init
     -> Cbuf_Init
     -> Cmd_Init
     -> Com_StartupVariable
     -> Com_InitDvars
     -> CCS_InitConstantConfigStrings
     -> PMem_Init
     -> DB_SetInitializing(true)
     -> PMem_BeginAlloc("$init", PHYS_ALLOC_HIGH)
     -> reach Com_InitXAssets call boundary
     -> STOP before DB_InitThread / Sys_SpawnDatabaseThread
```

The exact furthest executed canonical call is `PMem_BeginAlloc("$init", 1)`.
The trace then records that the next canonical call site is `Com_InitXAssets`;
its native body is one call to `DB_InitThread`, which in turn immediately calls
`Sys_SpawnDatabaseThread(DB_Thread)`. The Gate envelope does not replace that
thread with a no-op, cooperative task, census call, or main-thread DB shim.

## Deterministic native/Wasm checkpoint

The strict Emscripten/Node executable and Win32 x86 executable run the same
source closure and assert the same semantic state:

```text
gate3-trace stages=14 startup=3 commands=6 dvars=22 pmem=134217728 stop=DB_InitThread/WorkerHostedDatabase
commands=wait,vstr,exec,cmdlist,seta,set
pmem.low=0 pmem.high=134217728 pmem.highScopes=1 db.initializing=true
```

The 14 stages include the terminal `stop` event. The command list is the exact
linked-list order after native `Cmd_Init` and dvar command registration. The
tests also compare startup `set`/`seta`, all 22 prefix dvars, and representative
defaults and flags. Addresses and platform handles are never traced.

The shared command probe additionally verifies:

- exact command lookup and case-insensitive dispatch;
- limited tokenization and argument access;
- semicolon/newline command-buffer ordering and frame-style `wait` behavior;
- explicit add/find/remove lifetime; and
- startup command application before the memory/database boundary.

## Command-system convergence

Production Wasm and both strict trace targets now compile canonical
`src/qcommon/cmd.cpp`; `cmd_core.cpp` has been removed after the complete local
native, Wasm, and browser matrix passed. Its history remains available in Git,
and there is no second production command implementation.

Narrow Gate-prefix exclusions in canonical `cmd.cpp` are:

| Excluded surface | Owning future subsystem |
| --- | --- |
| `dumpraw` developer command | Full database/filesystem developer tooling |
| `Scr_MonitorCommand` and SP command notifications | Script VM/game runtime |
| server command buffer and `SV_GameCommand` forwarding | Server/game runtime |
| client command forwarding | Client runtime |
| filesystem autocomplete | Synchronous engine filesystem |
| `exec` disk/fastfile reads | Synchronous filesystem and canonical RawFile DB |
| memory-tracker/profile hooks | Native profiling/tracking closure |

`exec` remains registered in native order, but its Gate body fails explicitly
if invoked before its owning filesystem/DB boundary. It does not read from the
asynchronous browser VFS or the census. The canonical execution loop also now
examines the final linked-list entry, preserving the native command-dispatch
contract rather than silently skipping the oldest command.

## Physical memory ownership and semantics

`src/universal/physicalmemory.cpp` is newly compiled by production Wasm. The
Kisak-visible contract remains the canonical 128 MiB arena:

- one persistent backing allocation;
- page-aligned (`4096`) base;
- low allocations round upward and high allocations round downward;
- the two ends collide deterministically;
- named allocation scopes retain canonical begin/end/free and hole rules; and
- exhaustion reaches the engine fatal/error boundary.

Only backing-memory acquisition is platform-owned. Web uses checked aligned
linear-memory allocation through `Sys_AllocatePhysicalMemory`; normal Windows
builds retain `VirtualAlloc`. The Web/Gate path validates allocation-end index,
power-of-two alignment, addition/subtraction overflow, and low/high collision
before changing an arena cursor. Wasm uses memory growth only to make room for
the canonical fixed arena; PMem itself is not replaced by a browser allocator.

The native and Wasm semantic probe performs aligned 100-byte low and high
allocations, observes positions `100` and `0x07ffff80`, ends/frees both named
scopes back to `0`/`0x08000000`, and confirms that a one-byte-over-capacity
request follows deterministic failure behavior.

## Database initialization envelope

The real `g_initializing` state and `DB_SetInitializing` implementation are
isolated in `database/db_initialization.cpp` so the strict prefix can link the
canonical state transition without pulling the complete Win32 database graph.
Normal native builds consume the same definition; `db_registry.cpp` remains
its reader and owner of later database behavior.

Reached database state:

```text
g_initializing = true
$init high PMem scope active at the untouched high boundary
```

Not reached: `DB_InitThread`, `DB_Thread`, `DB_Init`, asset-pool header
initialization, the 32,767-entry free asset chain, `DB_LoadXAssets`,
`DB_LoadXZone`, or any zone/file read. Calling `DB_Init` early merely to report
more progress would violate native order: it is lazily entered by the first
`DB_LoadXAssets` call.

## Worker and synchronous-filesystem boundary

The immediate blocker is earlier than the first file open but is already a
genuine database-hosting boundary:

```text
Com_InitXAssets
  -> DB_InitThread
     -> Sys_SpawnDatabaseThread(DB_Thread)       <-- current stop
```

Once zones are requested, the first actual synchronous fastfile open is:

```text
DB_LoadXAssets
  -> DB_LoadXZone
  -> DB_Thread / DB_TryLoadXFile
  -> DB_TryLoadXFileInternal
  -> DB_BuildOSPath[_Mod]
  -> CreateFileA(... zone/<language>/<name>.ff ...)  <-- native file boundary
  -> GetFileSize
  -> PMem_BeginAlloc(zone->name, allocType)
  -> DB_LoadXFile
```

The next slice therefore requires a Worker-hosted synchronous engine
filesystem boundary. The minimum architecture is:

1. keep file selection, import, and asynchronous IndexedDB/OPFS staging on the
   DOM main thread;
2. start the Kisak Wasm runtime in a dedicated Worker, with its own error slots,
   PMem, command buffer, DB state, and OffscreenCanvas ownership;
3. expose imported immutable files to that Worker through synchronous OPFS
   access handles (or a rigorously bounded pre-staged synchronous file image),
   with Kisak-style open/read/seek/size/close semantics;
4. provide the real database execution context required by
   `Sys_SpawnDatabaseThread`, including wake/wait/notify/sync behavior, before
   entering `DB_InitThread`; and
5. keep UI/storage messages outside qcommon and DB—no Asyncify, Promise
   propagation, polling loop, or Killhouse-specific preload.

Whether the canonical DB context uses an Emscripten pthread/secondary Worker or
a separately justified single-engine-Worker scheduling adaptation is the next
design decision. Pthreads would require `SharedArrayBuffer`, cross-origin
isolation, and deployment changes and must not be enabled implicitly.

## Preserved Gate 2 and temporary oracle

The canonical GfxWorld loader, bounded Killhouse WebGL2 draw, canonical asset
loaders, launcher/storage behavior, and retail census regression oracle remain
unchanged. `web_qcommon_preinit.*` and `web_qcommon_runtime.*` also remain only
as regression infrastructure for the existing asynchronous VFS header probe;
they gained no canonical startup or DB responsibility.

The known diagnostic discrepancy remains recorded and was not investigated:

```text
native: wc/decal_porterjustice8
Wasm:   wc/me_cinderblock_wall2_top
```
