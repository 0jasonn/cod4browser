# Static sun-shadow partition milestone

Runtime `cc4af645` restores the native-shaped static-caster boundary in the
WebGL2 backend. Canonical `GfxStaticModelInst` AABBs are tested against each sun
shadow matrix, then only contiguous visible runs from the existing LOD-packed
instance buffer are submitted. Camera DPVS never enters this decision. The near
and far maps remain independent, off-camera casters inside either light-space
partition remain eligible, and authored spot-shadow membership is unchanged.

The measured family attribution at `794c3154` placed static XModels at 0.707 ms
of 0.928 ms across the three sun caster families. A shader-side experiment was
rejected because it retained all vertex submissions and increased CPU cost. The
delivered CPU visibility mask adds no geometry buffer, pause cache, JavaScript
state, or canonical object model.

## Controlled diagnostic result

The seeded paused CargoShip workload uses fixedtime 16, the same free-camera
view, 120 exact profile views, and a foreground headless Chrome window. The
profiler-inactive interval is recorded separately from the instrumented window.

| Metric | Control | Delivered | Change |
| --- | ---: | ---: | ---: |
| Sun-shadow draw CPU | 3.524 ms | 1.234 ms | -65.0% |
| Static sun-shadow CPU | 0.707 ms | 0.137 ms | -80.6% |
| Renderer backend CPU | 9.750 ms | 7.524 ms | -22.8% |
| Total profiled CPU | 15.953 ms | 13.787 ms | -13.6% |
| Profiler-inactive interval | 18.088 ms | 16.989 ms | -6.1% |
| Shadow instance submissions | 11,008 | 1,302 | -9,706 |
| Submitted indices | 4,816,164 | 2,918,796 | -1,897,368 |

The explicit static-partition comparator accepts only those two intended work
reductions. Across all 120 views, world/static camera selection, retained static
counts, dynamic/UI commands, uploads, merged world/dynamic sun ranges, camera,
time, and canonical world geometry remain exact. A second candidate run included
the context recovery check: generation 1 -> 2 with twelve matching resumed work
samples.

## Final production result

Fresh production A/B/B/A runs compare the saved `29545019` control artifact to
the one final Release from `cc4af645`. Each run uses six canonical checkpoints
covering 300 uncapped frames; the product defaults remain unchanged.

| Run, execution order | Mean frame interval, ms |
| --- | ---: |
| A1 control | 14.939 |
| B1 candidate | 12.828 |
| B2 candidate | 12.636 |
| A2 control | 14.955 |

The pair means are **14.947 -> 12.732 ms (14.82% lower)**. Both candidates beat
both controls, and the controls close within 0.016 ms. This is a local paused
renderer throughput result, not active-gameplay FPS or pixel equivalence.

## Validation and limits

- The focused `web_renderer_static_model_scene_tests` target passed (1/1,
  0.09 s). It checks canonical bound transfer, light-space intersection,
  boundary contact, existing camera packing, LODs, materials, and atomic errors.
- Diagnostic Release builds passed while iterating. One final production Release
  passed with the canonical runtime-prefix check; no runtime edits followed it.
- `renderer_workload.mjs --static-shadow-partitions` verified both final
  diagnostic runs against the attributed control. The ordinary production
  comparator verified the four A/B/B/A windows.
- No broad suite, mission check, capture, or compatibility promotion ran. The
  context test uses the explicit loss/restore hook rather than a driver fault.
- Bounds add 24 bytes to each CPU/GPU static instance record. The paused window
  does not exercise moving-camera static-buffer updates, so no upload or total
  memory improvement is claimed.

The final production Wasm SHA-256 is
`743278d9ed75db40f94499860ef05335a1decbbaa9a94b2f46a7f6c5eaf240b8`.
The final diagnostic Wasm SHA-256 is
`ab68f36eb1f67f09d989f8279ae84ef4a130c5d9032446cdb23e1c0018fcf137`.
Numeric results and raw-record hashes are in
[the companion record](static-sun-partitions-cc4af645.json). Retail data,
installation paths, and runtime logs remain outside version control.

## Recommended next task

Separate CPU-only shadow bounds from the GPU instance payload and measure the
moving-camera static upload path. Retain the same canonical bounds, independent
sun partitions, authored spot membership, and camera DPVS packing.
