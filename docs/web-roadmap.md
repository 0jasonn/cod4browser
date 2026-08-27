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
2. Add the smallest diagnostics-only canonical state probe and one opt-in
   headed retail mission validator, then prove one representative mission
   through AI/scripts/objectives,
   combat, checkpoint, death/restart, browser shutdown, save reload, and
   continued progression.
3. Continue renderer work only from the current measured bottleneck. World and
   shadow drawing now average 7.11/7.14 ms across the six maps, static models
   average 6.48 ms, and CargoShip remains the worst case. Do not combine this
   next optimization with the mission validator.
4. Measure encoded-image inspection/decode/upload/recovery work and improve it
   only if duplicate work is demonstrated without giving back the memory win.
5. After the mission and performance gates are stable, select the next two or
   three discovered SP
   maps by coverage value. Discovery remains `UNTESTED`.
