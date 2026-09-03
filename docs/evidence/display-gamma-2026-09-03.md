# Browser display gamma — 2026-09-03

The shipped Brightness slider now changes the finished browser frame, including
menus and HUDs. Canonical `r_gamma` remains the setting owner. `R_EndFrame`
passes its value to the existing WebGL2 final pass; menu-only frames now use
the same scene/composite targets. World film, blur, glow and depth-of-field
remain restricted to world frames and precede 2D composition.

The native `R_CalcGammaRamp` and `R_GammaCorrect` bodies moved unchanged from
`r_init.cpp` into `r_gamma.cpp`, with a graphics-API-independent header. Native
SP/MP, Radiant and browser source inventories include that source. The native
SP target was rebuilt successfully; MP and Radiant were not built in this run.

This is a permanent browser display adaptation. WebGL cannot install a D3D9
monitor ramp, so hardware-gamma capability remains false. The shader applies
the native exponent `1 / r_gamma` at presentation. `r_ignorehwgamma` bypasses
that correction. Gamma no longer belongs to a world-view descriptor or resets
when a world unloads. Saved-screen feedback and save-thumbnail reads remain
before display correction, avoiding a second correction when displayed later.

## Reproduction and focused checks

- Before the fix, the new diagnostic pixel test failed at gamma 0.5/input 32:
  output remained 32 while the native reference was 4 (28 units difference).
- Afterward, five gamma values (0.5, 0.8, 1, 2, 3) at six input levels match
  native ramp bytes within one RGBA8 unit. This tolerance accounts for native
  16-bit-ramp-to-byte truncation versus framebuffer UNORM rounding. Bypass,
  context restoration and resize are also checked.
- Existing saved-screen and text pixel assertions are unchanged. Their fixture
  setup now explicitly selects gamma 1 to isolate their pre-display arithmetic.
- Native and Wasm `r_gamma_tests` and `web_renderer_world_scene_tests` pass.
  All 256 entries produce the same trace hash in both gamma implementations:

| Gamma | Ramp hash | Input 128, 16-bit output | Byte output |
| --- | --- | --- | --- |
| 0.5 | `6f4ed205` | 16513 | 64 |
| 0.8 | `697c46a4` | 27689 | 107 |
| 1 | `ab39a0c5` | 32896 | 128 |
| 2 | `5e6a1544` | 46431 | 180 |
| 3 | `54a6f9b0` | 52083 | 202 |

## Owned retail qualification

Production headless Chrome **152.0.7977.65**, the pinned local English Steam
installation, and the hardware in `steam-reference-2026-09-02.json` were used.
No proprietary files or screenshots are added to Git.

The existing Options → Graphics menu was opened. Actual pointer clicks on its
Brightness slider selected approximately 0.588149 and 2.91458; canonical console
queries confirmed those values. Visual inspection confirmed the screen changed.
The committed optional `@retail-gamma @product` test repeats those real menu
clicks and measures rendered pixels, then loads real Killhouse and checks that
a canonical gamma change also affects world presentation. The run passed in
24.8 seconds with no page errors. Sampled mean RGB byte values were:

| Frame | Low gamma | High gamma |
| --- | --- | --- |
| Shipped options menu | 5.861 | 100.197 |
| Loaded Killhouse | 79.896 | 236.881 |

These are visible-effect checks, not reference-image comparisons or performance
measurements. They do not establish authored progress, mission completion,
calibrated monitor response, or visual equality with original Steam output.

The same menu inspection exposed a separate existing defect: **Video Mode** and
**Screen Refresh Rate** show `<not an enum dvar>`. Those controls still require
a browser-appropriate implementation and are not qualified by this change.

## Build and verification artifacts

- Production Release Wasm SHA-256:
  `a07762c114f1585c6193f45001d024db992d2e18749e74837edcc0a483d92ec4`.
- Diagnostic Release Wasm SHA-256:
  `c15463b025b262ba3d3c4c7a86daf0c2539158a7c42d1c5bdc9e81637c19fe8b`.
- Pinned Emscripten 6.0.6; native MSVC/SDK from `tools/native_toolchain.json`.
- Static checks and 83 Node protocol tests pass. Native SP builds.
- The production size gate still fails: Wasm 3,708,395 / 3,332,379 bytes;
  JavaScript 761,250 / 357,646; site 4,567,712 / 3,701,082. Budgets are unchanged.
  This patch adds 78 Wasm bytes to the previous localized-import artifact; the
  existing cinematic/reverb additions account for the outstanding size excess.
- Private logs: `build/goal-gamma-*.log`. Retail screenshots and measurements:
  `test-results/8194/`; exploratory captures: `build/gamma-retail-evidence/`.
  Production retail serving used isolated port 8194; the user's server on 8160
  was preserved. Synthetic runs explicitly clear inherited retail variables.

Final routine runs use bundled Chromium **149.0.7827.55**:

| Tier | Artifact | Isolated port | Result |
| --- | --- | --- | --- |
| `npm.cmd run test:browser` | Diagnostic Release | 8195 | 12 passed |
| `npm.cmd run test:browser:remainder` | Diagnostic Release | 8196 | 50 passed, 6 optional retail skips |
| Five routine production spec files | Production Release | 8197 | 43 passed |

The focused diagnostic gamma/saved-screen/text run also passed (three tests,
port 8192); the final remainder includes the subsequently added gamma context
restoration/resize assertions. The separate owned-retail run used Chrome 152
on port 8194. No exhaustive duplicate suite was run.
