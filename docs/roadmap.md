# cod4browser runtime roadmap

## Product objective

Deliver a complete offline Call of Duty 4 single-player runtime in a modern
browser. The player supplies assets from a legally owned installation; the
repository, build, tests, and hosted application must never contain or fetch
proprietary game data.

The target flow is:

```text
browser launcher
  -> import and persist a legal COD4 installation
  -> canonical Kisak filesystem and database
  -> single-player client/server/game startup
  -> mission/map load and player spawn
  -> movement, aiming, weapons, AI, scripts, and mission progression
  -> saves/checkpoints and a complete offline gameplay loop
```

This is a runtime roadmap, not a rendering wish list. Work is selected by the
highest-value blocker in the canonical gameplay flow.

## Architectural invariants

- Upstream KisakCOD/IW3 ownership is canonical. Browser code owns platform
  integration, not game state, entities, weapons, scripts, or server state.
- Runtime flow remains `input -> client -> server/game -> cgame -> renderer`.
- Rendering remains `COD4 game -> Kisak renderer frontend -> portable backend
  commands -> WebGL2`.
- `XModel`, `Material`, `GfxWorld`, FX definitions, sound aliases, and other
  engine assets retain their canonical identities to a genuine platform seam.
- Browser asynchrony stays at filesystem, launcher, audio-policy, and frame-pump
  boundaries. Portable engine operations remain synchronous-looking.
- Gate 2 and other frozen oracles are diagnostic evidence only. Production
  runtime behavior must never execute through them.
- Do not add a browser world representation, debug camera, fake scene, fake
  entities, hard-coded Killhouse behavior, or a duplicate gameplay/FX/audio
  system.
- Preserve GPL-3.0 and legal asset boundaries described in `AGENTS.md`.

Any task that cannot satisfy these invariants stops for architectural review.

## Current runtime boundary

Baseline: `f706d307` (canonical collision/common-world shutdown before web
asset release). The accepted runtime history also includes `d671f4e7` for
freeFlags unload/mark/promotion/default/removal and zone compaction,
`ec9b3ddd` for malformed SndCurve body repair at publication, and `ac9c7682`
for stable interned curve names plus empty identity -> canonical `default`.
Sound-frame ownership is completed by `f7de5a7f` (canonical sound update during
active cgame frames) and `538b3b5a` (the exact active/non-fullscreen/non-skipped
frame predicate without the `cls.uiStarted` requirement).

The browser production target currently compiles and runs the canonical
single-player filesystem, database, startup-zone loading, map loading,
`GfxWorld`, collision, server/game initialization, script VM, cgame, FX core,
and sound mixer/OpenAL path. Killhouse reaches a game-driven frame with real
world surfaces, static models, canonical ordinary and first-person DObjs, HUD,
movement, mouse look, ADS, and textured/lightmapped presentation. The current
renderer boundary is `game -> cgame -> Kisak renderer frontend -> portable
WebGL2`; no browser-owned entity, weapon, or camera state has been added.

The gameplay event chain is farther along than its presentation:

- `FireWeapon` and server events are canonical game owners.
- `CG_FireWeapon` applies recoil, chooses weapon-owned muzzle/brass effects,
  and selects canonical fire sound aliases.
- bullet-hit cgame events resolve the DB-owned impact table and spawn canonical
  impact FX/sounds.
- EffectsCore updates effects and generates canonical packed code-mesh
  vertices/indices.
- `R_AddCodeMeshDrawSurf` now converts those canonical batches at the renderer
  platform seam and WebGL2 retains their real Material/image, packed UVs,
  vertex RGBA, order, alpha blend, and depth state. This closes the prior
  discarded-geometry boundary; a retail run must still prove the expected
  muzzle/impact definitions are spawned and visible.
- `R_FilterXModelIntoScene` now retains canonical rigid XModel placements for
  ejected-brass/debris-style FX elements. The renderer selects a deterministic
  view-distance LOD, transforms the canonical XSurface once, and appends
  `FxXModel` batches between dynamic DObjs and code meshes. Invalid, deformed,
  over-limit, or allocation-failed optional model effects are bounded drops;
  they cannot abort the frame, displace canonical code meshes, or leave stale
  backend scene data. Skinned/deformed FX models remain a compatibility gap.
- `R_AddParticleCloudToScene` now retains 256 independent canonical cloud
  slots instead of returning one overwritten singleton. At the renderer seam,
  each admitted cloud expands the native 8x8x16 particle field into one
  complete `FxParticleCloud` batch after DObjs, FX XModels, and code meshes.
  Canonical emissive Material/image/state, packed color, view-facing radii,
  placement, UVs, and deterministic order reach WebGL2; invalid or over-budget
  clouds drop atomically. The browser uses deterministic platform-local jitter
  rather than native CRT `rand`, and preserves both explicit radii where the
  unavailable native shader interaction leaves a compatibility difference.
- Commit `748112cc` admits ordinary entity DObjs through the same fixed native
  512-entry scene array as first-person DObjs. Commit `de695b46` closes the
  renderer compatibility gap exposed by that admission: dynamic DObjs select
  their canonical `XModelGetLodForDist` result from cpose/view origins rather
  than forcing every model through LOD 0. Local Release Chrome then records 64
  DObjs, 102 models, 147 surfaces, 16,435 vertices, and 33,672 indices in the
  first dynamic scene, with a visibly posed Gaz actor and continued scene
  submission. The focused policy test covers canonical delegation, distance,
  invalid inputs, and the highest-LOD fallback.
- canonical sound assets, aliases, 53-channel selection, playback IDs,
  attenuation, pitch/volume, and LoadedSound PCM remain Worker-owned. A
  KISAK_WEB OpenAL-compatible proxy now transfers bounded PCM/device commands
  to a main-thread Web Audio owner, which handles AudioContext policy and
  AudioNode resources. Lifecycle hardening covers gesture resume, pause/stop,
  source generation reuse, natural completion, buffer replacement, sound
  shutdown/re-init, and host disposal. Loaded PCM16 mono/stereo is supported;
  streaming and reverb remain later compatibility work. Production Release
  Chrome now proves the console-triggered canonical
  `snd_playLocal weap_g36c_fire_plr` path through PCM upload, source start, and
  `source=canonical-openal-web-audio`. After canonical pickup and real Mouse1,
  `weap_g36c_fire_plr` reaches the same started source path; repeated shots
  also produce `bullet_large_wood` impact starts. A real `R` reload now starts
  `weap_g36_lift_plr`, `weap_g36_clipout_plr`, and `weap_g36_clipin_plr`.
- fresh browser profiles now install `r -> +reload`, wheel-up -> `weapnext`,
  and wheel-down -> `weapprev` only when each canonical key is unbound. DOM
  events still flow through the Worker queue, `IN_Frame`, `CL_KeyEvent`, and
  the native command/state machines. Native/Wasm tests prove deterministic
  defaults and preservation of custom bindings; a focused browser test proves
  the key pulses cross the host boundary. The canonical two-weapon selection
  path is now proven with a server-owned `cmd give` fixture (not a browser
  inventory or a claim of physical rack-pickup parity): G36C index 5 and
  Winchester 1200 index 10 both enter the owned inventory. Wheel-up selects
  G36C with `owned=1` while predicted/viewmodel state is Winchester; wheel-down
  selects Winchester with `owned=1` while predicted/viewmodel state is G36C.
  Screenshots confirm distinct canonical viewmodels. `weap_raise_plr_layer`
  starts on transition, and Winchester reload uses
  `weap_winch1200_loop_plr`/`weap_winch1200_pump_plr`.
- `e57331ba` corrected the FX archive callback ABI for Wasm, so archive names
  and canonical keys are serialized from the actual `XAssetHeader` rather than
  an incompatible aggregate-by-value callback. The subsequent lifecycle fixes
  are `d671f4e7`, `ec9b3ddd`, `ac9c7682`, and `f706d307` as listed above.
  Production Release Chrome now completes fresh `map killhouse`, a second
  `map killhouse` at `resourceGeneration=5`, `loadgame autosave/killhouse`,
  and a third `map killhouse` at `resourceGeneration=7` without aborting;
  each reaches canonical cgame/refdef with 8064 surfaces, 431747 vertices,
  and 793188 indices. After restart, F equips the real G36C with viewhands and
  HUD, movement changes the player origin, mouse input changes `viewForward`,
  five fire inputs retain `FxCodeMesh` (4 batches/40 vertices/60 indices,
  `,gfx_smk_white_atlas`) and `FxXModel` (`fx_wood_splinter01`,
  `mc/mtl_fx_wood_splinter`), and `R` visibly enters the canonical reload
  animation and the later reload trace records the canonical reload audio
  aliases above. The gameplay-fire trace records three real
  `weap_g36c_fire_plr` starts with snapshot clip deltas 27->26, 26->25, and
  25->24, plus three `bullet_large_wood` starts. The retained FX evidence
  includes `FxCodeMesh` (4 batches/28 vertices/42 indices,
  `,gfx_smk_white_atlas`) and `FxXModel` (`fx_wood_splinter02`).
  The bounded gameplay-fire evidence added in `a423ae21` records the selected
  WeaponDef alias and authoritative event/snapshot ammo values without owning
  gameplay state in the browser.

  A Winchester fire event also selects `weap_winch1200_fire_plr` with
  snapshot clip 7 -> event clip 6 and produces metal impact audio/FX; the
  fire-alias playback itself was not retained, so that result is not claimed
  as a started weapon-fire source.

The ordinary-entity renderer closure is now also proven. After `748112cc` and
`de695b46`, local Release Chrome records a first dynamic scene of 64 DObjs,
102 models, 147 surfaces, 16,435 vertices, and 33,672 indices; a posed Gaz
actor is visible and scene submission continues. The same run retains
`FxCodeMesh` (4 batches/44 vertices/66 indices) and `FxXModel` (5 batches/188
vertices/234 indices). Canonical target damage is proven against entity 244,
`actor_ally_hero_gaz_sas_woodland`: four authoritative health writes record
`100000000->99999915->99999830->99999778->99999726`. This proves damage
application, not enemy death, AI reaction, or script notification.

Therefore the active boundary is **canonical ordinary/first-person DObj
rendering, gameplay fire with authoritative ammo and target-damage deltas,
gameplay-selected fire and impact audio, retained muzzle/impact FX reaching
WebGL2, manual reload presentation/audio, and canonical two-weapon switching**.
Physical rack pickup parity, automatic reload semantics, continuous pointer
lock, enemy reaction/death/AI notification, natural rack traversal, and
campaign-map breadth remain unproven.

### Campaign variance boundary

The first `map cargoship` probe did not reach campaign asset parsing. It
stopped at the collision singleton assertion, but the bounded database trace
added in `10b99bad` and regression-covered in `607f5650` identified the
earlier causal boundary: opening `zone/english/cargoship.ff` failed in the
platform filesystem. The retained DB asset evidence still described
`common.ff`, so the assertion was a downstream symptom rather than proof of a
campaign `ClipMap` incompatibility.

The canonical singleton corrections remain accepted reusable work:
`55fc1282` restores `clipMap == &cm` lifecycle ownership and `7144b7d4`
rebinds aliased `ClipMap` publication to that singleton. Direct Wasm DB-stream
coverage passes those changes. They do not substitute for loading the missing
campaign fastfile.

Commit `acc5aa26` closes the importer discovery gap without a map manifest: a
selected installation now contributes every supported single-player `.ff`
directly under `zone/english`, while multiplayer and unrelated/nested files
remain excluded. The same size, path, count, fastfile probe, OPFS generation,
and atomic replacement rules apply. The currently reopened Chrome generation
is still the earlier 26-file profile (21 IWDs and four fastfiles: startup zones
plus Killhouse), so Cargoship remains unverified until a legal installation is
re-imported through the expanded importer. Commit `9530656b` exposes the
existing standards-based `webkitdirectory` boundary as an explicit compatible
picker while retaining File System Access as the primary action. Both actions
share one import orchestration path and therefore the same retained-generation
cancellation/failure behavior. All 19 focused asset-store browser tests pass,
including native-picker priority, explicit portable success/cancellation, SP
fastfile discovery, malformed-input rejection, and atomic rollback; the
production Release build and runtime-prefix guard also pass. Local Chrome
shows the new action and opens its file chooser. On that same rebuilt Release
site, a fresh `map killhouse` reaches `G_LoadLevel`, `CG_Init`, and the
game-driven frame; WebGL2 draws 8,064 world surfaces, 431,747 vertices, and
793,188 indices, while the dynamic scene retains 64 DObjs, 102 models, and 151
surfaces. The prior black-screen failure is not present. The attached Chrome
verification extension currently rejects setting the installation directory
until its file-URL access permission is enabled. The persisted browser
generation therefore remains the earlier Killhouse-only profile, and
rerunning Cargoship is the next integration action once that local permission
is changed or the folder is selected manually.

### Restart lifecycle boundary

The restart boundary is now converged through `f706d307`: web
`Com_Restart` shuts down `ComWorld` and collision before
`DB_ReleaseXAssets`, preserving the native owner/removal-hook order. The
accepted DB/SndCurve lifecycle fixes above preserve stable primary identity,
valid default bodies, dependency marking, and replacement zone ownership.
The production Release Chrome sequence documented above proves fresh map,
repeat map, loadgame, and a third map remain non-aborted. No browser-only
historical-freeFlags ownership classification is retained.

## Complete milestone: playable Killhouse / F.N.G. combat loop

Acceptance requires the following to work through canonical ownership in a
retail-asset browser run:

| Capability | Current reading | Owning subsystem | Next proof or closure |
| --- | --- | --- | --- |
| Spawn | Reached on the F.N.G. training runtime | game/server | Retain regression evidence; prove on campaign maps |
| Move | Reached | input/client/game | Retain focused browser proof |
| Aim / ADS | Reached | input/cgame | Retain focused browser proof |
| Fire and ammo consumption | Real Mouse1 reaches canonical fire; event ammo is stable and snapshot clip deltas are 30->29, 27->26, 26->25, and 25->24 across the validated shots | game/cgame | Retain recoil/frame evidence; broaden campaign coverage |
| Muzzle flash / brass | Real fire retains canonical `FxCodeMesh` and `FxXModel` batches with `,gfx_smk_white_atlas`, `fx_wood_splinter02`, and exact portable counts | cgame/FX/renderer | Prove broader weapon presentation and remaining brass/deformed FX cases |
| Bullet impact | Real fire reaches canonical `bullet_large_wood` audio starts and retained impact FX batches through the cgame/EffectsCore/renderer path | game/cgame/FX/renderer | Prove surface-dependent breadth |
| Ordinary entity DObjs | `748112cc` admits canonical ordinary DObjs; `de695b46` selects distance-based canonical XModel LODs. Chrome records 64 DObjs / 102 models / 147 surfaces and visibly posed Gaz | renderer frontend/xanim | Retain on F.N.G.; add broader entity/material coverage |
| Smoke / particle clouds | Canonical EffectsCore cloud slots and portable batches implemented; retail visibility proof pending | FX/renderer | Observe a real cloud effect and measure CPU expansion before broad performance work |
| Weapon sound | Real Mouse1 starts `weap_g36c_fire_plr` through canonical OpenAL/WebAudio; three `bullet_large_wood` impact starts are also recorded | cgame/audio/platform | Prove broader alias families; streaming/reverb remain later |
| Reload | `R` visibly enters the canonical reload animation and starts `weap_g36_lift_plr`, `weap_g36_clipout_plr`, and `weap_g36_clipin_plr`; post-reload ammo refill and automatic reload remain unproven | input/game/cgame/audio | Prove automatic reload and exact post-reload ammo/viewmodel state |
| Weapon switching | Canonical wheel-up/down both select owned G36C index 5 and Winchester 1200 index 10 through cgame, with distinct viewmodels and `weap_raise_plr_layer`; fixture uses server-owned `cmd give`, so rack pickup remains unproven | input/game/cgame | Prove physical mission/rack pickup and broader inventory transitions |
| Basic combat interaction | Four real `G_Damage` health writes against entity 244 Gaz are proven; enemy death/reaction/AI notification remain unproven | game/script/cgame | Prove reaction/death/script notification against real entities; no synthetic browser targets |

### Ordered work queue

1. **FX code-mesh rendering** — implemented in `41c6c8a5` and hardened in
   `b5d2c76e`. Canonical EffectsCore batches cross the renderer seam with
   deterministic material/image/UV/RGBA/order/depth/blend state and bounded,
   failure-atomic retention. Retail muzzle/impact observation remains part of
   the integration proof, not a second renderer implementation.
2. **Weapon audio closure** — platform ownership selected and loaded-sound
   bridge implemented in `38ffcc88`, then lifecycle/timing hardened in
   `7b02d1b0`; active-frame sound ownership was fixed in `f7de5a7f` and
   `538b3b5a`. Native x64, direct Wasm, focused browser bridge, exact boot,
   qcommon lifecycle, and console-triggered `weap_g36c_fire_plr` source-start
   checks pass. Real Mouse1 now proves the gameplay-selected fire and impact
   aliases; broader sound families remain.
3. **Rigid FX XModel rendering** — implemented in `5d49dbe1`, compatibility
   hardened in `86c2efbb` and `29f49b09`, and made assertion-authoritative in
   `2d3c9f10`. Canonical placements, XModel/XSurface/Material identities,
   deterministic LOD, transforms, ordering, and failure-atomic admission are
   covered in native x64 and direct Wasm tests. Retail brass/debris visibility
   remains part of integration proof; deformed/skinned effects are not faked.
4. **Particle-cloud rendering** — implemented in `cedc0cf2` and hardened in
   `67d6dbe7`. Independent canonical slots expand into complete 1,024-quad
   batches with emissive material identity, view-dependent axes, deterministic
   jitter, atomic capacity admission, and assertion-enabled native/Wasm
   coverage. Retail smoke/cloud visibility and measured CPU cost remain.
5. **Reload and weapon switching** — default browser reachability implemented
   in `bf3dd93b` and regression-hardened in `b45df61e`. Native x64, direct Wasm,
   focused browser input, production build, and exact boot checks pass. Manual
   reload presentation/audio and canonical two-weapon wheel transitions are
   now proven with a server-owned fixture; physical rack pickup, automatic
   reload, and exact post-reload state remain.
6. **Enemy interaction closure** — damage application is proven by the four
   canonical health deltas above; prove reaction, death, and script/AI
   notification against real map entities.
7. **Campaign variance probe** — load `cargoship`, record the first blocker by
   subsystem, fix the narrowest reusable runtime gap, and repeat the same
   acceptance set without introducing map-specific behavior.

After every completed item, re-audit the runtime rather than assuming the next
listed item is still the highest-value blocker.

## Architecture decision: browser audio ownership

Measured on the pinned toolchain and bundled Playwright Chromium:

- production Wasm executes in `engine_worker.mjs` with OffscreenCanvas and the
  synchronous Worker filesystem;
- a dedicated Worker reports `typeof AudioContext === "undefined"` and has no
  DOM `document`;
- Emscripten's OpenAL implementation constructs Web Audio objects in the
  calling realm and its autoplay helper registers events on `document`;
- consequently, the current Worker OpenAL driver cannot initialize or resume a
  browser audio context. This is not a missing weapon alias or payload bug.

Decision recorded after review:

1. **Main-thread Web Audio command bridge (selected).** Keep Kisak Wasm,
   canonical `SND_*` alias/channel/mixer state, filesystem, and rendering in the
   Worker. Replace only the browser audio-driver boundary with a typed,
   deterministic command stream to a main-thread Web Audio owner. The launcher
   creates/resumes the context under a user gesture; the Worker retains
   playback IDs, channel selection, spatial calculations, and game timing.
   Buffer/source lifecycle acknowledgements and bounded queue behavior must be
   explicit and tested.
2. **Move Wasm execution to the main thread.** This would let Emscripten OpenAL
   access Web Audio directly, but conflicts with the selected synchronous
   Worker filesystem/OffscreenCanvas architecture and risks blocking the DOM
   event loop. Not recommended.
3. **AudioWorklet/shared-memory backend.** This offers tighter scheduling but
   introduces SharedArrayBuffer, cross-origin-isolation, deployment, and custom
   DSP/protocol complexity before measurements justify it. Not recommended for
   the first playable slice.

The selected design is implemented below the canonical OpenAL-facing driver.
The Worker proxy mirrors only driver-visible device state and emits versioned
FIFO commands; it does not own aliases, game channels, mixing, spatial
calculations, or gameplay timing. Main-thread resources are bounded by 53
source IDs, 512 buffer IDs, and a 16 MiB per-upload validation limit. The
generated Worker module contains no Emscripten `AudioContext` or DOM access.

Accepted verification for the platform slice:

- native x64 and direct Wasm `web_openal_proxy_tests`;
- two focused `web_audio_driver.spec.mjs` browser tests, including a Worker PCM
  transfer;
- the exact WebGL2 boot smoke and two qcommon lifecycle browser tests;
- production Release build and `git diff --check`.

## Presentation milestone

Begin broad presentation work only after the combat loop is coherent. Prefer
features needed by real gameplay evidence in this order:

1. material technique and shader compatibility required by encountered assets;
2. alpha testing/blending and remaining image formats;
3. XModel material coverage and lightmap composition;
4. sky and fog;
5. general FX rendering: particles, smoke, explosions, decals/marks, trails,
   and environmental effects.

Perfect shader parity, shadows, reflections, and post-processing are explicitly
deferred. They do not outrank missing gameplay, audio, or effects.

Acceptance is recognizable COD4 presentation produced by real Materials,
models, lights, UI, FX, and audio across gameplay—not a polished asset viewer.

## Map and campaign expansion

Map expansion begins once Killhouse/F.N.G. combat is playable and is used to
discover general runtime assumptions.

1. F.N.G. training flow.
2. One additional multiplayer map as a renderer/asset/streaming variance probe.
3. One campaign test mission as the first mission-flow target.
4. Additional missions until campaign startup and progression are reliable.

For each new map, record:

- the first failing lifecycle stage;
- renderer assumptions exposed;
- missing asset families or material techniques;
- script/game/cgame differences;
- filesystem, memory, or streaming pressure;
- whether the fix is shared Kisak behavior, a narrow modification, or permanent
  platform code.

Campaign completion then proceeds through mission startup, loading screens,
cinematics or graceful browser-compatible replacement, checkpoints, save/load,
mission transitions, AI/script breadth, and full offline progression.

## Verification policy

Routine implementation work runs only:

- affected native tests;
- affected direct Wasm tests where the code is portable;
- one focused browser test for the changed runtime boundary when practical;
- the production web build when link/runtime closure is affected;
- `git diff --check` and a scoped diff review.

The smoke and non-overlapping browser remainder tiers run before ordinary
milestone handoff. Exhaustive browser suites, full CI, and fuzzing run at major
milestones or when their specific duplicate/malformed-input evidence is useful;
they are not repeated after every small change.

Retail assets may be used only as local manual/browser evidence. Automated
fixtures must remain synthetic or freely licensed with documented provenance.

## Decision gates requiring human review

Stop autonomous implementation when:

1. a new engine-wide architecture or subsystem redesign is required;
2. multiple materially valid platform designs remain after evidence gathering;
3. multiplayer becomes a goal;
4. a legal or licensing choice appears;
5. removal of major regression/oracle infrastructure is proposed;
6. threading, WebGPU, Asyncify, transport, storage, or another choice changes
   deployment or whole-engine architecture.

## Milestone ledger

| State | Milestone | Evidence / commits | Remaining blocker |
| --- | --- | --- | --- |
| Complete | Canonical filesystem and DB startup | See `docs/web-port-convergence.md` and history through `e652d43a` | Continue convergence; Gate 2 remains diagnostic only |
| Complete | Real Killhouse map/game/cgame frame | See canonical lifecycle and browser evidence through `e652d43a` | Presentation and gameplay feedback gaps |
| Complete | Textured/lightmapped world, static models, ordinary/weapon DObjs, HUD/input | `e652d43a`, `748112cc`, `de695b46` | Broader entity/material families |
| Complete | Canonical FX code-mesh renderer closure | `41c6c8a5`, `b5d2c76e` | Retail muzzle/impact visibility proof; marks/decals remain later FX families |
| Complete | Browser loaded-sound platform bridge | `38ffcc88`, `7b02d1b0`, `f7de5a7f`, `538b3b5a`; native/Wasm/browser lifecycle evidence | Broader retail aliases; streaming/reverb later |
| Complete | Gameplay fire/impact vertical slice | `a423ae21`; local Release Chrome ammo, fire/impact audio, and FX evidence | Target damage and broader combat interaction |
| Complete | Reload and weapon-cycle input reachability | `bf3dd93b`, `b45df61e`; native/Wasm/browser boundary evidence | Automatic reload/refill and retail weapon-switch presentation |
| Complete | Canonical two-weapon wheel switching | `4a48d861`; local Release Chrome server-owned `cmd give` fixture, both directions, distinct viewmodels, transition/reload audio | Physical rack pickup parity and broader inventory transitions |
| Complete | Canonical rigid FX XModel renderer closure | `5d49dbe1`, `86c2efbb`, `29f49b09`, `2d3c9f10`; assertion-enabled native x64/direct Wasm tests, production Release build, exact WebGL2 boot | Retail brass/debris visibility proof; deformed/skinned FX models, marks, and decals remain |
| Complete | Canonical particle-cloud renderer closure | `cedc0cf2`, `67d6dbe7`; assertion-enabled native x64/direct Wasm tests, production Release build, exact WebGL2 boot | Retail smoke/cloud visibility and performance proof; marks/decals remain |
| Complete | Playable Killhouse/F.N.G. combat loop | `748112cc`, `de695b46`, `839be67d`; Release Chrome proves posed Gaz, fire/audio/FX, and four authoritative Gaz health decrements | Natural rack traversal, automatic reload/refill, enemy reaction/death/AI notification |
| Pending | Recognizable COD4 presentation | — | Materials, remaining images, FX breadth, sky/fog |
| Active | Multiple maps and first campaign mission | Next probe is `cargoship` campaign variance | Renderer assumptions, asset families, script differences, streaming |
| Pending | Offline campaign runtime | — | Mission flow, saves/checkpoints, cinematics, breadth and performance |

Update this ledger and the current runtime boundary after every major milestone.
Record implementation commit hashes, architectural decisions, exact tests, and
the next autonomous objective. Keep detailed system classification in
`docs/web-port-convergence.md`; keep this file focused on product sequence and
engineering decisions.
