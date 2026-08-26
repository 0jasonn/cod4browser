# Campaign compatibility matrix

This ledger records execution evidence, not filenames. An imported fastfile is
only asset discovery. `LOADS`, `RENDERS`, and `PLAYABLE` require progressively
stronger canonical runtime evidence.

The historical baseline at `887f1c8775356c3b2c689cae1a8b3b0cb9df87d9`
recorded Killhouse as PLAYABLE and CargoShip as RENDERS. The strengthened
two-map matrix was completed on 2026-08-26 against a clean tree at
`ac063bb20cbc4027497841322d87c2069d736939`, using Release diagnostics,
Playwright Chromium 149.0.7827.55, and Windows 11 x64. See the
[execution report](../WEB_ROADMAP_EXECUTION_REPORT.md) and the
[machine-readable evidence](evidence/retail-phase1-ac063bb2.json). The record
contains no retail paths or proprietary content.

The first Phase 3 campaign batch completed on 2026-08-26 against a clean tree
at `6be926cb4e78693f9f6e638c348b0ee0f908b45f`, using the same Release
diagnostics, browser, and host platform. Blackout completed the canonical
runtime lifecycle, a 60-second gameplay window, both map transitions, context
recovery, and configuration persistence. See the
[Phase 3 structured evidence](evidence/retail-phase3-6be926cb.json), which also
contains no retail paths or proprietary content.

| Map | Asset discovered | DB load | Clip/world | Server | Game | CGame | First frame | 60s stable | Input | Audio | Transition in | Transition out | Save/load | Context recovery | Peak memory | Result | Failure class | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `killhouse` | Yes; required profile zone | Pass | Pass | Pass; not a separate structured boolean | Pass; not a separate structured boolean | Pass | Pass; 6,797.800 ms | Pass; 60,025.385 ms / 94 frames | Pass | Pass | Pass; fresh profile | Pass; old recovery retired before `cargoship` publication | Config checkpoint passed; 2 files / 3,154,912 B / 238.69 ms; aggregate config reload passed after CargoShip | Not separately measured; forced recovery ran on CargoShip | 1,521,922,580 B transition peak aggregate CPU recovery; 2,013,724,672 B Wasm capacity | PLAYABLE | — | Clean legally owned Phase 1 run at `ac063bb2`; [structured evidence](evidence/retail-phase1-ac063bb2.json). |
| `cargoship` | Yes; selected from supplied SP zones | Pass | Pass | Pass; not a separate structured boolean | Pass; not a separate structured boolean | Pass | Pass; 10,838.495 ms | Pass; 60,091.810 ms / 67 frames | Pass | Pass | Pass; `killhouse` → `cargoship` | No second map transition measured; explicit shutdown passed | Config checkpoint/reload passed; 7 files / 7,091,210 B / 408.28 ms | Pass; recovered frame in 1,779.19 ms, input resumed | 753,839,840 B aggregate CPU recovery; 2,013,724,672 B Wasm capacity | PLAYABLE | — | Clean legally owned Phase 1 run at `ac063bb2`; [structured evidence](evidence/retail-phase1-ac063bb2.json). |
| `blackout` | Yes; directly selected SP zone | Pass | Pass | Pass | Pass | Pass | Pass; 11,144.985 ms | Pass; 60,022.685 ms / 73 frames | Pass | Pass | Pass; `cargoship` → `blackout`, 742,024,672 B of old aggregate CPU recovery retired to zero | Pass; `blackout` → `killhouse`, 1,315,887,176 B of old aggregate CPU recovery retired to zero | Config checkpoint/reload passed; 7 files / 7,687,388 B / 974.925 ms | Pass; recovered frame in 2,149.595 ms, input resumed | 1,334,750,040 B transition peak aggregate CPU recovery; 1,776,091,136 B Wasm capacity | PLAYABLE | — | Clean legally owned Phase 3 run at `6be926cb`; [structured evidence](evidence/retail-phase3-6be926cb.json). |
| Other directly selected non-`mp_*`/non-`*_mp` SP zones | Not measured | Not run | Not run | Not run | Not run | Not run | Not run | Not run | Not run | Not run | Not run | Not run | Not run | Not run | Not recorded | UNTESTED | unknown | The importer admits bounded direct `zone/english/*.ff` SP candidates, but discovery or a header probe is not compatibility evidence. Add one row per map only after a legal local run identifies it. |

Result definitions:

- `UNTESTED`: no retail execution evidence.
- `LOADS`: canonical database/world loading completes, without a proven world frame.
- `RENDERS`: at least one actual canonical world frame is produced.
- `PLAYABLE`: sustained frames, functional player input, and no fatal error are
  observed during the validation window.
- `BLOCKED`: a deterministic failure is reproduced and classified.
- `REGRESSION`: a previously passing result fails on the current build.

Failure classes are `database`, `filesystem`, `renderer`, `material`, `entity`,
`fx`, `audio`, `cgame`, `memory`, `lifecycle`, or `unknown`.

No proprietary assets are used in hosted CI. Promote a row only from a local
run against legally owned files, recording the exact commit, browser, date, and
machine-readable `KISAK_RETAIL_RESULT`. The current local validator requires
both `killhouse.ff` and `cargoship.ff`; it measures 60-second stability, input,
audio, configuration checkpoint/reload, transition retirement, memory, and
context recovery. The separate Phase 3 campaign validator currently admits
Blackout and measures CargoShip-to-Blackout-to-Killhouse transitions plus the
same stability, input, audio, configuration, recovery, and shutdown boundaries.
These configuration checkpoints are not a claim of gameplay save/load parity.
