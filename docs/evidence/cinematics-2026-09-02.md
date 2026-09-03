# Cinematic playback — 2026-09-02

## Behavior and ownership

The browser now plays imported Bink 1 movies behind the existing Kisak
`R_Cinematic_*` API. `web_cinematic.cpp` uses the canonical filesystem, a
source-built FFmpeg decoder, the existing retained UI image path, and the
OpenAL/Web Audio device queue. The canonical client still handles the
`cinematic` command and Escape skipping; cgame/scripts retain their requests
and subsequent actions. No gameplay, mission, route or objective model was added.

The import boundary admits bounded `main/video/*.bik` files from the selected
owned installation. Existing browser copies need a new selection to add movies.
Missing movies remain an explicit omission. Decoder failures report failure
instead of inventing successful decoded frames. See [build, licensing and
limits](../cinematic-codec.md).

An existing OpenAL proxy defect was exposed: each `alGenSources` call restarted
allocation at source 1 and overwrote live sources. Allocation now preserves live
IDs and fails atomically when full. The device has 54 sources: 53 canonical game
channels and one movie track. Canonical SND channel counts are unchanged.

## Executed checks

- Production and diagnostic Release builds, including the strict canonical
  runtime-prefix check: pass, pinned Emscripten 6.0.6.
- `npm.cmd run check:web:static`: pass. `npm.cmd run test:protocol`: 83 pass,
  Node 24.18.0 / npm 11.16.0.
- `web_openal_proxy_tests`: pass in host-native MSVC and Wasm/Node, including
  live-source preservation, exhaustion and reuse. Native tests use the Visual
  Studio CMake recorded in their build cache, not the Emscripten CMake binary.
- `KisakCOD-web-cinematic-check`: synthetic malformed-header rejection passes.
  Optional owned `killhouse_load.bik` decode produces all 1,121 frames,
  duration 37.404071 s, 3,302,400 interleaved audio samples, luma FNV 2783495107.
  Final standalone Wasm decode took 2.369 s in Node; this is not gameplay timing.
- Routine Chromium smoke: 12 pass, 7.2 s, diagnostic port 8164.
- Non-overlapping Chromium remainder: 45 pass, six optional retail skips,
  17.2 s, diagnostic port 8165. Includes movie-path admission, native-DLL
  exclusion, malformed movie rejection, and missing-movie completion.
- Final production browser suite: 43 pass, 10.7 s, port 8018.
- Owned diagnostic movie check: one pass, about 1.1 min including import,
  persistent Chrome 152.0.7977.65, port 8167. The real intro renders changing
  frames, submits PCM to a running audio context, holds playback position while
  paused, resumes, completes naturally, then starts again and accepts native
  Escape skipping. Captured frames were visually inspected.
- Owned production movie check: one pass, about 1.0 min including import,
  persistent Chrome 152.0.7977.65, port 8168. The shipped console command
  starts the movie, the `$cinematic` audio source starts in the production
  audio backend, captured frames change, and natural completion arrives after
  37,405.845 ms. No diagnostic application facade is present. Captured frames
  were visually inspected.

Routine runs explicitly cleared inherited retail variables. Retail runs were
separate and used the locally owned installation pinned in
[the reference inventory](steam-reference-2026-09-02.json), on the same
7800X3D / RTX 3070 Ti host. No original/native game was launched for this check.
The first retail attempt used an incognito test context and hit its storage
quota before import; it was stopped and replaced by the established persistent
retail-profile setup. The product's quota check was preserved.

Private logs are under `build/goal-cinematic-*`; private captures are under
`test-results/8167/` and `test-results/8168/`. The latter includes
`production-cinematic-evidence.json`. Retail media is not in versioned fixtures.

## Artifact identity and remaining gate

Production Wasm SHA-256:
`b69750088e0054a456716f25145bda93e26caa9c3cfa3fcc9989ca19316414a7`.

Diagnostic Wasm SHA-256:
`a1b6ff63c8e30eef3b4386a0de21e7fa6f0988b5f1257243c18530085c2b92b1`.

The production boundary check fails its existing size budgets. They have not
been raised or disabled:

| Artifact | Current bytes | Existing budget |
| --- | ---: | ---: |
| Wasm | 3,707,608 | 3,332,379 |
| JavaScript | 363,485 | 357,646 |
| Site, including license texts | 4,145,261 | 3,701,082 |

The 24 raw Wasm exports and nine application exports remain within the existing
closed boundaries. The site adds only `licenses.txt` to its closed file list.
This feature therefore has working playback evidence but does not pass the
current release-size gate.

## Limits

This is movie playback, not authored campaign completion or Steam parity.
The first campaign chapter remains unverified end to end. No mission ledger
entry is promoted. Human listening and three-way audiovisual comparison have
not been performed. Synchronization during stalls/suspension, audio-context
recovery, context loss during movies, loop qualification, in-world cinematic
materials, and authored mission transitions remain open. The first adapter
supports the owned PC installation's zero/one-track movies; surround-track
mixing is rejected. Nearest chroma sampling can differ from native filtering.
