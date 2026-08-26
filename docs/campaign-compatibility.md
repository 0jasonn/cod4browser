# Campaign compatibility matrix

This ledger records execution evidence, not filenames. An imported fastfile is
only asset discovery. `LOADS`, `RENDERS`, and `PLAYABLE` require progressively
stronger canonical runtime evidence.

The Killhouse and CargoShip rows are historical evidence catalogued at commit
`887f1c8775356c3b2c689cae1a8b3b0cb9df87d9` on 2026-08-25. They are not a
claim about the current branch: the strengthened matrix was not rerun on
2026-08-26 because no retail root was supplied. New results must record their
own exact commit, date, browser, and machine-readable result.

| Map | Asset discovered | DB load | Clip/world | Server | Game | CGame | First frame | 60s stable | Input | Audio | Transition in | Transition out | Save/load | Context recovery | Peak memory | Result | Failure class | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `killhouse` | Yes; required profile zone | Pass | Pass | Pass | Pass | Pass | Pass | Not recorded | Pass | Pass | Pass; fresh/repeat/loadgame | Pass; prior transition to `cargoship` | Pass | Not recorded on retail | ~766 MiB decoded recovery before the unload fix; current quantitative run pending | PLAYABLE | — | Historical legally owned Release Chrome runs summarized in [web-port-convergence.md](web-port-convergence.md) and [roadmap.md](roadmap.md); catalogued at the commit/date above. |
| `cargoship` | Yes; optional discovered SP zone in prior local import | Pass | Pass | Pass | Pass | Pass | Pass | Not recorded | Not recorded | Not recorded | Pass; prior `killhouse` → `cargoship` | Not recorded | Not recorded | Not recorded | Not recorded | RENDERS | — | Historical legally owned Release Chrome reached the first-person view and thousands of frames; catalogued at the commit/date above. |
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
audio, persistence, transition retirement, memory, and context recovery.
