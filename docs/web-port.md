# Browser port status

The browser port starts from KisakCOD upstream commit
`1c03702cbe176e9274e486d295edcd035b3c2b5f` (7 August 2026). The local
`web-port` branch retains upstream history and tracks `upstream/master`.

## Milestone 0: browser bootstrap (complete)

The `KisakCOD-web` target is deliberately separate from the existing SP, MP,
dedicated-server, and Radiant targets. Those native targets currently include
Win32, Direct3D 9, Winsock, Steam, Miles, and Bink before their platform seams
are broad enough to replace safely.

The bootstrap proves four things without requiring retail game files:

1. C++20 builds and starts as WebAssembly.
2. Existing ODE physics math from `src/physics/ode/odemath.cpp` runs correctly
   in Wasm.
3. Emscripten creates a WebGL2 context and a minimal shader pipeline.
4. A non-blocking browser main loop renders frames and reports state to a
   hand-authored launcher.

This is a renderer/platform foothold, not yet a playable COD4 build.

## Milestone 1: browser system and headless engine slice (complete)

The bootstrap now has an explicit browser system boundary in
`src/web/web_system.cpp`. It preserves the engine-facing monotonic
`Sys_Milliseconds`/`Sys_MillisecondsRaw` and `Sys_Print` contracts while the
web-owned frame pump schedules exactly one engine callback per browser
`requestAnimationFrame`. It does not spin, sleep, enable Asyncify, or block the
browser event loop.

The Wasm target also compiles a deliberately narrow, platform-neutral slice of
the existing command and dvar design:

1. `src/qcommon/cmd_core.cpp` provides command registration, tokenization,
   quoted strings and comments, the 64 KiB command buffer, case-insensitive
   dispatch, and frame-based `wait` behavior through the existing `cmd.h` API.
2. `src/universal/dvar_core.cpp` provides a fixed registry and owned string
   values. It supports `set` and direct dvar query/set commands without storing
   pointers in integer fields.
3. Browser initialization executes quoted, escaped, commented, and
   case-insensitive synthetic commands. A queued `wait` sequence completes on
   RAF tick 2, proving that command-buffer work advances across browser frames.
4. Structured system and engine events expose the monotonic clock, pump ticks,
   and dvar results to the launcher and Playwright smoke test.

This is not the full `qcommon` runtime. The upstream `cmd.cpp`, `dvar.cpp`, and
`common.cpp` translation units still directly depend on Win32 synchronization,
the native filesystem, database/fastfile loading, scripts, client/server code,
and renderer integrations. Those dependencies were not hidden behind broad
stubs. The current slice is single-threaded, asset-free, and intentionally has
no `exec`, config persistence, client/server command forwarding, or arbitrary
JavaScript command bridge.

## Milestone 2: browser filesystem and legal asset import (complete)

The launcher now lets the user select the root of a legally owned Call of Duty 4
installation. It prefers the File System Access API and falls back to a portable
directory input. The native picker requests only `localization.txt` and
`main/iw_00.iwd`; the fallback normalizes relative paths, rejects absolute or
traversing paths and case-folded duplicates, then retains only those two files.
The application never receives or stores a host path.

`web/asset_store.mjs` is the asynchronous browser filesystem boundary. It:

1. streams the two allowlisted files into a randomly named staging directory in
   origin-private file system (OPFS) storage instead of loading an IWD into
   JavaScript or Wasm memory;
2. keeps the schema-versioned active manifest in IndexedDB and swaps that pointer
   only after both staged files have been closed, reopened, size-checked, and
   probed;
3. leaves an existing active import intact when a replacement fails and removes
   obsolete or interrupted staging data during startup, replacement, and clear;
4. exposes bounded `stat` and `read` operations for the next engine filesystem
   slice; and
5. restores and revalidates the active import on reload without reopening the
   picker.

Selected files receive the same bounded probes before the potentially large IWD
copy and again after OPFS reopen. OPFS, IndexedDB, and Web Locks are all required
so imports and clears are serialized safely across tabs; a `BroadcastChannel`
refreshes launcher state in other open tabs. The launcher requests durable
storage during the selection gesture and warns when the browser grants only
best-effort, evictable storage.

`src/web/web_asset_probe.cpp` is the C++/Wasm side of that boundary. The
localization probe enforces the native 4 KiB buffer limit, valid UTF-8/control
characters, a supported language marker, and a non-empty payload. The IWD probe
receives only a 4 KiB head, the final 65,557 bytes, and a 4 KiB central-directory
window. It independently validates a single-disk ZIP32 envelope, central-directory
bounds, the first local/central entry pair, encryption flags, and compression
method. A normal retail IWD of roughly 168 MB therefore never becomes a Wasm
allocation.

This selection-time IWD check remains deliberately structural. It is not a
retail-file hash or ownership check, and it does not decompress a member. The
complete bounded central-directory audit and representative-member CRC checks
described below run only after a staged import has committed, so picker
validation still never allocates the complete archive in Wasm.

The launcher publishes `kisakcod:assets` state separately from engine/runtime
state, so a missing, rejected, or evicted local import does not crash the
asset-free frame loop. It also offers an explicit removal action that deletes
only the browser copy; the selected Steam installation is always read-only.

## Milestone 3: portable IWD reader and asynchronous Wasm I/O (complete)

`src/web/web_filesystem.cpp` and `web/filesystem_bridge.mjs` now provide an
explicit request/response seam between C++ and browser storage. C++ submits
bounded `stat` or read requests through one of eight generation-safe slots; a
read is limited to 64 KiB and its destination buffer is owned by that C++
request. JavaScript opens an immutable source for the active OPFS import under a
shared Web Lock, then revalidates the import identity and recorded size on every
read. A clear or replacement makes an older source stale instead of silently
mixing bytes from two imports.

Browser Promises only queue completion metadata. `WebFs_PumpCompletions`
delivers callbacks from a later animation frame, and cancellation retires the
matching JavaScript operation token before its C++ buffer can be reused. The
bridge also reacquires the current Wasm heap view after every `await`, which is
required because memory growth may replace that view. The path does not use
Asyncify, synchronous OPFS access, or a blocking engine call.

`src/qcommon/iwd_archive.cpp` is the portable, storage-independent ZIP32/IWD
reader on top of that seam. It locates a bounded terminal EOCD, walks every
central-directory record, validates the corresponding local header, and
incrementally verifies stored and raw-deflate members with exact compressed and
uncompressed byte counts plus CRC-32. The default limits are 4,096 records, a
512 KiB central directory, 255 bytes per path, 1 MiB of cumulative path data,
32 MiB compressed and uncompressed per member, and 512 MiB total declared
uncompressed data. Record and cumulative budgets include duplicate records, so
duplicates cannot be used to evade them.

The reader rejects multi-disk and ZIP64 archives, encryption, data descriptors,
unsupported flags or compression methods, malformed extra fields, unsafe or
traversing paths, size/range inconsistencies, incomplete streams, excess
output, and CRC mismatches. Exact duplicate paths deterministically use the
final central-directory record, while distinct names that collide under ASCII
case-folding are rejected. Deflate uses Emscripten 6.0.6's pinned, maintained
zlib 1.3.2 port through `-sUSE_ZLIB=1`; it does not link the repository's legacy
native zlib copy.

`src/web/web_archive_job.cpp` proves the complete path without blocking a frame.
It reads the archive tail and central directory, then mounts the parsed index
for the engine filesystem described below. Representative stored and deflated
members are verified through that service in discard mode, so verification
retains only one 64 KiB output scratch buffer. The final ready payload is
assembled at no more than 64 archive entries per frame and dispatched as one
unchanged `kisakcod:archive` event only after the complete index is present.

## Milestone 4: bounded engine filesystem and one IWI asset (complete)

`src/web/web_engine_filesystem.cpp` turns the validated `main/iw_00.iwd` index
into a narrow, read-only engine service. It owns one immutable mount and accepts
one explicit asynchronous request at a time. Verification discards decoded
output, while a read-all request may retain at most 4 MiB plus a convenience NUL
terminator. Local headers and compressed input are still fetched in chunks no
larger than 64 KiB. Each frame performs at most one decoder call with no more
than 64 KiB of input and 64 KiB of output, so a high-ratio deflate stream yields
between decoded chunks instead of expanding its complete payload in one RAF.
Completion is delivered from the browser frame pump, and the decoded byte cache
is valid only for the synchronous callback; request state and retained capacity
are released before any later engine or JavaScript event is published. The web
target keeps Emscripten exception catching enabled so its bounded allocation
guards produce explicit failure states instead of aborting the runtime.

High-level request IDs are independent from the lower browser I/O slots. A
successful cancellation suppresses the high-level callback, and a stale OPFS
source invalidates the complete mount because its locator and index describe a
single import generation. Each completion preserves both browser-filesystem
and archive-decoder error codes. This is intentionally not a compatibility
implementation of the historical synchronous `FS_*` API, a seekable handle,
or a general extraction cache.

`src/qcommon/iwi_image.cpp` is the portable parser used by the first engine
asset consumer above that service. It parses the serialized 28-byte Call of
Duty 4 IWI v6 header with explicit little-endian reads, validates the supported
format range, signed dimension bounds, complete-file and selectable-picmip size
metadata, ordering, and mip-count semantics, and never invokes the legacy
Direct3D image loader. At Milestone 4 this stopped before raw pixel decode; the
Milestone 5 boundary below adds a separate, narrow portable conversion. The
browser job deterministically
selects the ASCII-case-insensitive first bounded `images/*.iwi` member instead
of hard-coding a proprietary filename. Its `kisakcod:engine-asset` result copies
only parsed metadata and confirms that the request cache has been released;
renderer recovery pixels are reported separately.

Malformed or absent IWI data does not invalidate an otherwise valid archive.
Automated fixtures cover stored and raw-deflate IWI members, delayed reads with
continued animation frames, a maximum-size high-ratio stream spread over many
frames, malformed IWI and local headers, missing and oversized members,
cancellation, stale-mount recovery, import replacement, and the absence of late
old-generation completion. `tests/fuzz/asset_parsers_fuzz.cpp` provides a
native libFuzzer entry point for the IWD framing/streaming decoder and IWI
parser; the portable unit suite also runs 10,000 deterministic synthetic parser
mutations.

## Milestone 5: renderer-owned IWI texture (complete)

`src/qcommon/iwi_image.cpp` now exposes a deliberately narrow portable RGBA8
decode boundary above the metadata parser. It accepts only CoD4 IWI v6 format 1
(named ARGB by the engine and serialized as BGRA), exactly the no-mipmap flag,
a two-dimensional depth of one, and one tightly packed mip. Width/height
multiplication, the 28-byte header addition, the complete payload length, and
the shared 4 MiB member ceiling are checked before allocation. Conversion
swizzles BGRA to RGBA and replaces the caller's output only after the complete
operation succeeds. Other valid IWI formats, flags, dimensions, and layouts
produce explicit non-fatal renderer-texture states instead of reaching WebGL.

`src/web/web_renderer.cpp` owns the WebGL2 context, shaders, backend vertex
objects, texture objects, resize state, draw calls, and context callbacks behind the
backend-neutral interface in `src/web/web_renderer.h`. Shared and engine-facing
code passes only a bounded RGBA8 descriptor; no `GLuint`, Emscripten context
handle, or Direct3D type crosses that interface. The renderer limits this first
slice to WebGL2's guaranteed 2048-by-2048 floor, atomically uploads with nearest
filtering, retains at most 4 MiB of CPU pixels, and releases the callback-scoped
engine-filesystem copy. At this milestone the backend-owned bootstrap triangle
sampled the uploaded image as the first visible texture consumer; Milestone 6
below moves that geometry across the renderer boundary.

On WebGL context loss every stale GPU handle is discarded. The shader pipeline,
bootstrap geometry, and texture are recreated from renderer-owned CPU state after
restoration, and `kisakcod:renderer-texture` reports residency, retained bytes,
upload/resource generations, and recovery count without exposing pixel data or
backend handles. At the M5 boundary, a valid compressed or otherwise unsupported IWI remained a successful
`kisakcod:engine-asset` metadata result while its renderer-texture state is
`unsupported`; malformed payload layout and backend upload failure are reported
separately and do not invalidate the mounted archive or stop the RAF pump.

Portable tests cover exact BGRA-to-RGBA conversion, aliased input/output,
unsupported format/flag/dimension slices, layout and overflow limits, atomic
failure, and the exact allocation boundary. Browser fixtures prove stored and
deflated upload paths, continued RAF progress during asynchronous reads, exact
synthetic framebuffer colors, graceful DXT1 deferral at that milestone and backend-dimension deferral,
bounded recovery ownership without a second filesystem read, cancellation-safe
synchronous event re-entry, and identical texture colors after context
restoration. A forced initial shader failure also proves that a partial renderer
cannot publish a false recovery after context loss. All fixtures remain synthetic
and contain no retail data.

## Milestone 6: engine-owned indexed surface (complete)

This section records the Milestone 6 baseline. Milestone 7 below supersedes its
three-vertex bootstrap fixture and exact 84-byte/6-byte upload counts with a
converted four-vertex world-surface quad; the renderer ownership contract remains.

`src/web/web_renderer_surface.h` defines the first backend-neutral surface
contract: a fixed position-2/color-3/UV-2 vertex, 16-bit indices, one
triangle-list draw range, and a neutral choice between vertex color and the
validated engine-image binding. The public types contain no WebGL, Emscripten,
or Direct3D objects. The callback-scoped descriptor is limited to 4096 vertices,
12288 indices, and 139264 retained bytes. Portable validation rejects empty or
over-limit arrays, unknown topology or texture intent, misaligned and overflowing
draw ranges, non-finite vertex components, and every out-of-range index before
the renderer copies or uploads data.

`src/web/web_engine_surface.cpp` now owns the deterministic three-vertex,
three-index synthetic surface and submits it before graphics initialization.
`WebRenderer_SetSurface` validates and copies both arrays atomically into bounded
renderer recovery storage, so no engine pointer survives the call and a failed
replacement cannot disturb the active surface. The WebGL2 backend creates a
private VAO, VBO, and element buffer from that copy, issues
`glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 0)`, and samples the existing
renderer-owned IWI texture only because the draw descriptor requests the neutral
engine-image binding. The earlier shader-authored vertex wobble was removed, so
the submitted engine coordinates determine the synthetic surface apart from
viewport aspect correction.

Surface lifetime is independent from archive and IWI generations. Cancelling or
replacing an imported image releases only its texture recovery pixels; the
engine surface remains resident and falls back to its vertex colors. On context
loss the renderer invalidates every private handle, then recreates the shader,
VAO/VBO/element buffer, and current texture from the latest renderer-owned
descriptions before reporting the runtime as resumed. The
`kisakcod:renderer-surface` event publishes only counts, bounded byte ownership,
draw intent, residency, and submission/resource/recovery generations; it never exposes
vertices, indices, pointers, or GPU handles.

Portable tests cover the valid surface contract, null and empty descriptors,
count ceilings, subtraction-safe draw ranges, topology and texture-binding
validation, NaN/infinity rejection, index bounds, caller-independent copies,
atomic invalid replacement, and result strings. Browser
tests prove the exact 84-byte vertex and 6-byte index uploads, indexed draw
parameters with no `drawArrays` fallback, continued rendering, resource
recreation without an engine resubmission, and non-resumption after a forced
element-buffer recovery failure. The existing synthetic IWI framebuffer test
also proves all four exact texel colors before and after combined surface and
texture recovery without another filesystem read.

## Milestone 7: authentic engine-world surface conversion (complete)

`src/web/web_engine_world_surface.h` introduces a D3D-free conversion boundary
shaped after the geometry actually consumed by the upstream world renderer. Its
`WebEngineWorldVertex` is layout-checked against the 44-byte `GfxWorldVertex`
shape, including xyz, packed color, base texture coordinates, lightmap
coordinates, and packed normal/tangent fields. `WebEngineWorldSurfaceRange` is
the 16-byte `srfTriangles_t` shape, retaining signed `firstVertex` and
`baseIndex` fields so malformed native descriptions are rejected before they
become offsets. No Direct3D declaration, WebGL handle, raw array, or pointer is
published through the browser lifecycle event.

`WebEngine_ConvertWorldSurface` converts exactly one surface-local slice from
the shared world vertex and 16-bit index arrays. It bounds-checks non-zero
`firstVertex` and `baseIndex` ranges with subtraction-safe arithmetic, accepts
only the upstream base/unlayered vertex format, and verifies every index against
the selected surface's vertex count. Upstream indices remain local to
`firstVertex`, so the converted renderer indices are copied unchanged instead
of being incorrectly treated as global world-vertex indices. Conversion is
atomic and retains the existing renderer ceilings of 4096 vertices and 12288
indices.

World xyz is reduced to the current renderer's two-dimensional clip position by
an explicit two-row affine projection. This keeps projection on the engine side
and avoids hiding a camera, perspective divide, or clipping policy inside the
WebGL backend. Native `GfxColor` is decoded numerically as `0xAARRGGBB` using
shifts, independent of host byte order; RGB and the base texture coordinates
then populate the existing position-2/color-3/UV-2 renderer vertex. Layer data,
lightmaps, packed normals/tangents, materials, and their shader techniques are
not interpreted by this slice.

`src/web/web_engine_surface.cpp` exercises that path with a freely generated
GfxWorld-shaped fixture. Sentinel data surrounds a four-vertex, six-index quad,
and both source slices begin at non-zero offsets, proving the surface-range and
local-index conventions rather than feeding a pre-packed renderer array. The
converted quad is submitted through the Milestone 6 surface seam and can sample
the existing bounded engine image. Renderer-owned converted vertices, indices,
and texture pixels survive WebGL context loss; restoration recreates private
VAO, VBO, element-buffer, and texture resources without another conversion or
filesystem read.

Portable checks cover layout and range assumptions, non-zero source slices,
local-index validation, packed-color and UV conversion, affine projection,
base-only format gating, output limits, non-finite values, allocation-safe
atomic failure, and result strings. Browser checks observe only scalar
`kisakcod:engine-world-surface` metadata, verify the four-vertex/six-index
conversion reaches the indexed draw, and retain the existing context-recovery
and exact synthetic framebuffer coverage.

At the Milestone 7 boundary this was an authentic representation conversion,
not a fastfile extraction. Milestone 8 below supersedes that part with a strict
synthetic `GfxWorld` decoder, but no `.d3dbsp`, retail surface, or retail
material is loaded, and no visibility, lightmap, layered vertex, shader, or
material-technique path is implemented. At that boundary the legal importer was
limited to `localization.txt` and `main/iw_00.iwd`; all automated geometry and
asset fixtures remain freely generated synthetic data.

## Milestone 8: fastfile/zone inventory and extraction contract (complete)

Milestone 8 inventories and implements the first bounded fastfile extraction
path. `src/web/web_fastfile_world_surface.cpp` reads an unauthenticated
`IWffu100` version-5 prefix and zlib stream, validates `XFile`, its nine logical
blocks, generated asset traversal and pointer fixups, and returns one owned
`GfxWorld` surface plus its material name and source metadata. The complete
evidence and serialized contract are in
[`fastfile-zone-inventory.md`](fastfile-zone-inventory.md), with exact upstream
file and line references.

The inventory establishes that fastfile pointers are 32-bit little-endian wire
tokens rather than portable pointers. It separates presence-only arrays, direct
block offsets, inline assets, inserted alias slots, and alias lookups; records
the alignment rule that advances a logical block without consuming compressed
padding; and fixes the relevant 732-byte world, 44-byte vertex, 48-byte surface,
16-byte triangle-range, eight-byte material-memory, and 80-byte minimal material
layouts. The first boundary retains only a bounded material name/key. Material
techniques, textures, shaders, lightmaps, visibility, and D3D runtime fields are
not part of it.

The resulting implementation is deliberately strict: one synthetic asset table
containing one inline `GfxWorld`, one base/unlayered surface, one material record
and alias, and nonzero storage only in blocks 0 and 4. Every unrelated nonzero
pointer or count is rejected because it changes traversal order. Default limits
are 4 MiB for the complete file; 8 MiB each for inflated bytes, an individual
logical block, and cumulative logical blocks; 65536 world vertices; 262144 world
indices; 4096 selected vertices; 12288 selected indices; and 255 material-name
bytes before NUL. All arithmetic, cursor movement, fixups, strings, floats, and
surface-local indices validate before the owned result replaces its destination.

`src/web/web_engine_surface.cpp` constructs a complete freely generated
fastfile, compresses it with zlib, calls the extractor, converts its result, and
submits it through the existing renderer seam during browser boot. The fixture
preserves the Milestone 7 offset quad: six shared vertices and twelve shared
indices surround a selected four-vertex/six-index surface whose raw
`firstVertex` is 1 and raw `baseIndex` is 3. Its block sizes are 812 and 372
bytes, the shared material alias is block-4 offset 40 (`0x40000029`), the
inflated stream is 1238 bytes, and declared zone memory is 1184 bytes. Browser
lifecycle metadata reports the source framing, block sizes, raw surface range,
material-reference kind/name, extraction generation, and converted counts.

This is a completed synthetic implementation, not a general fastfile decoder.
At the Milestone 8 boundary the extractor ran synchronously during boot. Although
the complete `XAsset` table is available first, inline bodies and dependencies
are interleaved afterward in generated-loader order and carry no generic byte
length. A world following an unsupported asset cannot be located safely without
traversing that earlier asset. General or user-owned fastfiles remain rejected;
no retail `.ff` or `.d3dbsp` is read, no browser importer capability changed,
and synthetic success must not be described as retail compatibility.

## Milestone 9: resumable strict extraction (complete)

Milestone 9 makes the Milestone 8 decoder resumable while deliberately retaining
the same strict single-asset format. `WorldSurfaceExtractionJob::Begin` takes
ownership of one complete source allocation. `Step` advances its inflate or
traversal stage and reports only the work performed by that call. Each call is
hard-capped at 64 KiB of compressed input, 64 KiB of inflated output or traversed
bytes, and 64 completed semantic records; zero or over-limit caller budgets are
rejected without consuming source or traversing a record and put the job in its
failed terminal state.

The browser runtime builds the same freely generated fixture, starts the job,
and calls exactly one `Step` from `WebEngineSurface_Frame` for each RAF callback.
It emits bounded per-step and cumulative progress before conversion and renderer
submission, so `loading` and `ready` are distinct browser-frame states. The
synchronous `ExtractWorldSurface` entry point remains available for portable
callers but now drives the same job until it reaches a terminal state rather
than maintaining a second parser.

Parsing never writes caller output. After success, `TakeResult` performs one
atomic move of the owned surface and can succeed only once; an early, failed, or
repeated take leaves the destination untouched. The job owns its complete
compressed source and inflated traversal staging while work is in progress and
releases both when extraction succeeds, before result handoff. This is resumable
CPU work; the runtime calls `Reset` to release a failed job's staging. It is not
asynchronous browser-file streaming or authorization to feed the decoder
user-owned files.

The logical arena bookkeeping also now matches the upstream block-0 asymmetry.
Every block has a checked current cursor and high-water mark. Popping a block-0
loader frame restores its entry cursor because block 0 is temporary storage,
while other blocks retain their advances. Declared sizes are reconciled against
high-water use, not a stale assumption that every terminal cursor must equal its
declared size. For Milestone 9's then-current single-asset fixture, block 0
reaches an 812-byte high-water mark before rewinding and block 4 advances
persistently through 372 bytes.

This milestone does not add a second asset, a generic skip mechanism, delayed
payload support, retail formats, or importer access. Its result is still exactly
one bounded base/unlayered `GfxWorld` surface with one zero-dependency material.

## Milestone 10: two-asset traversal and zone stream machine (complete)

Milestone 10 replaces the current synthetic input contract while preserving the
Milestone 8 and 9 history above. The accepted top-level table contains exactly
two complete `XAsset` records and is staged in block 4 before either body is
visited:

1. `ASSET_TYPE_MATERIAL` (`0x04`) with inline-shared token `0xfffffffe`;
2. `ASSET_TYPE_GFXWORLD` (`0x10`) with inline token `0xffffffff`.

Dispatch remains table-ordered. The zero-dependency 80-byte material body and
bounded name are parsed first. Before its block-0 frame rewinds, the decoder
copies the name to job-owned storage, assigns one stable job-local material
identity, and resolves the inserted alias cell at block-4 offset 16. The world
then reuses block 0 from offset zero. Its `MaterialMemory` handle and selected
surface both carry `0x40000011`, the one-based encoding of that block-4 cell,
and must resolve to the same identity. No serialized or native pointer escapes
the job, and this narrow identity mapping does not construct the native global
asset registry.

For `web/synthetic`, the current logical layout is exact:

```text
block 0: 732-byte declared/high-water extent, terminal cursor 0
    [  0,  80) top-level Material, then rewind
    [  0, 732) GfxWorld, then rewind

block 4: 380-byte persistent extent
    [  0,  16) XAsset[2]
    [ 16,  20) material alias cell; no compressed bytes
    [ 20,  34) "web/synthetic\0"
    [ 34,  58) twelve u16 indices
    [ 58,  60) alignment gap; no compressed bytes
    [ 60,  68) MaterialMemory[1]
    [ 68, 332) six GfxWorldVertex records
    [332, 380) one GfxSurface
```

The inflated traversal order is `XFile` (44), `XAssetList` (16), `XAsset[2]`
(16), `Material` (80), material name (14), `GfxWorld` (732), indices (24),
`MaterialMemory` (8), vertices (264), and surface (48): 1246 bytes total. The
active block extents are 732 and 380 bytes, so the fixture declares 1112 zone
bytes. Alias reservation and the two-byte alignment gap affect arena extent but
do not occur in the compressed stream.

`web_fastfile_zone_stream.*` now owns the portable nine-block state machine.
It models immediate reads in blocks 0 and 4 through 8, block-1 zero fill,
block-0 frame rewind with a retained high-water mark, persistent cursors in the
other blocks, checked alignment, and bounded push/pop frames. Delayed blocks 2
and 3 are represented by checked `{block, offset, length}` spans and replayed
FIFO. Because no checked-in generated loader selects those blocks, their replay
contract is exercised by a separate synthetic stream-machine microfixture, not
by inventing records in the two-asset zone.

The 64 KiB and 64-record per-step ceilings remain. Limits now also cover total
input, inflated output, cumulative and per-block arena bytes, two assets, stack
depth, delayed span count and bytes, total string bytes, and alias count before
allocation or publication. The accepted prefix remains deliberately exact:
wrong order, a non-shared material, an undefined/duplicate/misaligned alias, an
unsupported dependency or preceding type, a third asset, nonzero script
strings, record truncation, or block-size disagreement fails atomically.

This is still a freely generated synthetic contract. A read-only audit of a
legally owned Steam installation established that its ordinary PC fastfiles use
the expected unauthenticated version-5/zlib framing and that its IWI population
includes compressed DXT formats. That observation proves framing only. No
proprietary asset was copied into or committed to this repository, the importer
does not accept `.ff` files, and Milestone 10 does not establish retail asset or
map compatibility.

## Build

The toolchain is pinned in `tools/web_toolchain.json`. On Windows, the checked-in
scripts install it locally without changing the permanent user environment:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\bootstrap_web_toolchain.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\build_web.ps1 -Configuration Release
python .\tools\serve_web.py --directory .\build\web\site
```

Open `http://127.0.0.1:8000`. Do not open `index.html` through `file://`; Wasm
must be served with the `application/wasm` MIME type. The server intentionally
serves only the generated site directory, never the repository root. Keep that
terminal open while using the page and press Ctrl+C to stop the server. After
the first setup, only the build and serve commands are needed.

Node 24.18.0 and npm 11.16.0 are pinned in `.node-version` and `package.json`.
Install the test dependencies and Chromium with:

```powershell
npm.cmd ci
npx.cmd playwright install chromium
npm.cmd run test:browser

# Substantial browser-platform milestones:
npm.cmd run test:browser:full

# Diagnostic browser rerun of cases already covered by native/Wasm tests:
npm.cmd run test:browser:all
```

The portable archive, IWI, renderer-surface, engine-world conversion, fastfile
traversal, and zone-stream unit tests can be built separately from the native
game targets. Windows builds use KisakCOD's bundled zlib 1.1.4; other native
platforms require a zlib development package:

```powershell
cmake -S . -B build/portable-tests -A Win32 -DKISAK_PORTABLE_TESTS_ONLY=ON
cmake --build build/portable-tests
ctest --test-dir build/portable-tests -C Debug --output-on-failure
```

Those unit tests exercise stored and deflated streams, ZIP framing and local
header agreement, limits, duplicate-name policy, path validation, truncation,
size mismatches, output bounds, incomplete streams, CRC failure, IWI header and
dimension validation, file-size metadata, bounded RGBA8 layout and conversion,
surface descriptor limits and index validation, bounded world-surface range and
format conversion, exact two-asset traversal, nine-block cursor/delayed-replay
semantics, and deterministic mutated inputs. The same test target can be
configured with Emscripten and run under Node, in which case it uses the same
pinned zlib port as the browser target.

A native Clang/libFuzzer build is optional and separate from normal tests:

```powershell
cmake -S . -B build/fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++ `
  -DKISAK_PORTABLE_TESTS_ONLY=ON -DKISAK_BUILD_FUZZERS=ON
cmake --build build/fuzz --target asset_parsers_fuzz
```

The browser command checks command/dvar initialization, monotonic RAF progress,
WebGL2 indexed-surface rendering and context recovery, Wasm delivery, both
directory-selection paths, OPFS/IndexedDB commit and reload, immutable bounded
sources, stale-source
handling, cancellation without a late heap write, and frame progress while Blob
reads are delayed. At boot it requires the synthetic fastfile extraction to
remain `loading` while bounded work advances on distinct RAF callbacks and then
become `ready`. It checks the 64 KiB/64-record published ceilings, per-step and
cumulative work metrics, version, the exact two-asset count/order and stable
material identity, inflated and declared-zone bytes, block sizes, raw nonzero
surface offsets, material-reference kind/name, and extracted/converted counts.
It also drives synthetic stored and deflated IWD members through the asynchronous
archive job, checks CRC failure, then reads, parses, and uploads one bounded IWI
through the engine cache and renderer seam. It covers
all four exact synthetic framebuffer colors, renderer-owned recovery after
context loss, failed-initialization cleanup, graceful unsupported-format handling,
malformed selection and IWI
inputs, size limits, cache release, cancellation and replacement generations,
stale mount invalidation and recovery, incremental large-index publication,
replacement failure, cross-tab synchronization, cleanup, metadata repair,
storage-capability failures, the absence of retail files and asset network
requests, and the missing-module path. Every automated asset is generated
synthetic data. Set `KISAK_BROWSER_CHANNEL=msedge` to use an installed Microsoft
Edge build.

## Milestone 11: resumable source and asset registry (complete)

Milestone 11 separates two reusable lifetime boundaries from the strict
surface extractor:

- `BoundedSourceStream` is a single-unread-chunk queue with configured per-feed
  and cumulative limits. `Feed` copies caller bytes atomically, returns
  backpressure while a chunk remains unread, releases a drained allocation, and
  requires an explicit final marker.
- `ZoneAssetRegistry` owns asset names and sequential job-local identities. It
  reserves checked four-byte logical alias cells, publishes each cell once only
  after the target asset is registered, resolves tokens by expected asset type,
  and defines separate unload and full-reset behavior.

`WorldSurfaceExtractionJob::BeginStreaming` can now pause before or within the
12-byte prefix, across arbitrary zlib input boundaries, and after zlib stream
completion while waiting to distinguish clean EOF from trailing bytes. A
starved `Step` stays running and performs zero work. Source chunk, total-byte,
backpressure, duplicate-final, truncated-prefix, truncated-zlib, and trailing
data failures are deterministic and preserve the prior feed state. The original
vector-taking `Begin` and synchronous `ExtractWorldSurface` APIs drive this same
source seam, so there is still only one traversal implementation.

The browser runtime no longer gives the job one complete source allocation. It
feeds the freely generated fixture in 37-byte chunks across RAF callbacks,
deliberately reports `source-wait` before resuming, and releases its producer
buffer after the final copied feed. Lifecycle metadata exposes bounded per-step
source work, cumulative received/consumed bytes, feed count, and the two stable
registry identities. The accepted serialized envelope remains exactly
`Material(-2) -> GfxWorld(-1)`; no retail fastfile reaches this decoder.

## Milestone 12: English F.N.G. installation VFS (complete)

Milestone 12 widens the user-owned installation boundary without widening the
zone grammar. Manifest schema 2 defines the exact `sp-killhouse-english-v1`
profile:

- `localization.txt`;
- `main/iw_00.iwd` through `main/iw_13.iwd`;
- `main/localized_english_iw00.iwd` through
  `main/localized_english_iw06.iwd`;
- `zone/english/code_post_gfx.ff`, `ui.ff`, and `common.ff`;
- the F.N.G. map zone `zone/english/killhouse.ff`.

The portable folder picker still filters the browser-provided relative file
list, while the native directory picker requests only these exact files and
never enumerates unrelated installation content. Paths are folded only at the
selection boundary, stored canonically, and admitted to OPFS or VFS access only
through the immutable allowlist. Missing files identify their full relative
path. The English-only choice is explicit for this first single-player profile;
other localization archive families require a later profile decision.

Validation remains bounded. Localization uses the existing sub-4-KiB Wasm
probe. Each IWD uses a 4-KiB head, at most 65,557 tail bytes, and at most 4 KiB
of its declared central directory. Each fastfile sends only its first 14 bytes
to `KisakWeb_ProbeFastfileHeader`, which requires unsigned `IWffu100`, version
5, and a valid non-dictionary zlib header. No probe inflates a retail zone.
After streaming copies into OPFS, restore reopens all 26 files, checks every
recorded size, and repeats the bounded probes before publishing the active
manifest atomically. Schema-1 metadata is rejected and cleaned rather than
silently upgraded to an incomplete profile.

The existing immutable-source VFS now works for every allowlisted IWD and
fastfile. Reads remain capped at 1 MiB in the JavaScript adapter and 64 KiB at
the C++ bridge, hold an import generation across asynchronous work, and retain
the existing cancellation rule that prevents a late Promise from touching Wasm
memory. Tests stat/read the `killhouse` and `common` fastfiles, cancel a delayed
map read, and invalidate a startup-zone source after clear. Reading these files
does not change the synthetic extraction/conversion generations: retail `.ff`
bytes remain disconnected from `WorldSurfaceExtractionJob`.

A read-only audit of the user's English Steam install found every profile path.
Its 21 IWDs total 3,031,228,345 bytes; the four selected fastfiles total
146,974,411 bytes; with localization the selected profile is 3,178,205,238
bytes. No audited byte was copied, extracted, persisted, or committed by the
repository tests; all automated fixtures are freely generated.

## Milestone 13: cooperative qcommon pre-database shell (complete)

`web_qcommon_preinit` is a platform-neutral startup machine with a fixed action
grammar. It initializes a 256-KiB bounded startup arena, a 64-entry cooperative
event queue, and five startup dvars through the existing portable command/dvar
code. It then emits one stat and one offset-zero header read for each exact M12
profile path. The successful English F.N.G. sequence is therefore 55 actions:
three local initialization actions plus 52 asynchronous filesystem actions.
It reads eight localization bytes, four bytes from each of 21 IWDs, and 14
bytes from each of four fastfiles, or 148 bytes total.

`web_qcommon_runtime` connects that machine to `WebFs` and advances no more than
one action from each RAF callback. Promise completions remain queued until the
next `WebFs_PumpCompletions`; callback-lifetime bytes are copied into a fixed
14-byte completion record. Every request retains both an action token and the
filesystem request ID. Cancellation retires the JavaScript token before
releasing the Wasm buffer, and a restart uses a fresh generation with reset
metrics. The error boundary is a fixed diagnostic buffer and reports the exact
startup path plus a typed machine error.

The launcher starts this shell only after the schema-2 import has been restored
or published. Lifecycle events expose stage, generation, action/file/byte
counts, memory and event limits, current path, and scheduling choices. A ready
result has `stage = pre-database`, `filesChecked = 26`, `actionsIssued = 55`,
and `probeBytesRead = 148`. Browser tests cover success, restart, in-flight
cancellation, and a forced `killhouse.ff` I/O failure. The implementation does
not use Asyncify or pthreads and does not inflate or traverse retail zones.

## Milestone 14: cooperative engine-job scheduler (complete)

`web_cooperative_scheduler` is a fixed-capacity, allocation-free scheduler with
generational task handles. Registration assigns a stable order and a maximum
byte/record reservation. Before invoking a callback, the scheduler charges that
reservation against the frame envelope; call, byte, record, or inter-task wall
time exhaustion leaves a typed trace entry and increments per-task starvation
history. Three consecutive denied frames produce one warning. A callback that
reports work outside its reservation is quarantined and receives its
cancellation callback exactly once. A stale handle cannot disable, resume, or
unregister a replacement generation.

Milestone 14 originally registered seven tasks. Milestone 15 extends the live
schedule to these eight tasks in order:

1. filesystem completions;
2. qcommon pre-database work;
3. retail fastfile census work;
4. IWD archive work;
5. engine-asset work;
6. command-buffer execution;
7. synthetic world-surface extraction/conversion;
8. renderer submission.

Together they reserve 266,254 bytes and 267 records inside frame ceilings of
320 KiB, 320 records, and eight calls. The scheduler checks a 12-ms wall-time
window between callbacks. It cannot preempt a running C++ callback, so each job
retains its existing hard inner ceiling; in particular, fastfile and archive
steps remain bounded at no more than 64 KiB/64 records where applicable.
Renderer submission stays last, preserving the previous data dependency.

`web_engine_scheduler` publishes the first eight frames, every thirtieth frame,
and every exceptional frame as `kisakcod:schedule`. Each event contains the
limits, aggregate reservations and reported use, elapsed time, starvation and
protocol counters, plus the ordered eight-entry trace. Browser tests assert the
exact order and reservations and prove that the established converted surface
and indexed renderer result are unchanged. Portable tests independently force
each exhaustion class, repeated starvation, protocol quarantine, reset, and
stale-generation cancellation.

## Milestone 15: bounded retail fastfile census (complete)

`web_retail_fastfile_census` reads the unsigned `IWffu100`/version-5 outer
envelope through the M11 single-chunk source queue, incrementally inflates at
most a 256-KiB prefix, and validates the 44-byte XFile, all nine declared block
sizes, the 16-byte XAssetList, the complete script-string table, and the complete
eight-byte XAsset table. Work is capped at 64 KiB and 64 semantic records per
step. Counts, multiplication, string lengths, individual blocks, the block sum,
source bytes, and allocation are all bounded. The result is published atomically
only after the whole table has validated.

At the M15 boundary, `web_retail_census_job` read only
`zone/english/code_post_gfx.ff` from the
allowlisted, generation-bound browser VFS. The launcher started it after qcommon
reached `pre-database`, and started IWD mounting only after the retail job reached
`body-boundary`. Cancellation retires its in-flight VFS request. Lifecycle data
includes all nine block sizes, nonzero type counts, pointer-reference classes,
source and inflated-prefix metrics, and the exact first unsupported asset
type/index/token. It explicitly reports `traversesAssetBodies = false`.

The freely generated automated fixture contains three script-string records and
five asset headers followed by a recognizable sentinel body. Native/Wasm tests
cover arbitrary source splits, work ceilings, malformed blocks, unsupported
string references, invalid types, bad framing, and atomic failure. Browser tests
prove qcommon → census → archive ordering and that a truncated zlib prefix fails
closed without starting the archive.

A separate read-only audit of the user's legally owned Steam file confirmed the
real boundary without copying any retail byte into the repository: the file is
872,586 bytes; XFile reports size 1,378,265, external size 950,499, and block
sizes `[498816, 0, 0, 0, 407412, 0, 0, 4224, 480]`; 107 script-string slots end
at inflated offset 1,830; and 1,639 asset headers end at offset 14,942. Asset zero
is inline `techset` (type 5, token `0xffffffff`). The table contains 18 asset
types, dominated by 1,351 localize, 90 techset, 76 rawfile, and 72 material
records. M15 stopped there and did not claim those bodies were loadable.

## Milestone 16: leading technique-set traversal (complete)

M16 turns that census into the first generated-loader traversal. The bounded
job now models the native nested block-4 script-string and asset-table
allocations, pushes block 0 for the inline technique set, then returns to block
4 for the technique-set name and dependent records. It validates the 148-byte
`MaterialTechniqueSet`, its 34 embedded references, the first inline
`MaterialTechnique`, its complete pass-header array, a 100-byte
`MaterialVertexDeclaration`, a 16-byte `MaterialVertexShader`, its name, and a
bounded DWORD program with the Direct3D vertex-shader signature.

The native `Load_BuildVertexDecl` step is represented by an owned portable
32-byte stream-routing descriptor plus its hash; it does not create or retain a
D3D declaration. This makes the declaration dependency explicit without
pretending that its browser backend exists already.

Traversal terminates immediately before `Load_CreateMaterialVertexShader`, the
first native D3D9 side effect. No WebGL handle is invented, the pixel shader and
arguments are not skipped, and the incomplete technique set is not published:
`completedAssetCount = 0` and `techniqueSetPublished = false`. Lifecycle output
reports exact logical block offsets, block-0 high-water, block-4 cursor,
technique slot/pass/stream counts, shader name, DWORD count, and an FNV-1a hash
of the validated bytecode. Per-step ceilings remain 64 KiB/64 records, including
when a caller deliberately supplies much smaller budgets.

The freely generated fixture now contains this entire dependency prefix and a
sentinel where pixel-shader traversal would begin. Tests additionally cover
normal-reference rejection, bad shader signatures, declared block-4 overflow,
cancellation/restart, and the required qcommon-to-traversal-to-archive ordering.

The user's Steam file was audited read-only against the same contract. Asset
zero is technique set `sm2/2d`; only technique slot 4 is populated; it has one
pass and a three-stream vertex declaration. Its inline vertex shader is
`vertcol_simple.hlsl`, containing 103 DWORDs (412 bytes), with FNV-1a
`0x66467e0a`. The boundary is inflated offset 15,673. Logical block offsets are
asset table 1,772, technique 14,892, vertex declaration 14,920, vertex shader
15,020, shader name 15,036, and program 15,056; block 0 peaks at 148 and block 4
stops at 15,468. No retail byte was copied or modified.

## Milestone 17: first paired shader compatibility record (complete)

`web_shader_compatibility` is a renderer-independent, bounded D3D9 token and
CTAB decoder. It recognizes the first VS 1.1 / PS 2.0 pair by stage and version,
instruction inventory, named register bindings, and the prepared vertex-stream
routing contract. A filename or bytecode hash is diagnostic only. The one
accepted contract selects owned GLSL ES 3.00 sources under stable identifier
`webgl2.vertcol_simple2d.v1`; malformed, unknown, or partially matching programs
fail without replacing an earlier result.

The generated traversal now resumes after vertex bytecode, resolves the pixel
shader's normal name reference, validates and decodes its inline program, reads
all three eight-byte material arguments, and consumes the inline technique
name. Only then does it unwind the technique and block-0 scopes and atomically
publish asset zero. The boundary is immediately before asset one:
`completedAssetCount = 1`, `techniqueSetPublished = true`, and no native D3D9
handle is created or retained.

The freely generated fixture supplies independent synthetic CTAB and token
streams with the same semantic contract. Tests cover bounded token walking,
CTAB ranges, atomic decoder failure, routing mismatch, incremental traversal,
and browser lifecycle output. A separate ignored diagnostic ran the same C++
path read-only against the owned Steam file: technique `vertcol_simple2d`, VS
hash `0x66467e0a`, PS hash `0x523f57e2`, argument hash `0x90244fa9`, inflated
boundary 15,950, block-4 cursor 15,745, and one completed asset. No retail byte
was added to the repository or automated tests.

## Milestone 18: renderer-owned compatibility program (complete)

The M17 record now crosses a narrow renderer API as a registry-owned descriptor.
Retail bytes can select `webgl2.vertcol_simple2d.v1`, but cannot provide GLSL:
the renderer resolves and revalidates the stable ID, source text, and both source
hashes against compiled-in port code before allocation. It retains bounded
source descriptions for recovery and keeps all shader/program handles private.

With a current WebGL2 context, publication compiles and links a replacement
program before changing active state. It requires attribute locations 0/1/2 for
position, color, and texture coordinates, plus live locations for
`u_viewProjectionMatrix`, `u_worldMatrix`, and `u_colorMapSampler`. The first
deterministic draw binds identity world/view-projection matrices, texture unit
zero, the existing indexed surface VAO, and the existing fallback or imported
texture. The two-component position and three-component color arrays rely only
on WebGL's defined missing-component defaults (`z = 0`, `alpha = 1`). A GL error
on that first draw retires the replacement GPU program and returns subsequent
frames to the bootstrap pipeline.

Lifecycle output reports selection/resource generations, source hashes,
bindings, residency, recovery count, draw count, and first-draw completion.
Context loss discards every handle and retains only source descriptions; restore
recompiles, relinks, revalidates bindings, and increments recovery state. Import
restart/cancellation clears the compatibility program so a stale asset
generation cannot remain active. Browser tests intercept actual WebGL calls to
prove the two sources, six binding queries, two identity matrices, indexed draw,
atomic binding failure, bootstrap fallback, and context-loss reconstruction.

This is the first draw through a contract decoded from owned COD4 data, but its
geometry and fallback/test texture are still the repository's synthetic or
freely generated inputs. It is not yet a rendered retail surface.

## Milestone 19: bounded COD4 DXT image binding (complete)

The portable IWI boundary now decodes formats 11, 12, and 13 (DXT1/BC1,
DXT3/BC2, and DXT5/BC3) into tightly packed RGBA8. It validates the complete
member before allocation, accepts only bounded two-dimensional images with
known picmip/no-mipmap flags, rejects cubemaps and volumes, checks every block
and mip-size calculation, clips partial edge blocks, and preserves COD4's
smallest-to-largest serialized mip order while publishing only the largest
level. DXT1 transparent mode, DXT3 explicit alpha, both DXT5 alpha-table modes,
RGB565 expansion, malformed payloads, and the 4 MiB decoded recovery ceiling
have portable native/Wasm vectors. Publication remains atomic when input and
output alias or any validation/allocation step fails.

That decoder crosses the existing asynchronous IWD/member path. The bounded,
case-insensitive first `images/*.iwi` entry—`images/$black.iwi` in the verified
owned Steam archive—can now be decoded, copied into renderer-owned recovery
storage, uploaded as RGBA8, and bound to `u_colorMapSampler` on texture unit
zero while the M18 compatibility program draws. Launcher state exposes this
joint sampler/image binding as `rendererMaterial`, including stable shader ID,
image path, source IWI format, decoded format, recovery bytes, and whether the
current geometry is synthetic. Context recovery still recreates the texture
without rereading the archive.

Automated browser acceptance uses a freely generated `$black.iwi`-named DXT1
fixture and proves it is simultaneously resident with the M18 program; it does
not contain retail bytes. The owned install observation is read-only and is not
part of the repository. This is the first path capable of drawing genuine COD4
texture pixels, but the current indexed surface/material identity remains the
synthetic M10 fixture. It does not yet claim that a retail `Material` or
`GfxImage` asset record selected that image, nor that any map geometry rendered.

## Milestone 20: registered retail material/image binding (complete)

The retail generated-loader path now accepts the exact leading three-asset
prefix observed in the owned English `code_post_gfx.ff`: `sm2/2d`, `2d`, and
material `ui_cursor`, all inline. The first technique set still owns the strict
M17/M18 WebGL2 substitution. The second set is traversed separately as the
material's serialized dependency: its normal aliases to the first declaration,
shader name, and technique name are checked, its renderer-one vertex/pixel
programs receive bounded stage/version/terminal framing validation, and no
second shader substitution or native D3D object is invented.

`ui_cursor` is accepted only with one non-water 12-byte texture definition, no
constant table, one state-bits record, and a normal reference to the completed
`2d` technique-set asset. Its inline `GfxImage` must be a bounded two-dimensional
record with a safe inline name and a consistent bounded load definition. The
owned record is `3_cursor3`, 64 by 64 by 1, with a DXT3 load format and no
embedded resource bytes. The resulting archive path is constructed as
`images/3_cursor3.iwi`; it is present in the owned `main/iw_00.iwd` and is read
through the existing M3/M4 engine-filesystem service and M19 DXT decoder.

The shared zone registry reserves the pointer cells before traversal and owns
four stable identities in generated-loader publication order: compatibility
technique set 1, material technique set 2, nested image 3, and material 4. The
three top-level cells plus the material texture's image cell must all be defined
before publication. A bad prefix, unresolved or mistyped alias, water texture,
unsafe name, inconsistent image dimensions, negative/oversized resource, or
allocation failure leaves no externally available result.

The read-only owned diagnostic completes three top-level assets at inflated
offset 17,013 with block-0 high-water 148 and block-4 cursor 16,532. No retail
byte is stored in the repository. The freely generated fixture uses material
`web_cursor` and image `synthetic_engine_asset`; its IWD deliberately also
contains the lower-sorting `images/$black.iwi`, proving that the browser loads
the material-selected path instead of the former deterministic first-image
probe. Cancellation clears the binding, and a missing selected member fails
closed rather than falling back.

This is the first real serialized material choosing a real owned image. The
drawn indexed geometry remains the synthetic M10 world surface, so this is not
a retail map render.

## Milestone 21: retail world-asset inventory (complete)

The cooperative census now runs a second parser mode over the allowlisted
`zone/english/killhouse.ff` after the M20 startup material succeeds. This mode
validates the same unsigned v5/zlib envelope, XFile record, script strings, and
complete XAsset table, but intentionally stops before generated-loader body
zero. It publishes no map asset and never treats a declared size as permission
to skip an unknown body.

The read-only owned inventory observes a 70,391,800-byte file containing 1,684
assets. Only 16,295 compressed bytes are consumed to reach the 30,747-byte
inflated table boundary. The stable table-order hash is `0x12e39952`; asset zero
is an inline technique set, and the first inline `GfxWorld` is index 772. Every
one of the 772 preceding references is inline, so none can be resolved as an
already-published alias or omitted as null. Their type census is 146 `xanim`,
315 `xmodel`, 218 `techset`, one `com_map`, one `lightdef`, 10 `weapon`, 60
`fx`, and 21 `rawfile` entries.

The full owned table contains 270 `xanim`, 382 `xmodel`, six `material`, 238
`techset`, 466 `sound`, one each of `col_map_sp`, `com_map`, `game_map_sp`, and
`gfx_map`, one `lightdef`, three `menufile`, 151 `localize`, 12 `weapon`, 120
`fx`, and 31 `rawfile` entries. No retail bytes are stored in the repository.
The freely generated browser fixture has seven headers and places its synthetic
world at index five; malformed coverage proves a table without `GfxWorld`
fails before the already-validated startup material or shader is published.

This inventory materially narrows the route to a real world surface: direct
seeking to asset 772 would violate generated-loader ordering and pointer
publication. It does not parse a retail map body or change the synthetic drawn
geometry.

## Milestone 22: first retail map asset (complete)

The map census now continues past the M21 table boundary into the exact
generated-loader body-zero path. It allocates the 148-byte
`MaterialTechniqueSet` in temporary block 0, enters persistent block 4 for its
bounded name, and classifies all 34 technique-pointer tokens without converting
or executing any unknown dependency.

The owned asset is `,sm2/mc_l_sm_r0c0s0`: world-vertex format zero, null remap,
and 34 null technique pointers. Its name ends at inflated offset 30,915;
block-0 high-water is 148 and block-4 cursor is 30,708. With no nested technique
body pending, the parser unwinds both frames, registers stable identity 1, and
defines the reserved top-level asset-zero alias atomically. It then stops before
asset one, which the table identifies as another inline technique set.

The generalized boundary also handles dependency-bearing synthetic sets
conservatively: it records null, inline, shared-sentinel, and normal-alias token
classes, but leaves the top-level alias unresolved and stops before
`Load_MaterialTechnique`. Invalid header state or an unsafe/unterminated name
fails without making a result available. The browser fixture models the owned
zero-dependency publication and contains no retail bytes.

This is the first successfully published asset from a retail map fastfile. It
does not create a shader, read asset one, or render map geometry.

## Milestone 23: consecutive retail map assets (complete)

The world-prefix mode now repeats the checked 148-byte technique-set load for
each consecutive inline top-level entry. Every body receives its own bounded
result record, temporary block-0 allocation, persistent block-4 name span, and
table-cell alias. A set is registered and its alias published only when all 34
technique pointers are null. The loop stops before the first nested
`MaterialTechnique`, non-inline reference, or different top-level type.

The owned F.N.G. fastfile begins with 12 zero-dependency technique sets at asset
indices 0 through 11. All 12 publish with identities 1 through 12 and a 12/12
defined alias registry. Their serialized run ends at inflated offset 32,729;
temporary block 0 retains a 148-byte high-water mark, while persistent block 4
advances to 30,894. Asset 12 is an inline `XModel`, so traversal stops before
its body. No model bytes are interpreted.

The freely generated browser fixture publishes two consecutive sets before an
inline `XModel`. Companion fixtures stop on a dependency in set one and reject
a malformed later header without exposing a partial result. No retail bytes are
stored in the repository.

## Milestone 24: first retail XModel boundary (complete)

The world-prefix reader now enters inline asset 12 using the upstream generated
loader order. It consumes the fixed 220-byte `XModel` record, validates its
counts, LOD windows, finite bounds, pointer/count relationships, and collision
surface limit, then allocates only the simple skeleton prefix whose lengths are
proved by those fields. Bone script-string tokens are resolved through the
already checked zone string table. The traversal stops immediately before the
first `XSurface` array; it does not infer or skip any surface payload.

The owned first model is `ch_street_wall_light_01_off`: one root bone, six
surfaces, three two-surface LODs, two declared collision surfaces, radius
approximately 45.003, and memory usage 24,847 bytes. Its single bone token is 1
and resolves to `polysurface269`. The model prefix ends at inflated offset
33,012. Temporary block 0 reaches a 220-byte high-water mark, while persistent
block 4 advances from the model name at logical offset 30,894 through the bone
token, classification byte, and base matrix to logical cursor 30,960.

The asset-12 table alias is reserved but deliberately remains undefined because
the model has not completed its surface, material, collision, or physics
dependencies. The registry therefore reports 13 reserved aliases and 12
defined aliases. Synthetic fixtures cover the same successful boundary, a
non-inline bone-name dependency that stops after the fixed header, invalid
bounds, and an invalid bone string token; malformed inputs fail without
publishing a partial public result. No retail bytes are stored in the
repository, and no model geometry is rendered.

## Milestone 25: first retail XSurface dependency prefix (complete)

The map reader now continues from the M24 skeleton boundary through the exact
`Load_XSurfaceArray` order. It validates every fixed 56-byte surface header,
weighted-vertex counts and pointer relationships, then accounts for inline
32-byte packed vertices in block 7, rigid-vertex lists and collision trees in
block 4, and triangle indices in block 8. Payload sizes and aggregate counts
are independently capped; large vertex, node, leaf, and index arrays are hashed
as traversal evidence instead of being retained in the census result.

For `ch_street_wall_light_01_off`, all six surfaces complete: 754 vertices,
524 triangles, six rigid lists, 44 collision nodes, and 284 collision leaves.
The serialized surface dependency run occupies 29,216 bytes including the
surface and material-handle arrays. It ends at inflated offset 62,228 with
block-4 cursor 32,960. The six material slots are retained in surface order:
two inline material tokens followed by aliases `0x400080a9`, `0x400080ad`,
`0x400080a9`, and `0x400080ad`.

Traversal stops before the first inline `Material`. The asset-12 alias therefore
remains reserved and undefined, and the XModel is not rendered. Synthetic
coverage proves two surfaces across blocks 4, 7, and 8, one inline collision
tree, a null-tree rigid list, material ordering, fail-closed pointer/count
mismatch, and invalid collision scale. No retail bytes or retained proprietary
vertex/index payloads are stored in the repository.

## Milestone 26: first complete retail XModel dependency chain (complete)

The world reader now generalizes the checked material loader across the first
XModel's six ordered material handles. It loads two inline materials, resolves
four encoded aliases back to those handle cells, validates each technique-set
alias, and walks bounded texture, image, constant, and state-bit tables in the
same order as `Load_Material`. Inline images receive stable registry identities;
the second material also proves a typed image alias back to an image published
by the first material. Empty built-in images are accepted only with the exact
zero texture/dimension layout, while streamed two-dimensional images retain
bounded load-definition metadata and hashes rather than proprietary pixels.

The owned first model publishes material `mc/mtl_street_light_02` as identity
16 and `mc/mtl_street_light_bulb_02_off` as identity 18. Its six handles resolve
to identities `16, 18, 16, 18, 16, 18`. Traversal then consumes two checked
collision-surface headers and 96 finite collision triangles, validates one
40-byte bone-info record, and proves that both physics references are null.
The collision dependency accounts for 4,696 bytes and is hashed rather than
retained.

Only after that full chain succeeds does the registry assign XModel identity
19 and publish asset-table alias 12. The boundary is inflated offset 67,723,
block-4 cursor 38,112, with all 19 reserved aliases defined. Malformed material
or image aliases, collision bounds, bone info, and resource limits fail without
an externally available result. At this historical M26 boundary, a non-null
inline physics preset was a conservative stop; M39 below supersedes that
limitation. Browser fixtures are freely generated and contain no retail bytes.

## Milestone 27: first retail XModel surface render (complete)

At the M27 boundary the dependency reader retained serialized bytes for only the first XSurface,
and only when it fits the existing renderer ceiling of 4,096 vertices and 4,096
triangles. All later surfaces remain hash-only census evidence. Retention occurs
inside the private parser result; no packed pointer or WebGL2 handle is exposed
to JavaScript or shared engine code.

After the complete M26 XModel publishes, a separate D3D-free engine converter
checks the exact `vertexCount * 32` and `triangleCount * 6` byte lengths, decodes
finite xyz/binormal values, native packed color, and upstream high-half-U /
low-half-V IEEE half-float texture coordinates, and rejects every index outside
the local vertex array. It selects the two largest non-degenerate spatial axes
and aspect-preservingly fits them inside a clip-space margin. Only after the
surface-zero material handle resolves to a published typed identity does the
existing renderer validate, copy, upload, and atomically replace its surface.

The read-only owned profile converts surface zero of
`ch_street_wall_light_01_off`: 368 vertices, 252 triangles (756 indices), and
material identity 16 (`mc/mtl_street_light_02`). Its deterministic projection
uses X horizontally and Z vertically. The synthetic browser fixture observes a
second vertex/index buffer upload containing its freely generated packed quad.
A shader-binding failure leaves the original submission generation and
four-vertex bootstrap surface active; malformed lengths, non-finite floats or
halves, degenerate bounds, bad indices, missing material identity, allocation,
and backend upload failure likewise do not replace the active surface.

This is the first user-owned retail geometry drawn by the web renderer. The
surface currently samples the already resident startup image; M27 validates
and records the XModel material identity but does not yet load that material's
diffuse IWI. It is one orthographically fitted model surface, not a camera,
GfxWorld/map render, multi-surface LOD, lighting path, or playable game.

## Milestone 28: rendered XModel color-map binding (complete)

The M27 surface publication now passes its resolved material identity to a
separate D3D-free selector. The selector requires exactly one published texture
with upstream `TS_COLOR_MAP` semantic 2, follows that texture's typed image
identity across the bounded XModel dependency result, and accepts only a
published external two-dimensional DXT image with a traversed load definition
and no embedded resource payload. It then copies only material/image names,
identities, semantic, and the constructed `images/<name>.iwi` path. Ambiguous
materials, duplicate color maps, unresolved or duplicate images, unsafe names,
built-ins, and unsupported layouts leave the destination unchanged.

The asynchronous archive path now searches `main/iw_00.iwd` through
`main/iw_13.iwd` one at a time for that exact selected path and mounts only the
matching bounded index. It does not choose another image by filename order. If
the selected member is absent, the primary archive can still mount for the
rest of the engine, but the image request reports unavailable and the current
renderer texture remains resident. Built-in or unsupported selections suppress
the legacy first-IWI probe for the same reason. Successful decode and renderer
replacement remain atomic, and renderer-owned RGBA8 recovery pixels recreate
the selected image after WebGL2 context loss.

The read-only owned profile resolves surface-zero material
`mc/mtl_street_light_02` (identity 16) to semantic-2 image
`street_light_02_col` (identity 15), constructs
`images/street_light_02_col.iwi`, and locates it in `main/iw_03.iwd`. Its IWI v6
record is a 512 by 512 by 1 DXT1 image (format 11), within the existing 4 MiB
decoded recovery ceiling. No owned archive or image bytes are copied into the
repository.

The freely generated browser fixture deliberately places its selected image in
`iw_03` while lower archives contain other images, proving typed selection,
bounded multi-archive discovery, IWI decode/upload, sampler binding, and the
existing context-recovery seam. Separate fixtures prove that a built-in color
map and a missing selected IWI keep the already resident texture and surface.

This remains one orthographically fitted XModel surface and one material. It
does not add a general material system, multi-surface model draw list, lighting,
camera state, or map rendering.

## Milestone 29: bounded first-LOD XModel draw list (complete)

The XModel parser now retains packed render bytes only for surfaces in the
first declared LOD, under 4,096-vertex/triangle per-surface ceilings and
16,384-vertex/triangle aggregate retention ceilings. Later LODs remain census
metadata and hashes. A separate portable builder validates each retained
surface, resolves its material by typed identity, requires one supported
semantic-2 external color map, deduplicates repeated image identities, and
combines successful geometry into one shared-projection vertex/index buffer.
One unsupported surface is recorded and skipped without erasing prior draws.

The renderer now atomically owns up to 16 indexed draw ranges, 16,384 vertices,
49,152 indices, and eight texture slots. Each IWI is discovered by its exact
typed path and loaded sequentially even when slots live in different base
archives. Individual decode or upload failure leaves every already resident
slot and draw intact. Per-image recovery remains capped at 4 MiB and aggregate
draw-list texture recovery at 16 MiB. WebGL2 context recovery rebuilds the
shared geometry, all resident texture objects, and the selected compatibility
program before drawing resumes.

The combined vertex seam retains the model's third spatial axis and enables a
depth buffer for the orthographic preview; flattening every triangle to depth
zero had allowed later index ranges to overwrite nearer shade and bulb faces.
Canvas-aspect correction is applied to the imported compatibility program,
fully transparent DXT texels are discarded, and each color-map binding carries
its checked COD4 sampler byte into WebGL2. The owned lamp materials both publish
sampler state 11, which selects filtered repeating sampling instead of the
bootstrap nearest/clamped fallback.

The read-only owned profile proves that the first LOD of
`ch_street_wall_light_01_off` contains two supported draws: 385 combined
vertices and 828 indices projected on X/Z. Its two typed slots are
`street_light_02_col` (512 x 512 DXT1) and
`street_light_bulb_02_off_col` (64 x 64 DXT1); both exact members are present
in `main/iw_03.iwd`. The automated browser profile independently exercises two
material/image slots, sequential archive loading, multi-draw rendering, and
recreation of every slot after context loss. Those fixtures are generated test
data and contain no retail bytes.

This is still one isolated, orthographically fitted first-LOD XModel. It does
not add model placement, a perspective camera, lighting, general XModel
streaming, or `GfxWorld` rendering.

## Milestone 30: first post-XModel technique set (complete)

The generated world-loader traversal now resumes after publishing the complete
asset-12 XModel instead of treating that publication as the terminal parser
state. A separate census mode reads the next table record, requires an inline
type-5 `MaterialTechniqueSet`, reserves its exact block-4 table cell, and reuses
the checked 148-byte technique-set loader for exactly one body. It stops before
asset 14 or before the first nested `MaterialTechnique`; it does not loop into a
second XModel or reinterpret bytes from another asset class.

The owned asset 13 is `,sm2/mc_l_sm_r0c0n0s0`. Its world-vertex format and
remap token are zero, and all 34 technique pointers are null, so the parser can
unwind both zone frames, register identity 20, and publish the reserved alias.
Its body ends at inflated offset 67,893. Block-0 high-water remains 352 bytes,
block-4 advances to 38,134, and the registry is 20/20 defined. The next untouched
record is another inline technique set at asset 14.

Malformed post-XModel headers fail without making the previously traversed
prefix public. A freely generated dependency-bearing fixture keeps the XModel
published but leaves the new alias undefined and stops before
`Load_MaterialTechnique`. Browser lifecycle output identifies the post-model
record separately from the leading run. No new renderer work, proprietary
payload retention, or map claim is part of this milestone.

## Milestone 31: consecutive post-XModel technique sets (complete)

The post-model mode now continues after asset 13 while the next table entry is
an inline type-5 `MaterialTechniqueSet`. Each body receives a fresh bounded
148-byte block-0 allocation, checked block-4 name, reserved table alias, and
atomic registry publication. Traversal stops before the first nested
`MaterialTechnique`, non-inline set, end of table, or different top-level asset
type. The result separately records post-model bodies entered and completed,
retains the complete ordered technique-set metadata, and reports the untouched
next header.

The owned F.N.G. run spans assets 13 through 20. All eight sets have 34 null
technique pointers and publish as identities 20 through 27:

| Asset | Name | Identity | Inflated boundary |
| ---: | --- | ---: | ---: |
| 13 | `,sm2/mc_l_sm_r0c0n0s0` | 20 | 67,893 |
| 14 | `,sm2/mc_l_sm_r0c0n0` | 21 | 68,061 |
| 15 | `,sm2/mc_l_hsm_r0c0n0` | 22 | 68,230 |
| 16 | `,sm2/mc_l_hsm_r0c0n0s0` | 23 | 68,401 |
| 17 | `,mc_l_sm_r0c0n0s0` | 24 | 68,567 |
| 18 | `,mc_l_sm_r0c0n0` | 25 | 68,731 |
| 19 | `,mc_l_hsm_r0c0n0` | 26 | 68,896 |
| 20 | `,mc_l_hsm_r0c0n0s0` | 27 | 69,063 |

Block-0 high-water remains 352 bytes, block 4 advances to 38,268, and all 27
reserved aliases are defined. Asset 21 is an inline type-3 `XModel`, so M31
stops before its header. The freely generated browser fixture publishes two
post-model sets and stops before a different class; companion cases stop on a
dependency in the later set and fail closed on a malformed later header. No
retail bytes are stored in the repository.

## Milestone 32: second retail XModel boundary (complete)

The world reader now distinguishes the active XModel from the published first
model. After the M31 technique-set run, it reserves the asset-21 table alias,
pushes a fresh block-0 scope, validates the 220-byte `XModel` header, and walks
the same bounded name and skeleton-prefix stages used by M24. All second-model
state is retained separately; the first model's surfaces, materials, texture
bindings, draw list, identity, and renderer publication remain unchanged.

The owned asset is `com_steel_ladder`: one root bone, three surfaces, three
LODs, one collision surface, radius approximately 200.696, and memory usage
24,551 bytes. Its header and skeleton prefix finish at inflated offset 69,335.
The name begins at block-4 offset 38,268 and the final block-4 cursor is 38,324;
block-0 high-water remains 352 bytes. Registry identity count stays 27 while
the reserved/defined alias counts become 28/27 because asset 21 is deliberately
unpublished.

Traversal stops before `Load_XSurfaceArray`. A freely generated fixture proves
that the separate second record retains its bone name and LOD metadata without
altering the first model's render state. Invalid second-model bounds fail
without exposing any partial public result, while an unsupported bone-name
dependency returns a conservative boundary with the published first model
intact. No second-model vertex, index, material, collision, or renderer payload
is retained.

## Milestone 33: second retail XSurface prefix (complete)

The XSurface stages now operate on the active XModel rather than being tied to
the first retained model. After the M32 skeleton boundary, the parser validates
all three 56-byte headers and follows their packed vertices, rigid lists,
collision-tree references, and triangle indices in generated-loader order. It
then retains the three material handles and stops before the first inline
`Material` body.

The owned `com_steel_ladder` profile contains 750 vertices, 488 triangles, and
three rigid lists across its three surfaces. The bounded surface payload is
28,236 bytes, the inflated boundary is 97,571, and block 4 ends at 39,644.
The material references are one inline token followed by two aliases to that
same material. The asset alias remains unpublished at 28 reserved / 27 defined.

No ladder packed vertices or indices are retained for rendering, and no second
draw list or WebGL submission is created. Synthetic coverage proves the same
ordering and fails atomically for an invalid second-XSurface pointer/count
relationship. M33 therefore advances the shared XModel asset loader; it does
not create a standalone model-viewer workflow.

## Milestone 34: complete second retail XModel dependencies (complete)

The material/image, collision-surface, bone-info, and physics stages now use the
active XModel just like the M33 surface stages. Asset 21 therefore completes the
same checked dependency chain previously proven on asset 12, publishes its
reserved table alias, and leaves the parser at the next top-level asset.

The owned `com_steel_ladder` publishes as identity 32 at inflated offset
112,348. Its three material handles resolve to one inline material,
`mc/mtl_steel_ladder` (identity 31), whose technique set is identity 24. The
material has three textures, two constants, and seven state-bit records; its
three inline images publish as identities 28–30. The model's one collision
surface contains 296 triangles and 14,252 bounded collision bytes. Bone info
hashes to `0x604bd5f6`; both null physics references complete normally.

The final block-4 cursor is 54,188, all 33 reserved aliases are defined, and 32
typed assets are registered. The owned material's repeated serialized aliases
target its checked texture-table allocation rather than its raw handle slot;
the registry now accepts that canonical typed target only after the material is
published. A generated fixture exercises the same forward dependency-span alias
and fails closed for an invalid technique-set alias.

The first model's renderer-owned draw list and textures remain unchanged. The
ladder's census contains hashes and typed dependencies only; it is neither
retained for WebGL nor exposed as a viewer model.

## Milestone 35: bounded consecutive XModel collection (complete)

The world result now owns a bounded XModel collection instead of dedicated
first/second parser slots. A single active collection index drives the complete
shared header, skeleton, surface, material/image, collision, bone-info, and
physics path. After asset 21 publishes, the top-level loader loops directly into
consecutive inline XModels and stops cleanly at the first different asset type.
The configured collection ceiling is explicit and malformed later models still
prevent any partial result from being published.

The owned F.N.G. run now publishes three models: asset 12
`ch_street_wall_light_01_off` (identity 19), asset 21 `com_steel_ladder`
(identity 32), and asset 22 `com_steel_ladder_top` (identity 33). The third model
has four surfaces, 660 vertices, 420 triangles, four rigid lists, and 25,008
bounded surface bytes. All four material handles resolve to the already
published `mc/mtl_steel_ladder` identity 31, so no new material body is entered.
Its one collision surface contains 228 triangles and 10,988 bounded collision
bytes; bone info hashes to `0x499d1ece`, and both null physics references
complete normally.

The final inflated boundary is 148,660, the block-4 cursor is 66,676, all 34
reserved aliases are defined, and 33 typed assets are registered. Traversal
stops before inline technique-set asset 23. Eligible collection entries retain
packed first-LOD payload under a shared 16 MiB ceiling; entry zero is merely the
initial active renderer choice and remains exposed through the legacy
`firstXModel` alias. A generated three-model fixture additionally proves the repeat loop,
zero-surface completion, the collection ceiling, and atomic rejection of invalid
third-model bounds.

## Milestone 36: reusable XModel loader dispatch (complete)

`WorldXModelLoader` is now the canonical complete-loader mode; the former
`WorldXModelCollection` name remains an API-compatible alias. The supported
top-level dispatcher invokes the same bounded XModel operation for consecutive
and separated model runs and can resume technique-set traversal after any
completed model. Renderer payload selection is recorded explicitly on each
model instead of being inferred from the active parser index. The launcher
builds a selector from the published collection, disables entries without a
retained payload, and calls a narrow C export to atomically replace the active
WebGL2 draw list. Selection reuses the parsed collection, resolves material and
image aliases through deduplicated per-model catalogs, rebuilds the typed
color-map queue, and does not re-read or reparse the fastfile. Inventory publication still
succeeds when the aggregate byte budget prevents a model from being selectable.

A generated eight-asset fixture proves the complete sequence `techset,
techset, XModel, techset, techset, XModel, techset, GfxWorld`. Both models
publish through the same header, skeleton, surface, material/image, collision,
bone-info, and null-physics stages. The trailing technique set also publishes,
showing that loader completion returns control to the shared dispatcher rather
than a model-position special case. Existing three-consecutive-model, malformed
third-model, collection-limit, renderer-byte-ceiling, and live selection
coverage remains active.

The owned run still publishes XModel assets 12, 21, and 22, then enters inline
technique-set asset 23 instead of stopping before it. Asset 23 is
`sm2/mc_unlit`; its fixed header and name complete at inflated offset 148,821
with 16 null, two inline, zero shared, and 16 alias technique references. The
block-4 cursor is 66,689. The new alias is reserved but deliberately unpublished
(35 reserved, 34 defined), and the parser stops atomically before the first
nested `Load_MaterialTechnique` dependency.

This demonstrates reusable XModel dispatch across every supported layout, not
that all 315 XModels before `GfxWorld` have already been consumed. Proving the
entire owned population still requires typed loaders for the intervening inline
asset classes and any dependency variants they expose.

## Milestone 37: reusable MaterialTechnique dependency loader (complete)

The world technique-set path now invokes a bounded reusable
`MaterialTechnique` operation for each inline dependency. It validates the
fixed header and pass ceiling, walks each pass's vertex declaration,
vertex/pixel shader headers and bounded bytecode, shader arguments, optional
literal constants, and inline or prior-arena names, and never creates a native
D3D object. Pointer aliases must address an already allocated logical arena
region. An incomplete or malformed second dependency fails closed without
publishing the parent set.

Owned asset 23, `sm2/mc_unlit`, completes two one-pass dependencies. Slot 4 is
`vertcol_simple_fog_dtex` (201 vertex-program DWORDs, 77 pixel-program DWORDs,
five arguments); slot 28 is `wireframe_solid_dtex` (74/29 DWORDs and one
argument). The parent publishes as identity 34 only after both complete, at
inflated offset 150,864. Its original 16 null, two inline, zero shared, and 16
alias references remain intact.

Returning to the supported top-level dispatcher reuses the same operation for
assets 24 through 32. All ten dependency-bearing sets publish; asset 32 is
identity 43 at inflated offset 166,717. Traversal then enters XModel asset 33,
`com_studio_light_on`, and stops safely at `Load_GfxImage(alias)` after its
bounded surface and material prefix. The fourth XModel remains unpublished;
33 top-level assets are complete and the registry is 43 assets with 50 aliases
reserved / 48 defined at the new boundary.

Generated native and browser fixtures cover two successful inline dependencies,
dispatcher resumption, an invalid second shader, and incomplete dependency
input. Publication remains atomic in every failure case.

## Milestone 38: reusable GfxImage alias resolution (complete)

The image dependency path now models the generated loader's
`DB_InsertPointer` side effect when a `GfxImage` contains a shared (`-2`)
texture-load definition. Each insertion reserves one aligned four-byte cell in
block 4 before the bounded load definition is consumed from block 0. This keeps
all later logical arena addresses canonical, allowing image aliases to resolve
through the existing typed registry rather than through a guessed nearby
object. Undefined, forward, misaligned, and wrong-type aliases still fail
closed. Browser inventory exposes the insertion-cell offset for verification.

The alias in asset 33's `mc/mtl_tripodstudiolight_on` material now resolves to
the previously published `tripod_studio_light_col` image, identity 46. The same
model reaches a bounded `floodlight_beam` image whose serialized format is
`D3DFMT_X8R8G8B8` (`0x16`) and whose inline resource length is zero; that exact
metadata variant is accepted without decoding or creating a native texture.

Asset 33, `com_studio_light_on`, publishes atomically as identity 54 at inflated
offset 257,898 with five materials and five resolved images. The dispatcher then
publishes asset 34, `com_drop_rope`, as identity 59 at offset 374,026. It enters
asset 35, `mil_sandbag_desert_single_flat`, completes its bounded surface,
material, image, collision, and bone-info dependencies, and stops before inline
`Load_PhysPreset`. Asset 35 remains unpublished. At this boundary 35 top-level
assets are complete, the last published registry snapshot contains 59 assets
and 63 aliases reserved / 62 defined, and the block-4 cursor is 183,868. The
inflated-prefix ceiling is now 512 KiB so this boundary remains runnable in the
browser.

Generated native and browser coverage proves successful shared-image alias
resolution after insertion-pointer planning and rejects an undefined image
alias before its material or XModel can publish.

## Milestone 39: reusable PhysPreset dependencies (complete)

The XModel dependency path now mirrors `Load_PhysPresetPtr` and
`Load_PhysPreset` without creating native physics objects. It plans the aligned
44-byte block-0 body, validates both block-4 `XString` fields, rejects
non-finite numeric values and invalid Boolean/padding bytes, and registers the
asset through the existing type-1 registry. Inline and shared pointer forms
are supported; the shared form also reserves and publishes the exact aligned
block-4 `DB_InsertPointer` cell. The XModel's block-0 pointer field remains
undefined until the preset has been completely traversed and registered.

In the owned file, asset 35's preset publishes as `sandbag`, identity 63. Its
sound alias prefix is empty; mass/bounce/friction are 20/0.01/0.3, bullet and
explosive force scales are 0.6/0.25, and its pieces fields are zero. The body is
at block-0 offset 220, with the name and empty sound prefix at block-4 offsets
183,872 and 183,880. Traversal reaches inflated offset 397,206 and block-4
cursor 183,881. Only persistent block-4 insertion cells are retained as
aliases; temporary block-0 XModel fields are deliberately excluded from the
long-lived registry.

Asset 35 remains unpublished because its following `PhysGeomList` reference is
also inline. This is intentional atomicity: publishing the child preset does
not publish its parent before the complete XModel physics chain. Generated
native fixtures cover inline and shared success, including insertion-pointer
publication; native and browser fixtures reject non-finite values and invalid
sound prefixes without exposing a partial world result. The browser inventory
now exposes the typed preset fields, references, logical offsets, identity, and
publication state.

## Active throughput strategy

Development no longer stops at every newly reached pointer variant or assigns
one milestone to each boundary. Loader work is batched by complete generated
family, and the owned dispatcher continues until it reaches a genuinely
different unsupported layout. Metadata not needed for rendering is validated
and summarized rather than retained.

The first throughput batch completed `PhysGeomList`, including geom records,
brush wrappers, sides, individual or shared planes, and adjacent-edge bytes.
Asset 35 now publishes as XModel identity 64 at inflated offset 397,694. Its
physics summary is one geom, one brush, eight sides, eight planes, 40 edges,
and 488 serialized bytes.

The next family batch introduced one checked block-4 array-slice resolver and
uses it for XModel bone names, parent indices, quaternions, translations, part
classifications, base matrices, and bone-info bytes. Inline and shared-inline
forms use the same retained typed containers. A logical reference must resolve
inside an already published XModel array with matching element width,
alignment, and bounds; forged, forward, and cross-family references fail
closed. The image loader also recognizes comma-prefixed map-type-zero names as
engine-owned placeholders, including both `,$identitynormalmap` and
`,spotlight_lensflare`, without inventing a texture load definition.

The reusable FX family now validates the 32-byte `FxEffectDef`, the complete
252-byte element array, bounded velocity and visual-state samples, mark and
ordinary visual arrays, XString references, and optional trail payloads. Visual
dependencies resolve through the typed registry. Inline FX materials reuse the
same bounded material, texture, image, constant, and state-bit loader as XModel
materials; shared insertion cells publish with the completed material. Nested
inline XModels reuse the complete checked XModel loader. Empty comma-prefixed
FX XModels are recognized as engine-owned placeholders and cannot make a
non-comma empty model valid.

The owned run publishes `props/watermelon_splat` as asset 381 / identity 1242
and `props/watermelon` as asset 382 / identity 1250. The first has one mark
element and four inline materials. The second has six sampled elements, three
inline materials, prior aliases, and four nested engine-owned XModels. After
returning from those dependencies, the dispatcher reuses its existing loaders
through asset 394. It now follows native `Load_RawFile` ordering for asset 395:
the 12-byte canonical header is allocated in block 0, the name and `len + 1`
payload are bounded in block 4, the canonical `RawFile` pointers are backed by
owned stable storage, and publication occurs only after the full payload is
available. The owned file publishes identity 1290 for
`aitype/ally_blackkit_shtgn_winchester.gsc` with length 1,781 and identity 1291
for `character/character_sp_sas_ct_benjamin.gsc` with length 201. Returning to
the common dispatcher publishes XModel asset 397,
`body_complete_sp_sas_ct_benjamin`, including 15 surfaces, four LODs, and its
checked material/image dependencies. The dispatcher then publishes canonical
RawFiles 398, 400, 402, and 404, the intervening character XModels, and the
weapon XModel/FX/technique-set run through asset 436. FX assets 423-425, 427,
and 429-433 exercise the shared material dependency path; asset 423 alone owns
seven checked materials. The canonical XAnimParts loader then follows the
native 88-byte header and exact block-4 payload order, publishing assets
437-457 as identities 1368-1388 without adding animation playback. After the
common prerequisite handoff, the result has 459 completed top-level assets,
278 published XModels, 11 FX effects, 21 canonical XAnimParts, and one
canonical WeaponDef. It stops at the explicit first-published-weapon boundary,
asset 459. The owned integration uses a 128 MiB inflated ceiling; 313 top-level
records remain before the first `GfxWorld`.

Reaching `GfxWorld` still requires typed loaders for every intervening inline
asset class; geometry, lightmaps, visibility, and camera state remain separate
later boundaries.

The loader census and `Retail*` records are validation scaffolding, not the
destination engine model. New loader work must inventory and match the native
Kisak DB behavior, and the supported result must converge on canonical
`XAsset`, `XModel`, `Material`, and `GfxWorld` ownership rather than growing a
parallel browser scene format. See
[web-port-convergence.md](web-port-convergence.md) for the current ownership
inventory and retirement criteria.

Once a correctly published `GfxWorld` renders enough real Killhouse geometry to
prove the Kisak renderer-frontend to WebGL2-backend seam, renderer and viewer
expansion pauses. The next phase is integration of substantially more real
Kisak runtime code: `Com_Init`, `DB_LoadXZone`, `CM_LoadMap`, `CL_Init`,
`CG_Init`, `SV_Init`, game, cgame, xanim, collision, and the script VM. Browser
asynchrony remains at the host/platform boundary rather than becoming the
portable engine execution model.

## Convergence checkpoint 1: canonical asset ABI and semantic trace

The first convergence change extracts canonical `RawFile`, `XAssetHeader`,
`XAssetType`, and `XAsset` declarations from the dependency-heavy xanim header
into `src/database/db_asset_types.h`. This is the same engine type definition
for native and web code, not a browser wire mirror. Emscripten tests enforce the
original 32-bit sizes: 12-byte `RawFile`, four-byte `XAssetHeader`, and
eight-byte `XAsset`.

`src/database/db_semantic_trace.*` defines an address-independent comparison
record shared by future native and web DB producers. The reusable web loader
now emits bounded top-level `asset-begin`, `asset-publish`, and `boundary`
events with canonical `XAssetType`, asset index, logical identity, inflated
offset, zone block/offset, alias-cell coordinates, and validated name. The
trace has a deterministic hash, is included only in an atomically available
successful result, and fails closed at an explicit entry ceiling.

## Convergence checkpoint 2: native observer and canonical RawFile

The generated native asset-array loop now supplies stable top-level indices to
an opt-in semantic trace observer. `Load_RawFilePtr` emits begin/publication
events using block-0 object and block-4 reference-cell coordinates. A null
observer is the default and trace failures cannot change loader behavior. The
portable Win32 target now compiles and runs under MSVC, including the shared
semantic trace and retail traversal projection. The complete generated native
DB path is still not claimed as executed: that monolithic target also requires
the native DirectX, Bink, Miles, Steam, and legally owned runtime setup.

The Wasm suite drives the same observer contract against a mixed synthetic
technique-set/FX/RawFile fixture. Its portable contract hash excludes registry
identity and inflate read-ahead fields that are backend diagnostics, while
retaining event kind, canonical type, asset index, logical block coordinates,
alias coordinates, and validated name. Payload size and retained-byte ceilings
fail atomically.

The first MSVC run used the same bundled zlib 1.1.4 as the native game and
exposed a version-specific streaming rule: unlike newer zlib, 1.1.4 rejects a
null `next_in` pointer even when `avail_in` is zero. The IWD member decoder and
fastfile surface inflater now provide a stable non-null zero-length buffer.
All 16 portable Win32 tests pass with that native dependency.

The owned `killhouse.ff` run verifies that assets 395, 396, 398, 400, 402, and
404 reuse the same canonical RawFile operation across intervening XModels. It
then continues through the supported weapon-model and FX dependencies to asset
436 and publishes canonical XAnimParts assets 437-457. The native
`Load_WeaponDefPtr` / `Load_WeaponDef` path for inline type-23 asset 458 is
inventoried through its complete ordered dependency graph. A bounded canonical
slice now implements the fixed scalar record, root insertion and prior aliases,
all script strings and direct XStrings, four accuracy arrays, stable ownership,
delayed publication, and prior canonical XModel/Material/FX handle resolution.
Native sound-name cells and the bounce array resolve through an injected
database lookup rather than inline sound objects. The owned traversal now
reaches that lookup and stops because the standalone web path has no canonical
common-zone sound catalog; connecting the real catalog is the next ordered
dependency.

The prerequisite-zone and cross-zone ownership path is now connected to the
browser orchestration. Generic native block-4 Material visual alias semantics
cross FX asset 4,098 without an asset-specific exception. Ordered `common.ff`
traversal completes all 6,502 assets and retains all 1,723 sound table rows
beginning at asset 4,778 as DB/zone-owned canonical sound graphs. Repeated DB
names and serialized pointer aliases converge on 1,716 unique objects.

The case-insensitive sound catalog indexes those exact objects and retains the
common-zone owner; it does not copy or synthesize sound records. Its lookup
provider mirrors native missing-sound behavior by returning the indexed
canonical `null` object. At that checkpoint, the owned Killhouse traversal
published WeaponDef 458 (`winchester1200`) through its normal canonical lookup
path and stopped at the explicit first-published-weapon boundary at asset 459.
That temporary stop is retired by the ordered traversal below. No playback,
mixing, decoding, or browser audio runtime behavior is implemented.

## Ordered pre-world boundary, loader extraction, and synthetic CI

The first-WeaponDef stop is retired as a traversal boundary. The owned
common-to-Killhouse run now continues in serialized order, completes assets
0-771, and stops before asset 772, type 16 (`gfx_map`/`GfxWorld`). The supported
prefix publishes 218 technique sets, 320 XModels, 60 FX effects, 146
XAnimParts, ten WeaponDefs, 21 RawFiles, one canonical `ComWorld`, and one
canonical `GfxLightDef`. Asset 704 is `maps/killhouse.d3dbsp` with 24 canonical
`ComPrimaryLight` records. Asset 705 is `light_point_linear` with a canonical
`GfxImage*` attenuation dependency; assets 706-771 are technique sets.
The natural boundary has block-0 high-water 2,300, block-4 cursor 11,121,808,
inflated cursor 36,119,878, 1,840 registered assets, and 1,944 defined aliases.
`GfxWorld` was neither sought directly nor entered.

Native `Load_GfxLightDefPtr`, `Load_GfxLightDef`, and `Load_GfxLightImage` are
implemented in the dedicated `web_retail_load_lightdef.*` family. It preserves
the four-byte root envelope, 16-byte canonical body, block-4 XString order,
embedded eight-byte light-image record, image pointer aliases and insertion
cells, and final-only parent publication. A reusable database-side image family
publishes real canonical `GfxImage` metadata/name objects with a null texture
slot. IWI decoding and WebGL resource creation remain renderer/backend work;
the LightDef loader performs no GPU upload or light-runtime behavior.

Native `Load_XString` converts any nonzero non-inline token to an address, so a
valid pointer need not equal the first byte of an indexed string. Weapon
animation fields exercise this by pointing into earlier XAnim names. The
portable resolver now retains those suffixes with stable ownership and uses a
strictly bounded, monotonic compatibility offset only where temporary
pre-world allocation accounting requires it. Arbitrary WeaponDef strings no
longer establish large inferred offsets.

The generated WeaponDef field offsets, operation order, and canonical pointer
assignments have moved to `src/web/web_retail_load_weapon.*`. The dispatcher
still owns the shared stream, registry, publication, trace, and backing storage;
this is the first family extraction from the monolith, not a second registry or
engine object model. Continue extracting coherent generated-loader families
before adding the substantially larger clip-map/world loaders.

The zone registry now enforces independent asset, alias, and total-name-byte
ceilings. Canonical vector order is retained while identity, type/source,
hashed type/name, and alias-span indices provide bounded lookup; failures and
UnloadAll/Reset remain atomic and release index storage.

`retail_fastfile_dispatcher_fuzz` drives the real streaming dispatcher with low
file, inflate, block, collection, retained-byte, trace, step, and iteration
ceilings. Each mutation is tried both as a malformed physical file and as
arbitrary inflated bytes behind a valid synthetic fastfile envelope, allowing
the fuzzer to reach pointer/count/stream/publication paths rather than stopping
at the outer header. Its checked-in corpus contains only synthetic
truncated/malformed inputs. `.github/workflows/web-port.yml` adds legal
synthetic Linux native,
Clang sanitizer/fuzzer, Windows MSVC, Emscripten/Node differential, Playwright
smoke/full, and Release browser-artifact jobs. CI never downloads or packages
retail game data.

An optional loose `.d3dbsp` path is not an implicit shortcut: pursue it only
after an explicit design decision establishes that it is a supported legal
input, inventories its BSP lumps and versioning, and separates its native D3D9
allocation work from portable parsing. The importer remains limited to
the exact schema-2 F.N.G. profile; there is no retail map load, save-data path,
audio/input integration, or playable game loop.
