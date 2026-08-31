# Paused command-copy qualification

Recorded 2026-08-31. **The workload failed geometry qualification.** Diagnostic
profiling and comparison checks are delivered; the seven-line name-copy
optimization was reverted. No production performance improvement is claimed.
The [numeric record](paused-copy-qualification-cd85e18e.json) retains all three
runs, aggregate profiles, hashes, counters and rejected comparisons.

## What is now checked

The existing runner supports `fixedtime` for Release diagnostics as well as
production. Both builds use the same canonical command schedule and camera/time
checks described in [the previous milestone](controlled-renderer-552a468d.md).
The diagnostic host uses its existing probe request to submit those commands;
no engine command, state representation or production telemetry was added.

After the profiler-disabled views 240–540, the diagnostic runner schedules the
existing 120-frame profiler at view 600. Captured views **601–720** must all match
the frozen scene's camera, time, projection and world inventory. It records each
frame's world/static/dynamic/FX/mark/shadow counts, submitted indices and uploaded
buffer bytes. A separate comparison rejects differences in these work counts,
even when all six coarse camera checkpoints match.

## Measurements and rejection

All 120 samples within each run had constant work counts. All three runs also
matched the same camera/time trace. Between runs, however, geometry differed:

| Run | Submitted indices/frame | Uploaded bytes/frame | Batch retention mean | Total diagnostic CPU mean |
| --- | ---: | ---: | ---: | ---: |
| Baseline `cd85e18e` | 4,814,274 | 5,606,784 | 0.868 ms | 23.925 ms |
| Candidate `cfb4fbd4` | 4,816,326 | 5,638,320 | 0.800 ms | 24.281 ms |
| Same candidate, fresh load | 4,814,418 | 5,626,992 | 0.862 ms | 29.559 ms |

Every run drew 5,471 world surfaces, 1,872 static-model instances, 1,323 dynamic
batches and 16,496 shadow casters per frame. Thus equal draw counts and camera
traces did not establish equal geometry work. The two candidate runs had the
**same Wasm SHA256**, yet different index/upload totals. The second comparison
therefore reproduces fresh-load variation without changing the executable.

These timings describe the individual windows; they cannot establish a causal
speedup or slowdown. No run was discarded to find a matching subset. The exact
source of geometry variation is not established. Canonical map initialization
still seeds ordinary game state from time, but seed, model/LOD and save/replay
behavior need investigation before attributing this result to one mechanism.

Baseline command copying cost 1.307 ms: 0.225 ms geometry validation, 0.207 ms
geometry copying and 0.868 ms batch retention. The last interval also contains
batch validation, names, image lookup and fallback selection; it is not a pure
memory-copy timer. It does not justify a new cache or allocation policy alone.

The candidate removed the private backend's unused vertex-shader-name string
and world/dynamic technique-name assignments. Used shader hashes, pixel names
and static fallback technique logging were preserved. Because qualification
failed, `b059a212` reverted the candidate before production comparison. Runtime
sources and build configuration now match the milestone's initial `cd85e18e`.
Camera culling, static-model visibility, independent shadows, validation and
atomic publication are unchanged.

## Verification and reproduction

- The existing focused Node fixture was extended for all 120 profile views,
  clock/camera drift, missing samples, invalid counters and changed geometry
  with otherwise identical camera/draw state. It passed twice as checks were
  refined; final result 1/1, 62.998 ms. Runner syntax checks passed.
- Two diagnostic Release builds passed (15.697 s and 12.675 s). Three owned
  CargoShip runs completed, each with 300 profiler-disabled intervals and 120
  diagnostic samples, valid focus/visibility and zero page errors. Both
  cross-run work comparisons were rejected as described above.
- The final restored production Release passed in 15.513 s, including the
  existing 14-stage runtime-prefix check. Its Wasm hash is
  `336062d9e26c3f7a0d2d35b94bfe87182eff1c838f444109d03250c713b8ff21`.
  This rebuilt artifact is compile evidence; it was not separately browser-run.
  Existing compiler/toolchain warnings remain.
- Production A/B/B/A runs were not performed after diagnostic qualification
  failed. No broad suite, mission/progression check, screenshot or compatibility
  promotion ran. All task browser contexts and the server closed. The ignored
  diagnostic site still contains the labeled experimental candidate; rebuild it
  before measuring the restored runtime. No assets, retail logs or installation
  paths are committed.

With `KISAK_COD4_RETAIL_ROOT` set locally, build Release diagnostics, serve that
generated site on port 8051, and use:

```powershell
node tools/profile_web_renderer.mjs LABEL diagnostics BUILT_COMMIT build/web-diagnostics/site-diagnostics fixedtime
node tools/renderer_workload.mjs --profiles DIAGNOSTIC_A.json DIAGNOSTIC_B.json
```

The ordinary production comparison still checks only its available camera/time
and world-inventory telemetry. Passing it is a prerequisite, not proof of equal
dynamic geometry; qualify paired diagnostic workloads before interpreting a
future production optimization comparison.

Next: trace the differing canonical model/LOD submissions and establish a
repeatable canonical seed/save/replay state. Avoid additional copy optimizations
or browser-owned game-state substitutes until index and upload workloads match.
