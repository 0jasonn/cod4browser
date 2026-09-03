# Web port convergence inventory

Updated 2026-09-03. This page owns system classification; see
[current status](web-status.md) and [the active roadmap](web-roadmap.md) for
evidence and priorities. Earlier chronology is retained in
[the historical inventory](history/web-convergence-through-2026-08-28.md).

## Current verification boundary

The platform frame wrapper now checks the canonical latched error after its
body, matching `Com_Frame` and preventing a reached renderer failure from
leaving the host marked running. The portable light-grid helper accepts native
`R_InitEmptyLightGrid` and preserves its default-palette/primary-light result.
AC130 supplies current execution evidence for both reached boundaries; its
game, camera, mission and FX state remain canonical. See
[AC130 evidence](evidence/ac130-rendering-2026-09-03.md).

The asynchronous mount continuation now has a live canonical `setjmp` error
boundary after `Com_Init` has returned. `Com_Error` retains its native text
through the Worker cleanup/ownership protocol. The shared synthetic import
fixtures include real startup prerequisites, and the picker test waits for
completed mounting and restoration. See [verification evidence](evidence/verification-repair-2026-09-02.md).

The three-way reference remains incomplete: Steam hashes/settings are pinned,
and native SP now builds and enters main-menu code using the owned startup
fastfiles. Native headers share the canonical world and critical-section types;
common runtime definitions have a single owner, and XAnim counters use C++20
atomics. Native OpenAL avoids the owned Miles DLL's incompatible exports.
See [build, boot and remaining test failures](evidence/native-reference-2026-09-02.md).
No browser/runtime comparison can stand in for original Steam mission-completion
evidence.

Canonical `Com_Quit_f` now requests platform shutdown at the end of the Worker
frame. Canonical config serialization stays in C++; the browser flushes existing
writable handles, stops audio/input, releases leases and terminates the Worker
only after confirmed persistence. Worker destruction owns the final engine heap
release instead of native process teardown. The launcher retains a retry path
for save errors. This is a permanent platform adaptation, not another game
lifecycle model. See [Quit evidence](evidence/browser-quit-2026-09-02.md).

The frame callback now also owns a live canonical `setjmp` target. Shipped
Save and Quit raises `ERR_DISCONNECT` after saving; recovery uses the extracted
native `Com_ShutdownInternal`, SP `SV_Shutdown` and `Com_AssetLoadUI` owners to
retire the game and reload the main menu in the same Worker. Natural Killhouse
training/checkpoints and fresh-browser Resume Game exposed and verify this seam.
General error-class recovery remains incomplete. See
[disconnect evidence](evidence/campaign-training-disconnect-2026-09-02.md).

## Runtime flow

```text
user-owned COD4 files
  -> browser storage / synchronous Worker file primitives
  -> Kisak filesystem and database loaders
  -> XAsset / XModel / Material / GfxWorld
  -> Kisak client, server, game, cgame, script, collision and animation
  -> renderer frontend
  -> portable draw commands
  -> WebGL2 backend
```

Production and diagnostics compile the same runtime sources. Diagnostics add
browser test controls and telemetry through `KISAK_WEB_DIAGNOSTICS`; they no
longer contain a second asset loader, world model, scheduler, or renderer
oracle.

## Shared Kisak systems

| System | Current ownership |
| --- | --- |
| Common startup | Canonical `Dvar_Init` and the strict `Com_Init` prefix run in native order; the host mounts browser storage at the filesystem boundary, then the continuation restores canonical common-command and version-dvar registration before script/server/client startup. |
| Dvars/config | Canonical dvar types, domains, reset/current/latched values, flags, command handlers, key bindings, `Com_WriteConfigToFile`, and profile-relative `config.cfg` own settings. The Worker continuation restores `CL_InitKeyCommands` before filesystem/profile config execution; the browser frame pump only calls the shared `Com_WriteConfiguration` owner. |
| Profiles | Canonical `Com_*PlayerProfile` and `UI_*PlayerProfile` functions own profile identity, active-marker parsing, list/feeder selection, config replay, and deletion. Browser diagnostics only select fixed synthetic entries through those owners. |
| Filesystem | Canonical search paths, IWD/minizip behavior, config/profile calls and synchronous engine-facing operations use Worker file primitives. Recursive profile deletion maps to one durable Worker/OPFS tree-removal primitive. |
| Installation language | The platform import profile derives paths from the validated localization marker; canonical FS/SEH selects localized IWDs and the DB file adapter now uses that selection instead of hardcoded English. French/German synthetic import, DB open and reload pass. Localized retail text/audio remain unqualified. See [evidence](evidence/localized-imports-2026-09-02.md). |
| Database | Canonical XFile stream, allocation blocks, generated loaders, pointer aliases, registry pools, dependency ordering and final publication own runtime assets, including native-compatible leading-comma asset-stub resolution. |
| World/runtime | Canonical `GfxWorld`, collision, server/game, client/cgame, script VM, XAnim/DObj, effects, ragdoll, physics and sound code are in the browser link closure. |
| Frame order | The browser supplies elapsed time; canonical `Com_ModifyMsec`, `SV_Frame`, client frame work and `SCR_UpdateScreen` advance gameplay. Browser startup selects `com_maxFrameTime=5000`, and the platform pump uses the same long-stall ceiling instead of losing time on every frame longer than 100 ms. Script/developer timescales and opt-in `fixedtime` remain canonical. |
| SP UI | Canonical `CL_StartHunkUsers`, `CL_InitUI`, `UI_Init`, shipped MenuLists, `UI_SetActiveMenu`, `UI_Refresh`, and renderer 2D commands own main/options/profile/load/pause menus. Disconnected browser frames continue through `CL_Frame` and `SCR_UpdateScreen`; UI-only backend frames are valid before any `GfxWorld` exists, so startup presents the shipped main menu rather than a DOM substitute. |
| Text presentation | `r_text.cpp` and `r_text_cmds.cpp` move native glyph lookup, layout, color codes, shadows, subtitle glow, cursor blinking and reveal/decay logic into sources compiled by native and web targets. Canonical fonts/materials remain authoritative. The browser only provides scene/cursor clocks, SP color lookup and quad submission; console bytes pass through the native ring-copy routine. See [text evidence](evidence/text-presentation-2026-09-02.md). |
| Objectives | Native server configstrings, `CG_ParseObjectiveChange`, `objectiveInfo_t`, `objectiveinfo`, canonical localization, timed menu visibility, and renderer text/quads own objective notifications. Diagnostics only observe hashed emitted text and inject a freely usable string through the canonical parser. |
| Fresh-map randomness | Shared `SV_GetMapRandomSeed` serves native and browser spawn paths; optional cheat dvar `sv_mapSeed` defaults to the original clock seed. Game RNG and save/demo restoration remain canonical. |
| Renderer frontend | Kisak world, model, effect and UI state is translated only at the portable draw-command boundary. Native IW3's bounded 65,536 static-model cardinality is preserved across that seam. |
| Display gamma | Canonical `r_gamma` owns the setting; unchanged native ramp/correction functions are shared in `r_gamma.cpp`. The WebGL2 final pass implements the display-device boundary after world effects and all 2D, including menu-only frames. Save/feedback captures remain pre-display. Native/Wasm ramps and shipped slider/world pixels pass; Steam monitor-response parity is unverified. See [evidence](evidence/display-gamma-2026-09-03.md). |
| Display modes/restart | Platform `web_display.cpp` registers canonical resolution/refresh enums and owns canvas/device sizing. Shared client/UI viewport helpers and `CG_InitViewDimensions` consume native `vidConfig_t`. Shared `CL_Vid_Restart_f` keeps the existing process-owned DB executor; browser registration recreates WebGL resources. Shipped Apply, persistence and native in-game save/restart/load pass in Chrome; mission state stays in Kisak. See [evidence](evidence/display-options-2026-09-03.md). |
| Saved-screen effects | Canonical cgame owns flashbang/shellshock lifetime and intensities. Ordered renderer commands carry capture, native timer slots and packed colors into the WebGL2 UI pass; a GPU-only feedback texture is platform-owned and invalidated on resize/context loss. Synthetic pixels and a loaded retail material/composite pass are verified; authored effect fidelity remains unverified. See [evidence](evidence/saved-screen-2026-09-02.md). |
| FX lighting queries | Canonical `FX_CalculatePackedLighting` now receives world light-grid/sun samples through the existing renderer lighting helper, following native average-query selection and quantization. Host-native/Wasm checks pass; original-game visual comparison is pending. This adapted query retires with the portable grid helper when native backend compilation replaces it. See [evidence](evidence/average-lighting-2026-09-02.md). |
| Transient FX lights | `r_dynamiclights_core.h` shares canonical `GfxLight` construction and native importance partitioning between native and web callers. FX owns emission/lifetime; the frontend clears per-scene submissions, applies camera visibility and `r_dlightLimit`, then passes selected `GfxLight` records to WebGL2. The backend uses existing retained images, canonical material technique/state tables and destination-alpha coverage. Synthetic pixels and owned Killhouse visual/clear/context recovery checks pass. Native receiver/scissor selection, transient spot shadows and authored-effect fidelity remain open. See [evidence](evidence/transient-lights-2026-09-02.md). |
| Input | Browser events enter canonical key/mouse queues, bindings, usercmd creation and movement/weapon code. |
| Audio | Canonical mixer and OpenAL-facing state feed a browser Web Audio device boundary. Both three-band EQ stages cross that boundary; SND retains parameter/entchannel ownership, while the device computes IIR coefficients and owns filter nodes. Canonical room/wet changes now reach the existing OpenAL reverb DSP in an AudioWorklet with shared native presets. Native/Wasm differential and browser PCM routing tests pass; authored campaign transitions, callback cost and Steam comparison remain open. See [EQ evidence](evidence/browser-eq-2026-09-02.md) and [reverb scope/evidence](browser-reverb.md). |
| Save/persistence | Canonical game serialization, shared `ui_savegames.cpp`, feeders, menu scripts, Continue, and deletion own saves and gameplay state. Bounded header metadata supplies the Date column and dynamic description. Save commits request 512x512 JPEGs through native shared resampling; the Worker codec owns only temporary pixels/bytes. C++ checks save identity before persistence and publishes one canonical raw `Material`/`GfxImage`, using existing backend UI texture retention/recovery. Captures wait for a frame of the matching map and cancel on unload; shutdown drains admitted codec jobs before closing files. Native decoding uses reserved `rgp.rawImage`, canonical texture accounting and the existing D3DX dependency. Owned Airplane start-level/save capture, reload and menu display plus native decoder/device checks are verified; complete native menu and Steam comparison remain open. See [latest evidence](evidence/save-startup-native-2026-09-02.md). |

The [retained-renderer milestone](evidence/retained-renderer-49af3948.md) moves
brush submission closer to native separation of immutable `GfxBrushModel`
geometry and current placement. Only GPU-ready mesh/material resources and their
recovery copies live in the backend; canonical entities, poses, visibility and
light animation remain in Kisak. World retirement frees these resources, and
context restoration recreates GPU objects. Optional effects still account for
brush geometry in the logical budget. AC130 exceeded the old 250k/500k
vertex/index capacity; the bounded 500k/1m capacity retains that accounting
and admits the authored scene. One logical command is bounded at 40 MB,
excluding other renderer resources and staging/recovery copies.

Sun-depth range joining preserves original triangle order and membership without
using camera culling. Texture reuse is bounded and resets each frame/pass; it
preserves texture aliases and adds no persistent GL cache. Resource/remap lifetime
and the verified limitations are recorded in [the boundary notes](renderer-retained-resources.md).

## Modified Kisak seams

| Seam | Why it differs |
| --- | --- |
| `database/db_file_platform.cpp` | Maps DB file operations to the Worker filesystem. |
| `database/db_generated_image_platform.*` | Copies transient canonical image load definitions at the native texture-upload boundary into a bounded process-global source cache; canonical `GfxImage` identity remains authoritative. |
| `qcommon/common_runtime_commands.cpp` | Keeps the post-mount common-command continuation canonical and separate from browser hosting. |
| `cgame/cg_servercmds.cpp` | The shared slow-command parser converts decimal scales numerically instead of aliasing `long double` storage as `double`; this preserves normal-speed and scripted scale values in Wasm. |
| `web_client_server_lifecycle.cpp` | Continues synchronous-looking native startup after the main-thread host mounts user files, including canonical version dvars and diagnostic-only calls through real profile/save UI owners. |
| `web_main.cpp` | Admits disconnected-but-running client frames so canonical pre-map UI and config work run in native order. Its live frame jump target recovers canonical disconnects through shared native shutdown/UI owners; the browser owns the non-blocking pump. |
| `web_canonical_gfxworld.cpp` | Observes final DB publication only; canonical `R_RenderScene` owns world rendering. The obsolete proof submission is removed. |
| `web_renderer_frontend.cpp` | Converts canonical renderer state into backend-neutral commands. |
| `web_system*.cpp` | Supplies browser timing, frame pump, files (including recursive directory deletion), events and thread-context behavior. |

Changes in these seams should preserve native behavior unless the browser
platform makes that behavior impossible.

## Permanent web platform implementations

| Boundary | Implementation |
| --- | --- |
| Host/Worker split | DOM, picker and persistent-storage ownership stay on the main thread; Wasm and OffscreenCanvas run in a Worker. |
| Storage | OPFS/IndexedDB-backed import, validation, atomic replacement and synchronous Worker reads. File System Access is optional. |
| Rendering | WebGL2 context, buffers, textures, shaders, render targets, context recovery and presentation. GPU handles stay private to the backend. The shared 2D image pools retain the selected canonical encoded source and use the existing image decoder, including canonical IWI wavelet formats, transiently for initial upload and context restoration; this is recovery data at the platform boundary, not a second asset model or parser. |
| Input host | Pointer lock, keyboard/mouse normalization, focus release and cursor mode. |
| Audio device | AudioContext policy, buffers/nodes and PCM scheduling. Startup remains suspended and muted until an intentional canvas gesture resumes it; installation-picker interaction does not unlock sound. |
| Main loop | Non-blocking Emscripten frame pump; no Asyncify or pthread requirement. |
| Wasm stack | The web linker reserves 1 MiB, matching native Windows scale for canonical nested map/save loading rather than rewriting shared call chains. |
| Cinematics | Existing `R_Cinematic_*` callers drive the platform FFmpeg Bink decoder, retained UI texture, and OpenAL PCM queue. Movie identity and subsequent game actions stay in Kisak. See [codec scope and remaining qualification](cinematic-codec.md). |

## Control classification

| Control family | Classification |
| --- | --- |
| Reached SP main/options/profile/load/pause controls | Shared canonical dvars and commands. The focused menu trace rejects missing references. |
| `ui_sp_unlock` | Deliberate stock-retail dangling menu reference. Native COD4 1.7 emits the same `openmenuondvar` warning; no guessed browser dvar is registered. |
| D3D9/Win32 renderer controls | Native-only where they configure APIs absent from WebGL2; browser renderer capability controls remain platform-owned and are not aliases pretending to be native dvars. |
| Miles, Bink, and Steam controls | Native DLL integrations remain unavailable. Web Audio and the source-built FFmpeg codec provide device behavior behind existing sound/cinematic APIs; Steam integration remains omitted. |
| Multiplayer and dedicated-server controls | Not compiled into the initial offline SP target. They return only with the documented browser transport/server milestone, not as inert SP dvars. |

## Shared renderer and transport helpers

`web_renderer_material_lookup.h` shares identical image/constant table searches
across world, static-model, and DObj rendering. Technique selection and water
handling remain local. `worker_transport.mjs` shares only request bookkeeping;
production and diagnostic protocols, timeout/recovery policy, and filesystem
ownership remain separate. These are platform boundary utilities, not new
material or engine object models.

The single-surface proof is retired. Its relevant finite lightmap-coordinate
check lives in `WebRenderer_BuildWorldSceneCommand`; canonical vertex layout
assertions remain in `gfx_world_types.h`. Proof-only projection/mirror assertions
have no consumer. Surface APIs used by UI, overlays, and actual rendering remain.

Canonical camera visibility now runs through `gfx_d3d/r_dpvs_core.*`: extracted
native view setup/reset, portal clipping/queueing and static AABB traversal use
`GfxWorld`, `GfxViewParms` and `DpvsGlobals`. Native adapters retain worker jobs
and debug drawing; the engine Worker dispatches static cells synchronously.
There are no fake renderer globals or browser culling algorithms.

Every submitted view recomputes camera slot 0. Completion is explicit, including
all-zero results; loaded bytes and prior world/view results are never accepted
as completion. The backend snapshots the mask and repacks camera groups when
visibility or LOD changes, using `canonicalInstanceIndex` across light groups.
Shadow LOD ranges remain intact in the first half of the existing instance
buffer; camera ranges occupy the second half. This adds one instance-capacity
region in CPU/GPU storage plus a byte mask, without another GPU buffer. See
[the static-model visibility record](evidence/static-camera-visibility-2026-08-31.md)
for bounded synthetic execution and production compile evidence, not retail
visual or performance claims.

Static sun shadows now apply a separate light-space visibility mask over that
first-half LOD packing. Canonical `GfxStaticModelInst` AABBs cross the portable
boundary; each near/far matrix overwrites one backend byte per retained instance,
then only contiguous visible runs are submitted. Camera visibility is never
consulted, off-camera casters remain eligible, and spot shadows retain authored
canonical membership. This is the native-shaped partition seam, not a browser
engine model or pause cache. The
[upload follow-up](evidence/static-instance-uploads-ac8b00ca.md) retains the
24-byte AABBs only in CPU source/shadow-packing vectors. The 72-byte GPU record
contains shader and LOD/cull data, and visibility-only updates transfer only its
camera-packed half. Canonical `GfxStaticModelInst`, DPVS, and authored light
membership remain authoritative. See
[the partition evidence](evidence/static-sun-partitions-cc4af645.md).

Authored static spot membership now follows the same packed-mask shape. The
[spot milestone](evidence/static-spot-membership-26b3dc98.md) translates the
selected light's sorted `GfxShadowGeometry::smodelIndex` records once, then all
model surface batches scan the resulting LOD-packed bytes. Camera DPVS, sun
partition bytes, canonical asset identity, and GPU resource ownership remain
separate.

The [Kisak renderer optimization audit](kisak-renderer-optimization-audit.md)
tracks applicable native techniques to closure. The
[BSP sun milestone](evidence/bsp-sun-partitions-a23850aa.md) carries canonical
`GfxSurface` AABBs with retained spans and builds each cascade's light-space
selection independently. Camera DPVS is absent, range holes and alpha-tested
boundaries remain explicit, and adjacent opaque spans can still merge.
Dynamic sun draws now retain one world-space AABB after DObj/DynEnt assembly or
brush placement and test it against each sun matrix independently. This
platform-equivalent seam covers the five native dynamic families without
retaining entity state; it costs 24 bytes per flattened draw and one indexed
bounds scan per dynamic submission. See
[the dynamic sun evidence](evidence/dynamic-sun-partitions-6ece6ee9.md).
Dynamic spot draws now use the same retained bounds independently against each
selected perspective light matrix. DObj, DynEnt XModel, moving-brush, and
DynEnt-brush materials require the remap-aware build-shadowmap technique and
carry its cull/alpha state; camera and sun bytes remain absent. The
[primary-light closure](evidence/dynamic-primary-light-linkage-4ed38a84.md)
restores canonical entity/DynEnt visibility bits and authored light-region
hulls, then builds a selected-light mask before the existing matrix test.
Missing canonical arrays fall back conservatively to matrix-only selection.

The [dynamic geometry ownership milestone](evidence/dynamic-geometry-ownership-af601efe.md)
removes the remaining full CPU copy at the frontend/backend boundary. The
frontend transfers only final numeric vertex/index vector storage; the backend
retains its existing validation, GPU upload, recovery data and atomic command
publication. Failure returns ownership to the caller. World and static-model
commands, camera DPVS, primary-light linkage, and independent shadow selections
are unchanged. A measured inactive-buffer WebGL candidate was reverted after it
failed to reduce upload cost. Further work now begins at shared DObj
pose/lighting/skinning behavior rather than expanding platform GPU ownership.

The [canonical weighted-basis milestone](evidence/dobj-primary-basis-82b4de10.md)
makes the portable DObj skinner follow Kisak's native scalar and SSE rule:
positions blend across influences, while normals and tangents use the primary
bone. This deletes a browser-only behavioral divergence and its secondary basis
matrix work. Pose generation, packed decoding, finite checks, LOD/hide policy,
static-model camera culling and independent shadow selection are unchanged.

The [DObj lighting-handle milestone](evidence/dobj-lighting-cache-a16fb9f2.md)
uses canonical `cpose_t::lightingHandle` identity to reuse exact-origin
light-grid results. The platform retains only numeric sample payloads and
clears them on world unload; the per-frame atlas still follows submitted DObj
order. Changed origins recompute, and the handle now also reaches canonical
mark generation. Exact-work diagnostics observed 87.3% lower DObj lighting
time without changing static culling or independent shadow selection.

The [dynamic DObj convergence milestone](evidence/dobj-rigid-decode-3e65c7d0.md)
measures matrix, weighted and rigid skinning separately, then removes repeated
packed-attribute decoding from the dominant rigid path. The bounded cache owns
only immutable decoded vertex values and source identity until world unload;
current canonical bone matrices still produce every final vertex. Exact-work
diagnostics observed 22.3% lower rigid skinning, 8.4% lower DObj build and 6.6%
lower renderer-frontend time. A GPU rigid-placement candidate was rejected
because it changed shadow partition work and raised total time. Static-model
camera culling and independent sun/spot shadow selection remain intact.

Dynamic camera submission now mirrors Kisak's material draw-surface ordering
inside proven-safe opaque runs. Blended, depth-equal/disabled, no-color, FX,
sun, and other state-sensitive batches remain append-order anchors, and shadow
draw order is untouched. The platform retains one numeric key per batch and
one 32-bit camera-order index per dynamic draw; no engine identity or geometry
copy is added. See
[the dynamic order evidence](evidence/dynamic-opaque-sort-c8c4f335.md).

World camera visibility now extends that same canonical call through AABB
surface tests and cell cull groups, including native decal selection. It resets
the complete DB-allocated `staticSurfaceCount` camera mask, and checks sorted
surface ranges/indices and cull-group references before use. Shadow view bytes
and authored caster membership are untouched.

World draw commands retain each emitted canonical surface ID and its original
index-buffer span inside a merged material batch. The backend validates and
retains those spans, then builds contiguous visible camera runs on every view
submission. Empty completed masks produce no world camera draws; unavailable
or incorrectly sized results fail submission. Sun batches and authored spot
caster ranges keep the original index buffer, independent of camera visibility.
Static-model packing and LOD policy are unchanged.

This is permanent draw-command boundary metadata, not another world model.
It costs 40 bytes per emitted surface, plus up to 16 bytes per surface for
camera-run capacity and one reused 16-byte-per-range sun scratch vector. Camera
submission scans emitted surfaces once; sun drawing scans them once per
cascade. No extra geometry buffer, index upload, mask cache or JS visibility
state is added. World replacement clears runs; unload releases all vectors;
context restoration reuses retained geometry/spans. Retire the spans only if
canonical frontend draw commands directly provide the backend ranges. See
[world visibility evidence](evidence/world-camera-visibility-2026-08-31.md).

Diagnostics add five DObj CPU measurements (total build and four disjoint
substages) to the existing frame profiler. Production compiles them out. Pose,
lighting, and skinning ownership is unchanged; no DObj pose or geometry cache was added.
The [first focused stage profile](evidence/dobj-stages-946dc918.md), from a
120-frame headless CargoShip window, places geometry construction at 59.02% of
DObj build time. The subsequent [DObj-only hash deletion](evidence/dobj-hash-d8661476.md)
removes unused bytecode scans without changing canonical material identity or
world/static-model hashing. Matching short profiles observed 7.192 -> 6.347 ms
in geometry construction; no comparable foreground FPS result is claimed.

Six additional diagnostic intervals partition non-DObj scene work into setup,
command assembly, dynamic image resolution, dynamic submission, camera DPVS and
view submission. They extend existing telemetry only; canonical ownership and
the frontend/backend seam are unchanged. Upload timing nests within submission,
and all timers compile out of production. The
[focused scene profile](evidence/scene-stages-2ce03241.md) places 13.801 ms in
assembly and 4.638 ms in dynamic submission, prompting the finer attribution
below. No cache or runtime model was added.

Assembly diagnostics now distinguish physics/mark preparation, brush/model
work and command appends, with a nested particle-cloud append interval. The
[cloud-append comparison](evidence/cloud-append-ae37e80c.md) identified repeated
exact vector reservations as the dominant assembly cost. Removing those three
reservations lets standard vectors grow normally while preserving validation,
atomic logical rollback, draw order and canonical ownership. This remains a
platform-owned command-storage detail; no shared engine behavior or persistent
cache was added. Observed cloud append fell from 9.676 to 0.865 ms. Spare vector
capacity and the higher observed dynamic-submission cost need consideration in
the next optimization; no general FPS or memory reduction is claimed.

The [dynamic-staging continuation](evidence/dynamic-staging-9403a899.md) separates
CPU command copy, GPU resource setup and publication. Two backend-owned staging
vectors reuse prior dynamic geometry allocations, while published data remains
intact until upload succeeds. The portable staging-copy helper consumes already
validated spans; validation and canonical state ownership are unchanged. Unload
releases staging, and context recovery still consumes only published geometry.
This is platform-owned command storage, not a pose or geometry-result cache.
Keep it while the measured copy-tail reduction justifies the approximately
17 MiB staging capacity; retire it if frontend/backend geometry ownership can
be transferred directly without weakening publication or recovery semantics.
No whole-frame speedup or peak-memory reduction was measured.

The [dynamic texture continuation](evidence/dynamic-textures-74fe11aa.md) avoids
reapplying an identical six-texture/four-sampler set within the dynamic draw
block. It preserves the order of all six calls whenever any field changes,
because GL sampler parameters belong to texture objects and units can alias.
This is permanent backend-owned state handling, with a stack-local value reset
each frame and no canonical state, new resource or persistent cache. The helper
requires exclusive ownership of those bindings/parameters during the block;
reset it if a future path changes them. World/static culling and independent
shadow passes are unchanged. Diagnostic material/texture/draw intervals measured
a local texture-setup reduction, without a whole-frame CPU improvement.

The [falloff-uniform guard](evidence/falloff-uniforms-12ac17e5.md) keeps all three
falloff uploads on distance-falloff draws and omits them for other techniques,
whose shaders do not read those values. The shared backend material helper
owns this permanent uniform policy; canonical constants, validation and shader
arithmetic stay unchanged. No tracker or intermediate representation is added.
Observed dynamic material CPU cost fell in a focused comparison, with no
general FPS or visual-compatibility claim.

The [completed renderer CPU milestone](evidence/renderer-cpu-milestone-e4db91df.md)
adds pass-local backend state reuse for projections, material inputs and dynamic
feature flags, plus shadow-partition alpha/cull state. It also omits unused
detail and model-lighting uniform uploads. These are permanent GL-boundary
details: canonical commands, identities, validation, culling and independent
caster ranges remain unchanged. No GPU resources, persistent cache or engine
representation is added. Immutable batch/matrix contents and explicit reset
after direct overrides are required; new material inputs must extend the key.
Production frame timing observed a modest gain with overlapping run ranges.
The local renderer milestone is closed; canonical scene construction is the
next measured CPU priority.

The [direct DObj emission change](evidence/dobj-emission-fb596702.md) now constructs
vertices in the private replacement vector, removing a temporary vertex copy.
This remains a permanent platform draw-command detail, with no new retained
state or intermediate object model. Canonical pose, skinning, LOD and hide policy
remain shared inputs; validation and whole-command publication are preserved.
Focused execution covers emitted values and rejection without publication.
This prompted the brush construction/append measurement below.

The [brush cost investigation](evidence/brush-costs-f15c3dc9.md) retained diagnostic
attribution and a world/brush output oracle. Both proposed runtime changes were
reverted after production timing failed to support them. At that milestone,
geometry, hashing, technique selection and batch merge policy were unchanged;
no new cache, allocation policy or engine representation was added. Retained artifacts now
support interleaved comparisons before further optimization.

The [controlled timing follow-up](evidence/controlled-renderer-552a468d.md)
moves `Com_ModifyMsec` outside the temporary common.cpp compile gate and calls
it in the browser pump at the native pre-server boundary. Its single native
body owns fixedtime, time scaling and clamping for both paths; the browser
still owns only nonblocking callback admission. No prefix function was copied.
Canonical pause/free-move commands and existing refdef events now qualify a
paused renderer benchmark. The Node runner owns only test orchestration and
comparison; it does not own game state or a replay format. Exact sampled camera,
time and world-count matching is demonstrated, not complete dynamic state or
active-gameplay determinism. Culling and independent shadow paths are unchanged.

The [paused-copy follow-up](evidence/paused-copy-qualification-cd85e18e.md)
aligns diagnostic profiling with that scene and records actual geometry work.
It reproduced different index/upload totals across fresh loads of the same
Wasm, despite matching camera and draw counts. The name-copy experiment was
reverted; this milestone adds test orchestration and qualification only, with
no net engine/renderer changes. Resolving the remaining variation belongs in
canonical model/LOD and seed/save/replay behavior, not a browser game-state copy.

The [seeded brush follow-up](evidence/seeded-brush-hashes-06ad8004.md) partitions
accepted dynamic/UI geometry using four diagnostic-only counters. Variation was
in dynamic commands; the shared optional map seed produced matching measured
workloads across fresh loads. The runner owns only orchestration and metadata.
It does not author model, pose, random-generator or replay state in JavaScript.
The brush draw-command builder now reuses only the preceding shader hashes in
a stack record scoped to one synchronous build. This adds no persistent pointer,
heap allocation or global cache. Technique/material selection, output geometry,
atomic publication, canonical culling and independent shadows are unchanged.

## Temporary compatibility seams

| Seam | Retirement condition |
| --- | --- |
| Diagnostic launcher and C++ test exports | Keep only controls that exercise browser-only failure/recovery behavior; do not add product behavior here. |
| Normalized runtime telemetry | Retain while it catches ownership/order regressions; delete fields once no test or operational diagnosis consumes them. |

The former Gate 2 retail census, custom fastfile traversal, synthetic world
extraction, cooperative qcommon shell, scheduler, renderer comparison, and
proof jobs are retired. Canonical runtime tests replace their useful coverage.

## Native-only or excluded systems

| System | Browser position |
| --- | --- |
| Direct3D 9 and Win32 window/input code | Native-only; WebGL2 owns the browser backend. |
| Bink, Miles and Steam binaries | Never linked or distributed. |
| Raw UDP networking | Impossible in browsers; multiplayer requires an explicit gateway/transport design. |
| Dedicated server and Radiant | Excluded from the initial Wasm client target. |
| Loose editor/import development paths | Native-only unless a concrete browser workflow needs them. |

## Remaining work

The DObj conversion milestone keeps canonical pose generation, LOD/hide masks,
material resolution and lighting inputs unchanged. Final vertices are assembled
in one skinning pass, and only empty numeric geometry capacity survives frontend
submission. Selective web LTO exposes the shared Kisak packing and quaternion
helpers to that loop; no browser copies of their implementations were added.
Dynamic opaque sun ranges now use the same range-joining implementation as world
shadows, with placement/buffer identity preserved. See
[renderer resource ownership](renderer-retained-resources.md#dobj-conversion-and-dynamic-sun-ranges).
This is a renderer-boundary improvement, not expanded gameplay compatibility.
The [delivered evidence](evidence/dobj-conversion-30e34cff.md) records focused
checks, exact logical-work qualification, recovery and one final Release,
separating diagnostic DObj-stage reductions from the observed 7.54% production
pair-mean reduction and its control drift.

Dynamic DObj pose, weighted basis, lighting reuse, rigid transformation,
culling and shadow submission now converge on canonical Kisak behavior. The
final measured rigid path retains immutable packed-attribute decoding while
preserving the existing draw command and exact shadow work. This closes the
current renderer-frontend optimization path; further renderer changes should
respond to evidence from broader canonical gameplay rather than extend the
bootstrap as a standalone viewer.

The playable offline slice is already demonstrated across six campaign maps.
The mission-route author/replay and simulated-progression layer is retired with
no replacement. The first observe-only expansion run establishes `scoutsniper`
as `RENDERS` after canonical lifecycle completion and a 60-second stationary
window; it makes no visual, functional, or playable claim. See
[the stationary evidence](evidence/scoutsniper-stationary-838e047c.md).

- Probe `ac130` stationary next and close only measured thermal/material gaps,
  without introducing browser asset types or simulated gameplay.
- Qualify the source-built cinematic path against Steam: playback timing,
  synchronization, recovery, and in-world materials. Missing movies still
  report an explicit omission.
- Add gamepad input when it becomes a product requirement.
- Continue measuring the encoded-source recovery policy on later campaign
  batches. Consider source deduplication or reload from imported storage only
  if current retained-source or recovery-latency evidence justifies another
  strategy; profile streaming and Worker scheduling before considering
  pthreads.
- Design and document a gateway before compiling multiplayer transport code.

## Historical retail and memory evidence

Historical map classifications and their sources are consolidated in
[web-status.md](web-status.md) and [the campaign matrix](campaign-compatibility.md).
They were not rerun during this cleanup. Mission progression is not a prerequisite.

### Encoded recovery sources

Commit `c66d41e1` changed one recovery strategy. The world, static-model,
dynamic-model, and UI 2D pools validate each selected image through the canonical encoded layout and decode
once for its initial WebGL upload (the later `919f8c27` change), but they no
longer retain that decoded RGBA allocation. They retain either the exact DB
load-definition metadata and payload, the complete IWI member read through the
canonical filesystem, or the tiny raw `$white` pixels. Context restoration
decodes one image at a time through the existing Kisak image decoder and then
releases that temporary decoded buffer.

Sky/reflection cubes, lighting atlases, water, render targets, and other
supplemental paths were not converted by this strategy. Telemetry consequently
distinguishes:

- `textureRecoverySourceBytes`: actual CPU texture sources retained for
  recovery;
- `decodedTextureSourceBytes`: logical decoded texture size used for admission
  and GPU estimates, not retained RGBA bytes for the converted pools;
- `recoveryCopyBytes`: actual texture recovery sources plus retained geometry
  and shader/program recovery estimates; and
- `temporaryUploadBytes`: the largest transient decoded upload observed.

The unchanged 800 MiB limit remains a decoded-byte admission limit for each
shared image pool, not an aggregate actual-recovery budget. It was neither
raised nor arbitrarily lowered.

The A-F classes are A (irreplaceable), B (cheaply regenerable), C
(reloadable/redecodable from canonical imported source), D (duplicated by
content), E (process-global), and F (map-local):

| Resource | Classes | Lifecycle/ownership |
| --- | --- | --- |
| Retained LoadDef/IWI 2D recovery sources | B/C/F; LoadDef copies are also D while cached | Map-local renderer-backend recovery data derived from canonical DB or filesystem sources; discarded on map retirement. |
| Transient decoded 2D pixels | B | Recreated one image at a time for upload or context recovery and then released; the measured peak was 16,777,216 B. |
| Decoded supplemental texture recovery | B/C/F | Existing map-local cube, lighting, water, and related paths; not changed by `c66d41e1`. |
| DB LoadDef source cache | C/D/E | Bounded process-global cache at the canonical generated-image upload seam. A renderer LoadDef recovery copy duplicates its selected cached payload so later cache replacement cannot invalidate live recovery. |
| Retained geometry and portable draw commands | B/F | Regenerable map-local renderer data. |
| GPU map textures and buffers | B/F | Regenerable backend resources; destroyed and recreated without becoming engine state. |
| Render targets and fixed pipeline programs | B/E | Intentionally process-global backend resources. |
| Audio data | B/C, predominantly F | Canonical audio feeding the browser device boundary. |
| Wasm linear-memory capacity | Not a retained-resource class | Allocator capacity, reported separately from renderer ownership. |

No class-A retained resource was observed. Content identity across distinct
image assets has not been measured; class D above is limited to the traced
LoadDef cache/recovery copy of the same payload.

On the same headed-Chrome Killhouse comparison point, the instrumentation-only
implementation and encoded-source implementation measured:

| Metric | Decoded retention | Encoded sources | Reduction |
| --- | ---: | ---: | ---: |
| Aggregate renderer recovery | 1,417,257,708 B | 506,423,759 B | 64.27% |
| Allocator bytes in use | 1,731,297,456 B | 820,736,432 B | 52.59% |
| Wasm linear-memory capacity | 1,809,121,280 B | 989,921,280 B | 45.28% |
| Program break | 1,808,785,408 B | 901,816,320 B | 50.14% |

The final clean Chrome Killhouse run at `f5229806` reached its first frame in
5,710.910 ms and reported 1,419,469,864 B logical decoded textures,
427,163,511 B actual retained texture sources, 396,164,047 B encoded image
sources, 518,433,483 B aggregate renderer recovery, 1,440,467,016 B estimated
GPU textures, 91,269,972 B geometry, and 989,921,280 B Wasm capacity. Edge
reached the Killhouse first frame in 5,693.485 ms and reported 518,844,075 B
aggregate recovery at the same 989,921,280 B capacity.

Re-decoding trades some restoration latency for the memory reduction. Chrome
restored CargoShip in 1,357.375 ms, Blackout in 1,790.265 ms, and returned
Killhouse in 1,960.945 ms. Edge measured 1,349.035 ms, 1,849.430 ms, and
1,893.305 ms respectively. Every
recovery resumed real world frames and gameplay input, so the strategy passed
the clean Chrome and Edge acceptance matrices without asset corruption or a
map-lifecycle regression. Continue recording this latency rather than treating
the memory saving as free.

## Verification scope

The 2026-08-31 static-camera continuation uses the bounded checks recorded
in [the visibility evidence](evidence/static-camera-visibility-2026-08-31.md).
The [renderer handoff](evidence/renderer-efficiency-2026-08-31.md) is prior evidence. The
[cleanup record](evidence/cleanup-renderer-2026-08-31.md) is earlier evidence.
No routine full tier or mission-flow gate applies to this work. Native/Wasm parser tests remain
authoritative for cases that do not require a browser boundary; retail checks
require legally owned local files and are never routine CI fixtures.
