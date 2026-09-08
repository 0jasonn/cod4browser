# Browser architecture and ownership

This guide owns runtime architecture, subsystem classification and platform
invariants. [Current status](web-status.md), [campaign compatibility](campaign-compatibility.md)
and the [test inventory](web-test-inventory.md) own qualification and validation.
Historical measurements remain in Git; they do not establish current fidelity.

## Canonical data flow

```text
user-owned COD4 files
  -> browser import/storage and synchronous Worker file primitives
  -> Kisak filesystem, XFile stream and database loaders
  -> XAsset / XModel / Material / GfxWorld
  -> Kisak client, server, game, cgame, script, collision and animation
  -> renderer frontend -> portable draw commands -> WebGL2
  -> canonical sound/OpenAL commands -> Web Audio device
```

Engine identity and behavior stay in Kisak. JavaScript must not own parallel
entities, profiles, objectives, dvars, saves, asset parsers or mission progress.
Traverse every required pre-world asset family in dependency order; never seek
directly to `GfxWorld` or revive the retired census/extracted-world engine.

## Execution model

The main thread owns DOM, file selection, storage coordination and Web Audio.
One engine Worker owns Wasm, its synchronous filesystem view, OffscreenCanvas
and WebGL2. The reverb AudioWorklet and temporary capability probe are separate
platform services, not engine threads. File System Access is optional; the
required capabilities are listed in [browser support](browser-support.md).

The `requestAnimationFrame` pump awaits `WebAssembly.promising` around each
Wasm frame before scheduling another. Loading yields through JSPI and
`emscripten_sleep(0)`, keeping native map/DB calls synchronous-looking while
loading UI and movies can present. Worker operations wait for suspended engine
stacks. Compilation uses native Wasm exceptions for allocation recovery and
canonical `setjmp`/`longjmp`, a 1 MiB stack and growing linear memory. There is
no Asyncify or pthread build. Threading requires profiling and a deployment
decision, not just a compiler switch.

Canonical `Com_ModifyMsec`, `SV_Frame`, client/cgame work and `SCR_UpdateScreen`
own frame order and game clocks. The platform supplies elapsed time with a
5,000 ms long-stall ceiling; script/developer timescales, pause, `fixedtime`,
random state and save restoration remain canonical. Disconnected initialized
clients still run UI/config frames before a world exists.

## Ownership inventory

CMake inventories are authoritative: `scripts/common_files.cmake`,
`scripts/sp/sp_files.cmake` and `scripts/web/CMakeLists.txt` select the sources.
A source file existing in the repository does not mean the browser compiles it.

| Classification | Systems and current owner |
| --- | --- |
| Shared | Common startup/runtime commands, dvars/config, key bindings, profiles, filesystem search paths and IWD/minizip behavior. |
| Shared | Canonical DB stream/blocks, registry pools, aliases, generated asset loaders, `XAsset` and world structures. |
| Shared | SP client/server, game/cgame, script VM (`scr_compiler2.cpp`/`scr_yacc2.cpp`), collision, XAnim/DObj pose, effects, ragdoll, physics and sound mixer. |
| Shared | Menu/feeder/objective/save owners, glyph layout and text commands, gamma policy, DPVS core, particle-axis/random-bucket and selected lighting/math helpers. |
| Modified | Extracted common startup continuation and error/shutdown owners; typed callbacks and numeric conversions repair ABI differences in shared code rather than inventing browser behavior. |
| Modified | `web_client_server_lifecycle.cpp` resumes startup after mount; `web_main.cpp` supplies frame admission/recovery; `web_canonical_gfxworld.cpp` observes final DB publication. |
| Modified | `web_renderer_frontend.cpp` and model/light adapters translate canonical inputs at the draw-command boundary. Final vertex expansion, lighting-grid sampling and native draw packing are not wholly shared backend code. |
| Platform | Worker transport, filesystem primitives/journals, browser input/clipboard, canvas/display/context recovery, WebGL resources, Web Audio device, reverb worklet and FFmpeg cinematic decoder. |
| Temporary | Diagnostic launcher/test exports and consumed telemetry; adapted renderer algorithms retire when the corresponding canonical owner can be shared. Add no product behavior to diagnostic records. |
| Native-only | Win32/D3D9, native Miles/Bink/Steam integrations, loose editor/import loaders, dedicated server and Radiant. Native source is retained where those targets need it. |
| Uncompiled | Full native renderer/backend command graph and material-table sorting, excluded loose XAnim/XModel/FX importers, voice/Steam and multiplayer transport in the offline browser target. |

Production and diagnostics compile the same runtime sources. Diagnostics add
controls and telemetry with `KISAK_WEB_DIAGNOSTICS`; they have no second loader,
world, renderer oracle or scheduler. Remove telemetry once it has no consumer.

## Database publication and resource lifetime

The loader preserves native allocation blocks, stream positions, pointer aliases,
script-string ownership, dependency order and atomic publication. Required block
and length checks precede native cursor assertions. A failed load must not leave
partial assets visible or let diagnostic state influence retry.

Rollback selects the exact failed zone, restores overrides and pool ownership,
then releases PMem in reverse allocation order. World singletons (`clipMap_t`,
`ComWorld`, `GameWorldSp`, `GfxWorld`) retain their previous shallow owner records
until commit. Rollback restores their body, identity/hash and in-use state;
destructive world-unload hooks are deferred and run against the retired owner
only when replacement commits. Request rollback also covers a first fastfile's
publication followed by a second fastfile's failure. Broader request graphs and
non-world device effects still need qualification.

DB script strings keep a real `scr_stringlist` user while any zone or zone-0
default owns them. Last-zone release cannot invalidate a live default asset.
At `Load_Texture`, the existing `GfxTexture` union carries an opaque encoded-source
handle, not a second image identity or a GL object. Canonical copies/overrides
own it; completion/unload collects only unreferenced handles. Handles are not
reused, and the 256 MiB source budget rejects before copying instead of evicting
live data. Water follows `water_t::image` in the native dependency walk.

The independent oracle compiles original Win32 `db_load.cpp` and compares its
Material/Image and XModel/PhysPreset traces with adapted Win32/Wasm loaders.
It checks blocks, aliases, publication, failed overrides and retry using synthetic
fixtures and canonical registry/PMem services. This does not validate all malformed
nested assets, native device uploads or every exception boundary.

## Storage and shutdown

Imported files use shared leases; writable home data has one exclusive Web Lock
owner across tabs. Canonical filesystem/save/profile code owns logical paths and
file contents. The Worker owns normalized lookup, OPFS handles and a strict FIFO
persistence chain. Object identity/version checks prevent stale completions from
marking replacement data durable. Acceptance into memory is not a checkpoint.

| Boundary | Limit |
| --- | --- |
| Home content | 8,191 files, 8,191 directories, 64 MiB/file, 128 MiB live bytes. |
| Paths | Normalized UTF-8 must fit `MAX_OSPATH`: 259 bytes plus terminator. |
| Persistence queue | 16,384 queued plus reserved operations; 256 MiB queued plus reserved snapshots. |
| Backup metadata | 4 MiB manifest; ordinary home limits also apply. |

Open writers reserve their next close snapshot, with one writer per file.
Over-budget mutations fail before changing bytes or reservations. Conflicting
normalized/legacy names stop mount with raw export available. Storage failure
latches until explicit checkpoint retry; automatic timers stop after failure.
These budgets exclude backing-buffer slack and are not process-memory totals.

Canonical temporary-save/`FS_Rename` uses one recoverable platform journal:
finish prior writes; close `home-rename.json`; write/truncate/close the staged
destination; remove the source; remove the journal. Never delete an existing
destination to make room. A fresh tenure replays the journal before home loading
and budget checks; malformed intent remains exportable. Writable-stream close
provides staged publication, not a cross-file transaction or power-loss guarantee.

The `.kisak-home` envelope contains `KISAKHOME1\n`, a little-endian JSON length,
a versioned path/size/SHA-256 manifest and opaque file bytes. It preserves files
and necessary parents, not empty directories or imported installations. Checksums
identify damage, not a trusted publisher. Restore refuses existing destinations,
preflights combined budgets, stages/hashes one bounded file at a time, then commits
`home-restore.json`. Cancellation is allowed before intent commit; afterward replay
completes verified destinations without overwriting user files. Concurrent rename
and restore journals are invalid. Raw export bypasses mount/replay and includes
recovery sources, so damaged homes remain accessible.

Restore staging and immutable backup snapshots can each require another 128 MiB
beyond live-home storage; quota failure retains committed recovery sources. A
dialog keeps its lease until committed work settles, even after closing. Stale
callbacks cannot update a later dialog. [Save workflows](browser-support.md#local-package-contract)
cover canonical interpretation and compatibility separately.

Mount/checkpoint/unmount/shutdown watchdogs allow 15 seconds without progress in
diagnostics and 30 in production, with a separate five-minute absolute cap. Only
advancing counters/phases for the current request and Worker generation renew a
stall deadline; progress cannot extend the absolute cap.

Shutdown stops new work, persists dirty descriptors, drains admitted codec jobs
and queued mutations, closes file handles/connections/listeners, releases leases
and terminates the Worker last. Retryable save failures retain the stopped Worker
and offer retry. Unknown ownership requires termination before lease release and
reports that saving could not be confirmed. Canonical Quit writes configuration;
it does not create a campaign checkpoint. Worker termination replaces process
teardown; direct native `Com_Shutdown` remains unsupported.

## Menus, settings, errors and input

Kisak owns shipped main/options/profile/load/pause menus through `CL_InitUI`,
`UI_Init`, `UI_SetActiveMenu`, `UI_Refresh` and renderer 2D commands. Objectives
use server configstrings, `CG_ParseObjectiveChange`, `objectiveInfo_t`, localization
and native HUD timing; notifications use canonical console channels. Synthetic
objective injection demonstrates that boundary, not natural mission completion.

Canonical dvars own domains, flags, latched values and archived configuration.
Startup registers key commands before filesystem/profile config execution and
replays the selected profile after startup RawFiles publish. Existing active
profiles win; a default browser profile is created only when none is valid.
Shared profile feeders/select/delete code owns identity and config isolation.
`ui_sp_unlock` stays an intentional stock dangling reference. Newly created `seta`
variables alone do not mark the global archive dirty bit; no host workaround exists.

Recoverable initialized-frame errors use canonical `Com_ErrorCleanup`, renderer/UI
reinitialization and native error menus. Errors before full startup are fatal;
classification and repeated-error escalation stay in shared common code. Browser
controls expose installation/recovery/fullscreen only. They do not dismiss native
errors or change game state. Shift+Escape can open the accessible recovery dialog.

Physical keys and committed text use separate canonical queues. The platform
maps CSS coordinates to backing pixels, clamps absolute positions inside the
canvas, coalesces relative motion and releases/cancels input on blur/hide/disposal.
Text uses NFC-normalized Windows-1252 bytes; unrepresentable code points are dropped.
An editable sink accepts final IME composition once. Trusted paste snapshots at
most 4,095 first-line bytes, then canonical `Field_Paste` owns insertion/freeing.
Native fields are not UTF-8 fields; real IME candidate UI and other code pages
remain unqualified. Shared `r_text.cpp`/`r_text_cmds.cpp` own glyph layout, color,
shadow/glow, cursor and reveal/decay; the web adapter supplies clocks and quads.

`web_display.cpp` supplies canonical resolution enums and `vidConfig_t`.
Automatic follows canvas density; fixed modes preserve size/aspect. Refresh is
browser-controlled. Shared `vid_restart` retains the existing DB executor and
uses canonical save/disconnect/load behavior in-game. Gamma applies at final
presentation after 2D; saved-screen feedback and thumbnails remain pre-display.

## Rendering, audio and cinematics

Portable commands retain canonical material/image identity, surface spans, current
placement and validated numeric data. Camera DPVS and each sun/spot caster selection
remain independent; off-camera objects may still cast shadows. Backend resources
and recovery copies never become world/entity/pose state. Publication is atomic,
failed ownership transfers return storage, and unload releases map resources.

WebGL2 implements encountered authored shader families and pass/state ordering.
It does not translate arbitrary D3D shaders. Most 2D image pools retain encoded
LoadDef/IWI sources and decode transiently for upload/recovery; cubes, lighting,
water and other supplemental resources have separate policies. The 800 MiB limit
is per-pool decoded admission, not aggregate retained memory or measured VRAM.
See [renderer ownership and limits](renderer-retained-resources.md) for the boundary.

Canonical SND/OpenAL owns channels, aliases and room/EQ parameters; the device owns
AudioContext policy and PCM scheduling. Generation-tagged feedback follows device
time, with one snapshot in flight, rather than a Worker wall clock. Deliberate canvas
interaction unlocks sound. [Reverb](browser-reverb.md) owns DSP/device details.
[The cinematic codec](cinematic-codec.md) keeps `R_Cinematic_*` identity and game
actions canonical while FFmpeg supplies decode, WebGL supplies planes and audio
feedback supplies movie timing. Missing/failed movies report an explicit omission.

## Protocol, release and remaining convergence

Versioned named messages validate payloads/ranges and reject stale generations.
Input is checked at both ends; `input-event` is one-way and ordinary RPCs carry IDs.
`product_protocol.mjs` owns the operation/export allowlists. Production accepts no
diagnostic export. Shared transport handles IDs, abort/timeout cleanup and leases.

Release qualification requires a clean matching source/build receipt, pinned
lockfile/toolchain/dependency sources, site hashes and all required CI tiers.
Corresponding source and public dependency archives accompany the flat served site;
legacy native SDKs/binaries and proprietary assets are excluded. Exact export/file
and size gates remain required. Hashes detect mismatches; publisher authentication
and campaign acceptance remain separate. The product is not a qualified alpha.

Remaining work is canonical gameplay/material qualification, measured scene/streaming
costs and authored audio/video fidelity. Full native draw packing/material sorting,
equal-key object-ID ordering, unknown shader/multipass families and text-effect outer
material rejection remain incomplete or unqualified. Gamepad, non-Western text and
broader browser behavior need explicit work. Raw UDP is unavailable: multiplayer
requires a gateway/transport design. Expand shared owners when evidence requires it;
do not grow the bootstrap into an asset viewer or simulated campaign engine.
