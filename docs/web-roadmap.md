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
   The [completed CPU-efficiency milestone](evidence/renderer-cpu-milestone-e4db91df.md)
   covers projection/material/feature reuse, world/static material state and
   shadow alpha/cull reuse. Production mean frame intervals were 43.175 ->
   40.895 ms across two runs per version; variation limits the claim.
4. [Direct DObj emission](evidence/dobj-emission-fb596702.md) removes the temporary
   vertex copy: observed vertex emission fell 5.884 -> 3.351 ms and DObj geometry
   6.989 -> 4.630 ms. Canonical animation, collision, scripts and asset ownership
   remain unchanged. The [brush investigation](evidence/brush-costs-f15c3dc9.md)
   isolated material setup (2.029 ms) and geometry (1.496 ms) as the main costs.
   Both optimization candidates were reverted after production comparisons
   remained slower; the verified baseline runtime is restored. Profiling and
   focused regression coverage remain.
5. Next: make the timing workload repeatable, using retained artifacts,
   controlled scene position/time and repeated interleaved windows before
   further CPU changes. Do not infer whole-frame gains from local timers or
   add global GL caches, new batching representations or GPU-buffer policies
   from an aggregate renderer total alone.

The earlier renderer milestone used one focused native fixture, rerun after shadow
integration; three diagnostic builds and five 120-frame profiles; four
successful production timing windows; and one final production Release build.
A production benchmark setup mismatch was corrected with a targeted retry.
No broad tiers, mission/lifecycle checks, screenshots or compatibility promotion
were required. Escape and renderer polish remain outside scope.

The DObj follow-up used one extended native fixture (with a targeted test-setup
correction), two diagnostic comparisons and one final production Release.
The production before/after means were effectively unchanged (39.547 ->
39.317 ms), with worse p95 afterward; no overall FPS improvement is claimed.

The brush follow-up used the focused world/brush fixture, a targeted
implementation revision, three diagnostic builds/profiles, a candidate Release
and a targeted baseline-control Release, which became the final delivered build.

Historical plans and completed milestones remain in
[the earlier roadmap](history/web-roadmap-through-2026-08-28.md) and
[execution report](../WEB_ROADMAP_EXECUTION_REPORT.md). Use
[current status](web-status.md) for evidence scope; file discovery never
promotes compatibility.
