# Campaign compatibility matrix

This matrix preserves historical automated retail evidence and the stationary
`scoutsniper` and `ac130` probes. [Current status](web-status.md) distinguishes
the scope of each task. No mission-flow result is a prerequisite for renderer
or cleanup work.

## Retail fidelity acceptance — 2026-09-02

The [2026-09-05 production windows](evidence/browser-frame-time-2026-09-02.md#current-production-measurements--2026-09-05)
add current Killhouse/CargoShip/AC130 rendering, timing, audio and context-recovery
observations. The encountered two-pass multiply/fog material also has native
shader and synthetic boundary evidence. Neither changes mission-completion or
Steam/native visual-fidelity classifications; no automated player route or
objective success was supplied.

The [2026-09-03 AC130 check](evidence/ac130-rendering-2026-09-03.md) establishes
**RENDERS** through canonical lifecycle completion and 60 seconds of stationary
world frames after CargoShip. Production rendering and separate screenshot
inspection also pass. Gunship interaction, authored mission completion and
Steam/native thermal fidelity remain unverified.

The [display-options check](evidence/display-options-2026-09-03.md) verifies
shipped Apply, durable resolution and native save/restart/load of Killhouse
with the same observed view origin. This platform check does not advance any
mission-completion classification.

The [2026-09-03 gamma check](evidence/display-gamma-2026-09-03.md) verifies
the shipped Brightness slider and loaded Killhouse display response. It adds
presentation evidence and does not change campaign classifications.

The [localized import change](evidence/localized-imports-2026-09-02.md) adds
French/German synthetic path and persistence coverage. Retail campaign evidence
below remains English-only; localized mission behavior is unverified.

The [cinematic implementation](evidence/cinematics-2026-09-02.md) decodes and
plays the owned Killhouse intro through the existing Kisak cinematic API. The
production `map killhouse` path starts the movie before map initialization,
keeps gameplay and HUD state out of the movie, then loads Killhouse and starts
the authored fade. Standalone pause, suspension, recovery, completion and skip
checks also pass. This playback evidence does not advance any mission
classification or establish the authored Killhouse-to-CargoShip transition.

The [Steam inventory](evidence/steam-reference-2026-09-02.json) records local
build 2737681, 101 executable/data hashes, configured graphics/audio/input
settings, and reference hardware. These are inventory/configuration facts;
difficulty and visible outcomes still require an original-game observation.
The native SP reference now builds and enters main-menu code with owned data
(see [native evidence](evidence/native-reference-2026-09-02.md)). Native visual
and gameplay comparison remains unverified. The browser retail rerun passes
Killhouse pointer lock/pause/resume, profile persistence and Airplane save/load
and Continue after page reload. The test required real tab activation before
requesting pointer lock. Its injected objective and save probes do not establish
authored mission completion or Continue in a new browser process, and do not
promote any campaign classification.
The subsequent [Quit check](evidence/browser-quit-2026-09-02.md) also returns from
the active Airplane runtime to the launcher and starts a fresh Worker/main menu.
The [shared text renderer](evidence/text-presentation-2026-09-02.md) also passes
that menu/input/profile/save/Continue sequence; it adds presentation evidence,
not authored campaign-completion evidence.
The [EQ device check](evidence/browser-eq-2026-09-02.md) applies injected
canonical settings to a playing owned Killhouse sound and verifies bypass.
It establishes device routing/filter response, not authored audio fidelity or
additional campaign progress.
The [save presentation prerequisite fixes](evidence/save-presentation-2026-09-02.md)
restore bounded header metadata and typed thumbnail selection in shared UI.
The owned Airplane run verifies that map/date/time metadata survives reload
alongside the existing save/Continue/Quit checks. These checks do not qualify
screenshot capture or change campaign classifications. The subsequent
[thumbnail implementation](evidence/save-thumbnails-2026-09-02.md) captures the
owned Airplane scene, persists its JPEG through reload and displays it in the
shipped save menu. Its injected save and visual check add presentation evidence;
authored checkpoint timing and original-game comparison remain unverified.
The [start-level capture follow-up](evidence/save-startup-native-2026-09-02.md)
also requires the automatic Airplane start-level save to produce its JPEG
before a diagnostic save is issued. This covers the initial capture boundary;
it does not qualify later authored checkpoints or mission completion.

Subsequent [ordinary New Game gameplay](evidence/campaign-training-disconnect-2026-09-02.md)
completed Killhouse rifle, timed-shooting and sidearm/melee training and authored
checkpoints 1–3. A fresh Chrome process resumed checkpoint 3 with those objectives
complete. The reached Save and Quit disconnect crash is fixed; two production
Save and Quit cycles returned to the canonical menu and Resume Game restored
the checkpoint between them. The next objective is to locate Captain Price.
This adds early authored progress and fresh-browser Continue evidence, without
promoting Killhouse to mission-complete or qualifying death/restart.

The remaining campaign acceptance is shipped New Game through Killhouse, CargoShip
and the authored following mission transition. Require natural objectives,
scripted sequences, checkpoints, death/restart, fresh-browser Continue and
mission completion, matched against original Steam and native Kisak where
available. Ordinary gameplay is required; synthetic objectives and route/replay
substitutes cannot satisfy this gate. No classification below changes from
verification repairs or shader-boundary tests.

The [saved-screen check](evidence/saved-screen-2026-09-02.md) loaded Killhouse
and exercised its retail material against synthetic framebuffer colors. It
adds renderer-boundary evidence only; no campaign classification changes.

The [transient-light check](evidence/transient-lights-2026-09-02.md) uses
canonical pause and synthetic `R_Add*LightToScene` inputs in owned Killhouse.
It verifies visible world/model illumination, clearing and context recovery.
It does not validate authored FX timing, active gameplay or mission flow.

## Stationary campaign update — 2026-09-01

The obsolete mission-route author/replay system and its synthetic progression
tests are deleted. Route generation and simulated mission progression are
retired, and `village_assault` automation remains out of scope.

A legally owned, headless Release-diagnostics run based on `838e047c` completed
canonical DB, ClipMap/world, server/game, client/cgame, first-world-frame, and a
60.013-second stationary window for `scoutsniper`. It injected no gameplay
input and performed no visual inspection, so the map advances only to
`RENDERS`. See
[the stationary evidence](evidence/scoutsniper-stationary-838e047c.md).

## Historical corrected evidence — 2026-08-28

This section supersedes the performance and mission-progression interpretation
below. The six-map classifications are unchanged: Killhouse and Airplane are
`PLAYABLE`; CargoShip, Blackout, Hunted, and Bog A are `FUNCTIONAL`.

| Map | Clean FPS | p95 / p99 | Game/wall | Compatibility | Mission-flow flag |
| --- | ---: | ---: | ---: | --- | --- |
| `airplane` | 59.51 | 19.12 / 20.99 ms | 0.999783 | **PLAYABLE** | not validated |
| `killhouse` | 40.45 | 27.25 / 28.86 ms | 0.998363 | **PLAYABLE** | not validated |
| `blackout` | 28.79 | 37.33 / 39.97 ms | 0.998603 | **FUNCTIONAL** | not validated |
| `bog_a` | 23.42 | 48.46 / 51.52 ms | 0.999583 | **FUNCTIONAL** | not validated |
| `hunted` | 20.93 | 50.79 / 56.21 ms | 0.994109 | **FUNCTIONAL** | not validated |
| `cargoship` | 14.22 | 77.15 / 85.08 ms | 0.998161 | **FUNCTIONAL** | not validated |

These are independent profiling-disabled 60-second foreground windows from
the clean six-map rerun at `93451ec5` in headed Chrome 152.0.7977.64 on the
Windows 11 / Ryzen 7 7800X3D / RTX 3070 Ti reference host. The paired
diagnostic capture collected exactly 300 completed gameplay/render samples per
map and is not used for the compatibility label. Full aggregates are in
[retail-profile-93451ec5.json](evidence/retail-profile-93451ec5.json).

Airplane proves substantial canonical save/reload continuity, but the record
at `da1e592c` shows no objective hash, active/done objective count, or mission
flag change. It therefore does not earn `MISSION_FLOW_VALIDATED`. A stricter
Village Assault run at clean `e7be6898` reached live AI, scripts, an active
objective, changing actor state, and canonical input, but no monitored
progression marker changed in 120 seconds. The checkpoint/save/death/reload
flow after progression was not reached. This is unproven trigger traversal,
not a demonstrated canonical defect. See
[the sanitized failed gate](evidence/retail-mission-village-assault-e7be6898.json).

`village_assault` remains `UNTESTED`; file presence does not establish
compatibility. AC130 now has the separate `RENDERS` evidence above. The six
gameplay-validated maps remain permanent regression baselines.

This ledger records current execution evidence, not filenames. Asset discovery
alone is `UNTESTED`; compatibility advances through canonical database, world,
server, game, cgame, renderer, input, audio, persistence, transition, and
recovery evidence.

## Current reference

The initial `PLAYABLE` threshold on the recorded Windows 11 reference host is:

- average frame rate at least 30 FPS;
- p95 frame time at most 50 ms; and
- game-time/wall-time ratio at least 0.90.

This is a conservative reference for the recorded hardware and browser, not a
universal hardware requirement. Performance is classified only from a headed
window that remained visible and focused with zero background transitions.
Background-invalid timing cannot establish or remove `PLAYABLE` status.

The latest clean optimization rerun is `9e75a9dd`, in headed Chrome
151.0.7922.174 on the same Windows 11/RTX 3070 Ti reference host. All six maps
again passed canonical loading, 60-second foreground stability, gameplay,
audio, configuration checkpoint, transitions, context recovery, and clean
shutdown/reload. Current performance is summarized below; the machine-readable
record is [retail-profile-9e75a9dd.json](evidence/retail-profile-9e75a9dd.json).

| Map | Average FPS | p95 frame | Backend CPU average | Result |
| --- | ---: | ---: | ---: | --- |
| `killhouse` | 43.575 | 24.960 ms | 14.106 ms | **PLAYABLE** |
| `cargoship` | 11.941 | 92.575 ms | 45.613 ms | **FUNCTIONAL** |
| `blackout` | 24.009 | 45.775 ms | 31.897 ms | **FUNCTIONAL** |
| `airplane` | 59.962 | 18.505 ms | 2.309 ms | **PLAYABLE** |
| `hunted` | 21.629 | 49.105 ms | 29.462 ms | **FUNCTIONAL** |
| `bog_a` | 20.830 | 61.505 ms | 21.608 ms | **FUNCTIONAL** |

These results refresh performance and regression status; they do not promote
configuration checkpoints to complete gameplay save/load evidence. Separate
clean evidence at `da1e592c` proves one complete Airplane mission loop through
live AI/scripts/objective state, combat, natural and named saves, death/restart,
browser shutdown, fresh-runtime load, restored state, and continued play:
[retail-mission-da1e592c.json](evidence/retail-mission-da1e592c.json).

The final clean baseline at `f5229806` ran
`killhouse` -> `cargoship` -> `blackout` -> `killhouse` in headed branded
Chrome and Edge. Every 60-second window was visible, focused, free of
background transitions, and valid. Canonical input, audio, checkpoint,
transition retirement, and forced context recovery passed. The encoded-source
recovery strategy from `c66d41e1` was active. See the
[sanitized two-browser evidence](evidence/retail-foreground-f5229806.json),
which includes exact performance, memory, context-recovery, and WebGL identity
fields without retail paths or proprietary content.

The next campaign batch ran at clean `247980a6` in headed Chrome
151.0.7922.174 on Windows 11 Pro with an AMD Ryzen 7 7800X3D and 32 GiB of
system memory. See the
[sanitized machine-readable evidence](evidence/retail-campaign-247980a6.json).
It contains no retail paths or proprietary content.

Current totals are 8 runtime-probed maps: 2 `PLAYABLE`, 4 `FUNCTIONAL`, 2
`RENDERS`, 14 `UNTESTED`, and 0 `BLOCKED` or `REGRESSION`. These retain the
historical evidence levels below, not current-build performance claims.
Authored mission completion is unverified for every row. The user owns further
manual gameplay, death/restart/Continue and original/native fidelity acceptance;
implementation work is independent of that acceptance.

| Map | Canonical runtime | First frame | Valid foreground performance | Canonical gameplay | Transition / context recovery | Recovery / Wasm capacity | Result | Evidence |
| --- | --- | ---: | --- | --- | --- | --- | --- | --- |
| `killhouse` | DB, ClipMap/world, server, game, cgame, and actual world frames pass | Chrome 5,710.910 ms; Edge 5,693.485 ms | Chrome: 33.708 FPS, 32.210 ms p95, 0.999075 ratio; Edge: 35.027 FPS, 31.180 ms p95, 0.998705 ratio | Full input, canonical fire clip 30 -> 28, ADS, weapon selection, audio, and checkpoint pass | Fresh load and return transition pass; old-map recovery retires; returned-map forced recovery passes in 1,960.945/1,893.305 ms | Chrome 518,433,483 B recovery / 989,921,280 B capacity; Edge 518,844,075 B / 989,921,280 B | **PLAYABLE** | Clean legally owned Chrome/Edge baseline at `f5229806`. |
| `cargoship` | DB, ClipMap/world, server, game, cgame, and actual world frames pass | Chrome 6,834.410 ms; Edge 7,303.475 ms | Chrome: 13.151 FPS, 82.845 ms p95, 0.997954 ratio; Edge: 13.872 FPS, 79.775 ms p95, 0.997752 ratio | Full input, canonical fire clip 12 -> 11, ADS, weapon selection, audio, and checkpoint pass | `killhouse` -> `cargoship` and onward transition pass; forced recovery passes in 1,357.375/1,349.035 ms | Chrome 322,859,990 B recovery / 989,921,280 B capacity; Edge 321,075,594 B / 989,921,280 B | **FUNCTIONAL** | Clean `f5229806`; average and p95 miss the reference `PLAYABLE` threshold. |
| `blackout` | DB, ClipMap/world, server, game, cgame, and actual world frames pass | Chrome 6,249.210 ms; Edge 6,502.420 ms | Chrome: 26.823 FPS, 39.690 ms p95, 0.998008 ratio; Edge: 26.976 FPS, 39.470 ms p95, 0.997445 ratio | Full input, canonical fire clip 10 -> 9, ADS, weapon selection, audio, and checkpoint pass | `cargoship` -> `blackout` -> `killhouse` passes; forced recovery passes in 1,790.265/1,849.430 ms | Chrome 450,602,931 B recovery / 989,921,280 B capacity; Edge 450,954,755 B / 989,921,280 B | **FUNCTIONAL** | Clean `f5229806`; average FPS misses the reference `PLAYABLE` threshold. |
| `airplane` | All canonical runtime boundaries pass | 7,352.500 ms | 3,597 frames; 59.95 FPS; 18.65 ms p95; 0.99999 ratio | Fire clip 12 -> 11; ADS, wheel selection, movement, mouse, menu, pointer lock, audio, and checkpoint pass | CargoShip in / Killhouse out pass; 591.775 ms context recovery | 122,778,819 B aggregate CPU recovery / 866,582,528 B capacity | **PLAYABLE** | Clean `247980a6` record; compact indoor/conventional-combat coverage. |
| `hunted` | All canonical runtime boundaries pass | 6,660.220 ms | 1,145 frames; 19.08 FPS; 55.40 ms p95; 0.993989 ratio | Fire clip 6 -> 5; ADS, movement, mouse, menu, pointer lock, audio, and checkpoint pass; wheel is `NOT_APPLICABLE_SINGLE_WEAPON` | CargoShip in / Killhouse out pass; 1,984.015 ms context recovery | 480,307,702 B aggregate CPU recovery / 968,163,328 B capacity | **FUNCTIONAL** | Clean `247980a6` record; outdoor visibility, foliage, world, and dynamic-model coverage; average and p95 miss the threshold. |
| `bog_a` | All canonical runtime boundaries pass | 9,821.955 ms | 1,271 frames; 21.21 FPS; 56.64 ms p95; 0.999457 ratio | Fire clip 15 -> 14; ADS, wheel selection, movement, mouse, menu, pointer lock, audio, and checkpoint pass | CargoShip in / Killhouse out pass; 1,844.635 ms context recovery | 428,120,523 B aggregate CPU recovery / 961,937,408 B capacity | **FUNCTIONAL** | Clean `247980a6` rerun; dense combat, FX, material, entity, and audio coverage; average and p95 miss the threshold. |
| `scoutsniper` | DB, ClipMap/world, server, game, cgame, and actual world frames pass | 9,547.870 ms | Headless stationary window: 3,601 frames / 60,012.685 ms; no visual claim | Not exercised; no input injected | CargoShip transition-in passed; context recovery and transition-out not exercised | 414,917,420 B aggregate CPU recovery / 955,514,880 B capacity | **RENDERS** | Dirty observe-only run based on `838e047c`; no page, GL, or lifecycle errors. |
| `ac130` | Canonical DB/world, server/game and client/cgame pass | 6.046 seconds after command | Headless stationary only; active performance unverified | Not exercised | CargoShip transition-in; recovery/out not exercised | 930,676,736 B Wasm capacity | **RENDERS** | [Chrome 152 diagnostics and production; source/artifact identity](evidence/ac130-rendering-2026-09-03.md). |
| `aftermath` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |
| `airlift` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |
| `ambush` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |
| `armada` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |
| `bog_b` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |
| `coup` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |
| `icbm` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |
| `jeepride` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |
| `launchfacility_a` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |
| `launchfacility_b` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |
| `simplecredits` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |
| `sniperescape` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |
| `village_assault` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |
| `village_defend` | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Owned-file discovery only; no runtime or completion claim. |

`decodedTextureRecoveryBytes` is the logical decoded texture size. It is not
the retained recovery allocation after `c66d41e1`: the renderer now retains
encoded/canonical image sources where supported. `aggregateCpuRecoveryBytes`
and `textureRecoverySourceBytes` describe actual retained recovery storage.
At the same Killhouse point this reduced aggregate CPU recovery by 64.27% and
Wasm capacity by 45.28%, in exchange for longer successful context re-decode
(+57% to +94.75%, approximately +90% on returned Killhouse) and a 14.43%
Killhouse first-frame increase. Exact comparisons are in the execution report.

## Result definitions

- `UNTESTED`: no retail runtime evidence.
- `LOADS`: canonical database/world loading completes without a proven world
  frame.
- `RENDERS`: at least one actual canonical world frame is produced.
- `FUNCTIONAL`: sustained runtime with functional core gameplay, input, audio,
  transitions, and no fatal error.
- `PLAYABLE`: `FUNCTIONAL` plus a valid foreground performance window meeting
  the current documented reference threshold.
- `BLOCKED`: a deterministic compatibility failure is reproduced and
  classified.
- `REGRESSION`: a map with current passing evidence now fails.

Failure classes follow the earliest incorrect boundary: `filesystem`,
`database`, `asset publication`, `ClipMap/world`, `server`, `game/scripts`,
`client/cgame`, `renderer frontend`, `WebGL2 renderer`, `material`, `entity`,
`fx`, `audio`, `save/persistence`, `memory/lifecycle`, or `unknown`.

## Historical evidence

The historical `887f1c87` baseline recorded Killhouse as PLAYABLE and
CargoShip as RENDERS. The clean two-map matrix at `ac063bb2` and first Blackout
campaign run at `6be926cb` remain useful canonical lifecycle evidence:

- [two-map structured evidence](evidence/retail-phase1-ac063bb2.json)
- [Blackout structured evidence](evidence/retail-phase3-6be926cb.json)

Their unfocused/background-susceptible frame timings predate the foreground
validity gate and do not establish current performance status. The current
matrix above supersedes their compatibility classifications.

No proprietary assets are used in hosted CI. Promote a row only from a local
run against legally owned files, recording the exact commit, browser, date,
reference host, foreground validity, and sanitized machine-readable result.
Configuration checkpoints demonstrate config persistence, not complete
gameplay save/load parity. The Airplane `da1e592c` mission record is the sole
current complete gameplay save/load proof; do not generalize it to other maps.
