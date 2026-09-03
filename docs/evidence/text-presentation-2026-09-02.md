# Shared text presentation — 2026-09-02

## Change and reference

The browser's simplified glyph loop skipped color codes, ignored style/glow/
effect timing, drew a permanently visible cursor and left console text empty.
`r_text.cpp` now contains the native `DrawText2D` implementation and its glyph,
rotation, glow, cursor and pulse helpers, moved from `rb_backend.cpp` and
`r_font.cpp`. Native SP/MP and Radiant source inventories retain those owners;
the web target compiles the same source. `r_text_cmds.cpp` shares the native
glow/pulse parameter setters and console ring copy.

The web frontend builds canonical text command parameters and provides the
backend's scene/cursor clocks, SP color lookup and glyph-quad submission. Its
quad adapter converts native BGRA packing to WebGL RGBA, preserves the rotated
text origin, and uses the existing material/image/state and UI scene boundary.
The font and material records stay canonical. No JavaScript text layout, font
asset representation or gameplay/timing model was introduced.

This restores native shadow styles 3/6, monospace style 128, color escapes,
highlighted subtitle glow, four-pass glow offsets, cursor blinking, random
character reveal and per-letter decay. The native half-pixel origin, original
glow offset table and alpha behavior are preserved, including their quirks;
native agreement is not evidence of identical Steam rendering.

## Verification

- `r_text_tests`: native MSVC Win32 Release and Wasm/Node passed. Synthetic ASCII
  glyphs and a recording quad backend cover position/rotation, color packing,
  shadow offsets, monospace advance, subtitle glow selection/size, cursor
  blink, timed reveal/expiry and the native per-letter decay cutoff/fade.
  The harness supplies external clock, material and ASCII string dependencies;
  glyph selection and text/effect layout execute the shared native source.
- `text_presentation.spec.mjs`: browser command boundary passed. Exercises
  styles and real text wrappers, a wrapped `^2ABC` console buffer, subtitle and
  pulse console entry points, and a WebGL pixel for the native red color code.
  Pixel tolerance is two RGBA8 units; coordinate tolerance is 0.001 pixels.
- Native SP OpenAL Release links with the pinned toolchain in
  `build/native-sp-text`, leaving the already running native reference alone.
  EXE SHA-256: `736e24e80d2f5741b096de8a19ce67819a45cd2504d36baa075ac291e1aa857c`.
- Production and diagnostic Release builds / runtime-prefix checks pass.
  Static checks and all 81 protocol tests pass. Chromium 149.0.7827.55: 43
  production tests on port 8018, 12 smoke and 41 remainder on port 8144 passed;
  five optional retail cases were skipped with inherited retail variables
  explicitly cleared.
- Existing production size/API budgets pass unchanged: 3,318,884 B Wasm,
  349,076 B JavaScript, 3,679,012 B site; 24 raw / 9 application exports.

Final production Wasm SHA-256:
`1e3e8a1ae5c16de2c5c955525f0c4104079f2422051af450aef2a4ef005ccca3`.
Final diagnostic Wasm SHA-256:
`4cc30bb9f8bbd94165b71fe7f742217058d8702d8697986a3d1151048e0b50d7`.

Private logs use `build/goal-text-*.log`. All generated fixtures, build output,
owned-installation browser traces and captures stay ignored.

## Test isolation and limits

A concurrent product run failed before import with `ENOENT`: the diagnostic
remainder invocation cleared their shared Playwright output directory and
deleted its fixture. Output directories now default to `test-results/<port>`.
The concurrent rerun passed all 43 product tests without changing assertions.
One diagnostic Escape/lock-loss observation was missed during concurrent
browser runs; the separate unchanged remainder run passed all 41 cases. Run
foreground/input-sensitive browser suites separately when collecting acceptance
evidence; port isolation alone does not isolate focus/lock behavior.

The final diagnostic artifact passed the complete main-menu/persistence test
in Chrome 152.0.7977.65 on port 8143 (1.7 minutes; canvas-capture rerun 2.2 minutes), including Killhouse
input/pause, Airplane save/Continue, Quit and restart. A private canvas capture
shows readable menu labels and visible shadows against the loaded menu
background. It is retained under the ignored `build/text-retail-canvas-results`
directory; the first viewport capture was scrolled and was replaced with an
element capture. This inspection establishes browser presentation only.
The test's injected objective and page reload do not establish
natural mission completion or Continue in a new browser process. Exact Steam
font/glow/cinematic-subtitle comparison, other languages, and authored timed
text throughout the campaign remain unverified. Material-family equivalence
and invalid/unsupported material handling remain renderer acceptance work.
In particular, the web text-effect wrappers still need the native outer
fogable/default-material rejection behavior checked; shared glyph drawing
already retains its native font/glow material checks.
