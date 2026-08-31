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
3. After cloud-append optimization, [dynamic staging](evidence/dynamic-staging-9403a899.md)
   reduced observed geometry-copy p95 from 3.480 to 0.955 ms at the cost of
   16.962 MiB of retained staging capacity. Whole-frame CPU was unchanged.
   Recommended next task: inspect redundant material/state work in the
   dynamic-model draw pass (6.116 ms measured CPU), preserving draw order,
   canonical culling and independent shadows. No pose/geometry cache or further
   staging storage is justified by the current evidence.

The latest optimization used one focused native fixture, three diagnostic builds
and 120-frame profiles (attribution, narrower baseline, implementation), and one
final production Release build, with no retries.
No broad tiers, mission/lifecycle checks, screenshots or compatibility promotion
were required. Escape and renderer polish remain outside scope.

Historical plans and completed milestones remain in
[the earlier roadmap](history/web-roadmap-through-2026-08-28.md) and
[execution report](../WEB_ROADMAP_EXECUTION_REPORT.md). Use
[current status](web-status.md) for evidence scope; file discovery never
promotes compatibility.
