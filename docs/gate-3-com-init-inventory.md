# Gate 3 `Com_Init` inventory

This inventory records the first executable Gate 3 runtime slice. The browser
target now enters the canonical qcommon translation unit and advances in native
order through constant-config-string initialization. It deliberately stops
before physical-memory/database initialization; it does not claim to boot the
game or load a zone through the Kisak runtime.

## Executed boundary

`KisakCOD-web` compiles `src/qcommon/common.cpp` with a temporary
`KISAK_GATE3_COM_INIT_PREFIX` compile envelope. The envelope exists to keep
post-boundary client, server, filesystem, database, D3D, sound, and Win32
references out of this strict incremental link. It is compiled for both Win32
x86 and Wasm, contains no browser lifecycle or asynchronous state machine, and
must shrink and ultimately disappear as the canonical dependency closure
advances.

The executed order is:

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
     -> STOP before PMem_Init / DB_SetInitializing
```

No stage is skipped to reach a later call. The stop is immediately before the
native `IsFastFileLoad()` branch would enter `PMem_Init`,
`DB_SetInitializing(true)`, and `$init` allocation. `FS_InitFilesystem` is
later in the same canonical function and is also intentionally unreachable.

The production launcher supplies a mutable startup line containing `set` and
`seta`, initializes `Dvar_Init`, main-thread identity, `va_info`, and the
slot-2 `jmp_buf`, and then calls `Com_Init`. The browser event
`kisakcod:gate3-init` publishes the normalized result only after the stop is
reached.

## Semantic checkpoint

The same source closure executes in a strict Emscripten/Node target and a
Win32 x86 target. Both currently report:

```text
gate3-trace stages=10 startup=3 commands=4 dvars=22 stop=PMem_Init/DB_SetInitializing
```

The trace records the ten ordered stage labels, the stop stage, registered
command count, and the 22 dvar names registered by the prefix. Tests also check
representative defaults and flags, including `useFastFile`, `developer`,
`com_timescale`, `sys_smp_allowed`, and the archived startup dvar. It contains
no addresses, OS handles, or browser objects.

`tools/build_web.ps1` builds and runs the dedicated
`check-gate3-com-init` target on every browser build. That target retains
`ERROR_ON_UNDEFINED_SYMBOLS=1`; the checkpoint cannot pass by accepting an
unresolved closure.

## Newly shared or adapted engine code

The production Wasm target now compiles these additional Kisak translation
units:

- `qcommon/common.cpp` and the normalized `qcommon/com_init_trace.cpp`;
- `script/scr_memorytree.cpp` and `script/scr_stringlist.cpp`;
- `universal/com_constantconfigstrings.cpp`;
- the canonical `universal/dvar.cpp` and `universal/dvar_cmds.cpp`;
- `universal/q_shared.cpp` and `universal/com_shared.cpp`; and
- the exact shared-string allocation operations extracted into
  `universal/com_memory_string.cpp`.

The canonical dvar implementation replaces `universal/dvar_core.cpp` in the
production Web target. Gate-prefix compilation excludes dvar persistence,
filesystem parsing, localization-only commands, and memory-tracker reporting
that are not reachable before the stop; those functions remain unchanged for
normal native builds. The reduced `qcommon/cmd_core.cpp` is still temporary.
Only its canonical `Cbuf_Init`, `Cmd_Init`, tokenization, `set`, and `seta`
closure is used here. Full `cmd.cpp` remains the main command-system convergence
task for the next slice.

## Portable and platform boundaries

Shared qcommon headers no longer unconditionally require x86 SSE or Windows
intrinsics. `SnapFloatToInt` keeps `_mm_cvtss_si32` on native Windows x86/x64
and uses `std::nearbyint` on Wasm and other portable targets. Native and Wasm
tests cover its rounding behavior. Serialized/database structures are
unchanged.

The Web target explicitly defines `KISAK_WEB=1` and `KISAK_SP=1`. It does not
inherit `WIN32` and does not enable D3D9, Miles, Bink, Steam, raw sockets, or a
native server thread.

The browser-owned platform implementations are narrow:

- `qcommon/system.h` declares critical-section, CPU-count, value-slot, sleep,
  and fatal-error services without requiring renderer headers;
- `qcommon/thread_context.h` owns engine thread identities independently of
  D3D backend declarations;
- `web_thread_context.cpp` provides one main-thread context, four engine-visible
  value slots, deterministic critical-section semantics, and explicitly false
  render/server-thread identities;
- `web_system.cpp` owns `Sys_Print` and `Sys_Error`, while `common.cpp` owns
  `Com_PrintMessage`/`Com_Printf` formatting and ordering; and
- `web_assertive.cpp` routes shared assertions to the platform fatal-error
  sink.

This preserves the semantic role of `Sys_GetValue(2)` and `setjmp`/`longjmp`
without putting DOM error flow into qcommon. The first target stays
single-threaded and does not imply pthread or Worker-thread availability.

## Remaining canonical order

| Next phase | Native owner | Current blocker/decision |
| --- | --- | --- |
| `PMem_Init`, `DB_SetInitializing`, `PMem_BeginAlloc` | physical memory and database runtime | Requires a measured Wasm memory-owner design and real DB allocation semantics. Do not substitute the retail census. |
| `Com_InitXAssets` / DB thread startup | canonical database | Must converge on `DB_LoadXZone` and canonical ownership; no browser parser shortcut is permitted. |
| `CL_InitKeyCommands` | client command system | Requires the full canonical command closure, beginning with replacing `cmd_core.cpp`. |
| `FS_InitFilesystem`, configs, `Cbuf_Execute` | Kisak filesystem/config system | Engine calls are synchronous-looking, while the current DOM-host VFS boundary is asynchronous. The preferred resolution remains a Worker-hosted engine with staged browser I/O, not promises in qcommon or automatic Asyncify. |
| hunk, script, xanim/DObj, server/client, renderer/audio | shared runtime plus platform backends | Out of scope until the memory/DB/filesystem boundary is designed and the preceding order runs. |

The immediate next slice should inventory and implement the smallest real
`PMem_Init`/database allocation envelope while compiling more canonical
`cmd.cpp`. It should stop again before initiating zone I/O unless a documented
synchronous Worker filesystem boundary is available.

## Temporary bootstrap and Gate 2 oracle

`web_qcommon_preinit.*` and `web_qcommon_runtime.*` remain regression
infrastructure. The real prefix now reaches a stronger canonical ordering
checkpoint, but it does not yet perform their bounded browser-VFS header probe,
so retiring them would remove coverage rather than eliminate a duplicate
runtime. They must not gain new engine lifecycle behavior and should be removed
once the real FS/DB path provides an equivalent observable boundary.

Gate 2 loaders, canonical `GfxWorld`, the bounded Killhouse adapter, WebGL2
draw, and retail census tests remain unchanged and green. The diagnostic
material discrepancy remains unresolved and visible:

```text
native: wc/decal_porterjustice8
Wasm:   wc/me_cinderblock_wall2_top
```

No name was hard-coded and no translation table was added.
