# Focused non-DObj scene profile

Recorded 2026-08-31 at clean `2ce03241`, based on `09fcd217`. Six diagnostic
intervals extend the existing frame profiler and aggregation path. No renderer
behavior, asset identity, static-model culling, or independent sun/spot shadow
path changed. Production compiles these timers out; inactive diagnostic
captures make no timing calls. No cache or new profiling framework was added.

## Observed CPU intervals

[Sanitized aggregates](scene-stages-2ce03241.json) contain 120 completed
gameplay-frame samples after 30 drawn CargoShip world frames. The run used
Chrome 152.0.7977.64 headless, a fresh Playwright-owned persistent profile,
1440 x 1000, and the same Ryzen 7 7800X3D host as the prior DObj profiles.
The user-owned installation was imported through the portable directory input,
then `map cargoship` was issued. No gameplay input followed; the authored scene
continued running. Capture completed in 6.059 s, with no page errors or DOM
visibility/focus transitions. No build ran during the capture.

| Interval | Mean ms | p95 ms |
| --- | ---: | ---: |
| Scene construction total | 28.887 | 30.860 |
| Setup before DObj build | 0.005 | 0.010 |
| DObj build | 10.222 | 11.170 |
| Remaining command assembly | 13.801 | 14.690 |
| Dynamic batch image resolution | 0.019 | 0.025 |
| Dynamic scene submission | 4.638 | 5.815 |
| Camera visibility | 0.141 | 0.195 |
| View submission | 0.061 | 0.075 |
| Unassigned within scene total | 0.001 | 0.005 |

Command assembly accounts for 73.94% of the 18.665 ms outside DObj building.
This identifies the next area to investigate, not an individual expensive
function. Camera DPVS and image resolution are small in this window.

The scene total and DObj total are lower than the preceding run despite this
being instrumentation only. Do not attribute that difference to a performance
improvement. Headless execution, scene motion, host variation and active
profiler/GPU queries limit comparisons; there was no profiling-disabled run or
complete GPU timing analysis. This is neither an FPS benchmark nor new visual
or playability evidence.

## Interval boundaries

- `sceneSetupMs`: function entry through view/light setup and any initial
  world/static command publication, stopping before DObj building. Initial
  publication occurred before this warmed capture.
- `sceneAssemblyMs`: after DObj building through floating-origin adjustment,
  canonical FX/DynEntity physics, marks, brush commands, DynEntity/FX models,
  command appends, particle clouds and sun sprite/flare assembly.
- `sceneImageResolveMs`: the final dynamic batch image-resolution loop; remains
  zero when there is no dynamic command. Material lookups inside individual
  builders remain in their respective build/assembly intervals.
- `sceneDynamicSubmitMs`: descriptor assembly and `WebRenderer_SetDynamicModelScene`,
  including backend validation, retained copies, resource creation/uploads and
  replacement. Empty scene clearing is also covered.
- `sceneCameraVisibilityMs`: camera parameter construction and canonical
  `R_ComputeStaticCameraVisibility`, including the completed-mask checks.
- `sceneViewSubmitMs`: `WebRenderer_SubmitSceneView`, including its backend
  camera-run preparation. It does not time the later camera/shadow draw passes.

These intervals and `dobjBuildMs` are disjoint within `sceneBuildMs`. Minor
reporting/timing overhead remains unassigned. As before, local destruction on
function exit is outside the scene total. DObj substages nest within DObj build;
renderer upload timers can overlap submission. Do not add those nested timers
to the scene partition. Historical aggregates without these fields retain null
values rather than invented zero measurements.

## Checks actually run

- `node --test tests/node/retail_profile_aggregate.test.mjs`: one focused file,
  3/3 tests passed (53.4503 ms), covering percentile aggregation, historical
  missing values and propagation of the scene/DObj intervals.
- One `tools/build_web.ps1 -Configuration Release -Diagnostics` passed
  (15.161 s). The local profile runner reused the existing browser aggregation
  helper and checked all six new intervals plus DObj fields for 120 samples,
  nonnegative scene intervals and their per-frame sum within the scene total
  (0.001 ms tolerance). All checks passed on the first profile attempt.
- One final `tools/build_web.ps1 -Configuration Release` passed (14.885 s).
  Both build scripts passed their existing canonical runtime-prefix check;
  existing compiler/toolchain warnings remain. No retry was needed.
- Source/bridge inspection and `git diff --check` passed. No assertions were
  weakened. No broad suite, mission check, screenshot, context recovery,
  replay, save/death/restart or retail-visual validation ran.

The task browser and server were closed. Playwright owned and cleaned up this
run's temporary profile; the earlier retained profile was untouched. Committed
evidence contains only timing/counter summaries and environment metadata, with
no imported retail content, asset logs or local installation path.

Next task: isolate the measured assembly interval into canonical physics/mark
work, brush/model construction and command appends, then optimize the dominant
operation with a focused before/after check. Preserve validation, canonical
ownership, camera culling and independent shadows; do not add pose/geometry
caches or change DPVS based on this profile.
