# Authored Killhouse training and Save and Quit — 2026-09-02

The production browser now returns from the shipped Save and Quit action to
the canonical main menu and can resume the saved game in the same Worker.
Ordinary New Game gameplay exposed a missing live error boundary: the menu's
successful-save callback calls `CL_Disconnect_f`, which raises
`Com_Error(ERR_DISCONNECT)`. Initialization and mounting had already returned,
so their `setjmp` targets were no longer live. The save succeeded, but the
Worker terminated with `Uncaught [object Object]`, leaving the last frame frozen.
This reproduced after starting a fresh browser process from the saved checkpoint.

`web_main.cpp` now gives every frame a live canonical jump target. Disconnect
recovery calls the shared native shutdown and UI reload sequence, including
client/server/game/save cleanup, renderer initialization and `Com_AssetLoadUI`.
`common_shutdown.cpp` and `sv_shutdown.cpp` extract those existing native owners
into sources compiled by native and browser targets. No browser mission state
or menu implementation is added. Other error classes still terminate through
`Sys_Error` with their canonical message; general native error recovery remains
unfinished. A small non-inlined frame body keeps Emscripten's jump handling
within the existing production size budget.

## Authored campaign evidence

On the owned English installation, Chrome 152.0.7977.65 loaded shipped New Game
into Killhouse on the production artifact. Individually observed keyboard inputs
completed rifle pickup, aimed and hip-fired targets, penetration through wood,
timed shooting, pistol pickup/weapon switching, and the knife/melon sequence.
The canonical log records `aa_rifle_training`, `aa_timed_shooting_training` and
`aa_sidearm_melee`, each with gameskill 1 and zero deaths. Authored autosaves
advanced through `autosave\\killhouse-1`, `-2` and `-3`. The final observed
objective was to locate Captain Price. No diagnostic map load, injected
objective, teleport, give command, route or script-progression override was used.

A new persistent Chrome process used the shipped Resume Game menu and restored
the completed training objectives from checkpoint 3. After the fix, two observed
Save and Quit cycles returned to the main menu; Resume Game between them restored
the checkpoint. The Worker remained alive, with no page or Worker errors.
Screenshots of the objectives, restored checkpoint and returned menu were
inspected. This establishes natural early training and fresh-browser Continue;
it does not establish Killhouse course/mission completion, CargoShip or the
following transition, death/restart, or original Steam/native gameplay equivalence.

A subsequent ordinary-input session reached Captain Price, advanced the
objective to climbing the ladder and reached the course platform. The run
stopped there; equipment pickup and the timed course were not completed.
The preserved authored checkpoint remains the earlier training checkpoint.

The run was headless with manually observed screenshots on the recorded Ryzen
7 7800X3D / RTX 3070 Ti Windows 11 reference host. Pointer lock picked up host
mouse motion and was released; native turn/look/fire bindings were used instead.
Timed training included retries and ordinary Escape pauses between observations.
This is neither mouse-fidelity nor foreground active-performance evidence.
The imported files, profile, save data, logs and screenshots remain private in
ignored `build/first-chapter-profile` and `build/campaign-first-chapter-evidence`.

## Verification

- Pinned native SP OpenAL Release and production/diagnostic Wasm Release builds
  pass. Existing production budgets remain unchanged: 3,330,397 B Wasm,
  356,469 B JavaScript, 3,697,918 B site, 24 raw / 9 application exports, 19 files.
- Chromium 149.0.7827.55: 43 production tests pass (port 8018, 10.7 seconds),
  12 diagnostic smoke tests pass (port 8161, 7.5 seconds), and 44 remainder tests
  pass (port 8162, 16.4 seconds), with five optional retail skips. These suites
  ran serially with inherited retail variables cleared.
- The owned retail menu regression now exercises the shipped Save and Quit
  confirmation, checks main-menu restoration, resumes through canonical
  `loadgame_continue`, and retains all previous save/profile/Quit assertions.
  It passes in Chrome 152.0.7977.65 on diagnostic port 8163 (2.4 minutes).
  Its three-profile fixture reopens the shipped profile selector over `main`;
  the separate ordinary single-profile production cycles show the main menu.

The initial authored training used production Wasm SHA-256
`34041b1c90ced7a2b1bfa7a4f9966fa412f02344a8a326bd344f4e460ad1fca1`.
The fixed production artifact is
`bbb784edf8e4625c9f416461e6a1edfcc7e7ebd14783a8dfa95601cfa5047bef`;
fixed diagnostics are
`8992c3331389719cfca9b435e4f2f14b0127a004676e71c850592108f68f4224`.
Build/test logs are under ignored `build/goal-campaign-disconnect-*`.
Static/protocol suites and full native gameplay were not rerun for this change.
