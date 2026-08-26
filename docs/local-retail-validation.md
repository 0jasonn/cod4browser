# Local retail validation

This validation is local-only. It reads files from a legally owned English
Call of Duty 4 installation through the browser picker; it does not copy them
into the repository or CI artifacts. Close other cod4browser tabs first so the
test can acquire the exclusive writable-home lease.

After the pinned toolchain, npm dependencies, and Chromium are installed, run:

```powershell
.\tools\validate_web_retail.ps1 -RetailRoot 'D:\Games\Call of Duty 4'
```

The command builds the diagnostic artifact because forced WebGL context loss
is deliberately unavailable to the production protocol. The test otherwise
uses the normal importer and canonical runtime. It requires both
`killhouse.ff` and `cargoship.ff` and checks:

- installation import, persistence, and canonical mount;
- canonical `map killhouse` completion through `CG_Init`, a real world frame,
  and a 60-second stability window;
- visible W/A/S/D movement, jump, mouse look, primary-fire audio, secondary aim,
  wheel pulses, Escape/menu, and pointer-lock recovery through the canonical
  input queue;
- checkpoint duration and persisted-byte evidence after `writeconfig`;
- an in-process `killhouse` → `cargoship` transition with ordered unload begin,
  unload end, and new-world publication events on one WebGL context;
- the same CGame/frame/stability/input/audio/checkpoint checks on `cargoship`;
- forced WebGL2 context loss, recovery, resumed CargoShip frames and input;
- heap, renderer recovery/GPU/geometry/upload/program, frame-percentile, audio,
  transition-peak, shutdown-flush, and reload evidence in one non-proprietary
  `KISAK_RETAIL_RESULT` JSON record.

The command is intentionally skipped unless `KISAK_COD4_RETAIL_ROOT` is set.
Record the exact commit, browser version, matrix result, and renderer-memory
events before changing the recovery budget or compatibility matrix.

## Phase 3 campaign map

After the two-map matrix and memory review pass, validate the first additional
campaign map with:

```powershell
.\tools\validate_web_campaign_map.ps1 `
    -RetailRoot 'D:\Games\Call of Duty 4' `
    -Map blackout
```

The wrapper currently accepts only `blackout`. It starts with a fresh browser
profile, reaches a CargoShip world frame, transitions into Blackout, verifies
the exact canonical DB and runtime lifecycle, runs Blackout for at least 60
seconds, exercises gameplay input/audio/config persistence, records memory,
forces WebGL2 context recovery, and transitions out to Killhouse before clean
shutdown and reload. It emits one non-proprietary
`KISAK_RETAIL_PHASE3_RESULT` JSON record. This opt-in test is not registered
without an explicit campaign-map request, so routine browser-suite skip counts
and the authoritative Killhouse-to-CargoShip matrix remain unchanged.
