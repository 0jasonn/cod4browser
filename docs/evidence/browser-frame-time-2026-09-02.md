# Real-time browser frame progression

## Current production measurements — 2026-09-05

The fullscreen/size/material audit measured the production Wasm with SHA-256
`8e8319bedd9b72266f13779ba09ddf45a6c4311505a060583b154382abd6565b`.
Chrome 152.0.7977.77 ran headed on Windows, Ryzen 7 7800X3D, 32 GiB RAM and
RTX 3070 Ti through ANGLE/D3D11. Seven separate 30-second windows all remained
visible and focused at every 100 ms observation. No compilation, other test
tier, injected objectives, player route, fixed time or pause control ran in
those windows. Map commands used only locally imported owned data in the
isolated `build/advance-material-profile`; the user's training profile was
untouched. The final run passed in 6.3 minutes.

Canonical settings were `r_mode Automatic`, automatic picmip 0 for colour,
normal and specular textures, requested 4x AA, `com_maxfps 60`, both timescales
1 and `fixedtime 0`. The render-size assertion checks the canvas itself.
Production emits one frame counter per 30 rendered frames; FPS below uses
counter deltas against host arrival time. These are aggregate render rates,
not individual-frame percentile measurements. Canonical sampled view time
advanced at 0.99984–1.00006 of wall time across all windows.

| Scene window | Render size | Render FPS | Wasm capacity MiB | GPU texture estimate MiB | CPU recovery estimate MiB | Decoded audio MiB |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Killhouse | 1280×720 | 59.99 | 978.5 | 1179.9 | 434.7 | 5.8 |
| Killhouse | 1920×1080 | 60.00 | 978.5 | 1199.6 | 434.7 | 5.7 |
| Killhouse after real context recovery | 1920×1080 | 60.00 | 978.5 | 1199.6 | 434.7 | 5.7 |
| CargoShip | 1280×720 | 20.63 | 978.5 | 693.8 | 282.9 | 15.9 |
| CargoShip | 1920×1080 | 17.28 | 978.5 | 749.6 | 294.3 | 15.7 |
| AC130 | 1280×720 | 59.75 | 978.5 | 346.5 | 118.6 | 0.8 |
| AC130 | 1920×1080 | 59.87 | 978.5 | 374.0 | 119.6 | 2.8 |

Wasm capacity comes from inspector enumeration of live `WebAssembly.Memory`
objects after each clean window. It is capacity, not allocator use. GPU/CPU
renderer values are the existing resource estimates, not measured VRAM or
whole-process RSS; do not sum them with Wasm capacity. Inspector memory queries
and screenshots occur outside the clean windows. The reverb device separately
retains its 32 MiB memory. Web Audio remained running, its device clock advanced,
room selection changed with the maps, and telemetry reported no underruns,
overruns or evictions. This does not establish listening fidelity or hardware
output latency.

The map-command-to-first-world observations were 42.58 s for Killhouse,
41.39 s for CargoShip and 30.78 s for AC130, including authored movie/loading
work. A real `WEBGL_lose_context` cycle rebuilt Killhouse from resource
generation 1 to 2; state returned to running, the context was no longer lost,
`glGetError` returned zero, and more than 60 frames resumed within the 3.29 s
observation. The following clean window retained the same memory estimates
and Wasm capacity. Host animation-callback p99 was 2.82–5.50 ms and the largest
observed interval was 13.925 ms on this high-refresh display. Opening the
accessible recovery dialog through Shift+Escape was observed in 15.99 ms.
That is host UI responsiveness, not mouse-to-render or end-to-end latency.

CargoShip remains a current performance risk. Its scripted camera and actors
continued naturally between resolutions, so the two rows do not isolate pixel
cost. Killhouse's settled view faced the floor; its capped result cannot qualify
a busy training scene. AC130 throughput does not establish thermal fidelity.
No additional optimization or threading change is justified by these aggregate
windows alone. The next performance step is a bounded CPU/GPU stage profile
of the current slow CargoShip workload with matched settings and view evidence;
long sessions, combat and individual-frame tails remain unqualified. These
measurements also cannot attribute improvement or regression to `-Oz` versus
the older, different workload/artifact.

Disposable raw observations, screenshots and the exact Playwright harness are
under ignored `build/advance-production*`; the final run log is
`build/goal-advance-production-performance-4.log`. Preliminary harness attempts
used a fixed-size profile or waited for diagnostic-only/absent initial events;
they are excluded. Production recovery is checked through its actual resource
lifecycle and resumed counters. Manual campaign and native visual acceptance
remain unchanged.

## Historical timing correction

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
