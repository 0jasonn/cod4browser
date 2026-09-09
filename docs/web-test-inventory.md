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

Optional retail tiers are not qualified by synthetic passes. A later transient-
light run passed illumination/shadows/clearing/recovery but failed the final
positive DObj post-pose diagnostic; see the [renderer guide](renderer-retained-resources.md).
The 2026-09-07 Gate 3 attempt still expected camera-only geometry from before
`4bee7442` added shadow-only BSP surfaces. Its trace contains 8,480 surfaces,
445,595 vertices, 823,896 indices and 797 batches, reaching generation 53
versus the required 120 before timeout. Assertions remain unchanged; the
optional fixture is unqualified. Clear inherited retail settings for synthetic
tiers; authoritative retail checks also require clean committed source.

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

## Current execution evidence

Recorded 2026-09-09 on the local dirty performance working tree based on
`3a1aa20acb84895b9a758d3f4f6a34ba0d666b22`. Release now selects `-O2` with
full LTO and SIMD disabled. Tests cover shared arithmetic fixes, renderer state
reuse, shadow-bound eligibility, water simulation and audio command suppression.
These results qualify the exercised local paths, not a committed release.

| Check | Result |
| --- | --- |
| Syntax, lint and types | Pass; pinned Node 24.18.0 / npm 11.16.0. |
| Native MSVC x86 Release | 43/43 CTest cases pass. |
| Direct Wasm Release | 43/43 CTest cases pass with `-O2` and full LTO. |
| Node protocol/platform | 131/131 cases pass, including the final realtime warmup and clock-validation changes. |
| Production Chromium | 56/56 cases pass. |
| Diagnostic Chromium | 10/10 smoke; 64 remainder pass, 16 optional skips; retail inputs disabled. |
| Installed Chrome/D3D11 | `dynamic_lights.spec.mjs`: 1/1 case passes, including actual shader draws. |
| Source/package checks | 16/16 Python cases pass, including native-SDK exclusion from local input ZIPs and rejection even when an outer hash is refreshed. |
| Owned Cargoship context recovery | Actual `WEBGL_lose_context` loss/restoration passes; all 12 resumed paused samples match the original canonical work counts. |
| Product boundary | Pass: 4,564,302 Wasm bytes, 324,103 JavaScript bytes, 4,991,764 site bytes; 18 raw / nine application exports, 22 files, 5% size headroom. |
| Release builds | Production and diagnostics pass their canonical runtime-prefix checks; delivery uses the same measured engine binaries. |
| Headed production realtime | Repeated 1080p Cargoship: 39.78-39.97 to 51.91-51.93 FPS, target unmet. Defined idle Killhouse: 87.18-87.20 controls, 87.24-87.37 selected, cadence target passes with the documented tail increase. |

Logs are retained locally under ignored `build/performance/`: `node-final.log`,
`static-final.log`, `native-final.log`, `wasm-final.log`, `provenance-final2.log`,
`product-final.log`, `smoke-final.log`, `remainder-final.log`,
`chrome-sampler-final.log` and `final-recovery.log`. The recovery capture is
`build/renderer-efficiency-3a1aa20a-final-recovery.json`.
Final Node/static/package reruns are `node-delivery.log`, `static-delivery.log`,
`provenance-delivery.log` and `product-boundary-delivery.log`.
The subsequent snapshot-boundary correction passes `provenance-sanitized.log`;
`snapshot-sanitization.log` records 40 corrected local archives, with native-only
inputs retained as hash metadata. Original measurement JSON is preserved.

Routine browser tests used default Playwright Chromium on Windows; the sampler
check and owned Cargoship recovery used installed Chrome/D3D11. Paused recovery
does not establish active campaign performance or native/browser scene fidelity.
The optional transient-light retail fixture gained matching recovery-state
assertions but was not rerun; its earlier final DObj diagnostic failure remains
documented in the [renderer guide](renderer-retained-resources.md#validation-and-reproduction).
Realtime results and their scene limits are in the
[renderer guide](renderer-retained-resources.md#realtime-cadence-results).
Linux/hosted CI,
sanitizer fuzz, native SP gameplay, native MP, owned campaign acceptance,
exhaustive browser duplicates and aggregate release qualification were not rerun.

The earlier 30-second Windows sanitizer pass, owned loading check and superseded
execution records remain in [Git history](../README.md#historical-records).
