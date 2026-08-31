# Web product status

Updated 2026-08-31. This is the single current status page; the
[roadmap](web-roadmap.md) owns priorities and the
[convergence inventory](web-port-convergence.md) owns system classification.

## Renderer continuation from `bf5ec1e2`

Unchanged static-model LOD groups now avoid repacking and batch-range updates;
empty shadow batches skip setup. Camera draw ranges are separate, but canonical
DPVS camera filtering is **not integrated**: its native view/reset/global and
worker dependencies require a further seam. Camera and shadow draws still use
the conservative LOD-packed data. Five diagnostic DObj timings distinguish
total build, pose, lighting, skinning, and geometry costs without adding caches.

See [the renderer record](evidence/renderer-efficiency-2026-08-31.md) for exact
checks and remaining blockers. No new retail capture, visual assessment, FPS
measurement, or compatibility promotion was performed. The earlier cleanup
results below remain historical relative to this continuation.

## Cleanup baseline at `bf5ec1e2`

The DB publication hook now reports publication only. Canonical
`R_RenderScene` owns world drawing; the obsolete single-surface proof and its
private mirror types/tests are retired. The live world command retains bounds,
range, index, and atomic-publication checks and now also checks finite lightmap
coordinates before publication. Live surface APIs remain.

Assisted mission authoring, autonomous combat fallback, helper-only diagnostic
exports, and unused assist/prefix-skip options are removed. Manual F8/F9
recording and explicit replay remain diagnostic tools. Neither mission
progression nor save/death/restart validation is a prerequisite for future work.

World, static-model, and DObj paths share only identical material table lookups.
Worker hosts share request IDs, error envelopes, rejection cleanup, and reply
settlement; operation/event allowlists, timeout policies, recovery, and
filesystem leases remain separate. The static-model camera pass skips empty
LOD batches before material state and texture binds. No performance gain is
claimed without a new measurement.

Checks and remaining risks for this task are recorded in the
[cleanup evidence](evidence/cleanup-renderer-2026-08-31.md). No retail runtime,
new browser boot, screenshot, visual assessment, or compatibility promotion was
performed during this cleanup. The supplied installation was not copied or
modified. Visual gameplay assessment remains with the user.

## Historical automated evidence

These are earlier execution results, not checks of this working tree:

- The corrected Chrome 152 six-map profiles at `e31d62ac` and `93451ec5`
  classify Killhouse/Airplane as `PLAYABLE` and CargoShip/Blackout/Hunted/Bog A
  as `FUNCTIONAL`. CargoShip scene construction remained expensive after the
  scratch-capacity change; see [the comparison](evidence/retail-profile-93451ec5.md).
- `39de3d6d` recorded seven loads through the six-map set, canonical lifecycle
  plus 30 world frames at each stop, context recovery, no duplicate decoding,
  and bounded source-cache retirement. See
  [the regression record](evidence/retail-six-map-regression-39de3d6d.json).
- `da1e592c` recorded Airplane save/reload continuity and continued play. It
  did not prove objective/trigger progression. The later Village Assault
  action window also did not prove progression or an engine defect. These
  historical observations impose no development gate.

The [campaign matrix](campaign-compatibility.md) retains the exact historical
classifications; other discovered direct SP zones remain `UNTESTED`.
[Earlier status narratives](history/web-status-through-2026-08-28.md) preserve
measurements and their original context. No fresh visual claim is made here.

## Product boundaries

WebGL2 and the Worker/DOM/storage/audio adapters are platform-owned. Kisak owns
assets, game state, filesystem semantics, and the renderer frontend. Production
and diagnostics remain separate artifacts. Imported retail files remain local;
proprietary binaries and data are never distributed. Cinematics remain explicit
omissions; no WebGPU, pthreads, multiplayer, dependencies, or presentation work
was added.
