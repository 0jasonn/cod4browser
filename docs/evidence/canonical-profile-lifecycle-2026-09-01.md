# Canonical player-profile lifecycle — 2026-09-01

## Outcome

The browser runtime now completes the shared Kisak single-player profile
lifecycle: shipped UI list/feeder code creates and selects profiles, each
profile owns its archived `config.cfg`, `profiles/active.txt` restores the
selection after a complete page and Worker restart, and the normal UI delete
path removes a non-active profile durably.

No JavaScript profile model, config format, or profile-selection state was
added. Diagnostics use fixed underscore-only synthetic names because the
canonical active-profile parser accepts identifier tokens; hyphenated test
names would be tokenized at the first hyphen and are not valid restart probes.

## Earliest failures and fixes

1. Profile switching reset a dvar with a custom domain callback through a
   decompiler-shaped function-pointer cast. Wasm requires the callback's actual
   typed `DvarValue` ABI, so `Dvar_SetVariant` now calls `domainFunc` directly.
   The strict runtime-prefix test exercises this callback on every diagnostics
   build.
2. The browser startup continuation had not registered and checked
   `com_recommendedSet`; the shared profile-change owner now reaches its normal
   recommendation check without a null dvar.
3. `Sys_RemoveDirTree` was an inert web stub. It now delegates to a single
   synchronous Worker filesystem operation which updates the cache immediately
   and schedules ordered recursive OPFS deletion. The engine still owns the
   exact profile path and deletion decision.

## Focused proof

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build_web.ps1 -Diagnostics
PASS — diagnostics link; strict 14-stage Wasm runtime-prefix test, including typed dvar-domain callback

node --test --test-name-pattern="browser home removes a profile tree durably" tests/node/worker_sync_filesystem.test.mjs
PASS — 1 matched test

KISAK_WEB_TEST_PORT=8021 playwright test tests/browser/retail_ui_persistence.spec.mjs
PASS — 1 test, 1.0 min
```

The legal-retail browser run proved:

- the shipped `player_profile` menu and profile feeder were active;
- two synthetic profiles were created and enumerated beside the initial
  browser profile;
- archived values `101` and `202` stayed isolated across selection changes;
- the active marker and selected profile B survived a page/Worker restart;
- all three directories survived restart before deletion; and
- deleting non-active profile A removed it while profile B stayed active with
  its archived value.

## Scope

This establishes profile lifecycle and config isolation for the offline SP
browser runtime. Save list/load/delete continuity and save isolation between
profiles remain the next milestone. No proprietary files or host paths are
recorded in the repository.
