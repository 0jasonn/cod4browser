# Web product status

## Demonstrated

The production Release artifact is a Worker-hosted offline single-player
slice. With locally supplied, legally owned English assets, the canonical
runtime has been observed through DB publication, ClipMap, server/game, local
client/cgame, renderer-frontend commands, actual WebGL2 world frames, HUD,
keyboard/mouse gameplay, effects, Web Audio, configuration checkpoints,
in-process map transitions, context recovery, and clean shutdown/reload.

The current clean retail reference consists of the headed Chrome/Edge baseline
at `f5229806` and the campaign batch at `247980a6`. Six maps are validated:

- `PLAYABLE` (2): Killhouse and Airplane.
- `FUNCTIONAL` (4): CargoShip, Blackout, Hunted, and Bog A.
- `UNTESTED` (16): other discovered direct SP zones.
- `BLOCKED`/`REGRESSION` (0).

Every promoted map passed canonical DB, ClipMap/world, server, game, cgame,
first-frame, valid foreground 60-second, input, audio, checkpoint, transition,
context-recovery, and no-fatal-error checks. `FUNCTIONAL` means the complete
runtime matrix passed but the map missed the documented reference performance
threshold. Discovery alone never advances compatibility. Exact values are in
the [campaign matrix](campaign-compatibility.md),
[execution report](../WEB_ROADMAP_EXECUTION_REPORT.md), and sanitized
[foreground](evidence/retail-foreground-f5229806.json) and
[campaign](evidence/retail-campaign-247980a6.json) records.

Encoded-source recovery from `c66d41e1` is active. At the same Killhouse point,
aggregate CPU recovery fell from 1,417,257,708 B to 506,423,759 B (64.27%),
allocator use by 52.59%, Wasm capacity by 45.28%, and program break by 50.14%.
The cost is longer successful context re-decode: +57% on CargoShip, +94.75% on
Blackout, and approximately +90% on returned Killhouse. Killhouse first frame
increased 14.43%; CargoShip was approximately flat. The historical
background-throttled approximately 1 FPS timings are not current performance.

Production contains only named Worker operations and a versioned protocol. The
writable home filesystem has one cross-tab owner and an awaited, retryable
flush/unmount path. The separate diagnostics artifact uses the same runtime
sources plus browser-only controls and telemetry. Gate 2, parallel retail
loaders, proof jobs, synthetic production worlds, and the obsolete scheduler
remain retired.

## Current pass

An explicit legal retail root was supplied for the current headed Chrome pass.
Commit `9e75a9dd` implements the one evidence-ranked renderer change: opaque
sun/spot shadow casters no longer perform texture and sampler binds that the
shadow fragment shader cannot consume. Across six valid foreground profiles,
average shadow-draw CPU time fell 36.91%, texture binds fell 32.49%, and total
backend CPU time fell 6.56%. All six maps passed their 60-second runtime,
gameplay input, audio, configuration checkpoint, transition, context recovery,
and shutdown/reload gates. The sanitized profile is
[retail-profile-9e75a9dd.json](evidence/retail-profile-9e75a9dd.json).

The change does not close the performance gap. CargoShip measured 11.94 FPS
with an 83.74 ms average frame and 45.61 ms backend CPU time; Blackout, Hunted,
and Bog A also remain below the reference `PLAYABLE` threshold. After the
shadow reduction, measured backend costs are led by shadows/world geometry
(7.14/7.11 ms six-map averages) and static models (6.48 ms); CargoShip's static
models alone average 15.98 ms.

The representative mission objective/AI/combat/death/save-reload gate passes
on Airplane at clean `da1e592c`. A diagnostics-only canonical-state probe and
opt-in headed validator observed live actors/scripts, an active objective,
combat damage, a natural checkpoint, a named game save, death/restart, browser
shutdown, fresh-runtime load, restored state, and continued play. See
[retail-mission-da1e592c.json](evidence/retail-mission-da1e592c.json).
Configuration checkpoints on the other maps remain configuration evidence,
not gameplay save/load evidence.

## Product boundaries and gaps

- WebGL2 is the platform backend; Kisak owns assets, gameplay, filesystem
  semantics, server/client/cgame, and renderer frontend state.
- Imported retail files stay in browser-private storage and never enter CI or
  repository fixtures.
- Native Bink, Miles, Steam, raw UDP, and native DLLs are not shipped;
  cinematics currently complete as explicit visible omissions.
- Gamepad, full cinematics, advanced audio parity, 16 campaign zones, broader
  mission coverage, and remaining renderer/material families remain.

The approved production boundary is 3,173,694 B Wasm (3,332,379 B budget),
340,615 B application JavaScript (357,646 B budget), and 3,524,840 B across 17
site files (3,701,082 B budget), with 24 raw Wasm exports and 9 named application
exports. No file/export allowlist is loosened.
