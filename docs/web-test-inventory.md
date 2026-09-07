# Web test inventory

The build inventories and CI workflow own test registration. Use the
[README](../README.md#browser-validation) for setup and routine commands.

| Tier | Ownership |
| --- | --- |
| Native and direct Wasm | Parser bounds, serialization, math, ABI, database streams/pools/publication, aliases, rollback, canonical runtime contracts and portable renderer policy. `tests/native/CMakeLists.txt` owns registration. |
| Node (`test:protocol`) | Protocol validation, input conversion, Worker transport, import/filesystem lifecycle, checkpoint ownership, audio feedback and renderer workload comparisons. |
| Diagnostic smoke (`test:browser`) | Fast `@smoke` browser-platform checks against `build/web-diagnostics/site-diagnostics`. |
| Diagnostic remainder (`test:browser:remainder`) | Non-smoke, non-product browser boundaries: Worker/page lifecycle, file selection, OPFS, locks, events, WebGL2, audio, recovery and persistence. Clear `KISAK_COD4_RETAIL_ROOT` for an isolated synthetic run. |
| Production (`test:browser:product`) | Shipped launcher, capabilities, filesystem leases, input scaling and mounting against `build/web/site`. |
| Exhaustive (`test:browser:full`) | Explicit full browser run when duplicate boundary evidence addresses a remaining risk. |
| Static/product checks | Syntax, ESLint, JavaScript types, generated-file/export allowlists and size budgets. |
| Parser fuzz | Synthetic malformed IWD/IWI input under sanitizers; no retail fixtures. |
| Owned retail | Opt-in [local validation](local-retail-validation.md), campaign-map, cinematic and UI checks. Private local files only; never hosted CI fixtures. |

CI runs Linux/native, sanitized fuzz, Windows MSVC, direct-Wasm, Node/static,
production and diagnostic builds, product boundary checks, product browser,
diagnostic smoke and remainder tiers. Native/Wasm semantics should not be
routinely repeated in browser tests tagged `@native-covered`.

## Known qualification limits

On Windows, renderer sampler changes also need installed Chrome/D3D11:
set `KISAK_BROWSER_CHANNEL=chrome`, choose an isolated `KISAK_WEB_TEST_PORT`,
and run `npx.cmd playwright test tests/browser/dynamic_lights.spec.mjs`.
The first pixel draw forces ANGLE backend compilation; WebGL link/validation
alone did not catch an earlier HLSL helper-name collision.

The 2026-09-04 Chrome graphics run recorded three exact-pixel failures
(127 versus 128 in soft alpha and light/mip green output); its other five
cases passed. Default Chromium routine tiers passed. These observations do
not establish native/browser fidelity or justify weakening assertions.

Historical owned remainder failures included a clean-source guard rejecting
uncommitted changes, cinematic mounting observing a terminated Worker, and a
transient-omni brightness delta below its assertion. The separate Gate 3
geometry failure was resolved by following canonical lit/decal/emissive DPVS
ranges. See [current status](web-status.md) and the relevant evidence records
for qualification; historical test counts are not current suite totals.

## Optimized assertion and ABI matrix

Every target in `tests/native/CMakeLists.txt` inherits `-UNDEBUG` or `/UNDEBUG`
and a forced `test_assertions.h` compile guard, including future registrations.
The deliberately failing child is invoked only through a passing parent that
requires the intended assertion diagnostic and nonzero status. Production
directories retain their existing optimization/assertion policy. The separate
reverb test retains its assertion flags and includes the same guard.

| Matrix | Placement |
| --- | --- |
| Linux x86-64 host-portable | Portable parser/math/platform calculations. Configure prints explicit exclusions for canonical layouts. Primary-light core no longer includes `q_shared.h`. |
| Windows x86 canonical | All portable checks plus 32-bit Kisak layouts and native differential/oracle coverage. |
| Wasm32 | Canonical layouts, portable checks and direct platform/differential coverage; MSVC-only oracle remains native. |
| Windows x64 supplemental | Portable subset; useful independent host evidence, not Linux qualification. |

`r_gamma_tests`, `ui_savegames_tests`, `r_text_tests` and `r_image_quality_tests`
require the canonical 32-bit Windows/Wasm boundary. They are deliberately
registered there; no layout assertions or packing were changed. The static-model
Wasm fixture has a test-only 256 KiB stack with overflow checks after its default
64 KiB stack overflow was reproduced; production stack sizing is unchanged.

The supported Linux sequence remains:

```sh
cmake -S . -B build/audit-portable -G Ninja -DKISAK_PORTABLE_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/audit-portable --parallel 2
ctest --test-dir build/audit-portable --output-on-failure
```

## Seeded parser defense

`tests/fuzz/generate_corpus.py` reproduces 12 synthetic GPL-3.0 seeds and prints
their SHA-256 identities; `tests/fuzz/asset_parsers.dict` supplies format tokens.
The harness reaches valid and malformed IWI parse/decode and IWD member paths,
checks buffer progress and failed-image publication, and reports seeded path
families. It does not fuzz fastfiles, saves or cinematics.
Configure Clang with `KISAK_BUILD_FUZZERS=ON` and `KISAK_PORTABLE_TESTS_ONLY=ON`,
then build `asset_parsers_fuzz`. Run:

```sh
python3 tools/run_asset_fuzz.py build/fuzz/tests/native/asset_parsers_fuzz build/fuzz-run --seconds 30
```

Use a new disposable output directory each time. ASan/UBSan/libFuzzer remain
enabled; PR runs use 30 seconds, scheduled/manual runs 600. Limits are 256 KiB
input, 10 seconds per input, 1 GiB RSS, 512 MiB single allocation and a total
subprocess deadline of budget + 60 seconds. Failure artifacts contain synthetic
inputs/logs only. A successful process must also reach every seeded
success/rejection family; exact coverage counters are not cross-platform gates.

## Current execution evidence — 2026-09-07

The follow-up to `170feb7a` finishes canonical save-rename refusal handling,
cgame floating-point conversion fixes, JSPI/native-Wasm exception integration,
and complete source/dependency receipt binding. Local pinned tools remain
Node 24.18.0, npm 11.16.0, Emscripten 6.0.6, CMake 4.2.0-rc3 and Ninja 1.13.2.

- Native MSVC x86 Release: 43/43 CTest cases and the native SP build/relink pass.
- Direct Wasm Release: 43/43 CTest cases, including real cgame parsing and
  canonical save-write/rename refusal, retry and short-write handling.
- Static checks and Node: pass; 123 Node cases include deterministic absolute
  watchdog deadlines and the actual embedded frame pump's scheduling/error paths.
- Production Chromium: 56/56 cases, including rejection of missing JSPI APIs or
  native exception support before engine/asset storage access.
- Diagnostic Chromium smoke: 10/10 cases.
- Diagnostic Chromium remainder with retail disabled: 61 pass, 13 optional skips.
- Source qualification: 14/14 Python cases and collection of actual pinned
  FFmpeg, OpenAL, Emscripten runtime and zlib source archives pass. Altered source
  archives and unexpected runtime source files cannot reuse a build receipt.
- Release production/diagnostic builds and canonical runtime-prefix checks pass.
  The unchanged product gate passes: 3,181,796 Wasm bytes, 322,477 JavaScript
  bytes, 3,607,632 total site bytes, 17 raw and nine application exports.
- One owned-data complete Killhouse intro/loading test passes in headless Chrome:
  DB progress advances while suspended, registration finishes during the movie,
  and world frames resume after completion. It is functional loading evidence,
  not human playable or performance qualification.

The first routine remainder attempt inherited a local retail-root environment
setting and selected optional retail cases: the authoritative matrix rejected
dirty source, and the reduced Gate 3 fixture timed out on its scene-view
predicate. Its trace retains 8,480 surfaces / 445,595 vertices / 823,896 indices
and 797 batches; the fixture still expects the camera-only totals from before
`4bee7442` added shadow-only BSP geometry. Generation reached 53 versus the
required 120 during that run. The cull conversion does not own registration
counts; assertions were left intact. That attempt was interrupted; the synthetic tier explicitly clears
`KISAK_COD4_RETAIL_ROOT` and runs on a separate port. The existing port 8000
server was left untouched. Logs and private retail traces remain under ignored
`build/finish-uncommitted/` and `test-results/`.

Linux, sanitizer CI, aggregate package qualification, branded Edge acceptance,
full campaign save/Continue and actual release-version rollback were not run.
The targeted Chrome loading pass does not qualify the optional Gate 3 fixture.

## Previous execution evidence — 2026-09-06

The remediation uses audited HEAD `4bee744235da2f99dbf09dc0a5118717402758c6`
plus uncommitted changes. Local tools: Node 24.18.0, npm 11.16.0, TypeScript
7.0.2, Emscripten 6.0.6, CMake 4.2.0-rc3, Ninja 1.13.2; MSVC toolset directory
14.51.36231 (compiler 19.51.36256), SDK 10.0.28000.0. Playwright 1.61.1 uses
Chromium 149.0.7827.55 on Windows. Pinned TS rejects a disposable implicit-any
probe without explicit `--strict`; the separate runtime tier still explicitly
uses `--strict false`.

Release native x86 CTest passes 43/43, Wasm 42/42 and supplemental MSVC x64
25/25. Separate optimized native and Wasm reverb tests also pass with the shared
assertion guard. Active assertions exposed the missing thread enum, stale FX index,
missing fixture loading keepalive, retired map-defer expectation and Wasm
fixture stack overflow; all were repaired against canonical interfaces or
the audited HEAD's removed hook. No production behavior changed for these fixes.
Static checks and Node pass 120/120. Production browser passes 53; diagnostic
smoke passes 10 and remainder 61 with 13 explicit skips. These include real
OPFS same-profile restart recovery for rename and backup restore after journal
publication, interrupted partial writes and quota failure, each with two clean
reopens. Backup/raw-file reimport survives browser restart while external
requests are blocked. The backup owns its bytes after original-file deletion;
closing a committed restore retains its writer lease until publication finishes.
Export lease/pagination and invalid/oversized-home regressions also pass.
A delayed raw-export read error no longer changes a reopened dialog's status;
its regression failed before the generation guard and passes afterwards.
Both Release builds and canonical runtime-prefix checks pass. Packaging tests
pass eight cases, including all failed/cancelled/skipped/missing required-tier
combinations, standalone verification and rejection of private home backups.
Synthetic packages exercise the shipped local server at one loopback origin
through directory replacement and rollback; incomplete extraction and revision
mismatch leave the previous package intact. This is platform evidence, not
cross-version campaign acceptance or qualification of the current build.

The unchanged product gate **fails**: 74 raw Wasm exports exceed 24, and the
rebuilt Wasm is 5,343,379 bytes versus its 3,332,379-byte budget. The application
export list remains exact. The audited HEAD added Asyncify; no production
C++ or linker policy was changed by this remediation. Historical budget-pass
claims do not qualify this artifact. Gate failure prevents aggregate packaging.

Linux and sanitizer execution were unavailable locally. The SDK Clang 24.0.0git
Debug fuzz target links, but cannot start because its matching ASan DLL is
missing (exit `0xc0000135`). RelWithDebInfo also hits the bundled libFuzzer's
debug-STL link mismatch. CI is configured, not executed evidence. No campaign,
new performance, physical power-loss, other-browser or exhaustive duplicate
qualification was performed. Disposable commands, failures and results are in
`build/audit-remediation/`; the tracker identifies each audit item and next step.
