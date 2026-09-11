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

Backend batches retain the pixel shader's `lm_spot_` family as a boolean instead
of copying two shader names and two unused hashes per batch. Canonical shader
metadata remains unchanged in the frontend/database; world and static-model
conversion use the same family check.

Particle clouds submit constants against one retained 1,024-particle GPU lattice,
following native `R_CreateParticleCloudBuffer` and `R_SetParticleCloudConstants`.
Required-family admission still includes retained brush counts and charges each
optional cloud its logical 4,096 vertices/6,144 indices. Native lattice/corner
order, material state, outdoor lookup at the unexpanded center and RNG lifetime
are preserved. The shader transforms centers and expands billboard corners;
canonical FX state and camera-dependent geometry are not cached. Scalar expanded
builders remain the native/Wasm oracle and `-DisableDirectCloudAppend` control.
Finite constants use a conservative bound, with exact scalar corner validation
for extreme inputs. New lattice uploads publish only with a successful complete
dynamic command. Failure preserves old geometry/constants; loss recreates the
published lattice, registration changes its generation, and unload releases it.

Staged dynamic geometry and model-lighting uploads share a final GL error check
only when a context exists, both payloads are nonempty, and the persistent image
pool needs no upload. Other cases retain their individual checked helpers.
Zero object names or GL errors delete every staged GPU object and preserve the
published command, recovery data and caller ownership. The persistent image pool
keeps its separate error/cleanup contract; this is not a global deferred-error mode.

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
lighting, preserving culled handles and other DObjs' atlases. Full web LTO
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
scan only when the existing shadow-caster consumer uses them. Non-casters still
validate every vertex/index; transient spotlight and brush bounds retain their
separate canonical owners. Retire this metadata when frontend commands supply it.

Static instances preserve canonical IDs: shadow LOD packing is the first retained
half, camera packing the second. CPU descriptors remain 72 bytes; the GPU consumes
the 15 placement/lighting floats as four RGBA32F texels (64 bytes) per instance.
Visibility changes upload the camera rows, including unchanged neighbors at a row
boundary; LOD changes update both halves. The shader uses a base offset plus
`gl_InstanceID` instead of rebinding five attribute pointers per range. Brushes
and clouds retain their constant-attribute paths. The 24-byte AABBs stay in CPU
source/shadow arrays. World spans retain authored AABBs too. Each sun cascade
rebuilds its light-space mask independently of camera visibility.

The backend texture uses nearest `texelFetch`, unit 16, 256 texels per row and at
most 2048 rows, within core WebGL2 minimum limits. It has no image/mip policy and
does not need an extension. Temporary packing is bounded to 8 MiB at maximum
capacity and is released after upload. Publication, GL error checks, retained
recovery data and world-unload retirement follow the previous instance-buffer
owner. Main, FloatZ, dynamic-light and shadow passes consume the same exact floats.

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
Sun and spot families check GL errors once per shared readiness group. A family
becomes sampleable only after every CPU submission and the final check succeed.
Even an early CPU failure drains all GL error flags after partition cleanup, so
failure cannot leak into the next family. Allocation and other error-consuming
operations remain outside the group; failed families stay unavailable.
[Native shadow history](../src/gfx_d3d/r_shadowed_light_history.h) owns four-slot
retirement/reselection and fades. Invisible lights release immediately; world
retirement resets history, and scenes cannot pin their own lights. GPU context
loss preserves CPU history, selected slots and static visibility. Resetting history
at a paused timestamp spuriously selected new lights and added 299 caster draws;
actual Cargoship loss/restoration now preserves selection and 12 resumed draw samples.

Camera models, including translucency, follow canonical region/key order.
Emissive world/static/entity/FX lists merge by primary key with family-order ties;
FX AUTO/DECAL bands 48/24 preserve append order. Sun visibility and depth hack
retain separate passes. Primary keys use `Material::info.sortKey` when serialized
`drawSurf` is unset; full native material-table sorting remains uncompiled.
The backend computes each reorderable camera key once into a compact temporary
list and sorts by key plus original index. Index ties preserve the existing
stable order; anchors and distinct model/FX runs remain separate. This changes
only command construction, not canonical material identity or draw submission.

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
eye offsets. Separate raster keys reuse equal depth/blend/cull/write state while
material shader arguments still update. Primary-light constants and spot bindings
reuse a light/mode key only within an immutable camera pass; transient-light
receivers also reuse unchanged projections/depth ranges and raster state. All
keys reset at pass boundaries and direct overrides. The frame-local texture
parameter table has 4096 slots (32 KiB): a Cargoship trace found repeated writes
following collisions in the earlier 256-slot table. Reset fills in place;
collisions still cause writes, never incorrect reuse. No global GL/sampler-object cache.
World/static passes now use the same ordered whole-set rule, including mipmap and
attenuation state. Identical instance attribute offsets reuse setup only within
the same VAO/pass; binding changes and direct overrides invalidate that knowledge.

Sun and spotlight partitions share one complete GL error drain after both
families submit and before camera sampling. CPU failure suppresses its family;
any GL error suppresses both families for that frame. Failed submissions still
drain all error flags. No allocation or error-consuming helper intervenes;
the existing shadow-batching build switch retains the individual-check control.

## Presentation and fidelity boundaries

- [Native particle-cloud policy](../src/gfx_d3d/r_particle_cloud.h) preserves view-X
  sign, directed stretching, epsilon and `UV - 0.5` corners. Placement affects
  centers, not billboard dimensions. The 8x8x16 lattice consumes three random
  samples per cell in native x/y/z order; CRT bucket adaptation preserves range
  and lifecycle, not identical sequences. Outdoor masking uses the canonical
  image/matrix and inclusive height test; absent lookup skips that outdoor pass.
- Soft particles use authored bindings and signed FloatZ from lit/decal depth,
  including alpha rejection. A depth-only compilation of the shared camera
  shader removes unreachable lighting; its program and uniform locations are
  owned by the WebGL backend and recreated on context recovery. The prepass
  retains its traversal, textures, raster state, signed-depth encoding and
  completion check. Scope exit restores the full camera program, including on
  an error return. FloatZ shaders return before unused lighting and
  shading; opaque depth skips base-texture sampling, while alpha-tested depth
  preserves the 128/255 cutoff and signed depth packing.
  Non-feathered angle falloff remains independent of
  `r_zFeather`. Distortion resolves post-lighting colour before emissive draws,
  rejects offsets crossing foreground depth, and honors `r_distortion` separately.
  The observed MSAA source-reuse case requires `glFlush`, not a GPU completion wait.
- Saved screens retain one RGB8 feedback texture/four native timer slots, the
  0.99 blur cap and authored flash blend. Resize/context loss invalidates history;
  unload/shutdown releases it. Lost prior-frame pixels cannot be recovered.
  Save/feedback reads precede display gamma (`1 / r_gamma`) to avoid double correction.
- [Shared text](../src/gfx_d3d/r_text.cpp) owns glyph/style/glow/cursor timing and
  native half-pixel placement; the adapter only submits quads and converts BGRA.
  The backend merges adjacent complete triangle ranges only when material,
  image, sampler, state and color match. Geometry and glyph order are unchanged;
  saved-screen and shellshock commands remain barriers, as do color changes.
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

The harness records every completed submission in a bounded Worker buffer,
retrieved in batches. Clean timing has the diagnostic profiler disabled.
New runs use muted headless Chrome, opening no desktop windows. The harness
sets native `ui_autoContinue 1` before the map command, so canonical pregame
releases the loading intro after initialization. This avoids an asynchronous
Escape arriving after pregame and opening pause. It records the skip method. Authored in-engine sequences remain part of the mission.
Headless captures explicitly identify their execution mode; hardware GPU,
logical foreground, cadence, settings and workload checks still apply.
Never compare them with earlier headed captures: take fresh controls in the
same mode. Headless submission cadence does not establish visible presentation.
`canonicalFrameCpu` includes server/client, scene/frontend, uploads and sound;
`rendererSubmissionCpu` covers the later WebGL submission. Completion intervals
retain gameplay stalls. Their p95/p99 are per-frame wall intervals, never
percentiles of sparse status events. These CPU boundaries are not GPU execution.

Active Cargoship uses `sv_mapSeed 1`, `fixedtime 16`, `com_maxfps 125` and
`cg_drawFPS 0` (`Off`): the developer FPS overlay otherwise changes UI geometry
with measured timing. Before retaining timings, require view 30 at canonical time
779; another startup phase changes camera-shake RNG consumption. Retry only this
preselected phase rejection, never a slow result. Clean time 4139-8939 contains
301 samples/300 intervals; a later diagnostic time 9915-11819 contains 120 views.
The comparator normalizes generation coordinates only and checks exact time,
camera, projection, geometry and 3D/UI draw counts. Compiler comparisons also
require initialized helicopter jitter axes, defined wrapping `RandWithSeed`, and
initialized disabled memory-tracker outputs; those fixes preserve canonical work.

Drain all 120 issued GPU queries; reject missing, dropped, disjoint or stale
results. Six stages rotate across frames, so their 20-result means cannot be
added into whole-frame GPU time. CPU subfields and renderer-memory populations
also overlap. Per-frame Wasm capacity is a separate measure, not process RSS.

Use fresh A/B/B/A contexts with no concurrent builds, tests or other browser
jobs. Every served file is verified against the full local build receipt, source
snapshot, configuration and public dependency identities. Comparisons require
matching benchmark-driver hashes, hardware/driver, settings, foreground status
and 1080p backing dimensions. Dirty local receipts do not qualify a clean release.

Paused diagnostics use views 240-540 and profile 601-720 after canonical pause
admission and a fixed camera. `recovery` uses actual `WEBGL_lose_context`, checks
GPU identity and CPU shadow selection, and requires 12 equal resumed draw samples.
This proves resource/draw restoration; image, input and gameplay parity remain
separate. `uncapped` requests `com_maxfps 0`, retaining the existing 125 Hz safety
bound; ordinary FPS defaults are unchanged.

Only separate production `realtime` windows qualify gameplay cadence. They use
`fixedtime 0`, the ordinary FPS overlay, at least 60 wall seconds and consecutive
advancing same-world scenes. Report canonical/wall-clock advancement, preserving
native `Com_ModifyMsec` clamps. Require average submission throughput >=60 FPS,
p95 <=20 ms and p99 <=33.3 ms, retaining all gameplay stalls. Settings before and
after must match. Fixed-work and paused gains alone cannot establish this target.
Both maps begin at the first rendered scene at canonical time >=45,000 ms (and
generation >=240), after a settings snapshot at >=44,000 ms. A separate settings
probe found Cargoship's authored `sm_sunSampleSizeNear` transition from 0.5 to
0.25 between 35 and 40 seconds, then stable through 100 seconds. Earlier windows
starting at generation 240 or 20 seconds were rejected for changing settings;
their timing is not qualified evidence. The final window excludes that opening,
without overriding or ignoring the authored setting.
Killhouse additionally sets the existing spawn view with the canonical server
command `setviewpos 3072 -1155 64.125 100.9 0` once during the 30-second warmup.
This retains normal physics and leaves the simulation advancing. Verify the
horizontal view at all 12 captured camera/projection checkpoints from 45 through
100 seconds and compare those values exactly across runs. Earlier uncontrolled
runs mixed about 87 FPS with a 125 FPS floor-facing
view; a separate screenshot/input probe reproduced the latter with no input after
launch. Those uncontrolled runs cannot establish an optimization gain or a matched
Killhouse regression result. This defined idle view is not training/combat or
mission-completion evidence.

```powershell
# Serve the matching production site for these captures.
$env:KISAK_PROFILE_MAP = 'cargoship'
node tools/profile_web_renderer.mjs realtime-cargoship production HEAD build/web/site realtime
$env:KISAK_PROFILE_MAP = 'killhouse'
node tools/profile_web_renderer.mjs realtime-killhouse production HEAD build/web/site realtime
Remove-Item Env:KISAK_PROFILE_MAP
node tools/renderer_workload.mjs --measured A.json B.json B2.json A2.json
```

Keep production and diagnostics separate. Existing explicit workload-change
comparison modes apply only to their named retained-resource/shadow changes;
do not relax exact work equality to admit an optimization. Optional
`KISAK_PROFILE_WATER=1` measures authored water separately from default
`r_drawWater 0`; the observed water-enabled retail fixture still emitted zero
water work. Synthetic nonzero spectra cover animated generation, two times,
repeatability, unchanged sources and output-capacity rejection, not retail fidelity.

## Measured optimization results

The following tables record the prior Chrome 152 phase, not the subsequent
Chrome 153 continuation. Local dirty-tree evidence is based on
`3a1aa20acb84895b9a758d3f4f6a34ba0d666b22`,
headed Chrome 152.0.7977.77, Ryzen 7800X3D, RTX 3070 Ti through ANGLE D3D11,
NVIDIA driver 32.0.16.1664, actual 1920x1080. Pre-import page rAF averaged about
7.9 ms and clean-window page rAF about 7.65 ms. Those are presentation
opportunities, not physical refresh, scanout or proof each game frame appeared.

All values below are clean completed-submission intervals in milliseconds.
Diagnostics retained all 120 GPU results and exact canonical work. Compiler
order is A/B/C/C/B/A; independent candidate order is A/B/B/A.

| Experiment | Recorded intervals, in run order | Decision |
| --- | --- | --- |
| Oz / O2 / O3 | 28.901 / 25.939 / 25.969 / 25.933 / 25.874 / 29.162 | O2: about 10.76% shorter intervals than Oz; O3 overlaps and is about 209 KB larger. |
| O2 / full LTO / LTO+SIMD | 25.992 / 25.527 / 25.264 / 25.517 / 25.492 / 25.873 | Full LTO saves about 1.63%; SIMD overlaps variation and stays off. |
| World/static state reuse | 31.387 / 24.318 / 24.197 / 31.437 | Texture binds 11,251.66 to 5,161.94/frame; clear stage reduction, variable total benefit. |
| State reuse, fresh repeat | 25.668 / 24.277 / 29.097 / 25.457 | World stage 1.600/1.601 to 0.847/0.920 ms, but one total-time outlier; do not promote the first group's 22.8% as a general FPS gain. |
| Audio equality suppression | 25.578 / 25.583 / 25.387 / 25.493 | Total throughput indistinguishable; retain repeated sound-stage saving of about 0.019 ms. |
| Skip unused shadow bounds | 25.302 / 25.255 / 25.083 / 26.221 | Draw construction about 1.072 to 0.690 ms; total gain remains within control variability. |
| STREAM_DRAW hint | 25.665 / 32.164 / 32.347 / 25.469 | Regressed; removed. |
| Dynamic image index | 25.657 / 25.547 / 31.294 / 25.620 | Fewer comparisons but inconsistent time/tails; removed. |

No new water cache, buffer pools, staging machinery or static-range upload
machinery was justified in that phase. Its selected build uses O2/full LTO, state reuse, audio
equality and the exact non-caster bounds skip. Build alternatives are explicit:
`-Optimization Oz|O2|O3`, `-DisableFullLto`, `-Simd`,
`-DisableRendererStateReuse`, `-DisableAudioEquality`, `-DisableShadowBoundsSkip`
and `-BuildDirectory`. Disabling full LTO retains the prior selective LTO.

The final production fixed-work comparison uses Oz/selective LTO with candidates
off as A and the selected configuration as B, with the same correctness fixes.
Both also include corrected pacing and the high-performance adapter request;
these results do not assign them an isolated retail FPS gain. Synthetic timing
checks cover 60/120/144/165/240 Hz admission, cap changes, duplicate clocks,
stalls, suspension, wrap and game-clock conservation.

| Run | Mean ms | Submission FPS | p95 ms | p99 ms |
| --- | ---: | ---: | ---: | ---: |
| A1 | 34.602 | 28.900 | 39.185 | 43.635 |
| B1 | 23.656 | 42.273 | 26.885 | 28.950 |
| B2 | 23.540 | 42.481 | 26.735 | 28.390 |
| A2 | 28.781 | 34.745 | 32.495 | 34.205 |

B repeats closely, but A varies. The observed interval reduction is 18-32%
against individual controls; averaging the runs yields about 25.5% less time
(34% higher throughput), not a guarantee outside this workload. Selected Wasm
capacity is 878.5 MiB, within the control range; this is not total browser memory.
The selected production Wasm is 4,564,302 bytes, JavaScript 324,103 bytes and
the 22-file site 4,991,764 bytes. The speed-prioritized baseline keeps 5% size
headroom, the 24 raw-export cap and nine application exports.

Selected production clean CPU time is about 10.55 ms in canonical/frontend work
and 12.82 ms in later renderer submission. A separate O2/full-LTO diagnostic
control profile isolates scene build at 8.30 ms, including dynamic submission
4.04 ms, and backend submission at 9.99 ms; these nested stages are not additive.
Its six GPU-stage means span 0.08-1.23 ms with complete queries. This evidence
points to substantial CPU work; it does not establish a whole-frame GPU bound
or justify a renderer/API/quality change.

## Prior-phase realtime cadence (Chrome 152)

That phase's production A/B/B/A runs use the 45-second warmup defined above and
at least 60 wall seconds each. Controls and selected builds share exact source
inventories; served receipts, benchmark driver and graphics/environment checks
match within each map. These are advancing authored scenes with no external
input, not identical per-frame workloads or full mission completion.

| Map / run | Mean ms | Submission FPS | p95 ms | p99 ms | Target |
| --- | ---: | ---: | ---: | ---: | --- |
| Cargoship A1 | 25.141 | 39.776 | 28.200 | 29.555 | Miss |
| Cargoship B1 | 19.265 | 51.907 | 21.885 | 23.230 | Miss |
| Cargoship B2 | 19.255 | 51.935 | 21.885 | 23.055 | Miss |
| Cargoship A2 | 25.016 | 39.975 | 27.980 | 29.130 | Miss |
| Killhouse A1 | 11.470 | 87.181 | 13.850 | 14.715 | Pass |
| Killhouse B1 | 11.462 | 87.242 | 15.435 | 16.140 | Pass |
| Killhouse B2 | 11.446 | 87.370 | 15.590 | 16.325 | Pass |
| Killhouse A2 | 11.468 | 87.202 | 13.780 | 14.765 | Pass |

Cargoship repeats show about 30.2% more submission throughput (23.2% shorter
intervals) with improved p95/p99, but average FPS and p95 miss the requested
target. Selected canonical/frontend CPU averages 8.58 ms and later renderer
submission 10.49 ms, leaving more CPU work than the 16.67 ms frame budget.
Canonical/wall-clock ratios span 0.999925-1.000027. Selected Wasm capacity is
872.125/878.125 MiB versus 896.063/884.125 MiB controls, with no within-window
growth. Main-thread page rAF averages 7.65-7.67 ms; that is separate presentation
opportunity evidence and does not make a 51.9 FPS game render at 60 FPS.

Killhouse's defined idle view passes all three cadence thresholds in all four
runs, with exact camera/projection/world-geometry agreement at all 12 checkpoints.
Average throughput is effectively unchanged. The selected p95 is about 1.70 ms
higher and p99 about 1.49 ms higher than the control means, despite lower CPU
time; these tails remain inside the stated cadence limits. This is a measured
tradeoff, not a claim that every frame tail improves. Selected Wasm capacity is
978.625 MiB versus 978.688/978.750 MiB controls; canonical/wall-clock ratios span
0.999995-1.000007. This qualifies only the defined idle view, not combat/training
or general campaign gameplay. The overall Cargoship 60 FPS target remains unmet.

Raw accepted captures, startup/settings rejection logs, configuration archives
and comparison outputs stay under ignored `build/performance/` and
`build/renderer-efficiency-3a1aa20a-*.json`. That phase's final groups are
`final-production`, `final-realtime45-cargoship` and `final-realtime-camera-killhouse`;
use their accepted-file lists instead of selecting runs by favorable timing.
The initial private input ZIPs also copied inherited native-only SDK files. Those
bytes were removed from all 40 snapshots, and the source boundary now guards
local capture and verification. Original timing records and receipt JSON remain
unchanged; archived receipt refreshes record the original archive identity and
preserve identical public-source bytes. `snapshot-sanitization.json` records the
local correction. Final delivered receipts use the corrected capture path and
are checked against the measured engine/host byte identities.

## Chrome 153 continuation

The later local diagnostic comparisons use Chrome 153.0.8010.36 on the same
7800X3D/RTX 3070 Ti, ANGLE D3D11/32.0.16.1664 and 1920x1080. They are separate
experiments from Chrome 152 and from each other; do not combine their timings.
Accepted A/B/B/A lists are `next-shadow-cadence-abba`, `next-cloud-360-abba` and
`next-upload-360-abba`, each recorded under `build/performance/*-accepted.json`.
They retain exact fixed-work/camera/count equality, complete GPU queries and
matching source inventories, benchmark-driver hashes, settings and foreground checks.

`KISAK_PROFILE_CADENCE_MS` selects a browser launch before asset import or gameplay
timing: 240 page-rAF samples must average within 2% of the declared target. It
does not alter browser scheduling, the game cap or the comparator's 5% cadence
limit. The shadow group declares 4 ms; cloud/upload declare
2.7777777777777777 ms. These are callback opportunities, not monitor-refresh or
scanout evidence. Earlier shadow matrices failed cadence comparability; the
4 ms cloud attempts observed about 2.778 ms and rejected before import. They
remain unqualified. The replacement target was declared for a fresh full matrix;
startup/view failures also stay rejected, without relaxing work or provenance checks.

| Experiment | Clean interval ms, A/B/B/A | Retained evidence |
| --- | --- | --- |
| Shadow readiness groups, 4 ms target | 30.055 / 23.044 / 23.090 / 27.732 | Later submission CPU 16.03-16.07 to 12.26-12.33 ms; the first control also has a frontend outlier, so the entire interval difference is not an isolated shadow saving. |
| Direct clouds, 2.778 ms target | 26.720 / 26.773 / 25.987 / 26.685 | Total FPS is inconclusive; complete `sceneCommandAppendMs` averages about 1.171 to 0.669 ms. |
| Staged upload checks, 2.778 ms target | 26.304 / 26.051 / 25.705 / 26.003 | Total intervals overlap; geometry plus texture upload averages 1.973 to 1.903 ms, about 0.070 ms saved. |

The cloud control uses standalone build then append; both paths include the
shared empty-vector `resize` change, so that change has no isolated gain claim.
Direct `sceneCloudAppendMs` includes expansion, while the control starts that
timer after construction: compare the complete command-append stage. Upload
batching moves much of the wait from geometry to texture timing; its roughly
1 ms geometry reduction is not a 1 ms overall saving.

All three features default on; `-DisableShadowErrorBatching`,
`-DisableDirectCloudAppend` and `-DisableDynamicUploadErrorBatching` select
independent build controls. Loading keepalive now rejects
non-loading states before reading the browser clock, preserving its 33 ms loading
cadence; no isolated FPS benefit is assigned. Focused native/Wasm and browser
failure checks cover these boundaries. Updated production realtime comparisons
and final delivery qualification are still pending; these diagnostic results
do not establish the Cargoship 60 FPS target or campaign playability.

The subsequent production Cargoship A/B/B/A (`next-realtime-360-cargoship`)
uses the same declared 2.778 ms pre-import cadence and preserves settings,
foreground checks, source inventories and game-clock progression. All four
60-second windows pass the unchanged comparator. The user-facing production
menu was also open in the Codex browser throughout this group; these absolute
times describe that local session.

| Run | Mean ms | Submission FPS | p95 ms | p99 ms |
| --- | ---: | ---: | ---: | ---: |
| A1 | 18.698 | 53.483 | 21.415 | 22.645 |
| B1 | 17.466 | 57.255 | 20.160 | 21.415 |
| B2 | 17.639 | 56.692 | 20.315 | 21.575 |
| A2 | 18.616 | 53.717 | 21.220 | 22.405 |

The three continuation changes together save about 1.10 ms (5.9%) on average
against these fresh controls. The candidate remains 0.80-0.97 ms above the
16.67 ms mean budget; average FPS and p95 still miss the target. This is separate
from the Chrome 152 comparisons, not a new measurement of the original Oz build.
Killhouse repetition and owned context recovery for this candidate are pending.

## Cargoship follow-up on de737a0d

Fresh Chrome 153 production controls and the UI/FloatZ/combined-shadow candidate
use the same 1080p environment, graphics settings, 45-second canonical warmup
and 60-second realtime window. All four captures pass internal receipt,
foreground, clock and cadence validation. An explicit seven-file source-diff
allowlist compares the dirty candidate with its clean baseline; the stock
same-source comparator is not a cross-source qualification. These measurements
are separate from the preceding session's absolute FPS.

| Run | Mean ms | Submission FPS | p95 ms | p99 ms |
| --- | ---: | ---: | ---: | ---: |
| Baseline A1 | 23.536 | 42.488 | 27.850 | 29.570 |
| Candidate B1 | 21.861 | 45.743 | 26.120 | 27.985 |
| Candidate B2 | 22.051 | 45.350 | 26.330 | 28.250 |
| Baseline A2 | 21.826 | 45.816 | 27.040 | 29.640 |

The average interval is 0.725 ms (3.2%) shorter, but the baseline varies and
the second baseline overlaps the candidates. This does not establish a stable
mission-wide FPS gain. Candidate p95/p99 improve in these captures, yet the
60 FPS/20 ms p95 target remains unmet. Canonical/frontend CPU remains about
8.6 ms and later renderer submission about 13.0 ms. No native timing comparison
or full mission completion was measured. The initial UI/FloatZ-only production
run measured 23.411 ms; no isolated meaningful FPS gain is assigned to it.

The separate paused diagnostic pair retains all 120 canonical work-count and
camera/time samples, completes all 120 GPU queries, and produces byte-identical
PNG captures. UI draws fall from 102 to two, with unchanged 408 vertices/612
indices; UI CPU falls from 0.080 to 0.007 ms. Clean intervals are effectively
unchanged at 13.753/13.704 ms. This view has no spotlight maps, so it does not
isolate combined-shadow savings. Actual context loss/restoration preserves
all 12 resumed work-count samples and canonical shadow selection.

Raw captures are `build/renderer-efficiency-de737a0d-cargo-{before,shadow}-real-*.json`
and `cargo-{before,shadow}-paused.json` with the same prefix. Immutable build
snapshots, CPU sampling, source-diff comparisons and PNGs remain under ignored
`build/performance/cargo-*`. The CPU sample locates substantial time in WebGL
error queries; it is a diagnostic hotspot probe, not clean production timing.

The subsequent muted headless A/B/B/A compares that combined-shadow candidate
with pass-local lighting/raster/projection reuse and the 4096-slot texture memo.
All four fresh Chrome 153/D3D11 captures use native `ui_autoContinue`, identical
1080p settings, a 45-second warmup and a 60-second realtime window. They pass
receipt, hardware, clock, foreground and cadence checks, with a reviewed
13-file source-diff allowlist. These are separate from the headed captures.

| Run | Mean ms | Submission FPS | p95 ms | p99 ms |
| --- | ---: | ---: | ---: | ---: |
| Baseline A1 | 21.630 | 46.235 | 27.230 | 34.905 |
| Candidate B1 | 19.294 | 51.830 | 22.970 | 25.695 |
| Candidate B2 | 18.591 | 53.789 | 23.170 | 25.840 |
| Baseline A2 | 21.242 | 47.077 | 24.530 | 26.490 |

Mean frame time falls 11.6% (21.436 to 18.942 ms); renderer submission averages
12.76 versus 10.92 ms. Candidates still miss the 60 FPS/20 ms p95 target. The
post-window 10-frame GL probes show about 3,200 fewer scalar-uniform calls,
1,000 fewer matrix uploads, 4,200 fewer enable/disable calls and 1,300 fewer
texture-parameter integer writes per frame. Probe windows contain different
realtime frames, so draw counts are not an equality claim. Probe instrumentation
is excluded from clean timing. Wasm capacity is 923,467,776/923,533,312 bytes;
this is heap capacity, not process memory. Raw results use the
`cargo-auto-{a1,b1,b2,a2}` prefix and `cargo-auto-comparison.json` under `build/`.

Earlier Escape-based headless attempts had two zero-sample timeouts. No timing
was retained for those attempts. Native auto-continue avoids the key timing race;
the successful four-run comparison uses that same method throughout. These
results establish an opening/deck workload improvement, not full-mission or
native-performance parity.

The corresponding paused diagnostic pair (`cargo-auto-paused-{a,b}`) matches
all 120 camera/time/work-count samples and completes 120/120 GPU queries.
Actual context loss/restoration matches all 12 resumed samples and shadow
selection. Its post-recovery screenshot differs at two pixels by one byte unit;
a separate pre-recovery candidate capture (`cargo-auto-paused-pre-b`) is pixel-
identical to the baseline. The paused view is a dark ship/ocean camera and has
no selected spotlight maps; it does not establish full mission visual fidelity.
Paused timing varied (16.302 ms control, 13.917 and 18.992 ms candidate); use the
repeated production realtime windows above for the measured performance claim.

Memory reports retain current allocation/resource accounting, but cache the
immutable GPU identity per WebGL object and native context generation. Loss or
shutdown clears that identity; restoration reads it again even when the browser
reuses the JavaScript context object. Failed identity queries remain retryable.
This removes five driver queries from ordinary per-frame UI memory reports.

A fresh headless Chrome 153/D3D11 A/B/B/A against the state-reuse build measured
21.100/20.752/20.290/20.970 ms at 1080p, with the same 45-second warmup and
60-second realtime window. All captures pass receipt/environment/workload/clock
validation with a reviewed 13-file source-diff allowlist. Mean interval falls
2.4% (21.035 to 20.521 ms); candidates reach 48.19/49.29 submitted FPS versus
47.39/47.69 controls. Mean canonical/frontend time falls from 8.500 to 8.046 ms;
renderer submission is nearly unchanged (12.221/12.197 ms). Candidate p95 is
24.145/23.780 ms versus 24.915/25.575 ms. These are separate-session timings,
not additive percentage gains or native/full-mission parity. Post-window probes
confirm five identity parameter queries and one extension query become zero,
with one context-loss check. Wasm high-water capacity is 923,533,312 bytes in
both controls and B1; B2 reaches 926,416,896 bytes. Raw results and comparison
use the `cargo-identity-*` prefix under ignored `build/`.
The matching paused pair preserves all 120 camera/time/work samples and finishes
120/120 GPU queries. Its pre-recovery images differ at two pixels (three channels)
by one byte unit. Actual context loss/restoration preserves 12 resumed samples
and shadow selection. As before, this dark ship/ocean view selects no spotlight
maps and does not establish full-mission visual coverage.

The follow-up transient-light material/texture cache was rejected: a fresh
headless A/B/B/A measured 19.181/19.153/20.039/17.686 ms, with substantial control
variation and no repeatable improvement. Fewer GL calls alone did not justify
the additional state. Raw results use the `cargo-transient-*` prefix under
ignored `build/`; the retained renderer excludes that experiment.

Historical experiments, shader hashes and failures remain in
[Git history](../README.md#historical-records). Current suite results and optional
fixture failures belong in the [test inventory](web-test-inventory.md).

A subsequent fresh muted headless A/B/C/C/B/A compared the GPU-identity baseline,
shader-metadata reduction, and metadata plus `GL_STREAM_DRAW`, respectively.
Mean intervals were 21.477/21.486/24.456/27.054/19.067/21.752 ms with the same
1080p Cargoship realtime workload and driver. Both streaming-hint runs regressed;
the hint was removed and `GL_STATIC_DRAW` retained. Metadata alone overlapped
one control and improved in its other run, so no stable independent FPS gain is
claimed. The smaller backend records are retained. All six captures passed
internal timing, environment and reviewed source-difference checks; artifacts
use `cargo-geometry-*` under ignored `build/`. These remain opening/deck windows.

A dynamic geometry spare-set experiment passed five installed-Chrome atomic
upload, lighting, shadow and context-recovery tests. It removed two buffer and
one VAO allocations/deletions per frame while retaining the same four uploads
(including UI). An initial production pair measured 21.711/20.694 ms, but the
repeat candidate took 52.262 ms while another game was using substantial CPU.
The final control was cancelled by stopping only the owned benchmark process
tree. This is an inconclusive comparison, not evidence of a repeatable gain or
candidate regression. Spare storage reached 19,855,824 bytes in the first
candidate's retained telemetry window. The experiment was removed from current
source pending an uncontended comparison; snapshots and the complete patch use
`cargo-buffer-*` and `renderer-buffer-candidate-complete.patch` under `build/`.

The buffer-reuse trial was repeated on the retained sampler baseline.
A fresh production bridge-checkpoint CPU sample still locates significant time
in dynamic uploads and their driver checks: 985 ms in the dynamic upload error
query and 456 ms in its buffer uploads during a roughly ten-second sample.
Symbols were emitted from a byte-identical link of the measured Wasm. This is
diagnostic sampling, not an FPS comparison. The native bridge checkpoint reloads
from a fresh production session after the benchmark waits for command
acknowledgements; it uses a developer-positioned bridge view and does not prove
later combat progression. Evidence uses `cargo-bridge-*` under ignored `build/`.
The candidate passes five focused installed-Chrome checks, including reused
spare upload failures, caller-vector ownership and published GPU-byte retention.

The fresh opening/deck control and two candidates measured
18.012/17.164/17.274 ms. The closing control and its retry were rejected before
asset import because browser cadence changed from about 6.944 to 8 ms. No
samples from those rejected launches enter the comparison; this incomplete
sequence does not establish a stable opening/deck gain.

A separate muted headless Chrome 153/D3D11 A/B/B/A uses the native bridge save,
1080p, matching graphics settings and an approximately 8 ms browser cadence.
Admission is the first resumed canonical view plus 30,000 ms, followed by all
60 seconds of Worker frame timings. The first resumed view may differ by a
millisecond after loading; the laboratory validator retains raw times and
checks their relationship to admission instead of substituting opening times.
All four measured loads began at time 502257. The same save SHA-256, twelve
camera/projection/geometry checkpoints, uninterrupted world/context generations,
advancing game clock, build options and reviewed five-file source differences
are checked. All intrinsic frame-timing checks remain enabled.

| Run | Mean ms | Submission FPS | p95 ms | p99 ms |
| --- | ---: | ---: | ---: | ---: |
| Bridge control A1 | 11.397 | 87.746 | 17.175 | 18.505 |
| Buffer reuse B1 | 11.224 | 89.097 | 17.325 | 18.255 |
| Buffer reuse B2 | 11.107 | 90.033 | 17.180 | 18.200 |
| Bridge control A2 | 11.379 | 87.882 | 17.165 | 18.520 |

Mean interval falls 2.0% (11.388 to 11.165 ms), and measured CPU time falls
6.0% (9.152 to 8.601 ms). Tail latency is broadly unchanged. This repeatable
bounded gain justifies retaining the spare set; it does not establish native
parity or later combat performance. Post-window probes remove two buffer and
one VAO creations/deletions per frame while retaining all four geometry uploads.
Extra geometry storage peaks at 14,986,848 bytes in the bridge telemetry and
17,276,136 bytes in the first opening candidate. Bridge Wasm capacity remains
803,078,144 bytes; these counters are distinct from process memory. Immutable
builds, receipts, drivers, raw captures and comparisons use `cargo-buffer2-*`
under ignored `build/`. The saved bridge remains a developer-positioned scene.

The repeated buffer candidate also matches all 120 paused camera/time/work-count
samples and completes 120/120 GPU queries. Its pre-recovery image differs from
the sampler control at one channel of one pixel by one byte unit. Actual context
loss/restoration preserves all 12 resumed work-count samples and canonical
shadow selection; the candidate's before/after images are pixel-identical.
Paused mean intervals are 15.736/15.293 ms. This dark ship/ocean camera has no
selected spotlight maps and is limited renderer evidence, not gameplay FPS.
The five focused synthetic checks additionally cover failed reused uploads,
published GPU bytes, vector ownership, lighting and context recovery.

A subsequent frame-pump experiment reserved the next animation callback before
entering Wasm, retaining an explicit non-reentry guard and cancellation on stop
or error. Four Node scheduling checks and three installed-Chrome runtime/
responsiveness checks passed. A fresh saved-bridge A/B/B/A measured
11.109/11.075/11.243/11.224 ms with the same approximately 8 ms browser cadence.
All raw timing, camera, save, graphics, driver and source-difference checks pass.
The roughly 0.07% mean difference is within the controls' variation; gaps between
frames remain. The scheduling change and extra test were removed. Captures,
immutable builds and its reversible patch use `cargo-pump-*` and
`renderer-frame-pump-candidate.patch` under ignored `build/`.

Production world/dynamic command copies allocate only material names needed by
the sun-flare message; other labels remain diagnostic-only. The separate static-
model copy retains all fallback log labels. The earlier model/technique-label
removal was qualified as follows.
Canonical material/model identities, geometry and validation are unchanged.
The complete diagnostic site is byte-identical to the tested buffer-reuse
artifact, and five focused installed-Chrome checks pass. Fresh saved-bridge
A/B/B/A is 11.145/11.152/11.080/11.185 ms, with all existing comparison checks
passing. The small difference does not establish an additional FPS gain; this
is retained as removal of unused production work. Evidence uses `cargo-labels-*`
under ignored `build/`.
The matching production paused-camera pair is pixel-identical and preserves
the camera/time workload and ten-frame GL draw/upload call counts. This checks
the production-only compilation path; it is not a gameplay FPS comparison.

A later static-instance VAO pool trial is removed. A ten-frame Cargoship trace
contained 9,380 instance-layout updates for 338 distinct offsets. The candidate
used at most 1,024 VAOs over the same buffers, with frame-local offset reuse and
collision fallback. Three installed-Chrome checks passed, including base-VAO
rebinds, offset collisions, failed allocation cleanup and context restoration.
Fresh saved-bridge A/B/B/A is 11.279/11.489/11.462/11.302 ms at approximately
8 ms browser cadence. Raw timings, native save, graphics, camera/projection,
geometry, clock, driver and reviewed source-difference checks all pass. Mean
interval is 1.6% worse and renderer CPU time is 9.3% worse. The candidate's
paused GL probe reduces vertex-attribute pointer calls from 46,970 to 20,070,
but increases VAO binds from 6,260 to 16,560 over ten frames. All draw and buffer
upload counts remain equal. The fresh production paused-camera pair passes the
same workload/environment checks and is pixel-identical; paused timing is not
gameplay FPS evidence. This does not justify the extra resources or code;
the renderer and extra fixture were restored to the retained label-cleanup
version. Artifacts use `cargo-instance-*`, with the reversible
`renderer-instance-cache-candidate-complete.patch` under ignored `build/`.

A subsequent UI spare-buffer trial is also removed. It reused the existing
unpublished geometry transaction for the UI VAO/VBO/IBO, preserving saved-screen
ordering and both published CPU and GPU bytes on failed uploads. Seven focused
installed-Chrome checks passed, covering vertex/index/texture faults, recovery,
unload, text and saved-screen pixels. Fresh opening/deck A/B/B/A with native
intro skip, 1080p and approximately 8 ms browser cadence measured
17.487/21.138/17.547/17.244 ms. All raw timing, graphics, clock, driver and reviewed
source-difference checks pass, but neither candidate improves on the controls;
the cause of the slower first candidate is not established. There is no stable
benefit to retain. A later source correction for the inactive-UI recovery memory
counter was prepared but never built or tested before the whole trial was
removed. Immutable trial builds, raw captures and reversible patches use
`cargo-ui-buffer-*` and `renderer-ui-buffer-*` under ignored `build/`. The
existing label-cleanup runtime remains the retained build; bridge timing of
this trial was deferred and is not evidence.

An isolated native `devsave cargo_perf_deck01` now covers the deck after bridge
and crew quarters. Traversal uses keyboard input and developer positioning;
it is not an unassisted playthrough or full-mission evidence. Its SHA-256 is
`0f4d661081bd14a140ff80f45607f259f5c6ed22646b42c4fb8bbbf09692faa6`.
All 120 diagnostic frame samples and their GPU queries completed. Mean CPU
costs include 0.677 ms geometry checking, 0.949 ms cloud expansion and 1.208 ms
dynamic geometry upload; buffer uploads average 14.6 MB/frame. The save,
route, profile and identities remain under ignored `build/performance/`.

Two function-scoped SIMD finite-vertex trials are removed. Native and Wasm
surface checks covered every component, SIMD tails, finite extremes and both
signs of NaN/infinity; a focused Chrome upload-failure check also passed.
V1's fresh production deck A/B/B/A at approximately 8 ms browser cadence is
18.749/18.791/19.034/18.650 ms. V2 accumulates SIMD invalid flags before one
scalar reduction, halving the isolated scan from about 0.52 to 0.24 ms, but
fresh production A/B/B/A is 17.931/17.847/17.935/17.746 ms: no consistent game
gain. That second series uses approximately 2.78 ms browser cadence; its
initial 8 ms request was rejected before import. Do not compare absolute FPS
across the two series. Both completed series pass raw timing, graphics, save,
camera, geometry, clock, environment, driver and source-difference checks.
The retained label runtime remains unchanged. Artifacts and reversible patches
use `cargo-finite-*` and `renderer-finite-*`; no cloud GPU change is included.

The subsequent retained-cloud change follows the native GPU lattice path.
Fresh production deck A/B/B/A at approximately 2.78 ms browser cadence measures
17.909 / 14.529 / 14.655 / 17.665 ms, reducing mean frame interval by 18.0%
(about 56 to 69 FPS). Mean p99 falls from 26.56 to 18.27 ms. All four 60-second
captures pass raw timing, graphics, save, camera, geometry, clock, browser,
driver and reviewed source-difference checks. Wasm heap capacity high-water is
about 936 MB in controls and 803–805 MB in candidates; this is linear-memory
capacity, not total process memory. Artifacts use `cargo-cloud-fast-deck-*`.
An earlier 4 ms series completed A/B/B at 16.933 / 13.742 / 13.884 ms, then
rejected its final control before import when cadence changed to 2.78 ms.
That partial series is not the accepted four-run comparison.

A matched paused deck comparison verifies all 120 canonical views and logical
draw/triangle counts. Thirty-six clouds remove 147,456 streamed vertices and
221,184 streamed indices; buffer uploads fall from 13,853,184 to 2,351,616 bytes
per frame. Mean cloud preparation falls from 0.950 to 0.010 ms, geometry checking
from 0.586 to 0.098 ms, and geometry upload from 0.661 to 0.190 ms. Image-pool
linear comparisons differ by 30 (98,874 versus 98,844); lookup calls, hits,
texture uploads/binds and all logical draw counters match. The 1,878×1,057
rendered-canvas captures differ by at most one color level on 21 pixels;
actual context recovery differs on seven pixels within the same one-byte
raster tolerance and preserves 12 matching work samples and shadow selection.
These checks use `cargo-cloud-paused-deck-*` and `cargo-cloud-image-comparison`.
The saved encounter uses developer-assisted traversal and establishes neither
full-mission performance nor native-scene parity.
The paused helicopter-opening regression view emits no clouds. At approximately
4 ms cadence, A/B/B/A measures 12.865 / 13.085 / 13.052 / 13.124 ms; this short
diagnostic comparison establishes no consistent gain or cost. All views and
logical work counts match. The first candidate image differs by one byte on
one pixel and its restored image is identical. Artifacts use
`cargo-cloud-paused-opening-*`; these paused timings cannot qualify gameplay FPS.

A following brush-placement reuse trial is removed. It skipped exact duplicate
rotation/origin attributes and instance-enable uniforms only between adjacent
brush draws. A focused Chrome check verified actual attributes, VAOs, rotation,
translation, non-brush/cloud transitions, shadow-program state and context
recovery. Fresh production deck A/B/B/A at approximately 4 ms cadence measures
13.582 / 13.564 / 13.508 / 13.674 ms, a small 0.68% mean difference. All workload,
clock, environment and source checks pass, but the matched paused pair is
12.432 / 12.420 ms and profiled dynamic/shadow stages are essentially unchanged.
The paused probe saves 1,950 attribute writes and 833 scalar uniforms per frame;
all draw/upload counts match. Images differ by at most one byte on 32 pixels,
and actual recovery differs on 12 pixels within that tolerance. This does not
establish a useful, consistent frame-time gain over the retained cloud build.
The implementation and extra fixture are removed; captures and a reversible
patch use `cargo-brush-*` and `renderer-brush-placement-candidate.patch` in
ignored `build/performance/`.

A fresh retained-cloud deck probe measures 13.797 and 13.756 ms in separate
60-second production windows at approximately 4 ms browser cadence. These are
current baseline observations, not an optimization comparison. Subsequent
10-second Worker sampling attributes 3.929 seconds to `getError`, including
3.572 seconds beneath `WebRenderer_DrawFrame`. Symbol names come from
`--emit-symbol-map` on a byte-identical final Wasm link; the pre-Binaryen linker
map does not map final instruction offsets reliably. A separate 180-frame
error-query-only probe observes roughly 5.73 ms/frame across all checks, mostly
FloatZ completion and final multisample resolve. CPU sampling and instrumented
query times are diagnostic evidence, not clean gameplay FPS or removable cost.
Publication and immediate consumers still require their existing checks. Local
artifacts use `cargo-cloud-deck-cpu`, `cargo-cloud-deck-waits` and `cloud-symbols`.

Fresh image-hint production profiles at the first and later deck checkpoints
measure 12.468 / 9.830 ms in their clean realtime windows. Subsequent 10-second
Worker samples attribute 3.454 / 3.518 seconds to `getError` and
0.457 / 0.739 seconds to `DrawShadowPartition`. These are baseline observations,
not before/after gains. Final-link symbols again match the measured Wasm bytes.
Local evidence uses `cargo-current-deck-cpu`, `cargo-current-deck02-cpu` and
`current-symbols`. A separate muted headless Chrome capability check on the
same RTX 3070 Ti/D3D11 reports both base-instance draw extensions unavailable;
no extension-dependent path or browser launch flags are added.

The private `cargo_perf_bridge01` save adds the bridge firefight reached by
developer positioning and ordinary deck movement, with six enemies and live
canonical mission scripts. It is not an interior checkpoint. A fresh muted
headless production reload verifies the save hash, exact eye position, world
geometry, graphics, clock and receipt. Native `setviewpos` faces along the ship
during warmup, preserving scripted roll while making the forward vector
repeatable. At the newly observed 2.778 ms browser cadence, its 30-second
canonical warmup and 60-second window measure 8.061 ms mean, 11.625 ms p95 and
13.625 ms p99 (124.06 submission FPS at `com_maxfps 125`). These are new baseline
observations, not an improvement comparison with earlier 4 ms cadence runs.
A separate diagnostic reload measures 8.087 ms before instrumentation, then
completes 120 profiled frames with matching canonical views and 120 GPU results.
Instrumented backend time averages 4.774 ms; sun/spot shadow submission is
0.720/0.853 ms, dynamic-model submission 0.919 ms and scene building 2.473 ms.
These stages overlap and must not be summed as independent frame costs.
Evidence uses `cargo-bridge-360-forward-a1`, `cargo-bridge-360-profile-a1r`
and `cargo-bridge-progression-evidence` under ignored `build/`. The initial
4 ms cadence rejection, angled-camera mismatch and diagnostic-driver mode
rejection remain preserved. The earlier exploratory profile collected only
101 frames and is excluded. No new optimization, interior coverage, native
parity or whole-mission performance claim follows from this baseline.

The retained shadow-loop change moves the existing leading visibility scan
ahead of static-model material setup. Empty batches issue no material calls;
visible instance ranges, caster tests and draw order remain identical for sun,
baked-spot and transient-spot passes. It adds no retained cache or GPU resource.
Fresh muted headless Chrome production A/B/B/A at approximately 4 ms cadence
measures first-deck intervals 12.699 / 12.470 / 12.551 / 12.553 ms: mean frame
time falls 0.92% and renderer submission falls 1.18%. One candidate nearly ties
the final control. The later deck measures 9.790 / 9.799 / 9.855 / 9.839 ms;
timings overlap, and the candidate mean is 0.13% slower. This establishes no
later-deck gain. The repeated candidate first rejects before import at
3.894 ms cadence, then passes unchanged limits on a fresh launch. Workload,
source, graphics, environment and game-clock comparisons pass for both sets.

The matched paused pair preserves all 120 camera/work/counter samples except
the intended texture-bind reduction and completes all 120 GPU queries.
Ten-frame GL probes retain all draws and seven error checks per frame while
omitting 896 texture binds, 896 active-unit selections, 184 scalar uniforms,
30 texture parameters and 112 raster-state calls per frame. Sun/static shadow
submission falls from 0.428 to 0.380 ms and spot/static from 0.296 to 0.194 ms
in the diagnostic sample. These timings include instrumentation. Images differ
by at most one byte on 43 pixels before recovery and 77 after; actual recovery
retains 12 work samples and canonical shadow selection. Evidence uses
`cargo-shadow-empty-*`; images remain private under ignored `build/performance/`.
This is a small first-deck optimization, not native parity or whole-mission
performance qualification.

A fresh retained-build deck profile at 2.778 ms headless browser cadence
measures 17.197 ms mean and 21.935 ms p99 (58.15 submitted FPS), with a
1.000075 canonical/wall-clock ratio. Its subsequent ten-second Worker sample
attributes 3.584 seconds to `getError`, including 3.196 seconds beneath
`WebRenderer_DrawFrame`, and 0.366 seconds to `DrawShadowPartition` itself.
Final-link symbols match the retained shadow-empty Wasm byte-for-byte
(`18f56fd0df5d2d181735b194e94693b3e6c162bd9352e5e8bd4efa22e86c7632`).
Evidence uses `cargo-shadow-360-deck-cpu` and `shadow-empty-symbols`. These
are fresh baseline observations; the earlier 4 ms series is not comparable,
and sampled graphics waits are not safely removable error-check overhead.

A temporary spotlight-membership bitmap trial is removed. It replaces the
per-packed-instance binary search with one local 8 KiB bitmap for the selected
light, retaining the search fallback for larger canonical IDs. Native and
direct-Wasm world/static-model checks pass, including 4,096 generated mask
comparisons covering shuffled/duplicate IDs, absent lights and integer edges.
Both Release builds pass. Fresh headless muted Chrome/D3D11 production deck
A/B/B/A at 2.778 ms cadence measures 16.074 / 16.286 / 16.318 / 16.373 ms;
candidate mean frame time is 0.48% slower and renderer submission is 0.40%
slower. Runs overlap, with no useful improvement. Every capture passes save,
camera, graphics, clock and environment checks. Source comparison initially
flags three documentation updates already present before this trial; their
archived contents were reviewed before admitting those documentation-only
differences. No workload or timing tolerance changed. The implementation and
extra fixture are removed, so paused pixel/recovery qualification is unnecessary.
Evidence uses `cargo-spot-membership-*`, `spot-membership-{original,candidate}`
and `renderer-spot-membership-rejected.patch` under ignored `build/performance/`.
This rejects this implementation; it establishes neither native parity nor
exhausted optimization opportunities. Interior/whole-mission coverage remains open.

A separate disposable shadow-attribute probe omits attribute 9 pointer updates
only where shader reflection confirms it unused. It saves 531 pointer calls
per frame while retaining 4,996 draws and seven error checks. Paused A/B/B/A
measures 11.912 / 12.022 / 11.949 / 11.876 ms, with no gain; all four images
differ by at most one byte per channel. No engine change is made. Evidence
uses `cargo-shadow-attribute-probe-*` under ignored `build/performance/`.

An early-axis-exit shadow-bounds trial is removed. It retains each arithmetic
operation and comparison but calculates W first, stopping at the first outside
axis. Native/Wasm world and static-model tests pass 4,096 generated cases plus
exact/adjacent clip-plane contacts, signed zero and finite-input overflow.
Sixteen focused browser checks pass. The paused pair preserves all 120 views,
work/counters and ten-frame GL calls, completes 120 GPU queries and recovers
12 work samples; images differ within one byte on 63/65 pixels before/after
recovery. Production deck A/B/B/A measures
12.675 / 12.517 / 12.576 / 12.519 ms, but renderer submission averages
8.751 / 8.757 ms. The small whole-frame difference comes from canonical CPU
variation, and the paused renderer is also slightly slower. No useful renderer
gain is established. The implementation and extra fixture are removed; evidence
and the reversible patch use `cargo-shadow-axis-*`, `shadow-axis-rejected/`
and `renderer-shadow-axis-rejected.patch` under ignored `build/performance/`.

A partial texture-parameter update trial is also removed. It memoized effective
filter/anisotropy/wrap values in the existing frame-local table and emitted only
changed parameters, preserving ordered bindings and all error checks. Native
and Wasm memo checks pass, as does the expanded bundled-Chromium graphics case.
Installed Chrome passes the sampler assertions but reproduces the unchanged
baseline's one-byte normal-lighting pixel mismatch (`0xbf7f40` versus `0xbf8040`).
Fresh production deck A/B/B/A at approximately 4 ms cadence measures
13.690 / 13.742 / 13.856 / 13.761 ms: the candidate is 0.53% slower on average,
with no tail benefit. Provenance, workload and clock checks pass. The first
post-window probes reduce parameter calls, but those realtime probes contain
slightly different draw counts and do not establish an isolated saving. No
paused-image/recovery qualification was needed after rejection. The four touched
files were restored byte-for-byte; local evidence and the reversible patch use
`cargo-parameter-*` and `renderer-parameter-candidate.patch`.

A subsequent early-submission trial adds `glFlush` after the static-model
camera family, before dynamic draws, retaining every target-completion check.
The production probe confirms three flushes per deck frame. Fresh deck
A/B/B/A at approximately 4 ms cadence measures
13.713 / 13.848 / 13.955 / 13.661 ms; the candidate is 1.57% slower on average.
All four windows pass provenance, environment, camera/workload and game-clock
checks. The change is removed; it demonstrated no gain worth further paused
pixel/recovery qualification. Local artifacts and the reversible patch use
`cargo-submit-*` and `renderer-submit-candidate.patch`. The intermediate
`cargo-flush-production` snapshot is byte-identical to the cloud control after
an insertion guard rejected mismatched line endings; it is not a candidate.

The retained FloatZ specialization compiles the existing depth branches as a
separate program, letting the driver remove unused camera shading. Geometry,
alpha threshold, signed depth packing, raster/texture state and completion
checks stay shared. Fresh muted headless Chrome 153/D3D11 production deck
A/B/B/A at approximately 4 ms cadence measures
13.644 / 12.648 / 12.743 / 13.638 ms: 6.9% shorter mean frame intervals,
about 73.3 to 78.8 submission FPS. Mean p99 falls from 17.08 to 15.92 ms.
Both candidates beat both controls; all four 60-second windows pass raw timing,
graphics, save, camera, geometry, canonical clock, environment, driver and
reviewed source-difference checks. The frontend source difference is comment
cleanup only. Artifacts use `cargo-depth-deck-*` under ignored `build/performance/`.

The matched paused deck pair measures 12.373 / 11.428 ms. Profiled renderer
backend time falls from 9.522 to 8.681 ms, with all 120 canonical views and
logical work samples equal and all 120 GPU queries complete. Draws, uploads
and texture calls match; the separate program adds one restoration per frame.
The ten-frame GL probe confirms that only `useProgram` changes (170 to 180).
Image-pool comparisons differ by 30 while calls/hits match. Before images
differ by at most one byte on four pixels; actual context recovery differs on
57 pixels within that tolerance and preserves 12 logical work samples and
canonical shadow selection. These checks use `cargo-depth-paused-deck-*` and
`cargo-depth-deck-image-comparison`. This is one developer-assisted saved
encounter; native parity and performance across the mission remain unverified.
The independent paused helicopter-opening pair measures 12.964 / 11.466 ms.
All 120 canonical views, work samples and GPU results match; the ten-frame GL
probe again differs only by program restoration (110 to 120 calls). Images
differ by one byte at one pixel both before and after actual recovery, which
preserves 12 resumed samples and shadow selection. Artifacts use
`cargo-depth-paused-opening-*` and `cargo-depth-opening-image-comparison`.
These short paused timings corroborate renderer savings, not gameplay FPS.

The later `cargo_perf_deck02` save reaches the open deck near the tilted
containers through developer positioning and live canonical combat/script
progression. At the same 4 ms target cadence, fresh production A/B/B/A measures
10.481 / 9.939 / 9.979 / 10.485 ms: 5.0% shorter mean intervals, about
95.4 to 100.4 submission FPS. Mean p99 falls from 13.75 to 13.29 ms; Wasm
capacity is 702,087,168 bytes in all four runs. Both candidates beat both
controls, with all raw timing, graphics, save, camera, geometry, clock,
environment, driver and source checks passing. One B2 launch rejects before
import at 3.898 ms cadence; its fresh retry passes the unchanged 4 ms admission
limits. Evidence uses `cargo-depth-deck02-*`, `cargo-deck02-progression-evidence`
and the private local save. Its exploratory 120-frame profile is live, not
paused: native rejects the attempted direct write to read-only `cl_paused`.
This adds a second saved encounter, not whole-mission or native parity evidence.

A disposable same-scene probe also removes the now-unused FloatZ branches from
the main camera shader while keeping the separate depth program. Its paused
A/B/B/A intervals are 11.497 / 11.605 / 11.478 / 11.425 ms, with no renderer
gain. It is not implemented; `cargo-camera-probe-*` retains the local trial.

A fresh production deck capture on the retained depth build measures 12.628 ms
over 60 seconds with a 1.000048 game-clock ratio. A subsequent 180-frame narrow
wait probe measures 4.801 ms/frame across existing error checks. Regular-frame
FloatZ completion falls from the earlier cloud build's 2.007 to 1.123 ms; final
multisample resolve remains 2.837 ms. These checks include preceding graphics
work and remain required. Evidence uses `cargo-depth-deck-waits-*`.

Further disposable paused-deck shader probes keep the shared source arithmetic
and replay uniform values at program transitions in both arms. Specializing
world placement/model lighting to zero measures A/B/B/A
11.721 / 11.653 / 11.722 / 11.597 ms, with no gain. Specializing material mode
zero measures 11.923 / 11.645 / 11.822 / 11.979 ms. A separate cost probe repeats
the small advantage at 12.054 / 11.757 / 11.859 / 11.976 ms, with roughly 0.2 ms
less graphics wait, but its 69 uniform replays per frame cost about 0.3 ms in
both arms. That extra work is absent from production, so this does not qualify
a production improvement from straightforward program switching. Restricting
specialization further to alpha-test-disabled mode-zero draws is slightly
slower: 12.063 / 12.070 / 12.143 / 12.002 ms. All sixteen trial images differ
from their session's original image by at most one byte per channel, GL checks
pass, and material-mode draw counts match where recorded. No additional shader
variant is retained. Local evidence uses `cargo-world-shader-probe-*`,
`cargo-material-shader-probe-*`, `cargo-material-cost-probe-*`,
`cargo-opaque-shader-probe-*` and `cargo-extra-shader-probe-comparison`.

A later disposable probe tracks per-program uniform versions instead of
replaying every value at every material transition. Mode-zero specialization
measures paused A/B/B/A 11.605 / 11.473 / 11.505 / 11.559 ms. A narrower
mode-zero/non-instanced/non-model-lit variant initially measures
12.144 / 12.255 / 12.284 / 12.129 ms, but the probe's repeated name scans add
CPU work and alter graphics overlap. Replacing those scans with direct lookup
changes the narrower result to 11.593 / 11.494 / 11.458 / 11.575 ms; the broad
variant repeats at 11.490 / 11.355 / 11.411 / 11.521 ms. Thus the initial
narrow-variant rejection is not robust to instrumentation overhead. The direct-
lookup broad candidate replays about 822 uniforms in 69 transitions per frame,
costing 0.115 ms, compared with the earlier unconditional replay's 0.30 ms.

Comparing exact uniform values before advancing their versions further reduces
the broad candidate to 270 replays per frame and 0.079 ms of replay/scan time.
Its paused A/B/B/A measures 11.624 / 11.442 / 11.500 / 11.651 ms; renderer time
averages 8.821 / 8.621 ms. The instrumented control has no steady-state replay
writes and spends about 0.047 ms scanning. Every trial retains material-mode
draw counts and all seven error checks per frame. All twenty trial images
differ from their respective session's original by at most one byte per
channel. These are feasibility measurements in a paused scene, with JavaScript
instrumentation in both arms, rather than production performance evidence. The
retained runtime remains the image-index-hint build. Evidence uses
`cargo-material-lazy-*`, `cargo-world-material-lazy-*` and
`cargo-material-value-probe-*` under ignored `build/performance/`.

The subsequent C++ implementation reflects camera uniforms once, compares exact
values, and replays only dirty values when switching between the original and
mode-zero programs. It preserves ordinary writes and falls back to the original
program for unexpected uniform calls. Both Release builds and all 15 graphics
checks pass, including a new real-GL matrix/partial-array synchronization and
invalid-type fallback check before and after context recovery. Fresh muted
headless Chrome/D3D11 production A/B/B/A at approximately 4 ms cadence measures
12.490 / 13.051 / 12.919 / 12.644 ms in the first deck encounter. The candidate
is slower than both controls: mean frame time increases 3.33%, from 12.567 to
12.985 ms, and renderer submission time increases from 8.801 to 9.176 ms.
Source, environment, camera, graphics and game-clock comparisons pass. The
implementation and its extra fixture are removed; paused visual comparisons,
later-deck qualification and broad candidate suites are unnecessary after this
production regression. The patch, build snapshots and evidence remain under
ignored `build/performance/` as `renderer-material-native-rejected.patch`,
`material-native-rejected/` and `cargo-material-native-*`. This rejects this
implementation; it does not establish that all shader specialization is slower.

A separate disposable scalar-uniform probe skips only repeated finite values
at resolved scalar `GL_FLOAT` locations and invalidates on `uniform1fv` writes.
It removes 3,279 of 4,211 scalar uploads per frame while retaining 4,996 draws
and all seven GL error checks. Paused A/B/B/A measures
11.532 / 11.491 / 11.517 / 11.564 ms, a mean interval difference of only
0.044 ms; renderer submission averages 8.754 / 8.660 ms. All four images
match the pre-probe image within one byte per channel (20/21/77/9 changed
pixels). Both arms include JavaScript cache bookkeeping, so these small
differences do not establish a production gain. No scalar cache is added to
the engine. Local evidence uses `cargo-scalar-uniform-probe-*`.

The retained compact-key camera sorter replaces repeated batch/Material key
reads during comparisons. Native and Wasm DObj tests compare 512 generated
inputs with the original stable-sort policy, covering empty input, singleton runs,
ties, anchors and distinct adjacent groups. Installed Chrome's authored
distortion/order/recovery fixture passes. Fresh muted headless Chrome 153/D3D11
production A/B/B/A at approximately 4 ms cadence measures first-deck intervals
12.796 / 12.613 / 12.767 / 12.853 ms: 1.05% shorter on average, with 2.41% less
canonical CPU time. The later deck measures 9.971 / 9.904 / 9.839 / 10.029 ms:
1.29% shorter, with 2.80% less canonical CPU time. Both candidates beat both
controls in each encounter; mean p99 improves from 16.225 to 15.918 ms and
13.393 to 13.263 ms, respectively. Every provenance, settings, camera, geometry,
environment and game-clock comparison passes. One later-deck control launch
rejects before import at 3.895 ms cadence; the fresh retry passes unchanged
limits. Evidence uses `cargo-sort-deck-*` and `cargo-sort-deck02-*`.

The matched paused deck pair measures 11.488 / 11.424 ms. Profiled dynamic draw
construction falls from 0.336 to 0.256 ms, while all 120 views/work samples and
all ten-frame GL call counts match. Both captures drain all 120 GPU queries.
Images differ by at most one byte on 30 pixels; actual context recovery differs
on 80 pixels within that tolerance, retaining 12 work samples and canonical
shadow selection. Local evidence uses `cargo-sort-paused-*` and
`cargo-sort-deck-image-comparison`. The temporary key list is not retained
recovery data; no memory saving or whole-mission/native-parity claim is made.

The subsequent production-only material-label removal leaves the entire
diagnostic site byte-identical to the retained compact-sort build. Production
deck A/B/B/A at approximately 4 ms cadence measures
12.536 / 12.508 / 12.485 / 12.581 ms. Canonical CPU time averages
3.574 / 3.516 ms (1.62% lower); the 0.49% mean frame-interval difference is
small and is not a substantial FPS improvement. All existing provenance,
settings, camera, geometry, environment and game-clock comparisons pass.
The matched paused production pair preserves every ten-frame GL call count;
images differ by one byte on ten pixels. Its canonical CPU time falls from
2.584 to 2.479 ms, while renderer variation reverses the overall frame-time
difference (11.385 / 11.477 ms). This is retained as removal of unused production
work, preserving diagnostic labels and the production sun-flare/static-fallback
messages. Local evidence uses `cargo-material-label-*`; no memory saving is
claimed from the varying observed heap capacities.

The retained image-index hint removes repeated linear searches in
`RetainCanonicalWorldImage`. Its 1,024 integer slots cost 4 KiB and own no
canonical identities or GL objects. Every hinted index is checked against the
current pool's bounds and canonical identity. Pool construction and UI
replacement preserve unique identities, so a valid hint preserves first-match
semantics. A collision or stale index falls back to the original search;
admission, decoding, error handling and recovery remain unchanged.

Fresh production first-deck A/B/B/A at approximately 4 ms cadence measures
12.696 / 12.653 / 12.623 / 12.680 ms: mean intervals fall 0.39%, with canonical
CPU averaging 3.609 / 3.593 ms. Later-deck captures measure
9.873 / 9.835 / 9.890 / 9.927 ms: mean intervals fall 0.38%, with canonical CPU
averaging 3.107 / 3.078 ms. Both first-deck candidates beat both controls;
later-deck frame times overlap, and first-deck CPU time is mixed. These are
small mean differences, not a substantial or mission-wide FPS claim. All
provenance, graphics, save, camera, geometry, environment and clock checks pass.

The matched paused diagnostic pair reduces comparisons from 98,844 to 1,748
per frame while retaining 1,219 lookups/hits. Lookup time falls from 0.225 to
0.173 ms and command batch copying from 0.453 to 0.398 ms. All 120 views/work
samples, other counters and ten-frame GL call counts match; both captures
drain all 120 GPU queries. Images differ by at most one byte on 28 pixels;
actual context recovery differs on 70 pixels within that tolerance, with 12
matching resumed work samples and the same shadow selection. Local evidence
uses `cargo-image-hint-*`. Observed heap capacities vary; the hint adds fixed
storage and does not establish a memory saving or native performance parity.

The retained sampler change targets the 12,540 of 16,579 parameter-update groups
that touched one fallback object in a ten-frame Cargoship probe. Pass bindings
retain unit order but use each aliased object's final sampler at every binding;
no draw or texture read occurs inside that sequence. This avoids temporary
sampler changes while preserving the original last-write result, including
mipmap flags and disabled slots. The existing unconditional binding oracle
passes in native and Wasm surface/world tests, with added split/disabled alias
transitions and a check that six aliases require only one parameter update.
Production and diagnostic builds, Node and routine pinned-browser suites pass;
installed Chrome retains the two previously reproduced one-byte exact-pixel
differences recorded in the test inventory. Fresh retail evidence below confirms
the reduced API work and preserved images/recovery; no stable independent
production FPS gain is claimed.

An additional, now removed backend candidate submitted consecutive camera ranges of one world
batch through `WEBGL_multi_draw` when available. It preserves each range's count
and byte offset rather than joining across visibility holes. Material-pass order,
FloatZ ownership, raster/texture state and logical draw/index counters remain
unchanged. Scratch storage holds 64 ranges at a time; the tail and unsupported
contexts use ordinary draws. Water keeps its individual upload/error boundary.
Capability discovery runs at context creation and after recovery reset.
Native/Wasm grouping tests verify order and distinct-batch barriers. The existing
multiply-fog pixel fixture adds extension/fallback paths, a colored visibility
hole and a 65-draw chunk/tail comparison, recording the actual browser calls.
Installed Chrome reports the extension available and passes the expanded pixel
fixture before and after real context loss. Native/Wasm renderer tests, Node,
smoke, remainder, production and product-boundary suites pass; current counts
and limitations are recorded in the test inventory.
The fresh muted headless Chrome 153/D3D11 A/B/C/C/B/A used the same 1080p
Cargoship settings, 45-second warmup and 60-second realtime window. A is the
metadata baseline, B adds sampler alias resolution, and C adds multi-draw.
All six pass raw timing, canonical clock, workload, environment, driver identity
and reviewed source-difference checks.

| Run | Mean ms | Submission FPS | p95 ms | p99 ms |
| --- | ---: | ---: | ---: | ---: |
| Baseline A1 | 20.377 | 49.076 | 24.345 | 27.055 |
| Sampler B1 | 19.995 | 50.012 | 23.630 | 25.420 |
| Multi-draw C1 | 19.883 | 50.293 | 23.335 | 25.350 |
| Multi-draw C2 | 19.994 | 50.016 | 23.910 | 26.120 |
| Sampler B2 | 21.466 | 46.585 | 26.870 | 29.000 |
| Baseline A2 | 24.518 | 40.786 | 28.220 | 30.560 |

The baseline changes by 20.3% across the session, including canonical CPU
7.944 to 9.777 ms. Other application load was observed during the final control;
the observation does not establish causality for individual frames. These
captures do not establish a stable new FPS percentage. No run meets the
60 FPS/20 ms p95 target. Both multi-draw runs overlap the first sampler run.

A separate diagnostic comparison uses the earlier GPU-identity baseline,
sampler candidate and multi-draw candidate at the same paused camera/time.
All 120 work-count/view samples match, and all 120 GPU queries complete for
each build. Across ten matching frames, sampler parameter writes fall from
72,150 integer / 17,960 float calls to 17,359 / 4,262, with 43,690 logical
draws unchanged. The sampler and multi-draw pre-recovery images are identical;
each differs from the baseline at one channel of one pixel by one byte unit.
Actual context loss/restoration preserves all 12 resumed work-count samples
and canonical shadow selection for both candidates. The sampler recovery
changes that same one-byte pixel; the multi-draw recovery image is identical.

Paused mean intervals are 19.814/18.412/18.628 ms, respectively. This dark
ship/ocean view has no selected spotlight maps; it is limited renderer evidence,
not gameplay FPS or full-mission fidelity. Multi-draw does not show a consistent
additional timing benefit, so its code and extra fixtures were removed. The
smaller sampler change remains. Raw captures, GL probes, comparisons, PNGs and
the reversible multi-draw patch use `cargo-multidraw-*` and
`renderer-multidraw-candidate-complete.patch` under ignored `build/`.


The Cargoship intro investigation now captures 120 canonical seconds from the
first rendered gameplay frame, including helicopter approach and landing. All
runs are headless muted Chrome 153/RTX 3070 Ti/ANGLE D3D11 at 1080p and 2.778 ms
browser cadence, with canonical `ui_autoContinue 1`, seed 1 and `com_maxfps 125`.
No movement, camera or quality changes are made. These inherited settings include
4x AA, `r_drawWater 0` and `fx_marks 0`; they are not default-quality qualification.
The initial production observation is 13.233 ms mean/20.860 ms p99, or 75.57
submitted FPS. Diagnostic captures at 5/20/40/60/90 seconds each drain 120 GPU
results. Approach scene construction averages 5.54 ms (DObj 2.48 ms, skinning
1.47 ms); landing renderer time is about 10.05 ms, including 2.96 ms in postprocess.
Stages overlap and instrumented times are not clean FPS. Files use
`cargo-intro125-{baseline-a1r,diagnostic-a1}` under ignored `build/performance/`.

A later production A/B/B/A tests extending the existing bounded immutable
rigid-vertex decode cache to weighted surfaces. Native and direct-Wasm DObj tests
pass, including current-pose changes for all four influence counts, malformed
skin offsets/counts, nonfinite inputs, atomic failure and cache retirement. Both
Release builds pass. Full intro intervals are 10.960 / 11.795 / 11.009 / 10.983 ms;
both candidates are slower than both controls, with the repeated candidate nearly
tying them. The small early-approach reduction is not consistent across later
phases; average interval is 3.93% worse overall. This does not justify more cached
vertex data. The implementation and extra tests are removed byte-for-byte.
Artifacts, immutable sites and the reversible patch use `cargo-weighted-intro-*`
and `weighted-intro-rejected/`; prepared paused/recovery drivers are unexecuted.
Graphics before/after, driver, GPU, geometry, settled position, game clock and
reviewed source comparisons pass. The three pre-existing documentation changes
were read from `build-inputs.zip`; no runtime discrepancy was admitted.

The first repeated candidate fails unchanged intro coverage assertions: its
first rendered canonical time is 305, followed by 3500, leaving seconds 0/1/2
empty. Its raw capture and error log remain as `cargo-weighted-intro-b2`; `b2r`
is the fresh successful retry. Successful captures begin at canonical time 3500.
This startup variation needs separate qualification, not a relaxed assertion or
silent removal of frames. First-frame canonical/render CPU spans of roughly
678-726 ms, every one-second bucket and phase tails are retained separately from
mean inter-frame intervals. The later controls both average about 91 FPS; their
similarity establishes a useful local comparison, not a gain over the earlier
75.57 FPS observation. Neither series establishes stable 125 FPS or native parity.

A disposable paused landing resolve probe confirms DOF is actually disabled in
that scene. Original combined color/depth resolve versus separate color and depth
blits measures 10.801 / 10.870 / 10.925 / 10.909 ms; subsequent color-only trials
measure 10.879 / 10.875 ms against a 10.847 ms final control. Both outputs and all
seven error checks are preserved in split trials; unused depth is omitted only
in the color-only probe. All 4,001 draws/frame remain. No useful gain is observed,
so neither change is implemented. Six trial images match the pre-probe capture
within one byte/channel; the final unchanged control reaches two bytes on 42
pixels and therefore does not pass the existing one-byte fidelity tolerance.
No tolerance is changed. Evidence is `cargo-intro125-resolve-probe-*`.

Separate ten-second production Worker CPU captures cover canonical times
6825-16142 and 43519-53192. Matching final-link symbols attribute 3.176/3.779
seconds to `getError`, including 2.586/3.312 beneath frame drawing. Early
`R_RenderScene` self time is 1.174 seconds versus 0.523 later; LTO inlines several
frontend operations here, so these are not isolated skinning measurements.
They support investigating graphics-service work and dynamic scene conversion;
error checks remain required. Files use `cargo-intro125-cpu-a1-{3s,40s}` and the
byte-identical retained `shadow-empty-symbols` final link.

Two disposable GPU-service traces follow the same retained-build landing view.
The detailed three-second trace produces 645,041 events and heavily distorts
execution (304 error checks); it cannot qualify per-draw cost. The lighter
`gpu,gpu.angle,toplevel` capture produces 226,877 events, 1,496 error checks,
1.637 seconds in GPU command-buffer processing and 1.130 seconds in Worker
`GetGLError` spans. These overlap: the Worker waits while the separate GPU
service processes queued commands. No ANGLE function-level events are present,
so neither trace identifies an individual expensive draw/material or justifies
removing a check. Capture and drain span canonical times 45760-50227; this is
instrumented attribution, not a clean FPS window. Files use
`cargo-intro125-gpu-service-a1` and `cargo-intro125-gpu-light-a1`.

A later opaque world FloatZ range-merging trial is also removed. It joined
only contiguous index ranges with identical native FloatZ state and depth sign,
retaining alpha-test and visibility barriers. The paused opening pair removes
696 world draws and 1,079 texture binds per frame; all 120 views, canonical work
samples and GPU results match. Seven error checks per frame remain. Actual
context recovery preserves 12 work samples, and both candidate images differ
from control by at most one byte/channel at five pixels. Paused diagnostic mean
intervals improve from 13.485 to 12.564 ms, but fresh production intro A/B/B/A
measures **12.79841 / 13.25189 / 12.78744 / 12.64753 ms**: the candidate is 2.33%
slower overall. This does not justify keeping the extra merge loop. Source is
restored exactly; the trial and comparisons remain under ignored
`cargo-floatz-merge-*`, with `renderer-floatz-merge-rejected.patch`.

These runs use headless muted Chrome 153, the same RTX 3070 Ti/ANGLE D3D11,
1080p, 2.778 ms cadence, seed 1 and canonical cutscene skipping. Inherited AA 4,
water disabled and marks disabled remain unchanged. The initial candidate `b1`
is rejected for the same 305-to-3500 startup jump; `b1r` passes the unchanged
coverage assertions. Successful controls average 78.1/79.1 FPS, with p99
18.225/19.365 ms and first-frame CPU 692/762 ms. These results do not establish
stable 125 FPS. Raw samples show 4.7-5.0 seconds of simulation admitted after map
loading; the browser accumulator does not consume native `Com_ResetFrametime`.
That platform timing boundary requires separate correctness qualification.

The subsequent clock-reset fix is retained separately from renderer work.
`RunCGameFrame` samples the current platform clock after commands return, and
`WebFrameTiming` consumes the timestamp already written by native
`Com_ResetFrametime`. This clears pre-load admission debt without discarding
real gameplay stalls or changing the 5,000 ms suspension ceiling. Native and
direct-Wasm checks cover load/restart debt, post-reset elapsed time, two-second
gameplay stalls and uint32 clock wraparound.

Three fresh production startup checks begin at canonical times 307/307/309 with
8/8/10 ms simulation steps, instead of consuming the load as a 4.7-5.0 second
step. First-frame CPU remains 461/459/487 ms. The canonical `map_restart` check
also passes, starting at time 300 with a 15 ms admitted step and a 0.999985
simulation/wall-clock ratio over the following 20 seconds. Evidence uses
`cargo-clock-reset-startup-*` and `cargo-clock-reset-restart-b1-*`.

`cargo-clock-reset-intro-b1` covers the full 120 canonical seconds from its
first rendered frame with all one-second windows populated: **12.79888 ms /
78.13 FPS**, p99 **20.275 ms**. Loading no longer advances unseen intro gameplay;
this changes the covered interval, so it is a corrected baseline, not an A/B
speedup claim against earlier time-3500 starts. Graphics and cutscene-skipping
settings are unchanged. Stable 125 FPS and native timing parity remain unmet.

The matching final-link `clock-reset-symbols` capture isolates the remaining
startup cost. `cargo-clock-reset-startup-cpu` includes loading plus early
gameplay, so whole-profile totals must not be called first-frame cost. The
profiler-start alignment is bounded to 31.065 ms; conservatively retaining only
sample intervals wholly inside the first frame gives 447.378 ms of its
479.725 ms instrumented CPU span. This contains 119.63 ms in `inflate`, 114.01 ms
in DXT block decoding and 125.62 ms in `getError`. Stacks attribute image reads
to `RetainCanonicalWorldImage`/`CopyWorldCommand`, and decoding/upload to
`CreateWorldTextureObjects` under canonical scene submission. Frame two's
177.701 ms conservative sample window contains another 76.19 ms inflate and
26.96 ms DXT decoding, plus sound archive seeking. These are attribution only.
Texture loading/conversion is a measured next target; no texture-format,
sampling, mip-chain or image-admission change has been made.


Two small texture-startup changes are retained. The shared DXT decoder copies
its four-byte RGBA result with `std::memcpy`; its color/alpha arithmetic and
clipped-edge behavior are unchanged. Wasm A/B/B/A microbenchmarks on synthetic
1024-square DXT1/3/5 textures improve by 5.82%/6.69%/4.49%, with identical output
hashes for those textures and 768 generated clipped-edge cases. Native and
direct-Wasm IWI tests pass. Fresh production startup A/B/B/A measures first-frame
CPU 474.185/462.015/461.005/464.410 ms: about 7.79 ms less, but the 20-second
mean intervals overlap at 13.197/13.237/13.198/13.265 ms. This is a decode/startup
improvement, not a meaningful gameplay FPS claim. Paused work, GL calls and
recovery match; the pre-recovery image is exact and recovery differs by at most
one byte/channel on eight pixels. Evidence uses `cargo-dxt-copy-*` and
`dxt-bench-*` under ignored `build/performance/`.

A removed read probe identifies 127 repeated IWI archive reads among 680 reads:
20,735,867 duplicate bytes and 84.55 ms, with every duplicate byte-for-byte equal
to an already retained source. Registration and frame one account for 99 of
those duplicates/70.14 ms; this probe does not separate their costs. The retained
`FindRetainedIwiSource` lookup now reuses complete IWI bytes across existing
world/static/dynamic/UI pools after matching canonical pointer, name, support and
recovery-source kind. `InspectExternalCanonicalImage` still validates the layout
with current picmip before copying bytes. Misses take the original filesystem
path. Pools keep separate encoded vectors, admission accounting and GPU objects;
world unload clears their sources. There is no new cache, GPU alias, mip-policy,
quality, error-check or publication change.

Fresh production startup A/B/B/A against the DXT-copy build measures
**427.990/374.800/374.770/432.405 ms** first-frame CPU: a **55.41 ms/12.88%**
reduction. First-20-second mean intervals are
**12.73188/12.65405/12.62975/12.73525 ms**, a small **0.72%** reduction. Both
candidates beat both controls for these measures. Provenance, environment,
graphics, clock and every one-second coverage check pass. This remains headless
muted Chrome 153, RTX 3070 Ti/ANGLE D3D11, 1080p, 2.778 ms cadence, seed 1 and
canonical cutscene skipping; inherited AA 4, water disabled and marks disabled
remain unchanged. Do not add percentages from the separate DXT series.

The fresh paused pair matches all 120 views/work samples, all non-search-depth
counters and every GL call; both drain 120 GPU results. Actual context recovery
preserves 12 resumed work samples. Candidate/recovered images differ from the
control by at most one byte/channel on three/eight pixels. Paused intervals
13.278/13.503 ms do not establish a steady-state gain. A complete 120-second
candidate intro covers all windows at **12.20595 ms/81.93 FPS**, p99 **19.080 ms**,
worst-second mean **18.434 ms**, first-frame CPU **382.090 ms**. This single full
intro is coverage evidence, not an A/B improvement claim. Canonical `map_restart`
also passes 20 seconds: first time 300, 17 ms simulation step, 1,641 frames,
clock ratio 1.000011. Local evidence uses `cargo-encoded-reuse-*`; measured sites
are `cargo-encoded-reuse-r1-production` and `cargo-encoded-reuse-r1-diag`.
Stable 125 FPS and native timing parity remain unproved.

The next submission investigation uses disposable JS/GL probes on that same
retained production build. Raster discard changes little; omitting draw submission
under discard saves about 3.8 ms, while omitting float uniforms saves about 0.5 ms.
Suppressing repeated static instance pointers saves about 1.3 ms. These probes
deliberately change output and establish attribution only. Exact duplicate index
upload reuse gives no gain and is rejected. All keep error checks, and restored
images meet the existing one-byte tolerance. Local prefixes are
`cargo-raster-cost-*`, `cargo-submission-cost-*`, `cargo-instance-binding-cost-*`
and `cargo-index-reuse-*`.

A concrete static-instance texture prototype preserves paused images within one
byte/channel and every draw/check. Full instrumented intro A/B/B/A at 2.778 ms
cadence measures 12.63723/12.40377/12.36629/12.66823 ms (2.12% lower mean), but
GPU readbacks during live updates regress parts of the approach. Copying the
already supplied upload bytes instead removes repeated readbacks. Its fresh
4 ms series measures 12.59525/12.00813/12.08046/12.65100 ms (4.59% lower mean);
all 857,664 copied bytes match the GPU exactly. Both arms contain interception
overhead. These are prototype results, not shipping performance. Two intervening
launches are rejected before import when observed cadence changes to about 4 ms;
the later series uses fresh controls and unchanged admission checks.

The retained C++ backend change uses those existing retained descriptors directly,
with row-aligned partial texture updates and no GPU readback or JavaScript mirror.
Fresh **clean production** full-intro A/B/B/A at 4 ms cadence measures
**12.31677/11.56053/11.52766/12.29654 ms**: **12.30666 -> 11.54409 ms**, a **6.20%**
reduction, about **81.3 -> 86.6 submitted FPS**. Both candidates beat both controls
in every phase. Candidate p99 is 17.835/17.935 ms versus 19.085/19.070 ms; worst
one-second means remain 17.544/17.665 ms. First-frame CPU is 370/379 ms, so this
does not solve startup stalls or establish stable 125 FPS. All 120 canonical
seconds, first-frame inclusion, graphics, clock, driver, source and environment
checks pass. This remains headless muted Chrome 153/RTX 3070 Ti/ANGLE D3D11,
1080p, seed 1, `com_maxfps 125`, canonical `ui_autoContinue 1`, inherited AA 4,
water disabled and marks disabled. Do not compare across the cadence change.

Fresh paused diagnostic control/candidate matches all 120 views/work samples and
120 GPU results, plus 12 work samples after actual context recovery. Per frame,
938 buffer binds and 4,690 attribute pointers disappear; 943 integer uniforms,
five texture binds and seven active-texture calls are added. All other GL calls,
draws and seven error checks match. The pre-recovery image differs by one byte
at seven pixels; the recovered image is exact. Local evidence and immutable
production/diagnostic snapshots use `cargo-instance-texture-cpp-*`; the full-intro
and paused comparators retain their raw coverage and exact intended call deltas.

The following investigation retains no additional runtime change. On the new
instance-texture build, a conservative call inventory finds static-shadow groups
whose only intervening change is the base-instance uniform. A disposable
`WEBGL_multi_draw` shader uses `gl_DrawID` to select the exact original base for
each draw, preserving order, counts, offsets and seven error checks per frame.
It reduces submissions from 3,953 to 3,483 per frame, but paused A/B/B/A measures
11.75692/11.68985/11.67228/11.61546 ms: only 0.044% mean difference with overlapping
controls. All five candidate/control/restored images remain within one byte per
channel. The prototype is rejected; no extension path is added to the runtime.
Its first attempt failed because independently resolved WebGL uniform handles
did not match by identity and no draws were grouped. The corrected probe and
failure are preserved as `cargo-static-multidraw-landing-a1{,r}`.

New ten-second Worker CPU captures use `instance-texture-symbols`, a final-link
map verified byte-for-byte against the retained production Wasm. Approach/later
samples attribute 3.150/3.475 seconds to `getError`, including 2.604/3.071 beneath
frame drawing; these waits include earlier graphics-service work and do not
justify removing validation. Inlined `R_RenderScene` self time is 1.160/0.560
seconds. Separate diagnostic captures at 5/20/40/60/90 seconds each collect 120
frames and complete GPU results. Approach scene build averages 5.696 ms, including
2.475 ms dynamic submission and 2.555 ms DObj build; weighted skinning is
0.752/0.762 ms at 5/20 seconds and 0.360–0.458 ms later. These overlapping stages
are attribution, not clean FPS. Evidence is `cargo-instance-texture-cpu-a1-*`
and `cargo-instance-texture-stages-a1-*`.

A small trial specializes the four weighted skin blocks, following the native
`SkinWeightBlock` dispatch shape while preserving the existing scalar arithmetic.
Native/direct-Wasm DObj tests and both Release builds pass, including changed
poses, every weight count, malformed counts and atomic failure. Fresh clean
full-intro A/B/B/A gives **11.56090/11.67767/11.72749/11.64630 ms**: both candidates
are slower than both controls, **0.85% worse** overall, with worse p99. All 120
populated seconds, clock, graphics, source, driver and environment checks pass.
The specialization and its extra tests are removed byte-for-byte; the retained
instance-texture implementation stays. Trial snapshots, raw timings and reversible
patch are under `cargo-skin-block-*` and `skin-block-rejected.patch`. Both this
series and the multi-draw probe use headless muted Chrome 153/RTX 3070 Ti/D3D11,
1080p, observed 4 ms cadence, seed 1, canonical cutscene skip and inherited
graphics settings. Neither provides a new FPS gain or stable 125 FPS evidence.

A disposable `drawRangeElements` probe also retains no runtime change. Its first
attempt incorrectly assumed paused dynamic indices were immutable and stops on
70,560 range-cache misses. The corrected scope leaves all dynamic/UI streams
unchanged and supplies actual min/max indices for immutable buffers only. An
intermediate run contains unequal JavaScript forwarding costs and fails the
one-byte image tolerance in one candidate; it cannot establish a gain. With
direct argument forwarding in both arms, paused A/B/B/A measures
11.63369/11.64766/11.52737/11.58968 ms: overlapping controls and just 0.208% mean
difference. All 3,969 draws and seven error checks per frame remain; 2,385 draws
use ranges in the candidate. No measured readbacks or cache misses occur, all
3,778,800 mirrored index bytes match the GPU, and all five final images meet
the one-byte tolerance. Evidence uses `cargo-draw-range-*`; the earlier failures
remain distinct from the corrected comparison.

A command-local vertex-visit table for indexed shadow bounds is also removed.
A synthetic direct-Wasm comparison of 128 meshes with repeated triangle indices
reduces the scan from 1.47442 to 0.86091 ms, including the visit-table allocation,
with identical bounds hashes and malformed-input/signed-zero checks. The actual
renderer trial preserves index/finite checks, first-reference ordering and
separate stamps for overlapping batches. Both Release builds and the existing
upload/recovery fixture pass; its first browser launch fails before the fixture
because WebGL2 context creation fails, and an unchanged fresh launch passes.
Fresh clean full-intro A/B/B/A is **11.53271/11.54531/11.59285/11.60672 ms**:
**11.56972 versus 11.56908 ms**, only **0.0055%** difference, with overlapping
controls and no reliable approach gain. All 120 populated seconds, source,
driver, graphics, clock and environment checks pass. The source and extra fixture
checks are restored byte-for-byte; prepared paused/recovery and broad candidate
suites are not run after rejection. Local evidence uses `cargo-shadow-vertex-visits-*`,
`shadow-vertex-visit-bench.*` and `shadow-vertex-visits-rejected.patch`. Conditions
remain headless muted Chrome 153/RTX 3070 Ti/D3D11, 1080p, observed 4 ms cadence,
seed 1, canonical cutscene skip and unchanged inherited graphics settings.

A synthetic compressed-texture check identifies a separate fidelity boundary.
Eighteen generated DXT1/3/5 images (1/2-pixel authored tail mips and 4/8/64/256
pixel base levels) compare the current Wasm decoder with direct S3TC uploads
using exact texel fetches on headless muted Chrome 153/RTX 3070 Ti/D3D11.
Every RGBA8 control is byte-exact and GL checks pass, but GPU decompression
differs by up to six green-channel values and seven alpha values. It fails
the existing one-byte equivalence assertion; no compressed runtime path is
added and no threshold is loosened. This does not establish which output matches
native rendering. Native parity and a deliberate format/fidelity decision are
required before treating compressed uploads as an optimization. The first probe
incorrectly uploaded tiny levels as base images, then omitted mip sampling state;
those failures are preserved separately from the corrected fixture. Generated
blocks, decoder/driver hashes and results use `dxt-gpu-*` under ignored build;
no proprietary data or FPS measurement is involved.
