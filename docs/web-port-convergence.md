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

Current retail traversal of `killhouse.ff` completes top-level assets 0 through
436 and stops before inline type-2 `XAnimParts` asset 437. It has 335 top-level
records left before the first `GfxWorld` at asset 772. The retained result
includes 278 published XModels, 11 FX effects, and 1,367 registry identities.
RawFiles 395, 396, 398, 400, 402, and 404 are published through the canonical
Kisak type; the XModel/material/FX results remain census/preview records rather
than canonical publications.

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
| Canonical database asset ABI | `SHARED KISAK` / partial | `RawFile`, `XAssetHeader`, `XAssetType`, and `XAsset` now live in renderer-free `src/database/db_asset_types.h`; xanim consumes the same declarations. Wasm tests enforce their 32-bit IW3 layout. Expand this extraction only when a real shared consumer requires another canonical type. |
| IWD/ZIP reading | `MODIFIED KISAK` / partial | The bounded reader is portable and tested, but final integration should be through Kisak filesystem/database calls rather than a preview-only archive job. |
| IWI decoding | `MODIFIED KISAK` / partial | Bounded DXT decoding is reusable. Connect it to canonical `GfxImage` loading and renderer upload instead of browser material queues. |
| Fastfile framing and zone stream machine | `TEMPORARY WEB SUBSTITUTE` | It accurately models blocks, rewind/high-water behavior, pointer classes, aliases, and bounded streaming. Use it as differential evidence and migrate reusable mechanics toward the Kisak DB loader. |
| Asset registry | `TEMPORARY WEB SUBSTITUTE` | Stable typed identities prove alias behavior, but the destination is Kisak `XAsset` registration and native DB ownership. |
| Retail loader dispatcher | `TEMPORARY WEB SUBSTITUTE` | `web_retail_fastfile_census.*` is the current pre-world traversal vehicle. It reports canonical asset types through the shared semantic trace and now publishes one canonical RawFile before an explicit boundary. Continue reusable families without turning other `Retail*` results into the permanent object model. |
| `XModel` | `TEMPORARY WEB SUBSTITUTE`; canonical code `NOT COMPILED` | Retail data is retained as `RetailWorldXModel`. Converge loader output and publication on `XModel` from `src/xanim/xmodel.h`, then compile the consumers that require it. |
| `Material` and techniques | `TEMPORARY WEB SUBSTITUTE`; canonical code `NOT COMPILED` | Current `RetailXModelMaterial` and compatibility records validate dependencies. Converge on `Material`, `MaterialTechniqueSet`, and related Kisak structures; translate only D3D shader/backend state. |
| `GfxImage` | `TEMPORARY WEB SUBSTITUTE`; native backend `NATIVE ONLY` | Current metadata plus IWD lookup proves image selection. Publish canonical image assets while keeping GPU texture creation in the WebGL backend. |
| `GfxWorld` | `TEMPORARY WEB SUBSTITUTE`; canonical code `NOT COMPILED` | Synthetic extraction proves one surface and retail traversal is in progress. Finish all preceding retail families, load and publish a real canonical world, then render only enough Killhouse geometry to prove the seam. |
| XModel/model preview scene | `TEMPORARY WEB SUBSTITUTE` | Useful validation UI with orthographic projection and selectable models. Freeze feature growth after the real-world proof and retire it as an architectural center. |
| Renderer frontend | `TEMPORARY WEB SUBSTITUTE` / partial | Current converted surfaces and draw lists bypass most Kisak frontend behavior. Introduce portable draw commands behind the Kisak renderer frontend and keep backend handles private. |
| WebGL2 backend and context recovery | `WEB PLATFORM IMPLEMENTATION` | Permanent platform boundary. Preserve resource recreation and fail-safe publication. Do not expose WebGL handles to engine systems. |
| D3D9 renderer backend | `NATIVE ONLY` | Retain for native builds and use as behavioral reference; do not compile Direct3D objects into Wasm. |
| Shader compatibility | `MODIFIED KISAK` / `WEB PLATFORM IMPLEMENTATION` boundary | Native material/shader contracts should remain canonical; selecting or translating to built-in GLSL belongs at the backend seam. |
| ODE math | `SHARED KISAK` | `src/physics/ode/odemath.cpp` is compiled directly. Expand shared ODE/collision code based on compile inventory and measured needs. |
| Collision and `CM_LoadMap` | `NOT COMPILED` | Prioritize after the first real GfxWorld render so map state is consumed by real engine systems. |
| xanim and DObj | `NOT COMPILED` | Compile canonical XModel consumers rather than adding behavior to preview records. |
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

### Gate 1: finish the pre-GfxWorld dependency graph

- Inventory and implement native type-2 `XAnimParts` at asset 437, then continue
  every remaining inline family in serialized order.
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
