# Web product roadmap

Updated 2026-08-31. This is the single active roadmap; earlier orders and
mission-progression gates are superseded. Mission playing, progression
validation, route generation, and campaign expansion are outside this task.
Manual gameplay assessment can guide renderer work without a progression gate.

1. Static-model LOD evaluation now skips repacking unchanged groups; both
   camera and shadow passes reject empty batches before material setup. Camera
   draw ranges are separate, but native DPVS production remains blocked on
   native view/reset ownership and the coupled worker dispatch. Finish that
   native dependency seam before consuming `smodelVisData`; do not treat loaded
   bytes as a current camera result. The
   [renderer record](evidence/renderer-efficiency-2026-08-31.md) names the exact
   blockers and completed prerequisite. World-surface filtering stays deferred.
2. Preserve canonical asset/game/renderer ownership, imported-asset validation,
   and durable filesystem shutdown. Small shared material lookups and Worker
   transport bookkeeping must not absorb technique selection or host-specific
   filesystem recovery policy.
3. Use the five new diagnostic DObj build/substage timings to distinguish pose,
   lighting, skinning, and geometry construction before considering caches.
   No fresh capture or speedup is claimed. Keep any future measurement short
   and question-specific; mission automation remains outside this work.

This renderer task allows at most one focused test command and one incremental
production Release build per step; diagnostics are built only for changed
diagnostic code. No routine full tiers, mission checks, or lifecycle matrices
apply. The renderer record separates checks actually run from the historical
cleanup and CargoShip evidence.

Historical plans and completed milestones remain in
[the earlier roadmap](history/web-roadmap-through-2026-08-28.md) and
[execution report](../WEB_ROADMAP_EXECUTION_REPORT.md). Use
[current status](web-status.md) for evidence scope; file discovery never
promotes compatibility.
