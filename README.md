# KisakCOD browser port

This branch ports [KisakCOD](https://github.com/SwagSoftware/KisakCOD), the
GPL-3.0 C++20 reimplementation of Call of Duty 4, to modern browsers. It is a
platform port: portable engine behavior stays close to KisakCOD, while browser
storage, lifecycle, filesystem hosting, and WebGL2 live behind explicit
platform boundaries.

The Release product runs in a dedicated Worker and follows the canonical Kisak
path through `Com_Init`, database/XFile loading, ClipMap, server/game, local
client/cgame, renderer-frontend commands, and actual WebGL2 world frames.
Historical local retail evidence catalogued at commit `887f1c87` on 2026-08-25
includes Killhouse input, HUD, effects, and Web Audio; it was not rerun during
the 2026-08-26 cleanup because no retail root was supplied. This is still an
incomplete port, not a generally compatible COD4 release.

The opt-in diagnostics target builds the same runtime with browser-only test
controls and telemetry. Production does not expose those controls.

## Legal asset boundary

No proprietary COD4 data, Steam components, CD keys, Bink/Miles binaries, or
retail-derived test fixtures belong in this repository or its artifacts.
Users select files from a legally owned installation; the launcher validates
an allowlisted single-player profile and stores it privately in browser
storage. Automated tests use generated synthetic data only.

## Architecture

```text
COD4 fastfile
    -> Kisak filesystem and database
    -> canonical XAssets
    -> Kisak engine systems
    -> renderer frontend
    -> portable draw commands
    -> WebGL2 backend
```

Current permanent browser ownership is limited to the launcher/import flow,
OPFS and Worker filesystem host, page/Worker lifecycle, Emscripten system
adapters, input/audio hosts, and the WebGL2 backend. Canonical Kisak code owns
the engine, assets, game state, filesystem semantics, and renderer frontend.

The authoritative component classification and current blockers are in
[docs/web-port-convergence.md](docs/web-port-convergence.md). Concise build and
validation instructions are in [docs/web-port.md](docs/web-port.md). Historical
milestone narratives are retained under [docs/history](docs/history/README.md).

## Build

Requirements are bootstrapped into ignored `.tools/` directories:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/bootstrap_web_toolchain.ps1
$env:KISAK_BUILD_JOBS = "2" # optional; defaults to a conservative 2
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build_web.ps1 -Configuration Release
python tools/serve_web.py --directory build/web/site
```

The build produces `build/web/site`. Serve it over HTTP; `file://` is not a
supported runtime. The build keeps strict undefined-symbol checking enabled
and prints configure, compile, runtime-check, and total timings.

## Browser validation

After `npm.cmd ci` and Playwright browser installation:

```powershell
npm.cmd run test:browser            # @smoke
npm.cmd run test:browser:remainder  # non-smoke browser-boundary scenarios
npm.cmd run test:browser:full       # explicit exhaustive browser suite
npm.cmd run check:web:static        # ESLint and typed protocol check
npm.cmd run test:protocol           # protocol/profile/lifecycle unit tests
npm.cmd run check:web:product       # production files, symbols, exports, sizes
```

Build the separate diagnostic site with `tools/build_web.ps1 -Diagnostics`.
It is emitted under `build/web-diagnostics/site-diagnostics`.

The opt-in [local retail validation](docs/local-retail-validation.md) exercises
the Killhouse/CargoShip, persistence, input/audio, and context-loss matrix with
legally owned files.

Routine CI runs smoke and remainder once each against the exact diagnostic
Release artifact. Parser/database semantics stay in the direct native and Wasm
suites instead of being repeated in a browser.

## Project status

Demonstrated:

- a served Release Wasm site and non-blocking browser frame pump;
- legal local import, validation, persistence, restore, and Worker-mounted
  synchronous filesystem access;
- canonical command/dvar behavior, 128 MiB PMem, DB pools, XFile streaming,
  completion of all 8,176 ordered assets in the three engine-requested startup
  prerequisite zones, and normalized native/Wasm traces;
- a separate opt-in diagnostic artifact for browser-only context, audio,
  filesystem, input, storage, and failure controls; and
- synthetic Linux, MSVC x86, Wasm, browser, sanitizer, and fuzz validation.

The real `CM_LoadMap`, script/XAnim/DObj initialization, local-server command
path, `CL_Init`, `CG_Init`, and normalized x86/Wasm contracts are compiled and
tested. The Worker mount backs canonical `FS_InitFilesystem`, directory
enumeration, and Kisak minizip access without duplicating search paths or pack
state in JavaScript. Browser input and bounded Web Audio are demonstrated.
Native Bink playback is intentionally omitted with a visible structured skip;
gamepad support, full cinematics, advanced audio parity, broader campaign
validation, and remaining material families are outstanding.

## License

KisakCOD and this port are GPL-3.0. See [LICENSE](LICENSE). Call of Duty is
a trademark of Activision; this project is unaffiliated with Activision.
