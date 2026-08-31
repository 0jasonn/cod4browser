# Canonical world-camera visibility, 2026-08-31

Started from clean `317fc12f` on `web-port`. Scope: implementation, related
integration, independent shadow rendering, preserved static-model culling,
one focused test and one final production Release build. No retail files were
accessed, copied, modified or uploaded. Mission checks, broad suites, mandatory
captures and unrelated work were excluded by the user.

Implementation and focused coverage: `124d00c2`. Documentation is committed
separately; both commits belong to `web-port`.

## Implementation and ownership

`R_ComputeStaticCameraVisibility` optionally includes world surfaces. The real
canonical portal/AABB traversal writes camera slot 0, and synchronous static
cell dispatch now also runs canonical cull groups, matching the native worker
entry point. Native `r_drawDecals` selects the authored sorted surface lists.
Newly executing sorted-list ranges, surface IDs and cull-group references are
checked. The world mask is cleared using its DB allocation count
(`staticSurfaceCount`); other views and caster membership are untouched.

`R_RenderScene` opts into both static-model and world visibility on every
submitted view. Completion is explicit, including all-zero results; missing
producer data is an error. No browser culling algorithm or renderer globals
replace Kisak ownership.

The retained world command now carries canonical surface IDs and emitted index
spans through material batching, including gaps from omitted sky surfaces.
The backend validates span order, canonical IDs, index bounds and batch coverage
before replacing resources. Every view rebuilds contiguous visible runs in
command order, merging only within the same batch. Runs are cleared before
submission validation, on world replacement, and on unload; capacity is reused
across views. Empty results stay empty without falling back to full batches.
The existing diagnostic drawn-surface count uses actual camera-run counts.

Sun shadows still use original retained batches and `castsSunShadow`; spot
shadows still use authored `shadowGeom` caster ranges. Neither consumes the
camera runs. Geometry/index buffers are unchanged. Static-model canonical IDs,
LOD packing, camera visibility and separate shadow instance ranges are preserved.
Brush models, DObjs, sky drawing, materials and gameplay were not expanded.

The metadata belongs to the renderer command boundary: 16 bytes per emitted
surface and up to another 16 bytes per surface of camera-run capacity, plus
vector overhead. One O(emitted surfaces) scan runs per view; no index-buffer
upload, second geometry buffer or retained world mask is needed. There is no
measured speedup. Visibility holes can increase camera draw calls within a
previously merged batch. Profile that tradeoff before adding another strategy.

## Checks and evidence limits

The existing `web_renderer_static_model_scene_tests` target was built once in
Win32 Debug with Visual Studio 18 bundled CMake and `--parallel 2`. It now links
the production world-command implementation alongside the canonical DPVS core.
The synthetic fixture is original repository GPL-3.0 data.

Exactly one test invocation passed (1/1, 0.12 s total):

```powershell
ctest --test-dir build/portable-tests-msvc18-win32 -C Debug -R '^web_renderer_static_model_scene_tests$' --output-on-failure --timeout 20
```

It covers camera rotation, portal and far-plane rejection, cull-group-only
surfaces, decal selection, noncontiguous canonical IDs, merged-batch visibility
holes and coalescing, material-batch separation, completed-empty and unavailable
results, invalid masks/spans, unchanged sun/spot geometry and shadow view bytes,
and existing static-model identity/LOD packing checks. No test failed or reran.

The final `tools/build_web.ps1 -Configuration Release` passed once, including
its existing canonical runtime-prefix check and required generated-site outputs.
Total build time was 19.275 s (a build duration, not renderer performance).
No build failed or retried. Existing native-header warnings, the pinned
CMake/Emscripten shared-library warning, and the Debug linker incremental/ICF
warning remain. Source review and `git diff --check` also passed.

These checks establish canonical synthetic behavior and compilation, not new
browser boot, rendered retail correctness, gameplay compatibility or performance.
Skipped: browser/native/Wasm/fuzz suites, diagnostic build, retail captures,
mission/campaign checks, replay and save/death/restart checks. No dependencies,
asset formats, transport, threading or engine substitutes were added.

Recommended next task: use the existing DObj stage timings for a short,
question-specific profile to identify whether pose, lighting, skinning or
geometry construction dominates before choosing an optimization. This is a
separate task, not a gate on this delivery.
