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
