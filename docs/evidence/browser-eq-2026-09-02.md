# Browser parametric EQ device — 2026-09-02

## Behavior and ownership

`MSS_ApplyEqFilter` now forwards both canonical EQ stages, three bands each,
through the existing Worker OpenAL proxy. SND still owns channel selection,
enable flags, type, gain, frequency, Q, save state and command/script updates.
The device receives only a bounded source snapshot in native stage/band order.
Unchanged snapshots produce no messages, including the per-frame update loop.

The existing Web Audio output graph applies lowpass, highpass, lowshelf,
highshelf and bell filters before the existing spatializer/gain path. Loaded
and queued PCM share the filters. Updates replace filter nodes without
restarting buffer sources or rescheduling queued audio. Pause/restart retains
the parameter snapshot; stop, natural end, source deletion and device reset
release filter nodes. Disabling `snd_enableEq` removes the chain.

Coefficients use the Q form of the [W3C/RBJ Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/),
with native gain in dB and frequency in Hz evaluated at the AudioContext's
sample rate. IIRFilterNode supports shelf Q, which BiquadFilterNode ignores.
Zero/Nyquist limits use constant-gain nodes. The boundary rejects malformed
snapshots and nonfinite/unstable coefficients before replacing a live chain.
No new mixer, alias model, gameplay state or engine EQ representation exists.

The stereo regression exposed an existing PCM conversion defect: positive
samples were divided by 32767 while negative samples used 32768. Both now use
32768, matching [OpenAL Soft's signed sample conversion](https://github.com/kcat/openal-soft/blob/master/core/converter.cpp).
Opposite input samples remain opposite after filtering; the assertion was
retained unchanged rather than tolerating the conversion bias.

## Verification

- Host Win32 Release and Wasm/Node `web_openal_proxy_tests` pass, including
  invalid Q/type rejection and normal sixth-band admission.
- Browser offline PCM tests cover all five filter families, center gain,
  out-of-band attenuation, Q resonance (including shelves), all six stages in
  series, stereo sign preservation, queued-buffer continuity, live updates,
  32/44.1/48 kHz contexts, endpoint cutoffs, stale commands and atomic rejection.
- A diagnostic probe executes the real `MSS_ApplyEqFilter` with synthetic
  canonical parameters. Both stages arrive in order, unchanged updates are
  suppressed, and disabling EQ sends a complete bypass. The probe refuses an
  initialized retail sound runtime and is absent from production exports.
- The focused diagnostic audio run passed all 7 cases in Chromium
  149.0.7827.55. Native SP OpenAL Release links with the pinned toolchain;
  the already running native reference was left alone.
- Static checks and all 82 protocol tests pass. Product Release and diagnostic
  Release builds and runtime-prefix checks pass.
- Final Chromium 149 runs: 43 production tests (port 8018), the offline EQ PCM
  test against the production site (port 8148), 12 diagnostic smoke and 43
  remainder tests (port 8146), with five optional retail skips. Browser suites
  ran separately; inherited retail variables were cleared for synthetic runs.
- The owned Chrome 152.0.7977.65 Killhouse check enables EQ through the canonical
  console, enumerates canonical entchannels, and sets stage 1/band 2 to a
  -6 dB bell at 1 kHz. A playing `blackhawk_engine_low` source has an active
  filter with magnitude 0.5011872053 at 1 kHz and a running AudioContext.
  Disabling EQ removes every source's filters without a device rejection.
  These are injected settings, not a claim about authored mission audio.
  The complete owned menu/input/profile/save/Continue/Quit/restart test passed
  in 2.3 minutes on port 8147. Its game/wall probe was 3700/3692.75 ms, a
  regression check only, not an active-campaign performance qualification.

Production Wasm SHA-256:
`a4b70604ef7dd7381874b36cbffb9f6460e91406508f78af883b20cee4918332`.
Diagnostic Wasm SHA-256:
`c5dc7256462db0f4e7315cce3d35d9ddfcb3fb8864e91ec2adbd0cd5378d64d3`.
Unchanged product budgets pass: 3,319,929 B Wasm, 354,523 B total JavaScript,
3,685,504 B site, 24 raw / 9 application exports and 19 files.
Private logs/captures stay under ignored `build/goal-eq-*` and
`build/eq-retail-results`.

## Remaining fidelity work

The native OpenAL branch still has no parametric EQ implementation. Its output
cannot serve as an EQ reference. Miles' exact DSP coefficients and its update
transients have not been compared against these standard filters. IIR updates
reset filter history; unchanged frames retain nodes and history. Qualify audible
transients during authored sweeps before claiming continuous-update parity.

The inherited `snd_enableEq=0` default remains unchanged: upstream disabled it
for a Miles crash. This device milestone verifies explicit enable/bypass;
default activation and existing profile values still require a reference-backed
product decision. Both PC Miles and this port retain `eqLerp` state without
applying the Xbox crossfade behavior; do not infer Xbox semantics for Steam.

Room reverb/wet sends, original-game listening comparison, cinematics and
audio/video synchronization remain open. The owned check does not promote
campaign completion, active-campaign performance or retail-fidelity status.
