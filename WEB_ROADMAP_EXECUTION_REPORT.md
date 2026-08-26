# Web roadmap execution report

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

## Phase 1 retail execution

`KISAK_COD4_RETAIL_ROOT supplied: YES`

An explicit, legally owned retail root was supplied to the repository
validator. The clean run completed 1/1 in 4.6 minutes at source SHA
`ac063bb20cbc4027497841322d87c2069d736939` with no dirty-tree inputs. It used
the Release diagnostics build, Playwright Chromium 149.0.7827.55, and Windows
11 x64. The machine-readable record is
[retail-phase1-ac063bb2.json](docs/evidence/retail-phase1-ac063bb2.json); it
contains no retail paths or proprietary content.

## Current map results

### Killhouse

| Evidence field | Current result |
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

| Evidence field | Current result |
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

## Fixes

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

## Memory and Phase 2 decision

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

## Artifact

| Metric | Fresh actual | Budget/cap | Result |
| --- | ---: | ---: | --- |
| Wasm bytes | 3,168,351 | 3,324,821 | Pass |
| Application JavaScript bytes | 339,533 | 341,401 | Pass |
| Total site bytes | 3,518,415 | 3,676,856 | Pass |
| Site files | 17 | exact allowlist of 17 | Pass |
| Raw Wasm exports | 24 | 24 | Pass |
| Named application exports | 9 | exact allowlist of 9 | Pass |

No artifact budget was changed.

## Campaign matrix

Current promotions from this execution:

| Result | Count |
| --- | ---: |
| PLAYABLE | 2 |
| RENDERS | 0 |
| LOADS | 0 |
| BLOCKED | 0 |
| REGRESSION | 0 |
| UNTESTED | 0 new rows |

Killhouse remains PLAYABLE with current stronger evidence. CargoShip is
promoted from historical RENDERS to current PLAYABLE. Other directly selected
SP zones remain grouped as UNTESTED until each receives its own legal run.

## Remaining work

| Category | Remaining work |
| --- | --- |
| Current blocker | None in the Phase 1 two-map boundary |
| Next map batch | Validate one representative campaign map at a time; discovery is not compatibility |
| Renderer/material gap | No current retail failure observed; do not speculate |
| Audio gap | No current retail failure observed; do not speculate |
| Memory | Keep current eviction and per-pool cap; gather comparable evidence before another change |
| Future product feature | Proceed incrementally from the measured two-map baseline |

## Final recommendation

`READY FOR NEXT ROADMAP PHASE`
