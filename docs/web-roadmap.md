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
3. The [cloud-append optimization](evidence/cloud-append-ae37e80c.md) removes
   repeated exact vector reservations, with observed cloud-append mean time
   falling from 9.676 to 0.865 ms and assembly from 14.964 to 5.864 ms.
   Recommended next task: investigate dynamic submission's increase from
   5.562 to 8.723 ms, separating command validation/copy/allocation from GPU
   resource creation/upload and checking the spare-capacity memory tradeoff.
   Preserve canonical culling/shadows; no pose or geometry cache is justified.

The latest optimization used one focused native fixture, two diagnostic builds
with matched 120-frame profiles and one final production Release build, with no
retries.
No broad tiers, mission/lifecycle checks, screenshots or compatibility promotion
were required. Escape and renderer polish remain outside scope.

Historical plans and completed milestones remain in
[the earlier roadmap](history/web-roadmap-through-2026-08-28.md) and
[execution report](../WEB_ROADMAP_EXECUTION_REPORT.md). Use
[current status](web-status.md) for evidence scope; file discovery never
promotes compatibility.
