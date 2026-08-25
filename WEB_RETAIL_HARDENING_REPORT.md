# Web retail hardening report

Date: 2026-08-25

## Baseline

| Field | Value |
| --- | --- |
| Branch | `codex/fix-web-renderer-map-load` |
| Starting SHA | `3243b86848e8816f7ce06a37f31be33d90002193` |
| Ending implementation SHA | `b6112cb72be1ef586371edf42e93a56bf10ade5c` |
| Configuration | Web Release production and Web Release diagnostics; portable tests in Release |
| Emscripten | 6.0.6, Clang 24.0.0 |
| CMake / Ninja | 4.2.0-rc3 / 1.13.2 |
| Native MSVC | Visual Studio Community 2026 18.9; MSVC 19.51 / toolset 14.51; Windows SDK 10.0.28000 |
| Node / npm | 24.18.0 / 11.16.0 |
| Playwright | 1.61.1; bundled Chromium 149.0.7827.55 |
| Branded browsers | Chrome 151.0.7922.174; Edge 151.0.4129.101 |
| TypeScript / ESLint | 7.0.2 / 10.9.0 |

The branch was clean and matched its remote when this pass began. The ending
implementation SHA above intentionally precedes this report commit, because a
commit cannot contain its own SHA.

Baseline production artifact at the starting SHA:

| Measure | Baseline |
| --- | ---: |
| Wasm | 3,180,913 bytes |
| Application JavaScript | 305,454 bytes |
| Total site | 3,496,495 bytes |
| Site files | 15 |
| Raw Wasm exports | 24 |
| Named application exports | 9 |

## Existing corrective features

The following requested foundations were **ALREADY SATISFIED** at the starting
SHA and remain intact:

- the shared production-safe input core and typed product input transport;
- the exclusive writer-lease state machine and Worker-generation protection;
- the retryable checkpoint protocol and honest persistent-storage status;
- production/diagnostics separation and diagnostic source denylisting;
- renderer-frontend-owned map unload without replacing canonical asset or
  world ownership;
- exact production Wasm/application export allowlists;
- ratcheted Wasm, JavaScript, and site-size budgets.

This pass added regression evidence and hardened the remaining platform edges;
it did not introduce a JavaScript game/world model, production diagnostic
protocol, multiplayer work, or proprietary fixtures.

## Retail availability

```text
KISAK_COD4_RETAIL_ROOT supplied: NO

No retail compatibility claims are made from this pass.
```

The host was not searched and no installation path was inferred. Both retail
tests skipped with their explicit `KISAK_COD4_RETAIL_ROOT` reason. Prior legal
local-run evidence remains labelled as historical in
[`docs/campaign-compatibility.md`](docs/campaign-compatibility.md); it is not a
substitute for current measurements.

## Killhouse

| Measure | Current pass |
| --- | --- |
| DB | SKIPPED — retail root unavailable |
| CGame | SKIPPED — retail root unavailable |
| First frame | SKIPPED — retail root unavailable |
| Stable | SKIPPED — 60-second retail window unavailable |
| Input | SKIPPED — no real gameplay verification |
| Audio | SKIPPED — no real gameplay verification |
| Checkpoint | SKIPPED — no retail profile mounted |
| Peak memory | NOT MEASURED |

## CargoShip

| Measure | Current pass |
| --- | --- |
| DB | SKIPPED — retail root unavailable |
| CGame | SKIPPED — retail root unavailable |
| First frame | SKIPPED — retail root unavailable |
| Stable | SKIPPED — 60-second retail window unavailable |
| Input | SKIPPED — no real gameplay verification |
| Audio | SKIPPED — no real gameplay verification |
| Checkpoint | SKIPPED — no retail profile mounted |
| Peak memory | NOT MEASURED |

The local-only validator now records DB, CGame, first-frame and 60-second
stability timings; frame average/p95/p99; heap and renderer resource classes;
audio decoded/queued state; input behavior; checkpoint/shutdown duration;
ordered unload/publication events; context recovery; and reload persistence for
both maps. It emits only a non-proprietary `KISAK_RETAIL_RESULT` JSON summary.

## Transition

| Measure | Current pass |
| --- | --- |
| Context generation before | SKIPPED on retail |
| Context generation after | SKIPPED on retail |
| Old-map decoded bytes before unload | NOT MEASURED on retail |
| Old-map decoded bytes after unload | NOT MEASURED on retail |
| Peak transition bytes | NOT MEASURED |
| First CargoShip frame | SKIPPED |

The diagnostic renderer now reports `worldUnloadBegin`, `worldUnloadEnd`,
`oldMapBytesReleased`, context generations before/after, and
`newWorldPublished`. Synthetic browser evidence proves retained recovery bytes
fall during world unload while the WebGL context generation remains unchanged.
These developer events and test calls remain excluded from production. The
existing renderer frontend remains the lifecycle owner; no parallel renderer
or asset lifecycle was added.

## Input

Real gameplay verification: **SKIPPED**. W/A/S/D movement, jump, Escape menu
behavior, camera rotation, MOUSE1 fire, MOUSE2 secondary action, weapon wheel,
and the same behaviors after Killhouse → CargoShip require retail execution.

DOM/product/Worker transport verification: **PASS**. Production tests cover
keyboard, mouse buttons 1–5, Back/Forward navigation suppression, pixel/line/
page wheel normalization, blur and visibility release, pointer-lock loss and
reacquisition, coalesced relative motion, resize, CSS scaling, and DPR 1, 1.25,
1.5, and 2. The retail validator requires visible canonical gameplay response;
it does not treat DOM delivery alone as success.

## Filesystem

| Requirement | Result |
| --- | --- |
| Two-tab exclusion | PASS — second real page cannot overlap the first writer |
| Normal handoff | PASS — confirmed flush/unmount precedes second-page mount |
| Timeout handoff | PASS — state becomes `UNKNOWN_AFTER_TIMEOUT`; lease remains held until termination |
| Terminate before release | PASS — observed ordering is termination started, terminated, lease released, second tab acquired |
| Stale generation | PASS — old Worker reply cannot alter the replacement session |
| Checkpoint debounce | PASS — 7.5-second quiet checkpoint, fake-clock tested |
| Maximum dirty age | PASS — 30-second bound despite continued writes |

Checkpoint execution is non-overlapping. Dirty activity during a checkpoint
causes exactly one follow-up. Failure retains dirty state and writer ownership,
and a later retry succeeds. A hidden document requests an immediate coalesced
checkpoint; disposal cancels pending work. Existing filesystem close/write
notifications mark the browser home dirty without changing canonical save
semantics.

## Watchdog

| Setting/evidence | Result |
| --- | --- |
| Stall timeout | 30 seconds without progress; test-configurable |
| Absolute maximum | 5 minutes; test-configurable |
| Progress reporting | operation id, generation, phase, file/byte counters, timestamp; at most 4 updates/second |
| Long healthy synthetic operation | PASS — progress beyond the former 60-second wall does not time out |
| Stalled synthetic operation | PASS — enters unknown state and terminates safely |
| Continuously progressing pathological operation | PASS — absolute watchdog terminates it |
| Late progress/reply | PASS — cannot revive a timed-out generation |

Worker termination is confirmed before the writer lease is released in all
unhealthy-operation paths.

## Renderer recovery

```text
800 MiB recovery budget: RETAINED
```

No current retail transition measurement exists, so lowering the limit would
be speculative. Historical documentation records approximately 766 MiB of
decoded recovery for Killhouse before the unload correction; that value was
not re-measured in this pass and cannot justify a new budget. The strengthened
local validator is ready to classify recovery, GPU, geometry, temporary upload,
shader/program, audio, and old-map retention bytes when a legal asset root is
provided. No recovery optimisation was accepted without those measurements.

## Browser support

| Browser | Status | Evidence |
| --- | --- | --- |
| Chrome | SUPPORTED | Branded Chrome production product matrix: 15/15 |
| Edge | SUPPORTED | Branded Edge production product matrix: 15/15 |
| Firefox | UNTESTED | Not declared supported by current policy; no compatibility guess made |
| Safari | UNTESTED | Not declared supported by current policy; no compatibility guess made |

Startup uses feature detection, not user-agent sniffing, before engine Worker
creation or asset-store access. Required checks cover Wasm, WebGL2, Worker,
transferable OffscreenCanvas, IndexedDB, OPFS, Worker `SyncAccessHandle`, Web
Audio, and pointer lock. Persistent storage is optional and reported honestly.
A missing requirement produces a structured report and explicit user-facing
failure before a multi-gigabyte import can begin.

## Campaign matrix

The current ledger summary is:

```text
validated:    1 historical PLAYABLE row (Killhouse; not rerun in this pass)
renders only: 1 historical RENDERS row (CargoShip; not rerun in this pass)
blocked:      0
untested:     all other candidate single-player zones
```

Discovery is not compatibility. No map was promoted during this pass. See
[`docs/campaign-compatibility.md`](docs/campaign-compatibility.md) for the full
required evidence columns and result definitions.

## Test matrix

| Gate | Exact result |
| --- | --- |
| Production Release build | PASS, 1 build |
| Diagnostics Release build | PASS, 1 build |
| Production boundary / exact exports / size | PASS; 17 files, 24 raw Wasm exports, 9 named application exports |
| Production Playwright | PASS, 15/15 in bundled Chromium; 15/15 Chrome; 15/15 Edge |
| Diagnostic smoke | PASS, 18/18 |
| Diagnostic remainder | PASS, 48 passed; 2 explicitly skipped retail tests |
| Browser multi-tab | PASS, 4/4 product scenarios |
| DPR/resize input | PASS, 5/5 product scenarios |
| Node lifecycle/protocol/checkpoint/watchdog | PASS, 31/31 |
| Wasm portable | PASS, 37/37 |
| Native Clang portable | PASS, 29/29, fresh Clang 24 x64 Release build |
| Native MSVC portable | PASS, 29/29, fresh x64 Release build |
| ESLint | PASS |
| Strict checkJs | PASS |
| Gradual/runtime checkJs | PASS |
| Retail tests | SKIPPED, 2/2 with explicit missing-root reasons |
| `git diff --check` | PASS |

There were no unexpected skips. The diagnostic remainder command now excludes
`@product` as well as smoke and native-authoritative duplicates, so the routine
tiers are non-overlapping. Native Clang emitted legacy vendored-zlib prototype
and CRT deprecation warnings but compiled and passed all tests.

## Artifact size

| Artifact | Current | Budget | Difference to budget | Change from baseline |
| --- | ---: | ---: | ---: | ---: |
| Wasm | 3,180,981 | 3,340,060 | -159,079 (-4.76%) | +68 (+0.0021%) |
| Application JavaScript | 320,547 | 320,727 | -180 (-0.06%) | +15,093 (+4.9412%) |
| Total site | 3,511,656 | 3,671,421 | -159,765 (-4.35%) | +15,161 (+0.434%) |

The production artifact has 17 files. It contains no diagnostic lifecycle
events, diagnostic browser sources, arbitrary Wasm call surface, or retail
data.

## Atomic commits

```text
4b6916ff fix(web-fs): harden durable profile ownership
1b864226 test(web-input): cover scaling and auxiliary controls
8629d6e6 feat(web): gate unsupported browser capabilities
87e6ea0e refactor(web): keep capability gate within budget
12b130ae test(web-renderer): measure map resource retirement
637f3af5 test(web-retail): strengthen multimap validation evidence
f64862eb docs(web): record campaign validation evidence
b6112cb7 test(web): keep browser tiers disjoint
```

## Remaining blockers

- **P0 — current real retail proof:** supply an explicit legal
  `KISAK_COD4_RETAIL_ROOT` and run Killhouse → CargoShip through the local
  validator. Until DB/CGame/real frames, 60-second stability, visible gameplay
  input, audio, persistence, unload telemetry, context recovery, and transition
  memory all pass, the release-blocking retail criteria remain unvalidated.
- **P1 — evidence-driven renderer memory/campaign work:** use that run to
  classify the 800 MiB recovery pool and validate the next campaign map in a
  small batch. Make one resource/material fix at the earliest divergent
  portable boundary only if a real reproduction identifies it.
- **P2 — product breadth:** validate Firefox/Safari only when their required
  storage/Worker primitives satisfy the gate; continue the normal-user launcher
  flow and map-status UI; retain the advanced console behind a developer path.

## Reproduction commands

```powershell
npm.cmd ci
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build_web.ps1 -Configuration Release
npm.cmd run check:web:product
npm.cmd run test:browser:product

powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build_web.ps1 -Configuration Release -Diagnostics
$env:KISAK_WEB_SITE = 'build/web-diagnostics/site-diagnostics'
npm.cmd run test:browser
npm.cmd run test:browser:remainder

npm.cmd run test:protocol
npm.cmd run check:web:static

.\tools\validate_web_retail.ps1 -RetailRoot 'D:\LegallyOwned\Call of Duty 4'
```

Portable CTest builds used `KISAK_PORTABLE_TESTS_ONLY=ON`: Emscripten Release in
`build/retail-hardening-wasm`, MSVC x64 Release in
`build/retail-hardening-msvc`, and a fresh native Clang 24 Release build in
`build/retail-hardening-clang-final`.
