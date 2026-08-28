# Web product roadmap

## Mission-route pass status — 2026-08-28

1. **Complete:** diagnostics-only versioned route parsing, waypoint steering,
   ordinary canonical input replay, clear timeout/stuck/death/divergence/
   cancellation failures, and canonical checkpoint-restart observation.
2. **Complete:** opt-in headed authoring records sparse sanitized numeric
   observations and input transitions; F8 marks a target and F9 writes the
   route plus evidence sidecar.
3. **Complete:** 12 synthetic tests cover the requested route contract and
   prove that no direct gameplay-state mutation API exists.
4. **Blocked by inputs, not classified as an engine defect:** no explicit
   `KISAK_COD4_RETAIL_ROOT` was supplied, so Village Assault objective/trigger
   progression cannot be proved in this pass.
5. **Not started by design:** CargoShip substage profiling, canonical DPVS
   census, six-map reprofiling, one renderer optimization, Village Assault
   save/reload completion, Scout Sniper, AC-130, and further Wasm fixes all
   remain behind the progression gate.

Current decision: run headed Village Assault route authoring/replay next with
an explicitly supplied legal retail root. Do not start renderer or campaign
expansion work first.

## Corrected-profile pass status — 2026-08-28

1. **Complete:** profile requests count completed gameplay/render samples,
   isolate pump diagnostics, time out explicitly, and reset/transition/context
   state deterministically.
2. **Complete:** each retail map uses an independent profiling-disabled
   60-second clean window and an exact 300-gameplay-frame diagnostic window.
3. **Complete:** sanitized CPU, renderer, GPU, counter, upload, memory, and
   classification aggregates are committed for all six maps.
4. **Complete:** diagnostics rotate non-nested asynchronous WebGL2 queries
   across world, static-model, sun-shadow, spot-shadow, dynamic/FX, and UI/post
   stages with disjoint/stale rejection.
5. **Complete:** the corrected six-map baseline and one DObj scratch-capacity
   optimization were validated. The target's clean performance improved, but
   the separately profiled target stage did not; no second renderer change is
   authorized in this pass.
6. **Not complete:** Airplane proves save/reload continuity but not an
   objective/trigger change. Strict Village Assault automation observed no
   canonical progression marker within its bounded action window, so it did
   not proceed to progressed checkpoint/death/shutdown/reload proof. This has
   not established a canonical defect.
7. **Complete:** encoded-image inspection/decode/recovery telemetry proved and
   removed one avoidable initial re-decode. The exact seven-stop map chain,
   context recovery, retirement, cache bound, and memory behavior pass.
8. **Complete:** one focused SP Wasm numeric family has matching native/Wasm
   tests and a committed triage of the suspicious remainder.
9. **Prepared only:** `scoutsniper`, `village_assault`, and `ac130` exist in
   the supplied legal installation but remain `UNTESTED`. Do not execute broad
   expansion until the mission-progression gate is stable. The discovery-only
   evidence is [recorded here](evidence/next-campaign-batch-bad1e7b9.json).

Current decision: the measured renderer bottleneck remains open, and no map
has `MISSION_FLOW_VALIDATED`. The six-map regression set stays permanent.

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

The clean `f5229806` foreground baseline and `247980a6` campaign batch validate
six maps. Killhouse and Airplane are `PLAYABLE`; CargoShip, Blackout, Hunted,
and Bog A are `FUNCTIONAL`; 16 discovered direct SP zones remain `UNTESTED`.
All six pass the canonical runtime, real gameplay input, audio, checkpoint,
transition, context-recovery, and valid foreground stability boundaries.

Encoded-source recovery retains canonical compressed sources and reuses the
existing decoder. At the comparable Killhouse point it reduced aggregate CPU
recovery by 64.27% and Wasm capacity by 45.28%. Context recovery remained
successful but became +57% to +94.75% slower (approximately +90% on returned
Killhouse), while Killhouse first frame increased 14.43% and CargoShip remained
approximately flat. Previous-map resources still retire before the next world
publishes, and the 800 MiB per-pool decoded admission limit is unchanged.

The historical browser-only 20,000 static-model-instance ceiling is fixed at
the native IW3 65,536 cardinality. The old background-throttled performance
figures do not establish current compatibility; only the clean foreground
records do. See the [execution report](../WEB_ROADMAP_EXECUTION_REPORT.md).

## Next, in order

1. **Completed at `9e75a9dd`:** profile Airplane, Killhouse, Blackout, Bog A,
   Hunted, and CargoShip in valid foreground windows; rank the measured stages;
   implement exactly one evidence-selected renderer optimization; rerun all six
   functional and performance matrices. Opaque shadow texture/sampler binds
   were removed, reducing six-map average shadow CPU time by 36.91% and binds
   by 32.49%, while every lifecycle gate remained green.
2. **Completed at `da1e592c`:** the smallest diagnostics-only canonical state
   probe and one opt-in headed Airplane mission validator prove live
   AI/scripts/objectives, combat, a natural checkpoint, named game save,
   death/restart, browser shutdown, fresh-runtime save reload, restored state,
   and continued progression.
3. Continue renderer work only from the current measured bottleneck. World and
   shadow drawing now average 7.11/7.14 ms across the six maps, static models
   average 6.48 ms, and CargoShip remains the worst case. Do not combine this
   next optimization with the mission validator.
4. Measure encoded-image inspection/decode/upload/recovery work and improve it
   only if duplicate work is demonstrated without giving back the memory win.
5. The next prepared batch is `scoutsniper` (outdoor and long-range AI),
   `village_assault` (dense scripts/objectives/triggers), and `ac130` (vehicle,
   thermal, FX, and material paths). Preparation does not promote compatibility;
   discovery remains `UNTESTED` until each legal local runtime gate passes.
