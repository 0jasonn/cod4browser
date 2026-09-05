# Web product roadmap

Updated 2026-09-05. Target: full offline single-player fidelity with original
Steam COD4 (2007). Canonical Kisak owns gameplay and assets; the dedicated
Worker, synchronous filesystem adapter and WebGL2 backend own browser support.
Multiplayer remains out of scope.

Completed implementation and verification belong in [current status](web-status.md),
[system ownership](web-port-convergence.md) and the
[campaign evidence ledger](campaign-compatibility.md). Historical paused
benchmarks do not qualify current active-campaign performance.

## Engineering priorities

Work proceeds independently of campaign completion. Address an observed
engine/platform defect or a named fidelity gap in this order:

The 2026-09-05 audit covered browser fullscreen/recovery,
production size repair, one encountered authored-material technique, independent
native Material/Image and model/collision database verification, then current
foreground production performance. Fullscreen/recovery implementation and its
48-case production tier pass. The unchanged production size gate is restored.
The encountered `vertcol_mul_fog` technique now preserves both passes; native
shader pixel comparisons, native/Wasm boundaries and owned three-scene/context
observations pass. The original `db_load.cpp` Material/Image and XModel oracle
matches adapted native/Wasm publication/block traces and rollback/retry/resource
lifetime assertions. Seven foreground production windows now cover both
resolutions, transitions, real context recovery, audio, memory and host
responsiveness. CargoShip remains slow; isolate its current CPU/GPU stages
before choosing another optimization. The other scenes' capped rates do not
qualify busy gameplay. See [current measurements](evidence/browser-frame-time-2026-09-02.md#current-production-measurements--2026-09-05).
The existing priorities below remain the broader fidelity backlog.

1. **Text input:** qualify Windows IME candidate UI, non-Western code pages,
   localized glyphs and clipboard behavior beyond the bounded trusted-paste path.
2. **Graphics controls:** qualify remaining shipped controls, unknown shader
   families and multipass semantics. Compare display response and authored
   materials with matching Steam/native scenes.
3. **Rendering gaps:** compare transparency, lighting, special vision, saved
   screens, text and encountered effects against the original. Outstanding
   cases include AC130 thermal/cloud passes, soft particles and distortion,
   outdoor particle lighting, and equal-key transient receiver ordering.
   Keep canonical assets, FX and visibility authoritative.
4. **Audio/cinematics:** compare dialogue, music, positional audio, EQ activation,
   room transitions and reverb callback cost. Qualify movie synchronization,
   colour, authored in-world screens and transitions, long/background stalls,
   arbitrary audio tails and hardware output latency. See the
   [codec](cinematic-codec.md) and [reverb](browser-reverb.md) boundaries.
5. **Recovery/imported data:** extend the independent native loader oracle
   beyond the verified RawFile, Material/Image and XModel cases. Qualify larger multi-zone request
   graphs, non-world device rollback, image pressure, malformed loads and
   repeated transitions. Retain canonical DB publication and error cleanup;
   do not build a replacement loader.
6. **Distribution:** retain source/archive and production export boundaries;
   keep the restored product size gate within its unchanged budgets. Qualify Firefox and Safari separately from Chrome/Edge. Gamepad
   requirements must follow the product/reference; keyboard/mouse fidelity
   remains required.
7. **Measured performance:** profile current foreground production gameplay,
   combat, movies, repeated transitions and long sessions on named hardware.
   Record frame distributions/stalls, responsiveness, game/wall time, Wasm/GPU/
   audio memory, scheduling, context recovery and fresh-browser persistence.
   Optimize demonstrated bottlenecks; profile before considering pthreads.

## Manual campaign and reference acceptance

The user owns ordinary gameplay, original/native comparisons and mission
completion acceptance. Record these as unverified until observations establish
them; automated routes, replay substitutes and injected objective success do
not qualify.

- Complete shipped New Game through Killhouse, CargoShip and the authored next
  transition. Record natural objectives, scripted sequences, checkpoints,
  death/restart, fresh-browser Continue and mission completion. Current authored
  training/checkpoint evidence stops at the Captain Price/ladder platform;
  course completion and the remaining chapter flow still need verification.
  See [training evidence](evidence/campaign-training-disconnect-2026-09-02.md).
- Compare original Steam, native Kisak and browser Kisak with matched settings,
  difficulty and scenes. The [Steam inventory](evidence/steam-reference-2026-09-02.json)
  pins installation facts; the [native reference](evidence/native-reference-2026-09-02.md)
  establishes build/menu startup, with visual and gameplay qualification open.
- Qualify remaining missions, special mechanics, difficulty-dependent behavior,
  localized installations/dialogue and authored checkpoint/menu presentation.
  Compile, boot, render, functional, playable, mission-complete and retail
  fidelity remain separate claims.

## Working gates

Use the pinned build and [validation tiers](web-test-inventory.md). Run focused
checks while iterating, then smoke and the non-overlapping remainder before
handoff; run production tests for changed product boundaries. Owned retail
checks run separately. Exhaustive duplicate suites need a concrete reason.

Update system ownership and relevant evidence when implementation changes.
Missing gameplay/reference evidence must name the next required observation;
short windows, unchanged draw counts and synthetic notifications cannot promote
campaign compatibility.
