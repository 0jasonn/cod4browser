# Web product roadmap

Updated 2026-09-04. Target: full offline single-player fidelity with the
original Steam COD4 (2007). Multiplayer remains out of scope. Canonical Kisak
owns gameplay and assets; the dedicated Worker, synchronous filesystem adapter
and WebGL2 backend remain the platform architecture.

Completed optimization chronology is in
[Git history](history/README.md). Paused benchmarks do not
qualify active-campaign performance. See [current status](web-status.md) and
[the campaign evidence ledger](campaign-compatibility.md).

## Current executable milestones

Implementation proceeds independently of campaign completion. The user owns
manual gameplay, original/native comparisons and mission completion acceptance;
all remain unverified unless explicitly recorded from their observations. The
current implementation order is text input, functional graphics controls,
demonstrated renderer gaps, audio/cinematics, recovery/XFile validation,
distribution boundaries, then measured performance work. No automated mission
routes or objective/progression success are part of these engineering checks.

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
  Compare saved-screen/text presentation and point lighting with Steam. The
  transient spot-shadow and screen-scissor paths now follow native lifecycle,
  selection and bounds. BSP/static-model receiver volumes and DObj attachment
  exclusions now share native rules. Rigid DObj/FX/DynEntity model receivers use
  native spheres, brushes use canonical writable bounds, and animated DObjs
  use selected posed-bone bounds. All eligible receiver families now enter one
  globally sorted list using native's reverse material/surface key. Dynamic
  DObj/DynEntity spheres and brush boxes now require a portal-visible camera
  cell and native plane acceptance before expansion; animated DObjs also retest
  selected posed-bone boxes before skinning. Linked DObjs and scene brushes
  now consume native per-cell portal planes, with canonical cell-bit updates
  on link/unlink and snapshot initialization. DObj lighting follows pose
  rejection, and DynEntity draw construction precedes physics while mark
  expansion follows it, matching native frame order. DynEntity link/unlink,
  bank clearing and full per-cell plane admission now share bounded native/web
  helpers and consume canonical world visibility bytes. Animated DObjs now
  test current selected-bone boxes against the full cell planes and exact BSP
  cell membership before skinning, preserving native pose-use flags. Model/brush
  transient receiver keys now share native material-field construction instead
  of inheriting instance light/probe fields from camera ordering. Transient
  caster selection now follows native's separate family rules: BSP uses exact
  shifted light planes without camera DPVS, static models reuse the camera/light
  receiver mask, and dynamic models/brushes reuse exact receiver predicates and
  the static/entity child shadow dvars. Sun and authored primary-spot paths stay
  independent. Qualify equal-key
  object-ID ties and overlapping authored materials against the reference.
  Placement-only deformed FX/DynEntity surfaces now follow native authored-vertex
  placement; compare encountered effects visually. Particle-cloud dimensions,
  view-axis signs and projected stretching now share/track native policy with
  differential checks. Recognized soft-particle passes now use native FloatZ
  ownership and authored fade/fog/falloff/eye-offset constants; qualify matched
  scene output. The inspected distortion pass now uses the native post-lighting
  scene snapshot, projected basis and foreground-depth rejection; qualify
  authored effects and remaining variants. Authored outdoor particle-cloud
  materials now use canonical world lookup data with native height-mask and
  sampler behavior. Cloud centers now use the native 8x8x16 cell arithmetic,
  sample count and renderer-registration lifecycle; qualify an encountered
  scene against Steam.
  The audited dynamic material
  alias call belongs to `UI_MapLoadInfo`, which native `UI_Init` invokes only
  outside fastfile mode. Browser fastfile materials already come from the DB;
  a loose-file alias registry is unsupported and is not an active SP defect.
- **Cinematics:** the [source-built codec](cinematic-codec.md) now implements
  playback behind `R_Cinematic_*` using imported movies. Chrome playback,
  audio start, skip, pause/resume and natural completion pass
  ([evidence](evidence/cinematics-2026-09-02.md)). Compare synchronization,
  colour, authored in-world surfaces and transitions against Steam. Canonical
  planar code images now reach world/brush, static-model, DObj and UI draws;
  synthetic colour/alpha/filtering and recovery checks pass without a 2D scene.
  This does not qualify an authored TV scene. The decoder
  exceeds the previous production size baseline; that gate remains unchanged.
- **Audio:** both canonical EQ stages now reach the browser device
  ([evidence](evidence/browser-eq-2026-09-02.md)). Qualify authored/default EQ
  activation and filter-update transients against Steam. Room reverb/wet sends
  now use the [native DSP component](browser-reverb.md) in an AudioWorklet;
  native/Wasm impulses and browser routing checks pass. Qualify authored
  room changes and callback cost. Source position and processed buffers now
  come from Web Audio device time through bounded Worker feedback; delayed
  delivery and context suspension no longer advance a separate proxy clock.
  Synthetic native/Wasm, Node and served Chromium checks pass. Movie video
  follows cumulative played PCM with one pending decoded frame; owned Killhouse
  delay, audio suspension and WebGL recovery pass. Qualify long/background
  stalls, arbitrary audio tails and hardware output latency. Compare dialogue, music, positional audio and authored
  effects during gameplay, pause and cinematics.
- **Menus/platform:** prove each shipped setting changes its visible or
  functional output. Browser character input now reaches native console/profile
  fields. A focused browser text sink now permits composition while physical
  keys and pointer lock remain canonical; trusted paste reaches `Field_Paste`.
  Real Windows IME candidate UI, non-Western code pages, arbitrary clipboard
  reads and localized glyphs remain open.
  Anisotropy/filtering now use shared native sampler policy, and existing
  normal/specular/detail shader flags consume their dvars. Filtering, normal
  and detail pixel checks plus context recovery pass. Shared native picmip
  policy selects authored texture levels before upload; synthetic residency
  and recreation checks, plus owned Killhouse production restart/persistence,
  pass. Mip bias now affects implicit sampling and explicit reflection LOD,
  and filtering changes also reach cubemaps, volume lighting and water.
  Paused owned-scene specular/normal toggles change pixels and restore the
  baseline exactly; context recovery preserves their settings. Canonical
  technique sets now use native feature-token remapping after publication and
  on relevant dvar changes; an owned Killhouse load selects available variants
  and responds to `r_normal` without gameplay input. The shipped Texture
  Settings menu now applies Manual/Normal through its native `r_applyPicmip`
  action, performs a renderer restart, reduces owned Killhouse decoded texture
  residency, and persists across a new runtime. Unknown shader families,
  multipass semantics and native/Steam visual comparison remain open.
  Quit now reaches durable shutdown and restart. Save metadata,
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
- **Campaign breadth (manual acceptance):** qualify the remaining
  missions, special mechanics and difficulty-dependent behavior. Preserve the
  existing evidence levels: compile, boot, render, functional, playable,
  mission-complete and retail fidelity are separate claims.
- **Durability/performance:** measure current foreground production gameplay,
  combat, movies, repeated transitions and long sessions on named hardware.
  Record frame distributions/stalls, responsiveness, game/wall time, Wasm/GPU/
  renderer/audio memory and scheduling, context recovery and fresh-browser
  save/profile restoration. Optimize measured bottlenecks.
- **Imported data:** the active XFile fixture now runs 328 bounded mutations
  across block allocation, aliases, dependency order and explicit pool cleanup;
  Win32/Wasm traces match with engine assertions enabled. The absent-block
  allocation failure is repaired. The actual 32-bit native `db_load.cpp`
  RawFile routine and the adapted Win32/Wasm routine now pass one shared
  inline/insert/alias publication and stream-coordinate contract. Extend that
  independent comparison to more asset families and arbitrary alias graphs,
  and extend failure rollback through the remaining non-world device side effects. Real DB string
  lifetimes now preserve shared zone owners and live zone-0 defaults through
  unload, with native/Wasm final-owner retirement checks. Automatic
  RawFile override/pool rollback now passes 80 failures/retries with the real
  coordinator, PMem and Worker DB scheduler; same-flag zones survive and
  reused indices free in allocation order. Forty-three post-publication
  failures restore the previous ClipMap, ComWorld, GameWorldSp or GfxWorld
  singleton; rejected GfxWorld/ComWorld replacements leave their unload hooks
  untouched, and successful replacements retire the prior owner once. A
  bounded two-fastfile request retires both new zones atomically when its second
  file fails. Broader request graphs remain to qualify. Diagnostic trace flags
  no longer own load failure.
  Image resource handles now restore same-name override bytes
  across 40 failed loads, preserve default copies and retire unused sources;
  the 256 MiB cap rejects admission before reading/copying payload. Qualify
  broader authored-image transitions and pressure. Do not build a replacement
  loader. Native error cleanup now returns recoverable
  mounted-runtime failures to the canonical UI and permits another map load;
  qualify broader malformed-load/non-world device rollback and repeated error/transition
  paths next.
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
