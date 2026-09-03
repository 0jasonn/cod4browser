# Browser Quit lifecycle — 2026-09-02

## Behavior and ownership

`Com_Quit_f` no longer routes the canonical Quit command through `ERR_DROP`.
It requests shutdown through `web_system`; the Emscripten frame callback
unwinds, cancels its main loop and calls canonical `Com_WriteConfiguration`.
The existing runtime event tells the host to stop input and audio. A shared
launcher dialog flushes the existing Worker filesystem before disposing the
asset store and Worker. Start game reloads a fresh Worker and remounts the
retained installation. Game, script, save and profile state stay in Kisak.

The stopped Worker retains pending writes on a retryable storage failure.
The dialog reports the error and offers retry; it cannot be dismissed into a
stopped game. Unknown Worker ownership still follows the existing terminate-
before-release protocol and reports that saving could not be confirmed.
Diagnostics checkpoint before their deliberately destructive unmount operation;
their existing termination assertions are preserved.

Worker termination replaces native process/heap teardown. No asynchronous
operation is inserted into shared engine code, and no new Wasm export or Worker
operation is added. `Com_Shutdown` outside Quit remains an unsupported native
process path. Quitting does not create a new campaign checkpoint automatically.

## Verification

Pinned Release production and diagnostic builds and runtime-prefix checks pass.
Static checks and all 81 protocol tests pass. Isolated Chromium 149.0.7827.55:

- Production tier on port 8018: 43 passed. New checks run canonical Quit after
  a synthetic installation mount, confirm `snd_volume 0.37` written in the same
  command submission reaches OPFS config, verify frames stop and writer/import
  leases release, then restart and remount. An injected save failure verifies
  no disposal before retry and that Escape cannot dismiss the stopped state.
- Diagnostic port 8144: 12 smoke and 40 remainder passed, 5 optional retail
  cases skipped. Inherited retail variables were explicitly cleared.
- Product API/size check: 24 raw exports, 9 application exports; Wasm 3,311,097 B,
  JavaScript 348,716 B, complete site 3,670,865 B. Existing budgets pass unchanged.
- Separate owned-installation Chrome 152.0.7977.65, persistent headless profile,
  diagnostic site on port 8143: the complete main-menu/persistence test passed
  in 2.1 minutes. It covers Killhouse input/pause/resume, settings and profiles,
  Airplane save/load and Continue after reload, then canonical Quit from the
  active Airplane runtime and restart into the main menu. Its objective probes
  are synthetic, and the restarted Worker uses the same browser process.

Production Wasm SHA-256:
`3fa2aa034fe39f8edcd5a36bfc9331f217ac164fff2822121627c157b29677f0`.
Diagnostics Wasm SHA-256:
`7820e1656ed02e58643b3c2485f9e0716fe12044a6a3ee3d96f8d80b2290857a`.
Private build/test logs are `build/goal-quit-*.log`; fixtures and browser output
remain ignored. No proprietary files are included in this evidence.

## Reference limits

This replaces a browser error with the desktop game's expected Quit outcome,
adapted to a page that can start another Worker. The original/native confirmation
menu's exact presentation has not been compared. Shipped-menu mouse navigation,
active-campaign checkpoint durability across a new browser process, mission
completion and retail-wide fidelity remain separate acceptance work.

A trial using a newly created `seta` variable found canonical `Dvar_AddFlags`
does not mark the global archive dirty bit. That variable alone did not trigger
`Com_WriteConfiguration`; changing an existing archived menu setting does. The
regression test uses the real volume setting, and the shared native dvar behavior
is unchanged. A cross-platform correctness fix would need separate evidence.
