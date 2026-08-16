# Gate 3 `Com_Init` inventory

This inventory starts the Gate 3 runtime pivot. It records the current native
startup sequence, the browser substitutes that must be retired, the first
observed Wasm compile blockers, and the intended platform seams. It does not
change loader, database, or runtime behavior.

## Current boundary

The web target does not compile `src/qcommon/common.cpp` and does not call
`Com_Init`. It currently compiles:

- `src/qcommon/cmd_core.cpp` instead of the canonical `cmd.cpp`;
- `src/universal/dvar_core.cpp` instead of the canonical `dvar.cpp`;
- `web_qcommon_preinit.*`, a deterministic header-probe state machine; and
- `web_qcommon_runtime.*`, the browser-VFS adapter that advances that machine
  through the cooperative frame scheduler.

`web_main.cpp` separately calls `Dvar_Init`, `Cbuf_Init`, and `Cmd_Init` for a
small command/dvar smoke path. After a validated import, the temporary qcommon
machine allocates a 256 KiB arena, initializes a 64-entry event queue, registers
five web startup dvars, and checks 26 allowlisted file headers. Its terminal
`pre-database` state is an ordering and I/O proof; it is not an implementation
of any native `Com_Init` prefix.

The temporary machine remains useful as a regression oracle until real
`Com_Init` reaches an equivalent observable boundary. It must not become the
permanent engine lifecycle.

## Native entry envelope

The single-player Win32 entry point performs this work before `Com_Init`:

1. initializes critical sections and main-thread state;
2. initializes memory tracking and localization;
3. calls `Com_InitParse` and `Dvar_Init`;
4. initializes timing, system information, and profiling; and
5. calls `Com_Init` with a mutable command-line buffer.

Steam setup, splash windows, process-instance checks, and Win32 window-class
registration are native host concerns and do not belong in the browser port.
The parser, dvar, timing, thread-context, and profiling prerequisites require a
shared or web-platform implementation before the real call is safe.

`Com_Init` itself establishes the engine error boundary with the `Sys_GetValue`
slot-2 `jmp_buf`, calls `Com_Init_Try_Block_Function`, applies startup commands,
runs error cleanup, and finally starts renderer/hunk users when no server is
running. The browser currently has no `Sys_GetValue`/thread-context equivalent
for that error boundary.

## Canonical initialization order

The important order in `Com_Init_Try_Block_Function` is:

| Phase | Canonical calls | Current web status |
| --- | --- | --- |
| Core parsing | `Com_ParseCommandLine`, `SL_Init`, `Swap_Init` | Command-line parsing is uncompiled; string-list initialization is uncompiled; byte-swap initialization is uncompiled. |
| Commands and dvars | `Cbuf_Init`, `Cmd_Init`, `Com_StartupVariable`, `Com_InitDvars`, `CCS_InitConstantConfigStrings` | Reduced command/dvar cores exist, but the real implementations and `Com_Init` sequencing are uncompiled. |
| Database allocation envelope | `PMem_Init`, `DB_SetInitializing`, `PMem_BeginAlloc`, `Com_InitXAssets`/`DB_InitThread` | Canonical database structures are used by the Gate 1/2 loader, but these runtime owners are uncompiled. |
| Filesystem and configs | `CL_InitKeyCommands`, `FS_InitFilesystem`, `Con_InitChannels`, `Com_StartupConfigs`, `Cbuf_Execute` | The browser VFS is asynchronous at its host boundary. Native synchronous filesystem/config behavior is uncompiled. |
| Hunk and diagnostics | `Com_InitHunkMemory`, `Hunk_InitDebugMemory`, `ProfLoad_Init`, command registration, version dvars | Uncompiled. Native memory sizing and profile assumptions need an explicit Wasm inventory. |
| System and script runtime | `Sys_Init`, `Scr_InitVariables`, `Scr_Init`, `Com_SetScriptSettings`, `XAnimInit`, `DObjInit` | Browser timing/logging exist; the system entry contract, script VM, xanim, and DObj runtime are uncompiled. |
| Game/client runtime | `SV_Init`, `CL_InitOnceForAllClients`, `CL_Init` | Uncompiled. Gate 3 should preserve the single-player order rather than invent a browser lifecycle. |
| Platform backends | `SND_InitDriver`, `R_InitThreads`, `CL_InitRenderer`, `SND_Init`, `SV_InitServerThread` | WebGL2 resource ownership exists, but the Kisak renderer frontend is uncompiled. Audio and server-thread adapters are absent. |
| Completion | intro command, `PMem_EndAlloc`, `DB_SetInitializing(false)`, `com_fullyInitialized` | Uncompiled. Bink playback must remain disabled or use a browser-owned replacement. |

## First Wasm compile probe

The initial read-only syntax probe was:

```text
em++ -std=c++20 -DKISAK_WEB=1 -DKISAK_SP=1 -DdNODEBUG=1 \
  -Isrc -Ideps -fsyntax-only src/qcommon/common.cpp
```

It stops before parsing `common.cpp` because `qcommon.h` unconditionally
includes `xmmintrin.h` and `intrin.h`. Emscripten reports that SSE is not enabled
and cannot supply the next Windows `intrin.h`. `qcommon.h` uses `_mm_cvtss_si32`
for `SnapFloatToInt`; this requires a narrow portable implementation or a
platform-selected intrinsic, not enabling x86 SSE for Wasm.

The next known blockers, once that header is portable, are:

- `common.cpp` directly includes `win32/win_local.h`, `win_net_debug.h`,
  `win_storage.h`, and D3D renderer headers;
- the web target defines only `KISAK_WEB`; the offline target also needs an
  explicit single-player compile identity without inheriting Win32 behavior;
- `web_system.cpp` currently supplies the lightweight `Com_Printf`, while
  `common.cpp` owns the canonical implementation, so linking both would create
  a duplicate symbol;
- canonical `Com_Printf`/logging uses native filesystem locking and Windows CRT
  spellings, which need a platform log/file boundary;
- `Sys_GetValue`, main-thread identity, critical sections, and the initialization
  `jmp_buf` slots currently come from the native thread/system layer;
- filesystem/config and database initialization are synchronous-looking engine
  operations, while the current Wasm module runs on the DOM thread with an
  asynchronous JavaScript VFS;
- the full `Com_Init` translation unit references client, server, script,
  xanim, DObj, sound, renderer, hunk, profiling, and error-cleanup code, and the
  web link intentionally rejects undefined symbols; and
- native renderer, sound, Bink, raw networking, Steam, and server-thread
  implementations cannot be linked as accidental closure.

These are compile and ownership blockers, not reasons to reproduce `Com_Init`
as another browser-only state machine.

## Owned Gate 2 oracle carried into Gate 3

The post-cleanup owned browser run confirmed that canonical
`maps/killhouse.d3dbsp` surface 6077 was submitted as 2,009 vertices and 128
triangles, became a resident 384-index renderer surface, and produced an
instrumented WebGL2 384-index draw. The native and Wasm observations agree on
the world, surface, geometry, and draw but currently report different material
names for that surface: native reports `wc/decal_porterjustice8`, while Wasm
reports `wc/me_cinderblock_wall2_top`. The retired preview path is not involved
in either observation, and the bounded world proof does not use this name to
select a texture. Preserve this mismatch as a Gate 3 native/Wasm regression
oracle; do not conceal it by hard-coding either name or by adding a browser-only
material translation.

## Ownership decisions

Retain or adapt shared Kisak behavior for command-line parsing, command/dvar
semantics, string lists, byte swapping, initialization ordering, memory owners,
database startup, script/xanim/DObj initialization, client/server startup, and
error cleanup.

Keep browser timing, filesystem host calls, worker lifecycle, canvas/context,
audio unlock, persistent storage, and logging sinks behind platform APIs. The
preferred execution model remains a main-thread launcher/file picker plus a
dedicated Worker containing the engine Wasm, a synchronous-style engine
filesystem, and OffscreenCanvas/WebGL2. This inventory does not justify
Asyncify or pthreads.

Do not compile the D3D backend, native Bink/Miles/Steam integrations, or raw
socket transport into Wasm. Do not weaken `ERROR_ON_UNDEFINED_SYMBOLS` to make
the dependency closure appear complete.

## First implementation slice

The first Gate 3 implementation should be deliberately compile-led:

1. make the shared qcommon rounding/intrinsic helpers compile for Wasm without
   changing serialized or native behavior;
2. add an explicit web single-player compile identity and a compile-only
   `common.cpp` inventory target;
3. remove direct platform-header ownership from `common.cpp` through narrow
   system, logging, timing, and thread-context interfaces;
4. reconcile the reduced command/dvar cores with canonical `cmd.cpp`/`dvar.cpp`
   rather than expanding their parallel behavior; and
5. grow the link closure in canonical initialization order, recording each
   genuine platform dependency and stopping before asynchronous filesystem or
   database work is invoked on the DOM thread.

The first runtime checkpoint should come from the real `Com_Init` path and have
a native/Wasm semantic trace for initialization order, command-line startup
variables, dvar registrations, and failure stage. Only after that checkpoint is
stable should `web_qcommon_preinit.*` and `web_qcommon_runtime.*` be retired.

No Gate 3 runtime implementation is included in this inventory pass.
