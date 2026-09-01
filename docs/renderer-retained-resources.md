# Retained renderer resources

The major milestone is delivered in runtime `49af3948`: fresh production
A/B/B/A controls show 21.175 -> 14.731 ms with only the benchmark cap lifted,
a 30.43% reduction, alongside 29.67% fewer buffer-upload bytes. The default-cap
comparison is 21.469 -> 16.843 ms. The initial 25% throughput target is met
against current controls; the historical 26.027 ms mean is not used to claim
a gain across changing host conditions. This remains a local paused-renderer
result, not gameplay FPS. See [the evidence](evidence/retained-renderer-49af3948.md).

The previous brush path expanded `GfxBrushModel` vertices through every placement,
rebuilt material batches, appended them to the DObj/FX command, then copied and
uploaded that geometry again. Native brush submission instead keeps geometry and
placement separate. The WebGL shaders already support rigid instance placement
for position, normal, tangent and shadow depth.

The implementation moves that separation through the existing draw-command boundary:

- Keep canonical `GfxBrushModel`, entity/DynEntity placement and admission in
  Kisak. Do not cache pose, visibility or gameplay state in JavaScript.
- Lazily validate/retain a brush mesh at first use, keyed by its canonical model
  within the current world. Reuse the existing brush builder at identity
  placement; retain only backend geometry and material resources, as for world
  and static-model geometry. Material-remap lifetime is documented below.
- Submit current rigid placements every frame and preserve the existing
  DObj/brush/FX draw order. Reuse the existing instance shader inputs; avoid
  changing static-model packing or canonical camera DPVS.
- Route brush references through the independent sun-shadow pass as well as
  the camera pass. Preserve existing authored spot-shadow membership.
- World retirement must release resources, and context loss must rebuild their
  GPU objects from retained CPU data. Failed submissions must not publish
  partially updated draw lists.

Verification uses an independent comparison against the existing transformed
brush output, changing placements and shader inputs, invalid-input atomicity,
logical draw/index/shadow equality, reduced upload work, targeted renderer
recovery, and interleaved production timing. Keep the strict existing workload
comparator: any intended physical-upload change needs explicitly justified
logical-work comparison, not silently relaxed assertions.

Brush retention alone was followed by shadow-range and texture-state work to
reach the major target. No mission checks, broad suites or mandatory captures
were required.

The sun-depth backend joins only contiguous opaque world-index ranges. Cutouts,
non-casters and non-contiguous ranges break a run. It does not consult camera
visibility, change static instance selection, or alter authored spot membership.
`sunShadowMergedRanges` records saved submissions separately from actual shadow
caster draws; their sum preserves the previous logical range count.

Texture parameters use a bounded frame-local table keyed by GL object name.
Collisions conservatively repeat writes. Dynamic passes also omit known unchanged
bindings, but still reconcile aliased object parameters in the original unit
order. Uploads/context recovery cannot leave stale entries because each draw
frame resets the table. No sampler object ownership or persistent GL cache was
introduced.

Technique remapping currently runs only at `R_LoadWorld`, before brush retention.
The same world lifetime already owns world/static material resources. A future
runtime material-remap path must invalidate all affected retained renderer
resources. Primary-light type is stable and checked by `SetSceneView`; animated
color, direction, radius and attenuation continue to arrive each frame.

The focused world/brush native fixture covers rigid placement and overflow,
opaque-shadow triangle order/cutouts, texture aliases, collisions and reset.
The controlled browser workload caught and fixed logical geometry admission:
retained brushes still occupy the original scene budget before optional FX and
clouds. A diagnostic context-loss/restoration run then matched twelve resumed
work samples. Production timing qualification passed both interleaved sequences.

Production timing uses six existing canonical view checkpoints (240, 300, 360,
420, 480, 540): five contiguous 60-frame spans cover 300 frames. The old
callback-event method is unsuitable below 16 ms because `FramePumpTrampoline`
suppresses most fast callback telemetry. Baseline and candidate must both use
the checkpoint method. Report the total elapsed time divided by 300; percentiles
of the five span means are not per-frame latency percentiles. No production
profiler or high-frequency telemetry was added.

The optional `uncapped` production benchmark argument queues `com_maxfps 0`
through the canonical console after view 180, once the paused scene is selected.
It leaves the product's default 60 FPS setting unchanged and retains the web
125 Hz safety ceiling. Record this override in the workload; compare only runs
with the same setting. This is measurement of a paused renderer, not gameplay.

## Dynamic geometry ownership

Runtime `af601efe` transfers the completed per-frame dynamic vertex and index
vectors into backend staging. It does not change retained brush geometry or add
another persistent engine representation. The backend validates the same
descriptor and command metadata, uploads the same bytes, and publishes the new
draw command only after all work succeeds. Failure restores the caller's vector
ownership. The first scene retains its existing one-time geometry diagnostic.

The exact paused workload removes the attributed command geometry copy, while a
follow-up inactive GPU-buffer set failed to reduce upload time and was reverted.
See [the ownership evidence](evidence/dynamic-geometry-ownership-af601efe.md).
This marks the end of platform geometry-handoff optimization; the next measured
work belongs to canonical DObj pose, lighting and skinning.

## DObj conversion and dynamic sun ranges

Delivered in `30e34cff`: the final diagnostic comparison observes 41.1% lower
DObj build time and 59.1% lower combined skinning/geometry time. Fresh production
A/B/B/A pair means are 15.461 -> 14.296 ms (7.54% lower), with 1,374 additional
sun draw submissions avoided. Control drift and other limits are recorded in
[the DObj evidence](evidence/dobj-conversion-30e34cff.md).

DObj skinning now writes position, normal, tangent and decoded attributes directly
into the final vertex span. Indices are constructed as one span per surface.
The three intermediate float arrays and separate vertex-copy pass are removed.
After synchronous backend submission, the frontend recycles only numeric vertex
and index capacity; the builder also returns failed, unpublished geometry to that
workspace. World unload releases it. This trades retained CPU capacity for fewer
allocations and growth copies; it does not retain canonical asset pointers,
poses, lighting, visibility, materials or completed draw commands. All of those
inputs are consumed anew each frame. Backend context recovery still uses its
own published CPU geometry, never this empty frontend workspace.
Existing backend memory counters do not include this frontend capacity; no
total-memory saving is claimed.

Selective Emscripten LTO covers the DObj builder, lighting adapter and existing
Kisak `com_pack.cpp`/`com_math.cpp` helpers. This exposes helper bodies to the
optimizer instead of copying their implementations into the web renderer.
Exception catching, allocation-failure handling and finite-input checks remain
enabled. Native target behavior and browser capability requirements are unchanged.
The diagnostic skinning interval now includes final vertex construction and
attributes; the old separate vertex-emission interval is zero. Compare the sum
of skinning and geometry, or total DObj build, across this boundary.

The existing sun-range helper also accepts dynamic draw references. Adjacent
opaque ranges merge only within the same brush instance or shared skinned buffer.
Non-casters, depth-hack/FX exclusions, cutouts, gaps and instance changes break
the run. Camera visibility never enters the decision. Static-model culling,
static shadow instance selection and authored spot membership are unchanged.
`sunShadowMergedRanges` includes these additional avoided submissions.

Use `renderer_workload.mjs --shadow-ranges` for this physical-draw change. It
requires exact logical caster totals and exact equality of all other work,
including uploaded bytes and submitted indices, across all 120 samples. The
ordinary `--profiles` comparator remains strict.

## Static sun-shadow partitions

Delivered in `cc4af645`: the backend now mirrors native's distinct sun-shadow
visibility step for static XModels. The portable instance record carries the
canonical `GfxStaticModelInst` AABB. Once LOD packing is current, each near/far
pass computes a light-matrix visibility byte for every retained shadow instance
and submits contiguous visible runs from the existing instance buffer.

This mask belongs to the WebGL draw boundary. It stores no pose, asset, gameplay,
or camera state; it is overwritten independently for each partition. Camera DPVS
packing remains in the buffer's second half and never enters caster selection.
Authored spot-shadow membership keeps its existing canonical-index lookup.
Unload releases the mask, and context recovery reuses canonical retained bounds.

The explicit `--static-shadow-partitions` comparator normalizes only the intended
caster-instance and submitted-index reductions before requiring every other work
field and workload checkpoint to match. The final diagnostic candidate removes
9,706 caster instances and 1,897,368 indices per frame; sun CPU falls 65.0%.
Production A/B/B/A pair means fall 14.947 -> 12.732 ms (14.82%). See
[the full evidence](evidence/static-sun-partitions-cc4af645.md).

The [static-instance upload follow-up](evidence/static-instance-uploads-ac8b00ca.md)
keeps those 24-byte bounds in aligned CPU source and shadow-packed vectors. The
GPU instance record is again 72 bytes, and a visibility-only change uploads only
the camera half. LOD changes still upload both halves because both packings move.
No geometry buffer, camera-derived caster selection, or non-atomic publication
was added. Eleven controlled camera transitions save 7,861,920 transfer bytes;
no timing or gameplay-FPS improvement is inferred.
