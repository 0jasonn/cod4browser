# Canonical SP menu lifecycle — 2026-09-01

## Result

A fresh isolated browser profile imported a legally owned installation and
reached the shipped main menu without a map command. `cls.uiStarted` was 1,
the canonical UI owned 69 menus, `main` was present/open/visible, and the
renderer submitted a non-empty canonical 2D scene (220 quads in the observed
first scene).

The same run opened and closed `main_options`, `player_profile`, and
`save_load_menu`, loaded Killhouse, found `pausedmenu` and `objectiveinfo` in
the cgame menu set, and paused/resumed with Escape through the browser input
queue. `cl_paused` and the UI key catcher followed the canonical state.

## Earliest fixes

- The browser frame pump had rejected every disconnected frame, preventing
  `CL_Frame` from selecting the main menu and `SCR_UpdateScreen` from drawing
  it. It now gates on the initialized client owner instead of in-game state.
- SP registered `openmenu` by casting an `int()` function to the command
  system's `void()` callback. Wasm's typed table rejected the call. SP now uses
  the same `void()` signature as MP, with no cast.

No HTML game menu, retail asset, path, screenshot, generated build product, or
browser-owned UI state was added.

## Checks

- Release-diagnostics web build and 14-stage runtime-prefix check: passed.
- `tests/browser/retail_ui_persistence.spec.mjs`: 1 passed in 32.3 seconds.

Objective state/text timing, archived settings, profile switching/isolation,
and save-list workflows remain the next milestones.
