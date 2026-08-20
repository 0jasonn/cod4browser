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

Baseline: `b5d2c76e` (`Harden FX mesh retention and renderer regressions`),
following the implementation commit `41c6c8a5` (`Render canonical FX code
meshes in WebGL2`).

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
- sound assets and the OpenAL lifecycle compile, but the current deployment
  cannot create an audio device: the production Wasm module lives in a
  dedicated Worker, and the pinned Playwright Chromium exposes neither
  `AudioContext` nor `document` there. Emscripten OpenAL therefore fails before
  payload playback or browser unlock/resume can occur.

Therefore the active boundary is **canonical fire/impact events and canonical
FX geometry reaching WebGL2, with retail effect visibility still to prove and
browser audio blocked at an engine-wide platform-ownership decision**.

## Active milestone: playable Killhouse / F.N.G. combat loop

Acceptance requires the following to work through canonical ownership in a
retail-asset browser run:

| Capability | Current reading | Owning subsystem | Next proof or closure |
| --- | --- | --- | --- |
| Spawn | Reached on Killhouse | game/server | Retain regression evidence; prove on F.N.G. |
| Move | Reached | input/client/game | Retain focused browser proof |
| Aim / ADS | Reached | input/cgame | Retain focused browser proof |
| Fire and ammo consumption | Canonical path reached; end-to-end behavior needs explicit evidence | game/cgame | Trace event, ammo delta, recoil, and frame continuity |
| Muzzle flash / brass | Canonical FX code-mesh consumption implemented; retail visibility proof pending | cgame/FX/renderer | Observe real fire event, effect definition, retained FX batch, and draw |
| Bullet impact | Canonical trace/event/impact-table and FX renderer paths present; end-to-end result unproven | game/cgame/FX/renderer | Prove surface-dependent impact FX; audio follows the platform decision |
| Weapon sound | Alias/mixer/OpenAL code is present, but the Worker has no Web Audio API | cgame/audio/platform | Approve and implement a browser audio ownership boundary, then prove payload/channel playback |
| Reload | Canonical weapon state exists; browser key/action and animation/audio proof pending | input/game/cgame/audio | Exercise empty/partial reload without browser state |
| Weapon switching | Canonical inventory/state exists; input and presentation proof pending | input/game/cgame | Exercise next/previous/direct selection and viewmodel transition |
| Basic combat interaction | Real bullet/game systems compiled; target damage/death/AI response unproven | game/script/cgame | Use real entities in F.N.G. or campaign content; no synthetic browser targets |

### Ordered work queue

1. **FX code-mesh rendering** — implemented in `41c6c8a5` and hardened in
   `b5d2c76e`. Canonical EffectsCore batches cross the renderer seam with
   deterministic material/image/UV/RGBA/order/depth/blend state and bounded,
   failure-atomic retention. Retail muzzle/impact observation remains part of
   the integration proof, not a second renderer implementation.
2. **Weapon audio closure** — architecture review required. After a platform
   design is approved, trace one real fire alias from `WeaponDef` through
   `SND_PlaySoundAlias` to OpenAL buffer/source playback. Close only the first
   actual missing lifecycle or payload-compatibility boundary discovered.
3. **Fire/impact integration proof** — add focused browser observability that
   proves one trigger causes canonical server/cgame fire, recoil/ammo change,
   visible muzzle FX, a collision result, and an impact effect without owning
   any of those states in browser code.
4. **Reload and weapon switching** — close input bindings and any missing
   cgame/viewmodel/audio compatibility in canonical state machines.
5. **Basic combat interaction** — prove damage, reaction, death, and script/AI
   notification against real map entities.
6. **F.N.G. parity pass** — load F.N.G., record the first blocker by subsystem,
   fix the narrowest reusable runtime gap, and repeat until the same combat
   acceptance set passes.

After every completed item, re-audit the runtime rather than assuming the next
listed item is still the highest-value blocker.

## Pending architecture decision: browser audio ownership

Measured on the pinned toolchain and bundled Playwright Chromium:

- production Wasm executes in `engine_worker.mjs` with OffscreenCanvas and the
  synchronous Worker filesystem;
- a dedicated Worker reports `typeof AudioContext === "undefined"` and has no
  DOM `document`;
- Emscripten's OpenAL implementation constructs Web Audio objects in the
  calling realm and its autoplay helper registers events on `document`;
- consequently, the current Worker OpenAL driver cannot initialize or resume a
  browser audio context. This is not a missing weapon alias or payload bug.

Options requiring review:

1. **Main-thread Web Audio command bridge (recommended).** Keep Kisak Wasm,
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

Do not implement an audio bridge until this choice is approved; it is a new
permanent platform architecture with multiple viable designs.

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
| Complete | Canonical FX code-mesh renderer closure | `41c6c8a5`, `b5d2c76e` | Retail muzzle/impact visibility proof; models/clouds/marks remain later FX families |
| Blocked for review | Browser audio platform ownership | Worker capability probe on pinned Chromium; see decision section above | Select the permanent audio boundary |
| Active | Playable Killhouse/F.N.G. combat loop | FX renderer closure accepted | Audio architecture, retail FX proof, reload/switching, combat interaction |
| Pending | Recognizable COD4 presentation | — | Materials, remaining images, FX breadth, sky/fog |
| Pending | Multiple maps and first campaign mission | — | Unknown until F.N.G./campaign probes |
| Pending | Offline campaign runtime | — | Mission flow, saves/checkpoints, cinematics, breadth and performance |

Update this ledger and the current runtime boundary after every major milestone.
Record implementation commit hashes, architectural decisions, exact tests, and
the next autonomous objective. Keep detailed system classification in
`docs/web-port-convergence.md`; keep this file focused on product sequence and
engineering decisions.
