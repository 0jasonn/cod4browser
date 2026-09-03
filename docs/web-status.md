# Web product status

Updated 2026-09-03. This is the single current status page; the
[roadmap](web-roadmap.md) owns priorities and the
[convergence inventory](web-port-convergence.md) owns system classification.

## Verification and reference baseline

AC130 now renders its real scripted gunship scene after correcting empty-grid
lighting and the browser dynamic geometry capacity. The frame wrapper also
reports latched canonical errors instead of leaving a frozen game marked
running. CargoShip → AC130 passes a 60-second stationary diagnostic run;
production Chrome also renders the mission. Captures were visually inspected,
but Steam/native thermal fidelity and mission completion remain unverified.
See [AC130 evidence](evidence/ac130-rendering-2026-09-03.md).
Latest gates: five focused C++ tests on native and Wasm, static checks, 83 Node
tests, 12 smoke, 51 remainder (six optional retail skips), and 44 production
browser tests pass. The existing production size gate still fails unchanged.

Video Mode now selects an actual render resolution through canonical
`vid_restart`; Screen Refresh Rate reports `Browser controlled`. Resize updates
the native UI and cgame viewport together. The redundant database-thread spawn
and missing backend reinitialization are repaired. Production Chrome 152 passes
the shipped Apply confirmation, durable mode restoration and the native
save/restart/load sequence in Killhouse. See
[display evidence](evidence/display-options-2026-09-03.md).
Display milestone gates: static checks, 83 protocol tests, native SP build, 12 smoke,
50 remainder (six optional retail skips), 44 production browser tests and
separate owned Chrome 152 display/gamma checks pass.
The existing production size gate still fails; its budgets are unchanged.

The shipped Brightness slider affects menu, HUD and world pixels through the
existing final display pass. Native/Wasm ramp traces match. Steam display
equivalence remains unverified. See [gamma evidence](evidence/display-gamma-2026-09-03.md).

The cinematic platform implementation now decodes imported Bink movies with
source-built FFmpeg and presents them through the existing renderer and audio
boundaries. The owned Killhouse intro passes production Chrome playback and
natural completion, plus diagnostic pause/resume and native skipping. This establishes movie
playback, not authored campaign completion or Steam audiovisual parity. The
unchanged production size gate fails with the added decoder. See
[cinematic scope](cinematic-codec.md) and
[current execution evidence](evidence/cinematics-2026-09-02.md).

The verification repair passes static checks, 81 protocol tests, 12 diagnostic
smoke tests, 39 remainder tests (4 optional retail skips), and production
boot/canonical mount-error checks. The synthetic picker now reaches a completed
mount, and canonical errors cross the asynchronous mount boundary correctly.
See [execution and limits](evidence/verification-repair-2026-09-02.md).

Saved-screen flashbang/shellshock commands now capture and blend in the existing
WebGL2 UI pass, retaining canonical timing and intensities. Synthetic pixel
checks and an owned-retail Killhouse material/composite check pass; original
game visual/timing comparison remains unverified. See
[saved-screen evidence](evidence/saved-screen-2026-09-02.md).

FX point-lighting queries now use the canonical world's light grid and sun
color instead of constant white. The existing portable lighting helper passes
focused host-native and Wasm checks; the final browser gates remain green.
See [point-lighting evidence and limits](evidence/average-lighting-2026-09-02.md).

Transient FX omni/spot submissions now retain canonical `GfxLight` construction
and importance selection and draw the loaded additive material passes before
emissive geometry. Pixel checks cover attenuation, cone direction, normal maps,
vertex fog and destination-alpha coverage. A separate owned Killhouse check
visibly lights world/model geometry, clears back to baseline and survives
context recovery. Current routine gates: 12 smoke, 40 remainder, 5 optional
retail skips. Transient spot shadows, native receiver/scissor selection and
Steam visual comparison remain open. See [light evidence](evidence/transient-lights-2026-09-02.md).

The local Steam build is [pinned by hashes and configured settings](evidence/steam-reference-2026-09-02.json).
The native SP reference now builds with pinned MSVC and the existing OpenAL
backend, loads the owned startup fastfiles and enters main-menu code. Native
visual/gameplay verification remains open. The separate browser retail rerun
now passes pointer lock, pause/resume, profiles and Airplane save/Continue after
page reload. Activating the persistent test tab before clicking fixed the
pointer-lock failure without changing input behavior or assertions. See
[native bootstrap evidence](evidence/native-reference-2026-09-02.md).
No original/native/browser campaign comparison or mission completion has been
established by this work.

Canonical Quit now stops at the browser frame boundary, writes the current
profile and flushes it before returning to a launcher with a Start game action.
Retryable save failures keep the stopped runtime available. Production tests
verify a last-command volume change is durable, locks are released and startup
can remount the installation. Static, 81 protocol, 43 production browser,
12 smoke and 40 remainder tests pass (5 optional retail skips). See
[Quit evidence and limits](evidence/browser-quit-2026-09-02.md). A separate owned
Chrome 152 diagnostic run also passes Quit from Airplane and restart into the
main menu after its save/Continue checks.

The previously discarded text styles, subtitle glow and timing parameters now
reach shared native text code. Console-ring text is rendered, color escapes
change glyph colors, and the native shadow, monospace, cursor and reveal/decay
paths generate WebGL2 UI quads. Native Win32 and Wasm checks plus a browser
command/pixel check pass. Native SP still links; Release builds remain inside
existing size budgets. Routine verification is 81 protocol, 43 production
browser, 12 smoke and 41 remainder passes (5 optional retail skips). Steam
visual equivalence remains unverified. See [text evidence](evidence/text-presentation-2026-09-02.md).

Save menus now retain header descriptions, maps and timestamps, bound loading
to the native display-index capacity, and select paired profile image paths
without trusting embedded filenames. Native/Wasm regression checks also pass
for the shared screenshot resampler and its corrected end-of-image read.
Save commits now capture the rendered scene into a bounded JPEG, wait for its
durable write, and decode it into the selected canonical UI image. An owned
Airplane image survives page reload and appears in the shipped Load Game menu.
Visual inspection also exposed the wrong Date-column field and an unresolved
description marker; both now use the shared save metadata. Initial saves now
wait for a rendered frame from the matching map; native raw-image loading uses
Kisak's reserved image and passes a native decoder/device-reset test. Complete
native menu and Steam presentation comparison remain open. See
[startup/native evidence](evidence/save-startup-native-2026-09-02.md),
[thumbnail evidence](evidence/save-thumbnails-2026-09-02.md)
and the earlier [metadata/resampler checks](evidence/save-presentation-2026-09-02.md).
Current thumbnail milestone gates pass: native/Wasm save tests, static checks,
83 protocol, 43 production browser, 12 smoke and 44 remainder tests (5 optional
retail skips), plus the separate owned Chrome 152 save/menu/Continue/Quit run.

## Real-time gameplay

The import profile and database file adapter now select localized paths instead
of requiring English. French portable and German directory-picker fixtures
reach canonical database opens and survive reload; changed language markers are
rejected. The owned English Killhouse regression still passes. Non-English
retail inventories, text/fonts, dialogue and gameplay remain unqualified. See
[localized import evidence](evidence/localized-imports-2026-09-02.md).

The Web Audio device now applies both canonical three-band EQ stages and
removes them on bypass. Offline PCM checks cover all five filter types,
stereo/queue continuity and live updates; an owned Killhouse probe attaches a
verified -6 dB bell to a playing sound through canonical console settings.
SND still owns every EQ parameter. The inherited disabled default, Steam DSP
comparison and authored update transients remain open. See
[EQ device evidence](evidence/browser-eq-2026-09-02.md).

The existing OpenAL reverb DSP now passes native/Wasm differential checks for
all 26 room presets at five sample rates. The shared preset header also builds
in native SP. Canonical room and wet changes now reach an AudioWorklet through
the existing Web Audio driver, with browser PCM tests for routing, stereo,
position, EQ, live updates and reset. Authored campaign transitions, callback
cost and Steam audio comparison remain unqualified. A production Chrome 152
Killhouse run verifies the real map's room/wet settings, console-driven fades,
nonzero wet output and restoration. Current regression checks pass (12 smoke,
48 remainder with six optional skips, 43 production, 83 protocol), while the
unchanged production size gate still fails after cinematic/reverb additions. See
[reverb boundary and evidence](browser-reverb.md).

Unintended slow motion had two causes: a `long double`/`double` alias in the
canonical slow-command parser corrupted normal speed in Wasm, and the browser
plus native frame-time caps discarded elapsed time beyond 100 ms. The shared
parser now converts numerically; the browser selects the native five-second
long-stall ceiling. Deliberate script slow motion and opt-in profiling
`fixedtime` remain supported. See [the parser proof](evidence/canonical-timescale-2026-09-02.md)
and [the browser timing evidence](evidence/browser-frame-time-2026-09-02.md).

## Current campaign boundary

Shipped New Game now has observed natural Killhouse rifle, timed-shooting and
sidearm/melee training, three authored checkpoints and Resume Game in a fresh
browser process. The next observed objective is to locate Captain Price; the
course, mission completion, CargoShip and following transition remain unverified.
This gameplay exposed a Save and Quit crash after saving: the frame lacked a
live canonical disconnect jump target. The browser now unwinds that frame,
shares native client/server shutdown and UI reload, and resumes the checkpoint
without losing its Worker. Two production cycles and the separate owned Chrome
retail regression pass. Current routine gates pass: 43 production, 12 smoke and
44 remainder tests, with five optional retail skips. See
[training and disconnect evidence](evidence/campaign-training-disconnect-2026-09-02.md).

Canonical SP menu startup now runs before a map is active. A legal-retail
Release-diagnostics run loaded 69 shipped menus, opened `main`, Options,
Profiles, and Load Game through Kisak's command/UI owners, then loaded
Killhouse and verified the shipped pause and in-game menus. Escape paused and
resumed through the existing key queue. The reached Wasm defect was an invalid
SP `int()`-to-`void()` command callback cast; SP now matches MP's typed
`openmenu` callback. See [the focused evidence](evidence/canonical-menu-lifecycle-2026-09-01.md).

The top-left objective notification is also canonical end to end. The normal
server spawn loads `ui/ingame.txt`; a synthetic diagnostic configstring then
proved current/completed parsing, localized objective text reaching the
renderer command boundary, and the existing timed fade/hide behavior. See
[the objective evidence](evidence/canonical-objective-notification-2026-09-01.md).

Canonical dvar/config persistence now also runs in native order. The mounted
runtime restores the full shared dvar and key-command tables, executes the
active profile's `config.cfg` after retail RawFiles are available, and calls
`Com_WriteConfiguration` from the frame loop. A fresh legal-retail run proved
all seven dvar value families, representative ROM/cheat/archive/latch flags,
`seta`/`toggle`/`reset`/`bind`, config creation, and archive/binding survival
across a page and Worker restart. See
[the config evidence](evidence/canonical-dvar-config-2026-09-01.md).

Canonical player profiles now create, enumerate, select, isolate archived
configuration, survive a page/Worker restart, and delete through the shipped
UI and shared Kisak profile owner. The only new storage operation is recursive
directory removal at the Worker filesystem boundary; profile identity and
configuration remain native engine state. The reached Wasm dvar-domain callback
ABI mismatch was corrected at its shared typed call site. See
[the profile evidence](evidence/canonical-profile-lifecycle-2026-09-01.md).

An earlier canonical save/load menu persistence run made
an Airplane `devsave`, discovered and selected it through the shipped feeder,
loaded it through the real menu script, and reopened it after a full page and
Worker restart through Continue. Health, weapon, ammo, objective state, and
position matched; other profiles saw an empty list; exact canonical deletion
removed only the test-owned save. Its screenshot path used the shipped fallback;
the later thumbnail work above replaces that omission. Reached fixes restored
the canonical version dvars and a 1 MiB web
platform stack for native-scale load depth. See
[the save/load evidence](evidence/canonical-save-load-2026-09-01.md).

The playable offline slice already exists across six legally validated maps.
Those runs demonstrate canonical movement, aiming, firing, weapons, audio,
transitions, and Airplane save/load continuity. Building another player
controller or simulated movement path is out of scope. The obsolete mission
route author/replay system and its waypoint/progression simulation have been
deleted; `village_assault` progression automation remains retired.

The dynamic DObj convergence path is complete through `838e047c`. A headless
Release-diagnostics observe-only run based on that commit now establishes
`scoutsniper` as **RENDERS**: all canonical lifecycle stages completed, the
first real world frame arrived, and 3,601 stationary frames ran over 60.013
seconds with no page, WebGL, or lifecycle error. No input was injected and no
headed/manual visual inspection occurred, so this is not a `FUNCTIONAL`,
`PLAYABLE`, or visual-correctness claim. See
[the stationary evidence](evidence/scoutsniper-stationary-838e047c.md).

The requested canonical SP UI and persistence convergence task is complete.
The subsequent `ac130` renderer work is recorded above; it adds stationary
rendering evidence without extending the persistence or mission-flow claim.

## Renderer evidence index

The [convergence inventory](web-port-convergence.md) records current ownership
and implementation. These commit-bound measurements remain historical; they
do not establish current campaign performance or new compatibility claims.

| Area | Evidence |
| --- | --- |
| Dynamic geometry handoff | [the historical evidence and limits](evidence/dynamic-geometry-ownership-af601efe.md) |
| Renderer optimization audit completed | [evidence and limits](evidence/dynamic-primary-light-linkage-4ed38a84.md) |
| Dynamic opaque draw-order milestone | [evidence and limits](evidence/dynamic-opaque-sort-c8c4f335.md) |
| Dynamic spot-shadow caster milestone | [evidence and limits](evidence/dynamic-spot-shadows-9a253c6a.md) |
| Dynamic sun-cascade visibility milestone | [evidence and limits](evidence/dynamic-sun-partitions-6ece6ee9.md) |
| BSP sun-cascade visibility milestone | [evidence and limits](evidence/bsp-sun-partitions-a23850aa.md) |
| Static spot-shadow membership milestone | [evidence and limits](evidence/static-spot-membership-26b3dc98.md) |
| Static-instance upload milestone | [evidence and limits](evidence/static-instance-uploads-ac8b00ca.md) |
| Static sun-shadow partition milestone | [evidence and limits](evidence/static-sun-partitions-cc4af645.md) |
| DObj conversion and dynamic shadow milestone | [evidence and limits](evidence/dobj-conversion-30e34cff.md) |
| Retained-renderer milestone | [the evidence](evidence/retained-renderer-49af3948.md) |
| Seeded brush shader-hash optimization | [the evidence](evidence/seeded-brush-hashes-06ad8004.md) |
| Paused profiling exposes geometry variation | [the evidence](evidence/paused-copy-qualification-cd85e18e.md) |
| Camera/time qualification | [the evidence](evidence/controlled-renderer-552a468d.md) |
| Brush cost investigation | [the evidence](evidence/brush-costs-f15c3dc9.md) |
| Direct DObj vertex emission | [the evidence](evidence/dobj-emission-fb596702.md) |
| Renderer CPU-efficiency milestone | [the completed milestone](evidence/renderer-cpu-milestone-e4db91df.md) |
| Conditional falloff uniforms | [the evidence](evidence/falloff-uniforms-12ac17e5.md) |
| Dynamic draw texture setup | [the texture-state evidence](evidence/dynamic-textures-74fe11aa.md) |
| Dynamic geometry staging | [the staging evidence](evidence/dynamic-staging-9403a899.md) |
| Particle-cloud append optimization | [the comparison](evidence/cloud-append-ae37e80c.md) |
| Scene-construction measurement | [the scene profile](evidence/scene-stages-2ce03241.md) |
| DObj hashing optimization | [the comparison](evidence/dobj-hash-d8661476.md) |
| Focused DObj measurement (`946dc918`) | [the stage profile](evidence/dobj-stages-946dc918.md) |
| World-camera visibility | [the world visibility evidence](evidence/world-camera-visibility-2026-08-31.md) |
| Canonical static-camera visibility (`317fc12f`) | [the visibility evidence](evidence/static-camera-visibility-2026-08-31.md) |
| Renderer handoff (`a44119df`, from `bf5ec1e2`) | [the renderer record](evidence/renderer-efficiency-2026-08-31.md) |
| Cleanup baseline at `bf5ec1e2` | [cleanup evidence](evidence/cleanup-renderer-2026-08-31.md) |
| Historical automated evidence | [the comparison](evidence/retail-profile-93451ec5.md), [the regression record](evidence/retail-six-map-regression-39de3d6d.json) |

The [campaign matrix](campaign-compatibility.md) owns map classifications and
save/load claim scope. Earlier milestone narratives are in [Git history](history/README.md).

## Product boundaries

WebGL2 and the Worker/DOM/storage/audio adapters are platform-owned. Kisak owns
assets, game state, filesystem semantics, and the renderer frontend. Production
and diagnostics remain separate artifacts. Imported retail files remain local;
proprietary binaries and data are never distributed. The source-built FFmpeg
codec implements movie playback behind Kisak's cinematic API; missing imported
movies remain explicit omissions. WebGL2, single-threaded Wasm and the offline
single-player scope remain unchanged.
