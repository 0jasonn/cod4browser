# Corrected six-map renderer profile

This evidence was captured from clean commit
`e31d62ac640e42135b510b5a469396cf8c15978d` with Chrome 152.0.7977.64,
headed and foreground on Windows 11, an AMD Ryzen 7 7800X3D, 32 GiB of
system memory, and an NVIDIA GeForce RTX 3070 Ti through ANGLE D3D11. The
complete sanitized aggregates are in
[`retail-profile-e31d62ac.json`](retail-profile-e31d62ac.json).

Each map used a profiling-disabled 60-second clean window for classification,
followed by exactly 300 completed gameplay/render samples. Rotating,
non-nested WebGL timer queries attributed GPU time. The profiled window did
not affect classification.

## Clean classification evidence

| Map | FPS | p95 ms | p99 ms | game/wall | Profile samples | Profile overhead | Result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Airplane | 59.81 | 18.81 | 20.32 | 0.999786 | 300 | -0.16% | PLAYABLE |
| Killhouse | 33.24 | 33.31 | 35.88 | 0.999322 | 300 | +6.48% | PLAYABLE |
| Blackout | 25.18 | 42.35 | 44.29 | 0.997247 | 300 | -1.24% | FUNCTIONAL |
| Bog A | 23.18 | 49.20 | 51.45 | 0.999247 | 300 | -1.66% | FUNCTIONAL |
| Hunted | 20.94 | 50.80 | 57.38 | 0.994791 | 300 | -3.15% | FUNCTIONAL |
| CargoShip | 13.29 | 82.10 | 91.35 | 0.997490 | 300 | -1.41% | FUNCTIONAL |

The small negative overhead values are run-to-run variance, not claimed speedups.
Killhouse's measured 6.48% profiler cost confirms why the windows must remain
separate.

## Ranked bottleneck table

All values below are average milliseconds from the diagnostic profile. Shadow
and dynamic rows combine their separately persisted component aggregates only
for ranking; the JSON retains every individual distribution.

| Stage | Airplane | Killhouse | Blackout | Bog A | Hunted | CargoShip |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Renderer frontend CPU | 9.36 | 11.46 | 7.50 | 16.78 | 14.51 | **32.29** |
| Scene build CPU | 7.67 | 10.07 | 5.72 | 12.41 | 12.52 | **28.61** |
| Renderer backend CPU | 3.32 | 18.71 | 29.96 | 23.77 | 29.98 | **38.61** |
| World CPU | 0.45 | 3.96 | 9.10 | 8.64 | 10.43 | **9.01** |
| World GPU | 0.16 | 2.56 | 6.01 | 6.62 | 8.82 | **9.86** |
| Static models CPU | 0.48 | 5.69 | 7.86 | 4.54 | 5.94 | **11.99** |
| Static models GPU | 0.25 | 4.43 | 5.69 | 4.84 | 5.08 | **10.05** |
| Sun shadows CPU | 0.80 | 2.98 | 4.12 | 3.81 | 4.02 | **5.93** |
| Sun shadows GPU | 1.11 | 2.52 | 3.58 | 3.35 | 3.51 | **5.50** |
| Spot shadows CPU | 0.00 | 3.49 | 6.69 | 2.91 | 4.67 | **5.40** |
| Spot shadows GPU | 0.00 | 0.03 | 1.47 | 0.02 | 0.03 | **0.03** |
| Dynamic/FX CPU | 1.38 | 2.13 | 1.60 | 3.35 | 4.18 | **5.57** |
| Dynamic/FX GPU | 0.06 | 1.58 | 0.05 | 2.75 | 3.68 | **5.41** |
| UI/post GPU | 0.23 | 0.29 | 0.32 | 0.20 | 0.31 | **0.10** |

## Single selected target

CargoShip's renderer-frontend cost is the largest actionable fast/slow-map
difference: it is 22.93 ms above Airplane, and its nested scene-build interval
accounts for 28.61 ms of the 32.29 ms. The selected Phase 6 change is therefore
one Case D correction: reuse capacity for the C++ renderer frontend's
DObj skinning scratch storage across surfaces and frames. CargoShip submits
1,571 dynamic batches per profiled frame versus Airplane's 197, and the
frontend previously created four temporary vectors for each model/surface
conversion. Canonical scene generation, visibility, ownership, geometry, and
WebGL command semantics remain unchanged. No second renderer optimization is
included.
