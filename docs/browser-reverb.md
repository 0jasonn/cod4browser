# Browser reverb device boundary

Status: the existing OpenAL Soft reverb DSP now runs in an AudioWorklet connected
to browser game playback. `SND_SetRoomtype` and `SND_ApplyReverbSend` carry the
canonical room and wet send through the current Web Audio device. Native/Wasm
differential checks and browser PCM checks pass. Campaign or Steam audio parity
is not claimed.

`src/web/web_reverb.cpp` directly calls OpenAL Soft's `ReverbState` and stereo
`BFormatDec`, using the same 26 EAX presets as the native Kisak sound driver.
The preset array now lives in `src/sound/snd_reverb_presets.h`; native room
selection and parameters are unchanged. This component owns audio buffers and
effect history only. SND continues to own aliases, room selection, wet levels,
fades, channels, and game timing.

The device ABI accepts four planar ACN/N3D wet channels and returns two planar
stereo channels. Each channel has a 1024-float stride; processing admits 1–1024
frames. Initialization admits 8000–192000 Hz, invalid room indices are rejected,
and non-finite input is rejected before it reaches the feedback network. The
process call allocates no new buffers. It does not open an OpenAL device, start
a mixer/event thread, or use Wasm pthreads.

## Dependency and numeric behavior

The isolated CMake project at `scripts/web/reverb` pins OpenAL Soft 1.25.2 to
`b2c48f7718ef3fcf67921a8b6534c4914e328970`, the native reference's current
dependency. Its upstream source and copyright/license notices remain intact.
OpenAL Soft is LGPL-2.0-or-later. The generated site's `licenses.txt` includes
its license, attribution and source/build instructions alongside Kisak's GPL
distribution. No proprietary Miles or other game binary is used.

The first scalar Wasm comparison failed in modulated presets. OpenAL's
`fastf2u` deliberately allows platform rounding differences: native x86 uses
round-to-nearest, whereas the generic path truncates. This changes the reverb
LFO step and audibly relevant sample output. The isolated Wasm component uses
Emscripten's supported SSE2 compatibility path (`-msimd128 -msse2`) to retain
the native numeric behavior. See the official
[Emscripten SIMD documentation](https://emscripten.org/docs/porting/simd).
This requires Wasm SIMD support for this audio component. Failed module or
processor initialization reports a diagnostic and leaves existing dry playback
available.

`web_audio_fpu.cpp` replaces only OpenAL's host floating-point control-register
save/restore: WebAssembly has no writable SSE control register. Native tests
retain the upstream implementation. Wasm keeps IEEE subnormal behavior;
native OpenAL disables subnormals. The differential tolerance covers numeric
variation, not different effect parameters. The unused SSE1/MMX source mixer
is excluded from this dedicated component; both probes use OpenAL's default
`Mix_C` for the effect output. This library configuration is not a general
browser replacement for the full OpenAL device API.

Clang 24's function-effects analysis reports the consteval checked 64-to-32-bit
`_uz` literal conversion as a potential blocking call. The dependency's
optional analysis flag is disabled for this build. Checked conversions, STL
hardening, and test assertions remain enabled as before.

## Reproduce the differential check

Use the pinned toolchains from `tools/web_toolchain.json` and
`tools/native_toolchain.json`. From a PowerShell at the repository root:

```powershell
$env:EM_CONFIG = (Resolve-Path .tools/emsdk/.emscripten).Path
$reverbCmake = '.tools/emsdk/cmake/4.2.0-rc3_64bit/bin/cmake.exe'
$reverbToolchain = (Resolve-Path .tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake).Path
$reverbNinja = (Resolve-Path .tools/emsdk/ninja/1.13.2_64bit/ninja.exe).Path
& $reverbCmake -S scripts/web/reverb -B build/reverb-wasm -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$reverbToolchain" "-DCMAKE_MAKE_PROGRAM=$reverbNinja" -DCMAKE_BUILD_TYPE=Release
& $reverbCmake --build build/reverb-wasm --target web_reverb_tests --parallel 4
node build/reverb-wasm/web_reverb_tests.cjs build/reverb-wasm/impulse.f32

$reverbNativeCmake = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
& $reverbNativeCmake -S scripts/web/reverb -B build/reverb-native -G 'Visual Studio 18 2026' -A Win32 -T version=14.51.36231 -DCMAKE_SYSTEM_VERSION=10.0.28000.0
& $reverbNativeCmake --build build/reverb-native --config Release --target web_reverb_tests --parallel 4
& build/reverb-native/Release/web_reverb_tests.exe build/reverb-native/impulse.f32
node tools/compare_reverb.mjs build/reverb-native/impulse.f32 build/reverb-wasm/impulse.f32
```

The recorded run reused the already checked-out public dependency via
`-DFETCHCONTENT_SOURCE_DIR_OPENAL=<absolute build/native-sp-text/_deps/openal-src>`
after verifying its exact Git commit. A clean checkout otherwise uses CMake
FetchContent. All sources, binaries, and synthetic sample traces stay in
ignored build directories. The fresh-fetch path has not been exercised here.

## Standalone DSP evidence, 2026-09-02

- Native Win32 MSVC 14.51.36231 and Wasm Emscripten 6.0.6 Release tests passed
  all 26 presets at 8000, 44100, 48000, 96000, and 192000 Hz. Node 24.18.0 ran
  the Wasm test, not a browser.
- Tests cover initialization/shutdown, invalid rates/rooms/block sizes,
  non-finite input rejection, live room changes, stable buffer addresses,
  and block sizes 1, 17, 127, 128, and 1024.
- 130 impulse cases, 40,368,640 samples: largest absolute sample difference
  `7.264316082000732e-8`; largest per-case relative RMS error
  `0.000015560795861818597`. The original limits remained `2e-6` absolute
  and `1e-4` relative RMS throughout diagnosis.
- Native test executable SHA-256:
  `c50752fc68096eea7c4662dce55caca2c4e4077c4bfc8acf0a7cf58d20094ddc`.
  Wasm test SHA-256:
  `4dccc1a77e6583402718a30132fb049e84669947f84db8c788c196cdf6a33640`.
- Native `KisakCOD-sp` rebuilt successfully with the shared preset header.
- Existing diagnostic browser smoke: 12 passed on isolated port 8171, using
  bundled Chromium 149.0.7827.55 headless.
  Remainder: 45 passed, six optional retail skips on port 8172. Retail
  environment variables were cleared. These test the existing game artifact;
  they do not establish browser reverb playback.
  Diagnostic Wasm SHA-256:
  `a1b6ff63c8e30eef3b4386a0de21e7fa6f0988b5f1257243c18530085c2b92b1`.
  The production artifact was not changed in this milestone; its previously
  reported cinematic size-budget failure remains open.

Logs: `build/goal-reverb-{native,wasm}-test.log`,
`build/goal-reverb-comparison.log`, `build/goal-reverb-native-sp-build.log`,
`build/goal-reverb-smoke.log`, and `build/goal-reverb-remainder.log`.

## Browser integration

`tools/build_web.ps1` invokes `tools/build_reverb.ps1`, using the pinned public
dependency and Emscripten toolchain. It produces `reverb_dsp.mjs` with embedded
Wasm for the worklet, which has no fetch API. The component reserves 32 MiB,
separately reported by audio telemetry; it adds no game-thread shared memory.

The existing source PCM branches after its EQ filters into a shared reverb
processor. Mono positional encoding and non-spatial stereo encoding follow the
native OpenAL Pairwise convention. Per-source device gains and the room index
use k-rate AudioParams so their delivery is ordered with the audio graph. An
earlier MessagePort implementation could render offline PCM before receiving
its wet gain; the regression exposed that ordering defect. MessagePort now
carries only startup/error/shutdown messages. Reset closes the processor and
discards its tail; stale asynchronous initialization cannot reconnect it.

`tests/browser/audio_reverb.spec.mjs` checks real offline browser output: dry
preservation, wet scaling, distinct preset tails, stereo anti-phase preservation,
positional input, queued PCM, EQ before the wet send, live wet changes without
restarting PCM, generation rejection, failed module loading and reset during
startup. All three cases passed three repeated runs in Chromium 149.0.7827.55
on isolated port 8175 (`build/goal-reverb-browser-narrow-3.log`).

Owned Killhouse passed the production test `@retail-reverb` in headless Chrome
152.0.7977.65 on port 8179. The real level selected `mountains` (index 17), with
wet level 0.3 on dialogue and environmental sources; excluded ambient/cinematic
sources retained zero wet. The existing `snd_setEnvironmentEffects level`
console command selected `cave` and faded wet to 0.75, with intermediate SND
updates observed. A separate analyser on the wet output measured nonzero PCM
energy (`0.007347049202795963` across 2048 samples). The test then restored
`mountains`/0.3 and verified that state. No alias, PCM or mission was injected.
This qualifies device controls, not an authored room transition or audible
Steam comparison. The run reported 6,052,008 decoded PCM bytes, 33,554,432 DSP
memory bytes, one stream underrun, zero overruns/evictions and no reverb or page
errors. Callback cost and the stream underrun remain outside this short check.

The original test incorrectly used the reserved `shellshock` priority;
`EndShellShockSound` clears it every frame without an active shock. The fixture
now uses the level console API. A subsequent restoration check caught a wrong
preset name (`forest` is index 15); it was corrected to `mountains`, index 17.
Canonical sound behavior and assertions were preserved.

Retail log: `build/goal-reverb-retail-4.log`; private observations:
`test-results/8179/retail_ui_persistence-prod-942ec-ect-playing-Killhouse-audio-chromium/reverb-evidence.json`.
The final fixture also passed on isolated port 8183, explicitly requiring a
wet value strictly between the starting 0.3 and target 0.75; see
`build/goal-reverb-retail-final.log` and the corresponding `test-results/8183`
observations. This is a repeated device check, not additional campaign coverage.
Production Wasm SHA-256:
`feb8f46646b67e38177dd505bc517ca707b761dfd96087177cffde08a9153209`.
Diagnostic Wasm SHA-256:
`02f781a2236a0241c56b4ac81aafb2534299356c20c3ae11b03061775cb9d69d`.
Both sites use reverb module SHA-256:
`b38ba0db7efe17c84a010b49667073dbd5888931e40b1910374666fb25ff71be`.

The unchanged production size gate **fails**: main Wasm 3,708,294 bytes against
3,332,379; all JavaScript (including embedded DSP Wasm) 759,076 against 357,646;
site 4,565,437 against 3,701,082. The DSP module accounts for 385,509 bytes.
The exact product file and application-export checks passed before the size
failure. No budget was raised. See `build/goal-reverb-product-boundary.log`.

Integration verification also passed native and Wasm OpenAL-proxy tests, the
native SP rebuild, production/diagnostic Release builds, static checks and
83 Node protocol tests. Routine browser checks used bundled Chromium
149.0.7827.55: smoke 12 passed (port 8180); remainder 48 passed, six optional
retail skips (8181); production 43 passed (8182). Retail environment variables
were cleared for those runs. Logs are `build/goal-reverb-integrated-{smoke,
remainder,product,protocol}.log` and `build/goal-reverb-static-final.log`.
The exhaustive browser duplicates were not rerun.

The next fidelity checks are authored room/shellshock transitions during actual
gameplay, callback cost under load, and audible comparison with native Kisak and
the original Steam game. Native OpenAL DSP agreement does not establish Miles
parity or completed campaign behavior.
