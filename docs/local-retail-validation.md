# Local retail validation

Use a legally owned English COD4 installation. Files enter through the browser
picker and remain local; never package retail data, profiles, screenshots or
logs for CI. Close other cod4browser tabs to release the exclusive writable-home
lease. Complete the pinned setup in the [README](../README.md) first.

## Baseline and additional maps

From the repository root, run the diagnostic baseline in headed Chrome and Edge:

```powershell
.\tools\validate_web_retail.ps1 -RetailRoot 'D:\Games\Call of Duty 4' -Browser chrome
.\tools\validate_web_retail.ps1 -RetailRoot 'D:\Games\Call of Duty 4' -Browser msedge
```

The wrapper builds Release diagnostics and runs
`Killhouse -> CargoShip -> Blackout -> Killhouse` against a temporary browser
profile. It requires the three map fastfiles plus the startup files. Chrome is
the default; `chromium`, `chrome` and `msedge` are accepted. `-Port` selects an
isolated server (default 8030); `-Headless` disables the visible window.
Keep headed timed windows focused and visible, with no concurrent build.

The baseline checks import/mount/persistence; canonical DB, ClipMap/world,
server/game and client/cgame startup; actual world frames; movement, mouse,
fire/ammo, ADS, weapon selection, menus and pointer lock; decoded audio and
configuration persistence; map retirement/publication; forced WebGL2 recovery
with resumed input; and shutdown/reload. A weapon-selection check can report
`NOT_APPLICABLE_SINGLE_WEAPON`.

Each map has a 60-second window measuring frame intervals, p50/p95/p99,
game/wall advancement, focus and visibility. Diagnostics collect a separate
300-completed-frame profile and bounded lifecycle/memory observations.
`decodedTextureRecoveryBytes` is logical decoded size, not retained storage;
encoded recovery, GPU estimates and Wasm capacity are distinct populations and
must not be added into a process-memory total. Profiling and context-loss probes
use the diagnostic artifact because production does not expose those APIs.

Validate another single-player zone after the baseline passes:

```powershell
.\tools\validate_web_campaign_map.ps1 -RetailRoot 'D:\Games\Call of Duty 4' -Map airplane -Browser chrome
```

This starts a fresh profile, loads CargoShip, transitions to the target, runs
lifecycle/gameplay/timing/recovery checks, then transitions to Killhouse and
checks shutdown/reload. Names must contain lowercase letters, numbers or
underscores; `mp_*` and `*_mp` are rejected. The selected fastfile must exist.
Its default port is 8031.

## Stationary and mission probes

```powershell
.\tools\validate_web_campaign_map.ps1 -RetailRoot 'D:\Games\Call of Duty 4' -Map scoutsniper -Browser chrome -ObserveOnly -Headless
```

`-ObserveOnly` stops after the stationary window and profile. It injects no
keyboard/mouse/gameplay state, and skips config persistence, forced recovery
and transition-out. It can establish world frames and stationary stability;
headless observations do not qualify visual correctness or playable gameplay.

The optional `-Mission` collector uses bounded canonical input actions and
checks progression/combat plus save/death/reload boundaries;
`-MissionStage progression` stops before the later save checks. It cannot combine with
`-ObserveOnly`. Passing an automated collector is not authored mission
completion. Route/replay automation is retired; use ordinary observed gameplay
for the acceptance defined in [campaign compatibility](campaign-compatibility.md).

## Results and qualification

Collectors emit `KISAK_RETAIL_RESULT`, `KISAK_RETAIL_PHASE3_RESULT` or
`KISAK_RETAIL_MISSION_RESULT` JSON. The wrappers set `KISAK_COD4_RETAIL_ROOT`;
direct Playwright invocation skips retail cases without it. Additional map,
mission and decode-chain cases require their explicit opt-in variables.
Keep disposable runs under ignored `build/` and sanitize anything shared.

Authoritative runs require a clean source commit. `KISAK_RETAIL_ALLOW_DIRTY=1`
permits exploratory work, including an optional shortened stability window;
it does not provide release qualification. Record commit/artifact hashes,
clean/dirty state, date, browser/version, reference hardware, renderer identity,
foreground validity, results and the earliest failure boundary. A hidden or
unfocused window sets `performanceWindowValid: false`; such timing cannot
promote or demote the [compatibility levels](campaign-compatibility.md).
Configuration checkpoints are not gameplay saves. Runtime loading, rendering,
playability, authored completion and native/Steam fidelity remain separate claims.
