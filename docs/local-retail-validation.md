# Local retail validation

This validation is local-only. It reads files from a legally owned English
Call of Duty 4 installation through the browser picker; it does not copy them
into the repository or CI artifacts. Close other cod4browser tabs first so the
test can acquire the exclusive writable-home lease.

After installing the pinned toolchain, npm dependencies, and target browsers,
run the permanent three-map baseline in headed branded Chrome and Edge:

```powershell
.\tools\validate_web_retail.ps1 `
    -RetailRoot 'D:\Games\Call of Duty 4' `
    -Browser chrome

.\tools\validate_web_retail.ps1 `
    -RetailRoot 'D:\Games\Call of Duty 4' `
    -Browser msedge
```

Chrome is the default. `chromium`, `chrome`, and `msedge` are accepted. The
wrapper is headed unless `-Headless` is supplied; headless runs are functional
evidence, not the primary performance benchmark. Leave the game window in the
foreground during each timed window.

The command builds the diagnostic artifact because forced WebGL context loss
and canonical gameplay-state probes are deliberately unavailable to the
production protocol. The test otherwise uses the normal importer and
canonical runtime. It requires `killhouse.ff`, `cargoship.ff`, and
`blackout.ff`, then runs:

```text
Killhouse -> CargoShip -> Blackout -> Killhouse
```

It verifies:

- installation import, persistence, and canonical mount;
- database completion, ClipMap/world, server, game, cgame, and actual world
  frames on every map;
- a foreground 60-second window after `page.bringToFront()`, sampled every
  second for `document.visibilityState`, focus, and background transitions;
- actual frame intervals, p50/p95/p99, FPS equivalents, game/wall advancement,
  renderer identity, browser name/version, and performance-window validity;
- W/A/S/D movement, jump, mouse look, canonical clip/shot response to MOUSE1,
  canonical ADS/secondary response to MOUSE2, and canonical wheel selection or
  `NOT_APPLICABLE_SINGLE_WEAPON`;
- Escape/menu state, pointer-lock loss and reacquisition, full initial input,
  and a reduced critical subset after each in-process transition;
- decoded gameplay audio, config checkpoint duration/bytes, shutdown flush,
  and config reload;
- ordered unload begin/end and new-world publication on one WebGL context, with
  old map-local recovery retired before replacement publication;
- forced WebGL2 context loss/recovery on CargoShip, Blackout, and returned
  Killhouse, followed by resumed frames and input; and
- Wasm allocator/capacity snapshots before load, after database completion,
  after cgame, after first frame, at steady state, unload/publication, and
  context recovery, plus logical decoded texture size, actual encoded/source
  recovery, GPU estimates, geometry, transient upload, programs, and audio.

The result is one non-proprietary schema-v2 `KISAK_RETAIL_RESULT` JSON record.
If a timed window becomes hidden or unfocused, its performance fields are
invalidated with `performanceWindowValid: false`; slow background timing must
not be used to assign or remove `PLAYABLE` status.

The wrapper sets `KISAK_COD4_RETAIL_ROOT` itself. The Playwright case is
skipped only when invoked directly without that environment variable. Record
the exact commit, clean/dirty state, browser version, reference hardware,
foreground validity, result, and renderer-memory events before changing the
recovery budget or compatibility matrix.

## Additional campaign maps

Validate one discovered single-player zone at a time after the baseline is
green:

```powershell
.\tools\validate_web_campaign_map.ps1 `
    -RetailRoot 'D:\Games\Call of Duty 4' `
    -Map airplane `
    -Browser chrome
```

The wrapper accepts a lowercase zone name containing letters, numbers, or
underscores, rejects `mp_*` and `*_mp`, and requires the selected fastfile in
the supplied installation. It starts with a fresh browser profile, reaches a
CargoShip world frame, transitions into the target, verifies its full
canonical lifecycle, runs a foreground 60-second window, exercises canonical
gameplay input/audio/config persistence, records memory, forces WebGL2 context
recovery, and transitions to Killhouse before clean shutdown and reload.

It emits one non-proprietary `KISAK_RETAIL_PHASE3_RESULT` JSON record. The
opt-in campaign case is skipped without an explicit target, so routine browser
suites and the permanent Killhouse -> CargoShip -> Blackout -> Killhouse
baseline remain separate.

Use these compatibility results:

- `FUNCTIONAL` for a passing sustained runtime with core gameplay/input/audio;
- `PLAYABLE` only when `FUNCTIONAL` and a valid foreground window meets the
  current reference of average >=30 FPS, p95 <=50 ms, and game/wall ratio
  >=0.90; and
- `BLOCKED` only for a deterministic compatibility failure at an identified
  canonical boundary.

The threshold describes the recorded reference hardware/browser, not a
universal user requirement. Discovery alone remains `UNTESTED`.
