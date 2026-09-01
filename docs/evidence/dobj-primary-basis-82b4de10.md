# Canonical weighted DObj basis milestone

Runtime `82b4de10` makes the WebGL DObj weighted skinner match Kisak's native
scalar and SSE paths: position is blended across all bone influences, while
normal and tangent are transformed by the primary bone only. The previous web
path blended all three vectors, doing extra matrix work and producing a basis
that differed from canonical Kisak.

The change removes secondary normal and tangent transforms and their weighted
accumulators. Existing basis normalization, finite checks, packed-attribute
decoding, position blending, pose generation, LOD/hide policy, material and
lighting inputs, command atomicity, static-model camera culling, and independent
sun/spot shadow selection are unchanged. It adds no cache, retained state,
dependency, or browser capability.

## Qualified result

A targeted alternating retry compared the retained `af601efe` diagnostic
artifact with the candidate on the seeded paused CargoShip workload. Chrome
152.0.7977.65 ran both at 1440x1000 on the same Ryzen 7 7800X3D host. All 120
camera and work-count samples matched, including work-count SHA-256
`fed8ea624d731601e62fc6639021ecec6eb3dd28ed9944461cf4704dcba9917c`.

| CPU interval | Control mean ms | Candidate mean ms | Observed change |
| --- | ---: | ---: | ---: |
| DObj skinning | 1.3823 | 1.2328 | -10.8% |
| DObj build | 3.7197 | 3.5764 | -3.9% |
| Scene build | 6.0925 | 5.9351 | -2.6% |
| Renderer frontend | 6.8315 | 6.6595 | -2.5% |

The profiling-disabled diagnostic interval was 16.4524 -> 16.5061 ms. The
profiled total was 13.3059 -> 12.9792 ms. Host and browser variation remains
visible, so the milestone claims the attributed skinning reduction and canonical
behavior convergence, not a general gameplay FPS improvement.

## Validation and limits

- Focused Win32 Debug `web_renderer_dobj_submission_tests` passed 1/1 in
  0.03 seconds. Its authored four-bone fixture distinguishes weighted positions
  from primary-bone normal/tangent handling, then also covers changing poses,
  rigid lists, malformed inputs, atomic publication, storage reuse, material
  flags, and dynamic shadow ranges.
- The diagnostic Release and runtime-prefix check passed. The ordinary strict
  workload comparator accepted both 120-sample records without normalization
  or weakened assertions.
- The sole final production Release passed in 12.797 seconds, including the
  canonical runtime-prefix check. Final production Wasm SHA-256 is
  `5ad446ea8741eca9d0981f815d68a4514653ddf2dd7d94ad4b9b1688e0876f0d`.
- No broad suite, mission check, capture, recovery run, compatibility promotion,
  or unrelated work ran. Retail data, paths, and logs remain outside version
  control.

Numeric results and raw hashes are in
[the companion record](dobj-primary-basis-82b4de10.json).
