# Canonical save/load menu lifecycle — 2026-09-01

## Outcome

The browser runtime now completes the shared Kisak single-player save/load
workflow through the shipped Load Game menu and its canonical feeders and menu
scripts. A named test save is created in the active profile, discovered and
selected by `UI_LoadSavegames`, loaded through `UI_RunMenuScript`, reopened
after a complete page and Worker restart through Continue, and deleted through
`UI_DelSavegame`.

The browser adds no save format, save registry, gameplay snapshot, or menu
controller. OPFS remains only the durable implementation behind the synchronous
engine filesystem. The test removes only the exact synthetic save it creates
and leaves the map's natural autosave untouched.

## Reached fixes

1. Canonical map/save load exceeds Emscripten's 64 KiB default stack. The web
   platform now requests a 1 MiB stack, matching the native Windows scale at
   the platform boundary without changing shared engine call chains.
2. The temporary common-startup prefix stopped before native's `version` and
   `shortversion` dvar registration. The post-mount continuation now restores
   those canonical registrations in their native order, after common command
   setup and before script initialization.
3. `CG_DrawVersion` measured the version string correctly but passed the dvar
   union's integer member as a text pointer for both draws. Both shared call
   sites now use `version->current.string`.
4. Worker failures now forward their Wasm stack to the launcher log. This is
   error visibility at the platform boundary, not recovery policy or engine
   state.

## Focused proof

```text
npm.cmd run build:web:diagnostics
PASS — diagnostics link and strict 14-stage runtime-prefix check

KISAK_WEB_TEST_PORT=8036 playwright test tests/browser/retail_ui_persistence.spec.mjs --workers=1
PASS — 1 test, 2.1 min
```

Using legally owned local retail files, the browser run proved:

- Airplane created `kisak_web_ui_test.svg` through `devsave` and canonical
  `G_WriteGame`;
- the active profile's shipped save feeder listed and selected the exact save;
- a different temporary profile and the initial browser profile could neither
  list nor open that save;
- the shipped `save_load_menu` executed its real `Loadgame` menu script;
- restored health, owned weapon, ammo, objective hash and active/completed
  counts matched exactly;
- restored origin remained within eight engine units, without movement input,
  routes, or waypoint playback;
- after a full page/Worker restart, `loadgame_continue` reopened the same save
  and restored the same state; and
- canonical deletion removed only `kisak_web_ui_test.svg`, after exact-name
  selection and existence checks.

The loose-image screenshot path is not available in the browser target. The
canonical save record therefore has no `imageName`, and the shipped menu keeps
its normal unknown-save image fallback. Functional save/load does not depend on
a proprietary screenshot.

## Scope

This completes the requested canonical SP UI and persistence convergence
milestones. No proprietary assets, local installation paths, screenshots, or
generated build products are recorded in the repository.

## Final verification ledger

The final handoff used the diagnostics site only for browser boundary tests and
then rebuilt the production Release site from source:

```text
tools/build_web.ps1 -Configuration Release
PASS — 292 objects linked; 14-stage runtime prefix stopped at
       DB_LoadXAssets/engine-filesystem-mount

npm.cmd run test:browser:product
PASS — 40 tests

npm.cmd run test:browser
PASS — 12 tests (isolated diagnostics server)

npm.cmd run test:browser:remainder
PASS — 36 tests, 3 opt-in retail tests skipped
```

The smoke runtime-prefix assertion now records all 16 commands deliberately
linked by the production diagnostics target. The direct strict Wasm prefix
continues to assert its smaller six-command closure. The Gate 3 retail census
was also aligned to the current exact canonical trace: 9,635 publications,
including 9,634 named publications and one empty-name rawfile publication.
This removes two obsolete duplicate empty image expectations without relaxing
the census.

Two unrelated repository-wide checks remain red and were not hidden:

- `npm.cmd run check:web:static` passes syntax and lint, then strict types finds
  seven existing implicit-`any` errors in `web/worker_transport.mjs`.
- `npm.cmd run test:protocol` passes 80 of 81 tests; the existing renderer
  workload comparison still fails its `sunShadowMergedRanges` assertion in
  `tools/renderer_workload.mjs`.

An opt-in Gate 3 retail rerun passed the corrected complete startup census and
then timed out in its older downstream requirement for a drawn Killhouse scene
frame. That renderer proof is separate from this UI/persistence milestone. The
focused canonical Airplane save/load test passed end to end. No headed retail
screenshot was retained; this evidence intentionally records semantic traces
only.
