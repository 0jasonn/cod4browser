# Web-port working guide

This is the concise operational guide for the current `web-port` branch. The
authoritative architecture and debt classification is
[`web-port-convergence.md`](web-port-convergence.md). The former chronological
milestone log is preserved in
[`history/web-port-milestones.md`](history/web-port-milestones.md).

## Current runtime modes

Normal mode mounts a validated installation in the engine Worker, executes the
canonical runtime prefix, and reaches generated asset loading. It does not run
the retail census or bounded Killhouse renderer proof automatically.

Gate 2 diagnostic/oracle mode is explicitly started by tests or diagnostics.
It retains the census, canonical `GfxWorld` proof, and bounded WebGL2 draw as a
regression oracle. It is frozen infrastructure, not the product object model.

## Toolchain and build

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/bootstrap_web_toolchain.ps1
$env:KISAK_BUILD_JOBS = "2" # optional, integer 1-16
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build_web.ps1 -Configuration Release
python tools/serve_web.py --directory build/web/site
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
```

- `test:browser` runs fast `@smoke` browser-platform confidence.
- `test:browser:remainder` runs scenarios that are neither smoke nor
  `@native-covered`; it does not repeat smoke.
- `test:browser:full` is the explicit exhaustive browser suite, including
  native-authoritative duplicates useful for boundary diagnostics.

Direct native/Wasm tests own parser bounds, zlib failure, PMem, DB pool/hash,
pointer forms, stream transitions, generated loader semantics, ABI, atomicity,
and deterministic traces. Browser tests own Worker/page lifecycle, file
selection, OPFS, Web Locks, the synchronous Worker filesystem boundary,
JS/Wasm events, canvas/WebGL2, context recovery, reload, and persistence.

## Legal test profile

The launcher allowlists the current English single-player profile:
`localization.txt`, 21 stock/localized IWDs, three startup fastfiles, and
`killhouse.ff`. Files come only from a user-selected legally owned installation
and stay in private browser storage. Tests and CI use generated synthetic
fixtures and must not fetch or embed retail data.

## Near-term convergence work

The next runtime work should reduce prefix scaffolding by compiling canonical
Kisak owners and dependencies, then progress toward `CM_LoadMap`, `CL_Init`,
`CG_Init`, `SV_Init`, xanim/collision, and the script VM. Do not expand the
Gate 2 viewer or add a browser-owned engine model.
