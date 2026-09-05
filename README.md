# KisakCOD browser port

This branch ports [KisakCOD](https://github.com/SwagSoftware/KisakCOD), the
GPL-3.0 C++20 reimplementation of Call of Duty 4, to modern browsers. It is a
platform port: portable engine behavior stays close to KisakCOD, while browser
storage, lifecycle, filesystem hosting, and WebGL2 live behind explicit
platform boundaries.

The Release product runs in a dedicated Worker and follows the canonical Kisak
path through `Com_Init`, database/XFile loading, ClipMap, server/game, local
client/cgame, renderer-frontend commands, and actual WebGL2 world frames. Clean
historical local retail evidence at `f5229806` and `247980a6` covers six maps:
Killhouse and Airplane are `PLAYABLE`; CargoShip, Blackout, Hunted, and Bog A
are `FUNCTIONAL`. Scoutsniper and AC130 have separate `RENDERS` evidence;
14 discovered direct SP zones remain `UNTESTED`. This is
still an incomplete port, not a generally compatible COD4 release.

Historical Airplane evidence at `da1e592c` records save/reload continuity:
live AI/scripts/objective state, combat, natural and named saves, death/restart,
browser shutdown, fresh-runtime load, restored state, and continued play. This
does not establish objective/trigger progression. Mission-flow validation is
not a prerequisite for renderer improvements or cleanup.

The opt-in diagnostics target builds the same runtime with browser-only test
controls and telemetry. Production does not expose those controls.

## Legal asset boundary

No proprietary COD4 data, Steam components, CD keys, Bink/Miles binaries, or
retail-derived test fixtures belong in this repository or its artifacts.
Users select files from a legally owned installation; the launcher validates
an allowlisted single-player profile and stores it privately in browser
storage. Committed fixtures are synthetic; optional local checks use owned files.

Inherited Git history and local checkouts still contain legacy native SDKs.
Browser source releases must use `git archive` from the same committed revision
as the binary, with the versioned `.gitattributes` exclusions, and pass
`python tools/check_source_archive.py <archive.zip>`. For example:

```powershell
git archive --format=zip --output=build/kisakcod-web-source.zip HEAD
python tools/check_source_archive.py build/kisakcod-web-source.zip
```

The archive excludes Bink/Miles/Steam SDK directories and native binaries;
local native reference dependencies are retained in the checkout. A raw clone
or an archive of an older revision is not a sanitized browser source release.
The generated flat browser site has its own exact file/export and size gate.

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
[docs/web-port-convergence.md](docs/web-port-convergence.md). See the
[current status](docs/web-status.md), [ordered roadmap](docs/web-roadmap.md),
[campaign matrix](docs/campaign-compatibility.md) for claim scope and exact
evidence. Superseded records remain in [Git history](#historical-records).

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
the canonical Killhouse -> CargoShip -> Blackout -> Killhouse matrix; the
campaign mode validates one explicitly selected SP zone at a time. Both use
legally owned local files and cover persistence, input/audio, transitions, and
context loss.

Routine CI runs smoke and remainder once each against the exact diagnostic
Release artifact. Parser/database semantics stay in the direct native and Wasm
suites instead of being repeated in a browser.

## Historical records

Current ownership, priorities and compatibility evidence live in the guides
linked above. Superseded milestones and numeric benchmark dumps remain in Git;
retained evidence reports include exact retrieval commands. To inspect earlier
documents without changing the working tree:

```powershell
git log --all -- docs
git ls-tree -r --name-only 49d6168cab15181f03744cf07f10b288b673bc0c docs/history
git show 49d6168cab15181f03744cf07f10b288b673bc0c:docs/history/web-port-milestones.md
```

## License

KisakCOD and this port are GPL-3.0. See [LICENSE](LICENSE). Call of Duty is
a trademark of Activision; this project is unaffiliated with Activision.
