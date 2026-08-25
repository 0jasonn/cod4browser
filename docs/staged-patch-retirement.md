# Retired staged patch

`src/staged_changes.patch` was an unowned scratch patch rather than a build
input. It mixed unrelated native changes across client, multiplayer, game,
script, and D3D renderer code, had no recorded source commit, and had no
validation record. Keeping it under `src/` made its ownership and application
status ambiguous, so the web product cleanup removed it without applying it.

The potentially useful ideas preserved from the patch are:

- use the platform `CPUSTRING` in native version and update strings;
- express config-string bucket ends relative to their starts and keep the PC
  rumble bucket explicit;
- return builtin function pointers only after a bounded table match;
- retain the result of `R_AddBModelSurfaces` while adding shadow surfaces.

These are native correctness/portability candidates, not browser platform
requirements. If pursued, each should be recovered from repository history,
implemented as its own reviewed change against current upstream code, and
validated on the affected native target. None is required by or silently
folded into the web cleanup.
