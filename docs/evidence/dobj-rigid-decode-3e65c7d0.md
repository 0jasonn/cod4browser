# Dynamic DObj renderer convergence milestone

Runtime `3e65c7d0` closes the measured rigid-surface cost without changing the
canonical draw command. Immutable packed attributes for encountered rigid
`XSurface` vertex arrays are decoded once into bounded renderer scratch.
Current Kisak bone matrices still transform every position, normal and tangent
each frame, and the normal and tangent are normalized after that transform.
The cache is capped by the existing 250,000-vertex dynamic command limit and is
released by `R_UnloadWorld`; it retains no model, pose or material pointer.

This completes the current DObj renderer-convergence path. Pose still comes
directly from `CG_DObjCalcPose`. Weighted positions use every influence while
their basis follows Kisak's primary-bone rule. Model lighting reuses canonical
`cpose_t::lightingHandle` identity. LOD/hide masks, static-model camera culling,
dynamic world-space bounds, and independent sun and spot shadow submission are
unchanged. A measured GPU-placement candidate was rejected because it changed
shadow partition work and raised total time in this WebGL2 backend.

## Qualified result

The retained `4c6a1784` control and the final candidate ran the seeded paused
CargoShip workload in Chrome 152.0.7977.65 at 1440x1000 on the same Ryzen 7
7800X3D host. The strict comparator matched all 120 work-count samples,
including work-count SHA-256
`fed8ea624d731601e62fc6639021ecec6eb3dd28ed9944461cf4704dcba9917c`.

| CPU interval | Control mean ms | Candidate mean ms | Observed change |
| --- | ---: | ---: | ---: |
| DObj rigid skinning | 0.7094 | 0.5515 | -22.3% |
| DObj skinning | 1.2314 | 1.0227 | -16.9% |
| DObj build | 2.4566 | 2.2510 | -8.4% |
| Scene build | 4.9020 | 4.5659 | -6.9% |
| Renderer frontend | 5.6589 | 5.2855 | -6.6% |
| Profiled total | 11.9699 | 11.8297 | -1.2% |

The profiling-disabled interval was 16.6679 -> 16.6798 ms. The capped clean
interval is flat, so this milestone claims lower renderer CPU work rather than
a gameplay FPS increase.

## Validation and limits

- The focused Release `web_renderer_dobj_submission_tests` target passed after
  the final runtime change. It retains the pose, one-to-four-weight, rigid-list,
  hide-mask, malformed-input, atomic-publication, lighting and shadow assertions.
- The final diagnostic Release and canonical runtime-prefix check passed. The
  strict `renderer_workload.mjs --profiles` comparator accepted 120/120 samples
  without normalization or weakened assertions.
- The sole final production Release passed in 19.622 seconds, including the
  canonical runtime-prefix check. Final production Wasm SHA-256 is
  `fd604a8e07dcf3129dcad7635e3da2bbdeacfcbd40c888189f68e5d3bf8d5a86`.
- No mission check, broad suite, capture, recovery run, compatibility promotion
  or unrelated task ran. Retail data, paths and logs remain outside version
  control.

Numeric results and raw hashes are in
the companion record (archived in Git).

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/dobj-rigid-decode-3e65c7d0.json`.
