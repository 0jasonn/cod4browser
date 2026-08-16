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

Snapshot baseline: branch `web-port`, through canonical Killhouse `LightDef`
publication at asset 705 and the ordered pre-`GfxWorld` boundary at asset 772
in the current working tree.

The production web target contains 36 C/C++ translation units: six outside
`src/web` and 30 inside it. The non-web unit added before this milestone is the shared database semantic
trace. This is only a source-inventory baseline; it is not
a quality metric by itself because filesystem, lifecycle, and WebGL code should
remain platform-owned.

The last successful prerequisite traversal completes all 6,502 ordered assets
in `common.ff`. Its type-7 run begins at asset 4,778 and retains 1,723 sound
table rows backed by 1,716 unique canonical `snd_alias_list_t` objects. The
cross-zone index then lets `killhouse.ff` publish its canonical type-23
`WeaponDef` run beginning at asset 458 (`winchester1200`). Ordered traversal no
longer stops at that validation boundary: it completes assets 0-771 and reaches
the first `GfxWorld` at asset 772 without entering its body. The retained prefix
contains 218 technique sets, 320 XModels, 60 FX effects, 146
canonical `XAnimParts`, 10 WeaponDefs, 21 canonical RawFiles, and the canonical
type-12 `ComWorld` for `maps/killhouse.d3dbsp` with 24 primary lights. Asset 705
is the canonical `GfxLightDef` `light_point_linear`; its attenuation pointer
resolves to canonical `GfxImage` identity 1,773. Assets 706-771 are already
supported technique sets. The pre-world state is block-0 high-water 2,300,
block-4 cursor 11,121,808, inflated cursor 36,119,878, 1,840 registered assets,
and 1,944 defined aliases.
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
| Canonical database asset ABI | `SHARED KISAK` / partial | `RawFile`, `XAssetHeader`, `XAssetType`, and `XAsset` live in renderer-free `src/database/db_asset_types.h`. Canonical `XAnimParts`, `WeaponDef`, `LocalizeEntry`, XModel, Material, draw-surface key, FX, collision-plane, ClipMap, `ComWorld`, `ComPrimaryLight`, `GfxImage`, and `GfxLightDef` declarations are isolated in lightweight shared type headers consumed by both native declarations and the portable loader. Win32/Wasm tests enforce the 32-bit IW3 layouts. Expand this extraction only when a real shared consumer requires another canonical type. |
| IWD/ZIP reading | `MODIFIED KISAK` / partial | The bounded reader is portable and tested, but final integration should be through Kisak filesystem/database calls rather than a preview-only archive job. |
| IWI decoding | `MODIFIED KISAK` / partial | Bounded DXT decoding is reusable. Connect it to canonical `GfxImage` loading and renderer upload instead of browser material queues. |
| Fastfile framing and zone stream machine | `TEMPORARY WEB SUBSTITUTE` | It accurately models blocks, rewind/high-water behavior, pointer classes, aliases, and bounded streaming. Use it as differential evidence and migrate reusable mechanics toward the Kisak DB loader. |
| Asset registry | `TEMPORARY WEB SUBSTITUTE` | Stable typed identities prove alias behavior. Independent asset, alias, and name-byte ceilings now back indexed identity, source, type/name, and alias lookup with atomic reset/unload tests. The destination remains Kisak `XAsset` registration and native DB ownership. |
| Retail loader dispatcher | `TEMPORARY WEB SUBSTITUTE` | `web_retail_fastfile_census.*` is the current pre-world traversal vehicle. It reports canonical asset types through the shared semantic trace, publishes ordered assets 0-771, and reaches the `GfxWorld` 772 boundary without seeking or entering the body. `web_retail_load_context.h` exposes only stream, registry, ownership, trace, limits, lookup, and shared alias/XString services to extracted families. WeaponDef dependency resolution uses that seam; ClipMap, ComWorld, GfxImage, and LightDef own resumable generated-loader state in dedicated families. Stable families remain in the dispatcher until they are materially changed. |
| `clipMap_t` asset loading | `MODIFIED KISAK` / partial | The dedicated family transcribes the 284-byte `Load_clipMap_t` record, block-4 child order, block-1 zero-fill dynamic client allocations, root insertion/alias handling, bounded ownership, and atomic canonical publication for `col_map_sp`/`col_map_mp`. Synthetic MSVC/Wasm coverage exercises empty and populated child graphs under one-byte traversal budgets. Inline DynEntity dependency bodies remain fail-closed until their canonical families are compiled; no `CM_LoadMap` or collision runtime behavior is included. Type 12 `com_map` remains correctly distinct from `clipMap_t` and is now handled by the canonical ComWorld family. |
| `ComWorld` asset loading | `MODIFIED KISAK` / partial | `web_retail_load_comworld.*` transcribes native `Load_ComWorldPtr`, `Load_ComWorld`, `Load_ComPrimaryLightArray`, and `Load_ComPrimaryLight`: root null/inline/shared/prior-alias handling, block-0 body allocation, block-4 name and 68-byte light array, per-light `defName` XStrings, checked ceilings, and publication only after the complete body. The owned run publishes asset 704 as canonical `ComWorld`; no light rendering, evaluation, collision, gameplay, or `GfxWorld` behavior is present. |
| `GfxLightDef` asset loading | `MODIFIED KISAK` / partial | `web_retail_load_lightdef.*` transcribes `Load_GfxLightDefPtr`, `Load_GfxLightDef`, and `Load_GfxLightImage`: four-byte root cells, null/inline/shared/prior aliases, block-0 16-byte bodies, block-4 names, embedded 8-byte light images, canonical `GfxImage*` dependencies, insertion cells, and final-only publication. Killhouse asset 705 publishes as `light_point_linear`; no rendering, attenuation evaluation, shadow behavior, or primary-light linking is implemented. |
| `XAnimParts` asset loading | `MODIFIED KISAK` / partial | The bounded path mirrors native `Load_XAnimPartsPtr` / `Load_XAnimParts`: block-0 body allocation, optional shared insertion cell, block-4 name and payload scope, exact array order, low/high-frame index widths, and flexible delta translation/quaternion storage. It publishes the canonical Kisak structure with ownership-only backing; the owned run publishes assets 437-457. Replace the temporary owner with real zone allocation during DB convergence. |
| `WeaponDef` asset loading | `MODIFIED KISAK` / partial | The canonical header, fixed scalar decode, 40 script strings, all 48 native XStrings, four accuracy arrays, root insertion/alias handling, canonical XModel/Material/FX resolution, sound-name cells, bounce array, bounded ownership, and atomic publication are covered. Direct XStrings now preserve native address semantics, including pointers into earlier character payloads, while bounded compatibility translation is anchored only by generated WeaponDef order. The owned run publishes ten WeaponDefs before `com_map`; inline child bodies remain explicit future work. |
| Sound alias loading and catalog | `MODIFIED KISAK` / partial | The prerequisite dispatcher mirrors the native list/header/component order and owns canonical `snd_alias_list_t`, `snd_alias_t`, `SoundFile`, `LoadedSound`, `SndCurve`, and `SpeakerMap` metadata in the publishing zone. `web_sound_alias_catalog.*` remains a case-insensitive ownership/index seam: it stores pointers to those objects, retains the common-zone owner, collapses native DB name aliases, and uses the indexed canonical `null` sound for native missing-sound fallback. It neither copies nor synthesizes sound records. No payload playback or audio runtime behavior is implemented. |
| `LocalizeEntry` asset loading | `MODIFIED KISAK` / partial | The renderer-free canonical ABI and reusable pointer/body operation now cover inline/shared roots, direct/prior XStrings, bounded ownership, aliases, and atomic publication. This advances the owned `common.ff` traversal through all 3,028 localization assets rather than skipping their stream effects. |
| `XModel` | `MODIFIED KISAK` / partial | The existing checked traversal now publishes a stable canonical `XModel` top-level object with canonical name, scalar metadata, skeleton-array pointers, and canonical Material handle identity. `RetailWorldXModel` still owns temporary surface, collision, and physics retention; converge those nested graphs before compiling broader consumers. |
| `Material` and techniques | `MODIFIED KISAK` / partial | Existing XModel/FX material traversal now exposes canonical `Material` headers and stable names, and XModel handles point at those exact objects. Texture/image/technique ownership remains in temporary records; converge those canonical child graphs and translate only D3D shader/backend state. |
| `GfxImage` | `MODIFIED KISAK` / partial; native backend `NATIVE ONLY` | The reusable database path and the pre-existing material path now publish canonical `GfxImage` metadata/name pointers through the shared registry. Its canonical texture slot remains null; IWI decoding, retained payload metadata, WebGL resource creation, and context recovery remain renderer/backend infrastructure. Continue converging all remaining image consumers on this family. |
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
| Portable parser tests | `MODIFIED KISAK` / partial | Synthetic fixtures cover bounds and failure behavior. Native and Wasm suites include canonical ABI, trace determinism/limits, and the native-observer/web RawFile projection. A direct low-ceiling libFuzzer target now drives the real ordered dispatcher with malformed legal seeds; full generated native-loader execution remains a later environment-backed check. |
| Playwright browser tests | `WEB PLATFORM IMPLEMENTATION` | Continue boot, storage, lifecycle, context-loss, and end-to-end boundary coverage with synthetic assets. |
| Synthetic CI | `MODIFIED KISAK` / platform verification | GitHub Actions now builds and tests native Linux, sanitized parser/dispatcher fuzz targets, Win32 MSVC, Emscripten/Node differential contracts, Playwright smoke/full tiers, and a Release browser artifact. No retail assets are fetched or embedded. |

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
  `com_map` asset 704, type-17 `lightdef` asset 705, and the 66 technique sets
  at 706-771. It then stops before `GfxWorld` 772. No direct seek or world-body
  entry is used.

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
| Shared or narrowly modified Kisak code in the web target | Increase | Low but improving: six of 32 production translation units are outside `src/web`; canonical database asset types are directly consumable by Wasm and WeaponDef family mechanics are isolated for later DB integration. |
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
