# Browser audio: reverb and EQ

Canonical SND owns aliases, channels, room selection, wet levels, EQ parameters,
fades and game timing. The Worker OpenAL proxy carries bounded device state to
Web Audio. Loaded and queued PCM share the same output graph. Device-played
position, source generations and queue ordinals own completion; see
[cinematic synchronization](cinematic-codec.md).

## Reverb ownership and dependency

`src/web/web_reverb.cpp` calls OpenAL Soft's `ReverbState` and stereo
`BFormatDec` in an AudioWorklet. Native and browser use the same 26 EAX presets
in `src/sound/snd_reverb_presets.h`; `SND_SetRoomtype` and
`SND_ApplyReverbSend` retain canonical behavior. This component owns buffers and
effect history only, with no native mixer/event thread or Wasm pthreads.

The ABI accepts four planar ACN/N3D wet channels and returns planar stereo,
with a 1024-float channel stride, 1–1024 frames and 8000–192000 Hz. Invalid
rooms and nonfinite input fail before entering the feedback network. Processing
allocates no new buffers. The worklet reserves a separate 32 MiB memory.

`scripts/web/reverb` pins OpenAL Soft 1.25.2 to
`b2c48f7718ef3fcf67921a8b6534c4914e328970`. It is LGPL-2.0-or-later;
retain its source, copyright and license notices. Generated `licenses.txt`
includes attribution, license and source/build instructions alongside Kisak's
GPL distribution. Distribute corresponding dependency sources and build scripts;
a link alone does not replace source obligations. No Miles binary is used.

`tools/build_web.ps1` invokes `tools/build_reverb.ps1` to produce
`reverb_dsp.mjs` with embedded Wasm because the worklet has no fetch API.
The Wasm-only `web_audio_log.cpp` retains formatted stderr and level filtering
without unused native streams/callback registration. Current aggregate product
size results belong in the [test inventory](web-test-inventory.md#current-execution-evidence).

The DSP uses `-msimd128 -msse2` to preserve native x86 `fastf2u` round-to-nearest;
the generic scalar path truncates and changes modulated reverb output.
Wasm SIMD is required for this component. `web_audio_fpu.cpp` replaces host
floating-point control-register handling: Wasm preserves IEEE subnormals, while
native OpenAL disables them. Both comparison probes use `Mix_C` for effect
output; the unused SSE1/MMX source mixer is excluded. Optional Clang 24
function-effects analysis is disabled for its consteval `_uz` false positive;
checked conversions, hardening and assertions remain enabled.

Source PCM branches after EQ into the shared reverb processor. Positional mono
and non-spatial stereo use native OpenAL Pairwise encoding. Wet gains and room
index are k-rate AudioParams so delivery is ordered with the audio graph;
MessagePort carries startup/error/shutdown only. Reset discards the tail and
prevents stale initialization from reconnecting. Initialization failure reports
a diagnostic and leaves dry playback available.

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

CMake FetchContent obtains the pinned public dependency on a clean checkout.
Bootstrap the tools first using the [README](../README.md#build). Sources,
binaries and synthetic traces belong in ignored build directories.

## Parametric EQ

`MSS_ApplyEqFilter` forwards two canonical stages of three bands each in native
order. Unchanged snapshots send no updates. SND retains enable flags, type,
gain, frequency, Q, save state and command/script updates; no parallel mixer or
engine EQ representation is introduced.

The device applies lowpass, highpass, lowshelf, highshelf and bell filters before
spatialization/gain and the reverb wet branch. Coefficients use the Q form of the
[W3C/RBJ cookbook](https://www.w3.org/TR/audio-eq-cookbook/), with gain in dB and
frequency in Hz at the AudioContext sample rate. IIRFilterNode preserves shelf
Q, which BiquadFilterNode ignores. Zero/Nyquist limits use constant gain.
Malformed snapshots and nonfinite/unstable coefficients fail atomically.
Signed PCM conversion divides both signs by 32768 to retain stereo symmetry.

Updates replace filters without restarting/rescheduling PCM; changed filters
reset their history. Pause/restart retains parameters, while stop, natural end,
source deletion and reset release nodes. `snd_enableEq=0` bypasses the chain;
its inherited disabled default remains unchanged. Native OpenAL has no EQ
implementation and cannot serve as its reference. Exact Miles coefficients and
update transients are unverified. Both PC Miles and this port retain `eqLerp`
without Xbox crossfade behavior; do not infer Xbox semantics for Steam.

## Recorded checks and remaining work

- Native/Wasm impulse comparisons cover all 26 presets at five rates: 130 cases,
  40,368,640 samples. Maximum absolute difference was `7.264316082000732e-8`,
  relative RMS `0.000015560795861818597`, within unchanged `2e-6` absolute and
  `1e-4` relative RMS limits. Validation also covers malformed input, room
  changes, stable buffers and block sizes 1, 17, 127, 128 and 1024.
- `tests/browser/audio_reverb.spec.mjs` covers real offline PCM, dry preservation,
  wet scaling, preset tails, stereo/positional/queued input, EQ-before-wet,
  live updates, stale generations and initialization/reset failures.
- Owned Killhouse selected `mountains`/0.3 naturally. The canonical console
  selected `cave` and faded wet to 0.75 with nonzero measured wet PCM, then
  restored the original room. This verifies device controls, not an authored
  room transition; one stream underrun was observed in the short check.
- Native/Wasm OpenAL-proxy and browser offline EQ checks cover all five filter
  families, six bands in series, Q, stereo symmetry, queued/live updates,
  32/44.1/48 kHz rates, endpoints, stale commands and atomic rejection.
  An owned Killhouse check explicitly enabled a -6 dB bell at 1 kHz on a playing
  source and verified bypass. Injected EQ settings do not qualify authored audio.

These are historical signal/device observations, not campaign or Steam audio
acceptance. Still needed: authored room/shellshock transitions, EQ activation
and audible update transients, callback cost under load, output latency and
matched native/Steam listening. DSP agreement with OpenAL does not prove Miles
parity. Dated runs and artifact identities remain in
[Git history](../README.md#historical-records).
