# Web product status

Consolidated 2026-09-09. This page summarizes recorded implementation and
qualification; it does not certify the current checkout. The
[roadmap](web-roadmap.md) owns priorities, the
[convergence inventory](web-port-convergence.md) owns system classification,
and the [test inventory](web-test-inventory.md) owns execution results.

## Verification and reference baseline

The 2026-09-09 cleanup working tree passes 44 native and 43 direct-Wasm CTest
cases, 124 Node cases, 56 production Chromium cases, 10 diagnostic smoke cases
and 64 remainder cases, with 16 optional skips. Native SP and both Release
browser builds pass, as do the unchanged production size/export boundary and
14 source/package tests. See the [execution record](web-test-inventory.md#current-execution-evidence)
for exact scope and limits. These local synthetic checks do not establish
aggregate release or campaign acceptance.

The [Steam inventory](evidence/steam-reference-2026-09-02.json) pins installation
hashes and configured settings. The [native SP reference](evidence/native-reference-2026-09-02.md)
builds with pinned MSVC/OpenAL, loads owned startup fastfiles and enters menu
code. Original/native/browser visual and gameplay comparison remains open.

## Current campaign boundary

The [campaign matrix](campaign-compatibility.md) owns exact sources and claims:
Killhouse and Airplane have historical `PLAYABLE` evidence; CargoShip, Blackout,
Hunted and Bog A have `FUNCTIONAL` evidence; Scoutsniper and AC130 have separate
`RENDERS` evidence; 14 discovered direct SP zones remain `UNTESTED`.
Compile, boot, render, functional, playable and mission-complete are separate
evidence levels. No mission has established completion or full retail fidelity.

Shipped New Game has observed natural Killhouse rifle, timed-shooting and
sidearm/melee training, three authored checkpoints, and Resume Game after a
fresh browser start. Acceptance stops at the Captain Price/ladder platform;
the course, CargoShip chapter flow and next transition remain unverified.
Save and Quit now unwinds through the canonical disconnect/error boundary,
shuts down client/server and reloads UI without losing the Worker. See
[training and disconnect evidence](evidence/campaign-training-disconnect-2026-09-02.md).

Airplane separately proves save/reload continuity for live AI/scripts/objective
state, combat, saves, death/restart, browser shutdown and continued play. It
does not prove objective/trigger progression. The user owns manual gameplay,
mission completion and matched reference acceptance; implementation work does
not depend on mission completion. The route-author/replay and simulated
progression systems are retired.

## Runtime and platform

| Area | Implemented boundary and evidence | Remaining qualification |
| --- | --- | --- |
| Engine/database | Canonical `Com_Init`, DB/XFile, ClipMap, server/game, client/cgame and renderer frontend run in a dedicated Worker. Native loader oracles cover RawFile, Material/Image and XModel/PhysPreset; request rollback, singleton replacement, pool/PMem retirement and image/string ownership have native/Wasm tests. See [architecture](web-architecture.md#runtime-ownership). | Broader multi-zone graphs, malformed nested assets, non-world device side effects and remaining original-loader families. |
| Loading and errors | JSPI keeps synchronous-looking Kisak loading stacks responsive; native Wasm exceptions and canonical error cleanup preserve recovery. Mount errors carry canonical text through Worker requests. Progress renews stall deadlines, never the absolute deadline. See [loading architecture](cinematic-codec.md) and [protocol](web-architecture.md#product-protocol). | Nested errors, broader transition/recovery journeys, long stalls and campaign acceptance. Pre-mount errors remain terminal. |
| Storage/distribution | Imported assets stay local; browser home replacement is journaled, live/queued storage is bounded, and export/reimport and same-origin restart have synthetic checks. Source/dependency receipts bind release contents. See [browser support](browser-support.md). | Linux/hosted sanitizer and aggregate CI execution, actual release-version update/rollback, authorized campaign save/Continue and offline acceptance. |
| Input | Pointer lock, keyboard/mouse and focus release feed native key owners. Windows-1252 characters, editing/repeats and bounded trusted paste reach canonical text/clipboard owners. Served composition accepts supported bytes and rejects unsupported code points. | Windows IME candidate UI, other code pages, localized glyphs and arbitrary clipboard reads. Gamepad follows product requirements. |
| Menus/config/profiles | Canonical startup/pause/options menus, dvars, config writes and profile selection/isolation/deletion survive page/Worker restart. See [menu](evidence/canonical-menu-lifecycle-2026-09-01.md), [config](evidence/canonical-dvar-config-2026-09-01.md) and [profile](evidence/canonical-profile-lifecycle-2026-09-01.md) evidence. | Shipped localized/menu presentation and broader campaign use. Synthetic objective notifications prove UI plumbing, not mission progression. |
| Saves/Quit | Shared save metadata, menu feeders, Continue and deletion own save state. Matching-map capture, shared resampling and bounded JPEG work produce canonical thumbnails; shutdown drains admitted work. Quit flushes the profile and permits restart, retaining the stopped runtime on retryable save failure. See [save/load](evidence/canonical-save-load-2026-09-01.md), [thumbnail](evidence/save-thumbnails-2026-09-02.md), [startup/native image](evidence/save-startup-native-2026-09-02.md) and [Quit](evidence/browser-quit-2026-09-02.md) evidence. | Complete native menu/Steam presentation, localized dates/descriptions and authored chapter acceptance. |
| Display/text | Resolution Apply uses canonical `vid_restart`, refresh is browser-controlled, fullscreen is user-initiated, and brightness reaches the final pass. Shared text supports styles, console text, color, cursor and timed reveal. See [display](evidence/display-options-2026-09-03.md), [gamma](evidence/display-gamma-2026-09-03.md) and [text](evidence/text-presentation-2026-09-02.md) evidence. | Matched Steam display response and text fidelity. |
| Audio/video | Canonical audio feeds Web Audio; generation-tagged device-time feedback owns source completion. OpenAL Soft reverb and EQ stay behind the device boundary; public-source FFmpeg decodes user-imported Bink into canonical cinematic materials. See [reverb](browser-reverb.md), [EQ](evidence/browser-eq-2026-09-02.md) and [codec](cinematic-codec.md) evidence. | Authored room/EQ transitions, callback cost, hardware output latency, arbitrary audio tails, in-world movies and Steam audiovisual comparison. |

## Renderer and performance

Canonical assets, pose, LOD, visibility, material identity and renderer frontend
remain Kisak-owned. Portable commands feed the WebGL2 backend; retained geometry,
encoded texture recovery, instance packing and GPU state belong to that boundary.
See [resource ownership](renderer-retained-resources.md) and the detailed
[convergence inventory](web-port-convergence.md#shared-renderer-and-transport-helpers)
for implementation, native comparisons and individual evidence records.

Implemented paths include canonical BSP/static/dynamic camera admission,
independent sun/spot casters, native shadow-slot fading, transient receivers,
material region/key ordering, model-grid and direct lighting, authored objective
sheen and encountered reflex/lens passes. Sampler, picmip and normal/specular
controls have synthetic pixels and bounded owned-scene restart/recovery checks.
Those checks establish the exercised paths, not general shader or multipass
fidelity. Unknown material families and matched original/native scenes remain open.

| Rendering boundary | Recorded scope and remaining limit |
| --- | --- |
| Transient lighting | Native construction, bounds, portal admission, receiver ordering, shadow selection and scissor math have differential and pixel checks. Owned Killhouse lighting/shadow/clear/recovery has historical passes. A later shadow-history run passes those assertions but fails the final positive DObj post-pose diagnostic; complete current fixture qualification remains open. See [light evidence](evidence/transient-lights-2026-09-02.md). |
| Soft particles/distortion/clouds | Native-shaped FloatZ, authored parameters, distortion resolve and outdoor-cloud masking have synthetic native/Wasm/GPU checks, including real context recovery. Stationary AC130 did not emit an `FxParticleCloud`; Killhouse/CargoShip windows did not activate the recognized distortion snapshot. Owned cloud/distortion recovery, thermal appearance and matched effect fidelity remain unverified. |
| Saved screens | Canonical shellshock timing/intensities feed ordered framebuffer capture/blending. Synthetic pixels and a bounded owned Killhouse material/composite check pass; natural flashbang timing and original-game appearance remain open. See [saved-screen evidence](evidence/saved-screen-2026-09-02.md). |
| Graphics qualification | Installed Chrome/D3D11 exposed a repaired ANGLE HLSL helper-name collision. Three unchanged exact-pixel cases still recorded one-byte differences despite default Chromium passes. Native/browser rounding equivalence is unproven; see [test limits](web-test-inventory.md#known-qualification-limits). |
| Loader fixture | The optional Gate 3 fixture combines owned fastfiles with synthetic IWDs and is not material-fidelity evidence. Its later shadow-only BSP inventory mismatch remains unqualified; production owned-archive checks cover a different boundary. |

Seven headed production windows recorded on 2026-09-05 cover 1280×720 and
1920×1080, real Killhouse recovery, advancing game/audio clocks and 978.5 MiB
Wasm capacity. CargoShip measured 20.63/17.28 FPS in distinct naturally advancing
scenes. Killhouse's floor-facing view and near-60 FPS AC130 windows do not
qualify busy gameplay or thermal fidelity. See [settings, measurements and limits](evidence/browser-frame-time-2026-09-02.md#current-production-measurements--2026-09-05).
Current active CargoShip CPU/GPU stages need isolation before another performance
change. Paused renderer benchmarks and unchanged draw counts cannot establish
current campaign FPS; profile before considering pthreads.

## Historical records and product boundaries

Superseded task diaries and numeric dumps remain retrievable without changing
the checkout. The pre-consolidation status, including per-milestone test totals,
artifact hashes and failed attempts, is:

```powershell
git show 4bca1760f95edb60c362926fa944e96dbcae3f2a:docs/web-status.md
```

The [convergence archive references](web-port-convergence.md#historical-renderer-records)
retain the retired investigations. Historical results do not qualify later code.

The product remains single-threaded Wasm/WebGL2 and offline single-player.
Multiplayer requires a documented browser-compatible gateway; browsers cannot
open COD4 UDP sockets. Proprietary game files and native SDK binaries are never
distributed. Users supply owned data; source releases use the documented
archive exclusions and qualification process. Production and diagnostics
remain separate artifacts, and missing movies remain explicit omissions.
