# Cleanup and bounded renderer change, 2026-08-31

This is an implementation and verification record, not a retail benchmark or
compatibility promotion. The working tree was clean at entry. Retail data was
not read, copied, modified, packaged, or added to fixtures. No whole-repository
audit was repeated.

## Changes and preserved boundaries

- Removed `web_engine_world_surface.*`, the private mirrored vertex/projection
  types, the single-surface DB publication submission, and its dedicated
  native/Wasm target. `DB_PlatformPublishGfxWorld` now reports `published`, not
  `submitted`; `R_RenderScene` and its world command remain the rendering owner.
  The historical DB browser test now expects publication and retains its
  separate scene-view/frame assertions. It was not run during this task.
- Preserved the proof's relevant finite lightmap-coordinate validation in
  `WebRenderer_BuildWorldSceneCommand`, with NaN/infinity and atomic-failure
  coverage. Existing world bounds, subtraction-based range checks, local-index
  checks, and replacement-on-success behavior remain. Canonical vertex-size
  assertions already live in `gfx_world_types.h`; mirror/projection assertions
  are obsolete with their types. Live surface APIs were not removed.
- Removed assisted mission authoring, the autonomous enemy-damage fallback,
  objective/actor/target probes used only by those helpers, `RouteAssist`, and
  prefix-skipping support. Existing manual F8/F9 authoring and explicit route
  replay remain opt-in diagnostic tools. Replay no longer calls the combat
  fallback or skips a failed prefix segment. No new bot or validator was added.
- Shared five identical image/constant table lookups across world, static-model,
  and DObj rendering. Named color-map precedence, semantic fallback, sampler
  defaults, and missing-constant behavior remain unchanged. Technique selection,
  water handling, and shader-family differences stay in their existing owners.
- Shared only Worker request IDs, protocol errors, pending-request rejection,
  and generation-aware reply settlement in `worker_transport.mjs`. Product and
  diagnostic operation/event allowlists remain separate. Product progress/stall
  and absolute watchdogs, retryable flush ownership, diagnostic termination,
  filesystem mutation queues, and lease release order are unchanged. Both site
  inventories include the shared module; the product file inventory names it
  explicitly without admitting diagnostic files or operations.
- Consolidated current status, roadmap, and ownership pages. Earlier narratives
  and their evidence remain explicitly historical. Mission progression is not
  a prerequisite for renderer, cleanup, or subsequent platform work.

## Renderer evidence and bounded change

The existing corrected CargoShip profile at `e31d62ac` records 32.29 ms frontend,
28.61 ms nested scene construction, 38.61 ms backend, and 11.99 ms static-model
CPU time. The later scratch-capacity comparison records 29.22 ms scene
construction and 32.94 ms frontend; it does not establish that allocation was
the dominant remaining scene cost. These are historical measurements:
[baseline](retail-profile-e31d62ac.md),
[scratch-capacity comparison](retail-profile-93451ec5.md).

Source inspection distinguishes four costs:

1. Canonical world and static-model immutable commands are built once per
   publication, under `g_worldSceneSubmitted` / `g_staticModelSceneSubmitted`.
   Rebuilding these is not the steady-frame scene-construction explanation.
2. DObj geometry is converted per frame in `WebRenderer_BuildDObjSceneCommand`,
   including model lighting, bone/surface work, and skinning. Scratch capacity
   is already reused. The existing broad timer does not isolate these substages
   sufficiently to justify another skinning optimization.
3. Static-model LOD selection leaves batches for all authored LODs retained.
   The camera draw loop previously performed material setup and texture binds
   before checking `instanceCount == 0`. That existing rejection now runs at
   the start of the loop. Nonempty draws, geometry, LOD selection, and shadow
   submission are unchanged. No new culling heuristic or object model is added.
4. Static geometry still lacks canonical per-view DPVS filtering. The retained
   static-model instances and batches also feed shadow passes, so filtering the
   common instance list using the camera would incorrectly remove offscreen
   shadow casters.

No capture was necessary for the empty-batch change. Its impact was not
measured, and no FPS, frame-time, draw-count, or binding-count gain is claimed.

### Exact next upstream visibility seam

Use the canonical `GfxViewParms` at frontend `R_RenderScene` to integrate
`R_SetupWorldSurfacesDpvs` / `R_SetupDpvsForPoint`, then
`R_AddWorldSurfacesDpvs` and its cell/portal traversal. The native
`R_AddCellStaticSurfacesInFrustumCmd` and
`R_AddAabbTreeSurfacesInFrustum_r` populate the view-specific canonical
`GfxWorld::dpvs.smodelVisData` and `surfaceVisData` arrays. Native
`R_AddAllStaticModelSurfacesCamera` consumes the camera slot; shadow paths use
their own view slots and caster membership.

The next bounded integration should carry those canonical visible instance
indices into camera-only instance packing at the existing command boundary,
while leaving shadow instance ranges independent. Preserve original canonical
instance indices through material/LOD grouping; do not make the WebGL backend
invent visibility or rebuild assets per frame. World batches can contain
several surfaces, so world visibility also needs their existing surface/index
ranges, not a guessed batch-center test. Establish native-equivalent view-bit
semantics before consuming zeroed/uninitialized visibility arrays. This seam
was documented, not implemented, because its view setup, portal traversal, and
camera/shadow separation exceed the safe empty-batch change supported here.

## Escape / pointer-lock inspection

`tests/browser/input.spec.mjs` exits pointer lock explicitly because automated
Escape is not a trusted browser lock-release gesture. In
`input_controller_core.mjs`, synthetic Escape requires a previously locked
pointer, an unintended unlock, document focus, and no recently forwarded
Escape. Cursor/menu-driven releases deliberately set `programmaticUnlock`.
The test checks the lock element but does not establish the document-focus
state at the unlock event. Focus changes or event ordering are possible
explanations, not a confirmed cause. No focused reproduction was run, no input
code or Escape assertion was changed, and the intermittent failure remains
unresolved. Do not remove the focus/intent guards to force this assertion green.

## Checks actually run

Exactly two targeted test commands were used, with no retries:

1. `node --test tests/node/filesystem_lifecycle.test.mjs tests/node/diagnostic_filesystem_lifecycle.test.mjs tests/node/product_protocol.test.mjs`
   passed 32/32 tests. Coverage includes separate timeout/retry policies,
   stale replies, progress deadlines, single-writer exclusion, and termination
   before lease release.
2. The pinned CMake built only `web_renderer_world_scene_tests` and
   `web_renderer_static_model_scene_tests` in `build/cleanup-native-final`,
   Debug configuration (assertions enabled), followed by CTest with
   `-R '^(web_renderer_world_scene_tests|web_renderer_static_model_scene_tests)$'`.
   Both targets passed. This includes finite-coordinate/atomic-failure and
   material-lookup precedence/default coverage.

`tools/build_web.ps1 -Configuration Release -Diagnostics` passed to verify
removal of helper-only exports (19.065 s reported build total).
One final incremental `tools/build_web.ps1 -Configuration Release` passed
(18.649 s reported build total). Both scripts ran their built-in canonical
runtime-prefix check successfully. These are build/check durations, not game
performance measurements. Warnings remain in untouched native declarations
and `g_scr_main.cpp` array indexing, plus the pinned CMake/Emscripten
shared-library compatibility warning.

Intentionally skipped: browser boot/visual validation, Escape reproduction,
routine browser tiers, full native/Wasm/fuzz suites, extra static/lint/type and
product-size gate commands, six-map regression, retail profiling, mission
progression, route authoring/replay, campaign expansion, and save/death/restart
matrices. No compatibility or visual result from earlier work is claimed as
having been rerun here.

## Measured code reduction and risks

Relative to the starting HEAD, counting added shared files and excluding all
Markdown (including historical archives): 331 lines added, 2,532 removed,
**2,201 net lines removed**. Of that reduction, 942 lines are runtime C++/JS,
1,253 are tests, and 6 are build/tool configuration. This is line count, not
binary size or a performance measurement.

The renderer change has compile and synthetic boundary evidence only; visual
assessment remains with the user. DPVS integration and scene-construction cost
remain open. External consumers of the obsolete single-surface publication
fields must use publication plus real scene/frame events. Removed helper-only
diagnostic exports are intentionally unavailable. Escape remains an unconfirmed
flake. No retail content, dependencies, WebGPU, pthreads, multiplayer, cinematic
implementation, or presentation changes were introduced.
