# Web retail roadmap progress

## 2026-08-27 six-map profiling and one renderer optimization

An explicit legally owned retail root was supplied. Work was integrated on
`web-port` and pushed to `origin/web-port`; no extra roadmap branch remains.

CargoShip -> Bog A initially failed in canonical XAnim because Wasm uses a
16-byte `long double` where several native decompilation patterns assumed an
8-byte value. Commits `e42b48f3` through `721a981b` restore canonical owner
shutdown/reset and direct `double` math without changing game ownership. The
final CargoShip -> Bog A -> Killhouse run passed 60-second stability, input,
audio, checkpoint, context recovery, transition, and shutdown/reload. Temporary
diagnostic commits were fully reverted.

The clean pre-change profiles ranked combined shadow draws first at an 11.325
ms six-map mean. Commit `9e75a9dd` is the sole renderer optimization: opaque
shadow casters no longer bind unused textures or rewrite sampler state. The
post-change six-map mean is 7.144 ms (-36.91%), texture binds are down 32.49%,
and backend CPU is down 6.56%. All six maps passed current headed Chrome
validators. Killhouse and Airplane meet the reference `PLAYABLE` threshold;
CargoShip, Blackout, Hunted, and Bog A remain `FUNCTIONAL`. Evidence:
[retail-profile-9e75a9dd.json](docs/evidence/retail-profile-9e75a9dd.json).

| Gate | Current result |
| --- | --- |
| Retail root supplied | YES; path excluded from repository evidence |
| Release diagnostics retail baseline | Pass: Killhouse/CargoShip/Blackout |
| Release diagnostics campaign validators | Pass: Airplane/Hunted/Bog A |
| Production Release build | Pass |
| Browser smoke | 12/12 pass |
| Browser remainder | 36 pass, 2 expected retail-only skips |
| Production boundary | 3,173,476 B Wasm; 340,615 B JS; 3,524,622 B site; 17 files; 24 raw/9 named exports; pass |

The representative objective/AI/combat/death/game-save reload gate is not yet
automated and was not claimed. Existing campaign validators verify configuration
checkpoints and shutdown/config reload only. The next truthful evidence step is
a narrow diagnostics-only canonical-state probe and one opt-in headed mission
validator, followed by the next measured renderer fix. Current classification:
`PERFORMANCE BOTTLENECK IDENTIFIED — NEXT FIX REQUIRED`.

## Phase 0 baseline

| Field | Value |
| --- | --- |
| Starting SHA | `10000c094a940d7a1e7807df19dd3007e5ce6c8c` |
| Validated runtime SHA | `10000c094a940d7a1e7807df19dd3007e5ce6c8c` |
| `origin/web-port` SHA | `10000c094a940d7a1e7807df19dd3007e5ce6c8c` |
| Merge base | `10000c094a940d7a1e7807df19dd3007e5ce6c8c` |
| Cleaned foundation | `6a68f3c3972f4fc45a611a60e6e58352f21240a0`, contained in `origin/web-port` |
| Roadmap branch | `codex/web-retail-roadmap` |
| Date | 2026-08-26 |
| Host | Microsoft Windows 11 Pro 10.0.26200, build 26200 |
| Build configuration | Release except the Windows sanitizer fuzz target, which used Debug to match the bundled libFuzzer runtime |

Phase 0 completed on the validated runtime SHA. The values in this section are
the historical pre-retail baseline; later phases changed runtime sources and
are recorded separately below.

## Toolchain

| Component | Version |
| --- | --- |
| Emscripten | 6.0.6 (`ce75e06884093bcefb86a6b8fd56a5d62a4cc245`) |
| emsdk | `9981799f744be74ac67b1c1813ff172f63be0630` |
| CMake | 4.2.0-rc3 |
| Ninja | 1.13.2 |
| Node.js | 24.18.0 |
| npm | 11.16.0 |
| Playwright | 1.61.1 |
| Playwright Chromium used for browser tests | 149.0.7827.55 |
| Installed Google Chrome | 151.0.7922.174; inventoried, not used for this matrix |
| Installed Microsoft Edge | 151.0.4129.107; inventoried, not used for this matrix |
| TypeScript | 7.0.2 |
| ESLint | 10.9.0 |
| Native Clang | 24.0.0git (`ff6d537b14d737719d6377789784d04ff9565f65`), x86_64 Windows MSVC target |
| Native MSVC | 19.51.36256, x86 |
| Visual Studio / MSBuild | Visual Studio 2026 18.9.0 / MSBuild 18.9.1 |
| Windows SDK | 10.0.28000.0 |

## Phase 0 production artifact (historical)

The fresh Release build is authoritative. All values passed the checked-in
production boundary and its approved 5% budgets.

| Metric | Cleaned-foundation reference | Fresh actual | Budget/cap |
| --- | ---: | ---: | ---: |
| Wasm bytes | 3,166,484 | 3,166,484 | 3,324,821 |
| Application JavaScript bytes | 334,083 | 339,255 | 341,401 |
| Total site bytes | 3,510,695 | 3,516,270 | 3,676,856 |
| Site files | 17 | 17 | exact allowlist of 17 |
| Raw Wasm exports | 24 | 24 | 24 |
| Named application exports | 9 | 9 | exact allowlist of 9 |

The JavaScript and site totals changed after the fresh relink but remain inside
the reviewed budgets. No budget was increased.

## Phase 0 validation matrix (historical)

| Gate | Result |
| --- | --- |
| `npm.cmd ci` | Pass; 75 packages audited, 0 vulnerabilities |
| Node syntax (`node --check`) | Pass; all 18 listed web modules |
| ESLint | Pass |
| Strict `checkJs` | Pass |
| Runtime/gradual `checkJs` | Pass |
| Node protocol/lifecycle/filesystem | Pass; 74/74, no skips |
| Native Clang portable | Pass; 21/21, no skips |
| Native MSVC x86 portable | Pass; 29/29, no skips |
| Direct Wasm portable | Pass; 29/29, no skips |
| Sanitized parser fuzz smoke | Pass; 256 runs with libFuzzer, AddressSanitizer, and UndefinedBehaviorSanitizer |
| Production Release Web build | Pass, including canonical runtime-prefix check |
| Production boundary | Pass; exact files/exports and all size budgets |
| Production Playwright | Pass; 40/40, no skips |
| Diagnostics Release Web build | Pass, including canonical runtime-prefix check |
| Diagnostic smoke | Pass; 12/12, no skips |
| Diagnostic remainder | Pass; 34 passed, 2 expected `RETAIL_ROOT_MISSING` skips |
| `git diff --check` | Pass after report creation |

The first local diagnostic smoke attempt was invalid because Playwright reused
an unrelated server already listening on the default port and received the
production site. The authoritative rerun explicitly served the diagnostic site
on port 8020 and passed 12/12. The remainder used port 8021.

The first Windows `RelWithDebInfo` fuzz link attempt exposed a bundled
libFuzzer/MSVC iterator-debug-level mismatch before executing project code. The
same sanitizer target built and completed 256 runs in Debug, matching the
bundled Windows libFuzzer runtime. No source change was made.

## Phase 1 retail validation

An explicit, legally owned retail root was supplied. The repository validator
completed 1/1 in 4.6 minutes against a clean source tree at
`ac063bb20cbc4027497841322d87c2069d736939`, using the Release diagnostics
build on Playwright Chromium 149.0.7827.55 and Windows 11 x64. The structured
record is [retail-phase1-ac063bb2.json](docs/evidence/retail-phase1-ac063bb2.json).
It contains no retail paths or proprietary content.

Both maps passed canonical DB completion, cgame initialization, a real world
frame, at least 60 seconds of sustained rendering, keyboard/mouse input,
audio, a persisted configuration checkpoint, and the no-fatal-error check.

| Metric | Killhouse | CargoShip |
| --- | ---: | ---: |
| Result | PLAYABLE | PLAYABLE |
| Map command to first world frame | 6,797.800 ms | 10,838.495 ms |
| Stability window | 60,025.385 ms / 94 frames | 60,091.810 ms / 67 frames |
| Average / p95 / p99 frame time | 643.155 / 683.800 / 737.735 ms | 897.914 / 1,235.010 / 1,416.035 ms |
| Wasm linear-memory capacity at stability end | 2,013,724,672 B | 2,013,724,672 B |
| Decoded texture recovery | 1,426,285,608 B | 679,402,508 B |
| Aggregate CPU recovery | 1,517,951,436 B | 753,839,840 B |
| Estimated GPU textures | 1,447,485,000 B | 700,601,900 B |
| Geometry | 91,665,828 B | 74,437,332 B |
| Temporary uploads / shader programs | 0 B / 0 B | 0 B / 0 B |
| Audio decoded / queued buffers | 5,881,060 B / 15 | 1,582,592 B / 12 |
| Configuration checkpoint | 2 files / 3,154,912 B / 238.69 ms | 7 files / 7,091,210 B / 408.28 ms |

The Killhouse-to-CargoShip transition passed. Before unload, the old map held
1,430,575,400 B of decoded texture recovery and 1,521,922,580 B of aggregate
CPU recovery; both fell to zero before the new world was published. Peak
aggregate CPU recovery was therefore the old-map value, not an old/new overlap.
The WebGL context generation remained 1. Forced context recovery passed in
1,779.19 ms, with resource generation advancing 4 to 5 and both frames and
input resuming. Shutdown flushed in 228.14 ms and the persisted profile and
configuration reloaded successfully.

The failures exposed while reaching this clean run were fixed at their
canonical boundaries: leading-comma asset stubs now resolve through the normal
DB path (`1826e037`), Wasm earthquake duration conversion no longer type-puns
an 8-byte double through a 16-byte `long double` (`5c3b0fce`), and canonical
common runtime command registration and configuration writing resumed
(`a6d09fe1`). The validator and telemetry evidence were hardened in
`ac063bb2`. Focused native/Wasm checks passed for each affected boundary.

Phase 1 exit criteria passed. Proceed to Phase 2.

## Phase 1 production artifact (historical)

The production Release artifact was rebuilt after the Phase 1 fixes and still
passes the unchanged checked-in boundary.

| Metric | Current actual | Budget/cap |
| --- | ---: | ---: |
| Wasm bytes | 3,168,351 | 3,324,821 |
| Application JavaScript bytes | 339,533 | 341,401 |
| Total site bytes | 3,518,415 | 3,676,856 |
| Site files | 17 | exact allowlist of 17 |
| Raw Wasm exports | 24 | 24 |
| Named application exports | 9 | exact allowlist of 9 |

No artifact budget or export allowlist changed.

## Phase 2 memory decision

Keep the existing previous-map recovery eviction and make no new memory
optimization. It retired 1,521,922,580 B of old-map aggregate CPU recovery to
zero before CargoShip publication; the observed peak equalled the old-map
value, so there was no old/new recovery overlap. The unchanged 2,013,724,672 B
Wasm linear-memory capacity is monotonic allocator capacity, not evidence that
evicted recovery objects remained live.

Keep 800 MiB (838,860,800 B) as the **per retained-image-pool admission cap**,
not an aggregate decoded-recovery ceiling. Killhouse's static-model image pool
used 817,908,800 B, or 97.50% of the cap, leaving only 20,952,000 B
(approximately 19.98 MiB); the evidence does not support lowering it.

The measured ownership classification is:

| Resource | Classification |
| --- | --- |
| Decoded world/static/dynamic/UI/supplemental textures | Regenerable, reloadable from user storage, map-local |
| Retained geometry and portable draw commands | Regenerable, map-local |
| GPU map textures and buffers | Regenerable, map-local |
| Render targets and fixed pipeline programs | Regenerable, intentionally global |
| Shader source/cache | Regenerable, intentionally global; 0 B measured here |
| Temporary uploads | Regenerable, map-local; 0 B measured here |
| Audio data | Regenerable, reloadable, predominantly map-local |
| Wasm linear-memory capacity | Allocator capacity, outside the retained-resource classification |

No irreplaceable retained resource was observed. Cross-pool/content duplicate
bytes were not measured. The current data establishes a baseline only; no
unobserved before/after (A/B) timing or memory result was invented.

Phase 2 exit decision: retain the measured eviction and proceed to the next
one-map campaign batch without a speculative memory change.

## Phase 3 Blackout campaign batch

| Field | Value |
| --- | --- |
| Starting Phase 3 validator SHA | `8b2dc65c476a48cf9ae668ba8c6d3c35202b9be3` |
| Validated ending runtime SHA | `6be926cb4e78693f9f6e638c348b0ee0f908b45f` |
| Runtime-fix SHA | `164fc1f27b3f3536cf190df94e3313a8ed810ff3` |
| Date | 2026-08-26 |
| Retail root supplied | YES |
| Build configuration | Release diagnostics |
| Browser / host | Playwright Chromium 149.0.7827.55 / Windows 11 x64 |

An explicit, legally owned retail root was supplied. The clean Blackout
validator completed 1/1 in 2.4 minutes at source SHA
`6be926cb4e78693f9f6e638c348b0ee0f908b45f`, using the Release diagnostics
build on Playwright Chromium 149.0.7827.55 and Windows 11 x64. The structured
record is
[retail-phase3-6be926cb.json](docs/evidence/retail-phase3-6be926cb.json); it
contains no retail paths or proprietary content.

The validator established a fresh CargoShip baseline, transitioned in-process
to Blackout, exercised the full canonical ClipMap/server/game/client/cgame
lifecycle, rendered for at least 60 seconds, verified keyboard/mouse input and
audio, checkpointed configuration, recovered a forced WebGL context loss,
transitioned out to Killhouse, and completed shutdown/reload persistence.

| Metric | Blackout |
| --- | ---: |
| Result | PLAYABLE |
| Map command to first world frame | 11,144.985 ms |
| Stability window | 60,022.685 ms / 73 frames |
| Average / p95 / p99 frame time | 823.765 / 932.975 / 967.930 ms |
| Wasm linear-memory capacity before load / after cgame / after world / stability end | 1,058,537,472 / 1,675,165,696 / 1,675,165,696 / 1,776,091,136 B |
| Decoded texture recovery | 1,233,813,448 B |
| Aggregate CPU recovery | 1,300,761,988 B |
| Estimated GPU textures | 1,255,012,840 B |
| Geometry | 66,948,540 B |
| Temporary uploads / shader programs | 0 B / 0 B |
| Audio decoded / queued buffers | 3,678,608 B / 15 |
| Configuration checkpoint | 7 files / 7,687,388 B / 974.925 ms |

The CargoShip-to-Blackout transition released all 742,024,672 B of old-map
aggregate CPU recovery before Blackout publication and kept WebGL context
generation 1. Its observed transition peak was 1,185,984,324 B aggregate CPU
recovery, 1,120,643,784 B decoded texture recovery, 1,141,843,176 B estimated
GPU textures, and 71,863,764 B geometry. Blackout context recovery restored a
real world frame in 2,149.595 ms, advanced renderer resource generation from 4
to 5, and resumed input. The Blackout-to-Killhouse transition released all
1,315,887,176 B of old-map aggregate CPU recovery before Killhouse publication;
its peak was 1,334,750,040 B. Shutdown flushed in 887.360 ms, and the persisted
profile and configuration reloaded successfully.

### Phase 3 diagnostic and fix chain

The initial clean Blackout run completed canonical DB and cgame startup but did
not produce a world frame. The earliest failing boundary was portable world
command validation, not the launcher or canonical game systems.

| Commit | Role and evidence |
| --- | --- |
| `1305f4b8` | Published the existing world-submission result before the canonical renderer drop, exposing the first portable-boundary failure. |
| `5a0225a8` | Made the retail validator fail immediately when that submission error appeared instead of waiting for the five-minute frame timeout. |
| `c810b995` | Classified invalid world-descriptor branches and identified the rejected canonical spot-shadow static-model instance at the browser-only 20,000-instance limit. |
| `164fc1f2` | Raised the portable static-model cardinality to native IW3's bounded 65,536 instances and added focused native/direct-Wasm coverage for the maximum accepted instance set. |
| `6be926cb` | Made the retail validator await the canonical filesystem mount after asset readiness, removing a reload-only test race before persisted configuration execution. |

The final clean Blackout run passed after the canonical cardinality fix and
validator mount synchronization. No map-specific behavior, proprietary
fixture, synthetic world, weakened assertion, or production artifact-budget
change was introduced. The Phase 3 selected batch is PLAYABLE, satisfying the
campaign-batch exit condition.

## Final regression matrix

The final source state passed the complete applicable non-retail matrix and
both authoritative retail validators.

| Gate | Final result |
| --- | --- |
| Node syntax | Pass; all 18 listed web modules plus the retail validator |
| ESLint | Pass; repository static set plus the retail validator |
| Strict `checkJs` | Pass |
| Runtime/gradual `checkJs` | Pass |
| Node protocol/lifecycle/filesystem | Pass; 74/74 |
| Native Clang portable | Pass; 21/21 |
| Native MSVC x86 portable | Pass; 29/29 |
| Direct Wasm portable | Pass; 29/29 |
| Sanitized parser fuzz smoke | Pass; 256 runs |
| Production Playwright | Pass; 40/40 |
| Diagnostic smoke | Pass; 12/12 |
| Diagnostic remainder | Pass; 35 passed and exactly 2 expected `RETAIL_ROOT_MISSING` skips |
| Authoritative Phase 1 Killhouse/CargoShip validator | Pass; 1/1 in 4.8 minutes |
| Authoritative Phase 3 Blackout validator | Pass; 1/1 in 2.4 minutes |
| Production Release build and boundary | Pass |
| Diagnostics Release build | Pass |
| `git diff --check` | Pass before report handoff |

## Final production artifact

| Metric | Final actual | Budget/cap |
| --- | ---: | ---: |
| Wasm bytes | 3,170,512 | 3,324,821 |
| Application JavaScript bytes | 339,533 | 341,401 |
| Total site bytes | 3,520,576 | 3,676,856 |
| Site files | 17 | exact allowlist of 17 |
| Raw Wasm exports | 24 | 24 |
| Named application exports | 9 | exact allowlist of 9 |

All final artifact values remain inside the unchanged reviewed boundary.

## Later-phase decision

| Phase | Classification after the Blackout pass |
| --- | --- |
| Phase 4: renderer/material/entity/FX/audio | No further observed retail defect. The one observed Blackout renderer incompatibility was fixed at its portable boundary; additional work remains evidence-gated to a real map or gameplay event. |
| Phase 5: gamepad | Future product feature. The stable keyboard/mouse campaign slice makes it eligible as later work, but support and retail gameplay evidence do not yet exist. |
| Phase 6: browser cinematics | Explicitly deferred until campaign progression reaches a cinematic boundary that requires legal browser playback. Keep the tested visible omission. |
| Phase 7: launcher/map UX | Future product feature now evidence-eligible after multiple playable maps. Existing installation/storage foundations remain; normal-user map selection and compatibility labels are not claimed complete. |

No current roadmap blocker remains. The next execution should continue from
current evidence, not add speculative renderer, audio, cinematic, or launcher
systems.

## Final recommendation

`READY FOR NEXT ROADMAP PHASE`

## 2026-08-27 bounded frame-profiler pass

This section supersedes the historical recommendation immediately above.

| Field | Value |
| --- | --- |
| Starting SHA | `d3f4c8d696e21939e7ef85cc19eb331ef104c30b` |
| Implementation SHAs | `91788492`, `aff008c3` |
| Branch | `codex/web-frame-profile-mission` |
| Retail root supplied | NO; `KISAK_COD4_RETAIL_ROOT` absent |
| Build | Release production and Release diagnostics |

The diagnostics artifact now has bounded CPU/renderer stage profiling,
separate sun/spot shadow preparation and draw timings, draw/upload counters
that flag unmeasured texture formats, upload CPU duration, and non-blocking
asynchronous GPU timers. The retail
validator records those values in schema version 3, but no retail result was
generated in this process. Production remains at the exact approved artifact
baseline and rejects profiler symbols/events.

Validation passed: static checks, 76 Node tests, diagnostics build, 12 smoke
tests, 36 serialized remainder tests with exactly two expected missing-retail
skips, the focused 2-test profiler suite, 40 production browser tests, the
production boundary, and `git diff --check`. The earlier same-pass native,
direct-Wasm, and 256-run fuzz baselines also passed before diagnostics-only
instrumentation was added.

No renderer optimisation was selected and no mission checkpoint/death/restart
or save-reload claim was made. Those steps remain evidence-gated on the
explicit retail input.

Classification: `RETAIL ROOT NOT AVAILABLE`
