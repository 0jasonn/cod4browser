# Display options and renderer restart — 2026-09-03

The shipped Video Mode control now changes the browser render resolution.
Automatic follows canvas layout and pixel density; fixed modes retain their
render size and aspect when the page resizes. Screen Refresh Rate reports
`Browser controlled`: a web page cannot select the monitor's refresh rate.

## Boundary and correction

`web_display.cpp` registers canonical `r_mode` and `r_displayRefresh` enums,
filters fixed resolutions against WebGL texture/renderbuffer limits, and
populates native `vidConfig_t`. Host resize messages carry preferred dimensions;
the next engine frame applies them alongside existing screen placement, UI
dimensions and `CG_InitViewDimensions`. The DOM receives only the resulting
display dimensions/aspect. Game and mission state remain in Kisak.

The observed `vid_restart` abort was `Failed to create database thread`.
`CL_Vid_Restart_f` called `Com_InitXAssets` even though the process-owned database
executor was still alive. That redundant call is removed in shared client code;
the native single-owner assertion and browser spawn rejection remain intact.
The browser's `R_BeginRegistration` now recreates the backend after renderer
shutdown, using the same initialization function as first startup.

No alternate save or mission restart was added. The existing server path saves
`internal/vid_restart`, then runs `disconnect;vid_restart;loadgame
internal/vid_restart`.

## Execution

All builds used the pinned Emscripten 6.0.6 toolchain. Native SP also builds
with the pinned MSVC configuration. Production Wasm SHA-256:
`e2dc2ea4659ff996f2b74cb285a3d888214f9b16b7e856c9387ef8f48a947c70`.
Diagnostic Wasm SHA-256:
`432c4293b56c8ba2b75aef19c0e9c365bcc7b508de8d85c0a89babf483053bbf`.

- Static checks and 83 Node protocol tests pass.
- Diagnostic Chromium 149.0.7827.55: 12 smoke and 50 remainder pass; six
  optional retail cases skip with retail environment variables removed.
- Production Chromium 149: 44 tests pass, including fixed/Automatic resolution,
  browser resize, native restart, Quit and durable resolution restoration.
- Separate owned English installation, production Chrome 152.0.7977.65:
  shipped Graphics → Video Mode → Apply → Yes selects 640×480, returns to the
  main menu, and survives Quit/Start game. Automatic resize produces a matching
  1058×595 canonical cgame viewport. In-game restart saves and reloads Killhouse
  at 1280×720. The observed view origin remains `(3072,-1155,64.125)` and game
  time advances from 6506 to 8425 ms. No page errors occur.
- The separate shipped brightness-slider regression also passes at the
  now-applied 4:3 startup mode. Menu mean RGB changes 6.624 → 105.321; loaded
  world means are 85.072 at low gamma and 234.057 at high gamma. Existing
  dvar/pixel assertions are unchanged; input positions use canonical menu units.
- The production texture-quality regression now uses the shipped Graphics →
  Texture Settings menu rather than setting picmip dvars through the launcher.
  It measures full-quality Killhouse, returns through canonical disconnect,
  selects Manual plus Normal color/normal/specular resolution, and clicks Apply
  → Yes. The native menu command performs a renderer restart and reports picmip
  2 for all three semantic classes. Decoded texture bytes fall from
  1,153,857,573 to 168,912,933 and the GPU estimate falls from 1,174,304,805 to
  189,360,165. Quit/Start game and a new Killhouse load retain the exact reduced
  values. The focused owned production case passes in 50.9 seconds.

The first saved-screen remainder run sampled immediately after a queued resize.
It now waits for the actual 800×600 canvas before checking the unchanged black
pixel assertion. The narrow rerun and full remainder pass. The first retail
restart assertion missed a transient message in the launcher's bounded log;
the test now retains existing Worker log messages. An intermediate two-tag
command failed at the shell pipe boundary before running tests; the final
regex invocation uses no shell pipe.

Private captures and full engine traces remain under `test-results/8206/` and
`build/display-options-evidence/`. Build/check logs are `build/goal-display-*`.
No proprietary data or captures are added to the repository.

## Limits

This is settings and loaded-mission restart evidence. It does not complete
Killhouse or establish the New Game → CargoShip → following mission flow.
Steam/native visual comparison, every resolution/aspect choice, other shipped
graphics controls and non-Chromium browsers remain unqualified. The native
executable was rebuilt; native runtime restart was not exercised here.

The unchanged production size gate still fails: Wasm 3,710,055 bytes against
3,332,379; JavaScript 762,136 against 357,646; site 4,570,258 against 3,701,082.
The file/export checks preceding the size assertion pass. No budget was raised.
