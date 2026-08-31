# DObj shader-hash deletion and focused comparison

Implementation: `d8661476`, based on clean `2a6d10fa`, recorded 2026-08-31.
Removed 16 lines from `web_renderer_dobj_scene.cpp`: the per-surface bytecode
hash calculation and its only DObj call. The DObj command's zero-initialized
hash field stays zero; no backend drawing, validation or merging consumes it.
Shader-name/material selection, geometry, skinning, lighting, canonical state,
static-model culling and sun/spot shadow behavior are unchanged. World and
static-model hashing remains because those paths have identity consumers.
No cache, new abstraction, dependency or diagnostic interface was added.

## Observed comparison

The [baseline](dobj-stages-946dc918.md) and
[new sanitized aggregates](dobj-hash-d8661476.json) use clean source commits,
Chrome 152.0.7977.64 headless, 1440 x 1000, the same Ryzen 7 7800X3D host,
fresh persistent browser profiles, `map cargoship`, 30 warm-up world frames,
no gameplay input, and 120 completed gameplay-frame samples. The new capture
finished in 7.092 s with no page errors or DOM visibility/focus transitions.
No build ran during profiling.

| CPU interval | Before mean ms | After mean ms | Change |
| --- | ---: | ---: | ---: |
| DObj geometry | 7.192 | 6.347 | -11.75% |
| DObj build total | 12.186 | 11.320 | -7.11% |
| Pose | 0.300 | 0.297 | -0.90% |
| Lighting | 1.341 | 1.338 | -0.24% |
| Skinning | 2.810 | 2.803 | -0.26% |
| Scene construction | 32.288 | 31.770 | -1.60% |
| Profiled total CPU | 57.306 | 56.862 | -0.77% |

Geometry p95 was 8.015 -> 7.245 ms; DObj total p95 was 14.005 -> 12.970 ms.
The target interval decreased by 0.845 ms on average, while neighboring DObj
stages stayed nearly unchanged. This supports retaining the deletion, without
claiming repeatable general FPS gains from one before/after pair.

The authored opening scene continued to move. Mean dynamic batches were
1390.233 -> 1390.658 and submitted indices 5,428,689 -> 5,428,595. World surfaces
submitted stayed 13,125; visible world/static-model counts varied slightly and
buffer uploads increased about 1%. These are similar workloads, not identical
frame traces or proof of visual equivalence. Counter summaries from the original
local baseline aggregate are included in the new evidence file.

Headless execution, active profiler/GPU-query overhead, scene motion and this
short window limit attribution. There was no profiling-disabled benchmark or
complete GPU-results analysis. No retail-visual or playability promotion is made.

## Checks actually run

- Built and ran only Win32 Debug `web_renderer_dobj_submission_tests`:
  `ctest --test-dir build/portable-tests-msvc18-win32 -C Debug -R
  '^web_renderer_dobj_submission_tests$' --output-on-failure --timeout 20`;
  1/1 passed, 0.05 s total. This existing test covers LOD/admission,
  depth-hack/shadow flags, material fallback and technique helpers; it does not
  compile the full DObj builder. The diagnostic build/profile exercises that path.
- One `tools/build_web.ps1 -Configuration Release -Diagnostics` passed
  (11.566 s), followed by the one successful 120-frame profile.
- One final `tools/build_web.ps1 -Configuration Release` passed (12.692 s).
  Both build scripts passed their existing canonical runtime-prefix checks;
  existing compiler/toolchain warnings remain. No retry was needed.
- Source/consumer inspection and `git diff --check` passed. No assertion was
  weakened. No broad suite, mission checks, screenshot, context recovery,
  replay or save/death/restart validation ran.

The task browser and server were closed. Playwright owned this run's temporary
persistent profile and its normal cleanup; the earlier task's retained profile
was not touched. Retail files remained local and outside repository artifacts.

Next task: measure the roughly 20.451 ms of scene construction outside DObj
building, separating command assembly, resource resolution and submission work
before selecting another optimization. Do not infer its owner from subtraction
alone or add pose/geometry caches without evidence.
