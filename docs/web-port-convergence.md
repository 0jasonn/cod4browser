# Web port convergence inventory

This is the current ownership map for the browser port. Upstream KisakCOD
types and behavior remain canonical; browser code exists only at platform
boundaries. Historical milestone evidence belongs in `docs/history/` and Git,
not in this inventory.

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

## Modified Kisak seams

| Seam | Why it differs |
| --- | --- |
| `database/db_file_platform.cpp` | Maps DB file operations to the Worker filesystem. |
| `qcommon/common_runtime_commands.cpp` | Keeps the post-mount common-command continuation canonical and separate from browser hosting. |
| `web_client_server_lifecycle.cpp` | Continues synchronous-looking native startup after the main-thread host mounts user files. |
| `web_canonical_gfxworld.cpp` | Observes final DB publication and submits the bounded compatibility surface; the full renderer frontend remains the long-term owner. |
| `web_renderer_frontend.cpp` | Converts canonical renderer state into backend-neutral commands. |
| `web_system*.cpp` | Supplies browser timing, frame pump, files, events and thread-context behavior. |

Changes in these seams should preserve native behavior unless the browser
platform makes that behavior impossible.

## Permanent web platform implementations

| Boundary | Implementation |
| --- | --- |
| Host/Worker split | DOM, picker and persistent-storage ownership stay on the main thread; Wasm and OffscreenCanvas run in a Worker. |
| Storage | OPFS/IndexedDB-backed import, validation, atomic replacement and synchronous Worker reads. File System Access is optional. |
| Rendering | WebGL2 context, buffers, textures, shaders, render targets, context recovery and presentation. GPU handles stay private to the backend. |
| Input host | Pointer lock, keyboard/mouse normalization, focus release and cursor mode. |
| Audio device | AudioContext policy, gesture resume, buffers/nodes and PCM scheduling. |
| Main loop | Non-blocking Emscripten frame pump; no Asyncify or pthread requirement. |
| Cinematics | Native Bink is omitted safely until a browser-owned replacement is chosen. |

## Temporary compatibility seams

| Seam | Retirement condition |
| --- | --- |
| `web_engine_world_surface.*` and the bounded publication call in `web_canonical_gfxworld.cpp` | Remove when all useful validation and fallback rendering use the canonical renderer frontend world command. |
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
- Carry the measured Phase 2 renderer-resource classifications forward and
  gather comparable map evidence before changing recovery policy; profile
  streaming and Worker scheduling before considering pthreads.
- Design and document a gateway before compiling multiplayer transport code.
- Retire the bounded `web_engine_world_surface` compatibility path once its
  remaining tests are covered by the canonical frontend.

## Current retail and memory evidence

The strengthened Killhouse/CargoShip matrix passed on 2026-08-26 at clean
source SHA `ac063bb20cbc4027497841322d87c2069d736939`. Both maps reached
canonical DB publication, cgame initialization, real world frames, at least
60 seconds of sustained rendering, input, audio, and configuration
checkpoint/reload. The transition and forced WebGL context recovery also
passed. Exact values are in
[retail-phase1-ac063bb2.json](evidence/retail-phase1-ac063bb2.json).

The first Phase 3 campaign batch then validated Blackout as PLAYABLE on the
same date at clean source SHA
`6be926cb4e78693f9f6e638c348b0ee0f908b45f`. A browser-only 20,000
static-model-instance ceiling initially rejected canonical spot-shadow
instance 20,000 after DB/server/game/cgame startup. Commit `164fc1f2` restored
native IW3's 65,536-instance bound at the portable renderer seam. CargoShip to
Blackout to Killhouse transitions, a 60-second Blackout window, input, audio,
configuration persistence, context recovery, and shutdown/reload then passed.
Exact values are in
[retail-phase3-6be926cb.json](evidence/retail-phase3-6be926cb.json).

The renderer telemetry categories are disjoint. Their current lifecycle
classification is:

| Resource | Lifecycle/ownership |
| --- | --- |
| Decoded world/static/dynamic/UI/supplemental textures | Regenerable, reloadable from user storage, map-local |
| Retained geometry and portable draw commands | Regenerable, map-local |
| GPU map textures and buffers | Regenerable, map-local |
| Render targets and fixed pipeline programs | Regenerable, intentionally global |
| Shader source/cache | Regenerable, intentionally global; 0 B measured in Phase 1 |
| Temporary uploads | Regenerable, map-local; 0 B measured in Phase 1 |
| Audio data | Regenerable, reloadable, predominantly map-local |
| Wasm linear-memory capacity | Allocator capacity; outside retained renderer ownership |

No irreplaceable retained resource was observed; cross-pool/content duplicates
were not measured. Phase 2 keeps the existing previous-map recovery eviction:
old aggregate CPU recovery fell from 1,521,922,580 B to zero before new-world
publication, so no old/new overlap was observed. The 800 MiB limit remains a
per retained-image-pool admission cap. Killhouse's static-model pool reached
817,908,800 of 838,860,800 B (97.50%, 20,952,000 B headroom), so lowering it
would be unsupported. Wasm capacity remaining at 2,013,724,672 B reflects
monotonic allocator capacity, not an eviction failure. These measurements are
a baseline; no unmeasured before/after (A/B) timing is claimed. Authored mip
levels are included in dynamic/UI pool admission.

## Verification

Routine handoff checks are:

```text
npm.cmd ci
npm.cmd run check:web:static
tools/build_web.ps1 -Configuration Release
npm.cmd run check:web:product
npm.cmd run test:browser:product
tools/build_web.ps1 -Configuration Release -Diagnostics
npm.cmd run test:browser
npm.cmd run test:browser:remainder
```

Native/Wasm parser tests remain authoritative for cases that do not require a
browser boundary. Local retail validation requires a user-provided legal COD4
installation and is never a routine CI fixture.
