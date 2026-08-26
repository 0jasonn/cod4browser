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

## Completed roadmap evidence

The 2026-08-26 execution preserved the non-retail Phase 0 baseline and
completed the local Killhouse/CargoShip matrix at clean source SHA
`ac063bb20cbc4027497841322d87c2069d736939`. Both maps are currently PLAYABLE
under the matrix. Transition retirement, configuration checkpoint/reload,
audio, input, memory telemetry, and forced context recovery passed. See the
[progress report](../ROADMAP_PROGRESS_REPORT.md).

Phase 2 retained the existing previous-map recovery eviction and made no new
optimization. It released 1,521,922,580 B of old-map aggregate CPU recovery to
zero before new-world publication, with no old/new recovery overlap. Keep
800 MiB as the per retained-image-pool admission cap: the Killhouse
static-model pool used 817,908,800 of 838,860,800 B (97.50%). Wasm capacity is
monotonic allocator capacity, not a retained-resource measurement. No
unobserved before/after (A/B) timing was inferred.

The first Phase 3 campaign batch validated Blackout as PLAYABLE at clean source
SHA `6be926cb4e78693f9f6e638c348b0ee0f908b45f`. Canonical database, ClipMap,
world, server, game, client, and cgame initialization passed, followed by a
60,022.685 ms gameplay window, input, audio, configuration persistence,
CargoShip-to-Blackout-to-Killhouse retirement, and forced context recovery.
The measured blocker was a browser-only 20,000 static-model-instance ceiling;
`164fc1f2` restored native IW3's 65,536-instance cardinality with focused
native/Wasm coverage. See the
[Phase 3 evidence](evidence/retail-phase3-6be926cb.json).

## Next, in order

1. Continue validating additional campaign zones individually and update the
   compatibility matrix; discovery is not compatibility.
2. Close remaining renderer/material and advanced audio parity gaps found by
   those measured scenes.
3. Add gamepad support behind the existing input boundary.
4. Design a browser-compatible cinematic path only as a narrow, independently
   tested subsystem.
5. Consider multiplayer only with a documented WebSocket/WebTransport relay;
   browsers cannot use COD4 UDP directly.
