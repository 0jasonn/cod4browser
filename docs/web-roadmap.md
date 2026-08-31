# Web product roadmap

Updated 2026-08-31. This is the single active roadmap; earlier orders and
mission-progression gates are superseded. Mission playing, progression
validation, route generation, and campaign expansion are outside this task.
Manual gameplay assessment can guide renderer work without a progression gate.

1. Canonical static-camera DPVS now computes and filters camera instances,
   preserving shadow data and canonical identity across light groups/LODs.
   The [visibility record](evidence/static-camera-visibility-2026-08-31.md)
   separates synthetic execution from browser/retail verification. World-surface
   filtering requires a separate batch-boundary change and stays deferred.
2. Preserve canonical asset/game/renderer ownership, imported-asset validation,
   and durable filesystem shutdown. Small shared material lookups and Worker
   transport bookkeeping must not absorb technique selection or host-specific
   filesystem recovery policy.
3. Use the five new diagnostic DObj build/substage timings to distinguish pose,
   lighting, skinning, and geometry construction before considering caches.
   No fresh capture or speedup is claimed. Keep any future measurement short
   and question-specific; mission automation remains outside this work.

This continuation uses one focused existing test invocation and one final
incremental production Release build. Diagnostic interfaces are unchanged;
no diagnostic build, broad tiers, retail capture or mission/lifecycle checks.
Escape, renderer polish and further DObj optimization remain outside scope.

Historical plans and completed milestones remain in
[the earlier roadmap](history/web-roadmap-through-2026-08-28.md) and
[execution report](../WEB_ROADMAP_EXECUTION_REPORT.md). Use
[current status](web-status.md) for evidence scope; file discovery never
promotes compatibility.
