# Seeded brush shader-hash optimization

2026-08-31. Baseline runtime: `9cb348d4` (production control `0849a172`
adds only test linkage). Candidate: `06ad8004`.

## Workload correction

Four diagnostic-only counters partition accepted dynamic and UI command
vertices/indices. Two fresh loads of the same `f3450eac` Wasm retained identical
UI counts (240 vertices, 360 indices), but dynamic vertices changed from
68,455 to 68,615, indices from 165,354 to 164,976, and drawn batches from
1,319 to 1,323. This locates the earlier mismatch in the dynamic scene,
not the UI. Individual actor/model changes were not separately traced.

Fresh maps previously always seeded canonical game randomness from
`Sys_MillisecondsRaw`. The shared `SV_GetMapRandomSeed` now accepts the optional
cheat-protected `sv_mapSeed` integer. Its default `-1` preserves the clock;
nonnegative values select a repeatable seed. Both native and browser server
spawn paths call it. Canonical save/demo restoration still owns restored
random state; no browser game-state representation or replay format was added.

The controlled runner sends `set sv_mapSeed 1; devmap cargoship; fixedtime 16`
through the existing engine command path. The remaining schedule is unchanged:
free movement after view 30, pause after 60, camera positioning after 120/180,
300 profiler-disabled intervals from views 240–540 and diagnostic views 601–720.
Two fresh seeded baseline loads and the candidate matched all 120 camera/time
and measured work-count samples. Each frame had 68,818 dynamic vertices,
165,834 dynamic indices, 1,323 dynamic batches, 4,816,164 total submitted
indices and 5,636,952 uploaded bytes. World draws were 5,471, static instance
draws 1,872 and shadow-caster draws 16,496. UI counts stayed 240/360.

This is repeatability for this paused rendering window, not complete active
gameplay determinism or a comparison of every vertex byte. The synthetic
brush/world oracle separately compares output geometry and material state.

## Optimization

The brush builder retains only the preceding vertex/pixel shader pointers and
their hashes in a local stack record. Consecutive surfaces using the same shader
reuse its bytecode hash, independently across vertex and pixel stages and across
materials. Every new build starts empty. Technique selection, material state,
batch merging, validation and atomic command publication remain unchanged.
There is no heap allocation, global cache, GPU-buffer policy or persistent asset
pointer. Static-model culling and independent sun/spot rendering are untouched.

This re-evaluates the hash-only idea previously reverted in
[the uncontrolled brush investigation](brush-costs-f15c3dc9.md), now with
matching measured workloads. The geometry rewrite and unused-name experiment
remain reverted.

| Diagnostic CPU mean, ms | Baseline A | Baseline repeat | Hash reuse |
| --- | ---: | ---: | ---: |
| Brush material/batch setup | 2.027 | 2.024 | 1.451 |
| Brush construction/append | 4.074 | 4.042 | 3.661 |
| Whole-frame CPU | 29.501 | 28.902 | 29.811 |

Material setup fell about 28%; brush total fell about 10%. The diagnostic
whole-frame timer did not improve. Production timing is recorded separately;
nested diagnostic timers must not be summed with their parents.

## Production comparison and validation

The hash reuse is retained. Four production runs used the same headless Chrome
152.0.7977.64, Ryzen 7 7800X3D, 1440 x 1000 viewport, owned CargoShip files and
fresh browser profiles. All four passed the camera/time/workload comparison,
300 consecutive callback intervals, foreground checks and zero page errors.
Production compiles out diagnostic profiling; the paired diagnostic runs above
provide the measured geometry-work qualification.

| Production run, execution order | Mean interval ms | p95 ms |
| --- | ---: | ---: |
| Baseline A1 | 27.476 | 30.490 |
| Hash reuse B1 | 25.838 | 28.495 |
| Hash reuse B2 | 26.217 | 28.500 |
| Baseline A2 | 26.357 | 28.900 |

The two-run means were 26.917 -> 26.027 ms, an observed 3.30% reduction.
Both candidate runs were lower than both controls, but the baseline also
improved across the run order and the closest gap was only 0.140 ms. This is
limited evidence from one host and paused scene, not a statistically established
general FPS gain. The measured local saving and these production windows support
retaining the small, allocation-free optimization. All runs and uncertainty are
preserved in the numeric record (archived in Git).

The runner now reuses one command string for execution and metadata. A1
executed the seed prefix before this metadata correction; its original
methodology string omitted the prefix, while requestedMapSeed and source code
record the actual request. The raw result is preserved with that caveat.

The focused renderer fixture passed (1/1, 0.03 s), including comparison with
the uncached world path, shader changes between builds, null shaders, material
and technique changes, atomic malformed-input rejection and authored shadow
membership. The workload Node fixture passed (1/1, 64.821 ms). The targeted
map-command fixture passed (1/1, 0.05 s), checking clock fallback, explicit seed
1, valid seed 0 and restoration of the default. Its initial link failed because
the earlier shared time-adjustment change exposed two runtime globals absent
from this fixture; adding the fixture's normal runtime values fixed the retry.

Diagnostic Release builds passed in 15.837, 12.435 and 12.572 seconds. The
production control built in 17.179 seconds; final candidate Release passed in
12.650 seconds, including the existing 14-stage runtime-prefix check. Existing
toolchain warnings remain. Final production Wasm SHA-256:
`c293b42a74182ce9c609ab19f2122cad42d4fe8b6a1b7a30a88c7da3dfd3baa2`.

No broad suite, mission/progression validation, mandatory capture or retail
compatibility promotion is part of this milestone. Only aggregate counts,
timings and hashes are versioned; owned assets, retail logs and installation
paths stay outside Git.

All task browser contexts and servers closed. The delivered production site is
`build/web/site`; the retained baseline is
`build/renderer-seeded-control-0849a172`. The diagnostics site now contains the
retained candidate, not the earlier rejected name-copy experiment.

Next: inspect DObj lighting setup under this same seeded workload, keeping
canonical lighting/pose ownership and avoiding a pause-specific result cache.

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/seeded-brush-hashes-06ad8004.json`.
