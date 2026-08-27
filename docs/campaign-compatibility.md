# Campaign compatibility matrix

This ledger records current execution evidence, not filenames. Asset discovery
alone is `UNTESTED`; compatibility advances through canonical database, world,
server, game, cgame, renderer, input, audio, persistence, transition, and
recovery evidence.

## Current reference

The initial `PLAYABLE` threshold on the recorded Windows 11 reference host is:

- average frame rate at least 30 FPS;
- p95 frame time at most 50 ms; and
- game-time/wall-time ratio at least 0.90.

This is a conservative reference for the recorded hardware and browser, not a
universal hardware requirement. Performance is classified only from a headed
window that remained visible and focused with zero background transitions.
Background-invalid timing cannot establish or remove `PLAYABLE` status.

The clean baseline at `c66d41e1` ran
`killhouse` -> `cargoship` -> `blackout` -> `killhouse` in headed branded
Chrome and Edge. Every 60-second window was visible, focused, free of
background transitions, and valid. Canonical input, audio, checkpoint,
transition retirement, and forced context recovery passed. The encoded-source
recovery strategy from that commit was active.

The next campaign batch ran at clean `247980a6` in headed Chrome
151.0.7922.174 on Windows 11 Pro with an AMD Ryzen 7 7800X3D and 32 GiB of
system memory. See the
[sanitized machine-readable evidence](evidence/retail-campaign-247980a6.json).
It contains no retail paths or proprietary content.

| Map | Canonical runtime | First frame | Valid foreground performance | Canonical gameplay | Transition / context recovery | Recovery / Wasm capacity | Result | Evidence |
| --- | --- | ---: | --- | --- | --- | --- | --- | --- |
| `killhouse` | DB, ClipMap/world, server, game, cgame, and actual world frames pass | Pass | Chrome: 33.97 FPS, 31.905 ms p95, 0.998708 ratio; Edge: 34.64 FPS, 31.325 ms p95, 0.999062 ratio | Full input, canonical fire/ADS/weapon selection where applicable, audio, and checkpoint pass | Fresh load and return transition pass; old-map recovery retires; forced recovery passes after return | Encoded-source recovery active; lifecycle telemetry passes | **PLAYABLE** | Clean legally owned Chrome/Edge baseline at `c66d41e1`. |
| `cargoship` | DB, ClipMap/world, server, game, cgame, and actual world frames pass | Pass | Chrome: 12.79 FPS, 87.03 ms p95, 0.992821 ratio; Edge: 13.71 FPS, 80.035 ms p95, 0.998158 ratio | Full input, canonical fire/ADS/weapon selection where applicable, audio, and checkpoint pass | `killhouse` -> `cargoship` and onward transition pass; old-map recovery retires; forced recovery passes | Encoded-source recovery active; lifecycle telemetry passes | **FUNCTIONAL** | Clean legally owned Chrome/Edge baseline at `c66d41e1`; average and p95 miss the reference `PLAYABLE` threshold. |
| `blackout` | DB, ClipMap/world, server, game, cgame, and actual world frames pass | Pass | Chrome: 26.77 FPS, 39.925 ms p95, 0.999058 ratio; Edge: 26.90 FPS, 39.64 ms p95, approximately 0.999 ratio | Full input, canonical fire/ADS/weapon selection where applicable, audio, and checkpoint pass | `cargoship` -> `blackout` -> `killhouse` passes; old-map recovery retires; forced recovery passes | Encoded-source recovery active; lifecycle telemetry passes | **FUNCTIONAL** | Clean legally owned Chrome/Edge baseline at `c66d41e1`; average FPS misses the reference `PLAYABLE` threshold. |
| `airplane` | All canonical runtime boundaries pass | 7,352.500 ms | 3,597 frames; 59.95 FPS; 18.65 ms p95; 0.99999 ratio | Fire clip 12 -> 11; ADS, wheel selection, movement, mouse, menu, pointer lock, audio, and checkpoint pass | CargoShip in / Killhouse out pass; 591.775 ms context recovery | 122,778,819 B aggregate CPU recovery / 866,582,528 B capacity | **PLAYABLE** | Clean `247980a6` record; compact indoor/conventional-combat coverage. |
| `hunted` | All canonical runtime boundaries pass | 6,660.220 ms | 1,145 frames; 19.08 FPS; 55.40 ms p95; 0.993989 ratio | Fire clip 6 -> 5; ADS, movement, mouse, menu, pointer lock, audio, and checkpoint pass; wheel is `NOT_APPLICABLE_SINGLE_WEAPON` | CargoShip in / Killhouse out pass; 1,984.015 ms context recovery | 480,307,702 B aggregate CPU recovery / 968,163,328 B capacity | **FUNCTIONAL** | Clean `247980a6` record; outdoor visibility, foliage, world, and dynamic-model coverage; average and p95 miss the threshold. |
| `bog_a` | All canonical runtime boundaries pass | 9,821.955 ms | 1,271 frames; 21.21 FPS; 56.64 ms p95; 0.999457 ratio | Fire clip 15 -> 14; ADS, wheel selection, movement, mouse, menu, pointer lock, audio, and checkpoint pass | CargoShip in / Killhouse out pass; 1,844.635 ms context recovery | 428,120,523 B aggregate CPU recovery / 961,937,408 B capacity | **FUNCTIONAL** | Clean `247980a6` rerun; dense combat, FX, material, entity, and audio coverage; average and p95 miss the threshold. |
| Other directly selected non-`mp_*`/non-`*_mp` SP zones | Not run | Not run | Not measured | Not run | Not run | Not recorded | **UNTESTED** | Discovery or a header probe is not compatibility evidence. Add one row only after a legal local execution run. |

`decodedTextureRecoveryBytes` is the logical decoded texture size. It is not
the retained recovery allocation after `c66d41e1`: the renderer now retains
encoded/canonical image sources where supported. `aggregateCpuRecoveryBytes`
and `textureRecoverySourceBytes` describe actual retained recovery storage.

## Result definitions

- `UNTESTED`: no retail runtime evidence.
- `LOADS`: canonical database/world loading completes without a proven world
  frame.
- `RENDERS`: at least one actual canonical world frame is produced.
- `FUNCTIONAL`: sustained runtime with functional core gameplay, input, audio,
  transitions, and no fatal error.
- `PLAYABLE`: `FUNCTIONAL` plus a valid foreground performance window meeting
  the current documented reference threshold.
- `BLOCKED`: a deterministic compatibility failure is reproduced and
  classified.
- `REGRESSION`: a map with current passing evidence now fails.

Failure classes follow the earliest incorrect boundary: `filesystem`,
`database`, `asset publication`, `ClipMap/world`, `server`, `game/scripts`,
`client/cgame`, `renderer frontend`, `WebGL2 renderer`, `material`, `entity`,
`fx`, `audio`, `save/persistence`, `memory/lifecycle`, or `unknown`.

## Historical evidence

The historical `887f1c87` baseline recorded Killhouse as PLAYABLE and
CargoShip as RENDERS. The clean two-map matrix at `ac063bb2` and first Blackout
campaign run at `6be926cb` remain useful canonical lifecycle evidence:

- [two-map structured evidence](evidence/retail-phase1-ac063bb2.json)
- [Blackout structured evidence](evidence/retail-phase3-6be926cb.json)

Their unfocused/background-susceptible frame timings predate the foreground
validity gate and do not establish current performance status. The current
matrix above supersedes their compatibility classifications.

No proprietary assets are used in hosted CI. Promote a row only from a local
run against legally owned files, recording the exact commit, browser, date,
reference host, foreground validity, and sanitized machine-readable result.
Configuration checkpoints demonstrate config persistence, not complete
gameplay save/load parity.
