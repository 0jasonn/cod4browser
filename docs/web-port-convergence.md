# Web port convergence inventory

## 2026-08-28 corrected-roadmap update

The current pass preserves every ownership boundary. Gameplay-profile
sampling observes completed canonical gameplay/render frames; GPU timing and
decode telemetry remain diagnostics-only. The one renderer change reuses only
numeric DObj skinning scratch capacity and retains no canonical object pointer
or duplicate scene state.

Encoded-image publication now calls shared Kisak format/layout validation
without allocating pixels, decodes once for initial WebGL upload, releases the
temporary pixels, and re-decodes the retained canonical encoded source only
after context loss. This removes duplicate work without adding an image parser
or changing asset identity. The seven-stop retail chain proves map-owned source
retirement, bounded process-global cache ownership, and recovery without Wasm
capacity growth.

The mission validator reads canonical game/script/objective/save state only.
Its Village Assault action window did not change a monitored canonical
progression marker, so the validator stopped before save/reload proof. No
browser mission state or hard-coded map behavior was added, and the result is
not classified as a canonical defect without a reproduced canonical boundary
failure.

The focused numeric correction also stays in canonical SP code: typed dvar
parsing and script `floor`/`ceil` replace representation-dependent decompiler
puns, with native and Wasm tests using the same production math seam. The
remaining audit inventory is intentionally classified rather than bulk
rewritten.

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

## 2026-08-27 convergence update

The current WebAssembly fixes preserve canonical Kisak ownership while
removing ABI assumptions that only held when native `long double` was eight
bytes. Actor motion/damage angle wrapping and script trigonometry now compute
through ordinary `double` values without pointer-punning through Wasm's
16-byte `long double`. Map reload also releases game-owned runtime state before
database retirement and resets animation state at the canonical owner
boundary. CargoShip -> Bog A -> Killhouse now passes without non-finite XAnim
state; all temporary diagnostic assertions used to locate the fault were
reverted.

The single renderer change at `9e75a9dd` remains entirely backend-owned. Opaque
shadow casters skip unused texture/sampler binds; alpha-tested casters keep the
same canonical material sampling. The six-map headed Chrome rerun passed every
functional and lifecycle gate and reduced average shadow CPU time 36.91% and
texture binds 32.49%. Full measurements are in
[retail-profile-9e75a9dd.json](evidence/retail-profile-9e75a9dd.json). This does
not introduce an intermediate asset model or move shadow/material ownership
out of Kisak's renderer frontend.

Commit `da1e592c` adds no browser gameplay state. Its diagnostic export reads
canonical server player state, objectives, actors, script threads, and the
game-owned save header/body. The headed Airplane validator then drives the
normal input, `devsave`, `kill`, restart, shutdown, and `loadgame` paths. A
fresh browser runtime restored the named save and continued canonical play;
the sanitized evidence is
[retail-mission-da1e592c.json](evidence/retail-mission-da1e592c.json).

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
| Rendering | WebGL2 context, buffers, textures, shaders, render targets, context recovery and presentation. GPU handles stay private to the backend. The shared 2D image pools retain the selected canonical encoded source and use the existing image decoder transiently for initial upload and context restoration; this is recovery data at the platform boundary, not a second asset model or parser. |
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
- Continue measuring the encoded-source recovery policy on later campaign
  batches. Consider source deduplication or reload from imported storage only
  if current retained-source or recovery-latency evidence justifies another
  strategy; profile streaming and Worker scheduling before considering
  pthreads.
- Design and document a gateway before compiling multiplayer transport code.
- Retire the bounded `web_engine_world_surface` compatibility path once its
  remaining tests are covered by the canonical frontend.

## Current retail and memory evidence

### Foreground baseline and campaign status

At clean source SHA `f5229806`, headed branded Chrome 151 and Edge 151 on the
Windows 11 reference machine each passed the complete
Killhouse -> CargoShip -> Blackout -> Killhouse matrix in 4.6 minutes. Every
timed window was visible and focused, with zero background transitions. Both
browsers passed canonical loading and publication, gameplay input and audio,
ordered map retirement/publication, checkpoint persistence, forced WebGL
context loss/restoration, and the return to Killhouse.

Chrome measured Killhouse at 33.707671 average FPS-equivalent, 32.210 ms p95,
and a 0.999075 game-time/wall-time ratio. Edge measured 35.026568 average
FPS-equivalent, 31.180 ms p95, and a 0.998705 ratio. Against the initial
reference threshold of 30 average FPS, 50 ms p95, and 0.90 ratio, Killhouse is
PLAYABLE; CargoShip and Blackout remain FUNCTIONAL. The threshold describes
this reference hardware, not a universal requirement. The complete sanitized
record, including exact renderer identity, is
[retail-foreground-f5229806.json](evidence/retail-foreground-f5229806.json).

The next clean Chrome campaign batch at source SHA `247980a6` added three
representative maps. Airplane is PLAYABLE at 59.946157 average FPS-equivalent,
18.65 ms p95, and a 0.999992 ratio. Hunted is FUNCTIONAL at 19.079996 average,
55.40 ms p95, and a 0.993989 ratio. Bog A is FUNCTIONAL at 21.210488 average,
56.64 ms p95, and a 0.999457 ratio. Each passed canonical DB/world,
server/game/cgame, a foreground 60-second window, full gameplay input and
audio, checkpoint persistence, transition in and out, and context recovery.
The structured record is
[retail-campaign-247980a6.json](evidence/retail-campaign-247980a6.json).

The current six-map status is therefore:

| Result | Maps |
| --- | --- |
| PLAYABLE | Killhouse, Airplane |
| FUNCTIONAL | CargoShip, Blackout, Hunted, Bog A |
| RENDERS / LOADS / BLOCKED / REGRESSION | None in the validated six-map set |

### Encoded recovery sources

Commit `c66d41e1` changed one recovery strategy. The world, static-model,
dynamic-model, and UI 2D pools still decode each selected image once for
validation, decoded-byte admission, and its initial WebGL upload, but they no
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
