# Web-port working guide

This is the concise operational guide for the current `web-port` branch. The
authoritative architecture and debt classification is
[`web-port-convergence.md`](web-port-convergence.md). The former chronological
milestone log is preserved in
[`history/web-port-milestones.md`](history/web-port-milestones.md).

## Current runtime modes

Normal mode mounts a validated installation in the engine Worker and follows
the canonical runtime through generated loading, ClipMap, game/cgame, and the
renderer frontend. The separate diagnostics target compiles those same
sources with browser-only failure and recovery controls.

## Toolchain and build

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/bootstrap_web_toolchain.ps1
$env:KISAK_BUILD_JOBS = "2" # optional, integer 1-16
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build_web.ps1 -Configuration Release
python tools/serve_web.py --directory build/web/site

# Optional diagnostic artifact/site
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build_web.ps1 -Configuration Release -Diagnostics
```

Build output is isolated under `build/web`; the generated site is
`build/web/site`. `tools/build_web.ps1` configures Emscripten, builds with
bounded parallelism, runs the strict canonical-runtime prefix check, and emits
`KISAK_TIMING` lines. Never commit generated artifacts or the ignored `.tools`
runtime.

## Validation tiers

```powershell
npm.cmd ci
npm.cmd run test:browser
npm.cmd run test:browser:remainder
npm.cmd run test:browser:full
npm.cmd run check:web:static
npm.cmd run test:protocol
npm.cmd run check:web:product
```

- `test:browser` runs fast `@smoke` browser-platform confidence.
- `test:browser:remainder` runs non-smoke browser-boundary scenarios without
  repeating smoke.
- `test:browser:full` is the explicit exhaustive browser suite.
- `tools/validate_web_retail.ps1 -RetailRoot <path>` runs the opt-in local
  [retail validation matrix](local-retail-validation.md); it is never hosted CI.

Direct native/Wasm tests own parser bounds, zlib failure, PMem, DB pool/hash,
pointer forms, stream transitions, generated loader semantics, ABI, atomicity,
and deterministic traces. Browser tests own Worker/page lifecycle, file
selection, OPFS, Web Locks, the synchronous Worker filesystem boundary,
JS/Wasm events, canvas/WebGL2, context recovery, reload, and persistence.

## Legal test profile

The versioned launcher profile requires English localization, 21 stock/
localized IWDs, three startup fastfiles, and `killhouse.ff`. It also records
selected single-player fastfiles directly under `zone/english`, while excluding
the `mp_*` and `*_mp` naming families. Discovery is not a compatibility claim.
Files come only from a user-selected legally owned installation and stay in
private browser storage. Tests and CI use generated synthetic fixtures and
must not fetch or embed retail data.

## Near-term convergence work

The next work is evidence-driven compatibility and memory reduction: run the
local Killhouse/CargoShip transition matrix, measure recovery telemetry, then
reduce reloadable retained data without changing canonical traversal or adding
a browser-owned engine model.
