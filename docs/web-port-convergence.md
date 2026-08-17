# Web-port convergence report

## Purpose

This report tracks whether the browser target is converging on KisakCOD engine
code or accumulating browser-only engine replacements. It is an architectural
inventory, not a claim that listed code already boots the game.

The governing rule is:

> Browser-specific intermediate representations exist only at genuine platform
> boundaries. Prefer compiling and executing KisakCOD engine behavior wherever
> the browser platform allows it.

The desired path is:

```text
COD4 fastfile
     -> Kisak database/asset loader
     -> Kisak XAsset / XModel / Material / GfxWorld
     -> Kisak client and game systems
     -> Kisak renderer frontend
     -> portable draw commands
     -> WebGL2 backend
```

## Classification

| Status | Meaning |
| --- | --- |
| `SHARED KISAK` | The web target compiles the same portable Kisak implementation used by native targets. |
| `MODIFIED KISAK` | Kisak code or behavior is shared but has a narrow, intentional portability adaptation. |
| `WEB PLATFORM IMPLEMENTATION` | Browser-owned code at a genuine OS, API, or graphics-backend boundary. This category is permanent. |
| `TEMPORARY WEB SUBSTITUTE` | Bootstrap or validation code standing in for an engine system. It needs a convergence or retirement path. |
| `NATIVE ONLY` | A native implementation exists but cannot be used as-is because it owns a platform-specific boundary. |
| `NOT COMPILED` | Kisak source exists in the repository but is not part of `KisakCOD-web`. |

A component may have a canonical native implementation and a temporary web
substitute at the same time. The table calls this out rather than treating the
substitute as convergence.

## Snapshot

Snapshot baseline: branch `web-port`, through the canonical Worker/database
lifecycle and generated RawFile, PhysPreset, MaterialTechniqueSet, Material,
GfxImage, water, LocalizeEntry, SoundCurve, sound aliases, LoadedSound, plus Font
publication, while retaining
complete canonical Killhouse `GfxWorld` publication at asset 772 and one
bounded real-world WebGL2 draw in the explicit Gate 2 oracle.

The production web target contains 72 C/C++ translation units: 40 outside
`src/web` and 32 inside it. The runtime prefix includes real `common.cpp`, canonical
dvar and command implementations, real physical memory, database-initializing
state, string-list/memory support, constant config strings, and shared qcommon
utilities. This is only a source-inventory
baseline; it is not
a quality metric by itself because filesystem, lifecycle, and WebGL code should
remain platform-owned.

The last successful prerequisite traversal completes all 6,502 ordered assets
in `common.ff`. Its type-7 run begins at asset 4,778 and retains 1,723 sound
table rows backed by 1,716 unique canonical `snd_alias_list_t` objects. The
cross-zone index then lets `killhouse.ff` publish its canonical type-23
`WeaponDef` run beginning at asset 458 (`winchester1200`). Ordered traversal no
longer stops at that validation boundary: it completes assets 0-771 and
continues from that exact retained state through the complete generated-loader
order for `GfxWorld` 772. The published result contains 218 technique sets, 325
XModels (including five inline static-model dependencies), 60 FX effects, 146
canonical `XAnimParts`, 10 WeaponDefs, 21 canonical RawFiles, and the canonical
type-12 `ComWorld` for `maps/killhouse.d3dbsp` with 24 primary lights. Asset 705
is the canonical `GfxLightDef` `light_point_linear`; its attenuation pointer
resolves to canonical `GfxImage` identity 1,773. Assets 706-771 are already
supported technique sets. The world contains 448,962 vertices, 829,539 indices,
8,694 surfaces, three cells, three lightmaps, and 12,255 static models. Its
final inflated cursor is 86,162,172; block-0 high-water is 8,389,392, block-1
high-water is 509,456, block-4 cursor is 37,147,366, and the registry reports
2,371 assets with 2,479 defined aliases. No seek, rewind-to-world, or
reconstructed registry is used.
RawFiles 395, 396, 398, 400, 402, and 404 are published through the canonical
Kisak type. XModel, Material, and FX publications now also expose stable
canonical top-level objects, while their retained nested census/dependency
records remain temporary convergence scaffolding. Preview render graphs have
been removed.

### Runtime/oracle separation

Normal launcher startup mounts the validated import, starts qcommon, and runs
the canonical database/runtime check. It leaves the census, archive probe,
engine-asset proof, canonical retail world, and bounded Killhouse draw idle.
`runtime.startGate2Oracle()` explicitly enables that retained diagnostic path;
every dependent browser test requests it. The oracle remains linked for now so
the same production artifact can prove compatibility, but its sources are
isolated in the `kisak_web_gate2_oracle` static library.

Gate 2-only production sources are the retail census/load dispatcher,
archive/engine-asset job chain, sound catalog/job, and their bounded renderer
proof. They are frozen regression infrastructure. Canonical DB/runtime sources
must not call them. The eventual split is a runtime artifact without that
library plus a diagnostic/test artifact that retains it, once canonical DB can
reproduce the same asset/world evidence.

### Prefix, build, and test ownership

`db_runtime_prefix.cpp` and `common_gate3_prefix.inl` carry source-level
shrink/no-growth contracts. Their exact functions, native owners, platform
hook, and deletion path are listed in
[`canonical-runtime-prefix-inventory.md`](canonical-runtime-prefix-inventory.md).
All eight duplicated DB lifecycle functions have moved out of
`db_runtime_prefix.cpp`; the real `db_registry.cpp` translation unit now owns
their browser-SP compile slice. The prefix retains trace/failure state and the
exported browser start hook.

The production target and strict runtime check reuse one
`kisak_web_runtime_prefix` object library. The strict executable compiles only
its dedicated test translation unit instead of recompiling thirteen runtime
units. Compatible native/Wasm portable tests similarly reuse source-stream,
zone-stream, registry, surface, shader, and semantic-trace libraries;
sanitizer/fuzzer objects remain separately instrumented.

Routine browser ownership is split into disjoint `@smoke` and
`not @smoke and not @native-covered` tiers. Parser corruption, stream bounds,
PMem/pool exhaustion, generated-loader semantics, ABI, and deterministic
traces are authoritative in direct native/Wasm tests. Browser E2E retains one
Worker failure-propagation proof and owns lifecycle, storage, bridge, canvas,
and WebGL seams. The exhaustive browser suite remains explicitly available.

## System inventory

| System | Current status | Evidence and convergence action |
| --- | --- | --- |
| Emscripten build and packaging | `WEB PLATFORM IMPLEMENTATION` | Explicit `web` target, pinned toolchain, generated site, and HTTP server. Keep isolated from native target selection. |
| Browser launcher and legal asset selection | `WEB PLATFORM IMPLEMENTATION` | DOM file picker, exact-path validation, local manifest, and restore UI are genuine browser responsibilities. |
| OPFS/IndexedDB asset persistence | `WEB PLATFORM IMPLEMENTATION` | Keep storage asynchronous in the host and hidden behind the filesystem boundary. |
| Wasm filesystem bridge | `WEB PLATFORM IMPLEMENTATION` / partial | The engine Worker mounts every validated manifest entry through synchronous OPFS access handles and exposes logical open/size/seek/read/close semantics to C/C++. Import IDs and browser handles remain platform-only. The older cooperative VFS now uses the same Worker mount while retaining deferred C++ completions for Gate 2. |
| Browser lifecycle, logging, and timing | `WEB PLATFORM IMPLEMENTATION` | Event-loop and page-lifecycle handling remain in `src/web`. `common.cpp` now owns engine print formatting while `Sys_Print`/`Sys_Error` remain narrow platform sinks. |
| System/thread context | `WEB PLATFORM IMPLEMENTATION` / partial | KisakCOD Wasm now runs in a dedicated engine Worker. The first DB model is synchronous within that Worker but enters a distinct logical `THREAD_CONTEXT_DATABASE`, preserves separate value slots plus wake/completion ordering, and reports no render/server thread or pthread concurrency. |
| Cooperative scheduler | `TEMPORARY WEB SUBSTITUTE` | It currently advances bootstrap jobs and protects the main thread. Retain it for the present traversal, but do not spread its state machines through shared engine code. Long term, stage browser I/O outside a Worker-hosted synchronous-looking engine. |
| Command system | `MODIFIED KISAK` / partial | Production Wasm and strict trace tests now compile canonical `src/qcommon/cmd.cpp`; `cmd_core.cpp` is retired from all build lists. Native registration/lookup, tokenization, argument access, buffering, `wait`, startup commands, and removal lifetime are compared under Win32 x86 and Wasm. Script/server/client forwarding, developer tooling, autocomplete, and filesystem/fastfile `exec` remain gated with their owning subsystems. |
| Dvar system | `MODIFIED KISAK` / partial | Production Wasm now compiles canonical `src/universal/dvar.cpp` and `dvar_cmds.cpp`; `dvar_core.cpp` is retired from that target. Persistence, file parsing, localization-only commands, and tracking are gated beyond the current prefix and must return with their owning subsystems. |
| qcommon startup | `MODIFIED KISAK` / partial plus `TEMPORARY WEB SUBSTITUTE` oracle | Real `common.cpp` executes through `PMem_Init`, `DB_SetInitializing(true)`, `$init`, `Com_InitXAssets`, and `DB_InitThread`. Native-test/Wasm traces match through the new call and stop pending the first mounted `DB_LoadXAssets` request. The old browser pre-database shell remains regression infrastructure only. |
| Physical memory | `MODIFIED KISAK` / platform-owned backing | Production Wasm compiles canonical `physicalmemory.cpp` and its fixed 128 MiB two-ended arena. Only page-aligned backing acquisition is browser-owned; explicit 32-bit overflow/collision checks protect linear memory. Native/Wasm tests compare alignment, cursor order, named-scope reset, and failure. |
| Database initialization | `MODIFIED KISAK` / partial | The web target compiles real `db_registry.cpp` ownership for `DB_BuildOSPath`, `DB_TryLoadXFileInternal`, `DB_TryLoadXFile`, `DB_Thread`, `DB_LoadXZone`, `DB_LoadZone_f`, `DB_InitThread`, and `DB_LoadXAssets`. It then executes through XFile inflate, PMem blocks, streams, generated list/string/asset dispatch, RawFile, PhysPreset, MaterialTechniqueSet, Material, GfxImage, water, LocalizeEntry, SndCurve, sound aliases, LoadedSound, Font, and FxEffectDef. Narrow synchronous file and Sys context adapters own the platform boundary. Retail `code_post_gfx.ff` publishes 1,243 assets and reaches asset 1225 type 26 before `Load_FxImpactTablePtr`. |
| Canonical database asset ABI | `SHARED KISAK` / partial | `RawFile`, `XAssetHeader`, `XAssetType`, and `XAsset` remain renderer-free. The canonical 44-byte `PhysPreset` is shared from `physics/phys_preset.h`. Canonical Material/TechniqueSet records, texture and constant definitions, state bits, water, flexible techniques/passes, shader definitions, and arguments are shared from `gfx_d3d/material_types.h`; GfxImage/load-definition records remain in `gfx_image_types.h`. Native x86/Wasm tests cover exact 32-bit sizes and offsets. Canonical `XAnimParts`, `WeaponDef`, `LocalizeEntry`, XModel, FX, collision, world, and light structures remain shared lightweight ABI declarations. |
| IWD/ZIP reading | `MODIFIED KISAK` / partial | The bounded reader is portable and tested, but final integration should be through Kisak filesystem/database calls rather than a preview-only archive job. |
| IWI decoding | `MODIFIED KISAK` / partial | Bounded DXT decoding is reusable. Connect it to canonical `GfxImage` loading and renderer upload instead of browser material queues. |
| Fastfile framing and zone stream machine | `TEMPORARY WEB SUBSTITUTE` | It accurately models blocks, rewind/high-water behavior, pointer classes, aliases, and bounded streaming. Use it as differential evidence and migrate reusable mechanics toward the Kisak DB loader. |
| Asset registry | `MODIFIED KISAK` / partial plus `TEMPORARY WEB SUBSTITUTE` oracle | Generated RawFile, PhysPreset, MaterialTechniqueSet, Material, GfxImage, LocalizeEntry, SndCurve, sound alias, LoadedSound, Font, and FxEffectDef paths consume the canonical 32-bit asset-entry pool, per-type pools, free chain, normalized hash table, and zone ownership through `DB_AddXAsset`/`DB_LinkXAssetEntry`. Owned retail publishes 1,243 records through entry 1,258, with free entries 32,752 -> 31,509. Gate 2 remains a frozen oracle and is not called by the generated path. |
| Retail loader dispatcher | `TEMPORARY WEB SUBSTITUTE` / frozen Gate 2 oracle | `web_retail_fastfile_census.*` remains the orchestration vehicle, but normal startup no longer executes it. The source has an explicit freeze contract and is isolated with its diagnostic dependencies in `kisak_web_gate2_oracle`; canonical DB code must not call it. It remains linked only until canonical DB can reproduce equivalent asset/world evidence. |
| `clipMap_t` asset loading | `MODIFIED KISAK` / partial | The dedicated family transcribes the 284-byte `Load_clipMap_t` record, block-4 child order, block-1 zero-fill dynamic client allocations, root insertion/alias handling, bounded ownership, and atomic canonical publication for `col_map_sp`/`col_map_mp`. Synthetic MSVC/Wasm coverage exercises empty and populated child graphs under one-byte traversal budgets. Inline DynEntity dependency bodies remain fail-closed until their canonical families are compiled; no `CM_LoadMap` or collision runtime behavior is included. Type 12 `com_map` remains correctly distinct from `clipMap_t` and is now handled by the canonical ComWorld family. |
| `ComWorld` asset loading | `MODIFIED KISAK` / partial | `web_retail_load_comworld.*` transcribes native `Load_ComWorldPtr`, `Load_ComWorld`, `Load_ComPrimaryLightArray`, and `Load_ComPrimaryLight`: root null/inline/shared/prior-alias handling, block-0 body allocation, block-4 name and 68-byte light array, per-light `defName` XStrings, checked ceilings, and publication only after the complete body. The owned run publishes asset 704 as canonical `ComWorld`; no light rendering, evaluation, collision, gameplay, or `GfxWorld` behavior is present. |
| `GfxLightDef` asset loading | `MODIFIED KISAK` / partial | `web_retail_load_lightdef.*` transcribes `Load_GfxLightDefPtr`, `Load_GfxLightDef`, and `Load_GfxLightImage`: four-byte root cells, null/inline/shared/prior aliases, block-0 16-byte bodies, block-4 names, embedded 8-byte light images, canonical `GfxImage*` dependencies, insertion cells, and final-only publication. Killhouse asset 705 publishes as `light_point_linear`; no rendering, attenuation evaluation, shadow behavior, or primary-light linking is implemented. |
| `XAnimParts` asset loading | `MODIFIED KISAK` / partial | The bounded path mirrors native `Load_XAnimPartsPtr` / `Load_XAnimParts`: block-0 body allocation, optional shared insertion cell, block-4 name and payload scope, exact array order, low/high-frame index widths, and flexible delta translation/quaternion storage. It publishes the canonical Kisak structure with ownership-only backing; the owned run publishes assets 437-457. Replace the temporary owner with real zone allocation during DB convergence. |
| `WeaponDef` asset loading | `MODIFIED KISAK` / partial | The canonical header, fixed scalar decode, 40 script strings, all 48 native XStrings, four accuracy arrays, root insertion/alias handling, canonical XModel/Material/FX resolution, sound-name cells, bounce array, bounded ownership, and atomic publication are covered. Direct XStrings now preserve native address semantics, including pointers into earlier character payloads, while bounded compatibility translation is anchored only by generated WeaponDef order. The owned run publishes ten WeaponDefs before `com_map`; inline child bodies remain explicit future work. |
| Sound alias loading and catalog | `MODIFIED KISAK` / partial | The prerequisite dispatcher mirrors the native list/header/component order and owns canonical `snd_alias_list_t`, `snd_alias_t`, `SoundFile`, `LoadedSound`, `SndCurve`, and `SpeakerMap` metadata in the publishing zone. `web_sound_alias_catalog.*` remains a case-insensitive ownership/index seam: it stores pointers to those objects, retains the common-zone owner, collapses native DB name aliases, and uses the indexed canonical `null` sound for native missing-sound fallback. It neither copies nor synthesizes sound records. No payload playback or audio runtime behavior is implemented. |
| `LocalizeEntry` asset loading | `MODIFIED KISAK` / canonical reached closure | The normal generated DB path now owns the renderer-free eight-byte ABI, block-0 body, block-4 value/name XStrings, null/inline/shared/prior root forms, insertion cells, real pool/hash/zone ownership, and final-only publication. Owned `code_post_gfx.ff` publishes 1,116 consecutive LocalizeEntry assets before continuing naturally to the SoundCurve boundary. Gate 2 retains independent oracle evidence only. |
| Sound asset loading | `MODIFIED KISAK` / canonical reached closure | The normal generated path owns SndCurve, sound-alias list/entry arrays, streamed SoundFile names, SpeakerMap, LoadedSound payloads and real publication. `DB_PlatformSetLoadedSoundData` is the narrow temporary audio-payload seam and exposes no browser concepts to DB code. The native dispatcher intentionally performs no work for SndDriverGlobals, which is preserved exactly. |
| Font asset loading | `MODIFIED KISAK` / canonical reached closure | The generated path owns the 24-byte Font body, font-name XString, two canonical MaterialHandle dependencies, bounded Glyph array and final type-19 publication. Retail publishes nine Fonts before advancing to FX. |
| FX asset loading | `MODIFIED KISAK` / canonical reached closure | The generated path owns the 32-byte effect, 252-byte element graph, velocity/visual samples, Material/sound/effect visuals, trails, root aliases and final type-25 publication. Inline XModel bodies fail at the exact uncompiled native child boundary. Retail publishes `misc/missing_fx` after its nested Material and GfxImage, then advances to FxImpactTable. |
| `XModel` | `MODIFIED KISAK` / partial | The existing checked traversal now publishes a stable canonical `XModel` top-level object with canonical name, scalar metadata, skeleton-array pointers, and canonical Material handle identity. `RetailWorldXModel` still owns temporary surface, collision, and physics retention; converge those nested graphs before compiling broader consumers. |
| `Material` and techniques | `MODIFIED KISAK` / partial | The canonical generated path owns Material and TechniqueSet bodies, names, tables, techniques/passes, shader records/bytecode, texture/image and water dependencies, constants, state bits, pointer aliases, insertion cells, and dependency-ordered final DB publication. Renderer-disabled hooks retain canonical signatures without D3D objects. Owned retail publishes Material `ui_cursor` after its image dependency at asset 2. Gate 2's broader records remain oracle-only. |
| `GfxImage` | `MODIFIED KISAK` / partial; native backend `NATIVE ONLY` | The reusable generated family now loads 36-byte image bodies, XString names, texture load definitions/payloads, aliases, insertion cells, and canonical DB publication. Owned retail publishes nested `3_cursor3` and top-level `$black_3d`/`$black_cube`. The renderer-disabled `Load_Texture` seam leaves GPU handles null; IWI decoding, WebGL resource creation, and context recovery remain backend infrastructure. |
| `GfxWorld` | `MODIFIED KISAK` / partial | `web_retail_load_gfxworld.*` is a dedicated resumable transcription of `Load_GfxWorldPtr`, `Load_GfxWorld`, and their complete serialized child order. It publishes the canonical `GfxWorld` only after images, materials, cells/portals, lightmaps/grid, vertices/layers, models, shadow/light regions, DPVS static/dynamic, block-1 runtime allocations, and five inline XModel dependencies complete. Important stages emit normalized semantic checkpoints. D3D vertex buffers remain null and runtime texture-slot arrays are zero-filled. `WebEngine_BuildGfxWorldSurface` reads the canonical vertex/index/surface/material pointers directly and copies one bounded real Killhouse surface into the renderer contract; no retained browser scene object is introduced. Gate 2 renderer expansion stops here. |
| XModel/model preview scene | `RETIRED` | Removed after Gate 2: no selectable-model UI/state, retained preview geometry, preview camera/projection, preview material bridge, or multi-draw command path remains. Canonical XModel loading/publication and dependencies remain available to `GfxWorld`, `WeaponDef`, FX, and later runtime consumers. |
| Renderer frontend | `TEMPORARY WEB SUBSTITUTE` / partial | The bounded canonical `GfxWorld` surface still bypasses most Kisak frontend behavior. Introduce portable draw commands behind the Kisak renderer frontend and keep backend handles private. |
| WebGL2 backend and context recovery | `WEB PLATFORM IMPLEMENTATION` | Permanent platform boundary. Preserve resource recreation and fail-safe publication. Do not expose WebGL handles to engine systems. |
| D3D9 renderer backend | `NATIVE ONLY` | Retain for native builds and use as behavioral reference; do not compile Direct3D objects into Wasm. |
| Shader compatibility | `MODIFIED KISAK` / `WEB PLATFORM IMPLEMENTATION` boundary | Native material/shader contracts should remain canonical; selecting or translating to built-in GLSL belongs at the backend seam. |
| ODE math | `SHARED KISAK` | `src/physics/ode/odemath.cpp` is compiled directly. Expand shared ODE/collision code based on compile inventory and measured needs. |
| Collision and `CM_LoadMap` | `NOT COMPILED` | Prioritize after the first real GfxWorld render so map state is consumed by real engine systems. |
| xanim and DObj runtime | `NOT COMPILED` | Asset publication is tracked separately above. Do not add playback, skeletal evaluation, or browser-only DObj behavior during traversal; compile the real xanim/DObj consumers after the GfxWorld proof and runtime pivot. |
| Script VM and `Scr_Init` | `NOT COMPILED` | Bring up after database/world publication dependencies are credible. |
| Client and `CL_Init` | `NOT COMPILED` | Initial runtime pivot target; replace launcher-driven scene behavior with real client initialization. |
| cgame and `CG_Init` | `NOT COMPILED` | Required for a playable offline client; port through platform and renderer seams. |
| game and `SV_Init` | `NOT COMPILED` | Required for the local single-player server/runtime. Browser transport is not needed for the initial offline path. |
| Input | `NOT COMPILED` / future `WEB PLATFORM IMPLEMENTATION` | DOM keyboard, pointer lock, and gamepad collection belong at the platform edge; feed canonical Kisak input state. |
| Audio | `NOT COMPILED` / future `WEB PLATFORM IMPLEMENTATION` | Preserve Kisak sound semantics where possible and isolate autoplay/unlock and Web Audio/OpenAL integration. |
| Networking | `NOT COMPILED` / future `WEB PLATFORM IMPLEMENTATION` | Offline single-player first. Any later multiplayer requires a framed WebSocket/WebTransport relay; browsers cannot use raw COD4 UDP. |
| Bink cinematics, Miles, and Steam | `NATIVE ONLY` | Feature-gate them. Use browser-compatible video/audio/auth paths or graceful omission without shipping native proprietary binaries. |
| Database semantic trace | `MODIFIED KISAK` / partial | The Gate 3 runtime trace now records list boundaries, ordered interned strings, asset index/type/name, pointer class, publication boundaries, pool/entry indices, free counts, canonical hash, zone, nine final offsets, and failure stage without addresses. Win32 x86 and Wasm execute the same extracted generated closure and print identical normalized results. |
| Portable parser tests | `MODIFIED KISAK` / partial | Empty-list, string-list, RawFile, PhysPreset, TechniqueSet, Material, image, water, LocalizeEntry, SndCurve, sound alias, LoadedSound, Font, and FX fixtures cover pointer forms, dependency ordering, payloads, direct/interior XStrings, malformed streams/tokens/counts, pool/entry exhaustion, and failure-before-publication. The same direct executable runs under Win32 x86 and Emscripten; browser tests cover the Worker filesystem boundary. The census dispatcher fuzz target remains independent regression evidence. |
| Playwright browser tests | `WEB PLATFORM IMPLEMENTATION` | Routine smoke/remainder tiers are disjoint and focus on Worker, storage, lifecycle, bridge, canvas, and WebGL behavior. `@native-covered` duplicates run only in the explicit exhaustive suite. Synthetic assets only. |
| Synthetic CI | `MODIFIED KISAK` / platform verification | GitHub Actions builds and tests native Linux, sanitized parser/dispatcher fuzz targets, Win32 MSVC, Emscripten/Node differential contracts, and disjoint Playwright smoke/remainder tiers. It builds one Release site, tests that exact site, then uploads it; strict undefined-symbol checking reuses the runtime-prefix objects. No retail assets are fetched or embedded. |

## Convergence gates

### Checkpoint 1: shared ABI and trace vocabulary (complete)

- Canonical database-facing asset declarations no longer require the full
  Direct3D-heavy xanim include graph.
- Wasm verifies the original 32-bit `RawFile`, `XAssetHeader`, and `XAsset`
  layout.
- The current loader emits normalized top-level begin, publication, and
  boundary events with an explicit ceiling and deterministic hash.
- The trace intentionally contains no process addresses or graphics handles.
- The native producer hook is present and the portable observer projection
  passes under MSVC. Full native generated-loader execution remains pending the
  monolithic target's SDK/runtime prerequisites and a legally owned test
  environment; the projection is not mislabeled as that run.

### Checkpoint 2: canonical RawFile publication (complete)

- Native `Load_XAssetArrayCustom` establishes the top-level trace context and
  `Load_RawFilePtr` emits logical block coordinates around atomic publication.
- The web loader follows the same header/name/payload ordering and exposes a
  canonical `RawFile` whose pointers have stable owned lifetime.
- Explicit count, name, individual payload, aggregate retained-byte, and trace
  ceilings keep failure atomic.
- The synthetic contract test covers consecutive RawFiles, collection ceilings,
  and `RawFile -> XModel -> RawFile` dispatcher return. The owned Killhouse run
  publishes six canonical RawFiles through asset 404, then continues through
  XModel, technique-set, and FX dependencies to asset 436.
- FX visuals now reuse the checked XModel material/image dependency loader.
  Shared material insertion cells and typed aliases publish only after complete
  dependency success. Native `FxEffectDef::elemDefs` is treated as a presence
  field and `totalSize` does not size database traversal.

### Checkpoint 3: canonical XAnimParts publication (complete)

- The renderer-free canonical type header is shared by native xanim code and
  the portable database path; no permanent `RetailXAnim*` object model was
  introduced.
- Synthetic coverage mirrors `-1`, `-2`, insertion-cell, and prior-alias
  pointer forms; bone and notify tables; every packed data array; both index
  widths; flexible delta translation/quaternion payloads; zero-length presence
  fields; bounded ownership; and atomic failure.
- Both Win32/MSVC and Wasm suites pass after the shared change.
- The owned Killhouse run publishes XAnimParts assets 437-457 as identities
  1368-1388 before entering the WeaponDef dependency graph.

### Checkpoint 4: canonical WeaponDef dependency handles (complete for the pre-`com_map` prefix)

- Renderer-free canonical declarations for XModel, Material, the draw-surface
  key, and the FX header are shared by native headers and the portable loader;
  32-bit Win32/Wasm ABI tests cover their exact sizes and key offsets.
- Existing checked XModel, Material, and FX loaders expose stable canonical
  top-level objects. Weapon handles resolve prior typed registry aliases to
  those exact pointers in generated-loader order; inline child bodies still
  fail explicitly rather than forking the loaders.
- `Load_snd_alias_list_name` semantics are modeled as an XString-pointer cell
  followed by an injected `ASSET_TYPE_SOUND` name lookup. Direct cells, inline
  names, the 29-entry bounce array, reused cells, lookup failure, and atomic
  publication are covered synthetically. No placeholder sound asset is made.
- Generic FX Material visuals now preserve native block-4 pointer-cell alias
  semantics. Asset 4,098 is no longer a special boundary, and a synthetic
  regression covers a normal token that dereferences an earlier patched
  Material visual cell.
- The owned `common.ff` run completes all 6,502 assets, including all 1,723
  type-7 rows beginning at 4,778. It publishes 1,716 unique canonical sound
  objects into the cross-zone index while retaining serialized and DB aliases
  in the zone result.
- The owned common-to-Killhouse run publishes WeaponDef 458 through normal
  case-insensitive lookup. Pickup and ammo-pickup fields point at the exact
  common objects; a genuinely absent NPC alias follows native behavior to the
  indexed zone-owned `null` sound. No audio playback behavior was added.
- Generic XString conversion now models a native address rather than requiring
  an exact string-start key. Weapon animation fields can point into a prior
  XAnim name and retain the intended suffix. Bounded compatibility offsets are
  monotonic and inferred only from generated WeaponDef ordering, replacing the
  earlier unsafe arbitrary-string calibration.
- Ordered traversal continues through nine more WeaponDefs and all intervening
  technique-set, XModel, FX, XAnimParts, and RawFile rows. It publishes type-12
  `com_map` asset 704, type-17 `lightdef` asset 705, the 66 technique sets at
  706-771, and canonical `GfxWorld` 772. No direct seek or reconstructed
  world-body entry is used.

### Gate 1: finish the pre-GfxWorld dependency graph

- The native type-23 `Load_WeaponDefPtr` / `Load_WeaponDef` contract at asset
  458 is inventoried, including its canonical 2,168-byte layout, exact block-4
  dependency order, sound-name indirections, dynamic arrays, aliases, and
  atomic publication envelope. The canonical root/body, scalar, XString,
  script-string, accuracy-array, prior canonical child-alias, and sound-name
  indirection slices are implemented. The general prerequisite-zone owner,
  canonical sound publication, cross-zone index, and owned asset-458 rerun are
  complete. The native `Load_clipMap_ptr` / `Load_clipMap_t` family is isolated
  and publishes canonical `clipMap_t` for asset types 10/11. The dedicated
  ComWorld family publishes Killhouse asset 704. The dedicated LightDef and
  reusable image families now publish asset 705 and its canonical attenuation
  dependency. Gate 1 reaches the natural pre-world boundary at asset 772.
- Preserve block cursors, high-water marks, insertion cells, aliases, dependency
  order, and atomic publication.
- Do not seek directly to or enter asset 772 until the pre-world state receives
  architectural review.
- For each new family, inventory the corresponding Kisak generated/native
  loader before designing a representation.
- Add a semantic trace that can be compared with a native Kisak run where
  practical.

### Gate 2: canonical GfxWorld publication

- Complete. The full serialized graph loads into canonical Kisak structures,
  with wire decoding and ownership kept separate from the in-memory type.
- Publication is final-only and records exact block/registry state plus semantic
  stage checkpoints. Strict count and retained-byte ceilings guard every array.
- One bounded real Killhouse `GfxSurface` is copied directly from canonical
  `vd.vertices`, `indices`, `dpvs.surfaces`, and `Material*` into the existing
  portable WebGL2 contract. GPU resources remain renderer-owned.
- Stop viewer growth here and begin the Gate 3 runtime pivot.

### Gate 3: runtime pivot

Once Gate 2 renders enough geometry to prove the pipeline, stop broadening the
viewer. Prioritize compile inventories and vertical initialization slices for:

The current [`canonical runtime prefix inventory`](canonical-runtime-prefix-inventory.md)
classifies every remaining temporary DB/common owner and its retirement path.
The earlier Gate 3 checkpoints are retained under `docs/history/`.

The second runtime slice is complete: `common.cpp` reaches real `PMem_Init`,
`DB_SetInitializing(true)`, and `PMem_BeginAlloc("$init", 1)`. Both platforms
then reached the `Com_InitXAssets` call boundary before
`DB_InitThread`/`Sys_SpawnDatabaseThread`, with 14 stages, three startup-line
segments, six commands, 22 prefix dvars, and identical 128 MiB arena state.
Canonical `dvar.cpp` and `cmd.cpp` have replaced `dvar_core.cpp` and
`cmd_core.cpp` in production. No filesystem Promise, Asyncify path,
census-as-database call or post-boundary subsystem was added.

The third runtime slice is described in the historical
[`Gate 3 Worker/database inventory`](history/gate-3-worker-database-inventory.md).
The browser now uses the target main-thread launcher -> dedicated engine Worker
-> Wasm -> synchronous engine filesystem shape. Database work is synchronous
inside that Worker with a distinct logical DB context; inventory found no
current correctness requirement for a pthread or second Worker. Canonical pool
state and zone request ordering initialize before the normal logical zone path.
The follow-on
[`Gate 3 XFile streaming inventory`](history/gate-3-xfile-streaming-inventory.md)
now records incremental inflate, PMem block allocation, stream initialization,
and the former generated-loader boundary. The next
[`Gate 3 generated-loader inventory`](history/gate-3-generated-loader-inventory.md)
records the earlier shared generated list/string/asset/RawFile extraction.
The current
[`DB registry and PhysPreset inventory`](gate-3-db-registry-physpreset-inventory.md)
records lifecycle ownership, matching Win32 x86/Wasm PhysPreset publication,
and the owned retail traversal.
The follow-on
[`MaterialTechniqueSet inventory`](gate-3-material-technique-set-inventory.md)
records the exact generated child graph, renderer-disabled post-load boundary,
matching synthetic publication, and the new retail Material boundary.
The current [`Material inventory`](gate-3-material-inventory.md) records the
Material, image, and water generated closures, exact ABI/stream ordering,
matching synthetic ownership, and the retail LocalizeEntry boundary.
The [`LocalizeEntry inventory`](gate-3-localize-inventory.md) records the exact
eight-byte body/XString closure and 1,116 retail publications. The current
[`Sound and Font inventory`](gate-3-sound-font-inventory.md) records the next
four canonical families. The [`FX inventory`](gate-3-fx-inventory.md) records
the canonical element graph, explicit inline-XModel boundary, and the new
FxImpactTable retail blocker. The current
[`canonical asset coverage map`](gate-3-asset-coverage.md) separates normal DB
ownership from the remaining Gate 2-only families and temporary seams.

The former `DB_TryLoadXFileInternal -> CreateFileA` boundary is now a narrow
platform open over the Worker mount. The double-buffered reader, inflate setup,
XFile block table, zone streams, generated prefix, RawFile, PhysPreset,
MaterialTechniqueSet, Material, GfxImage, water, LocalizeEntry, SoundCurve,
sound alias, LoadedSound, Font, and FxEffectDef publication now use shared
Kisak code. The owned retail startup zone publishes 1,243 assets and stops at
asset 1225, type 26 `ASSET_TYPE_IMPACT_FX`; the next call is
`Load_FxImpactTablePtr` and its fixed FxEffectDef-handle table.

1. `Com_Init` and the real qcommon lifecycle.
2. `DB_LoadXZone` and canonical asset ownership.
3. `CM_LoadMap` and collision.
4. `CL_Init` and `CG_Init`.
5. `SV_Init`, game, cgame, xanim, and the script VM.
6. Browser input and audio adapters needed for an offline playable slice.

The preferred long-term host is a main-thread launcher/file picker plus a
dedicated Worker containing KisakCOD Wasm, synchronous-style engine filesystem
operations, and OffscreenCanvas/WebGL2. This direction does not itself justify
Asyncify or pthreads.

## Differential test contract

Where a native reference path can consume the same legal synthetic fixture,
compare semantic events rather than implementation details. The minimum useful
trace is:

```text
asset index and type
asset name
stream position and active block
allocation and insertion-cell offsets
pointer classification and alias target
dependency enter/leave order
publication order
nested asset counts
surface and index counts
material references
texture metadata
failure stage and location
```

Normalize real addresses and backend handles into stable logical identities.
A test should fail if native and web paths publish different semantic results,
even when both parsers individually report success.

## Trend indicators

Update this section when a milestone changes architectural ownership.

| Indicator | Required direction | Current reading |
| --- | --- | --- |
| Shared or narrowly modified Kisak code in the web target | Increase | Improving: 41 of 73 production translation units are outside `src/web`, now including real `db_registry.cpp`, the DB file-platform seam, generated RawFile/PhysPreset/TechniqueSet/Material/Image/water/LocalizeEntry/Sound/Font/FX loading, and canonical registry publication. |
| Browser-only engine substitutes | Decrease after their validation purpose is met | High but falling: `dvar_core.cpp` and `cmd_core.cpp` are retired from production. The VFS qcommon oracle, retail DB traversal, and temporary nested asset records remain substitutes. The XModel preview frontend is retired. |
| Permanent browser platform code | Stable and isolated | Good: launcher, storage, lifecycle, filesystem bridge, and WebGL2 are under explicit web boundaries. |
| Native engine systems not compiled | Decrease sharply after the GfxWorld proof | High: qcommon plus canonical DB framing, incremental inflate, PMem blocks, stream globals, generated list/string/asset dispatch, RawFile, PhysPreset, TechniqueSet, Material, GfxImage, water, LocalizeEntry, SoundCurve, sound aliases, LoadedSound, Font, FX, and their real DB publications now execute. Remaining generated families, client, cgame, game, xanim runtime, collision, and script VM remain outside the web target. |
| Native-vs-web semantic comparisons | Increase | The Gate 3 startup closure matches under Win32 x86 and Wasm through RawFile, PhysPreset, TechniqueSet, Material, nested/top-level images, water, LocalizeEntry, SoundCurve, sound aliases, LoadedSound, Font, and FX. Fixtures cover `-1`, `-2`, insertion, aliases, direct/inline/interior XStrings, dependency ordering, payloads, final-only publication, failure atomicity, and deterministic pool/free-chain deltas. |
| Viewer-only feature work | Stop after world proof | Retired: the canonical world-to-WebGL2 seam is proven and the XModel preview UI, state, bridge, retained geometry, and multi-draw path have been removed. |

## Update rule

For each substantial milestone, update the snapshot, affected rows, and trend
indicators. Record whether it:

- compiles more Kisak code,
- replaces or retires a temporary substitute,
- introduces a justified permanent platform implementation,
- adds a native-vs-web semantic comparison, or
- leaves convergence unchanged and why.
