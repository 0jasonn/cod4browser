# KisakCOD browser port

This branch ports [KisakCOD](https://github.com/SwagSoftware/KisakCOD), the
GPL-3.0 C++20 reimplementation of Call of Duty 4, to modern browsers. It is a
platform port: portable engine behavior stays close to KisakCOD, while browser
storage, lifecycle, filesystem hosting, and WebGL2 live behind explicit
platform boundaries.

The current build is an architecture and compatibility proof, not a playable
game. A dedicated Worker executes the canonical `Com_Init` prefix, physical
memory, database pools, XFile streaming, and the generated RawFile loader. A
separate, explicitly requested Gate 2 diagnostic traverses the retained retail
census path, publishes the canonical Killhouse `GfxWorld`, and renders one
bounded world surface through WebGL2. Normal startup does not run that oracle.

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
adapters, and the WebGL2 backend. The cooperative qcommon shell, retail census,
and extracted prefix files are temporary regression or integration scaffolds
with explicit retirement contracts.

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
npm.cmd run test:browser:remainder  # not @smoke and not @native-covered
npm.cmd run test:browser:full       # explicit exhaustive browser suite
```

Routine CI runs smoke and remainder once each against the exact Release
artifact it uploads. Tests tagged `@native-covered` remain available in the
exhaustive command, but their parser/database semantics are authoritative in
the direct native and Wasm suites.

## Project status

Demonstrated:

- a served Release Wasm site and non-blocking browser frame pump;
- legal local import, validation, persistence, restore, and Worker-mounted
  synchronous filesystem access;
- canonical command/dvar behavior, 128 MiB PMem, DB pools, XFile streaming,
  generated RawFile publication, and normalized native/Wasm traces;
- an opt-in Gate 2 oracle retaining the retail census, canonical Killhouse
  `GfxWorld`, and bounded WebGL2 world-surface proof; and
- synthetic Linux, MSVC x86, Wasm, browser, sanitizer, and fuzz validation.

Not yet demonstrated: `CM_LoadMap`, the complete client/server/game/cgame
runtime, script VM, input, audio, cinematics, multiplayer transport, or a
playable offline slice.

## License

KisakCOD and this port are GPL-3.0. See [LICENSE.md](LICENSE.md). Call of Duty is
a trademark of Activision; this project is unaffiliated with Activision.
