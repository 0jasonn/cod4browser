# Renderer resources and qualification

Kisak owns `GfxWorld`, materials, poses, LODs, visibility, FX and draw ordering.
The Worker frontend translates those records into portable commands; WebGL2 owns
GPU objects, upload/recovery storage and device state. This guide records current
constraints and bounded evidence, not complete native/browser visual parity.

## Resource lifetime and command publication

- World/static geometry and identity-placement brush meshes live within the
  canonical world; entity/DynEntity placements update per frame. Retained brushes
  consume logical scene capacity before optional FX, marks, clouds and sun quads;
  physical streamed indices account only for uploaded vertices.
- Validate descriptors, finite values, indices, bounds, material references and
  resource budgets before atomic publication. Failed dynamic submission returns
  caller vector ownership and preserves the previous published command. Context
  recovery reads published geometry, never unpublished staging or empty frontend
  scratch. World unload releases mesh, lighting and decoded-attribute caches.
- `web_renderer.h` bounds one dynamic command to 500,000 vertices and 1,000,000
  indices: at 72/four bytes, 40,000,000 logical geometry bytes. This neither
  preallocates the maximum nor caps the entire renderer/heap at 40 MB.
- Encoded images decode for upload/recovery without a retained second RGBA copy.
  DB `GfxTexture` handles preserve copy/override/default identity. The 256 MiB
  DB-source budget rejects before copying and cannot evict live sources; the
  separate 800 MiB image-pool limit measures decoded admission, not total recovery.
- Feature technique remaps run after publication and when relevant dvars change.
  Retained material snapshots must be invalidated when their dependencies change;
  do not assume remapping happens only at `R_LoadWorld`. World registration clears
  brush retention. Primary-light types stay validated while animated values arrive
  per frame; canonical material/image identity is never a GL object name.

## Geometry and DObj conversion

Completed dynamic vectors transfer into backend staging; only empty numeric
capacity returns to the frontend. Standard vector growth replaces per-cloud exact
reservations; failure restores logical contents/counts, though capacity may change.
Staging/frontend capacity is separate from recovery data and its counters;
no total-memory saving is implied.

[The DObj adapter](../src/web/web_renderer_dobj_scene.cpp) emits final skinned
vertices and one index span per surface from canonical `CG_DObjCalcPose`,
LOD/hide masks and materials. Weighted positions blend every influence; their
basis follows the primary bone. Rigid transforms preserve authored basis lengths,
including nonunit reflex tangents, with finite/nonzero validation. Immutable
attributes cache by packed-vertex identity/count within the world and vertex cap;
transforms and poses are evaluated afresh.

`cpose_t::lightingHandle` reuses numeric samples only on exact origin/primary-light
matches; movement recomputes, unload clears, and stale handles rebind. The atlas
follows current submission order and handles reach marks. Pose rejection precedes
lighting, preserving culled handles and other DObjs' atlases. Selective web LTO
exposes existing packing/quaternion helpers with exceptions and validation intact.

Inactive GPU buffer sets added memory without upload benefit; rigid GPU placement
changed shadow work and increased time; shader-side static sun culling kept vertex
work and increased CPU cost. Reconsider only with current qualified measurements.

## Camera visibility, ordering and shadows

[Shared DPVS](../src/gfx_d3d/r_dpvs_core.cpp) runs native view reset, portal/cell
and AABB/cull-group traversal synchronously. Recompute slot 0 per view: all-zero
means empty; invalid/missing producer data is an error. World commands preserve
canonical surface IDs/index spans and merge visible runs only within a batch.
Holes may increase draws; flattened dynamic bounds need a per-submission indexed
scan. Retire this metadata when canonical frontend commands supply the ranges.

Static instances preserve canonical IDs: shadow LOD packing is the first buffer
half, camera packing the second. Visibility changes upload only the camera half;
LOD changes update both. GPU instances are 72 bytes; 24-byte AABBs stay in CPU
source/shadow arrays. World spans retain authored AABBs too. Each sun cascade
rebuilds its light-space mask independently of camera visibility.

Primary spots preserve authored `GfxShadowGeometry` and canonical entity/DynEntity
linkage. Dynamic linkage uses the SP 2,208-entity stride, native cone/radius tests,
`GfxLightRegion` hulls and nearest model light; absent canonical arrays fall back
conservatively to matrix-only selection. Static instance flag bit 0 excludes
casters and static materials require `gameFlags & 0x40`; dynamic families use
the remap-aware build-shadowmap technique and its alpha/cull state. Shadow-only
BSP surfaces remain uploaded outside camera ranges. FX XModels and depth-hack
DObjs retain their primary-spot exclusions.

Opaque sun ranges merge only across contiguous indices with matching buffer,
placement and state; gaps, cutouts and non-casters break runs. Physical shadow
draws plus `sunShadowMergedRanges` preserve logical caster counts.
[Native shadow history](../src/gfx_d3d/r_shadowed_light_history.h) owns four-slot
retirement/reselection and fades. Invisible lights release immediately; world/
context retirement resets history, and scenes cannot pin their own lights.

Camera models, including translucency, follow canonical region/key order.
Emissive world/static/entity/FX lists merge by primary key with family-order ties;
FX AUTO/DECAL bands 48/24 preserve append order. Sun visibility and depth hack
retain separate passes. Primary keys use `Material::info.sortKey` when serialized
`drawSurf` is unset; full native material-table sorting remains uncompiled.

## Transient lighting and material state

[Shared light math](../src/gfx_d3d/r_dynamiclights_core.h) owns `GfxLight`
construction, importance, spot planes and tangent-sphere scissors. FX owns
lifetime; per-scene reset, 32/four ceilings, `r_dlightLimit` and `r_fullbright`
remain native. The first eligible added spot preserves near-plane bias and
competes for 512x512 slots. Admit test lights while time advances before pausing:
native shadow history cannot admit at a frozen timestamp.

| Transient family | Receiver/caster contract |
| --- | --- |
| BSP | Receiver: camera DPVS and original AABB against exact spot/omni bounds. Caster: shifted spot planes without camera DPVS. |
| Static models | Canonical ID/bounds after LOD packing select contiguous receiver runs; transient casters reuse the native camera/light receiver mask. |
| Rigid DObj / FX / DynEntity models | Preserve each native pose/XModel/scaled-FX sphere owner. Spot uses sphere/planes; omni uses radius sums. |
| Animated DObj | Selected-LOD bone masks and `XBoneInfo` transforms preserve scalar order and `viewOffset`; post-pose boxes retest full cell planes and BSP membership before skinning. |
| Scene / DynEntity brushes | Copy canonical writable world bounds before physics; do not transform them twice or infer them from retained geometry. |

Cgame/DynEntity keep native cell-bit banks and link identities. Bounded BSP walks
publish links atomically; invalid walks preserve prior bits. Repeated portal
paths OR admission, including native zero-plane and non-positive tangent rules.
`CG_UsedDObjCalcPose`/`CG_CullIn` preserve evaluated/visible status. FX attachment
and render flag 8 exclude scene DObj spots, retaining the rigid exception and
omni behavior. Transient caster child dvars are independent of sun/primary spots.

Loaded techniques 21/22 use authored images, attenuation, fog and render bits.
Clear destination alpha per light without changing RGB/depth, then scissor both
clear and receiver draws: `ONE_MINUS_DST_ALPHA` requires RGBA8 scene targets.
One list combines receiver families, excluding code meshes, marks, clouds and
sun billboards. Reverse-sort complements bits 54-59 before ascending comparison;
non-BSP keys inherit material light/probe fields rather than camera overrides.
Native low-bit object IDs are transient draw-buffer offsets absent at this seam;
equal-key stable frontend order does not establish exact native tie ordering.

State reuse stays local to each frame/pass/partition; complete texture-set changes
reapply original unit order to preserve aliased object parameters. Reset after
outside changes/direct overrides. Material keys include material/technique identity,
both state words, source/ambient mode and bitwise shader arguments; extend equality
for every consumed input. Matching blend bits alone cannot reuse feather/falloff/
eye offsets. Required uniforms always upload; no global GL/sampler-object cache.

## Presentation and fidelity boundaries

- [Native particle-cloud policy](../src/gfx_d3d/r_particle_cloud.h) preserves view-X
  sign, directed stretching, epsilon and `UV - 0.5` corners. Placement affects
  centers, not billboard dimensions. The 8x8x16 lattice consumes three random
  samples per cell in native x/y/z order; CRT bucket adaptation preserves range
  and lifecycle, not identical sequences. Outdoor masking uses the canonical
  image/matrix and inclusive height test; absent lookup skips that outdoor pass.
- Soft particles use authored bindings and signed FloatZ from lit/decal depth,
  including alpha rejection. Non-feathered angle falloff remains independent of
  `r_zFeather`. Distortion resolves post-lighting colour before emissive draws,
  rejects offsets crossing foreground depth, and honors `r_distortion` separately.
  The observed MSAA source-reuse case requires `glFlush`, not a GPU completion wait.
- Saved screens retain one RGB8 feedback texture/four native timer slots, the
  0.99 blur cap and authored flash blend. Resize/context loss invalidates history;
  unload/shutdown releases it. Lost prior-frame pixels cannot be recovered.
  Save/feedback reads precede display gamma (`1 / r_gamma`) to avoid double correction.
- [Shared text](../src/gfx_d3d/r_text.cpp) owns glyph/style/glow/cursor timing and
  native half-pixel placement; the adapter only submits quads and converts BGRA.
  [Shared gamma](../src/gfx_d3d/r_gamma.cpp) supplies native arithmetic; WebGL cannot
  install a monitor gamma ramp. AC130 accepts only native's precise empty-grid
  representation, not malformed missing storage; FX average lighting follows
  native's sun-selection/56-sample policy rather than the model-lighting query.

## Validation and reproduction

Native/Wasm fixtures retain atomicity, placement, visibility and shadow checks.
Differentials cover 4,096 spot directions/98,304 coefficients and bounds cases,
4,096 posed-box/sort and 1,024 culling/key cases, not the full native renderer.
Actual `WEBGL_lose_context` tests cover browser events; earlier direct hooks
proved resource reconstruction only.

Known limits remain: installed Chrome/D3D11 recorded three unchanged one-byte
exact-pixel discrepancies (127 versus 128), despite default Chromium passes.
The late transient-light test passed illumination, shadows, clearing, recovery
and limits but failed its final positive DObj post-pose diagnostic; do not report
the complete fixture as passing. Its DynEntity-brush path has synthetic evidence
only. Stationary AC130 emitted no particle cloud, so authored cloud/thermal
fidelity remains open. Matched native/Steam lighting, translucency, moving marks,
all shader families and full scene parity require separate observations.

For sampler changes run installed Chrome/D3D11 on an isolated port; WebGL link
success alone cannot expose ANGLE's first-draw shader compiler failures:

```powershell
$env:KISAK_BROWSER_CHANNEL = 'chrome'
$env:KISAK_WEB_TEST_PORT = '8052'
npx.cmd playwright test tests/browser/dynamic_lights.spec.mjs
node --test tests/node/renderer_workload.test.mjs tests/node/retail_profile_aggregate.test.mjs
```

For authorized local assets, serve only the selected built site on port 8051,
set `KISAK_COD4_RETAIL_ROOT` locally, and capture with the existing runner:

```powershell
python tools/serve_web.py --directory build/web-diagnostics/site-diagnostics --port 8051
# Run in a second shell; source revision and directory must match the served artifact.
node tools/profile_web_renderer.mjs active-before diagnostics HEAD build/web-diagnostics/site-diagnostics
node tools/profile_web_renderer.mjs paused-before diagnostics HEAD build/web-diagnostics/site-diagnostics fixedtime recovery
```

Active capture uses 30 warm-up frames, 300 clean intervals and 120 profiled frames;
`activeWorkloadMatched` stays false pending settings/view/work qualification.
Record GPU/driver, source/artifact hashes and duration; reject incomplete,
background/disjoint data. Preserve frame tails and distinct CPU/GPU/arrival
boundaries; overlapping memory estimates cannot be added together.
Paused diagnostics compare all 120 views 601-720 exactly with
`node tools/renderer_workload.mjs --profiles BEFORE.json AFTER.json`.
Use explicit `--retained`, `--shadow-ranges`, `--static-shadow-partitions`,
`--world-shadow-partitions`, `--dynamic-shadow-partitions` or
`--dynamic-spot-shadows` only for the corresponding intended work change.

Paused production uses `production BUILT_COMMIT SITE fixedtime uncapped`, fresh
A/B/B/A controls and checkpoints 240-540 over 300 frames. Report elapsed/300;
five span percentiles are not per-frame p95. Keep timing methods/caps identical
and builds separate. `com_maxfps 0` is benchmark-only; the 125 Hz safety bound remains.
Paused gains do not establish campaign FPS. Headed CargoShip measured 20.63/17.28
FPS on 2026-09-05 in different scenes, not an isolated resolution comparison.
Current active stages and tails still need profiling.

Historical experiments, shader hashes and failures remain in
[Git history](../README.md#historical-records). Current suite results and optional
fixture failures belong in the [test inventory](web-test-inventory.md).
