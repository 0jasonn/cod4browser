# Web product roadmap

Updated 2026-08-31. This is the single active roadmap; earlier orders and
mission-progression gates are superseded. Mission playing, progression
validation, route generation, and campaign expansion are outside this task.
Manual gameplay assessment can guide renderer work without a progression gate.

1. Continue bounded renderer work from the existing CargoShip profiles and
   canonical source inspection. Empty static-model LOD batches now skip camera
   material setup. This is not a measured speedup. The
   [cleanup record](evidence/cleanup-renderer-2026-08-31.md) identifies the next
   upstream DPVS seam and why camera filtering cannot simply be applied to
   shared shadow instances.
2. Preserve canonical asset/game/renderer ownership, imported-asset validation,
   and durable filesystem shutdown. Small shared material lookups and Worker
   transport bookkeeping must not absorb technique selection or host-specific
   filesystem recovery policy.
3. Address additional material/runtime gaps only from a concrete observed
   failure or profile. Keep manual authoring and explicit replay opt-in; do not
   grow another mission-playing helper or use mission evidence to block work.

Testing for this cleanup is limited to two targeted commands and one final
incremental production Release build. Diagnostics are built only to verify
removal of the helper-only exports. No routine tiers, full suites, retail
profiling, or mission checks are required here. Future test scope should match
the risk of the change rather than inherit old mission/lifecycle matrices.

Historical plans and completed milestones remain in
[the earlier roadmap](history/web-roadmap-through-2026-08-28.md) and
[execution report](../WEB_ROADMAP_EXECUTION_REPORT.md). Use
[current status](web-status.md) for evidence scope; file discovery never
promotes compatibility.
