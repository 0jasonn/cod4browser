# Static spot-shadow membership milestone

Runtime `26b3dc98` translates Kisak's authored static spot-caster list into one
packed visibility mask per selected light. Static surface batches scan that mask
instead of repeating canonical-index binary searches for every model surface.
The existing CPU shadow mask is reused; no GPU buffer, pause cache, or alternate
asset model is added.

`GfxWorld::shadowGeom` remains authoritative. The mask follows current LOD
packing, camera DPVS never selects casters, near/far sun masks remain independent,
and authored spot membership and draw order are unchanged.

## Diagnostic result

The seeded paused CargoShip workload uses fixedtime 16, one stable free-camera
view, and exact profile views 601-720. Diagnostic-only attribution first placed
0.982 ms of the 1.484 ms spot-shadow pass in static models; world casters used
0.021 ms and the current backend submitted no dynamic spot casters.

| Metric | Control | Candidate | Change |
| --- | ---: | ---: | ---: |
| Static spot-shadow CPU | 0.982 ms | 0.535 ms | -45.5% |
| Total spot-shadow CPU | 1.484 ms | 1.074 ms | -27.7% |
| Renderer backend CPU | 7.357 ms | 7.103 ms | -3.5% |
| Total profiled CPU | 13.594 ms | 13.427 ms | -1.2% |
| Profiler-inactive interval | 16.782 ms | 16.362 ms | -2.5% |

The ordinary diagnostic comparator confirms all 120 work-count samples match,
including draw calls, submitted indices, uploads, camera/static retention,
dynamic/UI commands, and shadow caster totals. Both runs completed with no page
errors.

## Final production result

The retained `501a68d8` control artifact and the one final Release from
`26b3dc98` ran in A/B/B/A order. Each run uses six canonical checkpoints covering
300 uncapped frames; product defaults remain unchanged outside the benchmark.

| Run | Mean frame interval |
| --- | ---: |
| A1 control | 12.648 ms |
| B1 candidate | 12.145 ms |
| B2 candidate | 12.035 ms |
| A2 control | 12.606 ms |

Pair means are **12.627 -> 12.090 ms (4.25% lower)**. Both candidates beat both
controls, and the controls close within 0.043 ms. This is local paused-renderer
throughput, not active-gameplay FPS or pixel-equivalence evidence.

## Validation and limits

- The focused `web_renderer_static_model_scene_tests` target passed (1/1,
  0.09 s). The new case uses authored canonical membership reordered relative
  to packed instances, a second light, an empty light, and an invalid size.
- The focused Node aggregate test passed (3/3) after adding the three disjoint
  spot-family fields. The first profile exposed the missing aggregate allowlist;
  the targeted retry passed after correcting that instrumentation boundary.
- Diagnostic Release builds passed for attribution and the candidate. One final
  production Release and canonical runtime-prefix check passed.
- No broad suite, mission check, capture, context-loss run, or compatibility
  promotion ran. Retail data, paths, and logs remain outside version control.
- Native Kisak also applies per-light visibility to dynamic scene entities. That
  technique remains incomplete in the web backend and is tracked in the renderer
  optimization audit; this milestone does not invent a substitute visibility
  rule.

Final production Wasm SHA-256:
`9b4fe094c22139f4df68135be3b20a6fc40071f8a83fc1b880628c6fd49d3ed4`.
Final diagnostic Wasm SHA-256:
`f8ac483291f36a594ebedd9f41c7ca2fc379c4aa1b14446a3606214b0b102030`.
Numeric results and raw hashes are in
the companion record (archived in Git).

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/static-spot-membership-26b3dc98.json`.
