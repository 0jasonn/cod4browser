# Canonical objective notification — 2026-09-01

## Result

The normal Killhouse server-spawn lifecycle loaded the shipped cgame menus;
`objectiveinfo` was present in `cgDC`. A diagnostics-only test used unused
objective slot 15 and the freely usable text `Kisak web objective test`.

The test set the server configstring, mirrored the received client
configstring using the same string-table operation as
`CL_ConfigstringModified`, and called `CG_ParseObjectiveChange`. State 4
(current) and state 3 (completed) both appeared in canonical
`objectiveInfo_t`. `CG_MenuShowNotify(5)` drove `objectiveinfo`, the canonical
localizer returned the literal test text, and the renderer frontend observed
that full text before glyph expansion. Advancing only the diagnostic clock
proved `CG_CheckHudObjectiveDisplay` and `CG_FadeObjectives` hide the notice at
the native timeout.

The diagnostic entry points are excluded from production. No product mutation
API, HTML overlay, browser objective record, retail content, route playback, or
simulated locomotion was added. Natural objective visual evidence and
save/load survival remain to be covered by later save workflow evidence.

## Checks

- Release-diagnostics build and 14-stage runtime-prefix check: passed.
- `tests/browser/retail_ui_persistence.spec.mjs`: 1 passed in 36.6 seconds.
