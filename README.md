# KisakCOD browser port

This branch ports [KisakCOD](https://github.com/SwagSoftware/KisakCOD), the
GPL-3.0 C++20 reimplementation of Call of Duty 4, to modern browsers. It is a
platform port: portable engine behavior stays close to KisakCOD, while browser
storage, lifecycle, filesystem hosting, and WebGL2 live behind explicit
platform boundaries.

The current build is an architecture and compatibility proof, not a playable
game. A dedicated Worker executes the canonical `Com_Init` prefix, physical
memory, database pools, XFile streaming, and every generated family required
to complete the canonical `code_post_gfx`, `ui`, and `common` startup-zone
request. The normal DB path then opens Killhouse, publishes the canonical
`GfxWorld`, compares it with the frozen Gate 2 oracle, and renders its bounded
world surface through WebGL2. Normal startup does not use the census as DB.

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
  completion of all 8,176 ordered assets in the three engine-requested startup
  prerequisite zones, and normalized native/Wasm traces;
- an opt-in Gate 2 oracle retaining the retail census, canonical Killhouse
  `GfxWorld`, and bounded WebGL2 world-surface proof; and
- synthetic Linux, MSVC x86, Wasm, browser, sanitizer, and fuzz validation.

The real `CM_LoadMap`, script/XAnim/DObj initialization, local-server command
path, and their normalized x86/Wasm contracts are compiled and tested. The
owned browser run still stops at the ordered `GameWorldSp` asset before
ClipMap publication. The next architecture decision is how the Worker mount
backs canonical `FS_InitFilesystem`, directory enumeration, and IWD/minizip
access so full `CL_Init` can replace the bounded startup entry without
duplicating browser asset state. Input, audio, cinematics, cgame, and a
playable offline slice are not yet demonstrated.

## License

KisakCOD and this port are GPL-3.0. See [LICENSE.md](LICENSE.md). Call of Duty is
a trademark of Activision; this project is unaffiliated with Activision.
