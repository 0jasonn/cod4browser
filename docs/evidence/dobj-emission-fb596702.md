# Direct DObj vertex emission

2026-08-31. Baseline checkout: `311ac40f`; diagnostic attribution:
`0ac738e5`; implementation: `fb596702`.

## Change and ownership

The DObj builder now value-initializes each vertex in its private replacement
vector and fills it directly, instead of filling a temporary 72-byte vertex
and copying it into the vector. This is a platform draw-command construction
detail. Canonical pose, skinning, hide bits, LOD selection, materials and
animation remain unchanged. No cache, retained allocation, new representation
or dependency was added.

The entire replacement is still published only on success. All finite-value,
index and output-limit checks remain, and allocation failures still take the
existing exception path. A malformed vertex can now occupy an unpublished
replacement slot before rejection; the caller's destination remains intact.
Vertex defaults, order, index rebasing, camera/depth-hack behavior and shadow
membership are unchanged. No world/static culling or renderer draw code changed.

## Measured attribution

Three diagnostic fields separate vertex emission, index emission and brush
construction/append. The first two nest inside DObj geometry; the third nests
inside model assembly. Timer overhead and remaining batch/material work stay
in their parents; these intervals must not be added to their parents again.

One matching diagnostic run per version collected 300 profiler-inactive
intervals followed by 120 profiled CargoShip frames. Both ran in headless Chrome
152.0.7977.64 on a Ryzen 7 7800X3D, at 1440 x 1000, using fresh browser profiles,
the same owned local installation, 30 warmup world frames and no gameplay input.
All sample-count, foreground and nested-interval checks passed, with no page
errors. The numeric record is [here](dobj-emission-fb596702.json).

| CPU interval | Before mean, ms | After mean, ms |
| --- | ---: | ---: |
| DObj vertex emission | 5.884 | 3.351 |
| DObj index emission | 0.816 | 0.980 |
| DObj geometry total | 6.989 | 4.630 |
| DObj build total | 11.883 | 9.591 |
| Skinning | 2.770 | 2.813 |
| Brush construction and append | 3.480 | 3.436 |
| Scene assembly | 6.562 | 6.486 |
| Scene construction total | 24.382 | 22.045 |
| Profiled whole-frame CPU | 43.303 | 41.266 |

Vertex emission fell 43.1%; geometry total fell 33.8%. Vertex p95 fell
6.480 -> 3.770 ms. Index emission rose, so the vertex saving must not be
reported as the whole geometry saving. Material/batch residual stayed near
0.3 ms and was left unchanged. The profiler-inactive diagnostic frame interval
was 40.541 -> 39.053 ms; compiled-in diagnostic wrappers still affect this
variant.

Workload populations are close but not identical: dynamic batches averaged
1468.65 -> 1473.63, shadow draws 9860.96 -> 9876.60, camera world surfaces
2261.99 -> 2417.57 and static instance draws 734.84 -> 782.57. World surfaces
submitted stayed 13,125. This moving scene reaches different points in equal
frame-count windows. The comparison supports the local copy removal, not
attribution of every frame-time difference to identical work.

## Production timing, profiler compiled out

The existing `e4db91df` production artifact supplies the before run: its runtime
matches milestone-start `311ac40f` (a documentation-only commit). The final
Release was built at `fb596702`. Both Wasm SHA-256 hashes are in the numeric
record, and production CMake has diagnostics disabled.

Using the same browser/viewport/import setup, each production run waited for
the first world draw and 30 subsequent main-loop callbacks, then recorded 300
intervals at Worker callback completion. Consecutive pump ticks were asserted;
no callbacks were skipped. No builds or other benchmarks overlapped the runs.

| Production interval statistic | Before, ms | After, ms |
| --- | ---: | ---: |
| Mean | 39.547 | 39.317 |
| Median | 39.695 | 38.450 |
| p95 | 43.865 | 46.595 |
| Maximum | 49.690 | 136.420 |

The mean changed by only 0.6%, while p95 worsened and the after window contains
a long frame. No samples were discarded. With one run per version and an
uncontrolled host, **no reliable whole-frame or FPS gain is established**.
The local CPU saving is the reason to retain this small change. These Worker
intervals are not display FPS and are separate from diagnostic timing.

## Verification and limits

- Extended the existing Win32 Debug `web_renderer_dobj_submission_tests` fixture
  to link the actual DObj builder and lighting implementation. Repository-authored
  synthetic geometry follows the repository license; runtime pose/matrix and
  packing dependencies are stubbed. Checks cover rigid and weighted output,
  vertex attributes/defaults, index rebasing, batch order, hidden surfaces,
  and unchanged destination data after invalid position, binormal, UV or index.
- The first test run failed because the new fixture omitted valid LOD
  parameters; the test setup was corrected using the existing LOD helper.
  The targeted rebuild/rerun passed 1/1 (0.03 s test, 0.05 s total). No engine
  assertion was relaxed. This does not validate full animation behavior or
  inject allocation failure.
- Two diagnostic Release builds passed (14.254 s and 13.101 s). The latter
  preceded the test-only LOD fixture correction; its runtime sources are
  identical to `fb596702` and its Wasm hash is recorded separately.
- One final production `tools/build_web.ps1 -Configuration Release` passed
  (15.034 s), including the existing 14-stage runtime-prefix check. Existing
  compiler/toolchain warnings remain. No runtime edits followed this build.
- No broad suite, mission/progression check, screenshot or compatibility
  promotion was performed. Source inspection preserves independent sun/spot
  shadows and world/static camera culling; this is not a pixel comparison or
  an end-to-end playability claim.
- All four browser windows passed with no page errors; their temporary
  profiles and the task's local servers were closed. `git diff --check` and
  source inspection passed.

Reproduce with the existing `tools/profile_web_renderer.mjs` runner and a
locally configured `KISAK_COD4_RETAIL_ROOT`; serve only the selected generated
site on port 8051. Diagnostic invocations use a run label. Production invocations
also take `production BUILT_COMMIT`. Local logs and numeric originals remain
under ignored `build/`; no retail assets, paths or logs enter this evidence.

## Next task

Brush construction plus append is now isolated at 3.436 ms, about 81% of the
4.221 ms model-assembly interval. Split its geometry conversion, shader/material
metadata and append costs before changing it. Current brush batches use shader
hashes in their merge equality, so the earlier DObj hash deletion cannot simply
be copied here. World-sized vertex remapping and rebuilt geometry are possible
costs, not proven reasons for a persistent cache. Keep canonical placement,
material identity, batch order and independent shadows intact.
