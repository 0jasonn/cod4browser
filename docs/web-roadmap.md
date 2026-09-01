# Web product roadmap

Updated 2026-09-01. This is the single active roadmap; earlier orders and
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
5. The [controlled timing work](evidence/controlled-renderer-552a468d.md) now
   qualifies a paused renderer window: two fresh loads matched all six camera,
   time and world-geometry checkpoints. A retained legacy candidate was rejected
   for ignoring fixedtime. Native time adjustment is shared with the web pump.
   Active-gameplay replay and dynamic entity/caster equality remain unproven.
6. [Aligned diagnostic profiling](evidence/paused-copy-qualification-cd85e18e.md)
   now checks all 120 sampled views and geometry work. Fresh loads of the same
   Wasm produced different uploaded-byte and submitted-index totals despite
   matching camera and draw counts. The unused-name candidate was reverted;
   production A/B/B/A was not justified by those mismatched workloads.
7. [Seeded brush optimization](evidence/seeded-brush-hashes-06ad8004.md) locates
   the variation in dynamic commands and adds shared optional `sv_mapSeed`
   control. Seed 1 matches all 120 measured workload samples across two fresh
   baseline loads and the hash-reuse candidate. Consecutive brush shader hashes
   are now reused locally within a build: material setup fell about 28%, and
   production A/B/B/A pair means were 26.917 -> 26.027 ms. Host drift and paused
   execution limit that 3.30% observation; no general gameplay FPS claim follows.
8. The [major retained-renderer milestone](evidence/retained-renderer-49af3948.md)
   removes repeated brush expansion/upload, joins adjacent opaque sun ranges,
   and reuses frame-local texture parameters/pass-local bindings. Canonical
   geometry, static camera culling and independent shadow membership are preserved.
   Fresh production means are 21.175 -> 14.731 ms (30.43% lower) with the benchmark
   cap lifted; default-cap means are 21.469 -> 16.843 ms. Upload bytes fall 29.67%.
   These are local paused-renderer results. Canonical checkpoint timing replaces
   callback telemetry that suppresses most fast frames; product defaults remain.
9. The [DObj conversion milestone](evidence/dobj-conversion-30e34cff.md) fuses final
   vertex construction with skinning, reuses numeric geometry capacity, exposes
   canonical math/packing helpers to selective LTO, and merges dynamic opaque
   sun ranges. The final diagnostic comparison observes 41.1% lower DObj build
   and 59.1% lower combined skinning/geometry time. Production A/B/B/A means are
   15.461 -> 14.296 ms (7.54% lower), with control drift documented. Another
   1,374 shadow submissions are avoided with identical logical geometry,
   culling and independent shadows. Focused checks, recovery and Release pass.
10. The [static sun-shadow partition milestone](evidence/static-sun-partitions-cc4af645.md)
    carries canonical static AABBs into each light-space cascade and submits only
    contiguous visible runs. Sun-shadow CPU falls 3.524 -> 1.234 ms, 9,706 caster
    instances are avoided, and production A/B/B/A pair means fall 14.947 ->
    12.732 ms (14.82%). Camera DPVS, independent near/far membership, authored
    spot membership, uploads, and dynamic/UI work remain intact.
11. The [static-instance upload milestone](evidence/static-instance-uploads-ac8b00ca.md)
    separates CPU-only shadow bounds from the 72-byte GPU instance record and
    uploads only the camera half for visibility-only changes. A moving-camera
    transition falls from 1,143,552 to 428,832 bytes (62.5%) while light-space
    sun selection, authored spot membership, and camera DPVS remain independent.
12. The [static spot-shadow milestone](evidence/static-spot-membership-26b3dc98.md)
    attributes the pass by caster family and replaces repeated per-surface static
    membership searches with one packed mask per authored light. Static spot CPU
    falls 45.5%; production A/B/B/A pair means fall 12.627 -> 12.090 ms (4.25%)
    with all diagnostic logical-work samples exact.
13. The [BSP sun-partition milestone](evidence/bsp-sun-partitions-a23850aa.md)
    carries canonical surface AABBs with retained spans and selects each cascade
    independently. Per frame, 578 physical shadow draws and 1,694,994 submitted
    indices are avoided; attributed sun drawing falls 8.27%. Production A/B/B/A
    pair means fall 15.189 -> 14.942 ms (1.63%) with documented run drift.
14. The [dynamic sun-partition milestone](evidence/dynamic-sun-partitions-6ece6ee9.md)
    retains world-space bounds per flattened dynamic draw and independently
    selects both cascades. Dynamic sun CPU falls 71.9%; 394 physical shadow
    draws and 265,812 indices are avoided. Bounds construction costs 0.328 ms;
    noisy production pair means fall 14.868 -> 14.421 ms (3.01%).
15. The [dynamic spot-shadow milestone](evidence/dynamic-spot-shadows-9a253c6a.md)
    submits build-shadowmap-qualified dynamic families independently per spot
    matrix. The focused comparator proves that only 10 caster draws and 65,760
    indices are added; primary-light linkage remains a named gap.
16. The [Kisak optimization audit](kisak-renderer-optimization-audit.md) owns
    completion of the broad renderer goal. Next: measure the remaining safe
    opaque-sorting opportunity, then restore canonical primary-light linkage.

The static-instance follow-up used the focused native fixture, isolated control
and candidate diagnostic builds, three final moving-camera profiles, and one
production Release. It claims exact transfer bytes, not timing or gameplay FPS.
No broad suite, mission check, context-loss run, or capture was required.

The static spot-shadow follow-up used the same focused native target, one focused
Node target, attributed control/candidate diagnostics, four production windows,
and one final Release. One targeted profile retry corrected an aggregate-field
allowlist; renderer assertions were not weakened.

The BSP sun follow-up used one focused native target, exact 120-sample targeted
comparison, four production windows, and one final Release. No broad tier,
mission check, capture, or unrelated compatibility promotion ran.

The dynamic sun follow-up used one focused native target, exact 120-sample
targeted comparison, four production windows, and one final Release. It records
the bounds-construction cost and timing drift rather than hiding either.

The dynamic spot follow-up used one focused native target, an exact 120-sample
unchanged-work comparison, and one final production Release. It restores missing
caster work and makes no broad performance claim.

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

The controlled-window follow-up used one focused Node fixture, setup trials and
an A / rejected legacy B / A qualification. The Release initially failed at the
common compile boundary; the targeted retry passed after sharing the native
time-adjustment body. No renderer expansion or broad suite was added.

Historical plans and completed milestones remain in
[the earlier roadmap](history/web-roadmap-through-2026-08-28.md) and
[execution report](../WEB_ROADMAP_EXECUTION_REPORT.md). Use
[current status](web-status.md) for evidence scope; file discovery never
promotes compatibility.
