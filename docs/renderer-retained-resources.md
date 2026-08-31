# Retained renderer resources

The major renderer milestone targets a repeatable reduction of at least 25%
from the seeded `06ad8004` production baseline (26.027 ms pair mean), together
with substantially less geometry rebuilt/uploaded each frame. This is a local
paused-renderer target, not a promise of gameplay FPS on other machines. A
single improved substage or a diagnostic-only change does not complete it.

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
  and static-model geometry. Audit material-remap invalidation before adoption.
- Submit current rigid placements every frame and preserve the existing
  DObj/brush/FX draw order. Reuse the existing instance shader inputs; avoid
  changing static-model packing or canonical camera DPVS.
- Route brush references through the independent sun-shadow pass as well as
  the camera pass. Preserve existing authored spot-shadow membership.
- World retirement must release resources, and context loss must rebuild their
  GPU objects from retained CPU data. Failed submissions must not publish
  partially updated draw lists.

Verification needs an independent comparison against the existing transformed
brush output, changing placements and shader inputs, invalid-input atomicity,
logical draw/index/shadow equality, reduced upload work, targeted renderer
recovery, and interleaved production timing. Keep the strict existing workload
comparator: any intended physical-upload change needs explicitly justified
logical-work comparison, not silently relaxed assertions.

Continue through other measured renderer costs if brush retention alone does
not reach the major target. No mission checks, broad suites or mandatory
captures are required.

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
work samples. Production timing qualification is the remaining delivery gate.

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
