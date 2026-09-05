# Dynamic spot-shadow caster milestone

Runtime `9a253c6a` submits DObj, DynEnt XModel, moving-brush, and DynEnt-brush
casters to each selected spot shadow map. Each light tests the retained
world-space dynamic AABB against its own perspective matrix. Camera DPVS and
sun-cascade membership are never consulted.

Dynamic materials now follow Kisak's build-shadowmap technique instead of the
static-model game-flag shortcut. The portable draw command carries that
technique's cull and alpha state, and opaque range joining stops at cull-mode,
alpha, geometry, or placement boundaries. FX XModels and depth-hack DObjs stay
excluded.

## Diagnostic result

The seeded paused CargoShip workload uses fixedtime 16, one stable camera, and
profile views 601-720. The targeted comparator permits only the missing spot
caster work; all camera, world/static retention, dynamic/UI command, upload,
buffer, and sun-shadow counts match for all 120 samples.

| Per-frame work | Control | Candidate | Added canonical work |
| --- | ---: | ---: | ---: |
| Physical shadow draws | 330 | 340 | 10 |
| Submitted indices | 957,990 | 1,023,750 | 65,760 |
| Merged sun ranges | 0 | 0 | 0 |

Dynamic spot attribution rises from profiler noise at 0.0008 ms to 0.0915 ms;
total spot drawing rises from 1.073 to 1.186 ms. Whole-backend and whole-frame
timings moved in the opposite direction in this single pair, so no broad
performance claim is made. This milestone restores missing light-space-qualified
rendering rather than promising a frame-time reduction.

## Validation and limits

- The focused `web_renderer_fx_model_scene_tests` target passed (1/1, 0.03 s;
  0.05 s total). It proves the static game flag cannot admit a DynEnt XModel,
  the remap-aware build-shadowmap technique does admit it, and its authored
  shadow state crosses the command boundary.
- The targeted `--dynamic-spot-shadows` comparator passed 120/120 unchanged-work
  samples, with exactly 10 draws and 65,760 indices added per frame.
- Diagnostic Release and runtime-prefix checks passed. One final production
  Release and runtime-prefix check passed.
- No broad suite, mission check, capture, context-loss run, or compatibility
  promotion ran. Retail data, paths, and logs remain outside version control.
- The selected-light matrix supplies the exact spot-frustum test. Canonical
  `primaryLightEntityShadowVis` / `primaryLightDynEntShadowVis` broad-phase
  linkage is still stubbed at the web frontend, so light-region membership is
  a named remaining convergence gap.

Final production Wasm SHA-256:
`aaf87575e213521a7736877df0563fcd1556262a5f590698aa356f7e553f49c0`.
Final diagnostic Wasm SHA-256:
`ded793d8676f8432b6c41409ea34caffaaed46196b87d8999e9a90c5841404bd`.
Numeric results and raw hashes are in
the companion record (archived in Git).

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/dynamic-spot-shadows-9a253c6a.json`.
