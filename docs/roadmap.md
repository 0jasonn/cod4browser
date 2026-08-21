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

Baseline: `67d6dbe7` (`Harden particle cloud material and axis
compatibility`), following the particle-cloud implementation commit
`cedc0cf2`.

The browser production target currently compiles and runs the canonical
single-player filesystem, database, startup-zone loading, map loading,
`GfxWorld`, collision, server/game initialization, script VM, cgame, FX core,
and sound mixer/OpenAL path. Killhouse reaches a game-driven frame with real
world surfaces, static models, the first-person weapon DObj, HUD, movement,
mouse look, ADS, and textured/lightmapped presentation.

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
- canonical sound assets, aliases, 53-channel selection, playback IDs,
  attenuation, pitch/volume, and LoadedSound PCM remain Worker-owned. A
  KISAK_WEB OpenAL-compatible proxy now transfers bounded PCM/device commands
  to a main-thread Web Audio owner, which handles AudioContext policy and
  AudioNode resources. Lifecycle hardening covers gesture resume, pause/stop,
  source generation reuse, natural completion, buffer replacement, sound
  shutdown/re-init, and host disposal. Loaded PCM16 mono/stereo is supported;
  streaming and reverb remain later compatibility work.
- fresh browser profiles now install `r -> +reload`, wheel-up -> `weapnext`,
  and wheel-down -> `weapprev` only when each canonical key is unbound. DOM
  events still flow through the Worker queue, `IN_Frame`, `CL_KeyEvent`, and
  the native command/state machines. Native/Wasm tests prove deterministic
  defaults and preservation of custom bindings; a focused browser test proves
  the key pulses cross the host boundary. Retail ammo/animation/viewmodel
  transition proof remains pending.

Therefore the active boundary is **canonical fire/impact events, sprite/beam
code meshes, rigid FX XModels, and particle clouds reaching WebGL2 plus a
verified canonical LoadedSound-to-Web-Audio device path, with a retail
fire/impact integration proof still required**.

## Active milestone: playable Killhouse / F.N.G. combat loop

Acceptance requires the following to work through canonical ownership in a
retail-asset browser run:

| Capability | Current reading | Owning subsystem | Next proof or closure |
| --- | --- | --- | --- |
| Spawn | Reached on Killhouse | game/server | Retain regression evidence; prove on F.N.G. |
| Move | Reached | input/client/game | Retain focused browser proof |
| Aim / ADS | Reached | input/cgame | Retain focused browser proof |
| Fire and ammo consumption | Canonical path reached; end-to-end behavior needs explicit evidence | game/cgame | Trace event, ammo delta, recoil, and frame continuity |
| Muzzle flash / brass | Canonical FX code-mesh and rigid FX XModel consumption implemented; retail visibility proof pending | cgame/FX/renderer | Observe real fire event, effect definition, retained sprite/model batches, and draws |
| Bullet impact | Canonical trace/event/impact-table and FX renderer paths present; end-to-end result unproven | game/cgame/FX/renderer | Prove surface-dependent impact FX; audio follows the platform decision |
| Smoke / particle clouds | Canonical EffectsCore cloud slots and portable batches implemented; retail visibility proof pending | FX/renderer | Observe a real cloud effect and measure CPU expansion before broad performance work |
| Weapon sound | Canonical loaded-sound bridge is implemented and lifecycle-tested; retail fire alias proof pending | cgame/audio/platform | Observe one real `WeaponDef` alias through channel selection, PCM upload, gesture-unlocked playback, and completion |
| Reload | Fresh profiles reach canonical `+reload`; retail state/animation/audio proof pending | input/game/cgame/audio | Exercise empty/partial reload and observe canonical ammo/state transitions |
| Weapon switching | Fresh profiles reach canonical `weapnext`/`weapprev`; retail presentation proof pending | input/game/cgame | Exercise both directions and observe canonical inventory/viewmodel transition |
| Basic combat interaction | Real bullet/game systems compiled; target damage/death/AI response unproven | game/script/cgame | Use real entities in F.N.G. or campaign content; no synthetic browser targets |

### Ordered work queue

1. **FX code-mesh rendering** — implemented in `41c6c8a5` and hardened in
   `b5d2c76e`. Canonical EffectsCore batches cross the renderer seam with
   deterministic material/image/UV/RGBA/order/depth/blend state and bounded,
   failure-atomic retention. Retail muzzle/impact observation remains part of
   the integration proof, not a second renderer implementation.
2. **Weapon audio closure** — platform ownership selected and loaded-sound
   bridge implemented in `38ffcc88`, then lifecycle/timing hardened in
   `7b02d1b0`. Native x64, direct Wasm, focused browser bridge, exact boot, and
   qcommon lifecycle checks pass. A retail run must still trace one real fire
   alias from `WeaponDef` through `SND_PlaySoundAlias` to audible playback.
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
5. **Fire/impact integration proof** — add focused browser observability that
   proves one trigger causes canonical server/cgame fire, recoil/ammo change,
   visible muzzle FX, a collision result, and an impact effect without owning
   any of those states in browser code.
6. **Reload and weapon switching** — default browser reachability implemented
   in `bf3dd93b` and regression-hardened in `b45df61e`. Native x64, direct Wasm,
   focused browser input, production build, and exact boot checks pass. Retail
   state, ammo, animation, viewmodel, and audio proof remains.
7. **Basic combat interaction** — prove damage, reaction, death, and script/AI
   notification against real map entities.
8. **F.N.G. parity pass** — load F.N.G., record the first blocker by subsystem,
   fix the narrowest reusable runtime gap, and repeat until the same combat
   acceptance set passes.

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
| Complete | Textured/lightmapped world, static models, weapon DObj, HUD/input | `e652d43a` | General entity draws and audio proof |
| Complete | Canonical FX code-mesh renderer closure | `41c6c8a5`, `b5d2c76e` | Retail muzzle/impact visibility proof; marks/decals remain later FX families |
| Complete | Browser loaded-sound platform bridge | `38ffcc88`, `7b02d1b0`; native/Wasm/browser lifecycle evidence | Retail weapon/impact alias proof; streaming/reverb later |
| Complete | Reload and weapon-cycle input reachability | `bf3dd93b`, `b45df61e`; native/Wasm/browser boundary evidence | Retail state/animation/viewmodel/audio proof |
| Complete | Canonical rigid FX XModel renderer closure | `5d49dbe1`, `86c2efbb`, `29f49b09`, `2d3c9f10`; assertion-enabled native x64/direct Wasm tests, production Release build, exact WebGL2 boot | Retail brass/debris visibility proof; deformed/skinned FX models, marks, and decals remain |
| Complete | Canonical particle-cloud renderer closure | `cedc0cf2`, `67d6dbe7`; assertion-enabled native x64/direct Wasm tests, production Release build, exact WebGL2 boot | Retail smoke/cloud visibility and performance proof; marks/decals remain |
| Active | Playable Killhouse/F.N.G. combat loop | FX renderer, loaded-sound device, and combat-input closures accepted | Retail fire/impact/reload/switch proof and combat interaction |
| Pending | Recognizable COD4 presentation | — | Materials, remaining images, FX breadth, sky/fog |
| Pending | Multiple maps and first campaign mission | — | Unknown until F.N.G./campaign probes |
| Pending | Offline campaign runtime | — | Mission flow, saves/checkpoints, cinematics, breadth and performance |

Update this ledger and the current runtime boundary after every major milestone.
Record implementation commit hashes, architectural decisions, exact tests, and
the next autonomous objective. Keep detailed system classification in
`docs/web-port-convergence.md`; keep this file focused on product sequence and
engineering decisions.
