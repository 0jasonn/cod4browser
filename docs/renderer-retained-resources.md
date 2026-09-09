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

Particle clouds now expand directly into admitted scene spans, using the same
builder and scalar math as standalone construction. Required-family admission
still includes retained brush counts. Each optional 4,096-vertex/6,144-index cloud
preserves native lattice/corner order, material state and RNG lifetime; allocation
failure or invalid output restores the complete prior vector prefix and counts.
Standalone and direct paths share geometric `resize` growth, avoiding cumulative
exact reserves; no canonical FX state or camera-dependent geometry is cached.

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
World/static passes now use the same ordered whole-set rule, including mipmap and
attenuation state. Identical instance attribute offsets reuse setup only within
the same VAO/pass; binding changes and direct overrides invalidate that knowledge.

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

The harness records every completed submission in a bounded Worker buffer,
retrieved in batches. Clean timing has the diagnostic profiler disabled.
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

Historical experiments, shader hashes and failures remain in
[Git history](../README.md#historical-records). Current suite results and optional
fixture failures belong in the [test inventory](web-test-inventory.md).
