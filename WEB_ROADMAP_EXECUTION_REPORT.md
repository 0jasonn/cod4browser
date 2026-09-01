# Web roadmap execution report — historical evidence

This report records earlier passes through 2026-08-28. Its gates, recommendations,
and uses of "current" are historical, not instructions for new work. See
[the active roadmap](docs/web-roadmap.md), [current status](docs/web-status.md),
and [the 2026-08-31 cleanup record](docs/evidence/cleanup-renderer-2026-08-31.md).
Mission progression is not a prerequisite for renderer work or cleanup.

## Retired mission-route pass — 2026-08-28

This historical pass once added a diagnostics-only mission-route author/replay
workflow. The workflow, browser branches, wrapper controls, and dedicated
synthetic tests were deleted on 2026-09-01. Route generation and simulated
mission progression are no longer part of the product or validation plan.

No Village Assault progression claim was demonstrated. The stages that followed
were therefore not started in this pass. Current handoff state:
`RETAIL ROOT UNAVAILABLE`.

## Corrected-profile roadmap pass — 2026-08-28

This section supersedes the mission-progression, profiling, renderer, and
decode recommendations in the older sections below. Historical measurements
remain valid only at their recorded commits.

| Field | Current result |
| --- | --- |
| Roadmap branch / base | `codex/corrected-profile-roadmap` / `aa407bb5` |
| Corrected six-map evidence | `f0b3c5b4`, sanitized at `d05f788f` |
| One renderer optimization | `93451ec5` |
| Strict mission validator | `e7be6898` |
| Decode investigation / optimization | `92a93e39` / `a83f8047` |
| Wasm numeric fix / audit | `d252515d` / `bad1e7b9` |
| Final verification source / retail regression | `39de3d6d` / seven-stop six-map chain passed |
| Browser / host | Headed Chrome 152.0.7977.64 / Windows 11 Pro x64 / Ryzen 7 7800X3D / RTX 3070 Ti / 32 GiB |
| Retail root | Explicit legal local root supplied; path and assets omitted from evidence |

The diagnostic capture now counts only completed gameplay frames that submit
renderer work. Non-gameplay pumps, context loss, render-less callbacks, and
old-map transition frames do not consume the target. Captures terminate at an
explicit timeout and report partial counts. Retail validation runs independent
60-second profiling-disabled clean windows and exact 300-gameplay-frame
diagnostic windows; only the clean window determines `PLAYABLE`. GPU stage
timing rotates non-nested asynchronous WebGL2 elapsed queries across world,
static models, sun shadows, spot shadows, dynamic/FX, and UI/post, rejecting
disjoint or stale-generation results.

The corrected six-map profile selected one renderer change: reuse numeric DObj
skinning scratch capacity across surfaces and frames without retaining
canonical pointers. The complete comparison is
[retail-profile-93451ec5.md](docs/evidence/retail-profile-93451ec5.md). Every
map, transition, context recovery, persistence, input, and audio gate passed.
CargoShip's authoritative clean window improved 13.29 -> 14.22 FPS (+6.99%),
with p95 82.10 -> 77.15 ms and p99 91.35 -> 85.08 ms. However, its separately
profiled scene-build stage changed 28.61 -> 29.22 ms (+2.13%). The bounded
allocation change is retained, but it did not prove removal of the dominant
remaining CargoShip frontend cost. No second renderer optimization was
stacked.

| Map | Clean FPS after | p95 / p99 | Game/wall | Current result |
| --- | ---: | ---: | ---: | --- |
| Airplane | 59.51 | 19.12 / 20.99 ms | 0.999783 | PLAYABLE |
| Killhouse | 40.45 | 27.25 / 28.86 ms | 0.998363 | PLAYABLE |
| Blackout | 28.79 | 37.33 / 39.97 ms | 0.998603 | FUNCTIONAL |
| Bog A | 23.42 | 48.46 / 51.52 ms | 0.999583 | FUNCTIONAL |
| Hunted | 20.93 | 50.79 / 56.21 ms | 0.994109 | FUNCTIONAL |
| CargoShip | 14.22 | 77.15 / 85.08 ms | 0.998161 | FUNCTIONAL |

Airplane remains valid evidence for AI/scripts, combat, natural and named save
creation, death/restart, browser shutdown, fresh-runtime `loadgame`, restored
gameplay state, and continued play. It is **not** objective/trigger progression
evidence: objective hash, active-objective count, completed-objective count,
and mission flags remained unchanged in the recorded interval.

The strict clean Village Assault run required one of those canonical markers
to change before allowing checkpoint/death/reload validation. Active actors,
alive actors, script threads, an active objective, actor-state change, and
canonical input all passed, but no monitored progression marker changed during
the bounded 120-second action window. The downstream progressed checkpoint,
death/restart, shutdown, reload, and continued-progression stages therefore did
not run. This does not prove a canonical defect; the automation did not prove
that it traversed the required mission trigger. The sanitized record is
[retail-mission-village-assault-e7be6898.json](docs/evidence/retail-mission-village-assault-e7be6898.json).
No map currently carries `MISSION_FLOW_VALIDATED`.

The encoded-source investigation proved an avoidable initial double decode.
Shared decoder validation now inspects format/layout without allocating
pixels, performs one decode for initial upload, discards temporary RGBA, and
re-decodes the retained encoded source only for context recovery. Across
`Killhouse -> CargoShip -> Blackout -> Hunted -> Bog A -> Airplane -> Killhouse`,
initial decoder calls fell 12,046 -> 6,033, duplicate decodes 6,023 -> 0,
initial decoded bytes by 49.63%, and initial decode CPU by 49.06%. Mean first
world frame improved 4.53%; mean recovery remained within 1.70%. Map-owned
sources retired at every unload, the global source cache stayed bounded, and
recovery added no Wasm capacity. See
[retail-decode-919f8c27.md](docs/evidence/retail-decode-919f8c27.md).

The focused Wasm numeric audit corrected SP script dvar-float parsing and
`floor`/`ceil`, whose decompiled 8-byte writes could not form a valid 16-byte
Wasm `long double`. Native and direct-Wasm behavioral tests cover the exact
typed production seam. The remaining matches are classified, not mechanically
rewritten, in
[wasm-numeric-portability-d252515d.md](docs/evidence/wasm-numeric-portability-d252515d.md).

The named next-batch fastfiles were present in the supplied installation. This
was discovery-only at the time; current evidence now records `scoutsniper` as
`RENDERS`, while `village_assault` and `ac130` remain `UNTESTED`.

### Final verification matrix

The runtime verification below ran from clean source commit `39de3d6d`. The
subsequent changes only add this sanitized evidence and report. Retail opt-in
was disabled for the routine remainder suite, preserving its two explicit
`RETAIL_ROOT_MISSING` skips; the six-map command supplied the legal root
explicitly.

| Gate | Result |
| --- | --- |
| Dependency install | Pass; 74 packages installed, 75 audited, 0 vulnerabilities |
| Node protocol/lifecycle/filesystem | 78 passed |
| `node --check` | Pass |
| ESLint | Pass |
| Strict `checkJs` | Pass |
| Runtime/gradual `checkJs` | Pass |
| Native Clang portable | 22 passed |
| Native MSVC portable | 30 passed |
| Direct Wasm portable | 30 passed |
| Sanitizer/libFuzzer smoke | 256 runs passed |
| Production Release build / canonical-prefix check | Pass |
| Production boundary | Pass; 3,173,293 B Wasm, 340,615 B application JavaScript, 3,524,439 B site, 17 files, 24 raw exports, 9 application exports |
| Production Playwright | 40 passed |
| Diagnostics Release build / canonical-prefix check | Pass |
| Diagnostic smoke | 12 passed |
| Diagnostic remainder | 36 passed; 2 expected `RETAIL_ROOT_MISSING` skips |
| Current six-map retail regression | 1 passed in 3.3 minutes; seven loads, canonical lifecycle plus 30 world frames at every stop, seven context recoveries, zero duplicate decodes, bounded source cache, map-owned source retirement |
| `git diff --check` | Pass before report-only evidence update; repeated after the update |

The final chain was `Killhouse -> CargoShip -> Blackout -> Hunted -> Bog A ->
Airplane -> Killhouse`. Peak encoded source-cache use was 27,919,060 B of the
268,435,456 B budget and no context recovery grew Wasm linear-memory capacity.
The sanitized record is
[retail-six-map-regression-39de3d6d.json](docs/evidence/retail-six-map-regression-39de3d6d.json).

## Historical profiling pass — 2026-08-27

This section supersedes the execution recommendation below; the historical
retail evidence remains valid only for its recorded commits and dates.

| Field | Current result |
| --- | --- |
| Starting SHA / branch | `d3f4c8d696e21939e7ef85cc19eb331ef104c30b` / `codex/web-frame-profile-mission` |
| Profiler implementation | `91788492`, completed by `aff008c3` |
| Integration branch | `web-port`; no additional branch remains |
| Retail root supplied to this process | **YES**; explicit local path omitted from evidence |
| Correctness fixes | `e42b48f3` through `721a981b` |
| Renderer optimization | `9e75a9dd` |
| Mission validator / branch | `da1e592c` / `codex/web-mission-progression` |
| Browser / host | Headed Chrome 151.0.7922.174 / Windows 11 Pro x64 / Ryzen 7 7800X3D / RTX 3070 Ti |

Commits `91788492` and `aff008c3` add an explicitly requested,
maximum-600-sample diagnostic capture. It reports engine CPU stages, renderer
frontend/backend stages, separate sun/spot shadow preparation and draw stages,
CPU upload duration and byte counts, actual GL draw work, retained and drawn
geometry counts, LOD/shadow activity, and asynchronous
`EXT_disjoint_timer_query_webgl2` results correlated by pump tick, context
generation, and view generation. GPU reads never synchronously wait. Disjoint
or stale-context results are classified and excluded from valid timing
summaries. The production boundary now rejects any `frame-profile` leak.

The supplied retail root first exposed a deterministic CargoShip -> Bog A
XAnim failure. The earliest incorrect boundary was a native ABI assumption:
several canonical game/script functions type-punned an 8-byte `double` through
`long double`, which is 16 bytes in Wasm. The final fixes preserve native
float-rounding order while using direct `double` operations. Game-owned state
is also released before map database retirement and animation state resets at
reload. CargoShip -> Bog A -> Killhouse now passes; all temporary diagnostic
commits were reverted.

Pre-change profiles identified combined sun/spot shadow drawing as the largest
backend stage. Commit `9e75a9dd` makes exactly one optimization: opaque shadow
casters skip texture/sampler binds that only alpha-tested casters consume.
Across six maps this reduced average shadow CPU time from 11.325 to 7.144 ms
(-36.91%), texture binds from 51,661 to 34,874 (-32.49%), and backend CPU time
from 25.863 to 24.166 ms (-6.56%). Every map passed its supported full retail
validator after the change. The sanitized evidence is
[retail-profile-9e75a9dd.json](docs/evidence/retail-profile-9e75a9dd.json).

| Map | FPS | Frame avg / p95 | Frontend / backend CPU | Backend GPU | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| Killhouse | 43.575 | 22.949 / 24.960 ms | 8.639 / 14.106 ms | 15.606 ms | PLAYABLE |
| CargoShip | 11.941 | 83.743 / 92.575 ms | 34.880 / 45.613 ms | 43.338 ms | FUNCTIONAL |
| Blackout | 24.009 | 41.650 / 45.775 ms | 10.267 / 31.897 ms | 32.382 ms | FUNCTIONAL |
| Airplane | 59.962 | 16.677 / 18.505 ms | 7.727 / 2.309 ms | 3.535 ms | PLAYABLE |
| Hunted | 21.629 | 46.235 / 49.105 ms | 15.664 / 29.462 ms | 31.280 ms | FUNCTIONAL |
| Bog A | 20.830 | 48.009 / 61.505 ms | 16.035 / 21.608 ms | 23.174 ms | FUNCTIONAL |

### Historical validation

| Gate | Result |
| --- | --- |
| Headed retail baseline: Killhouse -> CargoShip -> Blackout -> Killhouse | Pass, 1/1 in 4.8 minutes |
| Headed retail Airplane campaign validator | Pass, 1/1 |
| Headed retail Hunted campaign validator | Pass, 1/1 |
| Headed retail Bog A campaign validator | Pass, 1/1 |
| Headed retail Airplane mission/save/restart validator | Pass, 1/1 in 1.6 minutes at clean `da1e592c` |
| Production Release build and canonical prefix | Pass |
| Routine browser smoke | 12/12 pass |
| Non-overlapping browser remainder | 36 pass; 2 expected retail-only skips |
| Production boundary | Pass; exact files/exports and all budgets |

The production artifact is 3,173,476 B Wasm, 340,615 B application JavaScript,
and 3,524,622 B total across 17 files, with 24 raw Wasm exports and 9 named
application exports. The respective budgets remain 3,332,379 B, 357,646 B,
and 3,701,082 B.

The representative mission-flow proof now passes without synthetic gameplay
state. At clean `da1e592c`, Airplane exposed 6 live canonical actors, 476
script threads, and one active objective; actor state changed, primary fire
produced an enemy-damage event, and the natural checkpoint committed. A named
236,562-byte save survived canonical death/restart, browser shutdown, and a
fresh-runtime `loadgame`; objective identity, player state, weapon state, live
actors/scripts, movement, firing, and frame progression continued afterward.
The sanitized record is
[retail-mission-da1e592c.json](docs/evidence/retail-mission-da1e592c.json).

This historical next-batch statement is superseded: `scoutsniper` now has
stationary `RENDERS` evidence, `village_assault` progression automation is
retired, and `ac130` is the next stationary thermal/material probe.

Final classification: `PERFORMANCE BOTTLENECK IDENTIFIED — NEXT FIX REQUIRED`

## Historical iteration — 2026-08-27 foreground, recovery memory, and campaign batch

This section supersedes the compatibility labels and recommendations in the
historical sections below. It records local execution against a legally owned
installation without recording its path or any proprietary content.

| Field | Current evidence |
| --- | --- |
| Starting SHA | `01e54eacc3d6961f572c44c08fcc9a59600f478f` |
| Clean foreground runtime evidence SHA | `f52298064234092ebce455096c6fe28ea70490e5` |
| Current roadmap branch / starting SHA | `codex/web-frame-profile-mission` / `d3f4c8d696e21939e7ef85cc19eb331ef104c30b` |
| Foreground evidence artifact commit | `b59c32a009b4224e5ec06f0602549e1471d827d6` |
| Policy/campaign evidence commit | `c1c17121ada5e66e09138eaeb9ff12afa40cf35f` |
| Date | 2026-08-27 |
| Browsers | Headed branded Chrome 151 and Microsoft Edge 151 |
| Campaign evidence browser | Chrome 151.0.7922.174, headed |
| OS | Windows 11 Pro 10.0.26200 x64 |
| Reference hardware | AMD Ryzen 7 7800X3D, 16 logical processors, 33,944,879,104 B system memory |
| Build | Release diagnostics |
| Retail root for recorded six-map evidence | YES; local path deliberately omitted |
| Retail root in the current profiling process | NO; `KISAK_COD4_RETAIL_ROOT` absent, so no fresh retail rerun is claimed |

The implementation chain for this iteration is:

| Commit | Evidence or implementation |
| --- | --- |
| `e25fcc2c` | Added allocator, Wasm capacity/program-break, image-cache, source-classification, and full renderer-memory lifecycle telemetry. |
| `c66d41e1` | Retained canonical encoded LoadDef/IWI recovery sources for shared 2D image pools and re-decoded them through the existing decoder for upload and context restoration. |
| `247980a6` | Made the diagnostic validator count every existing `kisakcod:audio-playback` event instead of relying on a finite summarized playback view during audio-heavy scenes. |
| `c1c17121` | Tracked the repository policy and the non-proprietary campaign evidence record. |
| `658e7787` | Kept rich renderer-memory fields diagnostics-only and restored the compact production event. |
| `f5229806` | Made the retail validator wait for the Worker-owned relative-mouse-mode publication before the trusted pointer-lock click. |
| `b59c32a0` | Preserved the final clean Chrome/Edge foreground result and exact renderer identity without retail paths or assets. |

No proprietary fastfile, decoded asset, retail path, or game installation data
is present in the evidence artifact.

### Foreground validation and playable threshold

The initial reference threshold is average FPS-equivalent >= 30, p95 frame
time <= 50 ms, and game-time/wall-time ratio >= 0.90 on the reference machine.
It is a classification reference for this hardware, not a universal minimum
requirement.

The clean Killhouse -> CargoShip -> Blackout -> Killhouse matrix passed in
headed Chrome and headed Edge. Every timed window was visible and focused,
had zero background transitions, and was valid for performance use. Exact
values retained from that foreground run are:

| Map | Browser | Average FPS-equivalent | p95 frame time | Game/wall ratio | Current result |
| --- | --- | ---: | ---: | ---: | --- |
| Killhouse | Chrome 151.0.7922.174 | 33.708 | 32.210 ms | 0.999075 | PLAYABLE |
| CargoShip | Chrome 151.0.7922.174 | 13.151 | 82.845 ms | 0.997954 | FUNCTIONAL |
| Blackout | Chrome 151.0.7922.174 | 26.823 | 39.690 ms | 0.998008 | FUNCTIONAL |
| Killhouse | Edge 151.0.4129.107 | 35.027 | 31.180 ms | 0.998705 | PLAYABLE |
| CargoShip | Edge 151.0.4129.107 | 13.872 | 79.775 ms | 0.997752 | FUNCTIONAL |
| Blackout | Edge 151.0.4129.107 | 26.976 | 39.470 ms | 0.997445 | FUNCTIONAL |

The full canonical input matrix passed on each baseline map: W/S/A/D, jump,
mouse look, MOUSE1 with canonical clip/ammo response, MOUSE2/ADS, wheel weapon
selection or the explicit single-weapon not-applicable result, Escape/menu,
pointer-lock loss, and pointer-lock reacquisition. Reduced critical input also
passed after every in-process transition. These results replace the historical
background-throttled frame conclusions; the old approximately 1 FPS numbers
remain invalid for performance interpretation.

The tracked
[foreground evidence](docs/evidence/retail-foreground-f5229806.json) preserves
the complete p50/p95/p99 fields and exact WebGL identity. Both browsers used
WebGL 2 through ANGLE's Direct3D 11 backend on an NVIDIA GeForce RTX 3070 Ti;
the implementation reported `hardware-or-driver`, not a software renderer.

### Recovery-memory instrumentation and measured strategy

The `c66d41e1` strategy changes only the recovery representation for the four
shared 2D image pools. It retains exact database LoadDef metadata/payload or a
complete canonical IWI member, validates admission against logical decoded
bytes, and transiently decodes one image at a time for initial upload or
context restoration. It does not add a parallel parser, lower the 800 MiB
decoded per-pool admission limit, or change sky/reflection cubes, lighting
atlases, water, render targets, or canonical asset ownership.

Same-point Killhouse steady-state measurements were:

| Metric | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| Aggregate CPU recovery copy | 1,417,257,708 B | 506,423,759 B | 64.27% |
| Allocator bytes in use | 1,731,297,456 B | 820,736,432 B | 52.59% |
| Wasm linear-memory capacity | 1,809,121,280 B | 989,921,280 B | 45.28% |
| Wasm program break | 1,808,785,408 B | 901,816,320 B | 50.14% |

The after-strategy Killhouse snapshot contained 1,377,932,328 B of logical
decoded image data, 416,472,491 B of actual retained texture recovery sources,
385,473,027 B of encoded image sources, 89,951,268 B of geometry, and a
16,777,216 B maximum transient decode/upload. The process-global database
LoadDef cache held 23,987,140 B. Source classification was:

| Recovery source | Images | Retained bytes | Logical decoded bytes | Class |
| --- | ---: | ---: | ---: | --- |
| Database LoadDef | 8 | 28,311,552 | 56,623,104 | B/C/E or F according to database ownership |
| Canonical IWI member | 1,019 | 357,161,475 | 1,290,309,760 | B/C/F |
| Synthetic raw `$white` | 2 | 8 | 8 | B/E |

No irreplaceable source was found. Map-local recovery returned to zero or the
expected process-global cache floor before the next publication; no old/new
map overlap was accepted. Content duplication was not assumed or merged.

The memory saving traded CPU capacity for longer, but still successful,
context re-decode time:

| Recovery point | Before | After | Change |
| --- | ---: | ---: | ---: |
| CargoShip | 853.930 ms | 1,343.815 ms | +57% |
| Blackout | 959.670 ms | 1,868.980 ms | +94.75% |
| Returned Killhouse | 1,011.245 ms | 1,921.340 ms | approximately +90% |

Killhouse map-to-first-frame moved from 4,954 ms to 5,668.86 ms (+14.43%);
CargoShip remained approximately flat at 6,815 ms versus 6,834 ms. Short
comparison windows measured Killhouse at 36.16 versus 35.36 FPS-equivalent and
CargoShip at 12.61 versus 12.77. The complete three-map transition chain,
canonical input, asset publication/retirement, and forced context recovery on
CargoShip, Blackout, and returned Killhouse all passed after the change.

### Campaign batch: Airplane, Hunted, and Bog A

The representative batch was selected from discovered canonical SP zones:
Airplane for a compact conventional combat/input slice, Hunted for outdoor
visibility and foliage, and Bog A for dense combat, FX, materials, entities,
and audio. The clean machine-readable record is
[retail-campaign-247980a6.json](docs/evidence/retail-campaign-247980a6.json).

All three maps passed asset discovery, canonical DB completion, ClipMap/world,
server, game, cgame, first actual world frame, a valid foreground 60-second
window, player/mouse/fire/ADS input, audio, transition in/out, configuration
checkpoint, forced context recovery, and the no-fatal-error assertion.

| Map | First frame | Frames / average FPS | p95 / game-wall ratio | CPU recovery / Wasm capacity | GPU estimate / geometry / max upload | Audio / context recovery | Result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Airplane | 7,352.500 ms | 3,597 / 59.946 | 18.650 ms / 0.999992 | 122,778,819 / 866,582,528 B | 338,121,048 / 10,871,916 / 8,388,608 B | 1,192,220 B / 591.775 ms | PLAYABLE |
| Hunted | 6,660.220 ms | 1,145 / 19.080 | 55.400 ms / 0.993989 | 480,307,702 / 968,163,328 B | 1,395,967,260 / 62,769,240 / 16,777,216 B | 5,938,804 B / 1,984.015 ms | FUNCTIONAL |
| Bog A | 9,821.955 ms | 1,271 / 21.210 | 56.640 ms / 0.999457 | 428,120,523 / 961,937,408 B | 1,257,474,100 / 54,295,548 / 16,777,216 B | 7,385,336 B / 1,844.635 ms | FUNCTIONAL |

The input evidence is canonical rather than audio-inferred: Airplane changed
clip 12 -> 11, Hunted 6 -> 5, and Bog A 15 -> 14. Wheel selection passed on
Airplane and Bog A; Hunted correctly recorded
`NOT_APPLICABLE_SINGLE_WEAPON`. Pointer-lock loss and reacquisition passed on
all three. The later final baseline reproduced the same intermittent boundary
after context recovery: the canonical menu catcher had cleared, but the
validator clicked before the Worker's relative-mouse-mode publication reached
the DOM host. Commit `f5229806` waits for that existing platform signal before
the trusted click; the unchanged pointer-lock assertion then passed the
complete clean Chrome and Edge matrices.

Current campaign classification is:

| Result | Count | Maps |
| --- | ---: | --- |
| PLAYABLE | 2 | Killhouse, Airplane |
| FUNCTIONAL | 4 | CargoShip, Blackout, Hunted, Bog A |
| RENDERS | 0 | — |
| LOADS | 0 | — |
| BLOCKED | 0 | — |
| REGRESSION | 0 | — |
| UNTESTED | 16 | Other discovered direct SP zones; discovery is not compatibility evidence |

The FUNCTIONAL labels for CargoShip and Blackout are threshold
reclassifications, not runtime regressions: both still pass their canonical
runtime, gameplay, transition, persistence, and recovery matrices.

### Historical fixes, gates, and artifact status

Two validator corrections were required. Dense Bog A audio exposed that the
finite summarized playback view was not a reliable event counter, so
`247980a6` counts every existing playback event. Final context-recovery input
then exposed an asynchronous Worker-to-DOM mouse-mode boundary; `f5229806`
waits for relative mode before clicking. Neither change bypasses a map,
weakens an assertion, or substitutes for canonical gameplay state.

The complete non-overlapping validated-runtime-milestone gate matrix passed:

| Gate | Result |
| --- | --- |
| Dependency install | `npm ci` passed; 75 packages audited, 0 vulnerabilities |
| Static JavaScript | Syntax, ESLint, strict `checkJs`, and runtime `checkJs` passed |
| Node protocol/lifecycle/filesystem | 76 passed |
| Native Clang portable | 21 passed |
| Native Win32 MSVC portable | 29 passed |
| Direct Wasm portable | 29 passed |
| Sanitizer fuzz smoke | 256 runs passed |
| Production browser | 40 passed |
| Diagnostics smoke | 12 passed |
| Diagnostics remainder | 35 passed; exactly 2 expected retail-root skips |
| Production/diagnostics builds | Passed |
| Production boundary | Passed; exact files and exports preserved |

The exhaustive browser aggregate was not rerun because it duplicates the
authoritative native/direct-Wasm cases; the repository's routine smoke and
non-overlapping remainder tiers are the required handoff set.

The accidental-bloat review found that new allocator/cache/source diagnostic
field names were still serialized in production even though the allocator
values were diagnostics-only. Commit `658e7787` restores the compact existing
production memory event while retaining the complete diagnostics event. This
reduced production application JavaScript from 342,674 B to 340,615 B before
the milestone baseline was approved.

The fresh Release baseline is anchored to
`658e778784da6a13dcfbde8189f5c98d48f17334` with 5% headroom:

| Metric | Actual/baseline | Budget/cap | Result |
| --- | ---: | ---: | --- |
| Wasm | 3,173,694 B | 3,332,379 B | Pass |
| Application JavaScript | 340,615 B | 357,646 B | Pass |
| Total site | 3,524,840 B | 3,701,082 B | Pass |
| Site files | 17 | exact allowlist of 17 | Pass |
| Raw Wasm exports | 24 | 24 | Pass |
| Named application exports | 9 | exact allowlist of 9 | Pass |

The pinned baseline records Emscripten 6.0.6, CMake 4.2.0-rc3, and Ninja
1.13.2. The file/export allowlists and diagnostic-source exclusions were not
loosened.

## Historical recommendation

Begin bounded frame-stage instrumentation, then obtain valid foreground
profiles for all six known maps before selecting one renderer optimisation.
After that change is stable, prove one real mission checkpoint/death/restart/
shutdown/save-reload loop. Broader campaign expansion follows those gates.
The current process cannot perform the retail portions because
`KISAK_COD4_RETAIL_ROOT` is absent.

All sections below are historical records of the 2026-08-26 milestone. Their
old PLAYABLE labels and recommendations do not override the current
threshold-based classifications and gate results above.

## Phase 0 baseline (historical)

| Field | Value |
| --- | --- |
| Starting and validated runtime SHA | `10000c094a940d7a1e7807df19dd3007e5ce6c8c` |
| `origin/web-port` SHA | `10000c094a940d7a1e7807df19dd3007e5ce6c8c` |
| Merge base | `10000c094a940d7a1e7807df19dd3007e5ce6c8c` |
| Roadmap branch | `codex/web-retail-roadmap` |
| Date | 2026-08-26 |
| Browser used | Playwright Chromium 149.0.7827.55 |
| OS | Microsoft Windows 11 Pro 10.0.26200, build 26200 |
| Production configuration | Release |

The pinned toolchain was Emscripten 6.0.6, CMake 4.2.0-rc3, Ninja
1.13.2, Node.js 24.18.0, npm 11.16.0, Playwright 1.61.1, TypeScript
7.0.2, and ESLint 10.9.0. Native coverage used Clang 24.0.0git and
MSVC 19.51.36256 x86.

Baseline counts were 74 Node tests, 21 native Clang portable tests, 29
native MSVC x86 portable tests, 29 direct-Wasm tests, 256 sanitizer fuzz
runs, 40 production browser tests, 12 diagnostic smoke tests, and 34 passing
diagnostic remainder tests with two expected retail-root skips. All applicable
non-retail gates passed.

## Phase 1 retail execution (historical)

`KISAK_COD4_RETAIL_ROOT supplied: YES`

An explicit, legally owned retail root was supplied to the repository
validator. The clean run completed 1/1 in 4.6 minutes at source SHA
`ac063bb20cbc4027497841322d87c2069d736939` with no dirty-tree inputs. It used
the Release diagnostics build, Playwright Chromium 149.0.7827.55, and Windows
11 x64. The machine-readable record is
[retail-phase1-ac063bb2.json](docs/evidence/retail-phase1-ac063bb2.json); it
contains no retail paths or proprietary content.

## Phase 1 map results (historical at `ac063bb2`)

### Killhouse

| Evidence field | Historical result |
| --- | --- |
| DB / CGame / world | Pass |
| Clip / Server / Game | Canonical SP path reached; not separate booleans in the structured result |
| First world frame | Pass; 6,797.800 ms after map command |
| 60s stable | Pass; 60,025.385 ms and 94 frames |
| Frame time | 643.155 ms average; 683.800 ms p95; 737.735 ms p99 |
| Input | Pass; movement, mouse look, primary-fire audio, secondary action |
| Audio | Pass; 5,881,060 B decoded, 15 buffers queued |
| Transition out | Pass; old recovery retired before CargoShip publication |
| Configuration checkpoint/reload | Checkpoint passed: 2 files, 3,154,912 B, 238.69 ms; aggregate reload passed after CargoShip |
| Context recovery | Not separately measured on Killhouse; the forced recovery ran on CargoShip |
| Stability-end Wasm capacity | 2,013,724,672 B |
| Aggregate CPU / decoded / GPU estimate / geometry | 1,517,951,436 / 1,426,285,608 / 1,447,485,000 / 91,665,828 B |
| Temporary upload / shader program | 0 / 0 B |
| Result | PLAYABLE |
| Failure class | None |

### CargoShip

| Evidence field | Historical result |
| --- | --- |
| DB / CGame / world | Pass |
| Clip / Server / Game | Canonical SP path reached; not separate booleans in the structured result |
| First world frame | Pass; 10,838.495 ms after map command |
| 60s stable | Pass; 60,091.810 ms and 67 frames |
| Frame time | 897.914 ms average; 1,235.010 ms p95; 1,416.035 ms p99 |
| Input | Pass; movement, mouse look, primary-fire audio, secondary action |
| Audio | Pass; 1,582,592 B decoded, 12 buffers queued |
| Transition in | Pass; Killhouse to CargoShip |
| Transition out | No second map transition measured; explicit shutdown passed |
| Configuration checkpoint/reload | Pass; 7 files, 7,091,210 B, 408.28 ms; shutdown reload passed |
| Context recovery | Pass; recovered world frame in 1,779.19 ms and input resumed |
| Stability-end Wasm capacity | 2,013,724,672 B |
| Aggregate CPU / decoded / GPU estimate / geometry | 753,839,840 / 679,402,508 / 700,601,900 / 74,437,332 B |
| Temporary upload / shader program | 0 / 0 B |
| Result | PLAYABLE |
| Failure class | None |

Retained texture recovery was admitted independently per image pool:

| Image recovery pool | Killhouse | CargoShip |
| --- | ---: | ---: |
| World | 285,524,036 B | 135,217,216 B |
| Static models | 817,908,800 B | 207,635,520 B |
| Dynamic models | 289,375,232 B | 303,781,952 B |
| UI | 2,478,084 B | 2,359,300 B |
| Supplemental textures | 30,999,456 B | 30,408,520 B |
| Per-pool admission cap | 838,860,800 B | 838,860,800 B |

## Fixes (historical)

Phase 1 reproduced and fixed three runtime-boundary defects:

| Commit | Reproduction and earliest boundary | Smallest implementation | Focused regression |
| --- | --- | --- | --- |
| `1826e037` | Generated DB loading published unresolved leading-comma asset stubs; the earliest defect was canonical DB alias/default resolution. | Strip and resolve the stub through the normal DB path without enlarging pools. | `gate3_db_stream_trace_tests` passed in direct Wasm and MSVC. |
| `5c3b0fce` | CargoShip reported `duration must be greater than 0`; the earliest defect was the Wasm ABI conversion in `GScr_Earthquake`. | Preserve the native floor conversion without type-punning an 8-byte double through a 16-byte `long double`. | The retail assertion passed and the complete two-map matrix contained no matching error. |
| `a6d09fe1` | The canonical `writeconfig` command was absent after startup; the earliest defect was the split common-initialization continuation. | Resume `quit`, `writeconfig`, and `writedefaults` registration and canonical configuration serialization after `ProfLoad_Init`, matching native order. | Strict-prefix checks passed in direct Wasm and Win32 MSVC. |

After each runtime fix, the established Killhouse path was rerun before the
CargoShip transition. The final clean Phase 1 matrix then passed both complete
60-second map regressions, transition retirement, context recovery, shutdown,
and reload at `ac063bb2`.

Commit `ac063bb2` hardened the renderer telemetry and validator so the clean
run records exact DB, cgame, frame, input, audio, checkpoint, transition,
context-recovery, shutdown, source-SHA, and dirty-tree evidence.

## Memory and Phase 2 decision (historical)

The Killhouse-to-CargoShip transition passed with WebGL context generation
unchanged at 1. Old-map decoded texture recovery fell from 1,430,575,400 B to
zero and old-map aggregate CPU recovery fell from 1,521,922,580 B to zero
before the new world was published. Peak aggregate CPU recovery was exactly
1,521,922,580 B, so the run observed no old/new recovery overlap. CargoShip
world publication began with 175,806,748 B of aggregate CPU recovery.

| Transition peak metric | Bytes |
| --- | ---: |
| Decoded texture recovery | 1,430,575,400 |
| Aggregate CPU recovery | 1,521,922,580 |
| Estimated GPU textures | 1,451,774,792 |
| Geometry | 91,347,180 |
| Temporary uploads | 0 |
| Shader programs | 0 |
| Wasm linear-memory capacity | 2,013,724,672 |

Retain the existing previous-map recovery eviction and add no optimization.
Wasm linear-memory capacity remained 2,013,724,672 B because allocator capacity
is monotonic; that is not an eviction failure.

Keep 800 MiB (838,860,800 B) as the per retained-image-pool admission cap, not
an aggregate decoded-recovery ceiling. Killhouse's static-model image pool was
817,908,800 B, 97.50% of the cap, with 20,952,000 B (about 19.98 MiB) of
headroom. Lowering the cap is not supported by this run.

| Class | Meaning | Observed resources |
| --- | --- | --- |
| A | Irreplaceable | None observed |
| B | Regenerable | All classified renderer/audio resources below |
| C | Reloadable from user storage | Decoded textures and audio |
| D | Cross-pool/content duplicate | Not measured |
| E | Intentionally global | Render targets, fixed pipeline programs, shader source/cache |
| F | Map-local | Decoded textures, geometry/draw commands, GPU map resources, temporary uploads, predominantly audio |

Decoded world/static/dynamic/UI/supplemental textures are B/C/F. Retained
geometry and portable draw commands are B/F; GPU map textures and buffers are
B/F; render targets and fixed pipeline programs are B/E; shader source/cache
is B/E (0 B measured); temporary uploads are B/F (0 B measured); and audio is
B/C and predominantly F. Wasm capacity is allocator capacity, not a retained
renderer resource class. This is a measured baseline only; no unobserved
before/after (A/B) memory or timing result is claimed.

## Phase 3 Blackout execution (historical)

The clean Phase 3 validator ran at
`6be926cb4e78693f9f6e638c348b0ee0f908b45f` with an explicitly supplied,
legally owned retail root. It passed 1/1 in 2.4 minutes in the Release
diagnostics build on Playwright Chromium 149.0.7827.55 and Windows 11 x64.
The non-proprietary machine-readable record is
[retail-phase3-6be926cb.json](docs/evidence/retail-phase3-6be926cb.json).

### Blackout

| Evidence field | Historical result |
| --- | --- |
| DB / Clip / world | Pass |
| Server / Game | Pass; `G_InitGame`, `G_LoadLevel`, `SV_InitGameVM`, and `SV_InitGameProgs` completed |
| CGame | Pass; `CG_Init` and `CL_InitCGame` completed |
| First world frame | Pass; 11,144.985 ms after map command |
| 60s stable | Pass; 60,022.685 ms and 73 frames |
| Frame time | 823.765 ms average; 932.975 ms p95; 967.930 ms p99 |
| Input | Pass; movement, mouse look, primary-fire audio, secondary action |
| Audio | Pass; 3,678,608 B decoded, 15 buffers queued |
| Transition in | Pass; CargoShip to Blackout, ordered unload/publication, context generation unchanged |
| Transition out | Pass; Blackout to Killhouse, ordered unload/publication |
| Configuration checkpoint/reload | Pass; 7 files, 7,687,388 B, 974.925 ms; shutdown reload passed |
| Context recovery | Pass; real frame recovered in 2,149.595 ms and input resumed |
| Stability-end Wasm capacity | 1,776,091,136 B |
| Aggregate CPU / decoded / GPU estimate / geometry | 1,300,761,988 / 1,233,813,448 / 1,255,012,840 / 66,948,540 B |
| Temporary upload / shader program | 0 / 0 B |
| Result | PLAYABLE |
| Failure class | None |

Blackout's retained image pools used 332,809,216 B for world images,
592,563,200 B for static models, 267,329,536 B for dynamic models, and
2,359,552 B for UI, plus 38,751,944 B of supplemental texture recovery. Each
pool remained below the unchanged 838,860,800 B per-pool admission cap.

The CargoShip-to-Blackout transition released 742,024,672 B of old-map
aggregate CPU recovery to zero before publication. Its peak aggregate CPU
recovery was 1,185,984,324 B. Forced context recovery advanced renderer
resource generation from 4 to 5. The Blackout-to-Killhouse transition released
1,315,887,176 B of old-map aggregate CPU recovery to zero; its peak was
1,334,750,040 B. Shutdown flushed in 887.360 ms, and persisted profile and
configuration state reloaded.

### Phase 3 diagnostic and fix chain

| Commit | Reproduction, boundary, and resolution |
| --- | --- |
| `1305f4b8` | Exposed the existing portable world-submission error before the canonical renderer drop. |
| `5a0225a8` | Woke the retail validator on that error, replacing a five-minute symptom timeout with the first failing boundary. |
| `c810b995` | Classified invalid descriptors and identified a valid canonical spot-shadow static-model instance rejected at the browser-only 20,000-instance ceiling. |
| `164fc1f2` | Restored native IW3's bounded 65,536-instance cardinality at the portable renderer boundary and added focused native/direct-Wasm maximum-cardinality coverage. |
| `6be926cb` | Awaited canonical filesystem mount after asset readiness so reload validation could not race persisted configuration execution. |

The final Blackout run passed without a map-specific bypass, synthetic world,
proprietary fixture, weakened assertion, or browser-owned gameplay state.

## Final validation matrix (historical)

| Gate | Final result |
| --- | --- |
| Node syntax | Pass; all 18 listed web modules plus the retail validator |
| ESLint | Pass; repository static set plus the retail validator |
| Strict `checkJs` | Pass |
| Runtime/gradual `checkJs` | Pass |
| Node protocol/lifecycle/filesystem | 74 passed |
| Native Clang portable | 21 passed |
| Native MSVC x86 portable | 29 passed |
| Direct Wasm portable | 29 passed |
| Sanitized fuzz smoke | 256 runs passed |
| Production Playwright | 40 passed |
| Diagnostic smoke | 12 passed |
| Diagnostic remainder | 35 passed; exactly 2 expected `RETAIL_ROOT_MISSING` skips |
| Phase 1 Killhouse/CargoShip retail validator | 1/1 passed in 4.8 minutes |
| Phase 3 Blackout retail validator | 1/1 passed in 2.4 minutes |
| Production/diagnostics builds and production boundary | Pass |

## Artifact (historical)

| Metric | Final actual | Budget/cap | Result |
| --- | ---: | ---: | --- |
| Wasm bytes | 3,170,512 | 3,324,821 | Pass |
| Application JavaScript bytes | 339,533 | 341,401 | Pass |
| Total site bytes | 3,520,576 | 3,676,856 | Pass |
| Site files | 17 | exact allowlist of 17 | Pass |
| Raw Wasm exports | 24 | 24 | Pass |
| Named application exports | 9 | exact allowlist of 9 | Pass |

No artifact budget was changed.

## Campaign matrix (historical)

Historical promotions from this execution were:

| Result | Count |
| --- | ---: |
| PLAYABLE | 3 |
| RENDERS | 0 |
| LOADS | 0 |
| BLOCKED | 0 |
| REGRESSION | 0 |
| UNTESTED | 0 new rows |

Killhouse and CargoShip were recorded as PLAYABLE, and Blackout was promoted to
PLAYABLE by that Phase 3 evidence. The current foreground matrix above
supersedes those performance classifications. Other directly selected SP zones
remained grouped as UNTESTED until each received its own legal run.

## Remaining work (historical)

| Category | Remaining work |
| --- | --- |
| Current blocker | None after the Phase 3 Blackout campaign batch |
| Next map batch | Validate one or two representative campaign maps at a time; discovery is not compatibility |
| Phase 4 renderer/material/entity/FX | The observed Blackout static-model cardinality defect is fixed; further work requires a real-map failure |
| Phase 4 advanced audio | No current retail failure observed; advanced parity remains evidence-gated |
| Memory | Keep current eviction and per-pool cap; gather comparable evidence before another change |
| Phase 5 gamepad | Future product feature; eligible after the stable keyboard/mouse slice, but not yet implemented or retail-proven |
| Phase 6 cinematics | Evidence-gated future work; retain the tested visible omission until campaign progression requires legal browser playback |
| Phase 7 launcher/map UX | Future product feature now evidence-eligible after multiple playable maps; map selection and compatibility labels are not yet claimed complete |
