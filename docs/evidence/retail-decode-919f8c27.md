# Encoded-source decode and recovery validation

This comparison used headed Chrome 152.0.7977.64 on Windows 11, an AMD Ryzen
7 7800X3D, 32 GiB system memory, and the same Release diagnostics build mode.
Both runs executed the exact chain:

`Killhouse -> CargoShip -> Blackout -> Hunted -> Bog A -> Airplane -> Killhouse`

Each stop waited 30 sustained world frames and then forced one WebGL context
loss/restoration. The clean before run is `92a93e39`; the clean after run is
`919f8c27`, containing the implementation from `a83f8047`. Full sanitized
aggregates are in the adjacent JSON evidence files.

## Result

The previous publication path decoded each retained encoded image to validate
it, discarded those pixels, and decoded the same source again for first upload.
The corrected path applies the decoder's shared format/layout validation without
allocating pixels, retains the encoded source, and decodes once for first upload.
Context recovery still re-decodes the retained encoded source.

| Metric | Before | After | Change |
| --- | ---: | ---: | ---: |
| Initial-upload decoder calls | 12,046 | 6,033 | -49.92% |
| Immediate duplicate decodes | 6,023 | 0 | -100.00% |
| Initial decoded bytes | 13,802,133,384 | 6,952,152,004 | -49.63% |
| Initial decode CPU | 9,632.13 ms | 4,906.47 ms | -49.06% |
| Mean map-command to first world frame | 7,134.58 ms | 6,811.54 ms | -4.53% |
| Mean context recovery to first world frame | 1,582.74 ms | 1,555.82 ms | -1.70% |

| Chain stop | First frame before | First frame after | Initial decode CPU before | Initial decode CPU after |
| --- | ---: | ---: | ---: | ---: |
| Killhouse | 5,675.46 ms | 4,848.34 ms | 1,747.58 ms | 882.18 ms |
| CargoShip | 7,205.33 ms | 7,093.50 ms | 869.78 ms | 444.87 ms |
| Blackout | 7,308.33 ms | 7,233.13 ms | 1,597.46 ms | 808.85 ms |
| Hunted | 6,511.52 ms | 6,296.09 ms | 1,647.40 ms | 850.53 ms |
| Bog A | 9,850.99 ms | 9,857.50 ms | 1,590.54 ms | 836.70 ms |
| Airplane | 7,632.06 ms | 7,390.92 ms | 393.62 ms | 199.97 ms |
| Returned Killhouse | 5,758.38 ms | 4,961.30 ms | 1,785.75 ms | 883.37 ms |

Bog A's 6.50 ms first-frame difference is neutral measurement noise (+0.07%),
while its measured decoder CPU fell 47.40%. No claim is made that decoding owns
all map-load latency.

## Recovery and cache acceptance

- All seven map stops produced canonical world frames before and after context
  restoration; recovery pixel-decode work remained present on every stop.
- The post-change recovery run performed 6,052 context-recovery decoder calls,
  decoded 7,006,387,140 bytes, and reported zero duplicate decodes.
- Map-owned renderer encoded sources reached zero at every world-unload boundary.
- The DB load-definition cache is intentionally global across world unload. Its
  entry and byte counts remained unchanged within every unload boundary.
- The maximum observed cache payload was 27,919,060 bytes against a 268,435,456
  byte hard budget. No eviction was needed in either run.
- Post-change context recovery caused zero Wasm linear-memory capacity growth on
  every stop. No functional map, transition, or context-recovery regression was
  observed.

The optimization is accepted. It changes neither canonical asset identity nor
decoded pixel semantics; it only shares validation logic between inspection and
decode so initial publication no longer converts the same pixels twice.
