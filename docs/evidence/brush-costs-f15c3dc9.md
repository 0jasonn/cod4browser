# Brush cost investigation

2026-08-31. Starting checkout: `efc59db7`. Retained instrumentation/runtime
baseline: `f15c3dc9`. Evaluated candidates: `006bc35a` and `2b86c7c7`.
**Neither optimization is retained.** The delivered production runtime matches
the baseline control; the measured local saving did not justify shipping the
slower candidate frame timings.

## Delivered work

Four fields in the existing diagnostic profiler separate brush remapping,
geometry emission, material/batch construction and append. They nest inside
`sceneBrushBuildMs`, itself part of model assembly. Validation, publication and
timer overhead remain in the parent. Do not add nested costs to their parents.

The focused world/brush fixture now checks brush output against the uncached
world path. The profiling runner accepts an optional production site directory
so retained baseline/candidate artifacts can be measured without rebuilding.
No rendering algorithm, geometry emission, hash policy, batch merge rule,
canonical ownership, culling or independent shadow policy changed in the final
runtime. No new runtime cache or allocation policy remains.

## Diagnostic attribution and rejected changes

Each run used headless Chrome 152.0.7977.64 on a Ryzen 7 7800X3D, at 1440 x 1000,
with a fresh browser profile and the same owned local installation. CargoShip
warmed for 30 world frames, then supplied 300 profiler-inactive intervals and
120 profiled frames without gameplay input. All population, foreground,
callback-continuity and nested-timer checks passed; no page errors occurred.
The [numeric record](brush-costs-f15c3dc9.json) retains all nine windows.

| Profiled CPU mean, ms | Baseline | Technique/vertex candidate | Hash-only candidate |
| --- | ---: | ---: | ---: |
| Brush remapping | 0.095 | 0.121 | 0.117 |
| Brush geometry | 1.496 | 1.974 | 1.838 |
| Brush material/batch setup | 2.029 | 1.626 | 1.512 |
| Brush append | 0.144 | 0.176 | 0.173 |
| Brush construction/append total | 3.943 | 4.125 | 3.870 |
| Scene assembly | 7.090 | 7.914 | 7.534 |
| DObj build, unchanged here | 9.077 | 10.847 | 10.789 |
| Whole-frame CPU | 41.246 | 46.160 | 45.393 |

`006bc35a` reused consecutive complete technique results and emitted brush
vertices in place. Material setup improved but geometry and total brush time
did not; both changes were replaced. `2b86c7c7` restored original geometry and
reused only the previous vertex/pixel shader hashes, independently across
materials. It preserved hash values and added no allocation or state across
builds. Material setup fell 25.5%, but brush total barely changed and total CPU
rose. The final production checks below led to reverting this candidate too.

Profiler-inactive diagnostic intervals were 38.728 / 40.636 / 41.283 ms.
Workloads are not identical: baseline/hash-only camera world surfaces averaged
2338.28 / 2416.91, static instance draws 755.03 / 789.32, dynamic batches
1467.31 / 1487.59 and shadow draws 9859.17 / 9913.98. Submitted world surfaces
stayed 13,125. The scene moves and host load is uncontrolled; no overall gain
can be inferred from the isolated material timer.

## Production timing, profiler compiled out

Two initial baseline runs used the existing `fb596702` production artifact,
whose runtime matches the starting checkout. Two candidate runs used the
Release built at `2b86c7c7`. Because those timings disagreed with the local
material result, the candidate artifact was retained and a baseline control
was rebuilt from runtime sources verified against `f15c3dc9`. It was followed
immediately by another run of the retained candidate.

| Production run, chronological | Mean interval ms | p95 ms |
| --- | ---: | ---: |
| Initial baseline 1 | 38.916 | 43.730 |
| Initial baseline 2 | 38.792 | 43.240 |
| Hash-only candidate 1 | 40.231 | 45.375 |
| Hash-only candidate 2 | 40.039 | 45.165 |
| Rebuilt baseline control | 40.861 | 46.060 |
| Retained candidate, interleaved | 42.124 | 47.530 |

The initial run-pair mean was 38.854 -> 40.135 ms (3.3% longer); the later
control/candidate pair was 40.861 -> 42.124 ms (3.1% longer). The baseline also
slowed over time, so this does not isolate a causal regression or establish
statistical significance. It does provide insufficient evidence to ship a
performance optimization. The candidate was reverted, and the verified
baseline-control artifact remains the delivered production build.

Each production window collected 300 intervals at Worker main-loop callback
completion after a world draw and 30 subsequent callbacks. Consecutive pump
ticks were asserted. Focus/visibility remained valid, no samples were removed,
and no build or other benchmark overlapped measurement. These are not display
FPS measurements. Full artifact hashes are recorded. Some working-tree dirty
flags reflect source/document edits while an already-built artifact was
served; they are preserved, not presented as clean source builds.

## Verification and reproduction

- One focused Win32 Debug `web_renderer_world_scene_tests` fixture passed
  initially, after the narrower candidate, and after restoring the baseline:
  each 1/1, 0.04 s test / 0.06 s total. The added checks remain as regression
  coverage. Synthetic repository-authored data uses the repository license.
  Checks cover vertex values, shader hashes, material/state and lit/spot
  transitions, independent null shader changes, new shader bytes between
  builds, missing techniques and rejection without replacing published data.
  Existing placement, world and independent caster-range checks remain.
- Three diagnostic Release builds passed: 14.833 s, 13.698 s and 12.336 s.
  The candidate Release passed (16.404 s); the targeted baseline-control
  Release passed (12.332 s) and became the final artifact. Both passed the
  existing 14-stage runtime-prefix check. Existing compiler/toolchain warnings
  remain. The restored runtime and delivered Wasm hash were verified.
- No build or test failed. No broad suite, mission/progression check,
  screenshot, GPU pixel comparison, allocation-failure injection or retail
  compatibility promotion ran. All task browser contexts and servers closed.

Serve the selected generated site on port 8051. Use
`node tools/profile_web_renderer.mjs LABEL` for diagnostics, or
`node tools/profile_web_renderer.mjs LABEL production BUILT_COMMIT [SITE_DIRECTORY]`
for production. The optional directory must match the site being served.
Set `KISAK_COD4_RETAIL_ROOT` locally. Control and candidate snapshots remain in
ignored `build/brush-control-f15c3dc9` and `build/brush-candidate-2b86c7c7`.
Evidence contains no installation paths, assets or retail logs.

## Next task

Make the CargoShip timing workload repeatable before further CPU changes:
retain both artifacts, control scene position/time and use repeated interleaved
windows. Remapping and append are small; no new allocation policy is justified.
Dynamic command batch copying remains a possible later target, but additional
micro-optimizations should not proceed from a local timer alone.
