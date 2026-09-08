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

Recorded 2026-09-09 on the cleanup working tree based on
`4bca1760f95edb60c362926fa944e96dbcae3f2a`. Removed compiler duplicates and
unused platform/fixture APIs; archived superseded reports in Git. The remaining
11 captured fixture outputs have identical lengths and SHA-256 hashes.

| Check | Result |
| --- | --- |
| Pinned `npm.cmd ci`, syntax, lint and types | Pass; Node 24.18.0 / npm 11.16.0. |
| Native MSVC x86 Release | 44/44 CTest cases; pinned native SP build/relink passes. |
| Direct Wasm Release | 43/43 CTest cases pass. |
| Node protocol/platform | 124/124 cases pass. |
| Production Chromium | 56/56 cases pass. |
| Diagnostic Chromium | 10/10 smoke; 64 remainder pass, 16 optional skips; retail inputs disabled. |
| Release builds | Production and diagnostics pass, including both canonical runtime-prefix checks. |
| Product boundary | Pass: 3,191,265 Wasm bytes, 322,498 JavaScript bytes, 3,617,122 site bytes; 17 raw / nine application exports, 22 files. |
| Source/package checks | 14/14 Python cases and generated source archive validation pass. |

Tests used default Playwright Chromium on Windows. Native SP was built, not
launched for gameplay. The original disposable `build/repo-cleanup/` logs were
recycled during cleanup. The source receipt records `dirty=True`; this is local synthetic platform evidence.
Linux/hosted CI, sanitizer fuzz, native MP, owned campaign acceptance, installed
Chrome fidelity, exhaustive browser duplicates and aggregate release
qualification were not rerun for this cleanup.

The earlier 30-second Windows sanitizer pass, owned loading check and superseded
execution records remain in [Git history](../README.md#historical-records).
The documentation consolidation only ran reference and whitespace checks; it did
not recreate the recycled toolchain or rerun runtime suites.
