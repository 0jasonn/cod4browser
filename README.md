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
New-Item -ItemType Directory -Path build -Force | Out-Null
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

## Documentation

These guides explain how to build and maintain the port; the game does not read
them. Keep current decisions, commands and known limits here. Use Git for dated
reports and superseded plans.

| Guide | Purpose |
| --- | --- |
| [Status and priorities](docs/web-status.md) | What works, what is unqualified and what to do next. |
| [Architecture and ownership](docs/architecture.md) | Which systems use shared Kisak code and which belong to the browser platform. |
| [Campaign compatibility](docs/campaign-compatibility.md) | Map-by-map observations and limits on gameplay claims. |
| [Browser support and recovery](docs/browser-support.md) | Required APIs, local packages, updates and save backups. |
| [Tests](docs/web-test-inventory.md) | Validation tiers, commands and the latest recorded results. |
| [Local retail validation](docs/local-retail-validation.md) | Optional checks with a legally owned installation. |
| [Native reference](docs/native-reference.md) | Build and launch the native comparison target. |
| [Renderer resources](docs/renderer-retained-resources.md) | GPU ownership, rendering constraints and recovery. |
| [Browser audio](docs/browser-reverb.md) | Reverb/EQ ownership, dependency licensing and signal checks. |
| [Cinematics](docs/cinematic-codec.md) | Bink decoder build/licensing, loading and synchronization. |

## Build

Requirements are bootstrapped into ignored `.tools/` directories:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/bootstrap_web_toolchain.ps1
npm.cmd ci
$env:KISAK_BUILD_JOBS = "2" # optional; defaults to a conservative 2
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build_web.ps1 -Configuration Release
python tools/serve_web.py --directory build/web/site
```

The build produces `build/web/site`. Serve it over HTTP; `file://` is not a
supported runtime. The build keeps strict undefined-symbol checking enabled
and prints configure, compile, runtime-check, and total timings.

Release defaults to `-O2` with full LTO; SIMD stays disabled. Compiler comparisons
use `-Optimization Oz|O2|O3`, `-DisableFullLto` and opt-in `-Simd`, with
`-BuildDirectory` keeping each artifact separate. The switches
`-DisableRendererStateReuse`, `-DisableAudioEquality` and
`-DisableShadowBoundsSkip` provide controls for the retained optimizations.
See [renderer measurement and reproduction](docs/renderer-retained-resources.md#validation-and-reproduction)
for matching workloads, production timing and context recovery checks.

Authoritative JavaScript checks and builds enforce the versions in
`package.json` and `tools/web_toolchain.json`. Release builds record source,
tool and site hashes outside the served directory. CI can pair exact source
and site only after every required tier succeeds; its package is explicitly
synthetic-platform qualification, not single-player alpha acceptance.
See [distribution and recovery](docs/browser-support.md#local-package-contract).
Release uses JSPI loading suspension and native Wasm exceptions, and retains
verified public dependency sources outside the served site. The
[test inventory](docs/web-test-inventory.md#current-execution-evidence) records
the latest local checks; aggregate CI and manual campaign acceptance remain
separate qualification requirements.

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

The dated evidence reports, original architecture and roadmap are archived in
commit `15c316606281e4de63cbf3c06626ee60a307498d`. They record results for their
stated revisions; they do not qualify later code. Inspect them without changing
the working tree:

```powershell
git ls-tree -r --name-only 15c316606281e4de63cbf3c06626ee60a307498d docs
git show 15c316606281e4de63cbf3c06626ee60a307498d:docs/evidence/native-reference-2026-09-02.md
```

Substitute any listed path in the second command. For documents removed earlier,
use `git log --all -- docs` to find their revision. Disposable build logs and
captures were recycled during cleanup; Git retains committed reports only.

## License

KisakCOD and this port are GPL-3.0. See [LICENSE](LICENSE). Call of Duty is
a trademark of Activision; this project is unaffiliated with Activision.
