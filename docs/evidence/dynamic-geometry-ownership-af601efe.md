# Dynamic geometry ownership milestone

Runtime `af601efe` removes the last full CPU copy of final per-frame dynamic
vertex and index storage. The renderer frontend now transfers those two vectors
through the existing backend command boundary. The backend still performs the
same descriptor, finite-value, index, batch, image, lighting and GPU-upload
checks before publication. A failed submission restores both source vectors;
only a successful publication exchanges their reusable capacity with the prior
backend staging storage.

World and static-model commands are unchanged. Canonical camera DPVS, static
model culling, dynamic draw ordering, and independent sun and spot shadow
selection remain separate. No asset, pose, animation, gameplay, JavaScript or
GPU object ownership moved across the platform boundary.

## Qualified result

The seeded paused CargoShip workload fixes engine time and camera position and
profiles views 601-720. The control and candidate match all 120 recorded work
samples exactly, including a common work-count SHA-256 of
`fed8ea624d731601e62fc6639021ecec6eb3dd28ed9944461cf4704dcba9917c`.

| CPU interval | Control mean ms | Candidate mean ms | Change |
| --- | ---: | ---: | ---: |
| Command geometry copy | 0.1217 | 0.0005 | -99.6% |
| Dynamic copy total | 1.0026 | 0.8916 | -11.1% |
| Dynamic submission | 1.7592 | 1.6389 | -6.8% |
| Renderer frontend | 6.6323 | 6.5711 | -0.9% |

The profiling-disabled diagnostic interval was 16.4638 -> 16.5193 ms and the
profiled total was 12.7807 -> 12.8671 ms. Those whole-frame values are flat
within run variation, so this milestone claims removal of the attributed copy,
not a general gameplay FPS improvement.

An authored active-scene investigation exposed the larger workload: its control
sample averaged 195,000 dynamic vertices, about 16 MiB of buffer transfer,
5.181 ms of DObj construction and 3.000 ms of dynamic copy. The ownership build
reduced its command geometry copy from 0.636 to 0.001 ms, but neither fixed time
nor `sv_mapSeed 1` reproduced the authored camera, effects and work counts.
Even two unchanged control loads diverged, so the strict comparator rejected
those runs. They are bottleneck evidence only and support no A/B or FPS claim.

## Rejected follow-up and category boundary

A follow-up retained an inactive VAO/VBO/index-buffer set and updated it with
`glBufferSubData` before swapping it into use. It matched all 120 paused work
samples, but dynamic geometry upload was 0.5056 -> 0.5085 ms, renderer buffer
upload was 0.1555 -> 0.1579 ms, and the clean interval was 16.5193 -> 17.3294
ms. The candidate was reverted; it added GPU memory and lifecycle state without
a measured benefit.

This closes the measured platform geometry-handoff work. The largest exposed
active frontend component is now shared DObj pose, lighting and skinning rather
than backend storage. Further work should begin from that canonical
animation/model boundary instead of adding WebGL buffer machinery.

## Validation and limits

- Focused `renderer_workload.mjs --profiles` comparison passed 120/120 exact
  camera and work-count samples.
- The ownership diagnostic Release and runtime-prefix check passed. The rejected
  buffer candidate also built and profiled successfully before being reverted.
- The one final production Release and runtime-prefix check passed in 18.947 s.
- Final production Wasm SHA-256:
  `17acbcefe80beada7a8c1744a26d850d4a5b554cdc013fdc71d0255e8aadecd2`.
- Final accepted diagnostic Wasm SHA-256:
  `2b74038748e1978a723c5ac17c9b3bb60c45234a0ec3ee5971330e7494734082`.
- No assertion was weakened. No broad suite, mission check, capture,
  context-loss run or unrelated compatibility work ran. Retail data, paths and
  logs remain outside version control.

Numeric results and raw hashes are in
[the companion record](dynamic-geometry-ownership-af601efe.json).
