# Dynamic opaque draw-order milestone

Runtime `c8c4f335` carries Kisak's numeric material draw-surface key into the
WebGL backend and stable-sorts only contiguous camera runs proven safe to move:
ordinary DObj, DynEnt XModel, moving-brush, and DynEnt-brush draws with no
blending, color writes enabled, depth writes enabled, and LESS/LEQUAL depth.

Blended, depth-equal, depth-disabled, no-color, FX, sun sprite/flare, and other
state-sensitive batches remain fixed append-order anchors. Equal keys preserve
their original order. Ordinary and depth-hack draws still execute in separate
camera passes. Shadow draws retain their independent original order, culling,
and coalescing.

## Diagnostic result

The seeded paused CargoShip workload uses fixedtime 16, one stable camera, and
profile views 601-720. Camera/world/static work, dynamic/UI command sizes,
uploads, draw calls, submitted indices, and shadow work match exactly for all
120 samples.

| Dynamic camera metric | Control | Candidate | Change |
| --- | ---: | ---: | ---: |
| Draw calls | 1,323 | 1,323 | exact |
| Material-state updates | 556 | 418 | -138 (-24.8%) |
| Feature-state updates | 567 | 348 | -219 (-38.6%) |
| Material-state CPU | 0.650 ms | 0.621 ms | -4.34% |
| Texture-state CPU | 1.016 ms | 0.878 ms | -13.6% |
| Dynamic camera block | 3.820 ms | 3.641 ms | -4.69% |
| Dynamic command copy/order | 0.949 ms | 0.988 ms | +4.04% |

The ordering vector costs one 32-bit index per dynamic draw. The retained
numeric sort key is platform command metadata; no geometry copy, engine object,
material pointer alias, or persistent sort cache was added.

## Final production result

The retained `9a253c6a` control and final `c8c4f335` Release ran A/B/B/A. Each
window covers 300 uncapped frames through six canonical checkpoints.

| Run | Mean frame interval |
| --- | ---: |
| A1 control | 14.150 ms |
| B1 candidate | 14.760 ms |
| B2 candidate | 13.472 ms |
| A2 control | 15.077 ms |

Pair means are **14.613 -> 14.116 ms (3.41% lower)**. Control drift is 0.927 ms
and candidate drift is 1.288 ms. This is a noisy local paused-renderer result,
not active-gameplay FPS or pixel-equivalence evidence.

## Validation and limits

- The focused `web_renderer_dobj_submission_tests` target passed (1/1, 0.03 s;
  0.05 s total). It proves unsafe anchors do not move and equal-key batches
  remain stable while safe runs follow their sort keys.
- The diagnostic comparator passed 120/120 exact work-count samples.
- Diagnostic Release and runtime-prefix checks passed. One final production
  Release and runtime-prefix check passed.
- A blocked attempt to replace an existing ignored control directory was
  retried with a fresh verified path; no build or renderer assertion changed.
- No broad suite, mission check, capture, context-loss run, or compatibility
  promotion ran. Retail data, paths, and logs remain outside version control.

Final production Wasm SHA-256:
`0c438933362da1b96e4775ea38af78fffd6677d6e5a9c09a4e93f983506cc7a5`.
Final diagnostic Wasm SHA-256:
`3a1c214e11f7ded2d9ef83fb53f51204584dc73d1b91a730b4897a5945391c4d`.
Numeric results and raw hashes are in
the companion record (archived in Git).

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/dynamic-opaque-sort-c8c4f335.json`.
