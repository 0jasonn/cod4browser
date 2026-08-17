# Canonical runtime prefix retirement inventory

The current Wasm runtime compiles canonical command/dvar behavior, PMem,
database pools, XFile streaming, and the generated RawFile loader. Two
temporary extraction files close dependencies that are not yet portable. Both
are shrink-only integration mechanisms, not browser-owned engine layers.

## `db_runtime_prefix.cpp`

Native `db_registry.cpp` already owns these canonical functions, but its full
dependency closure is not yet in the Wasm target:

- `DB_BuildOSPath`
- `DB_TryLoadXFileInternal`
- `DB_TryLoadXFile`
- `DB_Thread`
- `DB_LoadXZone`
- `DB_LoadZone_f`
- `DB_InitThread`
- `DB_LoadXAssets`

They remain temporary duplicates. None could be safely removed in this cleanup
because doing so would sever the current Worker-hosted database path. Each must
be deleted as the corresponding canonical owner and its Sys/FS/thread
dependencies compile.

Temporary deterministic trace/failure scaffolding comprises `Trace`, `Stop`,
`CurrentScriptIdentity`, every `DB_RuntimeTrace*` function,
`DB_RuntimeGeneratedFailure`, `DB_RuntimeGeneratedLoadFailed`,
`DB_RuntimeStreamCanRead`, and `DB_GetRuntimeTrace`. These exist for strict
native/Wasm semantic comparison and should retire with the integration prefix.

`KisakWeb_StartCanonicalDbRuntimeCheck` is the only genuine browser-facing
entry hook in the file. Its eventual owner should be a narrow web platform
adapter; no DOM, Promise, OPFS object, or WebGL type is exposed to canonical DB
structures.

Include audit: the direct `<emscripten.h>`/`EM_JS` dependencies are confined to
this temporary DB prefix and the conditional trace emitter in
`com_init_trace.cpp`. They do not leak into canonical DB types or generated
loaders. Moving both emitters behind a narrow web trace adapter is remaining
cleanup debt; doing it here would have expanded the ABI/linkage change beyond
the measured build sprint.

Rule: add no canonical database behavior here when the native owner can be
compiled. Browser I/O belongs behind Sys/FS/thread interfaces.

## `common_gate3_prefix.inl`

The extraction currently owns the globals required by the linked `common.cpp`
prefix and temporary definitions of:

- `Com_InitXAssets`;
- print/error boundary functions;
- command-line parsing and startup-variable application;
- prefix dvar registration;
- `Com_Init_Try_Block_Function`; and
- `Com_Init`.

It drives the real canonical dependencies through `DB_InitThread` and stops at
the mounted `DB_LoadXAssets` request. The prefix contains no Promise, Asyncify,
pthread, retail-census call, or browser-owned command/dvar implementation.

Rule: do not add unrelated future `Com_Init` behavior. Compile canonical
`common.cpp` dependencies, move functionality back to its native owner, and
delete the corresponding extraction. The end state is normal `common.cpp`, not
`common_web.cpp`.

## Verification

The strict `KisakCOD-web-runtime-prefix-check` executable and the native/Wasm
canonical runtime tests compare normalized command, dvar, PMem, DB stream,
pool/hash, pointer/publication, and failure state without platform addresses.
The production target reuses the same runtime-prefix object library; the strict
check compiles only its dedicated test translation unit.
