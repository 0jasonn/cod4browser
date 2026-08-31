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
3. The [DObj hash deletion](evidence/dobj-hash-d8661476.md) reduced observed
   geometry-build mean time from 7.192 to 6.347 ms in matching short headless
   profiles. The subsequent [scene profile](evidence/scene-stages-2ce03241.md)
   places 73.94% of non-DObj scene time in command assembly (13.801 ms), followed
   by dynamic submission (4.638 ms). Recommended next task: separate assembly's
   physics/mark work, brush/model construction and appends, then optimize the
   dominant operation. Preserve canonical culling/shadows; no pose or geometry
   cache is justified.

The latest measurement used one focused test file, one diagnostic build, one
120-frame profile and one final production Release build, with no retries.
No broad tiers, mission/lifecycle checks, screenshots or compatibility promotion
were required. Escape and renderer polish remain outside scope.

Historical plans and completed milestones remain in
[the earlier roadmap](history/web-roadmap-through-2026-08-28.md) and
[execution report](../WEB_ROADMAP_EXECUTION_REPORT.md). Use
[current status](web-status.md) for evidence scope; file discovery never
promotes compatibility.
