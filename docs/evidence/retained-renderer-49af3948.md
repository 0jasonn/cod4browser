# Retained renderer milestone

2026-08-31. Baseline runtime: `3a0fa34e` (same runtime as `06ad8004`).
Candidate runtime: `49af3948`. Final benchmark harness: `9e185783`.

The major milestone is delivered: the fresh, interleaved production comparison
with the benchmark cap lifted reduced mean canonical-view interval by
**30.43%**, from **21.175 to 14.731 ms**.
The default-cap comparison improved **21.55%**, from
21.469 to 16.843 ms, near the product's 60 FPS setting.
These are paused-renderer observations on one host, not active-gameplay FPS.

## Implementation and ownership

- Brush geometry/material resources are validated and retained once per canonical
  `GfxBrushModel` within the world lifetime. Current rigid placements remain
  per-frame commands and use the existing GPU instance path. DObj/brush/FX
  ordering, depth-hack separation and independent sun-shadow participation remain.
- Retained brushes still occupy the original logical scene budget. DynEntities,
  optional FX, marks, code meshes, clouds and sun quads account for that occupancy
  while rebasing indices against only the physically streamed vertices.
- The sun-depth pass joins adjacent opaque world-index ranges without changing
  triangles or their order. Cutouts, non-casters and gaps break runs. Canonical
  static-model camera culling and authored spot-shadow membership are unchanged.
- Frame-local texture-parameter reuse and pass-local binding reuse remove repeated
  GL calls while preserving object aliases and their last-write order. No
  persistent GL-state cache, sampler-object system or new dependency was added.

See [resource lifetime and boundaries](../renderer-retained-resources.md).
World retirement releases retained brush resources; context restoration rebuilds
GPU objects from renderer-owned CPU descriptions. Primary-light animation stays
per-frame; stable indexed types remain validated. Technique remaps currently run
at world load; any future live-remap path must invalidate retained material state.

## Work qualification

All 120 final diagnostic frames match the baseline camera/time and logical work.
Physical changes are checked explicitly, without weakening the original strict
comparator: each frame removes exactly 20,942 streamed vertices and 41,190 indices.
At 72 bytes per vertex and four per index, this is 1,672,584 fewer uploaded bytes.

| Per-frame counter | Baseline | Candidate |
| --- | ---: | ---: |
| Buffer upload bytes | 5,636,952 | 3,964,368 |
| Streamed dynamic vertices | 68,818 | 47,876 |
| Streamed dynamic indices | 165,834 | 124,644 |
| Submitted indices, all passes | 4,816,164 | 4,816,164 |
| Camera world surfaces | 5,471 | 5,471 |
| Static instance draws | 1,872 | 1,872 |
| Dynamic batches | 1,323 | 1,323 |
| Shadow caster draws, including instances | 16,496 | 12,382 |
| Opaque sun ranges merged into preceding draws | 0 | 4,114 |

Upload bytes fall **29.67%**. The shadow counter plus merged ranges equals the
original logical count; 4,114 actual world-shadow draw calls are avoided.
UI counts remain 240 vertices/360 indices. The native fixture independently
checks rigid transforms, non-finite/overflow rejection, malformed brush atomicity,
shadow triangle order/cutouts and aliased texture state, including collisions/reset.
The explicit recovery hooks advanced resource generation 1 to 2 and produced
12 resumed samples matching every measured work counter.

Warmed diagnostic brush construction/append fell from 3.661 to
0.006 ms; geometry is no longer rebuilt there.
Diagnostic stage timings are supporting evidence, not the production speed claim.
Neither aggregate counters nor this fixture establish whole-scene pixel equality.
GPU rigid-transform rounding may differ from the former CPU expansion.

## Production timing

Both sequences use fresh headless Chrome 152.0.7977.64 profiles on the same
Ryzen 7 7800X3D host, a 1440 x 1000 viewport and locally supplied owned files.
The existing seed/fixedtime/pause/camera schedule is unchanged. Six canonical view
checkpoints, 240 through 540, delimit five 60-frame spans: 300 rendered views per
run. The mean is total elapsed time divided by 300. No diagnostic profiler is
compiled into production, and no extra high-frequency production telemetry was
introduced. Span-mean percentiles in the numeric record are **not per-frame p95**.

| Run, order within each separate sequence | Default cap, ms | Benchmark cap lifted, ms |
| --- | ---: | ---: |
| A1 | 21.573 | 21.161 |
| B1 | 16.834 | 14.788 |
| B2 | 16.852 | 14.674 |
| A2 | 21.364 | 21.189 |

The lifted-cap sequence queues canonical `com_maxfps 0` after view 180, once the
scene is paused; the web 125 Hz safety ceiling remains. Product defaults and the
delivered artifact are unchanged. Both candidate windows are below both controls
in each sequence. All windows pass camera/time/environment/foreground guards,
with zero page errors. These short single-host windows are not broad performance
or gameplay compatibility certification.

An earlier callback-based control completed at 21.145 ms, but two candidate runs
could not collect that window: `FramePumpTrampoline` suppresses most callback
telemetry below 16 ms. The corrected checkpoint method measures fast frames
without changing the engine. The old control is excluded from the paired result
because its timing method differs, not because of its value. Mixed methods,
changed cap settings and changed submitted geometry are explicitly rejected.
All final windows and hashes are in the numeric record (archived in Git).

## Checks and delivery

One focused native target, `web_renderer_world_scene_tests`, was extended and
rerun after relevant changes; final result was 1/1 passing. Eleven diagnostic
builds and twelve diagnostic runs covered implementation and targeted retries:
five runs completed, six early trials exposed the same logical-budget integration
error, and one recovery-harness wait needed correction. No runtime validation was
relaxed. Final committed diagnostics passed the 120-frame logical comparison and
12-frame recovery check.

One final production Release build passed (18.342 seconds); its unchanged Wasm
was used for all four candidate timing windows above. The first two callback
sampling attempts were rejected before the corrected sequences. No broad suites,
mission checks or mandatory captures were run. Owned assets and retail logs stay
outside version control.

Production Wasm SHA-256:
`48645b870e833247dbceba9437db5091d572d5e5a0effe1d01ebc95285e15592`.

Next: reduce DObj skinning and vertex-assembly cost using canonical pose data and
this same workload qualification. Avoid caching paused gameplay/pose results.

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/retained-renderer-49af3948.json`.
