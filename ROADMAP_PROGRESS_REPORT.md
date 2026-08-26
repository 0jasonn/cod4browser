# Web retail roadmap progress

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

## Current production artifact

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
