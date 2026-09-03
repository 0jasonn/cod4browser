# Browser cinematic codec

The existing Kisak `R_Cinematic_*` calls remain the cinematic control boundary.
The web implementation reads user-imported `main/video/*.bik` files through
Kisak's filesystem and uses FFmpeg 8.0.3 for Bink 1 decoding. It uploads frames
through the existing retained UI image path and queues PCM through the existing
OpenAL/Web Audio device adapter. Game scripts and client code own requests,
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

Remaining fidelity evidence includes synchronization under stalls, audio-context
suspension, background recovery, native/Steam color comparison, in-world movie
materials, and authored campaign transitions. CPU chroma conversion currently
uses nearest chroma samples; the native filtered planar shader can differ at
color edges. Fullscreen playback is a platform milestone, not campaign completion.

The optional `KisakCOD-web-cinematic-check` CMake target builds the standalone
Wasm decoder check. Run its `.cjs` output with Node for synthetic rejection tests;
an optional local filename additionally decodes a caller-owned movie completely.
No retail input is copied into fixtures.
