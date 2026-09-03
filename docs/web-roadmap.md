# Web product roadmap

Updated 2026-09-03. Target: full offline single-player fidelity with the
original Steam COD4 (2007). Multiplayer remains out of scope. Canonical Kisak
owns gameplay and assets; the dedicated Worker, synchronous filesystem adapter
and WebGL2 backend remain the platform architecture.

Completed optimization chronology is in
[history](history/web-roadmap-through-2026-09-02.md). Paused benchmarks do not
qualify active-campaign performance. See [current status](web-status.md) and
[the campaign evidence ledger](campaign-compatibility.md).

## Current executable milestones

1. **A complete first campaign chapter.** Use shipped New Game through
   Killhouse, CargoShip and the authored following transition. Record natural
   objectives, scripted sequences, checkpoints, death/restart, a fresh-browser
   Continue and mission completion. Use ordinary gameplay; no routes,
   waypoints, replay/progression substitutes or injected objective success.
   Rifle/timed-shooting/sidearm training, checkpoints 1–3 and fresh-browser
   Resume now pass; Save and Quit also returns safely to the main menu
   ([evidence](evidence/campaign-training-disconnect-2026-09-02.md)). The reached
   Captain Price/ladder platform is the extent of that gameplay evidence;
   course completion and the remaining chapter flow still require ordinary
   gameplay verification. Automated route or progression substitutes remain
   out of scope.
2. **Three-way reference.** Compare original Steam -> native Kisak -> browser
   Kisak. The [owned installation inventory](evidence/steam-reference-2026-09-02.json)
   pins hashes and configured settings. Native SP now builds and enters menu
   code ([evidence](evidence/native-reference-2026-09-02.md)). Finish its visual
   qualification and record
   difficulty/settings in all three runtimes before attributing differences.

Supporting repairs must address an observed engine/platform defect or a named
fidelity gap. Synthetic regression cases qualify those boundaries; campaign
acceptance requires the original mission scripts to drive the real game.
Completed verification and renderer repairs are recorded in the status and
convergence pages rather than counted as remaining product milestones here.

## Remaining player-visible gaps and acceptance

- **Rendering:** validate encountered material passes, transparency, lighting
  and special vision against the original. AC130 now renders and has inspected
  captures ([evidence](evidence/ac130-rendering-2026-09-03.md)); compare its
  thermal and cloud passes against matching Steam/native scenes. Keep all state in
  canonical assets and FX, and translate only at the renderer boundary.
  Compare saved-screen/text presentation and point lighting with Steam; finish
  transient spot shadows and native receiver/scissor selection.
- **Cinematics:** the [source-built codec](cinematic-codec.md) now implements
  playback behind `R_Cinematic_*` using imported movies. Chrome playback,
  audio start, skip, pause/resume and natural completion pass
  ([evidence](evidence/cinematics-2026-09-02.md)). Compare synchronization,
  color, in-world materials and authored transitions against Steam. The decoder
  exceeds the previous production size baseline; that gate remains unchanged.
- **Audio:** both canonical EQ stages now reach the browser device
  ([evidence](evidence/browser-eq-2026-09-02.md)). Qualify authored/default EQ
  activation and filter-update transients against Steam. Room reverb/wet sends
  now use the [native DSP component](browser-reverb.md) in an AudioWorklet;
  native/Wasm impulses and browser routing checks pass. Qualify authored
  room changes and callback cost. Compare dialogue, music, positional audio and authored
  effects during gameplay, pause and cinematics.
- **Menus/platform:** prove each shipped setting changes its visible or
  functional output. Quit now reaches durable shutdown and restart. Save metadata,
  list bounds, screenshot capture, durable JPEG completion and raw-image upload
  now work for an owned Airplane save ([evidence](evidence/save-thumbnails-2026-09-02.md)).
  Start-level capture now waits for a matching frame, and native raw-image
  loading passes a device test ([evidence](evidence/save-startup-native-2026-09-02.md)).
  Compare authored checkpoint/menu presentation with Steam and the native game.
  Localized profile/DB paths now pass French/German synthetic import and reload;
  qualify owned localized inventories, glyphs and dialogue
  ([evidence](evidence/localized-imports-2026-09-02.md)). Gamma now reaches the
  final pass and the shipped slider visibly changes menus/worlds
  ([evidence](evidence/display-gamma-2026-09-03.md)); compare display response
  with Steam. Resolution Apply, persistence and native in-game save/restart/load
  now pass; monitor refresh remains browser-controlled
  ([evidence](evidence/display-options-2026-09-03.md)). Qualify remaining shipped
  graphics controls and native/Steam display behavior.
- **Campaign breadth:** after the first completed chapter, qualify the remaining
  missions, special mechanics and difficulty-dependent behavior. Preserve the
  existing evidence levels: compile, boot, render, functional, playable,
  mission-complete and retail fidelity are separate claims.
- **Durability/performance:** measure current foreground production gameplay,
  combat, movies, repeated transitions and long sessions on named hardware.
  Record frame distributions/stalls, responsiveness, game/wall time, Wasm/GPU/
  renderer/audio memory and scheduling, context recovery and fresh-browser
  save/profile restoration. Optimize measured bottlenecks.
- **Imported data:** extend bounded synthetic fuzz inputs into canonical XFile
  block allocation, aliases, dependency order and atomic publication. Do not
  build a replacement loader.
- **Browser qualification:** Chrome/Edge evidence does not qualify Firefox or
  Safari. Test those separately. Gamepad requirements must follow the product
  and original-game reference; keyboard/mouse fidelity remains required.

## Working gates

Build with the pinned toolchain. Run focused checks during each change, then
the routine smoke and non-overlapping remainder before handoff; run production
checks for changed product boundaries. Run owned retail validation separately.
Exhaustive duplicate suites are useful only for a concrete remaining risk.

Every milestone must update the convergence inventory and evidence ledger.
Missing human gameplay or original/native reference evidence must be recorded
as unverified, with the exact next observation required. No campaign promotion
follows from a short window, unchanged draw counts or synthetic notifications.
