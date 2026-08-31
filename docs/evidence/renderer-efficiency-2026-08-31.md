# Renderer efficiency continuation, 2026-08-31

Started from clean `bf5ec1e2` on `web-port`. Scope was the requested renderer
paths, not another audit or cleanup. Retail files were not accessed or copied.
Mission automation, input, filesystem behavior, and canonical asset/game state
were not changed.

## Step 1: unchanged LOD groups and empty shadows

`UpdateStaticModelLods` still evaluates every canonical LOD each frame. A local
group-change flag now bypasses packing and batch-range updates when that group's
selections match. The initial `-2` sentinel forces population even if all LODs
select culled `-1`. Transitions between visible LODs or into/out of `-1` still
repack in the existing stable order. Any changed group still triggers the
existing full instance-buffer upload; this change does not attempt partial GPU
uploads or introduce a cache. Context restoration still recreates the buffer
from retained packed data.

The static-model shadow loop now rejects `instanceCount == 0` before per-batch
material, uniform, or texture setup, matching the prior camera change. Nonempty
sun and spotlight paths are unchanged.

Source inspection and one incremental production Release build passed. No
separate test command or timing capture was used for this small step.

## Step 2: dependency blocker and runnable prerequisite

**Canonical camera visibility is not integrated.** The following existing native
owners were inspected before choosing the permitted prerequisite fallback:

- `R_SetupWorldSurfacesDpvs` / `R_SetupDpvsForPoint` use canonical
  `GfxViewParms`, `scene.dpvs.localClientNum`, `dpvsGlob`, and `rgp.world` to set
  frustum, near/far planes, and view origin. `r_dpvs.h` includes `r_init.h`, which
  still directly includes `d3d9.h` and native device/global types.
- `R_AddWorldSurfacesDpvs` walks portals/cells. Its
  `R_AddCellSurfacesAndCullGroupsInFrustumDelayed` dispatches four worker jobs:
  static cells, dynamic models, scene entities, and dynamic brushes. Importing
  that caller is not a static-model-only dependency closure.
- `R_AddAabbTreeSurfacesInFrustum_r` writes canonical `smodelVisData` through
  native renderer globals and selected-view pointers. It also references world
  surfaces and native debug/frontend state. An independent browser traversal or
  fake renderer globals would bypass the required ownership boundary.
- `R_ClearDpvsScene` assumes `R_InitSceneBuffers`-owned entity/index buffers,
  clears all native static/dynamic view arrays, and selects the view. The current
  browser frontend does not run that scene-buffer/reset lifecycle. DB loading of
  `smodelVisData` bytes does not make them a completed current camera result.
- `R_AddAllStaticModelSurfacesCamera` consumes camera slot 0, applies distance
  and LOD policy, and constructs native draw surfaces. Shadow paths consume
  other views/caster membership. Camera rejection cannot mutate the shared
  shadow source.

These are source-inspected dependency blockers, not a claimed failed compile
experiment. No stubs, dummy masks, replacement visibility algorithm, or broad
native header rewrite was introduced.

The completed prerequisite makes retained camera offset/count fields explicit
and routes camera draws through them. Shadow offset/count fields and caster
lookup continue to use the original packed instance buffer. Initial publication
and LOD changes populate both ranges conservatively. This costs two integers
per batch, no additional instance buffer or per-frame copy. It does **not** yet
provide separate visibility-filtered camera packing. The native fixture proves
canonical indices survive noncontiguous light grouping and two authored LODs.

The next coherent native seam is device-independent DPVS view/reset ownership
plus static-cell dispatch, preserving the actual upstream portal/AABB functions.
Once it produces a completed camera view, carry its canonical instance indices
through the command boundary and pack a camera range without modifying shadow
instances. Track completed-view validity separately from visibility contents:
all-zero is a valid empty result, not a reason to draw everything. Reset validity
on world/view changes and invalidate camera packing on visibility changes even
when LODs match. Do not wire the present DB arrays to drawing before that producer
exists. World filtering remains deferred because batches span multiple surfaces.

## Step 3: focused DObj measurements

The existing diagnostic frame profiler now exports/aggregates:

| CPU field | Interval |
| --- | --- |
| `dobjBuildMs` | Whole `WebRenderer_BuildDObjSceneCommand` call, nested in scene construction |
| `dobjPoseMs` | Canonical `CG_DObjCalcPose` calls |
| `dobjLightingMs` | Lighting-atlas initialization and per-submission evaluation/entry setup |
| `dobjSkinningMs` | Bone-matrix preparation and rigid/weighted vertex skinning |
| `dobjGeometryMs` | Vertex conversion/validation, indices, material lookup and draw-batch construction |

The four substages are disjoint and nested in the build total. LOD/hide checks,
other validation, bookkeeping, and final cleanup remain unassigned within that
total. No timer calls execute when profiling is inactive, and all new timing
code/fields are behind `KISAK_WEB_DIAGNOSTICS`. Historical samples lacking the
fields aggregate to `null`, not invented zero measurements. No profiling
framework, pose cache, or geometry cache was added.

No CargoShip capture was run. The historical approximately 29 ms scene-build
interval remains a reason to measure, not proof of a dominant DObj substage or
a new speedup. A future short stationary capture can answer which of these
intervals dominates; no 60-second stability or mission gate is needed.

## Checks actually run and limits

- Step 1: one `tools/build_web.ps1 -Configuration Release` build passed.
- Step 2: one focused command built `web_renderer_static_model_scene_tests` in
  `build/cleanup-native-final` Debug (assertions enabled), then ran only that
  CTest target; passed. One incremental production Release build passed.
- Step 3: `node --test tests/node/retail_profile_aggregate.test.mjs` passed 3/3.
  The diagnostic Release build passed, verifying the changed timing bridge.
  The final production Release build passed, verifying diagnostics compile out.

There were three production builds total, one per step, and one diagnostic
build for step 3. Reported build totals were 13.419 s, 13.270 s, and 14.202 s
for production, and 15.887 s for diagnostics. These are build durations, not
renderer performance results.

Each build uses the existing build script, including its canonical runtime-prefix
check. Compiler warnings in untouched declarations/client code and the pinned
CMake/Emscripten shared-library warning remain. These checks establish compile
and synthetic behavior, not new browser boot or visual/gameplay evidence.

Intentionally skipped: full browser/native/Wasm/fuzz tiers, extra static/type/size
commands, retail captures, six-map matrices, mission checks, route authoring or
replay, repeated lifecycle checks, and Escape investigation. No failures were
rerun. Visual assessment remains with the user. Less CPU packing/setup work is
expected from source behavior; FPS, CPU-stage savings, and GPU gains are unmeasured.
