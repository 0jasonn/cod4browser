# BSP sun-cascade visibility milestone

Runtime `a23850aa` applies Kisak's canonical `GfxSurface` bounds independently
to the near and far sun matrices before world-shadow submission. Each retained
surface span now carries its authored AABB. The backend builds one temporary
visible-range list per cascade, preserves holes and alpha-tested boundaries,
and still joins adjacent opaque spans.

This is the WebGL boundary equivalent of native BSP sun-view visibility. Camera
`surfaceVisData[0]` never participates, so off-camera casters remain eligible.
Static-model partition masks, authored spot membership, dynamic commands, and
the retained world index buffer are unchanged.

## Diagnostic result

The seeded paused CargoShip workload uses fixedtime 16, one stable camera, and
profile views 601-720. The control and candidate submit the same camera, model,
dynamic/UI, and upload work in every sample. The comparator qualifies only the
intended BSP sun reductions.

| Per-frame work | Control | Candidate | Reduction |
| --- | ---: | ---: | ---: |
| Physical shadow draws | 1,302 | 724 | 578 |
| Merged sun ranges | 5,488 | 1,374 | 4,114 |
| Submitted indices | 2,918,796 | 1,223,802 | 1,694,994 |

| CPU metric | Control | Candidate | Change |
| --- | ---: | ---: | ---: |
| Total sun-shadow draw | 1.208 ms | 1.108 ms | -8.27% |
| World sun selection/draw | 0.094 ms | 0.101 ms | +8.08% |
| Renderer backend | 7.103 ms | 6.940 ms | -2.29% |
| Total profiled CPU | 13.427 ms | 13.213 ms | -1.59% |

The small world-stage cost is the two light-space AABB scans. It is outweighed
in this diagnostic run by reduced submission work. Both candidate profiles used
the same diagnostic Wasm hash; the clean `a23850aa` rerun confirmed exact source
provenance and the same 120-sample logical-work reduction. Timing from the first
run is reported because the clean rerun experienced unrelated host slowdown.

## Final production result

The retained `26b3dc98` control and final `a23850aa` Release ran A/B/B/A. Each
window covers 300 uncapped frames through six canonical checkpoints.

| Run | Mean frame interval |
| --- | ---: |
| A1 control | 14.887 ms |
| B1 candidate | 15.391 ms |
| B2 candidate | 14.494 ms |
| A2 control | 15.492 ms |

Pair means are **15.189 -> 14.942 ms (1.63% lower)**. Control drift is 0.605 ms
and candidate drift is 0.897 ms, so this is a modest local paused-renderer
observation, not an active-gameplay FPS or pixel-equivalence claim.

## Validation and limits

- The focused `web_renderer_world_scene_tests` target passed (1/1, 0.03 s;
  0.05 s total). Its new case proves independent near/far selection, a rejected
  middle surface leaves a range hole, contiguous opaque spans still merge, and
  malformed bounds fail atomically.
- The targeted workload comparator passed for all 120 samples. Only shadow
  draws, merged sun ranges, and submitted indices changed; every other recorded
  work count remained exact.
- Diagnostic Release builds passed. One final production Release and canonical
  runtime-prefix check passed.
- No broad suite, mission check, capture, context-loss run, or compatibility
  promotion ran. Retail data, paths, and logs remain outside version control.
- Native dynamic scene-entity partition visibility remains open. This milestone
  does not derive dynamic caster membership from the camera or BSP selection.

Final production Wasm SHA-256:
`e27d47607616a27dcf0947be807b10b7987d02b767f9084d44b986ea4893aeb8`.
Final diagnostic Wasm SHA-256:
`d365cbd008f7dba2213de196e8dd86b5924b89dfc5e9045ace339d86c2d19ff6`.
Numeric results and raw hashes are in
the companion record (archived in Git).

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/bsp-sun-partitions-a23850aa.json`.
