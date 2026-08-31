# Canonical static-camera visibility, 2026-08-31

The completed LOD/shadow/DObj handoff was reviewed and committed separately as
`a44119df` before implementation. Its checks were not rerun. No unrelated work
or retail assets were modified, imported, copied or uploaded.

## Change and ownership

`gfx_d3d/r_dpvs_core.*` extracts native view setup, static-mask reset, portal
queue/clipping/bevels and AABB/cull-group algorithms. Canonical types moved into
`gfx_dpvs_types.h`; native entry-point adapters retain debug drawing, selected
view assertions, worker dispatch and plane-pool waits. The browser doesn't
compile the native renderer or manufacture `rg`, `rgp` or `scene` globals.

`R_RenderScene` supplies canonical D3D-space `GfxViewParms` and DB-owned
`GfxWorld` to `R_ComputeStaticCameraVisibility` on the engine Worker. Each call
resets slot 0 and executes static cells synchronously. Completion is returned
separately from mask bytes; all-zero is a completed empty view. Recomputing every
view avoids stale world/camera/FOV/far-plane results. Missing required producer
data reports an error for worlds containing static models, not success.
Portal scratch is reset before use, transient pointers are released after the
call, and newly executing index/winding/list boundaries reject invalid bounds.

The existing per-view command carries the completed mask. Camera packing uses
`canonicalInstanceIndex`, with a separate camera region in the existing instance
buffer. Visibility changes trigger packing/upload even when LOD choices match;
unchanged groups otherwise retain the prior optimization. Shadow ranges and
caster lookup stay on the original LOD-packed region. Cost: another instance
capacity in CPU/GPU storage and a retained byte mask. No speedup was measured.
World-surface masks/batches are not filtered, and DObj code is unchanged.

## Evidence and limits

- **Compiled:** `tools/build_web.ps1 -Configuration Release` passed once,
  including its existing canonical runtime-prefix check. Total build time
  71.087 seconds is a build duration, not renderer timing. Existing warnings
  remain; diagnostics were not built because no diagnostic interface changed.
- **Computed canonical visibility:** the Win32 Debug existing
  `web_renderer_static_model_scene_tests` fixture executed real setup, queued
  portal traversal, AABB model tests, camera rotation and far-plane rejection.
- **Filtered camera instances:** that fixture executed the production packing
  helper, proving valid empty results, noncontiguous canonical IDs, two LODs,
  visibility changes with unchanged selections, unchanged shadow storage/view
  bytes, and untouched world-surface bytes. Browser draw integration compiled;
  it was not observed in a running browser during this task.
- **Visually verified:** no. No new browser boot, retail/gameplay compatibility
  or performance claim is made. No implementation dependency blocker remains.

Exactly one CTest invocation ran (1/1 passed):

```powershell
ctest --test-dir build/portable-tests-msvc18-win32 -C Debug -R '^web_renderer_static_model_scene_tests$' --output-on-failure --timeout 20
```

The target was built with the Visual Studio 18 bundled CMake, Debug, target
`web_renderer_static_model_scene_tests`, `--parallel 2`. Build-only setup
corrections preceded that sole test run: the original 64-bit directory exposed
canonical 32-bit ABI assertions/missing dependency include; Win32 regeneration
exposed a CMake newline error and the pinned CMake's missing MSVC 19.51 feature
metadata; the supported VS CMake then exposed missing math/host-service link
symbols. These were fixed without weakening assertions or replacing algorithms.
The canonical producer portion is enabled on 32-bit targets; existing portable
scene tests remain available on 64-bit hosts. No test execution failed or reran.
Source review and `git diff --check` also ran.

Skipped: broad browser/native/Wasm/fuzz suites, prior handoff checks, diagnostic
builds, retail captures, mission/campaign matrices, replay and save/death/restart
checks. Escape investigation, renderer polish, world-surface filtering and
additional DObj optimization were not attempted.
