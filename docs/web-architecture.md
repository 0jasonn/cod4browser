# Web product architecture

## Runtime ownership

```text
launcher / install UI (main thread)
    -> versioned named messages
engine Worker + synchronous filesystem view
    -> canonical Kisak DB, game, cgame, renderer frontend
portable draw commands
    -> Worker-owned OffscreenCanvas / WebGL2
OpenAL-compatible commands
    -> main-thread Web Audio
```

The browser owns only platform boundaries: page lifecycle, file selection,
OPFS/IndexedDB coordination, Worker hosting, WebGL2, Web Audio, and input event
translation. `XAsset`, `XModel`, `Material`, `GfxWorld`, game/cgame, collision,
and script behavior remain canonical Kisak owners.

Imported fastfiles use the active canonical stream and generated loaders.
Missing required allocation blocks are rejected at the web stream boundary
before native cursor assertions. A bounded, state-owning mutation mode in the
existing DB test checks the native/Wasm stream, alias and publication trace.
Generated-load failure and bounds checks live in the XFile/stream owners;
diagnostic state cannot affect retry. Failed-zone cleanup selects that exact
zone, restores overrides and returns its pools before freeing PMem in reverse
allocation order. A companion test uses the real coordinator, PMem and Worker
DB scheduler for 80 partial RawFile failures/retries. The compiled world
singletons (`clipMap_t`, `ComWorld`, `GameWorldSp` and `GfxWorld`) are not
ordinary override pools, so the synchronous XFile publication transaction
retains each prior shallow owner record until commit. Forty-three failures
after a replacement singleton has published restore the previous body, name,
hash ownership, in-use state and PMem state before accepting a retry. Destructive
`R_UnloadWorld` and `Com_UnloadWorld` hooks stay deferred during rollback and
run once against the retired owner when a replacement commits. The same
boundary also covers a two-fastfile request: when the first file publishes a
replacement ClipMap and the second exhausts its `MapEnts` pool, both request
zones retire in one reverse-order release and the pre-request singleton is
restored. Broader request graphs and non-world device effects remain
qualification work.

The independent loader oracle now compiles the original, unchanged Win32
`db_load.cpp` translation unit with native headers and assertions, excluding
the adapted generated loader translation units. Its GPL synthetic fixtures
cover Material → Image dependencies and XModel → PhysPreset, including model
vertices, index and collision-leaf allocation blocks. The original loader,
adapted Win32 loader and adapted Wasm loader produce the same normalized
publication trace: every dependency's nine block offsets, active block/stack,
pool and zone, followed by final stream positions and alias identity. CTest
compares each with `tests/native/db_asset_oracle_expected.txt`, captured from
the original loader. Material ends at block 4 offset 248; XModel ends at
block 4/7/8 offsets 640/32/6. The stronger identity assertion corrected an
existing synthetic XModel alias which targeted the XAsset type word instead
of its header.

Each asset family also exercises eight replacement-zone failures after
publication, followed by successful retry and unload. A deliberately overlong
trailing RawFile causes the partial-zone failure; canonical DB transactions
restore published identities, prior geometry or Image resource handles, pools
and PMem. Image payloads change on retry and retire on unload. This is an
independent **loader** comparison, using the same canonical registry/PMem/DB
scheduler and portable bounds/resource services in all three harnesses. It
does not qualify the native D3D upload path, arbitrary malformed nested assets
or every native exception boundary. No proprietary fixture or shader bytecode
is included.
DB-owned script strings retain one real `scr_stringlist` user while any zone
or zone-0 default owns them. Releasing one of two zone owners preserves the
interned identity; releasing the last removes it. The last loaded zone no
longer invokes the whole DB-user shutdown while a live default asset still
owns its name. Native/Wasm tests cover shared zones, repeated default owners,
final-owner retirement, and a default image copy surviving zone release.
No production parser or asset representation was added.

At native `Load_Texture`, the browser now stores an opaque resource handle in
the existing `GfxTexture` union. It identifies retained encoded bytes at the
renderer boundary, never a second image identity or a WebGL object. Canonical
copies, overrides, defaults and texture aliases carry that handle. Reusing a
name cannot replace another image's payload. Completed loads and zone unloads
collect resources absent from all canonical image primary/override entries;
handles are never reused. The 256 MiB retained-source budget rejects admission
before copying instead of evicting live resources during a speculative load.
The existing eviction telemetry field stays zero. Forty failed image overrides,
successful retry, default-copy survival and rejection before an oversized read
pass in native/Wasm. Water dependencies follow `water_t::image`, matching the
native material mark walk. Broader authored-image and memory-pressure acceptance
remain separate from these boundary checks.

## Product protocol

Every request and startup handshake carries protocol version 1. Production
accepts only `init`, `mountAssets`, `flushAndUnmount`, `probeAsset`,
`checkpoint`, `submitCanonicalCommand`, `resize`, `input`, `runtimeStatus`, and
`shutdown`. Input uses the separate one-way `input-event` message: the host
validates and posts it without allocating an RPC request ID, while the Worker
validates it again before calling the canonical input boundary.
Character input has its own bounded `char` event and `CL_CharEvent` queue;
physical `event.code` input still reaches `CL_KeyEvent`. The latter owns native
gameplay repeat suppression and console/menu navigation, while layout-aware
`event.key` characters and Backspace repeat independently. Field insertion,
overstrike, cursor movement and validation remain in Kisak. The current text
adapter encodes Windows-1252, normalizes committed accents to NFC and drops
unrepresentable code points instead of truncating them into unrelated bytes.
The canvas's trusted mouse press focuses a one-byte browser-owned textarea
while pointer lock remains on the canvas. This gives the browser an editable
IME target; intermediate `Dead`/`Process`/composing keys stay outside Kisak and
the final `compositionend` text traverses the same byte adapter once. The sink
is outside tab order and clears its value after every commit.
Trusted browser `paste` events snapshot the first line as at most 4,095 of
those native bytes, install it in a Worker-owned platform cache, then queue
character 22. Canonical `Field_Paste` obtains its usual zone-allocated copy
from `Sys_GetClipboardData` and frees it through `Com_FreeEvent`. This avoids
the asynchronous permission API and preserves native console/chat editing;
menu edit fields retain their native control-character behavior. Served
Chromium verifies editable-sink focus alongside pointer lock and one-time
composition delivery. Real Windows IME candidate UI, non-Western code pages,
arbitrary programmatic clipboard reads and localized glyph qualification
remain open; the byte-oriented native fields are not UTF-8 fields.
The Worker validates payloads/ranges at its boundary and returns structured
errors. The host supplies timeouts, supports `AbortSignal` for non-mutating
probe/status work, avoids request-ID collisions after wrap, and rejects all
pending work on protocol, Worker, message, or shutdown failure.

## Storage and shutdown

Imported assets coordinate shared reads. Browser home data has an exclusive
Web Lock owner. Filesystem mutations enter one strict FIFO persistence chain;
path barriers plus object-identity and version checks prevent an old completion
from marking a replacement durable. Shutdown stops new work, persists dirty
open descriptors, awaits the mutation chain, closes imported handles, releases
leases, closes the asset-store BroadcastChannel and IndexedDB connection,
removes listeners, and terminates the Worker last.

Mount, checkpoint, unmount and shutdown use a progress watchdog: 15 seconds
without progress in diagnostics, 30 seconds in production, and a separate
five-minute absolute cap. Successful synchronous file reads during native
runtime mount report cumulative bytes from the filesystem adapter; they can
reach the host while Wasm is busy loading, without a Worker timer or changes
to canonical loading. The observer ends on mount success or failure. Shared
transport code throttles reports and accepts only advancing counters or phase
changes for the current request, operation and Worker generation. Duplicate,
malformed and retired progress cannot renew the stall deadline; progress never
renews the absolute deadline. Ordinary RPC reply deadlines remain fixed.

## Input coordinates and lifecycle

Canonical `UI_Component::MouseEvent` treats `x >= screenWidth` or
`y >= screenHeight` as outside, so absolute browser coordinates are pixel
indices clamped to `0..width-1` and `0..height-1`. CSS coordinates are scaled
to canvas backing pixels, including device-pixel ratio. Zero-sized resize
transients emit no absolute event. Relative motion is frame-coalesced, but its
pending callback and deltas are cancelled on blur, hidden visibility,
controller disposal, or fatal transport failure; held keys and buttons still
receive their release events while delivery remains available.

## Graphics controls

Graphics controls cross the renderer boundary through native dvars. Anisotropy
minimum/maximum, forced mip filtering and filtering disable use shared
`R_SetTexFilter` policy; WebGL owns extension capability clamping and texture
parameters. Runtime changes now cover 2D textures, sky/reflection cubemaps,
the model-lighting volume and animated water. Reflection probes use native
`R_SetReflectionProbe` sampler 0x72; model lighting uses `RB_InitCodeImages`
linear/no-mipmap/clamp-UVW policy. Normal/specular/detail controls gate existing material shader
features. The renderer applies the native 20-token technique-set feature policy
after atomic zone publication and whenever the relevant dvar signature changes.
It selects available `sm`/`hsm`, normal, specular, detail, z-feather, outdoor
and tweak variants through canonical DB identities, then resolves leading-comma
aliases to the selected target. Missing variants retain the source set. WebGL2
selects the hardware-shadow token because its shadow path uses depth textures.
Native `R_SetPicmipForMemory` and `Image_GetPicmip` select color,
normal and specular mip levels. The existing IWI/load-definition decoder selects
authored levels before allocation/upload, respecting native no-picmip and small
IWI exemptions and preserving bounds checks. Retained encoded sources remain
complete; recovery reuses the selected mip recipe. Native D3D supplies actual
memory inputs to the shared policy. WebGL exposes no VRAM capacity, so Auto
uses the existing 800 MiB per-pool admission limit and a 1 GiB planning input,
preserving default full quality; these are policy inputs, not hardware readings.
Manual quality survives `vid_restart` and a fresh production runtime with owned
Killhouse files. The shipped `r_applyPicmip` menu action is registered at the
browser renderer boundary and queues canonical `vid_restart`; that restart
rebuilds the retained WebGL texture recipes at the new authored mip levels.
Broader shader-family and multipass material qualification
remain open. Display resolution, gamma and MSAA retain
their existing consumers; monitor refresh/presentation cadence belong to the
browser. Normal/specular/detail retain native non-archived dvar semantics.
Owned paused Killhouse specular/normal toggles change pixels and restore their
baseline; actual context recovery preserves both disabled controls and the
captured frame. The narrow paused view does not qualify authored material
equivalence with the original game.

The native cheat dvar `r_texFilterMipBias` now reaches material, sky and
alpha-tested shadow sampling through GLSL fragment bias; explicit reflection
LOD also adds it. WebGL has no sampler LOD-bias parameter. GLSL's implicit
sampling bias is subject to `MAX_TEXTURE_LOD_BIAS`; the native registered range
and cheat/non-archived semantics are unchanged. Single-level postprocess and
movie images retain their only available level. Synthetic coloured-mip pixels
verify positive/negative bias, fractional trilinear interpolation, unchanged
single-level sampling and context recovery. This proves sampling behavior,
not original-game image equivalence. See the
[GLSL ES 3.00 texture functions](https://registry.khronos.org/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf),
[ES 3.0 LOD rules](https://registry.khronos.org/OpenGL/specs/es/3.0/es_spec_3.0.pdf)
and [D3D texldl sampler bias](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/texldl---ps).

## Particle-cloud and soft-particle boundary

Cloud quad expansion remains a renderer adapter over canonical
`GfxParticleCloud`. Shared native axis policy now supplies the two view-space
rows. The adapter projects the radius-scaled endpoint direction, converts
native view X through `-refdef.axis[1]`, and applies each row to `UV - 0.5`.
This corrects doubled dimensions, horizontal reflection and incorrect directed
stretching. Native signed-threshold fallback and minimum projected length are
preserved. Placement transforms affect particle centers only. Native/Wasm
corner checks and an unmodified native function oracle verify the arithmetic;
they do not establish Steam visual fidelity. Native and web now call the same
cell-position helper for the 8x8x16 cloud lattice. Web registration consumes
three CRT samples per cell in native x/y/z order, maps a wider CRT result into
the original 32,768 inclusive buckets, and retains the 1,024 centers for the
renderer lifetime. Renderer re-registration regenerates the lattice after the
client seed, matching native initialization and restart ownership. CRT
algorithms differ across runtimes, so equal seeds do not imply equal centers.

The canonical world command now retains `GfxWorld::outdoorImage` and
`outdoorLookupMatrix` atomically. `$outdoor` follows the existing generated-image
resource path, including context reconstruction. Cloud expansion writes the
unexpanded center into its otherwise-unused normal slot, avoiding another vertex
format or intermediate object. The backend selects the authored outdoor pass
only for its exact seven binding roles, samples the lookup with native sampler
state `0x62`, and keeps the inclusive `center height >= lookup height` test by
zeroing alpha below the stored height. A recognized outdoor-only cloud is skipped
when its world lookup is unavailable. Native/Wasm boundary tests and synthetic
GPU pixels cover the matrix, sampler columns, threshold equality, masking,
colour and actual context restoration. An encountered owned authored scene and
matched Steam appearance remain qualification work.

Recognized z-feather passes now use canonical shader arguments for intersection
and near-camera fading, additive colour, fog, angle falloff and eye offset.
The backend follows native `TECHNIQUE_BUILD_FLOAT_Z`: lit/decal geometry writes
signed linear depth to a separate, non-MSAA target before opaque, point-light
and emissive draws. Native alpha-tested depth passes multiply texture and vertex
alpha. Viewmodel depth retains its negative sign independently of depth-range
compression. IEEE float bits are stored losslessly in nearest-sampled RGBA8,
avoiding a mandatory float-render-target extension; dithering and blending are
disabled for this pass. Existing geometry and canonical slot-1 state are reused.
The target is allocated when recognized soft particles or distortion need it,
subject to `r_floatz`, with resize, memory accounting and recovery owned by the
renderer. `r_zFeather` controls both soft-particle fades independently of
distortion's depth test. Unknown and multipass shaders remain outside this
compatibility subset.
The temporary inspection command is removed from source and rebuilt artifacts;
owned shader bytes and disassembly remain private under ignored `build/`.

The 2026-09-05 material milestone implements the encountered `vertcol_mul_fog`
technique. Publication telemetry in owned Killhouse, CargoShip and AC130 found
it on respectively 4, 11 and 6 distinct stain/decal materials (8, 30 and 6
material/technique selections). All selected records previously used only
`mul.hlsl`; the authored second pass is `mul_fog.hlsl`. Native bytecode inspected
locally specifies a vertex-alpha-controlled framebuffer multiplier followed by
an additive fog-color correction. Both passes sample the canonical `colorMap`
at sampler 0; texture alpha is unused. The second vertex program interpolates
fog color multiplied by `max(1 - exp(-density * (distance - start)), 0)`.
World/view matrices and fog constants remain canonical renderer inputs.

The boundary recognizes both shader pairs and all exact argument roles,
including matrix rows, fog registers, image identity and both state entries.
It reuses the existing uploaded image/sampler and geometry for both draws;
there is no second material, texture allocation or shader translator. Each
pass runs over the complete contiguous material group, following
`R_RenderDrawSurfListMaterial` in `rb_backend.cpp`; visibility holes are
preserved. World, static instances and moving brush submission share this
scheduling rule. The existing state decoder applies each pass's authored
blend, alpha-write, depth and culling state. Material caches reset after the
second pass, including before a neighboring group. Other multipass families
and generic texture fallbacks remain unqualified.

Verification uses nine synthetic RGBA samples evaluated by D3D9 HAL with the
encountered original vertex/pixel programs: first pass alone, half fog, disabled
fog, fog start, zero/full vertex alpha, overlapping ranges, disabled second-pass
RGB writes, and changed texture alpha. WebGL2 matches within one UNORM step
before and after a real `WEBGL_lose_context` loss/restoration. Shader bytes,
disassembly and the local native runner remain under ignored `build/`; the
committed fixture contains only repository-authored synthetic data (GPL-3.0).
Native and Wasm world/static boundary targets pass with assertions enabled.
This work discovered that the world target had lost its Release assertions;
its CMake target now explicitly undefines `NDEBUG`. The browser graphics and
dynamic-light tier passes nine tests. A fresh diagnostic run renders all three
owned worlds; Killhouse restores resource generation 1 to 2 while preserving
its 8,475 surfaces and 823,464 indices. Stationary headless observations do not
establish matched native appearance or campaign acceptance.

The inspected `distortion_scale_zfeather_dtex` pass now projects the canonical
normal/tangent basis with authored scale and vertex red/green controls; vertex
blue tints the sampled scene and alpha controls opacity. A separate RGBA8
snapshot follows native `RESOLVED_POST_SUN` ordering: lit/decal, sun and point
lighting finish before the copy; emissive draws then sample it. Displaced depth
in front of the particle selects the original screen coordinate. This uses
the existing signed FloatZ target and supports an MSAA scene. The copy is
submitted with `glFlush`: the Chromium fixture otherwise loses its deferred
MSAA snapshot when the source is cleared. No readback or GPU completion wait
is needed in production. `r_distortion` skips native resolved-post-sun groups,
including unknown shader families; enabling it does not make unknown programs
supported. Disabling the latched `r_floatz` while requiring distortion reports
the missing prerequisite, matching native's inability to draw that pass.
Matched original/native distortion and soft-particle appearance remain open.

Context callbacks now register the Worker canvas in Emscripten's event-target
table as well as its GL canvas table. The two APIs use different lookups; the
missing event mapping left real loss unhandled. Diagnostic loss hooks now call
`WEBGL_lose_context` instead of directly invoking recovery handlers. The
graphics tests and the owned paused graphics-control scene pass actual
loss/restoration; older captures using the former hook establish resource
reconstruction only.

## Audio device feedback

Canonical SND channel and alias state remain in Wasm. Web Audio samples source
offsets and completed PCM buffers from `AudioContext.currentTime`, then sends
validated `audio-playback` snapshots to the Worker. The proxy no longer infers
completion from its own wall clock. Source generations reject feedback from
before play/pause/seek/reuse; absolute queue ordinals keep late replies aligned
after unqueue. Device admission failures explicitly stop the logical source.

The host samples at 25 ms and admits one snapshot until the Worker acknowledges
it. This bounds feedback during synchronous engine stalls; position is sampled,
not sample-accurate, and includes message/frame latency. Context suspension
freezes device time. Cinematic video follows cumulative played PCM, preserving
unqueued duration; one decoder-owned pending frame provides audio ahead of its
display time. Silent/device-failure movies retain a wall-time fallback. Hardware
output latency, arbitrary audio-tail layouts and long/background recovery
remain qualification work.

## Build products

`KisakCOD-web` and `build/web/site` are production. With
`KISAK_WEB_DIAGNOSTICS=ON`, `KisakCOD-web-diagnostics` and
`build/web-diagnostics/site-diagnostics` expose browser-only test controls and
telemetry. Both artifacts compile the same runtime sources; diagnostic exports
cannot enter production.

Source releases use `git archive --format=zip` from the exact binary revision,
then `tools/check_source_archive.py`. Versioned export exclusions remove legacy
Bink/Miles/Steam SDK trees and compiled binaries. CI checks this archive;
local native dependencies and inherited Git history remain unchanged. The
product checker rejects every directory and any unexpected file in the flat
site, in addition to its exact application exports and diagnostic checks.
The native reference CI job disables runtime DLL copying and retains build
checks without uploading the inherited `bin` directory. Local native setup
and its dependencies remain available; it is not a source of release packages.

The 2026-09-05 Release gate passes the existing budgets; the prior increase
proposal is retired. At base `9612ab4c` plus this audit's fullscreen/recovery,
size and material changes:

| Artifact | Actual bytes | Unchanged budget |
| --- | ---: | ---: |
| Engine Wasm | 3,189,283 | 3,332,379 |
| All JavaScript | 313,992 | 357,646 |
| Complete site | 3,604,419 | 3,701,082 |

All 24 raw exports, nine exact application exports and 22 allowed files pass.
The engine SHA-256 is
`8e8319bedd9b72266f13779ba09ddf45a6c4311505a060583b154382abd6565b`.

The source-built FFmpeg decoder, OpenAL reverb and license texts remain shipped.
The worklet's native logger imported C++ streams and a second formatter; the
browser stderr adapter preserves formatted errors and reduces `reverb_dsp.mjs`
from 385,509 to 59,211 B, retaining its embedded Wasm. The native/Wasm 130-case
DSP comparison passes. No bytes were moved between size categories.

Non-Debug web compilation and linking now use `-Oz`, retaining the established
selective LTO and exception handling. Engine Wasm falls from 3,758,563 to
3,186,181 B before the material expansion. Link-only `-Oz` measured 3,653,764 B; whole-program LTO instead
increased it to 4,536,299 B and was reverted. The [current foreground production
measurements](evidence/browser-frame-time-2026-09-02.md#current-production-measurements--2026-09-05)
verify bounded running, game/audio clocks and recovery; CargoShip remains slow.
They do not isolate the performance effect of `-Oz` from older workloads.
Runtime assertions, source ownership and native engine build options are unchanged.

Release builds run pinned Terser 5.39.0 over copied host modules, preserving
module exports/property names and using no compression transforms. Emscripten's
engine and DSP output are excluded to preserve exact export inspection. Run
`npm.cmd ci` before building. Readable corresponding source remains in `web/`;
the minifier is a build dependency and adds no shipped library.

CI records the boundary outcome, runs independent browser checks, and requires
the boundary to have succeeded before uploading the production site. A size
failure still fails the overall job. This workflow change has been inspected
locally; remote CI execution remains unverified.
