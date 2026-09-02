# Canonical slow-command numeric conversion

`CG_SlowServerCommand` stored `atof` results in `long double`, then read their
storage through `double *`. Emscripten uses a 16-byte `long double`; this is
not a numeric conversion. A normal-speed `1` became zero, which subsequently
made `Com_ModifyMsec` fall back to its minimum one-millisecond game step.
Neither the developer `timescale=1` nor `fixedtime=0` exposed that corrupted
script-owned scale.

The shared command owner now converts `atof` results numerically to floats,
preserving the original float-valued input to `CG_AlterTimescale`. Script
interpolation, deliberate slow motion, and returning to normal remain native.
No browser-side game clock or forced per-frame timescale was introduced.

## Focused evidence

- The new `cgame_timescale_tests` compiles the real `cg_servercmds.cpp` and
  captures its call to `CG_AlterTimescale` using synthetic command arguments.
- The old Wasm command failed the normal-speed assertion. The corrected owner
  passes normal speed, slowdown, restoration, and speed-up cases with exact
  expected scalar values and duration/client checks.
- Build/run: CMake portable Emscripten target `cgame_timescale_tests`, then
  `node build/final-wasm-tests-2/tests/native/cgame_timescale_tests.cjs`.
- Native MSVC compilation was attempted, but standalone linking of this full
  cgame translation unit requires unrelated runtime symbols. The focused
  target is therefore Wasm-only; no native test pass is claimed.

This correctness fix is kept separate from the browser's long-frame policy.
