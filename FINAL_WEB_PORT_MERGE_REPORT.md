# Final Web-Port Merge Report

## Baseline

The focused merge-readiness pass started from a clean
`codex/fix-web-renderer-map-load` worktree. The ending implementation SHA below
is the fully tested code state immediately before the documentation-only commit
that adds this report; a commit cannot include its own SHA.

| Item | Exact value |
| --- | --- |
| Starting SHA | `475dd434da11e0c0d4605174cf80e9dd667acd31` |
| Ending implementation SHA | `3ac79a4b481d9906fb0fb4edc3766ab58b0ea2b2` |
| `origin/web-port` SHA | `1a9ad9981982ca0a173954fc784247092ff3348c` |
| Merge-base | `1a9ad9981982ca0a173954fc784247092ff3348c` |
| Emscripten | `6.0.6`; installed compiler commit `ce75e06884093bcefb86a6b8fd56a5d62a4cc245`; pinned emsdk commit `9981799f744be74ac67b1c1813ff172f63be0630` |
| CMake / Ninja | CMake `4.2.0-rc3`; Ninja `1.13.2` |
| Node / npm | Node `24.18.0`; npm `11.16.0` |
| Browser tooling | Playwright `1.61.1`; TypeScript `7.0.2`; ESLint `10.9.0` |
| Native compilers | Clang `24.0.0git` (`ff6d537...` LLVM revision), target `x86_64-pc-windows-msvc`; MSVC x86 `19.51.36256.0`, Visual Studio Community/MSBuild `18.9`, Windows SDK `10.0.28000` |

The pre-change matrix passed: Node 64/64; static syntax checks over 17 modules;
ESLint; strict `checkJs` over 4 modules; runtime `checkJs` over 7 modules;
MSVC portable 29/29; Wasm portable 29/29; Clang portable 21/21;
production Release build and runtime-prefix check; production Playwright 26/26;
diagnostic smoke 12/12; diagnostic remainder 34 passed with 2 expected
retail-data skips; and `git diff --check`. The starting production artifact was
within the approved boundary. No retail root was supplied or searched for.

## Issue disposition

| Exact-source audit issue | Disposition |
| --- | --- |
| A Worker can reacquire the writable home profile with a stale in-memory copy after another tab modifies durable state. | **CONFIRMED AND FIXED** |
| A latest-selection request submitted from the prior request's Promise continuation can be stranded. | **CONFIRMED AND FIXED** |
| The capability probe checks `SyncAccessHandle` shape without performing a real Worker OPFS synchronous-access operation. | **CONFIRMED AND FIXED** |

The fixes are intentionally narrow. They do not restore Gate 2, parallel retail
loaders, synthetic proof systems, generic production Wasm calls, or diagnostic
controls in the product build.

## Home tenure evidence

The production `createWorkerSyncFilesystem()` implementation is exercised
directly. After a successful durable flush and unmount it now closes mounted
handles and explicitly resets files, directories, directory-entry caches, byte
accounting, the cached OPFS directory handle, and `homeLoaded`. The next writer
tenure reloads durable OPFS state. A failed flush retains dirty memory,
ownership, queued persistence, and retry capability.

| Scenario | Result |
| --- | --- |
| A writes `A` -> B reads `A` and writes `B` -> A remounts | PASS: A reads durable value `B` |
| Cross-writer removal | PASS: A observes the file absent after B removes and flushes it |
| Cross-writer rename over existing | PASS: A observes B's durable replacement |
| Failed flush and retry | PASS: dirty state and ownership survive failure; retry persists it and only then resets the cache |
| Old-tenure persistence completion | PASS: a remount waits for the tracked flush finalizer, so old work cannot mutate a new tenure |
| Reload accounting | PASS: file bytes and directory accounting are rebuilt from durable state |

All 20 direct filesystem tests pass, including the prior FIFO mutation-ordering,
checkpoint-summary, flush/unmount, failure/retry, remove/recreate,
rename-over-existing, rapid replacement, blocked-queue, and fresh-restart
regressions plus the 10 new tenure cases.

## Mount-controller evidence

Drain startup is centralized. Every scheduling path publishes pending work and
calls the same starter; the current drain's finalizer clears its identity and
immediately starts any pending request. `busy` covers both active and pending
work, disposal settles queued requests, and the finalizer consumes unexpected
rejections.

| Scenario | Result |
| --- | --- |
| Sequential `await select(A); await select(B)` | PASS: B settles, mounts, and becomes current |
| Sequential `await select(A); await clear()` | PASS: clear settles and the runtime is unmounted |
| Request from A's Promise continuation | PASS: B cannot be stranded during A's final settlement |
| A fails, then B is selected | PASS: the later ready selection proceeds |
| Newer work during an active mount | PASS: B/clear supersedes A and older completion cannot publish over it |
| Disposal during settlement | PASS: pending work settles deterministically |
| Pending work after idle | PASS: none remains and `busy` is false |

The authoritative mount-controller suite passes 10/10 with deterministic
Promise controls and no timing sleeps.

## Capability evidence

The temporary module Worker now opens OPFS, creates a collision-resistant
`.kisak-capability-probe-<uuid>` file, obtains a real synchronous access handle,
calls `getSize()`, closes the handle, and removes the entry in `finally`. The
transferred `OffscreenCanvas` is received in the Worker and performs a WebGL2
clear operation. Worker creation and transfer are inside structured failure
handling, and the Worker is always terminated.

| Probe behavior | Result |
| --- | --- |
| Real OPFS Worker operation | PASS in the production Playwright browser |
| `SyncAccessHandle` open/use/close | PASS; injected open, operation, and close failures each return their specific structured code |
| Temporary entry cleanup | PASS on success and after operation failure; the real probe leaves the root entry list unchanged |
| Synchronous Worker construction failure | PASS: mapped to normal structured unsupported capability data |
| Malformed Worker response | PASS: rejected as a structured probe failure |
| Probe timeout | PASS with a deterministic fake timer |
| OffscreenCanvas transfer and Worker WebGL2 operation | PASS; transfer failure is separately mapped |
| Startup after a required probe failure | PASS: stops before engine start or asset import |

The capability suite passes 10/10. Web Locks and BroadcastChannel remain
required startup coordination gates.

## Test matrix

These are the exact final results after proving that the fetched
`origin/web-port` is already an ancestor of the branch.

| Gate | Final result |
| --- | --- |
| Node protocol/lifecycle/filesystem | 74 passed, 0 failed, 0 skipped |
| Direct worker filesystem | 20/20 passed (included in Node total) |
| Mount controller | 10/10 passed (included in production Playwright) |
| Capability probes | 10/10 passed (included in production Playwright) |
| `node --check` | 17 modules passed |
| ESLint | passed, 0 errors |
| Strict `checkJs` | 4 modules passed |
| Runtime/gradual `checkJs` | 7 modules passed |
| Fresh native Clang portable | 21/21 passed |
| Fresh native MSVC Win32 portable | 29/29 passed |
| Fresh Wasm portable | 29/29 passed |
| Fuzz smoke | 256/256 inputs passed; coverage counter 111 |
| Production Release build | passed |
| Production runtime-prefix validation | passed |
| Production Playwright | 40/40 passed, 0 failed, 0 skipped |
| Diagnostics Release build | passed |
| Diagnostic smoke | 12/12 passed |
| Diagnostic remainder | 34 passed, 0 failed, 2 expected retail-data skips |
| Production boundary | passed: allowlist, application exports, raw export cap, diagnostic exclusion, and byte budgets |
| `git diff --check` | passed, 0 errors |

The local Windows fuzz binary was built in Debug because the available bundled
libFuzzer runtime is iterator-debug compatible with DebugCRT; the final run also
loaded the installed Visual Studio AddressSanitizer, DebugCRT, and SDK UCRT
runtimes. The Linux CI job retains its existing RelWithDebInfo fuzz gate.

The only skipped diagnostics are the two existing tests explicitly gated on
`KISAK_COD4_RETAIL_ROOT`: retail database validation and local retail
validation. No retail root was provided, no machine search was performed, and
no retail compatibility claim is made. The production suite has no skips.

## Artifacts

No budget was rebaselined. The production Release build remains inside every
approved limit.

| Artifact boundary | Actual | Approved budget |
| --- | ---: | ---: |
| Wasm | 3,166,484 bytes | 3,324,821 bytes |
| Application JavaScript | 334,083 bytes | 341,401 bytes |
| Total site | 3,510,695 bytes | 3,676,856 bytes |
| Site files | 17 | exact allowlist passed |
| Raw Wasm exports | 24 | cap passed |
| Named application exports | 9 | exact allowlist passed |

The JavaScript increase is 8,940 bytes (2.75%) and the total-site increase is
8,928 bytes (0.25%) relative to the recorded starting artifact; Wasm decreased
by 12 bytes. Diagnostic sources remain excluded from production.

## Merge status

| Item | Result |
| --- | --- |
| Integrated `origin/web-port` SHA | `1a9ad9981982ca0a173954fc784247092ff3348c` |
| Integration method | Already contained: `git merge-base --is-ancestor origin/web-port HEAD` succeeds; no unnecessary merge commit was created |
| Conflicts | None |
| Post-integration tests | Complete matrix above passed |
| CI coverage | Existing `codex/**` push workflow already runs the new tests through the authoritative Node and product commands; no redundant CI-only commit was needed |
| Worktree | Clean after committing this report and running final repository hygiene checks |
| Safe to open PR | **YES** |

No generated `cod4browser-*.zip` is tracked. This pass added or modified no
proprietary files, retail data, Bink/Miles/Steam binaries, or game assets. The
known native-only upstream DLLs already present at the starting commit remain
untouched and excluded from the browser artifact.

## Remaining work

- **Merge blocker:** None.
- **Release blocker:** Validate the launcher, database/runtime, renderer, and
  gameplay with user-supplied legally owned retail data, beginning with real
  Killhouse and CargoShip. This pass cannot establish retail release readiness
  without that input.
- **Future product work:** Address concrete campaign incompatibilities and only
  then prioritize measured renderer/runtime gaps, cinematics, audio parity,
  gamepad support, multiplayer transport, and performance work. These are not
  blockers for merging this focused branch.

No further broad cleanup is recommended. The branch is ready to merge into `web-port`; subsequent work should be driven by real retail Killhouse/CargoShip validation and concrete campaign incompatibilities.
