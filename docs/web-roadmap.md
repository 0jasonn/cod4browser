# Web product roadmap

Updated 2026-08-31. This is the single active roadmap; earlier orders and
mission-progression gates are superseded. Mission playing, progression
validation, route generation, and campaign expansion are outside this task.
Manual gameplay assessment can guide renderer work without a progression gate.

1. Canonical camera DPVS now filters both static-model instances and world
   surfaces. World camera runs preserve merged-batch order and canonical IDs;
   sun/spot shadows retain independent ranges. The
   [world visibility record](evidence/world-camera-visibility-2026-08-31.md)
   separates synthetic execution and production compilation from browser/retail
   verification. No new visual or performance result is claimed.
2. Preserve canonical asset/game/renderer ownership, imported-asset validation,
   and durable filesystem shutdown. Small shared material lookups and Worker
   transport bookkeeping must not absorb technique selection or host-specific
   filesystem recovery policy.
3. The [focused DObj profile](evidence/dobj-stages-946dc918.md) identifies
   geometry construction as 59.02% of DObj build in a short headless CargoShip
   window. Recommended next task: remove unused per-surface pixel-shader
   hashing from the DObj path, preserve world/static-model behavior, then run
   one focused semantic check and repeat the same short profile. The hash's
   individual cost is not yet measured; do not promise a speedup or add caches.

The world-camera milestone used one focused test and one final production
Release build. The subsequent measurement used one diagnostic build and one
successful 120-frame profile after browser setup corrections. No broad tiers,
mission/lifecycle checks, screenshots or compatibility promotion were required.
Escape and renderer polish remain outside scope.

Historical plans and completed milestones remain in
[the earlier roadmap](history/web-roadmap-through-2026-08-28.md) and
[execution report](../WEB_ROADMAP_EXECUTION_REPORT.md). Use
[current status](web-status.md) for evidence scope; file discovery never
promotes compatibility.
