# FX point lighting — 2026-09-02

Working tree based on `a2d54b7d`, after the
[saved-screen milestone](saved-screen-2026-09-02.md).

`FX_CalculatePackedLighting` already calls canonical
`R_GetAverageLightingAtPoint`; the browser implementation always returned
white. It now samples the canonical world's light grid and sun color through
the existing renderer lighting helper and canonical visibility callbacks.
FX continues to own its packed color and effect state.

The implementation follows native `rb_light.cpp`'s average query, which is
different from the existing model-lighting query: only a sun-selected lookup
blends eligible ambient palettes; other primary selections use black ambient
and sun alpha 128. Sun/occluded corner weights, merged palette weights, the
existing fixed-point blend, the 56-sample average, and final truncated/clamped
byte color are retained. White remains only the pre-world/unusable-sample
fallback. No new asset or FX representation was added. This is an adaptation
in the existing renderer boundary; compiling the native light-grid backend
would retire it alongside that helper's current portable lookup.

## Execution and limits

- The focused `web_renderer_lighting_tests` target passed on Windows x64 with
  Clang 24.0.0 and under Emscripten 6.0.6 Wasm/Node 24.18.0. Assertions remain
  enabled in Release. New synthetic cases check native arithmetic for full
  and partial sun weights, excluded non-sun palettes, non-sun selection,
  saturation/truncation, non-finite positions and invalid palette indices.
  These execute the same portable helper on both architectures; they are not
  a run of the full native renderer.
- Release production and diagnostics, including both canonical runtime-prefix
  checks, passed. Bundled headless Chromium 149.0.7827.55: routine smoke 12
  passed, non-overlapping remainder 39 passed / 4 optional retail skips.
  Synthetic runs explicitly cleared inherited retail variables on port 8137.
- Final production port 8138: mount-error and boot/API checks 2 passed;
  production API/size gate passed (3,298,143-byte Wasm, 24 raw exports,
  9 application exports).
- A separate final-artifact run in headless Chrome 152.0.7977.65, persistent
  test profile on port 8139, passed the owned Killhouse load and saved-screen
  material check. This detects runtime regressions through map startup; it is
  not a visual reference for FX lighting.

Final Wasm SHA-256: production
`0e84916d2c73ffb198d470a03b6136037461008c60da280235bef4cf42fddde5`;
diagnostics `e0432856ef1c6f9c50acb6078149a5f9103c5b57e5043e3b8bcfee5a61698bee`.

No campaign classification changes. Original Steam/native/browser FX color
comparison during authored gameplay remains required. Transient omni/spot
submission, text effects, cinematics, audio reverb/EQ and the other roadmap
milestones remain open.
