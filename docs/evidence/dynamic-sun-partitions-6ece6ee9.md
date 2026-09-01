# Dynamic sun-cascade visibility milestone

Runtime `6ece6ee9` retains one world-space AABB with each flattened dynamic draw
and tests it independently against the near and far sun matrices. Skinned DObj
and transformed DynEnt model bounds come from their final indexed geometry.
Moving brushes reuse retained local geometry bounds transformed by the current
canonical placement.

This is the WebGL boundary equivalent of Kisak's dynamic sun-view visibility
for scene DObjs, scene XModels, DynEnt models, moving brushes, and DynEnt
brushes. Camera visibility is absent. Material shadow eligibility, depth-hack
exclusion, placement/buffer identity, alpha boundaries, and opaque joining are
preserved.

## Diagnostic result

The seeded paused CargoShip workload uses fixedtime 16, one stable camera, and
profile views 601-720. All 120 samples match for camera/world/static retention,
dynamic/UI commands, uploads, and buffers. The targeted comparator qualifies
only the intended dynamic sun reductions.

| Per-frame work | Control | Candidate | Reduction |
| --- | ---: | ---: | ---: |
| Physical shadow draws | 724 | 330 | 394 |
| Merged dynamic sun ranges | 1,374 | 0 | 1,374 |
| Submitted indices | 1,223,802 | 957,990 | 265,812 |

| CPU metric | Control | Candidate | Change |
| --- | ---: | ---: | ---: |
| Dynamic sun draw | 0.148 ms | 0.041 ms | -71.9% |
| Total sun-shadow draw | 1.181 ms | 0.535 ms | -54.7% |
| Dynamic command copy/bounds | 0.606 ms | 0.934 ms | +54.1% |
| Renderer backend | 9.195 ms | 8.700 ms | -5.38% |
| Total profiled CPU | 16.062 ms | 15.936 ms | -0.79% |

The added CPU cost is the per-frame indexed-bound construction required after
the current flattened command loses native scene-entity visibility records. It
is smaller than the shadow submission saving in this run, but it is a concrete
cost and a future canonical producer integration may retire that scan.

## Final production result

The retained `a23850aa` control and final `6ece6ee9` Release ran A/B/B/A. Each
window covers 300 uncapped frames through six canonical checkpoints.

| Run | Mean frame interval |
| --- | ---: |
| A1 control | 14.454 ms |
| B1 candidate | 15.128 ms |
| B2 candidate | 13.713 ms |
| A2 control | 15.282 ms |

Pair means are **14.868 -> 14.421 ms (3.01% lower)**. Control drift is 0.829 ms
and candidate drift is 1.416 ms. This is a noisy local paused-renderer result,
not active-gameplay FPS or pixel-equivalence evidence.

## Validation and limits

- The focused `web_renderer_dobj_submission_tests` target passed (1/1, 0.02 s;
  0.04 s total). Its shadow-range case now proves partition-invisible draws
  break opaque runs without changing index, placement, or cutout identity.
- The targeted workload comparator passed all 120 samples. Only physical
  shadow draws, merged dynamic ranges, and submitted indices changed.
- Diagnostic Release builds passed. One final production Release and canonical
  runtime-prefix check passed.
- No broad suite, mission check, capture, context-loss run, or compatibility
  promotion ran. Retail data, paths, and logs remain outside version control.
- Dynamic spot submission remains open. This milestone does not infer spot
  membership from sun or camera visibility.

Final production Wasm SHA-256:
`50bb7cef7f5e7e9db334faffdc553329f2e7f6e39de4a77fb0575323faa1c1a2`.
Final diagnostic Wasm SHA-256:
`21edca25af14c982400e257b79ddde2392de724333da021560099df6d3c81e40`.
Numeric results and raw hashes are in
[the companion record](dynamic-sun-partitions-6ece6ee9.json).

## Recommended next task

Bring over dynamic scene-entity spot-shadow visibility and submission. Reuse
the retained world-space dynamic bounds against each authored spot matrix, keep
per-light selection independent, and do not substitute sun or camera bytes.
