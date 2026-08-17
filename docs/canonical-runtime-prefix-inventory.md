# Canonical runtime prefix retirement inventory

The current Wasm runtime compiles canonical command/dvar behavior, PMem,
database pools, XFile streaming, generated RawFile, PhysPreset,
MaterialTechniqueSet, Material, GfxImage, water, LocalizeEntry, SoundCurve,
sound alias, LoadedSound, and Font loading, and
canonical FxEffectDef loading, and
the browser-SP lifecycle slice owned by the real `db_registry.cpp` translation
unit. Remaining extraction files are shrink-only integration mechanisms, not
browser-owned engine layers.

## `db_runtime_prefix.cpp`

The following former duplicates have been deleted from this file and are now
compiled under canonical `db_registry.cpp` ownership:

- `DB_BuildOSPath`
- `DB_TryLoadXFileInternal`
- `DB_TryLoadXFile`
- `DB_Thread`
- `DB_LoadXZone`
- `DB_LoadZone_f`
- `DB_InitThread`
- `DB_LoadXAssets`

Their browser-SP closure retains native request order, flags, logical DB thread
context, zone records, PMem scope, XFile call order, and sync completion.
Native mod/reorder, renderer archive, zone unload, lost-device, and the
infinite OS-thread wait loop remain gated until their owning subsystems
compile. Synchronous open/read/size/close and path construction are below
`db_file_platform.h`; shared DB units no longer include web filesystem headers.

Temporary deterministic trace/failure scaffolding comprises `Trace`, `Stop`,
`CaptureStreamState`, `CurrentScriptIdentity`, every `DB_RuntimeTrace*` and
`DB_RuntimeSet*` function,
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
