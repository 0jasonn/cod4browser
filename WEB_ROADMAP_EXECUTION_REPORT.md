# Web roadmap execution report

## Baseline

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

## Retail availability

`KISAK_COD4_RETAIL_ROOT supplied: NO`

No current retail compatibility claim is made.

The repository validator requires an explicitly supplied legal retail root.
No machine search, inferred installation path, download, or historical result
was substituted for current execution.

## Current map results

### Killhouse

| Evidence field | Current result |
| --- | --- |
| DB | Not run — `RETAIL_ROOT_MISSING` |
| Clip/world | Not run — `RETAIL_ROOT_MISSING` |
| Server | Not run — `RETAIL_ROOT_MISSING` |
| Game | Not run — `RETAIL_ROOT_MISSING` |
| CGame | Not run — `RETAIL_ROOT_MISSING` |
| First frame | Not run — `RETAIL_ROOT_MISSING` |
| 60s stable | Not run — `RETAIL_ROOT_MISSING` |
| Input | Not run — `RETAIL_ROOT_MISSING` |
| Audio | Not run — `RETAIL_ROOT_MISSING` |
| Transition in | Not run — `RETAIL_ROOT_MISSING` |
| Transition out | Not run — `RETAIL_ROOT_MISSING` |
| Save/load | Not run — `RETAIL_ROOT_MISSING` |
| Context recovery | Not run — `RETAIL_ROOT_MISSING` |
| Peak memory | Not measured — `RETAIL_ROOT_MISSING` |
| Result | No current result; historical ledger unchanged |
| Failure class | N/A |

### CargoShip

| Evidence field | Current result |
| --- | --- |
| DB | Not run — `RETAIL_ROOT_MISSING` |
| Clip/world | Not run — `RETAIL_ROOT_MISSING` |
| Server | Not run — `RETAIL_ROOT_MISSING` |
| Game | Not run — `RETAIL_ROOT_MISSING` |
| CGame | Not run — `RETAIL_ROOT_MISSING` |
| First frame | Not run — `RETAIL_ROOT_MISSING` |
| 60s stable | Not run — `RETAIL_ROOT_MISSING` |
| Input | Not run — `RETAIL_ROOT_MISSING` |
| Audio | Not run — `RETAIL_ROOT_MISSING` |
| Transition in | Not run — `RETAIL_ROOT_MISSING` |
| Transition out | Not run — `RETAIL_ROOT_MISSING` |
| Save/load | Not run — `RETAIL_ROOT_MISSING` |
| Context recovery | Not run — `RETAIL_ROOT_MISSING` |
| Peak memory | Not measured — `RETAIL_ROOT_MISSING` |
| Result | No current result; historical ledger unchanged |
| Failure class | N/A |

## Fixes

No runtime failure was reproduced and no runtime fix was implemented. The
diagnostic browser suite was rerun on isolated ports after the first attempt
reused a pre-existing production server on port 8000. The Windows fuzz smoke
used Debug to match the iterator-debug level of the bundled libFuzzer runtime.
Neither adjustment changed repository source.

Killhouse regression, CargoShip regression, and the retail full matrix were
not run because `KISAK_COD4_RETAIL_ROOT` was absent.

## Memory

The following values were not measured in this execution because no retail map
was loaded:

| Metric | Result |
| --- | --- |
| Wasm heap | Not measured |
| Decoded recovery | Not measured |
| GPU estimate | Not measured |
| Geometry | Not measured |
| Temporary upload | Not measured |
| Program/shader | Not measured |
| Audio | Not measured |
| Transition peak | Not measured |
| Bytes released at unload | Not measured |

Historical values were not reused as current evidence.

## Artifact

| Metric | Fresh actual | Budget/cap | Result |
| --- | ---: | ---: | --- |
| Wasm bytes | 3,166,484 | 3,324,821 | Pass |
| Application JavaScript bytes | 339,255 | 341,401 | Pass |
| Total site bytes | 3,516,270 | 3,676,856 | Pass |
| Site files | 17 | exact allowlist of 17 | Pass |
| Raw Wasm exports | 24 | 24 | Pass |
| Named application exports | 9 | exact allowlist of 9 | Pass |

No artifact budget was changed.

## Campaign matrix

Current promotions from this execution:

| Result | Count |
| --- | ---: |
| PLAYABLE | 0 |
| RENDERS | 0 |
| LOADS | 0 |
| BLOCKED | 0 |
| REGRESSION | 0 |
| UNTESTED | 0 new rows |

The separate historical ledger remains unchanged: Killhouse is recorded as
historical PLAYABLE evidence, CargoShip as historical RENDERS evidence, and
other directly selected SP zones as a grouped historical UNTESTED entry. None
was promoted or demoted by this execution.

## Remaining work

| Category | Remaining work |
| --- | --- |
| Current blocker | Supply `KISAK_COD4_RETAIL_ROOT` explicitly and rerun the canonical local retail validator |
| Next map batch | None selected until Killhouse and CargoShip satisfy the current Phase 1 matrix |
| Renderer/material gap | No current retail failure observed; do not speculate |
| Audio gap | No current retail failure observed; do not speculate |
| Future product feature | Phases 2 through 7 remain gated on current Killhouse-to-CargoShip evidence |

## Final recommendation

`BLOCKED BY MISSING RETAIL ROOT`
