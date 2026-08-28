# DObj scratch-capacity renderer comparison

Commit `93451ec52d011c226a0134ae963601e9822588c3` makes the one renderer
optimization selected by the corrected profile: the C++ DObj scene builder
reuses numeric skinning scratch capacity across surfaces and frames. It does
not retain canonical pointers or change scene, visibility, material, geometry,
or WebGL semantics. The complete post-change aggregates are in
[`retail-profile-93451ec5.json`](retail-profile-93451ec5.json); the baseline is
[`retail-profile-e31d62ac.json`](retail-profile-e31d62ac.json).

Both captures used the same headed Chrome 152.0.7977.64 installation and the
same Windows 11 / Ryzen 7 7800X3D / 32 GiB / RTX 3070 Ti reference host. Each
row compares independent 60-second profiling-disabled clean windows and
independent 300-completed-gameplay-frame diagnostic profiles.

| Map | Clean FPS | p95 ms | p99 ms | game/wall | Scene CPU ms | Frontend CPU ms | Backend CPU ms | Result |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Airplane | 59.81 → 59.51 (-0.50%) | 18.81 → 19.12 | 20.32 → 20.99 | 0.999786 → 0.999783 | 7.67 → 6.93 (-9.71%) | 9.36 → 8.31 | 3.32 → 2.74 | PLAYABLE |
| Killhouse | 33.24 → 40.45 (+21.67%) | 33.31 → 27.25 | 35.88 → 28.86 | 0.999322 → 0.998363 | 10.07 → 9.35 (-7.15%) | 11.46 → 10.59 | 18.71 → 15.01 | PLAYABLE |
| Blackout | 25.18 → 28.79 (+14.34%) | 42.35 → 37.33 | 44.29 → 39.97 | 0.997247 → 0.998603 | 5.72 → 5.69 (-0.41%) | 7.50 → 7.36 | 29.96 → 25.35 | FUNCTIONAL |
| Bog A | 23.18 → 23.42 (+1.05%) | 49.20 → 48.46 | 51.45 → 51.52 | 0.999247 → 0.999583 | 12.41 → 12.45 (+0.34%) | 16.78 → 16.72 | 23.77 → 23.42 | FUNCTIONAL |
| Hunted | 20.94 → 20.93 (-0.03%) | 50.80 → 50.79 | 57.38 → 56.21 | 0.994791 → 0.994109 | 12.52 → 12.51 (-0.07%) | 14.51 → 14.50 | 29.98 → 29.91 | FUNCTIONAL |
| CargoShip | 13.29 → 14.22 (+6.99%) | 82.10 → 77.15 | 91.35 → 85.08 | 0.997490 → 0.998161 | 28.61 → 29.22 (+2.13%) | 32.29 → 32.94 | 38.61 → 34.56 | FUNCTIONAL |

CargoShip, the target workload, improved by 6.99% in the authoritative clean
window; p95 and p99 improved by 6.03% and 6.87%. However, the intended
scene-build stage did not reproduce that improvement in the separate profiled
window. It regressed by 2.13%, while the whole profiled gameplay frame improved
from 72.67 to 69.05 ms. The result justifies retaining the bounded allocation
change, but does **not** establish that scratch allocation was the dominant
remaining CargoShip frontend cost. No second optimization is stacked.

The change is CPU-only. GPU timings remained in the same workload-dependent
ranges; no GPU-stage improvement is attributed to it. Draw and upload work
remained equivalent for the target (1,571.0 → 1,567.6 dynamic batches and
7,858,854 → 7,856,002 submitted indices on average). CargoShip, Killhouse, and
Blackout held the same Wasm capacity; Bog A changed by +2.3 MiB and Hunted by
-1.3 MiB, with no monotonic regression. All six validations produced exactly
300 profile samples and 300 GPU results. Every exercised context recovery,
map transition, input/audio check, checkpoint, shutdown, and persistent reload
passed. No compatibility classification changed.
