# Web product roadmap

## Completed cleanup boundary

- Separate production and diagnostic CMake targets/sites.
- Remove Gate 2, proof jobs, generic calls, test controls, monkey patches, and
  the old filesystem bridge from production.
- Version and validate the product Worker protocol.
- Make filesystem flush/shutdown durable and enforce one writable-home owner.
- Add idempotent browser-module disposal and explicit Worker termination order.
- Extract WebGL context ownership and add renderer/audio memory telemetry.
- Version the offline SP asset profile and expose cinematic omission.
- Gate production files, map objects/symbols, Wasm export count, and artifact
  sizes in CI.

## Next, in order

The 2026-08-26 execution completed the non-retail baseline and is blocked
before step 1 because `KISAK_COD4_RETAIL_ROOT` was not supplied. See the
[progress report](../ROADMAP_PROGRESS_REPORT.md).

1. Run the documented local retail matrix for Killhouse and CargoShip with
   user-owned assets, including transition, persistence, audio, input, and
   context recovery.
2. Use measured telemetry to classify reloadable/non-reloadable recovery
   data, then evaluate compressed-source retention, deduplication, or LRU.
3. Validate campaign zones individually and update the compatibility matrix;
   discovery is not compatibility.
4. Close remaining renderer/material and advanced audio parity gaps.
5. Add gamepad support behind the existing input boundary.
6. Design a browser-compatible cinematic path only as a narrow, independently
   tested subsystem.
7. Consider multiplayer only with a documented WebSocket/WebTransport relay;
   browsers cannot use COD4 UDP directly.
