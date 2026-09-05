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

The final 2026-09-05 fullscreen/recovery, size and material artifact passes the unchanged
production budgets and exact export boundary. Its optimized/minified production
tier passes 48 cases; diagnostics pass 10 smoke and 60 remainder cases with ten
optional owned-data skips, and Node passes 101. Native CTest passes all 42 and
direct Wasm all 41, including the original `db_load.cpp` Material/Image/XModel
oracle and its normalized publication/block expectations. Nine native D3D9
shader pixel cases match WebGL within one UNORM step before/after actual context
recovery; the native/Wasm reverb comparison covers 130 room/rate cases.
Seven foreground production windows and owned three-scene material observations
add bounded execution evidence. See [architecture](web-architecture.md#build-products)
and [performance limits](evidence/browser-frame-time-2026-09-02.md#current-production-measurements--2026-09-05).

One Node checkpoint timer assertion failed during concurrent compilation;
the isolated full 101-test rerun passed with assertions unchanged. The final
Wasm build required adding the missing cinematic platform stub to the map
fixture, with defer/reissue assertions. Routine browser runs use an isolated
port (8254 here) because 8000 was already occupied. An earlier remainder run
inherited the retail-root environment variable and stopped on an optional
movie check; the final synthetic tier explicitly clears it. Linux/sanitizer,
remote CI, exhaustive duplicates, other browsers and manual campaign/native
visual acceptance were not rerun or promoted by this audit.
