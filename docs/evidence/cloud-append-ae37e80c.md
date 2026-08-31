# Particle-cloud append optimization

Delivered from `13c3865f` on 2026-08-31. `f7d437b8` adds focused assembly
timings; `ae37e80c` removes three exact per-cloud vector reservations from
`WebRenderer_AppendParticleCloudCommand`. Both profile source trees were clean.

## Finding and change

The baseline isolates particle-cloud appending at 9.676 ms, 64.66% of the
14.964 ms scene-assembly interval. Reserving exactly the next cloud's required
capacity can reallocate and copy the entire accumulated scene for every cloud.
The helper now uses ordinary `std::vector` insertion/growth. No custom growth
policy, persistent cache, dependency or engine representation was added.

Both callers use the same corrected helper: the frontend's dynamic scene and
the multi-cloud builder. Source shape/index/batch checks, output limits,
rebasing, draw order, material identity and allocation-failure rollback remain
unchanged. The backend receives the same logical command. Canonical physics,
marks, model building, static/world camera culling and independent sun/spot
shadow paths were not changed.

Capacity can exceed logical size under standard vector growth. This trades
some temporary spare capacity for fewer copies; no peak-memory reduction is
claimed. The vectors remain local to the command and are released normally.
Allocation failure still restores prior sizes, contents and surface count;
capacity may change, as it could with the old reserve sequence.

## Matched short profiles

[Sanitized before/after aggregates](cloud-append-ae37e80c.json) use Chrome
152.0.7977.64 headless on the same Ryzen 7 7800X3D host, 1440 x 1000, a fresh
Playwright-owned persistent profile per run, and the existing portable import
of the user-owned installation. Each run issued `map cargoship`, waited for
30 drawn world frames, then collected 120 completed gameplay frames with the
same diagnostic timers. No gameplay input or builds ran during the windows.
Captures completed in 7.223 s before and 5.662 s after, with no page errors or
DOM visibility/focus transitions.

| CPU interval | Before mean ms | After mean ms | Change |
| --- | ---: | ---: | ---: |
| Particle-cloud append | 9.676 | 0.865 | -91.07% |
| Command append total | 10.951 | 2.079 | -81.02% |
| Scene assembly total | 14.964 | 5.864 | -60.82% |
| Physics/marks/preparation | 0.204 | 0.205 | +0.61% |
| Brush/DynEntity/FX model work | 3.809 | 3.580 | -6.02% |
| DObj build | 11.573 | 11.458 | -0.99% |
| Dynamic submission | 5.562 | 8.723 | +56.82% |
| Scene construction total | 32.370 | 26.267 | -18.85% |

Cloud append p95 fell from 10.565 to 0.945 ms, and assembly p95 from 16.420 to
6.390 ms. This supports retaining the small append change. Dynamic submission
increased, with p95 rising from 6.940 to 15.110 ms; its cause is unresolved.
Do not hide that cost or attribute every change in the full frame to this fix.

The authored opening scene kept moving, so matching the method does not yield
identical frame traces. Mean particle batches increased 22.617 -> 23.258;
dynamic batches were 1390.367 -> 1390.233, indices 5,431,767 -> 5,409,143 and
buffer-upload bytes 17,967,081 -> 18,016,024. Submitted world surfaces remained
13,125; camera-visible world/static counts varied. These counters are workload
context, not proof of visual equivalence.

This is diagnostic CPU evidence, not a profiling-disabled FPS benchmark.
Headless execution, active timers/GPU queries, elapsed-time differences, scene
motion and host variation limit attribution. No complete GPU analysis, memory
high-water measurement, retail-visual or playability promotion was performed.

## Diagnostic boundaries

`sceneEffectsPrepareMs`, `sceneModelBuildMs` and `sceneCommandAppendMs` are
disjoint partitions of `sceneAssemblyMs`. They cover, respectively:

- Floating-origin adjustment, canonical FX/DynEntity physics, static/entity
  marks and active brush collection.
- Brush construction/append, DynEntity model collection, lighting,
  construction/append, and FX-model LOD selection/construction.
- FX-model, mark and code-mesh appends, cloud construction/append, and sun
  sprite/flare assembly.

`sceneCloudAppendMs` nests within the last interval and times only calls to
`WebRenderer_AppendParticleCloudCommand`. Never add it again to assembly total.
The existing frame event and aggregate helper carry these fields; historical
samples leave absent fields null. Production compiles the timers out, and
inactive diagnostic captures make no clock calls.

## Checks actually run

- Built and ran only Win32 Debug `web_renderer_particle_cloud_scene_tests`:
  `ctest --test-dir build/portable-tests-msvc18-win32 -C Debug -R
  '^web_renderer_particle_cloud_scene_tests$' --output-on-failure --timeout 20`.
  1/1 passed (0.06 s test, 0.08 s total). The fixture compiles the actual helper.
  Existing deterministic geometry/material, retention, two-cloud ordering,
  malformed-index and capacity checks remain. New checks compare 24 repeated
  appends with original vertices and rebased indices, preserve the preceding
  DObj batch, and inject each successive allocation failure until append
  succeeds, verifying rollback of prior command contents/counts.
- Two `tools/build_web.ps1 -Configuration Release -Diagnostics` runs passed:
  15.116 s for the baseline and 11.260 s after the optimization. Each was
  followed by its one successful 120-frame profile. The runner verified all
  new field sample counts, nonnegative intervals, the assembly partition and
  cloud-parent containment with 0.001 ms tolerance.
- One final `tools/build_web.ps1 -Configuration Release` passed (16.287 s).
  All three builds passed their existing canonical runtime-prefix check;
  existing toolchain/compiler warnings remain. No build/test/profile retry
  was needed. The new timing field is present only in diagnostic output.
- `git diff --check` and source/caller/bridge inspection passed. The existing
  Node aggregation fixture was extended but not separately run; live profiles
  exercised the actual event/aggregation path. No assertion was weakened.

No broad suites, mission checks, screenshots, replay, context recovery or
save/death/restart validation ran. Both task browsers and the local server were
closed; Playwright owned the new temporary profiles and their cleanup. Earlier
retained profiles were untouched. No retail content or installation paths are
in the committed evidence.

Next task: investigate the higher and more variable dynamic-submission cost,
separating retained-command validation/copy/allocation from GPU resource
creation and upload. Check the capacity/memory tradeoff as part of that work
before choosing another optimization; do not assume the entire increase is
caused by this change or add a cache based on the total alone.
