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
   The subsequent [dynamic texture change](evidence/dynamic-textures-74fe11aa.md)
   skips consecutive identical binding sets, preserving texture aliases and
   reducing observed texture setup from 2.041 to 1.538 ms. Total CPU did not
   improve. The [falloff-uniform guard](evidence/falloff-uniforms-12ac17e5.md)
   then removed three unused uploads from other material techniques, with
   observed dynamic material setup at 1.747 -> 1.248 ms in a fresh comparison.
   Recommended next task: measure repeated view/projection setup within the
   dynamic camera passes before moving uploads out of individual draws.
   Preserve sun-query/sprite overrides, depth-hack order, canonical culling
   and independent shadows. No global GL cache or extra staging is justified.

The latest optimization used one focused native fixture, two diagnostic builds
and 120-frame profiles (baseline and implementation), and one
final production Release build, with no retries.
No broad tiers, mission/lifecycle checks, screenshots or compatibility promotion
were required. Escape and renderer polish remain outside scope.

Historical plans and completed milestones remain in
[the earlier roadmap](history/web-roadmap-through-2026-08-28.md) and
[execution report](../WEB_ROADMAP_EXECUTION_REPORT.md). Use
[current status](web-status.md) for evidence scope; file discovery never
promotes compatibility.
