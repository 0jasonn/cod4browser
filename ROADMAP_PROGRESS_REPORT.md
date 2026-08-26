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

Phase 0 completed on the validated runtime SHA. The reports added afterward do
not modify runtime or build inputs.

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

## Production artifact

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

## Validation matrix

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

## Phase 1 retail availability

`KISAK_COD4_RETAIL_ROOT supplied: NO`

No current retail compatibility claim is made.

| Field | Value |
| --- | --- |
| Map result | No current run; historical compatibility rows unchanged |
| Failure class | N/A; runtime was not entered |
| First failing boundary | N/A |
| Fix implemented | None |
| Memory before/after | Not measured in this execution |
| Artifact before/after | Recorded above; no runtime source change |
| Remaining blocker | `RETAIL_ROOT_MISSING` |

Phase 1 is blocked by missing user-supplied input. The validator was not run,
the machine was not searched for an installation, and no speculative runtime
change was made. Phases 2 through 7 were not started.
