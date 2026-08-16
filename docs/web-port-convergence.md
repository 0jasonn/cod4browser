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

Snapshot baseline: branch `web-port`, convergence checkpoint 1 at commit
`66213ad1`, plus checkpoint 2 in the current working tree.

The production web target contains 31 C/C++ translation units: six outside
`src/web` and 25 inside it. The new non-web unit is the shared database semantic
trace. This is only a source-inventory baseline; it is not
a quality metric by itself because filesystem, lifecycle, and WebGL code should
remain platform-owned.

The last successful owned traversal of `killhouse.ff` completes top-level
assets 0 through 457 at the inline type-23 `WeaponDef` asset 458 boundary. It
has 314 top-level records left before the first `GfxWorld` at asset 772. The
retained result
includes 278 published XModels, 11 FX effects, 21 canonical `XAnimParts`, and
1,388 registry identities.
RawFiles 395, 396, 398, 400, 402, and 404 are published through the canonical
Kisak type. XModel, Material, and FX publications now also expose stable
canonical top-level objects, while their retained nested census/preview graphs
remain temporary convergence scaffolding.

## System inventory

| System | Current status | Evidence and convergence action |
| --- | --- | --- |
| Emscripten build and packaging | `WEB PLATFORM IMPLEMENTATION` | Explicit `web` target, pinned toolchain, generated site, and HTTP server. Keep isolated from native target selection. |
| Browser launcher and legal asset selection | `WEB PLATFORM IMPLEMENTATION` | DOM file picker, exact-path validation, local manifest, and restore UI are genuine browser responsibilities. |
| OPFS/IndexedDB asset persistence | `WEB PLATFORM IMPLEMENTATION` | Keep storage asynchronous in the host and hidden behind the filesystem boundary. |
| Wasm filesystem bridge | `WEB PLATFORM IMPLEMENTATION` / partial | The browser I/O side is permanent. Its engine-facing API must converge on Kisak filesystem semantics rather than become a separate VFS used directly by game systems. |
| Browser lifecycle, logging, and timing | `WEB PLATFORM IMPLEMENTATION` | Keep event-loop and page-lifecycle handling in `src/web`; expose narrow system calls to shared code. |
| Cooperative scheduler | `TEMPORARY WEB SUBSTITUTE` | It currently advances bootstrap jobs and protects the main thread. Retain it for the present traversal, but do not spread its state machines through shared engine code. Long term, stage browser I/O outside a Worker-hosted synchronous-looking engine. |
| Command system | `MODIFIED KISAK` / partial | `src/qcommon/cmd_core.cpp` implements the Kisak command APIs as a reduced portable core. Reconcile it with `src/qcommon/cmd.cpp` as more qcommon code compiles. |
| Dvar system | `MODIFIED KISAK` / partial | `src/universal/dvar_core.cpp` is a reduced portable implementation. Preserve API and behavior parity and converge with the full dvar implementation. |
| qcommon startup | `TEMPORARY WEB SUBSTITUTE` | The bounded pre-database shell proves ordering and I/O but is not `Com_Init`. Replace milestone-specific startup actions by compiling the real initialization path behind platform services. |
| Canonical database asset ABI | `SHARED KISAK` / partial | `RawFile`, `XAssetHeader`, `XAssetType`, and `XAsset` live in renderer-free `src/database/db_asset_types.h`. Canonical `XAnimParts`, `WeaponDef`, `LocalizeEntry`, XModel, Material, draw-surface key, and FX header declarations are isolated in renderer-free type headers consumed by both the native declarations and portable loader. Win32/Wasm tests enforce their 32-bit IW3 layouts. Expand this extraction only when a real shared consumer requires another canonical type. |
| IWD/ZIP reading | `MODIFIED KISAK` / partial | The bounded reader is portable and tested, but final integration should be through Kisak filesystem/database calls rather than a preview-only archive job. |
| IWI decoding | `MODIFIED KISAK` / partial | Bounded DXT decoding is reusable. Connect it to canonical `GfxImage` loading and renderer upload instead of browser material queues. |
| Fastfile framing and zone stream machine | `TEMPORARY WEB SUBSTITUTE` | It accurately models blocks, rewind/high-water behavior, pointer classes, aliases, and bounded streaming. Use it as differential evidence and migrate reusable mechanics toward the Kisak DB loader. |
| Asset registry | `TEMPORARY WEB SUBSTITUTE` | Stable typed identities prove alias behavior, but the destination is Kisak `XAsset` registration and native DB ownership. |
| Retail loader dispatcher | `TEMPORARY WEB SUBSTITUTE` | `web_retail_fastfile_census.*` is the current pre-world traversal vehicle. It reports canonical asset types through the shared semantic trace, publishes canonical RawFile and XAnimParts assets, and now contains a bounded partial canonical WeaponDef operation. Continue reusable families without turning other `Retail*` results into the permanent object model. |
| `XAnimParts` asset loading | `MODIFIED KISAK` / partial | The bounded path mirrors native `Load_XAnimPartsPtr` / `Load_XAnimParts`: block-0 body allocation, optional shared insertion cell, block-4 name and payload scope, exact array order, low/high-frame index widths, and flexible delta translation/quaternion storage. It publishes the canonical Kisak structure with ownership-only backing; the owned run publishes assets 437-457. Replace the temporary owner with real zone allocation during DB convergence. |
| `WeaponDef` asset loading | `MODIFIED KISAK` / partial | The canonical header, fixed scalar decode, 40 script strings, 48 direct XStrings (including prior non-weapon zone strings), four accuracy arrays, root insertion cell/alias handling, canonical prior-alias XModel/Material/FX resolution, 48 native sound-name cells, the 29-entry bounce array, bounded ownership, and atomic publication are implemented and covered synthetically. Sound names resolve only through an injected database lookup; the owned web traversal now reaches that unavailable catalog boundary rather than publishing fabricated sound assets. Inline `-1`/`-2` XModel, Material, or FX bodies inside a WeaponDef remain explicit failures. |
| Sound alias catalog | `MODIFIED KISAK` / partial | `web_sound_alias_catalog.*` is the bounded cross-zone ownership seam for canonical `snd_alias_list_t` pointers. It matches native case-insensitive asset-name lookup, retains the publishing zone owner, rejects duplicates/missing entries, and is now the lookup provider passed to the Killhouse WeaponDef traversal. The browser catalog remains empty until the real common-zone sound assets are published; no placeholder aliases are created. |
| `LocalizeEntry` asset loading | `MODIFIED KISAK` / partial | The renderer-free canonical ABI and reusable pointer/body operation now cover inline/shared roots, direct/prior XStrings, bounded ownership, aliases, and atomic publication. This advances the owned `common.ff` traversal through all 3,028 localization assets rather than skipping their stream effects. |
| `XModel` | `MODIFIED KISAK` / partial | The existing checked traversal now publishes a stable canonical `XModel` top-level object with canonical name, scalar metadata, skeleton-array pointers, and canonical Material handle identity. `RetailWorldXModel` still owns temporary surface, collision, and physics retention; converge those nested graphs before compiling broader consumers. |
| `Material` and techniques | `MODIFIED KISAK` / partial | Existing XModel/FX material traversal now exposes canonical `Material` headers and stable names, and XModel handles point at those exact objects. Texture/image/technique ownership remains in temporary records; converge those canonical child graphs and translate only D3D shader/backend state. |
| `GfxImage` | `TEMPORARY WEB SUBSTITUTE`; native backend `NATIVE ONLY` | Current metadata plus IWD lookup proves image selection. Publish canonical image assets while keeping GPU texture creation in the WebGL backend. |
| `GfxWorld` | `TEMPORARY WEB SUBSTITUTE`; canonical code `NOT COMPILED` | Synthetic extraction proves one surface and retail traversal is in progress. Finish all preceding retail families, load and publish a real canonical world, then render only enough Killhouse geometry to prove the seam. |
| XModel/model preview scene | `TEMPORARY WEB SUBSTITUTE` | Useful validation UI with orthographic projection and selectable models. Freeze feature growth after the real-world proof and retire it as an architectural center. |
| Renderer frontend | `TEMPORARY WEB SUBSTITUTE` / partial | Current converted surfaces and draw lists bypass most Kisak frontend behavior. Introduce portable draw commands behind the Kisak renderer frontend and keep backend handles private. |
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
| Database semantic trace | `MODIFIED KISAK` / partial | A shared address-independent event format, exact hash, and portable contract hash now exist. The web loader emits bounded events; the native asset-array/RawFile generated path has an inert-by-default observer hook. MSVC compiles and passes the portable trace projection; full generated-loader execution still requires the monolithic native target and runtime prerequisites. |
| Portable parser tests | `MODIFIED KISAK` / partial | Synthetic fixtures cover bounds and failure behavior. Both the 16-test Wasm suite and the 16-test Win32 MSVC suite pass, including canonical ABI, trace determinism/limits, and the native-observer/web RawFile contract projection. Full generated native-loader execution remains a later environment-backed check. |
| Playwright browser tests | `WEB PLATFORM IMPLEMENTATION` | Continue boot, storage, lifecycle, context-loss, and end-to-end boundary coverage with synthetic assets. |

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
- Both 16-test Win32/MSVC and 16-test Wasm suites pass after the shared change.
- The owned Killhouse run publishes XAnimParts assets 437-457 as identities
  1368-1388 before entering the WeaponDef dependency graph.

### Checkpoint 4: canonical WeaponDef dependency handles (partial)

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
- The owned Killhouse diagnostic now gets through canonical child and prior
  XString aliases and stops at the unavailable sound database lookup. Connecting
  the real canonical sound catalog is the remaining owned asset-458 boundary.
- The cross-zone catalog/lookup lifetime contract is connected. An owned
  `common.ff` inventory confirms 1,723 type-7 sound assets beginning at asset
  4,778. Its ordered traversal now crosses the 1,006 XAnim and 3,028
  `LocalizeEntry` assets, handles prior XString names and the native
  A8/A8L8 image formats, and reaches the first FX run. The next common boundary
  is an unresolved chained Material visual alias in FX asset 4,098. Sound catalog
  population therefore remains pending and Killhouse still fails closed at
  `WeaponSoundLookupFailed`.

### Gate 1: finish the pre-GfxWorld dependency graph

- The native type-23 `Load_WeaponDefPtr` / `Load_WeaponDef` contract at asset
  458 is inventoried, including its canonical 2,168-byte layout, exact block-4
  dependency order, sound-name indirections, dynamic arrays, aliases, and
  atomic publication envelope. The canonical root/body, scalar, XString,
  script-string, accuracy-array, prior canonical child-alias, and sound-name
  indirection slices are implemented. The catalog lookup/lifetime seam is now
  connected; next finish the ordered common-zone traversal from Material asset
  4,098's FX visual alias through the first type-7 sound at 4,778, publish those
  canonical sound assets into the catalog, and rerun owned asset 458. Continue to reuse the
  existing child loaders if a later WeaponDef contains inline child bodies.
- Preserve block cursors, high-water marks, insertion cells, aliases, dependency
  order, and atomic publication.
- Do not seek directly to asset 772.
- For each new family, inventory the corresponding Kisak generated/native
  loader before designing a representation.
- Add a semantic trace that can be compared with a native Kisak run where
  practical.

### Gate 2: canonical GfxWorld publication

- Load the world and its dependencies into canonical Kisak structures or a
  narrowly adapted shared DB path.
- Keep serialized wire decoding distinct from the in-memory engine type, but do
  not introduce a second browser scene model.
- Publish only after the complete supported dependency graph succeeds.
- Prove a bounded piece of real Killhouse world geometry through the renderer
  frontend and WebGL2 backend.

### Gate 3: runtime pivot

Once Gate 2 renders enough geometry to prove the pipeline, stop broadening the
viewer. Prioritize compile inventories and vertical initialization slices for:

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
| Shared or narrowly modified Kisak code in the web target | Increase | Low but improving: six of 31 production translation units are outside `src/web`; canonical database asset types are now directly consumable by Wasm. |
| Browser-only engine substitutes | Decrease after their validation purpose is met | High: qcommon bootstrap, retail DB traversal, asset records, and preview frontend remain substitutes. |
| Permanent browser platform code | Stable and isolated | Good: launcher, storage, lifecycle, filesystem bridge, and WebGL2 are under explicit web boundaries. |
| Native engine systems not compiled | Decrease sharply after the GfxWorld proof | High: DB, client, cgame, game, xanim, collision, and script VM are not in the web target. |
| Native-vs-web semantic comparisons | Increase | Foundation present: shared trace format and the RawFile contract projection pass in both Wasm and Win32 MSVC. Execution of the generated native producer remains pending. |
| Viewer-only feature work | Stop after world proof | Controlled: current preview exists to validate assets and rendering, not as the product direction. |

## Update rule

For each substantial milestone, update the snapshot, affected rows, and trend
indicators. Record whether it:

- compiles more Kisak code,
- replaces or retires a temporary substitute,
- introduces a justified permanent platform implementation,
- adds a native-vs-web semantic comparison, or
- leaves convergence unchanged and why.
