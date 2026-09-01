# Canonical dvar and config persistence — 2026-09-01

## Outcome

The browser runtime now continues the native dvar, key-command, profile-config,
and configuration-write lifecycle instead of stopping at the earlier compile
checkpoint. No JavaScript dvar registry, config serializer, or binding parser
was added.

## Earliest failures and fixes

1. The production Wasm object still selected the strict prefix oracle's reduced
   dvar-command table. A production-only compile definition now restores the
   full existing `Dvar_AddCommands` table; the strict Node prefix oracle remains
   deliberately limited to startup `set`/`seta`.
2. The post-mount continuation skipped native `CL_InitKeyCommands` immediately
   before `FS_InitFilesystem`. Restoring it at that point lets profile configs
   replay canonical `bind` commands.
3. The custom frame loop did not call `Com_WriteConfiguration`. The shared
   owner now checks `com_fullyInitialized` and `DVAR_ARCHIVE`, builds the active
   profile path, and delegates to existing `Com_WriteConfigToFile`.
4. A fresh browser install had only an in-memory profile name. The profile
   owner now creates the `browser` directory and `players/profiles/active.txt`
   only when no valid active profile exists. Existing active profiles win.
5. Profile config execution originally ran without the active config path and
   before retail RawFiles were published. The post-fastfile startup pass now
   executes the active profile's canonical `config.cfg` after those assets are
   available.

## Focused proof

Build:

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build_web.ps1 -Diagnostics
PASS — production diagnostics link and strict 14-stage runtime-prefix check
```

Legal-retail browser test:

```text
KISAK_WEB_TEST_PORT=8035 playwright test tests/browser/retail_ui_persistence.spec.mjs --project=chromium
PASS — 1 test, 49.0 s (49.9 s total)
```

The test proved:

- bool, int, float, string, enum, vec3, and color registrations, plus
  representative ROM, cheat, archive, and latch flags;
- real command-form `seta`, value-sequence `toggle`, `reset`, and `bind`;
- a non-empty active-profile `config.cfg` written by normal frames;
- the archived value and `F9` binding after a complete page/Worker restart;
- no missing dvar from the reached shipped menu set except `ui_sp_unlock`.

`ui_sp_unlock` is intentionally not invented here. Stock COD4 1.7 console
traces emit the same `openmenuondvar: cannot find dvar ui_sp_unlock` warning at
the shipped main-menu script, so it is classified as a native dangling retail
reference rather than a browser compatibility dvar.

## Scope

This proves configuration semantics and restart persistence for the current
offline SP browser runtime. Profile switching/isolation and save/load UI are
separate subsequent milestones. Native D3D9/Win32, Miles, Bink, Steam,
multiplayer, and dedicated-server controls remain classified in the convergence
inventory rather than represented by inert or guessed browser dvars.
