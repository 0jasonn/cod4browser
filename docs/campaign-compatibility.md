# Campaign compatibility

These are historical execution levels, not a fresh qualification of the current
build. Eight maps have runtime evidence: two `PLAYABLE`, four `FUNCTIONAL`,
two `RENDERS`; fourteen zones remain `UNTESTED`. No map has verified authored
mission completion or complete original Steam/native/browser fidelity.
See [current status](web-status.md) and [local validation](local-retail-validation.md).

| Map | Level | Demonstrated scope |
| --- | --- | --- |
| `killhouse` | **PLAYABLE** | Canonical runtime, gameplay/input/audio, foreground timing, transitions and context recovery; early authored training below. |
| `airplane` | **PLAYABLE** | Canonical runtime and core gameplay; substantial save/load continuity, shipped Continue and save presentation. |
| `cargoship` | **FUNCTIONAL** | Sustained core gameplay, transitions and recovery; recorded performance missed the playable threshold. |
| `blackout` | **FUNCTIONAL** | Sustained core gameplay, transitions and recovery; recorded performance missed the playable threshold. |
| `hunted` | **FUNCTIONAL** | Sustained core gameplay, transitions and recovery; wheel selection was inapplicable with one weapon. |
| `bog_a` | **FUNCTIONAL** | Sustained core gameplay, transitions and recovery; recorded performance missed the playable threshold. |
| `scoutsniper` | **RENDERS** | Canonical startup and 60-second stationary headless window after CargoShip; gameplay, visual correctness and recovery untested. |
| `ac130` | **RENDERS** | Canonical startup, stationary diagnostics and production world frames with screenshot inspection; gunship interaction, mission flow and thermal fidelity unverified. |

Untested zones: `aftermath`, `airlift`, `ambush`, `armada`, `bog_b`, `coup`,
`icbm`, `jeepride`, `launchfacility_a`, `launchfacility_b`, `simplecredits`,
`sniperescape`, `village_assault`, `village_defend`. File discovery alone never
promotes a zone. Village Assault's bounded automation failed to demonstrate
trigger traversal; that does not establish a deterministic engine defect.

The six gameplay levels retain corrected clean six-map evidence at `93451ec5`
and earlier Chrome/Edge regression coverage at `f5229806`. The reference host
was Windows 11, Ryzen 7 7800X3D, 32 GiB RAM and RTX 3070 Ti. The historical
`PLAYABLE` threshold requires `FUNCTIONAL`, average >=30 FPS, p95 <=50 ms and
game-time/wall-time ratio >=0.90 in a profiling-disabled 60-second headed,
visible, focused window with no background transitions. It is a reference
benchmark, not a universal hardware requirement. Headless or background timing
cannot establish or revoke `PLAYABLE`.

## Authored campaign acceptance

Ordinary shipped New Game on owned English data completed Killhouse rifle,
timed-shooting and sidearm/melee training and authored checkpoints 1-3. A fresh
Chrome process resumed checkpoint 3 with those objectives complete. Two
production Save and Quit cycles returned to the canonical menu, and Resume
Game restored that checkpoint. Further ordinary input reached Captain Price,
climbed the ladder and reached the course platform. Equipment pickup and the
timed course were not completed; the preserved save remains checkpoint 3.

That manually observed headless session used ordinary input and screenshots;
it establishes early authored progress and fresh-browser Continue, not mouse
fidelity, active foreground performance, death/restart or chapter completion.
Remaining acceptance is natural New Game through Killhouse, CargoShip and the
authored following transition: objectives, scripts, checkpoints, death/restart,
fresh-browser Continue and completion, compared with original Steam and native
Kisak where available. Retired route/replay automation, injected objectives,
teleports and synthetic progression are not substitutes for this acceptance.

## Profiles, saves and language

Kisak owns dvars, bindings, profiles, save serialization and shipped menus; OPFS
provides persistence. Archived configuration and bindings restore after page/
Worker restart. `players/profiles/active.txt` restores profile selection; config
and save files stay isolated per profile, and canonical deletion is durable.
Profile test names use identifier tokens such as underscores, not hyphens.

Owned Airplane checks cover save enumeration, selection, load, deletion,
restored health/weapons/ammo/objective state and Continue after page/Worker
restart. Injected save/objective probes establish those boundaries, not authored
mission progression. Configuration checkpoints prove configuration durability,
not complete gameplay save/load. Do not generalize Airplane coverage to other
maps; Killhouse's fresh-browser evidence is the natural checkpoint above.

Save metadata is bounded and the shared menu shows map, description and date.
Paired 512x512 JPEGs use canonical save identity, wait for a matching rendered
map and reject stale completions after replacement/deletion. Shutdown drains
admitted codec jobs. A save can remain valid without a thumbnail if capture
never gets a frame or its queue is full; the menu retains its fallback.
Airplane's automatic start-level thumbnail and reload were inspected. Locale
formatting, capture timing/gamma and full native/Steam menu fidelity remain
unverified. Quit flushes pending writes and returns to the launcher; it does not
create a campaign checkpoint automatically. Save and Quit is a separate shipped
menu action.

Retail gameplay evidence is English-only. French/German synthetic tests verify
localized import paths, canonical language selection, persistence and rejection
of changed localization metadata; marker acceptance does not qualify translated
menus, glyphs, dialogue, cinematics or gameplay. Other allowed languages remain
unverified. The original Steam inventory (app 7940, build 2737681) records files
and configured settings, not observed fidelity or difficulty. See
[native reference](native-reference.md) for reproducible native startup.

## Recording a result

`UNTESTED` means no qualifying runtime evidence; `LOADS` proves canonical DB/
world loading; `RENDERS` adds actual world frames; `FUNCTIONAL` adds sustained
core gameplay/input/audio/transitions without fatal errors; `PLAYABLE` adds the
valid performance window above. `BLOCKED` requires a reproduced deterministic
failure; `REGRESSION` requires a previously passing case to fail. Record the
earliest incorrect engine/platform boundary rather than weakening assertions.

New claims require exact source and artifact identity, clean/dirty state, date,
browser/hardware, foreground validity and a sanitized local result. Historical
full reports are archived in Git at `15c316606281e4de63cbf3c06626ee60a307498d`.
