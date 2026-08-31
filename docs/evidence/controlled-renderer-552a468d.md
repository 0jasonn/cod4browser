# Controlled paused renderer timing

Recorded 2026-08-31. Runtime `552a468d`; runner `578e2194`. This qualifies a
**paused renderer benchmark**, not active gameplay performance or display FPS.
The [numeric record](controlled-renderer-552a468d.json) includes all setup
trials, rejected windows, artifact hashes and the two qualified runs.

## Result and boundary

Two fresh CargoShip loads produced exactly matching six-checkpoint camera,
scene-time, projection and submitted-world-geometry traces. The corrected
artifact was measured before and after an incompatible retained candidate:

| Execution order | Mean callback interval | p95 | Qualification |
| --- | ---: | ---: | --- |
| Corrected baseline A1 | 26.655 ms | 29.070 ms | Passed |
| Retained `2b86c7c7` candidate | Excluded | Excluded | Ignored fixedtime: warmup advanced 1,089 ms instead of 480 ms |
| Corrected baseline A2 | 26.134 ms | 28.950 ms | Passed; trace exactly matches A1 |

This is an A / rejected legacy B / A qualification, **not a successful
before/after optimization comparison**. Both valid runs used the same Wasm:
`ca35f9027230958b43735ea1ddd53ec2a92d7d296e208330a98bf40c1c53b083`.
The roughly 2% spread is observed run variation, not an optimization gain.
Do not compare these numbers with earlier moving-camera, active-server windows.

Existing sparse production telemetry checks canonical view generations, scene
time, origin/forward, viewport/FOV/near plane and submitted world counts. It
does not prove equality of every entity pose, particle, dynamic batch or shadow
caster. Canonical map initialization still uses its ordinary random seed.
Camera DPVS, static-model culling, draw order and independent sun/spot shadow
paths were not changed. No playability or retail compatibility promotion follows.

## Implementation

The browser pump omitted native `Com_ModifyMsec`. It now calls that function
after nonblocking wall-clock admission and before `SV_Frame`, as `Com_Frame`
does. The original function was moved outside the temporary common.cpp compile
gate so native and browser paths share one body. Fixedtime, time scaling and
clamping remain Kisak-owned; no new clock or browser-specific game state exists.

The existing profiling runner gained an optional production `fixedtime` mode:

1. Load with the normal `devmap cargoship; fixedtime 16` command. Devmap enables
   the cheat dvar through its canonical command path.
2. At view 30, queue `cg_ufo`. At view 60, queue
   `cl_paused_simple 1; pause`, avoiding the pause menu. Assert the intervening
   scene time advanced exactly 480 ms.
3. At views 120 and 180, queue `cg_setviewpos -9732 -9384 2041 73 16`.
   The second placement corrects the relative debug-angle offset after the
   first placement renders. Cgame owns the resulting view-height adjustment.
4. Measure 300 intervals between completed Worker callbacks for views 240–540.
   Six existing refdef checkpoints must remain at scene time 1259 ms and camera
   origin `[-9732, -9384, 2101]`, with unchanged direction. Pair comparison also
   requires exact checkpoint and environment equality.

Scheduled setup commands enter the existing validated production Worker
request handler directly at its view events, for execution on the next pump.
This removes DOM/Worker message latency from the choice of paused frame. There
are no diagnostic exports, Wasm memory edits, extra product telemetry, new
dependencies or commands during the measured window. The ordinary command UI
still starts the map. The runner verifies the served Wasm matches the selected
local artifact before loading assets.

Each run uses a fresh Playwright-managed Chrome profile, the owned local
installation picker, a 1440x1000 viewport and headless Chrome 152.0.7977.64.
Intervals use Worker `performance.now`; all 301 sample timestamps must have
consecutive pump generations. Focus/visibility, inactive profiler and zero page
errors are required. Sparse system telemetry can omit very fast callbacks;
such a run fails qualification rather than silently dropping intervals.

## Investigation and validation

Fixedtime alone passed scene-clock checks but two fresh loads had different
authored camera paths. Free-camera trials then exposed placement transitions,
relative-angle offsets and later camera shake. These active windows are retained
as setup evidence only. One second-placement attempt submitted an emptied UI
form; that setup error was corrected. The final paused mode removes ongoing
camera motion through canonical pause/free-move behavior. Assertions were not
relaxed to admit the failed windows.

One focused Node fixture covers missing checkpoints, bad fixedtime, unpaused
scene time, moving/nonfinite cameras, geometry/environment differences,
incomplete sampling, diagnostic/profiling contamination and page/focus errors.
It passed on all five focused invocations as the workflow was refined; final
result 1/1, 57.301 ms. Runner syntax and diff whitespace checks passed.

The first Release attempt failed because the common compile prefix excluded
`Com_ModifyMsec`. Moving the existing body outside that gate fixed the link.
The targeted Release retry passed in 15.454 s, including the 14-stage runtime
prefix check. Existing toolchain/compiler warnings remain. The delivered build
is from `552a468d`; subsequent runtime edits remove whitespace only. The stale
diagnostic artifact was not used or relabeled. No broad suite, mission check,
capture or new optimization ran. All task browser contexts and servers closed.

## Reproduction and next task

Set `KISAK_COD4_RETAIL_ROOT` locally; never record its value or commit assets.
Serve the selected generated site on port 8051:

```powershell
python tools/serve_web.py --directory build/web/site --port 8051
node tools/profile_web_renderer.mjs LABEL production BUILT_COMMIT build/web/site fixedtime
```

For a future candidate, retain both generated sites built with the same timing
fix and use A/B/B/A order, with no overlapping builds or benchmarks. Point the
server and runner directory to the same artifact each time. Reject mismatched
workloads before interpreting timing:

```powershell
node tools/renderer_workload.mjs RUN_A1.json RUN_B1.json RUN_B2.json RUN_A2.json
```

The older `build/brush-candidate-2b86c7c7` must not be treated as a compatible
controlled baseline. Its rejection does not reverse the previous decision to
discard the brush optimization.
The qualified production site is also retained locally at ignored
`build/renderer-control-552a468d`; its Wasm hash matches the delivered site.

Next: align diagnostic profiling with this paused window, inspect actual
dynamic work counts, and only then evaluate a focused command-copy optimization
using qualified interleaved production runs. Active-gameplay timing still needs
its own deterministic state/replay solution; this tool does not claim one.
