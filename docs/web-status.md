# Web product status

Updated 2026-09-01. This is the single current status page; the
[roadmap](web-roadmap.md) owns priorities and the
[convergence inventory](web-port-convergence.md) owns system classification.

## Current campaign boundary

Canonical SP menu startup now runs before a map is active. A legal-retail
Release-diagnostics run loaded 69 shipped menus, opened `main`, Options,
Profiles, and Load Game through Kisak's command/UI owners, then loaded
Killhouse and verified the shipped pause and in-game menus. Escape paused and
resumed through the existing key queue. The reached Wasm defect was an invalid
SP `int()`-to-`void()` command callback cast; SP now matches MP's typed
`openmenu` callback. See [the focused evidence](evidence/canonical-menu-lifecycle-2026-09-01.md).

The playable offline slice already exists across six legally validated maps.
Those runs demonstrate canonical movement, aiming, firing, weapons, audio,
transitions, and Airplane save/load continuity. Building another player
controller or simulated movement path is out of scope. The obsolete mission
route author/replay system and its waypoint/progression simulation have been
deleted; `village_assault` progression automation remains retired.

The dynamic DObj convergence path is complete through `838e047c`. A headless
Release-diagnostics observe-only run based on that commit now establishes
`scoutsniper` as **RENDERS**: all canonical lifecycle stages completed, the
first real world frame arrived, and 3,601 stationary frames ran over 60.013
seconds with no page, WebGL, or lifecycle error. No input was injected and no
headed/manual visual inspection occurred, so this is not a `FUNCTIONAL`,
`PLAYABLE`, or visual-correctness claim. See
[the stationary evidence](evidence/scoutsniper-stationary-838e047c.md).

The active convergence task is now objective notification, dvar/config,
profiles, and save/load menu persistence. The stationary `ac130` probe is
deferred behind that user-facing UI/persistence work.

## Prior dynamic geometry handoff

Runtime `af601efe` transfers final per-frame dynamic vertex/index vectors into
backend staging instead of copying them. Descriptor, finite/index, batch,
lighting, upload and atomic-publication checks remain in place, and failure
restores frontend ownership. World/static geometry, canonical culling, draw
order, and independent sun/spot shadows are unchanged.

The seeded paused comparison matches all 120 work samples. Command geometry copy
falls **0.1217 -> 0.0005 ms**, dynamic copy **1.0026 -> 0.8916 ms**, and dynamic
submission **1.7592 -> 1.6389 ms**. Whole-frame timings are flat, so no gameplay
FPS gain is claimed. A double-buffered WebGL upload follow-up produced no upload
gain and was reverted.

Authored active-scene runs once exposed DObj construction as a large frontend
component, but unchanged control loads did not reproduce their camera/effect
work. Later milestones closed the DObj path through `838e047c`. See
[the historical evidence and limits](evidence/dynamic-geometry-ownership-af601efe.md).

## Renderer optimization audit completed

Runtime `4ed38a84` restores canonical primary-light linkage for DObj,
moving-brush, DynEnt model, and DynEnt-brush spot casters. The canonical
visibility bit arrays and authored light-region hulls now filter each selected
spot light before the existing independent matrix test. Camera DPVS, both sun
cascades, static membership, and dynamic draw ordering remain separate.

The focused primary-light test passed, all 120 diagnostic work-count samples
match, and the one final production Release passed. This CargoShip view retained
the same 340 shadow draws and 1,023,750 submitted indices, so the result closes
semantic convergence without a new performance claim. The optimization audit
now has no applicable Open or Partial rows. See
[evidence and limits](evidence/dynamic-primary-light-linkage-4ed38a84.md).

## Prior dynamic opaque draw-order milestone

Runtime `c8c4f335` applies Kisak's canonical material key only within contiguous
dynamic model/brush runs proven opaque and depth-writing. Every blended, FX,
depth-sensitive, sun, and other unsafe batch remains a stable append-order
anchor; depth-hack and shadow passes stay independent.

All 120 work-count samples match. Across 1,323 draws, material updates fall
556→418 and feature updates 567→348; dynamic camera CPU falls 3.820→3.641 ms
while command copy/order rises 0.949→0.988 ms. Noisy production pair means are
14.613→14.116 ms (3.41% lower). See
[evidence and limits](evidence/dynamic-opaque-sort-c8c4f335.md).

## Prior dynamic spot-shadow caster milestone

Runtime `9a253c6a` restores dynamic DObj, DynEnt XModel, moving-brush, and
DynEnt-brush casters for every selected spot map. Each light applies its own
matrix AABB test, and build-shadowmap cull/alpha state now crosses the material
boundary. Camera and sun visibility remain independent.

All 120 unchanged-work samples match. The missing rendering adds 10 physical
caster draws and 65,760 indices per frame; dynamic spot attribution is 0.0915
ms. The final production Release passed. At that milestone, primary-light
linkage and dynamic opaque ordering remained the two convergence gaps.
See [evidence and limits](evidence/dynamic-spot-shadows-9a253c6a.md).

## Prior dynamic sun-cascade visibility milestone

Runtime `6ece6ee9` retains one world-space AABB per flattened DObj, XModel,
DynEnt, and moving-brush draw, then tests it independently against both sun
cascades. Camera visibility, material eligibility, placement identity, and
alpha boundaries remain independent.

Across all 120 diagnostic samples, physical shadow draws fall **724 -> 330**,
merged dynamic ranges **1,374 -> 0**, and submitted indices **1,223,802 ->
957,990**, with every other recorded work count exact. Dynamic sun CPU falls
**0.148 -> 0.041 ms (71.9%)** and total sun drawing **1.181 -> 0.535 ms
(54.7%)**. Bounds construction adds 0.328 ms to dynamic copy. Production pair
means are **14.868 -> 14.421 ms (3.01% lower)** with substantial run drift. See
[evidence and limits](evidence/dynamic-sun-partitions-6ece6ee9.md).

The [Kisak optimization audit](kisak-renderer-optimization-audit.md) is closed.

## Prior BSP sun-cascade visibility milestone

Runtime `a23850aa` carries canonical `GfxSurface` bounds with retained world
spans and selects near/far sun casters independently in light space. Camera DPVS
is absent from shadow selection; range holes, alpha boundaries, static masks,
authored spot membership, and dynamic commands are preserved.

Across all 120 diagnostic samples, physical shadow draws fall **1,302 -> 724**,
merged ranges **5,488 -> 1,374**, and submitted indices **2,918,796 ->
1,223,802** while every other recorded work count remains exact. Sun-shadow
draw time falls **1.208 -> 1.108 ms (8.27%)**. Production A/B/B/A pair means are
**15.189 -> 14.942 ms (1.63% lower)** with material run drift, so that timing is
a local observation. The focused test and one final Release passed. See
[evidence and limits](evidence/bsp-sun-partitions-a23850aa.md).

## Prior static spot-shadow membership milestone

Runtime `26b3dc98` converts authored `GfxWorld::shadowGeom` static membership
into one packed mask per selected spot light. Model surface batches reuse that
mask instead of repeating canonical-index searches. Camera DPVS remains absent
from caster selection, and the existing CPU shadow mask is reused.

Diagnostic static spot-shadow CPU falls **0.982 -> 0.535 ms (45.5%)** and total
spot drawing **1.484 -> 1.074 ms (27.7%)**, with all 120 logical-work samples
matching. Production A/B/B/A pair means are **12.627 -> 12.090 ms (4.25%
lower)**. The focused native and Node checks and one final Release passed. See
[evidence and limits](evidence/static-spot-membership-26b3dc98.md).

## Prior static-instance upload milestone

Runtime `ac8b00ca` moves 24-byte canonical shadow bounds out of the instanced GPU
record and uploads only the camera-packed half when DPVS visibility changes
without a LOD repack. The shader record returns to 72 bytes; CPU bounds remain
aligned with shadow LOD packing and preserve independent near/far and authored
spot membership.

In an exact 120-view moving-camera diagnostic, each visibility transition falls
from **1,143,552 to 428,832 bytes (62.5% lower)** for 5,956 source instances.
Eleven transitions save 7,861,920 bytes. One focused test and the single final
production Release passed. This is deterministic transfer evidence; noisy
single-run timing does not establish a CPU or FPS improvement. See
[evidence and limits](evidence/static-instance-uploads-ac8b00ca.md).

Its spot-shadow follow-up is delivered in the current section above.

## Prior static sun-shadow partition milestone

Runtime `cc4af645` carries canonical static-model AABBs through the portable
boundary, tests them against each sun cascade, and submits only contiguous
visible instance runs. This matches native's separate shadow visibility shape:
camera DPVS does not select casters, near/far membership remains independent,
and authored spot membership is unchanged.

Diagnostic sun-shadow CPU fell **3.524 -> 1.234 ms (65.0%)** and static-shadow
submission **0.707 -> 0.137 ms (80.6%)**. The controlled workload avoids 9,706
static caster-instance submissions and 1,897,368 indices per frame while all
120 samples preserve camera/static retention, dynamic/UI commands, uploads,
merged ranges, camera, time, and world geometry.

Fresh production A/B/B/A pair means are **14.947 -> 12.732 ms (14.82% lower)**.
The focused static-model target, recovery check, explicit work comparator, and
one final Release passed. This is paused CargoShip throughput, not gameplay FPS
or pixel equivalence. See [evidence and limits](evidence/static-sun-partitions-cc4af645.md).

Its instance-upload follow-up is delivered in the current section above.

## Prior DObj conversion and dynamic shadow milestone

Runtime `30e34cff` fuses skinning with final vertex construction, recycles numeric
geometry capacity, enables selective LTO of existing Kisak helpers, and extends
opaque sun-range merging to dynamic geometry. The final diagnostic comparison
observes **41.1% lower DObj build time** and **59.1% lower combined skinning/geometry
time**, with current canonical poses and lighting still evaluated each frame.

Fresh production A/B/B/A means are **15.461 -> 14.296 ms (7.54% lower)** with only
the benchmark cap lifted. Both candidates beat both controls, but control drift
limits precision. These are paused CargoShip results, not gameplay FPS. Diagnostic
inactive windows also drift and do not independently establish a frame-time gain.

Another 1,374 sun draw calls are avoided per frame. All 120 samples preserve
logical caster totals, indices, uploads and world/static camera work. Independent
shadows, static culling, atomic failures and recovery remain intact. One focused
native target and one final production Release passed; no broad suites, mission
checks or captures were required. See [evidence and limits](evidence/dobj-conversion-30e34cff.md).

That milestone's static-caster follow-up is delivered in the current section above.

## Prior retained-renderer milestone

Runtime `49af3948` retains canonical brush geometry, joins adjacent opaque
sun-shadow ranges, and removes repeated texture state/binding calls. Fresh
production A/B/B/A means are **21.175 -> 14.731 ms (30.43% lower)** with the cap
lifted only in temporary benchmark sessions. With the product default cap,
means are **21.469 -> 16.843 ms**, near its 60 FPS setting. These are paused
CargoShip renderer measurements, not active-gameplay FPS.

Each sampled frame uploads 29.67% fewer buffer bytes and avoids 4,114 sun-shadow
draw calls. All 120 diagnostic samples preserve logical geometry; static-model
culling and independent sun/spot membership remain intact. Retained brushes still
consume the original logical geometry budget before optional effects.

The focused native fixture, explicit resource-recovery check and final Release
passed. Production timing now uses existing canonical view checkpoints because
routine callback telemetry omits most sub-16 ms callbacks. Eight final production
windows pass the workload guards. See [the evidence](evidence/retained-renderer-49af3948.md)
and [resource ownership](renderer-retained-resources.md).
The DObj follow-up is delivered above.

## Prior seeded brush shader-hash optimization

Fresh-map randomness caused the changing dynamic geometry; UI counts matched.
The shared server now offers optional `sv_mapSeed` control, defaulting to the
original clock seed. With seed 1, two baseline loads and the candidate matched
all 120 diagnostic camera/time and measured geometry/shadow-work samples.

The brush builder now reuses consecutive shader hashes within each synchronous
build. Material setup fell about 28%; production A/B/B/A pair means were
26.917 -> 26.027 ms (3.30% lower). Host drift and the paused workload limit this
observation; no general gameplay FPS gain is claimed. Static-model culling,
independent shadows, validation and atomic publication remain unchanged.

Focused checks and the final Release passed; the production candidate was
browser-run twice. See [the evidence](evidence/seeded-brush-hashes-06ad8004.md).
This provided the controlled baseline for the retained-renderer milestone above.

## Prior paused profiling exposes geometry variation

Diagnostic profiling now samples exact views 601–720 of the paused scene and
checks every view plus actual geometry work. Three runs matched camera/time and
draw counts but differed in submitted indices and uploaded bytes, including two
runs of the same Wasm. The seven-line unused-name optimization was reverted;
no production performance gain is claimed.

The comparison guard, focused fixture and final restored Release passed. See
[the evidence](evidence/paused-copy-qualification-cd85e18e.md). This prompted
the dynamic/UI partition and shared seed control above.

## Prior camera/time qualification

The browser pump now shares native `Com_ModifyMsec`, restoring canonical
fixedtime/time-scale handling. The existing profiling runner can select an
exact paused scene time and fixed free-camera view, then reject mismatched
camera/time/geometry traces before timing comparison.

Two corrected-build runs matched all six checkpoints: mean callback intervals
were 26.655 and 26.134 ms. An interleaved legacy candidate was correctly rejected
for ignoring fixedtime. These are **paused renderer measurements**, not active
gameplay or FPS gains; entity/particle/caster equality is not yet established.
The focused fixture and Release retry passed. See [the evidence](evidence/controlled-renderer-552a468d.md).
This prompted the diagnostic geometry checks above; the earlier camera/time
qualification alone is insufficient for dynamic-copy comparisons.

## Prior brush cost investigation

Brush costs are now separated: material/batch setup measured 2.029 ms,
geometry 1.496 ms, remapping 0.095 ms and append 0.144 ms in the baseline.
Two optimization candidates were evaluated and **both reverted**. Hash reuse
reduced the material timer, but production windows remained slower, including
an interleaved control. The verified baseline runtime/build is restored.

Profiling, focused regression coverage and retained-artifact comparison support
remain. Tests and Release/control builds passed. See [the evidence](evidence/brush-costs-f15c3dc9.md)
for all runs and uncertainty. This prompted the controlled timing work above.
No rendering or playability improvement is claimed.

## Prior direct DObj vertex emission

`fb596702` fills vertices directly in the private replacement command. A focused
CargoShip comparison measured vertex emission at **5.884 -> 3.351 ms** and DObj
geometry at **6.989 -> 4.630 ms**. All validation and atomic publication remain;
canonical pose/LOD, culling, draw order and independent shadows are unchanged.
Production intervals were 39.547 -> 39.317 ms with worse p95; this short
comparison does not establish an overall FPS improvement.

The extended DObj fixture and final production Release passed, with one test
setup correction and targeted rerun. See [the evidence](evidence/dobj-emission-fb596702.md)
for timing populations and limits. This prompted the brush work above.

## Prior renderer CPU-efficiency milestone

`e4db91df` finishes local projection/material/feature-state reuse and extends
material reuse to world/static draws plus alpha/cull reuse to independent shadow
partitions. Draw order, canonical culling, caster membership and per-draw values
are preserved; state is local to each pass and resets after direct overrides.

Two production runs per version measured mean frame intervals of
**43.175 -> 40.895 ms (5.28% lower)** with the profiler compiled out. Run ranges
overlap, so this is a modest observed gain, not a fixed FPS or playability claim.
The focused fixture, diagnostic comparisons and final production Release passed.
See [the completed milestone](evidence/renderer-cpu-milestone-e4db91df.md) for
all windows, the targeted benchmark correction and verification limits.

Projection, material, uniform and draw-submission opportunities are now either
implemented or explicitly ruled out there. This prompted the DObj geometry
emission and scene assembly work above.

## Prior conditional falloff uniforms

`12ac17e5` uploads distance-falloff constants only for their technique, removing
three unused uniform uploads from other material setups. No tracker, shader
arithmetic, culling or shadow-policy change was added. Matching short CargoShip
profiles observed dynamic material setup at 1.747 -> 1.248 ms and dynamic-model
drawing at 6.993 -> 6.417 ms; the total CPU reduction is not solely attributable
to this change.

The focused existing native fixture, two diagnostic builds/profiles and final
production Release passed. See [the evidence](evidence/falloff-uniforms-12ac17e5.md)
for the source-verified shader contract and execution limits. This prompted the
completed view/projection and state work above.

## Prior dynamic draw texture setup

`74fe11aa` skips repeated complete texture binding sets within each dynamic
draw block, preserving sampler alias order. Short matching CargoShip profiles
observed texture setup at 2.041 -> 1.538 ms and dynamic-model drawing at
5.299 -> 4.710 ms. Total CPU did not improve; this is a local reduction only.
Draw order, canonical culling and independent shadow passes remain unchanged.

One focused native fixture, two diagnostic builds/profiles and the final
production Release passed without retries. See
[the texture-state evidence](evidence/dynamic-textures-74fe11aa.md) for scope
and limitations. This prompted the material/uniform inspection above; material
state averaged 1.133 ms in that earlier window.

## Prior dynamic geometry staging

`9403a899` reuses vertex/index staging while keeping the published command
intact until upload succeeds. Matched short CargoShip profiles observed geometry
copy at 1.621 -> 0.815 ms (p95 3.480 -> 0.955 ms) and dynamic submission at
6.442 -> 5.668 ms. Median copy time rose; total CPU time stayed essentially
unchanged. Staging retained 16.962 MiB in the final diagnostic snapshot.
Validation, culling and shadow behavior remain unchanged; unload releases staging.

The focused native test, three diagnostic builds/profiles and final production
Release passed. See [the staging evidence](evidence/dynamic-staging-9403a899.md)
for limitations and the memory tradeoff. This prompted the dynamic draw work
above (6.116 ms measured CPU time in that earlier window).

## Prior particle-cloud append optimization

`ae37e80c` removes three exact per-cloud vector reservations after a focused
profile identified repeated appends as 64.66% of assembly time. Matched short
CargoShip profiles observed cloud append at 9.676 -> 0.865 ms and total assembly
at 14.964 -> 5.864 ms. Bounds checks, rollback, canonical behavior, camera
culling and independent shadows are preserved. The focused native fixture,
both diagnostic builds/profiles and final production Release passed.

Dynamic submission increased from 5.562 to 8.723 ms in that comparison;
standard vector growth can also retain spare capacity within a command.
See [the comparison](evidence/cloud-append-ae37e80c.md) for the tradeoff,
allocation-failure checks and measurement limits. No general FPS or visual
improvement is claimed.

## Prior scene-construction measurement

`2ce03241` adds six diagnostic scene intervals without changing rendering.
A 120-frame headless CargoShip profile measured command assembly at 13.801 ms
(73.94% of scene time outside DObj building), dynamic submission at 4.638 ms,
camera visibility at 0.141 ms and dynamic image resolution at 0.019 ms.
This prompted isolation of assembly's physics/mark, brush/model and append
costs. One focused test file, one diagnostic
build/profile and one final production Release build passed without retries.
See [the scene profile](evidence/scene-stages-2ce03241.md) for boundaries and
limitations. This is diagnostic CPU evidence, with no FPS or visual promotion.

## Prior DObj hashing optimization

`d8661476` removes unused per-surface DObj shader hashing while preserving
world/static-model hashing, canonical culling and shadow behavior. A matching
120-frame headless CargoShip profile observed geometry at 6.347 ms versus
7.192 ms (-11.75%), and DObj build at 11.320 ms versus 12.186 ms (-7.11%).
This short comparison supports the deletion, not a general FPS or visual claim.
One focused Debug test, one diagnostic build and one final production Release
build passed with no retries. See [the comparison](evidence/dobj-hash-d8661476.md).

## Prior focused DObj measurement (`946dc918`)

A 120-frame headless Chrome CargoShip profile at `946dc918` measured geometry
construction at 7.192 ms (59.02% of DObj build), skinning at 2.810 ms, lighting
at 1.341 ms and pose at 0.300 ms. DObj build averaged 12.186 ms within 32.288 ms
of scene construction. No optimization or pose/geometry cache was added.
See [the stage profile](evidence/dobj-stages-946dc918.md) for setup corrections,
methodology, limitations and the recommended small DObj hash deletion.
This is diagnostic CPU evidence, not a clean FPS or retail-visual assessment.

## Canonical world-camera visibility

The shared DPVS call now computes world surfaces as well as static models,
including native AABB, portal, cull-group and decal policy. Per-surface index
spans survive merged material batches, and the WebGL camera pass draws only
contiguous visible runs. Sun batches and authored spotlight caster ranges stay
independent; existing static-model culling/LOD packing is preserved.

Production Release and the focused Win32 Debug fixture each passed once, with
no retries. The fixture exercises the real producer,
world command construction and camera-run selection alongside existing static
packing checks, including empty views, portal/far-plane rejection, sky gaps,
batch boundaries, decals and unchanged shadow geometry. See
[the world visibility evidence](evidence/world-camera-visibility-2026-08-31.md)
for the final Release result and validation limits. No browser boot, retail
visual, gameplay or performance promotion is claimed.

## Prior canonical static-camera visibility (`317fc12f`)

Canonical DPVS setup/reset, portal traversal and static-cell AABB work now run
synchronously from `R_RenderScene` on the engine Worker. Camera packing consumes
completed slot-0 visibility by canonical instance index, including valid empty
results, independently of shadow instances and LOD-change detection. No producer
dependency blocker remains; world-surface filtering is still deferred.

Production Release compiled. One Win32 Debug fixture executed the producer and
camera packing with portal, empty-mask, identity and shadow-separation checks.
Browser execution and retail visuals were not observed in this task; gameplay
assessment remains with the user. No performance improvement is measured.
See [the visibility evidence](evidence/static-camera-visibility-2026-08-31.md)
for exact checks, build setup corrections and skipped work.

## Prior renderer handoff (`a44119df`, from `bf5ec1e2`)

Unchanged static-model LOD groups now avoid repacking and batch-range updates;
empty shadow batches skip setup. Camera draw ranges are separate, but canonical
DPVS camera filtering is **not integrated**: its native view/reset/global and
worker dependencies require a further seam. Camera and shadow draws still use
the conservative LOD-packed data. Five diagnostic DObj timings distinguish
total build, pose, lighting, skinning, and geometry costs without adding caches.

See [the renderer record](evidence/renderer-efficiency-2026-08-31.md) for exact
checks and remaining blockers. No new retail capture, visual assessment, FPS
measurement, or compatibility promotion was performed. The earlier cleanup
results below remain historical relative to this continuation.

## Cleanup baseline at `bf5ec1e2`

The DB publication hook now reports publication only. Canonical
`R_RenderScene` owns world drawing; the obsolete single-surface proof and its
private mirror types/tests are retired. The live world command retains bounds,
range, index, and atomic-publication checks and now also checks finite lightmap
coordinates before publication. Live surface APIs remain.

Assisted mission authoring, route author/replay, autonomous combat fallback,
helper-only diagnostic exports, and unused assist/prefix-skip options are
removed. No replacement controller, waypoint format, replay abstraction, or
player-state simulator is planned.

World, static-model, and DObj paths share only identical material table lookups.
Worker hosts share request IDs, error envelopes, rejection cleanup, and reply
settlement; operation/event allowlists, timeout policies, recovery, and
filesystem leases remain separate. The static-model camera pass skips empty
LOD batches before material state and texture binds. No performance gain is
claimed without a new measurement.

Checks and remaining risks for this task are recorded in the
[cleanup evidence](evidence/cleanup-renderer-2026-08-31.md). No retail runtime,
new browser boot, screenshot, visual assessment, or compatibility promotion was
performed during this cleanup. The supplied installation was not copied or
modified. Visual gameplay assessment remains with the user.

## Historical automated evidence

These are earlier execution results, not checks of this working tree:

- The corrected Chrome 152 six-map profiles at `e31d62ac` and `93451ec5`
  classify Killhouse/Airplane as `PLAYABLE` and CargoShip/Blackout/Hunted/Bog A
  as `FUNCTIONAL`. CargoShip scene construction remained expensive after the
  scratch-capacity change; see [the comparison](evidence/retail-profile-93451ec5.md).
- `39de3d6d` recorded seven loads through the six-map set, canonical lifecycle
  plus 30 world frames at each stop, context recovery, no duplicate decoding,
  and bounded source-cache retirement. See
  [the regression record](evidence/retail-six-map-regression-39de3d6d.json).
- `da1e592c` recorded Airplane save/reload continuity and continued play. It
  did not prove objective/trigger progression. The later Village Assault
  action window also did not prove progression or an engine defect. These
  historical observations impose no development gate.

The [campaign matrix](campaign-compatibility.md) retains the exact historical
classifications and the new stationary `scoutsniper` `RENDERS` result; other
discovered direct SP zones remain `UNTESTED`.
[Earlier status narratives](history/web-status-through-2026-08-28.md) preserve
measurements and their original context. No fresh visual claim is made here.

## Product boundaries

WebGL2 and the Worker/DOM/storage/audio adapters are platform-owned. Kisak owns
assets, game state, filesystem semantics, and the renderer frontend. Production
and diagnostics remain separate artifacts. Imported retail files remain local;
proprietary binaries and data are never distributed. Cinematics remain explicit
omissions; no WebGPU, pthreads, multiplayer, dependencies, or presentation work
was added.
