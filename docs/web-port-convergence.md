# Web port convergence inventory

Updated 2026-08-31. This page owns system classification; see
[current status](web-status.md) and [the active roadmap](web-roadmap.md) for
evidence and priorities. Earlier chronology is retained in
[the historical inventory](history/web-convergence-through-2026-08-28.md).

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
| Common startup | Canonical `Dvar_Init` and the strict `Com_Init` prefix run in native order; the host mounts browser storage at the filesystem boundary, then the continuation restores canonical common-command registration before script/server/client startup. |
| Filesystem | Canonical search paths, IWD/minizip behavior, config/profile calls and synchronous engine-facing operations use Worker file primitives. |
| Database | Canonical XFile stream, allocation blocks, generated loaders, pointer aliases, registry pools, dependency ordering and final publication own runtime assets, including native-compatible leading-comma asset-stub resolution. |
| World/runtime | Canonical `GfxWorld`, collision, server/game, client/cgame, script VM, XAnim/DObj, effects, ragdoll, physics and sound code are in the browser link closure. |
| Frame order | The browser supplies elapsed time; canonical `SV_Frame`, client frame work and `SCR_UpdateScreen` advance gameplay. |
| Renderer frontend | Kisak world, model, effect and UI state is translated only at the portable draw-command boundary. Native IW3's bounded 65,536 static-model cardinality is preserved across that seam. |
| Input | Browser events enter canonical key/mouse queues, bindings, usercmd creation and movement/weapon code. |
| Audio | Canonical mixer and OpenAL-facing state feed a browser Web Audio device boundary. |
| Save/persistence | Canonical game save serialization and load own gameplay state; the browser host only persists and flushes the engine filesystem at the platform boundary. |

## Modified Kisak seams

| Seam | Why it differs |
| --- | --- |
| `database/db_file_platform.cpp` | Maps DB file operations to the Worker filesystem. |
| `database/db_generated_image_platform.*` | Copies transient canonical image load definitions at the native texture-upload boundary into a bounded process-global source cache; canonical `GfxImage` identity remains authoritative. |
| `qcommon/common_runtime_commands.cpp` | Keeps the post-mount common-command continuation canonical and separate from browser hosting. |
| `web_client_server_lifecycle.cpp` | Continues synchronous-looking native startup after the main-thread host mounts user files. |
| `web_canonical_gfxworld.cpp` | Observes final DB publication only; canonical `R_RenderScene` owns world rendering. The obsolete proof submission is removed. |
| `web_renderer_frontend.cpp` | Converts canonical renderer state into backend-neutral commands. |
| `web_system*.cpp` | Supplies browser timing, frame pump, files, events and thread-context behavior. |

Changes in these seams should preserve native behavior unless the browser
platform makes that behavior impossible.

## Permanent web platform implementations

| Boundary | Implementation |
| --- | --- |
| Host/Worker split | DOM, picker and persistent-storage ownership stay on the main thread; Wasm and OffscreenCanvas run in a Worker. |
| Storage | OPFS/IndexedDB-backed import, validation, atomic replacement and synchronous Worker reads. File System Access is optional. |
| Rendering | WebGL2 context, buffers, textures, shaders, render targets, context recovery and presentation. GPU handles stay private to the backend. The shared 2D image pools retain the selected canonical encoded source and use the existing image decoder transiently for initial upload and context restoration; this is recovery data at the platform boundary, not a second asset model or parser. |
| Input host | Pointer lock, keyboard/mouse normalization, focus release and cursor mode. |
| Audio device | AudioContext policy, gesture resume, buffers/nodes and PCM scheduling. |
| Main loop | Non-blocking Emscripten frame pump; no Asyncify or pthread requirement. |
| Cinematics | Native Bink is omitted safely until a browser-owned replacement is chosen. |

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

Static-model LODs are still evaluated each frame; unchanged groups retain
packed instances/ranges, while initial population and culling transitions
repack. Empty batches skip camera and shadow material setup. Camera draw
ranges are now explicit and separate from shadow ranges, but use the same
conservative LOD-packed buffer until canonical DPVS setup/traversal is admitted.
No camera visibility array is consumed yet. The
[renderer record](evidence/renderer-efficiency-2026-08-31.md) documents the
native global/view/reset and dispatch blockers. Original canonical instance
indices survive grouping and LODs; no JavaScript visibility state was added.

Diagnostics add five DObj CPU measurements (total build and four disjoint
substages) to the existing frame profiler. Production compiles them out. Pose,
lighting, and skinning ownership is unchanged; no pose or geometry cache was added.

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

- Broaden campaign coverage and close renderer/material gaps from measured
  canonical scenes, without introducing browser asset types.
- Add a legal browser cinematic path or a documented graceful omission.
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

The 2026-08-31 renderer continuation uses the per-step bounded checks recorded
in [the renderer evidence](evidence/renderer-efficiency-2026-08-31.md). The
[cleanup record](evidence/cleanup-renderer-2026-08-31.md) is earlier evidence.
No routine full tier or mission-flow gate applies to this work. Native/Wasm parser tests remain
authoritative for cases that do not require a browser boundary; retail checks
require legally owned local files and are never routine CI fixtures.
