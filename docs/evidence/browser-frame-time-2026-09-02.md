# Real-time browser frame progression

The browser pump previously admitted at most 100 ms per frame, and the native
`com_maxFrameTime=100` applied the same limit again. Frames below 10 FPS thus
lost elapsed time even with normal timescale settings. Browser startup now
selects `com_maxFrameTime=5000`, and the platform accumulator retains elapsed
time up to that long-stall ceiling. Native defaults are unchanged.

This complements the separate [canonical slow-command conversion fix](canonical-timescale-2026-09-02.md).
The live probe found `timescale=1`, `com_timescale=1`, and `fixedtime=0`; the
near-frozen game was not caused by a retained profiling dvar. Raising the frame
cap alone still produced only 50 ms of game progression over about eight
seconds, before correcting the script-scale conversion.

## Browser evidence

The opt-in retail UI/persistence test now adds twelve bounded 250 ms Worker
stalls after its existing pause/resume checks. It observes canonical
`level.time` against browser elapsed time, with no simulated movement or
timescale overrides. The final Chromium run measured **4700 ms game time over
4948.005 ms elapsed** (95.0%). The 15% tolerance covers frame-quantized snapshots
and asynchronous endpoint sampling; it is not a performance/FPS claim.

The full test passed, including main-menu startup, menu rendering, synthetic
objective notification, pause/resume, profile isolation, and save/reload
persistence. An early probe placed before the objective check delayed that
time-sensitive fixture; the final probe runs after the existing UI checks,
without changing any of their assertions. Test-owned browser profiles are
temporary; no user profiles or saves were modified.

## Final checks

- Direct-Wasm `cgame_timescale_tests`: four exact scalar cases passed.
- Release diagnostics build and runtime-prefix check: passed.
- Opt-in `retail_ui_persistence.spec.mjs`, one Chromium worker: 1 passed.
- `npm.cmd run test:browser`: 12 passed.
- `npm.cmd run test:browser:remainder`: 37 passed, 3 optional retail tests
  skipped with the retail-root environment variable unset.
- Production Release build and runtime-prefix check: passed.
- `npm.cmd run test:browser:product`: 40 passed in Chromium.
- Changed browser-test syntax and `git diff --check`: passed.

Scripted slow motion, developer timescales, explicit profiling `fixedtime`,
the native server-load control, and muted audio startup remain intact. Gaps
longer than five seconds are still capped; this is a suspension safety bound,
not a promise to simulate unlimited background time. Retail timing evidence
uses the diagnostic build; no new production retail visual claim is made.
