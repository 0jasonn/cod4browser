# Renderer CPU-efficiency milestone

Completed 2026-08-31, from `53804772`. Runtime implementation: `e4db91df`.
The numeric record (archived in Git) retains every successful
window, including intermediate results and separate production measurements.

## Delivered changes and invariants

- Dynamic projection uploads occur only when the selected matrix changes;
  depth range is set once per ordinary/depth-hack camera pass.
- World, static-model and dynamic draws reuse identical material state within
  their own pass. The key covers every per-batch input read by
  `ApplyWorldMaterialState`, including canonical-state presence, both state-bit
  words, technique, source kind, ambient mode and bitwise falloff constants.
- Dynamic draws reuse unchanged shader feature flags. Detail-scale uniforms
  are uploaded only when detail mapping is enabled, and dynamic lighting
  coordinates only when model lighting is enabled. Required per-model values
  still update for each draw. Shader arithmetic is unchanged.
- Each shadow partition reuses unchanged alpha/sampling uniforms and spot
  cull mode. Texture bindings, instance offsets, caster ranges and draw calls
  remain in their original sequence.

These small backend values live on the stack. Batch and matrix contents must
remain immutable within a pass; nothing persists across frames or contexts.
Dynamic state resets around sun-query/sprite overrides and for each camera
pass. World state resets after the water fallback's direct material-mode
override. Each near/far sun or spot partition gets fresh shadow state.
Future changes to the material helper's inputs must update its equality key.

Source inspection confirms no change to canonical DPVS producers, world camera
ranges, static camera-instance counts, camera-region rejection, LOD packing,
authored spot membership, sun caster filtering, index ranges or draw ordering.
Shadow data remains independent of camera visibility. No canonical asset/game
identity was replaced, no trust-boundary validation was removed, and no GPU
resource/publication or native game implementation changed.

## Production performance, profiler compiled out

The existing `12ac17e5` production artifact is the baseline: it has the same
engine code as goal-start `53804772` (the latter only added documentation).
Its Wasm SHA-256 begins `5270c741efc80b7a`. The final production artifact, built
once at `e4db91df`, begins `94b47785e183a5f3`. Full hashes are in the JSON.
The production configuration has diagnostics disabled and its JS exports do
not contain `KisakWeb_TestBeginFrameProfile`.

Both versions used Chrome 152.0.7977.64 headless, Ryzen 7 7800X3D, a 1440 x 1000
viewport, fresh Playwright-owned persistent profiles, the same owned local
installation, and `map cargoship` without gameplay input. After the first
canonical world draw and 30 subsequent callbacks, each run recorded 300 frame
intervals using Worker `performance.now()` at completed main-loop events.
Consecutive pump ticks were asserted: the production telemetry's conditional
emission did not skip any callbacks in these windows. All windows remained
focused/visible and had no page errors. No build overlapped measurement.

| Production run | Mean interval ms | p95 interval ms |
| --- | ---: | ---: |
| Before 1 | 43.808 | 49.465 |
| Before 2 | 42.542 | 48.015 |
| After 1 | 42.711 | 49.035 |
| After 2 | 39.080 | 43.075 |

The mean of the two run means is **43.175 -> 40.895 ms**, an observed **5.28%**
reduction. The reciprocal is 23.16 -> 24.45 frames/s equivalent, not measured
display FPS. Run ranges overlap, and the after runs vary substantially. This
is evidence of a modest improvement in this workload, not a fixed gain,
statistical significance claim, 30/60 FPS promise or playability promotion.
The scene continues moving and host load is uncontrolled.

## Diagnostic attribution

`4cfedfc9` adds projection/parameter intervals and update counters to the existing
profiler. Two baseline runs were followed by two dynamic-only runs at
`65afd7ef`, then an integrated run at `e4db91df`. Each collected 300 intervals
with the profiler inactive, then 120 profiled gameplay frames. The inactive
diagnostic means were 45.031/44.838 ms before, 44.475/42.438 ms for dynamic-only,
and 40.706 ms integrated. Diagnostic wrappers and telemetry remain compiled
into that artifact; the production table above is the stronger frame-pacing
comparison. Do not combine these two measurement populations.

| Profiled CPU interval | Baseline mean of two runs, ms | Integrated mean, ms |
| --- | ---: | ---: |
| Dynamic projection setup | 0.649 | 0.303 |
| Dynamic material setup | 1.385 | 0.796 |
| Dynamic parameters, including image lookup | 1.833 | 1.084 |
| Dynamic textures | 2.347 | 2.159 |
| Dynamic draw issuance | 0.492 | 0.443 |
| Dynamic-model draw total | 7.366 | 5.400 |
| World drawing | 1.633 | 1.463 |
| Static-model drawing | 0.875 | 0.779 |
| Sun-shadow drawing | 6.752 | 5.170 |
| Spot-shadow drawing | 2.861 | 1.701 |

The new nested intervals and counters follow the existing dynamic-model bucket,
excluding FX and sun queries. Projection timing now includes its equality check;
the two hoisted depth-range calls are outside that per-batch timer. Parameter
timing includes image lookup and shader setup. Timers nest inside the parent
draw interval and must not be added to it again. Profiler overhead affects these
small intervals, so the counter reduction also matters: baseline projection,
material and feature updates each averaged 1565.013 per frame. Integrated
counts were 1, 636.567 and 638.542 respectively, for 1476.025 dynamic batches.

Workload counts are not identical. World surfaces submitted stayed 13,125;
camera-visible world surfaces averaged 2146.467 -> 2357.425, static-instance
draws 732.192 -> 764.817, and shadow draws 10122.342 -> 9883.250. The different
speed changes how far the moving scene advances in a fixed frame-count window.
The stage reductions cannot all be attributed to identical work. GPU query
results were not used to claim a GPU improvement.

## Closed opportunities and remaining limits

| Opportunity | Disposition |
| --- | --- |
| Repeated projections, material state, dynamic feature flags | Implemented and measured, with explicit override/pass resets. |
| Unused detail/model-lighting uniforms | Omitted only when the shader cannot consume them; required per-draw values retained. |
| Repeated shadow alpha/cull state | Implemented per partition; independent caster policy and texture sampling preserved. |
| Further draw merging/instancing | Not pursued: dynamic draw issuance is 0.443 ms in a 44.018 ms profiled frame. Different model-lighting coordinates, geometry and ordered camera/shadow ranges require additional batching rules; eliminating every such call would still have a small direct ceiling. |
| Broader texture/sampler or global uniform caching | Not pursued: the existing complete texture-set tracker preserves alias/last-write semantics. Per-unit sampler conversion introduces resource lifecycle and alias changes; a global tracker needs invalidation across programs/passes. Neither is needed for this local milestone. |
| More GPU-buffer/storage reuse | Not pursued: geometry resource creation/upload still measures 1.900 ms, but reuse changes atomic publication/recovery handling and memory ownership. Existing staging remains; the submission total alone does not justify another buffer policy. |

The remaining dominant work is scene construction (24.795 ms): DObj building
12.017 ms, including geometry emission 7.157 ms and skinning 2.767 ms, plus
assembly 6.626 ms (model/brush work 4.209 ms, command appends 2.211 ms).
Next milestone: profile and reduce avoidable work in canonical DObj geometry
emission and scene assembly, preserving pose, collision, animation and script
ownership. Further renderer-state expansion is not the recommended next step.

## Verification and reproduction

- One focused fixture was used: Win32 Debug `web_renderer_surface_tests`, built
  with its CMake target and run through CTest
  `--test-dir build/portable-tests-msvc18-win32 -C Debug -R
  '^web_renderer_surface_tests$' --output-on-failure --timeout 20`.
  The initial 10-check fixture passed (0.03 s test / 0.07 s total); a targeted
  rerun after shadow integration passed all 11 checks (0.05 s / 0.07 s).
  New checks exercise the actual state helpers: projection transitions, all
  state-bit positions, each falloff component, signed zero, material/feature
  changes, direct-override reset, every alpha/sampling/cull mode and fresh
  shadow partitions. Existing texture alias, ownership and validation checks
  remain. This is not an exhaustive GL-driver, visual or context-loss test.
- Three diagnostic Release builds passed: 15.456 s, 13.099 s and 23.470 s.
  All five diagnostic windows passed their sample population and nested timing
  checks. All four production windows passed contiguous callback and foreground
  checks. One earlier production benchmark attempt failed at bootstrap because
  it assumed the diagnostic launcher global; the runner was corrected to use
  product controls/events and the targeted retry passed. No engine assertion
  was relaxed, and no test or build failed.
- Exactly one final `tools/build_web.ps1 -Configuration Release` ran in this
  goal and passed (17.277 s). All builds passed the existing runtime-prefix
  check. Existing compiler/toolchain warnings remain. No later runtime edit
  followed this production build.
- Source/caller/invalidation inspection and `git diff --check` passed. All
  task browsers and servers were closed; Playwright cleaned its own profiles.
  The ignored baseline site snapshot was retained for reproducibility.

For a repeat, set `KISAK_COD4_RETAIL_ROOT` locally to an owned installation,
build the requested variant and serve its generated site on port 8051. Run
`node tools/profile_web_renderer.mjs LABEL` for diagnostics, or
`node tools/profile_web_renderer.mjs LABEL production BUILT_COMMIT` for the
production artifact. The latter requires the actual built source revision and
records the Wasm hash. Each run writes numeric evidence below ignored `build/`.
Do not run builds or concurrent benchmarks during a timing window.

No mission check, broad suite, screenshot, save/death/restart check or retail
compatibility promotion was performed. Committed evidence contains no retail
assets, installation paths or asset logs. The renderer-efficiency milestone is
complete; general offline playability remains a separate objective.

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/renderer-cpu-milestone-e4db91df.json`.
