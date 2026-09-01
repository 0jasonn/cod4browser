# Canonical DObj lighting-handle milestone

Runtime `a16fb9f2` restores the model-lighting reuse seam already present in
Kisak's native renderer. `cpose_t::lightingHandle` now identifies a numeric
browser lighting-cache entry. An exact lighting-origin and primary-light match
reuses the prior light-grid sample; a changed origin recomputes and replaces it.
World unload clears every cached sample, and a stale pose handle is safely
rebound on its next evaluation.

The cache retains light-grid colors, weights, indices and origins, not model,
pose, material or world pointers. The per-frame atlas is still rebuilt and
uploaded with the current submitted DObj ordering. The assigned canonical
handle also reaches the existing mark-generation path, with its prior nonzero
fallback preserved when model lighting is unavailable. Pose calculation,
skinning, LOD/hide policy, material resolution, static-model camera culling,
and independent sun/spot shadow selection are unchanged.

## Qualified result

The retained `82b4de10` primary-basis diagnostic artifact and final candidate
ran the seeded paused CargoShip workload in Chrome 152.0.7977.65 at 1440x1000
on the same Ryzen 7 7800X3D host. All 120 camera and work-count samples matched,
including work-count SHA-256
`fed8ea624d731601e62fc6639021ecec6eb3dd28ed9944461cf4704dcba9917c`.

| CPU interval | Control mean ms | Candidate mean ms | Observed change |
| --- | ---: | ---: | ---: |
| DObj lighting | 1.2058 | 0.1526 | -87.3% |
| DObj build | 3.5764 | 2.5052 | -30.0% |
| Scene build | 5.9351 | 4.8847 | -17.7% |
| Renderer frontend | 6.6595 | 5.6653 | -14.9% |

The profiling-disabled diagnostic interval was 16.5061 -> 16.6420 ms, while
the profiled total was 12.9792 -> 12.2836 ms. The default frame cap and browser
variation hide the CPU reduction in the clean interval, so this milestone does
not claim a gameplay FPS gain.

## Validation and limits

- Focused Win32 Debug `web_renderer_dobj_submission_tests` passed 1/1 in
  0.03 seconds after the final runtime changes. Its synthetic light grid proves
  handle assignment, exact-origin reuse, movement invalidation, and cache reset
  while preserving the existing DObj geometry, pose, rejection, ordering and
  shadow assertions.
- The final diagnostic Release and runtime-prefix check passed. The strict
  workload comparator accepted both 120-sample records without normalization
  or weakened assertions.
- The sole final production Release passed in 13.803 seconds, including the
  canonical runtime-prefix check. Final production Wasm SHA-256 is
  `36b16d58fc8aaf9ea697e291d50ca0938189f2b021a0410693f1ab0d2a3c152e`.
- No broad suite, mission check, capture, recovery run, compatibility promotion,
  or unrelated work ran. Retail data, paths and logs remain outside version
  control.

Numeric results and raw hashes are in
[the companion record](dobj-lighting-cache-a16fb9f2.json).
