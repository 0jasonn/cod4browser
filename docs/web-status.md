# Web product status

Updated 2026-08-31. This is the single current status page; the
[roadmap](web-roadmap.md) owns priorities and the
[convergence inventory](web-port-convergence.md) owns system classification.

## Renderer CPU-efficiency milestone complete

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
implemented or explicitly ruled out there. Next: canonical DObj geometry
emission and scene assembly, which dominate remaining CPU work.

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

Assisted mission authoring, autonomous combat fallback, helper-only diagnostic
exports, and unused assist/prefix-skip options are removed. Manual F8/F9
recording and explicit replay remain diagnostic tools. Neither mission
progression nor save/death/restart validation is a prerequisite for future work.

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
classifications; other discovered direct SP zones remain `UNTESTED`.
[Earlier status narratives](history/web-status-through-2026-08-28.md) preserve
measurements and their original context. No fresh visual claim is made here.

## Product boundaries

WebGL2 and the Worker/DOM/storage/audio adapters are platform-owned. Kisak owns
assets, game state, filesystem semantics, and the renderer frontend. Production
and diagnostics remain separate artifacts. Imported retail files remain local;
proprietary binaries and data are never distributed. Cinematics remain explicit
omissions; no WebGPU, pthreads, multiplayer, dependencies, or presentation work
was added.
