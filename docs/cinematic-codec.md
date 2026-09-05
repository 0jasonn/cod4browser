# Browser cinematic codec

The existing Kisak `R_Cinematic_*` calls remain the cinematic control boundary.
The web implementation reads user-imported `main/video/*.bik` files through
Kisak's filesystem and uses FFmpeg 8.0.3 for Bink 1 decoding. It publishes
canonical Y/Cr/Cb/A code images and queues PCM through the existing OpenAL/Web
Audio device adapter. Game scripts and client code own requests,
skipping, subtitles, and subsequent mission actions.

`tools/cinematic_codec.json` pins the public FFmpeg source archive and the GNU
make build tool by SHA-256. `tools/bootstrap_cinematic_codec.ps1` verifies these
archives and builds under ignored `.tools/` and `build/ffmpeg-bink/`. Git for
Windows Bash and the pinned Emscripten toolchain are required. The normal web
build invokes this step. The configure command in `tools/build_cinematic_codec.sh`
enables only the Bink demuxer and Bink video/RDFT/DCT audio decoders; networking,
programs, other codecs, assembly, and threads are disabled.

FFmpeg is copyright its contributors and this configuration is LGPL-2.1-or-later.
It is linked into the GPL-3.0 Kisak program. Source files and notices are preserved
unmodified in the verified archive. A distributor must provide the corresponding
Kisak and FFmpeg sources, build scripts, and applicable license notices with the
distribution; the pinned upstream archive link alone is not a substitute for
that obligation. The generated site includes the GPL and LGPL texts in
`licenses.txt`. Public source archive: https://ffmpeg.org/releases/ffmpeg-8.0.3.tar.xz.
No original Bink DLL or movie is included in the build or repository.

Current limits: BIKi import headers, 1920×1080, 1–120 fps, one hour, 512 MiB per
file, and zero or one mono/stereo audio track. All 53 files in the pinned owned
English PC installation have zero or one audio track. Surround-track mixing is
rejected. Previously imported profiles need a new folder selection to add movies.
Header admission is not a promise that damaged frame data will decode.

The OpenAL proxy now receives actual Web Audio source position and processed
buffer counts instead of estimating them from Worker wall time. Static and
queued synthetic PCM waits through delayed delivery and AudioContext suspension
in served Chromium. Feedback carries source generations and absolute queue
ordinals, with one snapshot in flight, so late replies cannot retire a reused
source or the wrong buffer prefix. Movie time now sums played PCM, including
buffers already unqueued. One pending video frame lets the decoder queue its
preceding audio before display time without copying another frame queue.
Display/subtitle time advances when that frame is presented, and completion
waits for both the video duration and queued audio duration. Movies without
audio, or with a failed audio device, retain the wall-time fallback.

Native runs the loading movie on its cinematic thread while the main thread
loads the map. The browser now keeps the native `SV_SpawnServer` ordering:
`CL_MapLoading` starts the movie and opens `briefing`, then map loading proceeds
while `Sys_LoadingKeepAlive` presents the movie and native UI between assets
and compressed-file refills. It never pumps game frames, commands, or input
inside an unfinished database transaction. Native pregame holds gameplay and
starts the queued in-game fade when the intro finishes or the player skips.
The former `nextmap` movie-first replay is removed.

Loading keepalive uses Emscripten Asyncify with one platform-owned
`emscripten_sleep(0)` yield and a 1 MiB unwind stack. This is a deliberate
exception to the bootstrap's no-Asyncify preference: the former production
Killhouse sequence measured 2,722 ms of map loading after the 37,624-ms intro.
A yield lets OffscreenCanvas present and receives device-played audio feedback
without splitting canonical DB allocation/publication or adding pthreads.
The ordinary frame pump is guarded against re-entry; Worker RPCs await the
suspended stack, while validated audio feedback only updates JS device state.
No cross-origin-isolation or new browser feature requirement is introduced.
The measured Release Wasm increases from 3,189,365 to 5,330,129 bytes
(about 67%; 1,848,062 bytes gzip). This cost includes Asyncify instrumentation
and the loading/registration fixes; gameplay performance remains a separate
qualification.

The native map-zone progress reset is restored. The bar reads
`DB_GetLoadedFraction`, measuring compressed fastfile work. External IWI pixels
are renderer-registration resources in this port, so the DB denominator excludes the
native D3D external-image allocation estimate; no guessed completed bytes or
movie timer is used to fill the bar. A full DB bar precedes the remaining
canonical game/cgame setup and graphics registration; the pregame gate still
owns readiness. `R_EndRegistration` prepares the existing world/static-model
geometry, material images and WebGL objects while the intro plays. This uses
canonical world lighting without inventing a camera or advancing gameplay.
Per-frame camera visibility and dynamic geometry remain in `R_RenderScene`.
Texture uploads yield between completed images while replacement resources
remain private until atomic publication. Both launchers batch log-panel layout
once per animation frame so verbose registration cannot starve movie/audio
delivery on the page thread.

On 2026-09-05 the owned Killhouse map path passed in headless Google Chrome
152.0.7977.77: the intro started 29 ms after the command, loading plus graphics
registration finished 7,692 ms into the 37,464-ms movie, the queued fade started
12 ms after completion, and the first game-driven renderer frame appeared
56 ms after completion. The early capture shows the stock bar filling from
native DB progress; pregame hides it when ready. A separate run confirms native
Escape skip starts the queued fade after loading. This demonstrates loading,
rendering and transition behavior, not human gameplay or audiovisual fidelity.

Owned Killhouse movie playback now waits through held audio delivery and actual
AudioContext suspension, resumes, and survives WebGL context loss/restoration
in Chromium 149. A 900-ms blocked page thread followed by 150 ms of recovery
advances video by 433 ms in the planar-material run, consuming queued PCM
before waiting for more delivery.
This is platform synchronization/recovery evidence; human
listening, hardware output latency, arbitrary audio-tail layouts and matched
native/Steam audiovisual comparison remain unqualified.

The renderer now uses the canonical `cinematic` material instead of a private
RGBA material. The observed single-pass `cinematic.hlsl` family binds code
samplers 22�25 to Y/Cr/Cb/A. World, brush, static-model, DObj and UI draws resolve
those images at draw time, so a new movie frame does not rebuild scene geometry.
Four retained R8 images preserve decoder plane dimensions and linear chroma
filtering. The shader applies the native limited-range coefficients, or the
codec's full-range conversion, then vertex/UI colour and alpha. CPU nearest
chroma expansion is removed. Inactive images use native black/gray/gray/black
defaults; movies without alpha use an opaque alpha plane.

Recovery restores these images even without an active 2D scene, including
frames uploaded while WebGL is lost. World retirement clears their references; repeated idle stop calls preserve
the already published inactive textures.
Native/Wasm tests cover canonical material selection and malformed bindings;
served Chromium pixel tests cover colour, tint, alpha, odd dimensions, padded
strides, linear chroma sampling, frame replacement and context recovery.

Remaining fidelity evidence includes long/background stalls, device replacement,
native/Steam colour comparison, authored in-world movie surfaces and campaign
transitions. The targeted material/backend evidence does not qualify a TV or
other authored in-world scene. Manual gameplay acceptance remains with the user.

The optional `KisakCOD-web-cinematic-check` CMake target builds the standalone
Wasm decoder check. Run its `.cjs` output with Node for synthetic rejection tests;
an optional local filename additionally decodes a caller-owned movie completely.
No retail input is copied into fixtures.
