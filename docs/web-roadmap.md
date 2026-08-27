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

1. **Instrumentation completed at `91788492` and `aff008c3`:** bounded
   diagnostics-only CPU frame-stage, renderer-stage, exact-work, separate
   shadow preparation/draw, upload duration/bytes, and optional asynchronous
   WebGL2 GPU timing evidence. Six-map evidence remains pending step 2.
2. Profile Airplane, Killhouse, Blackout, Bog A, Hunted, and CargoShip in valid
   foreground windows. Rank the measured stage deltas.
3. Implement exactly one renderer optimisation selected by that evidence, then
   rerun the six-map functional and performance matrix.
4. Prove one representative canonical mission through AI/scripts/objectives,
   combat, checkpoint, death/restart, browser shutdown, save reload, and
   continued progression.
5. Measure encoded-image inspection/decode/upload/recovery work and improve it
   only if duplicate work is demonstrated without giving back the memory win.
6. After those gates are stable, select the next two or three discovered SP
   maps by coverage value. Discovery remains `UNTESTED`.

The present process has no explicitly supplied `KISAK_COD4_RETAIL_ROOT`.
Instrumentation and synthetic gates are complete, but steps 2–6 cannot produce
fresh retail conclusions until that input is provided. No optimisation is to
be selected speculatively.
