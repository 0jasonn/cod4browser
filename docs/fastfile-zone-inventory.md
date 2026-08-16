# Fastfile and `GfxWorld` zone inventory

## Status and scope

This document records the Milestone 8 inventory of the upstream
fastfile/database path needed to recover one bounded `GfxWorld` surface, the
strict portable decoder implemented from that inventory, the Milestone 9
resumable execution boundary, and Milestone 10's exact supported-prefix
multi-asset contract, plus Milestone 11's bounded source and asset-registry
lifetime seams. The decoder compiles into the browser target and consumes
a freely generated two-asset synthetic fastfile: one top-level `Material`
followed by one `GfxWorld`. It is not a general fastfile loader and does not
establish retail-map compatibility.

`src/web/web_fastfile_world_surface.h` and
`src/web/web_fastfile_world_surface.cpp` implement the subset defined below.
They reject every unsupported branch deterministically and expose owned output
through a one-shot atomic result handoff. `src/web/web_engine_surface.cpp`
generates the canonical input, advances its extraction once per browser frame,
passes the selected surface to the existing world-surface converter, and
submits the converted quad to the renderer. No user-owned fastfile reaches that
strict decoder. The legal importer exposes only the exact schema-2 English
F.N.G. profile; M15 separately routes a bounded `code_post_gfx.ff` prefix into
the census described below, never into the synthetic renderer path.

The relevant upstream implementation is concentrated in:

- `src/database/db_file_load.cpp`, which reads the file prefix, initializes
  zlib, allocates zone blocks, and starts asset traversal;
- `src/database/db_stream.cpp` and `src/database/db_stream_load.cpp`, which
  implement logical blocks, alignment, delayed streams, and pointer fixups;
- `src/database/db_load.cpp`, whose generated-style loaders define serialized
  traversal order;
- `src/xanim/xanim.h`, `src/gfx_d3d/r_bsp.h`,
  `src/gfx_d3d/r_gfx.h`, and `src/gfx_d3d/r_material.h`, which declare the
  32-bit layouts mirrored by the loader;
- `src/web/web_fastfile_world_surface.h` and
  `src/web/web_fastfile_world_surface.cpp`, which contain the checked portable
  implementation and its result/error contract;
- `src/web/web_engine_surface.cpp`, which builds and runs the synthetic runtime
  fixture through extraction, conversion, and renderer submission.

## File framing and compression

The outer prefix is not part of the zlib stream:

| File bytes | Meaning | Evidence |
| --- | --- | --- |
| `0..7` | Eight-byte magic | `src/database/db_file_load.cpp:233-243` |
| `8..11` | Native-loaded 32-bit version, required to be `5` | `src/database/db_file_load.cpp:244-264` |
| `12..end` | One zlib-wrapped stream | `src/database/db_file_load.cpp:265-279`, `src/database/db_auth.cpp:8-21` |

The loader recognizes `IWff0100` and `IWffu100`, but then classifies every
magic other than `IWffu100` as secure and rejects authenticated loading.
Consequently, this implementation can actually consume only unauthenticated
`IWffu100`, version 5 (`src/database/db_file_load.cpp:239-276`). It calls
`inflateInit_`, so the payload is an ordinary zlib wrapper rather than raw
DEFLATE or gzip (`src/database/db_auth.cpp:8-12`).

The first decompressed value is a 44-byte `XFile`:

```text
u32 size
u32 externalSize
u32 blockSize[9]
```

The layout and size are declared in `src/xanim/xanim.h:1123-1129`, and the read
occurs at `src/database/db_file_load.cpp:279`. The native loader does not use
`size` as an output limit or consistency check. `externalSize` affects progress
reporting only, while all nine `blockSize` values are passed directly to the
allocator (`src/database/db_file_load.cpp:280-292` and
`src/database/db_memory.cpp:79-123`). A portable reader therefore must not
interpret any of those fields as trusted allocation authority.

The next decompressed record is a 16-byte `XAssetList`, read into a global
rather than a zone block (`src/database/db_file_load.cpp:326-335`). Its layout
is:

```text
u32 scriptStringCount
u32 scriptStringsToken
u32 assetCount
u32 assetsToken
```

`ScriptStringList` and `XAssetList` are declared at
`src/xanim/xanim.h:1108-1121`. Script-string payload is traversed first, after
which the complete `XAsset[assetCount]` table is read into logical block 4
before any individual asset body is visited
(`src/database/db_file_load.cpp:293-301,326-349`). Each `XAsset` is eight bytes:
a 32-bit type and a 32-bit header token (`src/xanim/xanim.h:997-1002`). The
current PC asset enum identifies `ASSET_TYPE_MATERIAL` as `0x04` and
`ASSET_TYPE_GFXWORLD` as `0x10` (`src/xanim/xanim.h:906-945`), dispatching them
to `Load_MaterialHandle` and `Load_GfxWorldPtr` respectively at
`src/database/db_load.cpp:7143-7255`.

## Serialized ABI and endian assumptions

The current loader copies integers, floats, pointers, enums, and complete
structures directly into native memory. The prefix version is read through a
`uint32_t *`, arrays use hard-coded element sizes, and there is no byte-swap
stage (`src/database/db_file_load.cpp:235-247`,
`src/database/db_load.cpp:808-916`). Its wire contract is therefore:

- little-endian integers;
- IEEE-754 binary32 floats;
- eight-bit bytes and 16-/32-bit integer fields;
- 32-bit serialized pointer tokens and 32-bit enums;
- the Win32 runtime structure sizes and member alignment recorded in the
  headers, including padding;
- compiler-defined layouts such as `GfxDrawSurf` bitfields, which are outside
  the first geometry boundary.

WebAssembly is little-endian and normally uses 32-bit pointers, but that does
not make native structure casts safe. The portable decoder must read explicit
little-endian fields into fixed-width POD values, validate them, and never
expose a serialized or arena address as a C++ pointer.

## Logical zone blocks and stream behavior

`XZoneMemory` owns nine independently sized `XBlock` arenas
(`src/xanim/xanim.h:1080-1093`). Their native names and allocation classes are
listed at `src/database/db_memory.cpp:8-21`:

| Block | Native name | Load behavior | Relevance to this slice |
| ---: | --- | --- | --- |
| 0 | `temp` | Consumes zlib bytes immediately | Inline asset/object bodies, including `GfxWorld` and `Material` |
| 1 | `runtime` | Destination is zero-filled; consumes no zlib bytes | Runtime-only world visibility/texture state; must remain absent in the strict subset |
| 2 | `large_runtime` | Read is queued and replayed after asset traversal | Not used by the inventoried `GfxWorld` geometry path |
| 3 | `physical_runtime` | Read is queued and replayed after asset traversal | Not used by the inventoried `GfxWorld` geometry path |
| 4 | `virtual` | Consumes zlib bytes immediately | Asset table, strings, indices, vertices, surfaces, and alias slots |
| 5 | `large` | Consumes zlib bytes immediately | Not used by this strict subset |
| 6 | `physical` | Consumes zlib bytes immediately | Not used by this strict subset |
| 7 | `vertex` | Consumes bytes and mirrors them to a locked D3D vertex buffer on a stream switch | Used by `XSurface`, not by the `GfxWorld` vertex array selected here |
| 8 | `index` | Consumes bytes and mirrors them to a locked D3D index buffer on a stream switch | Used by `XSurface`, not by the `GfxWorld` index array selected here |

Initialization gives every block its own cursor
(`src/database/db_stream.cpp:18-29`). A push records the selected block's entry
cursor. Pop has an important asymmetry: it restores that saved cursor only for
block 0, whose storage is temporary; every other block retains its advance.
Switching away from block 7 or 8 performs the native D3D clone
(`src/database/db_stream.cpp:31-78`). A portable model therefore needs both a
current cursor and a high-water mark for each block. Rewinding block 0 changes
the former but must not erase the memory extent already required by an asset
body.

`Load_Stream` makes block 1 zero-filled, queues blocks 2 and 3, and reads
compressed bytes immediately for block 0 and blocks 4 through 8
(`src/database/db_stream_load.cpp:5-29`). Delayed reads are replayed only after
asset traversal (`src/database/db_stream_load.cpp:31-37`,
`src/database/db_file_load.cpp:302-305`).

### Alignment

`DB_AllocStreamPos(mask)` computes `(cursor + mask) & ~mask`
(`src/database/db_stream.cpp:85-89`). Generated helpers use masks `0`, `1`,
`3`, `15`, and `127`, corresponding to alignments of 1, 2, 4, 16, and 128
bytes (`src/database/db_load.cpp:858-946,1906-1909,3013-3016`).

Alignment advances only the logical block cursor. It does not consume or emit
padding in the zlib payload. Block sizes include the skipped arena bytes, while
the decompressed traversal does not. This distinction is essential when
constructing a fixture or validating a block cursor and its high-water mark.

The native calculation truncates addresses through `uint32_t`; it is not a
portable allocator. A new reader should track offsets as checked unsigned
integers and validate the aligned result against the selected block before any
read or reservation.

## Pointer-token classes

There is no single interpretation for a nonzero serialized pointer field. The
generated loader surrounding a field defines its token class.

### Presence-only inline arrays

For these fields, zero means absent and every nonzero token means "allocate at
the current logical cursor and read the array inline." The token value itself
is discarded. The required geometry fields are in this class:

- `GfxWorld.indices` (`src/database/db_load.cpp:6782-6787`);
- `GfxWorld.materialMemory` (`src/database/db_load.cpp:6861-6866`);
- `GfxWorld.vd.vertices` (`src/database/db_load.cpp:6400-6415`);
- `GfxWorld.vld.data` (`src/database/db_load.cpp:6417-6429`);
- `GfxWorld.dpvs.surfaces` (`src/database/db_load.cpp:6706-6711`).

An encoded offset placed in one of these fields is not an offset reference; it
still requests a new inline payload.

### Direct arena pointers

For a direct pointer, zero is null, `0xffffffff` requests a new inline object,
and another value is converted to an address inside an arena. `GfxWorld.sunLight`
and `GfxWorldDpvsPlanes.planes` demonstrate this class at
`src/database/db_load.cpp:6796-6807` and
`src/database/db_load.cpp:6745-6756`.

### Asset handles and aliases

Asset references, including the root `GfxWorld` header and each surface's
`Material *`, use:

- `0`: null;
- `0xffffffff`: allocate and load an inline asset;
- `0xfffffffe`: allocate and load inline, and reserve a four-byte alias slot in
  block 4;
- any other nonzero token: find an already populated alias slot and read the
  resolved asset pointer stored there.

The root world behavior is at `src/database/db_load.cpp:6956-6984`; material
handles use the same pattern at `src/database/db_load.cpp:2635-2663`.
`DB_InsertPointer` reserves a four-byte-aligned slot without consuming
compressed data (`src/database/db_stream.cpp:100-108`).

For both direct and alias forms, a normal token encodes:

```text
block = (token - 1) >> 28
offset = (token - 1) & 0x0fffffff
token = (block << 28) | (offset + 1)
```

Direct conversion returns `block[block].data + offset`. Alias conversion reads
the four-byte pointer stored at that location
(`src/database/db_stream_load.cpp:39-51`). The native functions do not validate
the block nibble, offset, width, alignment, or whether an alias slot has already
been populated. A portable decoder must reject block values 9 through 15,
offset zero underflow, an out-of-range access, a misaligned alias slot, an alias
before its definition, and sentinels used in the wrong token class.

## `GfxWorld` geometry layout and traversal

`GfxWorld` is a 732-byte Win32 structure
(`src/gfx_d3d/r_bsp.h:143-205`). The following offsets are derived from that
declared 32-bit layout and agree with the hard-coded loader sizes:

| Field | World offset | Wire size | Meaning in the strict subset |
| --- | ---: | ---: | --- |
| `indexCount` | `0x010` | 4 | Count of shared 16-bit indices |
| `indices` | `0x014` | 4 | Presence-only block-4 array token |
| `surfaceCount` | `0x018` | 4 | Number of `GfxSurface` records |
| `vertexCount` | `0x030` | 4 | Count of shared 44-byte vertices |
| `vd.vertices` | `0x034` | 4 | Presence-only block-4 array token |
| `vd.worldVb` | `0x038` | 4 | Native D3D runtime field; required zero by the subset |
| `vertexLayerDataSize` | `0x03c` | 4 | Required zero by the base/unlayered subset |
| `vld.data` | `0x040` | 4 | Required zero by the base/unlayered subset |
| `vld.layerVb` | `0x044` | 4 | Native D3D runtime field; required zero |
| `materialMemoryCount` | `0x174` | 4 | Number of eight-byte `MaterialMemory` records |
| `materialMemory` | `0x178` | 4 | Presence-only block-4 array token |
| `dpvs` | `0x244` | 104 | Embedded `GfxWorldDpvsStatic` |
| `dpvs.staticSurfaceCount` | `0x248` | 4 | Static-surface count |
| `dpvs.surfaces` | `0x294` | 4 | Presence-only block-4 surface-array token |

The root body is allocated and read in block 0. `Load_GfxWorld` then pushes
block 4 and traverses its children (`src/database/db_load.cpp:6774-6953`). For
the fields retained by this boundary, the compressed payload order is:

1. optional world name and base-name strings;
2. `indexCount * 2` index bytes (`src/database/db_load.cpp:6782-6787`);
3. any earlier enabled world dependencies, all forbidden in the strict subset;
4. `materialMemoryCount * 8` material-memory records and their material-handle
   traversal (`src/database/db_load.cpp:6378-6397,6861-6866`);
5. `vertexCount * 44` vertex bytes (`src/database/db_load.cpp:6400-6415`);
6. optional vertex-layer bytes, forbidden by this subset
   (`src/database/db_load.cpp:6417-6429`);
7. later enabled world dependencies, also forbidden;
8. `surfaceCount * 48` surface bytes, followed by each surface material-handle
   traversal (`src/database/db_load.cpp:2846-2865,6635-6740`).

The surfaces are near the end of the world traversal, even though their
presence token is embedded in the original 732-byte body. A parser cannot read
the root body and then jump directly to the surface payload without accounting
for every enabled earlier branch.

### World vertices

`GfxWorldVertex` is 44 bytes (`src/gfx_d3d/r_gfx.h:399-408`):

| Offset | Field |
| ---: | --- |
| `0` | `float xyz[3]` |
| `12` | `float binormalSign` |
| `16` | packed 32-bit color |
| `20` | `float texCoord[2]` |
| `28` | `float lmapCoord[2]` |
| `36` | packed 32-bit normal |
| `40` | packed 32-bit tangent |

The existing D3D-free `WebEngineWorldVertex` mirror asserts the same size and
offsets in `src/web/web_engine_world_surface.h:10-32`.

### Surfaces and indices

`GfxSurface` is 48 bytes (`src/gfx_d3d/r_gfx.h:278-287`):

| Offset | Field |
| ---: | --- |
| `0x00` | 16-byte `srfTriangles_t` |
| `0x10` | serialized `Material *` asset-handle token |
| `0x14` | lightmap, reflection-probe, primary-light, and flags bytes |
| `0x18` | `float bounds[2][3]` |

The triangle record contains signed `vertexLayerData`, signed `firstVertex`,
unsigned 16-bit `vertexCount`, unsigned 16-bit `triCount`, and signed
`baseIndex` (`src/gfx_d3d/r_gfx.h:14-21`). The world index slice begins at
`baseIndex`, contains `triCount * 3` entries, and each entry is local to
`firstVertex`. The native BSP construction and validation paths demonstrate
those conventions in `src/gfx_d3d/r_bsp_load_obj.cpp:1406-1470,1481-1508`.
The current browser converter preserves them in
`src/web/web_engine_world_surface.h:34-50,110-116`.

### Material identity versus material rendering

`MaterialMemory` is an independent eight-byte accounting record containing a
material handle and an integer byte count
(`src/gfx_d3d/r_material.h:506-510`). It is not the surface record itself.

Each surface has its own material asset handle. An inline `Material` body is 80
bytes; its first 24 bytes are `MaterialInfo`, whose first field is the material
name token. Technique-set, texture-table, constant-table, and state-bit-table
pointers follow (`src/gfx_d3d/r_material.h:434-473`). The complete traversal is
implemented at `src/database/db_load.cpp:2585-2663`.

The first extraction boundary resolves only a bounded material name/key. It
does not load a technique set, select shaders, resolve texture tables or images,
interpret lightmaps, or register assets in the global native database. All of
those dependency counts and tokens must be zero in the canonical fixture.

## Supported two-asset synthetic subset

The Milestone 10 portable format boundary is intentionally narrower than a
normal zone. `ExtractWorldSurface` requires:

- `IWffu100`, version 5, followed by exactly one complete zlib stream;
- a complete 44-byte `XFile` within the configured byte budgets;
- zero script strings;
- exactly two complete top-level `XAsset` records, read before either body;
- record 0 to be `ASSET_TYPE_MATERIAL` (`0x04`) with inline-shared token
  `0xfffffffe`, followed by record 1 as `ASSET_TYPE_GFXWORLD` (`0x10`) with
  inline token `0xffffffff`;
- one zero-dependency named material copied to stable job-owned metadata before
  its block-0 frame rewinds;
- exactly one surface and one static surface;
- nonzero storage and serialized payload only in blocks 0 and 4;
- base/unlayered vertices, with zero layer-data size and tokens;
- one `MaterialMemory` record and the surface handle both resolving through the
  already defined top-level material alias;
- every unrelated `GfxWorld`, DPVS, lighting, model, image, cell, visibility,
  shadow, and runtime pointer/count branch set to zero;
- zero native D3D buffer fields and zero material technique/texture/constant/
  state dependencies.

Anything else returns a specific unsupported or malformed-input error, not a
reason to continue speculatively. In particular, an unsupported nonzero pointer
must not be ignored: it changes the decompressed traversal and would
desynchronize every later record. The complete public error vocabulary is in
`src/web/web_fastfile_world_surface.h`.

The compatibility API may still own one complete in-memory vector, but it feeds
the same bounded queue used by `BeginStreaming`. The browser runtime supplies
37-byte copied source chunks across RAF callbacks and can pause with zero work
while the next chunk is unavailable. This is not permission to accept
user-owned fastfile data.
Successful extraction returns only selected owned vertex/index slices, source
metadata, block sizes, a bounded material name, and stable job-local material
and world identities. The runtime then calls `WebEngine_ConvertWorldSurface` and
`WebRenderer_SetSurface`.

### Canonical Milestone 10 fixture

The runtime deliberately preserves the offset quad used by Milestone 7. Its
world arrays contain six vertices and twelve indices, including prefix/suffix
guard records; the selected four-vertex, six-index surface starts at raw
`firstVertex = 1` and `baseIndex = 3`. Using the NUL-terminated material name
`web/synthetic` produces this exact logical arena layout:

```text
block 0 (732-byte high-water, terminal cursor 0)
    [  0,  80) top-level Material, then rewind
    [  0, 732) GfxWorld, then rewind

block 4 (380-byte persistent extent)
    [  0,  16) XAsset[2]
    [ 16,  20) material alias cell; no compressed bytes
    [ 20,  34) "web/synthetic\0"
    [ 34,  58) twelve u16 indices
    [ 58,  60) alignment gap; no compressed bytes
    [ 60,  68) MaterialMemory[1]
    [ 68, 332) six GfxWorldVertex records
    [332, 380) one GfxSurface
```

The complete two-record table is present before the material body. The
top-level material's `0xfffffffe` token reserves the alias cell at block-4
offset 16 before its 80-byte body is read. After validation, that cell is bound
to stable job-local identity 1; it never stores or exposes a serialized/native
pointer. Both the material-memory and surface tokens are `0x40000011`: block 4,
offset 16, plus the format's one-based adjustment. The world root and
presence-only array markers are `0xffffffff`; the latter fields semantically
accept any nonzero marker.

The decompressed byte sequence is different from the logical arena layout:

```text
XFile                         44 bytes
XAssetList                    16 bytes
XAsset[2]                     16 bytes
Material                      80 bytes
material name                 14 bytes
GfxWorld                     732 bytes
indices                       24 bytes
MaterialMemory                 8 bytes
vertices                     264 bytes
surface                       48 bytes
                              ---------
total                       1246 bytes
```

Neither the alignment gap nor the four-byte alias cell appears in the
compressed sequence. The two active block sizes sum to 1112 bytes: block 0 is
732 bytes and block 4 is 380 bytes. The runtime fixture writes that value to
`XFile.size`. The native loader does not establish a checked semantic for that
field, so the portable decoder records and bounds it without treating equality
to the block sum as inferred retail-format truth.

The surface triangle descriptor is:

```text
vertexLayerData = 0
firstVertex     = 1
vertexCount     = 4
triCount        = 2
baseIndex       = 3
```

All vertices, bounds, and projection-relevant values are finite. The six
selected indices are less than the surface-local vertex count; sentinel values
outside the raw slice prove that extraction honors both nonzero source offsets.
`ExtractedWorldSurface` retains `1` and `3` in metadata while its owned vectors
and `View()` normalize the extracted slice to zero-based ranges
(`src/web/web_fastfile_world_surface.h:69-95`).

## Milestone 9 resumable execution contract

`WorldSurfaceExtractionJob` retains the complete input allocation across calls.
`Begin` establishes the owned source and limits; each `Step` advances the
current inflate or traversal stage and reports only that call's work. Both its
compressed-input and inflated-output/traversal windows are independently capped
at 64 KiB, and no step completes more than 64 semantic records. A zero or
over-limit caller budget consumes no source and traverses no record, then puts
the job in its failed terminal state.

The browser starts this job for the canonical fixture and calls exactly one
`Step` from `WebEngineSurface_Frame` per RAF callback. The synchronous
`ExtractWorldSurface` API is retained, but it drives the same job to a terminal
state, so there is one validation and traversal implementation. This scheduling
change does not make the source stream incrementally readable: the job still
starts from a complete owned compressed file and retains inflated staging while
work is in progress. Both allocations are released when extraction succeeds,
before the result is taken. A failed job retains its state until `Reset`; the
browser runtime resets it immediately on failure.

No step writes a caller destination. `TakeResult` is available only after
success, atomically moves the fully owned result, and succeeds once. Calls made
before success, after failure, or after the successful take leave the supplied
destination unchanged. The runtime resets source and traversal staging before
conversion and renderer submission.

Milestone 9 also replaced the former monotonic-cursor assumption with checked
current cursors and high-water marks. In that milestone's original single-asset
fixture, nested loading temporarily raised block 0's high-water mark to 812
bytes and loader-frame pop rewound its current cursor; persistent block 4
advanced through 372 bytes. Milestone 10 retains that rule but places the
material and world in separate top-level block-0 frames, so their storage is
reused and the current high-water mark is 732 rather than their sum.

## Required ceilings and validation

The strict decoder exposes `kisak::fastfile::Limits` and applies its limits
before allocation, cursor movement, or output replacement. The implemented
defaults in `src/web/web_fastfile_world_surface.h:28-45` are:

| Resource | Initial ceiling |
| --- | ---: |
| Complete input, including prefix | 4 MiB |
| Total zlib output | 8 MiB |
| Sum of all nine logical block sizes | 8 MiB |
| Any individual logical block | 8 MiB |
| Script strings | 0 |
| Assets | 2 |
| Loader-frame stack depth | 64 |
| Delayed spans | 4096 |
| Delayed bytes | 8 MiB |
| Total string bytes | 256 |
| World/static surfaces | 1 |
| World vertices | 65536 |
| World indices | 262144 |
| Selected-surface vertices | 4096 |
| Selected-surface indices | 12288 |
| Selected-surface triangles | 4096 |
| Material-memory records | 1 |
| Material-name bytes before NUL | 255 |
| Alias slots | 1 |

The selected vertex/index ceilings match
`src/web/web_renderer_surface.h:10-14`; the larger world-array limits permit the
decoder to prove nonzero slicing without relaxing renderer publication. A
future broader traversal may choose different staging limits only through an
explicit memory-budget decision; it must still publish at most one
renderer-bounded surface at this seam.

Validation must include:

- checked addition, multiplication, alignment, and `size_t` conversion;
- nonnegative signed counts before conversion to an unsigned size;
- complete two-record table staging and exact Material-before-GfxWorld order;
- exact block-id, offset, width, and alias alignment/range checks;
- alias definition before use, single-definition policy, and stable identity
  agreement across the material-memory and surface references;
- every logical cursor and high-water mark not exceeding its declared block;
- for the canonical fixture, each active block's high-water mark exactly
  matching its declared size even when block 0 has rewound;
- exact zlib end, with truncation, excess output, and unexpected trailing data
  rejected by the strict subset;
- `firstVertex >= 0`, `baseIndex >= 0`, and subtraction-safe range checks;
- checked `triCount * 3` and bounds against the world index count;
- every selected index less than the selected surface vertex count;
- finite vertex components and bounds;
- a NUL terminator within the material-name limit;
- a valid, zero-dependency minimal material;
- destination atomicity: any error leaves the previously published surface
  unchanged.

Malformed tests must cover bad or authenticated magic, wrong version, prefix,
`XFile`, and zlib truncation, corrupt zlib data, early or excess output, every
count and allocation ceiling, block-size/cursor disagreement, invalid block
nibbles, offset underflow, out-of-range and misaligned aliases, alias-before-use,
sentinels in the wrong token class, unsupported nonzero branches, unterminated
names, negative and overflowing ranges, out-of-range local indices, non-finite
floats, and atomic failure.

The implementation reports those conditions through `kisak::fastfile::Error`
and stable `ErrorString` text, retains no serialized arena pointer, checks the
active logical high-water marks against their declared blocks, and exposes the
replacement result only after complete validation. `TakeResult` then moves it
atomically into the caller's destination.

## Why this is not general retail traversal

The top-level `XAsset` table is read in one operation, but its asset bodies are
then visited in table order. Inline asset bodies and their dependencies consume
the same zlib stream while switching among logical blocks
(`src/database/db_file_load.cpp:337-349`,
`src/database/db_load.cpp:7143-7255`). There is no independent byte length on
an `XAsset` record that permits an unknown type to be skipped.

Therefore a narrow `GfxWorld` parser cannot safely find a world following an
unsupported asset. Milestone 10 can traverse its one explicitly supported
material prefix because that exact generated order is known; it still cannot
scan through an arbitrary prefix. Even within `GfxWorld`, a nonzero unsupported
dependency before the surfaces changes the stream position. Supporting a
typical retail zone requires a much broader generated-loader traversal that can
safely parse every preceding asset type and dependency; resumable scheduling
alone does not provide that coverage. The canonical synthetic fixture exercises
the narrow extraction/conversion path but does not imply general traversal.

The following remain explicitly unsupported:

- authenticated fastfiles and versions other than PC version 5;
- any asset table other than the exact two-record Material/GfxWorld prefix, and
  general asset-table traversal;
- retail `.ff` or `.d3dbsp` map loading;
- layered world vertices, visibility, cells, models, lightmaps, shadows, and
  streaming/runtime blocks;
- the material technique, shader, texture-table, and image dependency graph;
- native/global asset registration, D3D buffers, or global database side
  effects; the supported identity is job-local only;
- accepting `.ff` files through the browser importer.

## Milestone 10 stream-machine and identity contract (complete)

The logical-block bookkeeping is now an explicit reusable nine-block machine,
not a collection of extraction-specific cursor increments. It classifies block
0 and blocks 4 through 8 as immediate, block 1 as zero-fill, and blocks 2 and 3
as delayed. Push/pop frames preserve nonzero-block advances while rewinding
block 0 to the frame's entry cursor without lowering its high-water mark.
Reservations align the arena cursor without consuming inflated bytes.

Every delayed read is retained as a checked `{block, offset, length}` span.
Replay begins only with a balanced frame stack and consumes those spans FIFO;
partial consumption preserves the unconsumed suffix. The generated loaders in
this repository select only blocks 0, 1, 4, 7, and 8, so a separate freely
generated stream-machine microfixture defines delayed-block behavior without
adding invented payloads to the two-asset zone.

The extraction job keeps its 64 KiB/64-record step limits and applies cumulative
ceilings to input, inflated output, total/per-block arena sizes, asset count,
frame depth, delayed spans and bytes, total strings, and aliases. The alias cell
moves from reserved/unresolved to exactly one stable job-local material identity
after the material body and name validate. The material's block-0 bytes may then
be reused by the world without invalidating either later alias resolution.

A read-only audit of a legally owned Steam installation can confirm that its PC
fastfiles use the expected version-5/zlib framing and that compressed DXT IWI
formats are common. That establishes framing and format prevalence only. No
proprietary file is copied into or committed to this repository, and neither the
decoder nor importer accepts retail `.ff` input.

## Milestone 11 source and registry contract (complete)

The source boundary is now a bounded one-chunk queue independent of the parser.
A feed is copied atomically, cannot exceed its per-chunk or cumulative byte
limit, and is rejected while unread bytes remain. Draining a chunk releases its
allocation. End-of-source is explicit: the parser can wait after zlib reports
stream end, then accept an empty final feed or reject any subsequent byte as
trailing data. Prefix bytes and compressed payload may be split at every byte
boundary without changing the extracted result.

The asset-lifetime boundary is now a reusable registry independent of
`GfxWorld` traversal. It owns asset records and names, allocates non-pointer
identities beginning at one, validates alias cell block/range/alignment against
declared arenas, and enforces unresolved-to-defined exactly once. Both
`MaterialMemory.material` and `GfxSurface.material` resolve through this
registry to material identity 1; the later top-level world is registered as
identity 2. No block-0 address or serialized token is retained as an identity.
`UnloadAll` releases names/assets/aliases while preserving configuration and
restarting identity allocation; `Reset` additionally removes configuration.

Portable tests cover starvation, backpressure, caller-buffer independence,
split prefixes, explicit EOF, truncated and trailing zlib input, alias type and
range errors, duplicate publication, unload/reset, and exact output equivalence.
The browser supplies the canonical synthetic file in 37-byte chunks over
separate RAF callbacks and reports its source lifecycle. The supported asset
shape is unchanged and retail input remains disconnected.

## Milestone 12 installation/VFS boundary (complete)

The allowlisted English F.N.G. profile now contains 21 stock IWDs and four
fastfiles in addition to localization. Fastfile selection validates only a
14-byte window: unsigned `IWffu100`, little-endian version 5, and a valid zlib
header without a preset dictionary. IWD validation retains its bounded ZIP32
head/tail/central windows. OPFS restore verifies the schema-2 manifest, all 26
canonical paths, every recorded file size, and all bounded probes before the
profile becomes active.

The read-only browser VFS can open immutable sources for the startup and
`killhouse` zones, perform range-checked reads, cancel delayed work without a
late Wasm-memory write, and detect a cleared/replaced import generation. It does
not hand those bytes to this document's strict zone extractor. Automated tests
verify that reading `killhouse.ff` leaves the synthetic extraction generation
unchanged. A read-only retail audit confirmed the selected files and framing;
no retail bytes are test fixtures or repository content.

## Milestone 13 qcommon boundary (complete)

The portable qcommon startup machine now consumes the M12 profile through the
asynchronous VFS and stops at `pre-database`. It performs three bounded local
initialization actions and stat/read pairs for the 26 allowlisted files. Only
148 aggregate header bytes cross this boundary. The four `.ff` reads validate
their 14-byte unsigned-version-5/zlib framing and are not routed into either a
retail loader or this document's strict synthetic two-record extractor.

The browser advances at most one startup action per RAF callback and publishes
generation, action, file, byte, memory, event, and current-path diagnostics.
Tests cover repeatable generations, cancellation while a VFS request is live,
and typed path-specific I/O failure.

## Milestone 14 scheduler boundary (complete)

The original seven browser frame responsibilities run through one fixed-capacity
cooperative scheduler in deterministic dependency order. M15 adds the retail
census as an eighth task. A frame now admits no more than eight task calls,
320 KiB of reservations, and 320 reserved records, and it
checks a 12-ms wall-time window between callbacks. Generational ownership,
single-delivery cancellation, starvation history, and protocol quarantine are
portable and tested independently from browser timing. Sampled browser traces
confirm the exact 266,254-byte/267-record admitted schedule and unchanged
synthetic extraction and renderer output.

## Milestone 15 retail census boundary (complete)

The prefix-only reader now consumes `code_post_gfx.ff` through the browser VFS,
the M11 source queue, and the M14 scheduler. It validates unsigned version-5
framing, bounded zlib output, XFile and all nine block declarations, the script
string pointer table and inline strings, and the complete XAsset table. It
records every type and pointer class but stops at the byte immediately before
asset body zero. Unknown generated records remain non-skippable.

The legally owned Steam file was inspected read-only. Its exact prefix is:

- file bytes: 872,586;
- XFile size/external size: 1,378,265 / 950,499;
- blocks 0..8: 498,816, 0, 0, 0, 407,412, 0, 0, 4,224, 480;
- script strings: 107 slots (106 inline, one null), with string data ending at
  inflated offset 1,830;
- assets: 1,639 headers, ending at inflated offset 14,942;
- asset zero: type 5 (`techset`), inline token `0xffffffff`.

The nonzero census is: physpreset 1, xanim 11, xmodel 1, material 72,
techset 90, image 7, sound 11, sndcurve 1, lightdef 2, font 9, menufile 2,
localize 1,351, weapon 1, snddriverglobals 1, fx 1, impactfx 1, rawfile 76,
and stringtable 1. Exactly 1,638 asset headers are inline and one is a normal
alias. These facts are documentation and acceptance evidence; retail bytes are
not test fixtures and were neither copied nor modified.

## Milestone 16 technique-set boundary (complete)

The reader now models the generated loader's block transitions and advances
from the census through asset zero. The accepted sequence is the inline
148-byte technique-set body in block 0, its block-4 name, exactly one populated
inline technique reference, the technique header and complete pass-header
array, the first inline vertex declaration, the inline vertex-shader header and
name, and its bounded DWORD program. It stops before
`Load_CreateMaterialVertexShader`; the later pixel shader, shader arguments,
technique name, and remaining top-level assets are not skipped.
`Load_BuildVertexDecl` is replaced only by an owned 32-byte portable routing
descriptor and hash, not by a native or WebGL object.

The real read-only boundary is technique set `sm2/2d`, technique slot 4, one
pass, three vertex streams, and vertex shader `vertcol_simple.hlsl`. Its program
is 103 DWORDs with FNV-1a `0x66467e0a`; serialized traversal stops at inflated
offset 15,673. Logical offsets are block-4 asset table 1,772, technique 14,892,
vertex declaration 14,920, vertex shader 15,020, name 15,036, and bytecode
15,056. Block 0 has high-water 148 and block 4 has cursor 15,468. Since the
native D3D creation operation has no browser implementation yet, zero assets
are published at this boundary.

## Milestone 17 paired shader and asset-zero boundary (complete)

The portable decoder now validates D3D9 instruction framing and the embedded
CTAB structures for the first vertex/pixel pair. `sm2/2d` resolves the pixel
shader name back to `vertcol_simple.hlsl`; its PS 2.0 program is 55 DWORDs with
FNV-1a `0x523f57e2`. The paired structural contract selects
`webgl2.vertcol_simple2d.v1`, after which three material arguments (hash
`0x90244fa9`) and technique name `vertcol_simple2d` are consumed. The real
read-only traversal ends at inflated offset 15,950 with block-4 cursor 15,745,
block-0 high-water 148, and one fully completed top-level asset. M18 owns actual
WebGL2 program creation; M17 stores no graphics handles.

## Milestone 18 renderer boundary (complete)

The stable compatibility ID now resolves to compiled-in GLSL ES 3.00 inside the
renderer. WebGL2 compilation, link, locations 0/1/2, both matrix uniforms, and
the sampler uniform all validate before the program becomes active. The first
indexed draw uses two identity matrices and texture unit zero; success is
reported only after `glDrawElements` completes without a GL error. Failed
binding validation leaves the bootstrap program drawing, while context restore
rebuilds the compatibility program from bounded CPU-owned source descriptions.
No retail shader bytecode is passed to WebGL and no retail surface is claimed.

## Milestone 19 compressed-image boundary (complete)

IWI formats 11/12/13 now have a bounded portable RGBA8 decode path for DXT1,
DXT3, and DXT5. Exact block/mip layout, smallest-to-largest mip order, partial
edge blocks, RGB565 interpolation, DXT1 transparency, and both alpha encodings
are validated before atomic publication. Cubemaps, volumes, unknown flags, and
decoded output above 4 MiB remain outside the boundary. The existing archive
job can therefore bind the owned install's deterministic first bounded image
(`images/$black.iwi`) to the M18 sampler instead of reporting format 11 as
unsupported.

This milestone joined a real compressed IWI image path to the retail-derived
shader contract, but at that boundary the surface's material registry identity
was still synthetic.

## Milestone 20 material/image registry boundary (complete)

The supported retail prefix now extends through top-level assets 0–2:

| Index | Type | Name | Publication |
| ---: | --- | --- | --- |
| 0 | techset | `sm2/2d` | compatibility identity 1 |
| 1 | techset | `2d` | material-technique identity 2 |
| 2 | material | `ui_cursor` | material identity 4 |

The nested `GfxImage` `3_cursor3` is published as identity 3 before its owning
material, matching generated-loader dependency order. The material's technique
token resolves the asset-one pointer cell in block 4; its sole non-water texture
record owns a separately reserved image cell. All four registry aliases are
defined before the result becomes visible.

The material body is 80 bytes with texture/constant/state-bits counts 1/0/1.
Its texture record is 12 bytes and references one inline 36-byte 2D image. The
image name produces `images/3_cursor3.iwi`; its 16-byte load definition reports
64 by 64 by 1, DXT3, and zero embedded resource bytes. Parsing still models the
nested block-0/block-4 frames, so temporary material and image bodies rewind
without invalidating owned names or registry identities. The final owned-file
boundary is inflated offset 17,013, block-0 high-water 148, and block-4 cursor
16,532.

Only the first technique set selects the compiled-in shader substitution. The
second renderer-one shader pair is bounded and structurally framed so traversal
can reach the material, but it does not select GLSL or create GPU state. The
browser binds the IWI selected by the registered image name; a missing member
does not fall back to another archive entry. Synthetic tests reproduce the
layout and use a decoy lower-sorting image without containing retail data.

## Milestone 21 killhouse world-asset inventory (complete)

The table-only world inventory mode uses the same bounded streaming inflater
and nine-block declarations but returns atomically at the asset-table boundary.
It requires at least one type-16 `gfx_map`/`GfxWorld` header, records its exact
index and pointer token, retains a rolling FNV-1a hash over every serialized
eight-byte table record, and snapshots both type and pointer-reference counts
before the first world. No registry cell or asset identity is published because
body zero has not run.

The owned F.N.G. `killhouse.ff` observation is:

| Field | Value |
| --- | ---: |
| File bytes | 70,391,800 |
| Compressed bytes consumed | 16,295 |
| Inflated table boundary | 30,747 |
| Total asset headers | 1,684 |
| Table-order hash | `0x12e39952` |
| First body | inline `techset` at index 0 |
| First `GfxWorld` | inline index 772 |
| References before world | 772 inline; 0 shared, alias, or null |

Those 772 preceding entries comprise 146 `xanim`, 315 `xmodel`, 218 `techset`,
one `com_map`, one `lightdef`, 10 `weapon`, 60 `fx`, and 21 `rawfile` headers.
The result is a dependency-order proof, not permission to jump to offset 772:
all preceding bodies must be traversed so their nested allocations and aliases
are defined exactly as the generated loader expects.

The synthetic fixture uses seven headers and a world at index five. M21 stops
at its table boundary even though the fixture now also carries the M23
two-technique-set prefix. A companion table with no world fails with
`GfxWorldMissing` and cannot expose a partial result.

## Milestone 22 first killhouse asset boundary (complete)

The owned table's first entry is an inline 148-byte `MaterialTechniqueSet`.
The generated loader reads its XString and then visits the fixed 34-element
`MaterialTechniquePtr` array. M22 models exactly that ordering and distinguishes
null, inline (`0xffffffff`), shared-sentinel (`0xfffffffe`), and normal encoded
references without assuming that the pointer helpers are interchangeable.

The read-only owned result is:

| Field | Value |
| --- | --- |
| Name | `,sm2/mc_l_sm_r0c0s0` |
| World vertex format | 0 |
| Remapped set token | null |
| Technique tokens | 34 null |
| Inflated boundary | 30,915 |
| Block-0 high-water | 148 |
| Block-4 cursor | 30,708 |
| Registry publication | identity 1; alias 1/1 defined |
| Next body | inline `techset` at asset index 1 |

Because every nested technique token is null, body zero has no dependent
`MaterialTechnique` allocation. The parser can pop the name and asset frames,
register the set, and publish its table cell exactly where upstream calls
`Load_MaterialTechniqueSetAsset`. Dependency-bearing fixtures instead stop
before the first technique body with the alias still reserved and undefined.
Malformed headers likewise expose no partial registry state.

## Milestone 23 consecutive killhouse technique sets (complete)

The same body loader now loops while the next top-level table entry is an inline
type-5 technique set. Each completed zero-dependency body is registered by its
asset index and name, then atomically publishes the exact block-4 table-cell
alias reserved for that entry. A dependency-bearing body remains reserved but
undefined, and a malformed later body makes the entire public result
unavailable.

The read-only owned result is:

| Field | Value |
| --- | --- |
| Technique-set bodies entered | 12 (asset indices 0–11) |
| Completed/published | 12 |
| Stable identities | 1–12 |
| Registry publication | 12 aliases; 12 defined |
| Technique pointers | 408 null; no nested technique body |
| Final inflated boundary | 32,729 |
| Block-0 high-water | 148 |
| Block-4 cursor | 30,894 |
| Next body | inline `xmodel` at asset index 12 |

The names cover the owned leading `mc_l` shadow/light technique variants; the
first remains `,sm2/mc_l_sm_r0c0s0` and the twelfth is
`,mc_l_hsm_b0c0`. The next byte belongs to an `XModel` body and is not parsed.
This remains a generated-loader prefix proof, not a model or map render.

## Milestone 24 first killhouse XModel prefix (complete)

The checked traversal now enters asset 12 in the same field and pointer order
as upstream `Load_XModel`. It validates the 220-byte fixed record and consumes
the bounded skeleton prefix: name, bone script-string tokens, child-parent
indices, child quaternions and translations, part classifications, and base
matrices. It stops before `Load_XSurfaceArray`; no surface, material, collision,
or physics body is skipped.

The read-only owned result is:

| Field | Value |
| --- | --- |
| Asset index / type | 12 / inline `xmodel` |
| Name | `ch_street_wall_light_01_off` |
| Bones | 1 total; 1 root |
| Bone 0 | token 1 / `polysurface269` |
| Surfaces | 6 declared |
| LODs | 3; distances 800, 1,200, and 1,000,000; two surfaces each |
| Collision | LOD 2; 2 surfaces declared |
| Radius | approximately 45.003 |
| Bounds | min `[0, -14.6680, -12.6054]`; max `[44.5406, 14.6680, 22.7265]` |
| Model memory usage | 24,847 bytes |
| Final inflated boundary | 33,012 |
| Block-0 high-water | 220 |
| Block-4 cursor | 30,960 |
| Registry publication | 13 aliases reserved; 12 defined; XModel unpublished |
| Stop operation | `Load_XSurfaceArray` |

The synthetic fixture uses a freely generated one-bone model and proves the
same ordering without copying retail data. Separate cases preserve a valid
fixed header at an unsupported bone-name reference and fail closed for invalid
bounds or an out-of-range bone script-string token. This is an XModel metadata
and skeleton-prefix result, not a model render.

## Milestone 25 first killhouse XSurface prefix (complete)

The loader now consumes the six fixed `XSurface` records and follows each
surface's dependencies in upstream order: blend metadata, block-7 packed
vertices, block-4 rigid lists and collision trees, then block-8 triangle
indices. It retains structural records and hashes of large payloads, not retail
geometry bytes. After all surfaces complete, it inventories the six material
handle tokens and stops before the first inline material body.

The read-only owned result is:

| Surface | Vertices | Triangles | Rigid lists | Collision nodes | Collision leaves |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 368 | 252 | 1 | 21 | 133 |
| 1 | 17 | 24 | 1 | 3 | 18 |
| 2 | 213 | 140 | 1 | 10 | 72 |
| 3 | 9 | 12 | 1 | 1 | 10 |
| 4 | 141 | 92 | 1 | 8 | 48 |
| 5 | 6 | 4 | 1 | 1 | 3 |
| **Total** | **754** | **524** | **6** | **44** | **284** |

| Boundary field | Value |
| --- | --- |
| Inflated boundary | 62,228 |
| Surface-prefix bytes | 29,216 |
| Block-4 cursor | 32,960 |
| Material slots | `ffffffff`, `ffffffff`, `400080a9`, `400080ad`, `400080a9`, `400080ad` |
| Registry publication | 13 aliases reserved; 12 defined; XModel unpublished |
| Stop operation | `Load_Material` |

The freely generated two-surface fixture exercises the same block switching,
including an inline collision tree and a null collision-tree reference.
Pointer/count mismatch and invalid collision scale fixtures fail atomically.
This is complete XSurface dependency metadata for one model, not a decoded or
rendered retail model.

## Milestone 26 first complete killhouse XModel (complete)

The generated-loader subset now continues from the six material-handle cells
through two inline `Material` bodies. The remaining four handle tokens decode
to the first two block-4 cells and resolve through the typed registry:

| Handle | Serialized token | Resolved material identity |
| ---: | ---: | ---: |
| 0 | `ffffffff` | 16 |
| 1 | `ffffffff` | 18 |
| 2 | `400080a9` | 16 |
| 3 | `400080ad` | 18 |
| 4 | `400080a9` | 16 |
| 5 | `400080ad` | 18 |

The first material has three texture records, two constants, and seven state
records. It publishes three inline images, including the exact zero-resource
`,$identitynormalmap` layout. The second material has two textures, one
constant, and four state records; one image token resolves to the first
material's published image and its other image is inline. Every technique-set,
material, and image reference is type checked before use.

After materials, upstream order is preserved across two 44-byte
`XModelCollSurf_s` headers, 92 and four collision triangles respectively, one
40-byte `XBoneInfo`, the null `PhysPreset *`, and the null `PhysGeomList *`.
Large triangle arrays are validated for finite values and hashed rather than
retained.

| Boundary field | Value |
| --- | --- |
| Inflated boundary | 67,723 |
| Collision triangles | 96 |
| Collision dependency bytes | 4,696 |
| Block-0 high-water | 352 |
| Block-4 cursor | 38,112 |
| Registry publication | 19 aliases reserved; 19 defined; XModel identity 19 published |
| Stop operation | none; dependency chain complete |

The freely generated six-surface fixture proves two inline materials, four
material aliases, an image alias, collision and bone-info traversal, and atomic
XModel publication. Separate fixtures reject undefined aliases, invalid
collision bounds, and invalid bone info. At this historical M26 boundary, a
non-null inline physics preset stopped before its body; M39 below supersedes
that limitation. No retail payload is stored in the repository.

## Milestone 27 first rendered killhouse XSurface (complete)

At the M27 boundary the dependency result retained only surface zero's bounded
packed vertex and index bytes. For the owned model this was 11,776 vertex bytes
and 1,512 index bytes; the other five surfaces remained hash-only. M29 below
supersedes that retention rule for the declared first LOD. The engine converter
decodes and validates records outside both the zone parser and WebGL2.

| Render field | Value |
| --- | --- |
| Model / surface | `ch_street_wall_light_01_off` / 0 |
| Packed vertices | 368 records / 11,776 bytes |
| Triangle indices | 756 / 1,512 bytes |
| Resolved material | identity 16 / `mc/mtl_street_light_02` |
| Projection axes | X horizontal / Z vertical |
| Renderer topology | indexed triangle list |
| Publication | atomic replacement after complete XModel and shader publication |

Every position, binormal sign, and half-float UV must be finite; every index
must be below 368; and the complete byte lengths must match the checked header.
The browser renderer owns only the converted three-dimensional preview
positions, RGB color, UVs, and 16-bit indices used for context-loss recovery. Conversion or
binding failure leaves the synthetic bootstrap surface active. Automated
fixtures generate their packed geometry freely; no owned vertex or index bytes
are copied into the repository.

This is a first retail model-surface render. The current sampler image still
comes from the startup material path, so correct XModel color-map selection is
the next bounded dependency rather than an implied part of M27.

## Milestone 28 rendered XModel color map (complete)

The rendered surface's material identity is now the only entry point to image
selection. The portable engine selector finds that one published material,
requires exactly one texture with semantic 2 (`TS_COLOR_MAP`), and resolves its
typed image identity across the retained dependency records. It rejects
filename-order substitution, duplicate semantics or identities, built-ins,
embedded resources, and anything other than a bounded external 2D DXT image.

| Binding field | Owned F.N.G. value |
| --- | --- |
| Surface material | identity 16 / `mc/mtl_street_light_02` |
| Texture semantic | 2 / `TS_COLOR_MAP` |
| Image | identity 15 / `street_light_02_col` |
| Constructed member | `images/street_light_02_col.iwi` |
| Owning archive | `main/iw_03.iwd` |
| IWI metadata | v6, DXT1 format 11, 512 x 512 x 1 |

The archive job searches the fourteen required base IWDs sequentially and
mounts only the index containing the exact selected member. A missing member
falls back to mounting the primary archive for unrelated engine work but never
falls back to another image. Unsupported selection and failed read/decode/upload
preserve the current renderer texture; successful replacement reuses the
existing renderer recovery ownership.

The synthetic end-to-end profile locates its selected image in `iw_03`, binds
it to the one M27 surface, and has negative built-in and missing-member cases.
No retail IWI bytes are retained in parser output, JavaScript state, tests, or
repository fixtures.

## Milestone 29 first-LOD XModel draw list (complete)

Retention now follows `lods[0].surfaceIndex/surfaceCount` instead of an
implicit surface-zero rule. Each candidate must independently survive packed
geometry conversion and typed color-map resolution before it enters the
renderer-owned list. All accepted surfaces use one projection fitted to their
combined finite bounds, so separate XSurface records preserve their relative
placement.

| First-LOD field | Owned F.N.G. value |
| --- | --- |
| Surface range | indices 0-1 / 2 draws |
| Combined geometry | 385 vertices / 828 indices |
| Projection axes | X horizontal / Z vertical |
| Texture slot 0 | `street_light_02_col`, 512 x 512 DXT1 |
| Texture slot 1 | `street_light_bulb_02_off_col`, 64 x 64 DXT1 |
| Exact archive membership | both in `main/iw_03.iwd` |

Renderer ceilings are 16 draws, 16,384 vertices, 49,152 indices, eight texture
slots, 4 MiB per decoded texture, and 16 MiB total retained texture pixels.
Archive discovery and IWI upload advance one typed slot at a time; a later
missing, unsupported, or failed slot cannot discard earlier successful draws.
The renderer recreates the combined buffers and every resident slot after
context loss. No owned geometry, archive member, decoded pixel, or derived
asset payload is written to the repository.

The M29 correction keeps the projection's remaining model axis as normalized
depth, enables depth testing, applies compatibility-program aspect correction,
and discards fully transparent sampled texels. Typed material bindings also
retain the parsed sampler byte; both owned lamp slots report state 11 and are
therefore filtered and repeated from their retail metadata rather than from a
lamp-specific renderer rule.

## Milestone 30 first post-XModel technique set (complete)

Traversal now returns to `Load_XAssetHeader` after the complete asset-12
XModel and enters exactly one following inline type-5 record. The same bounded
`MaterialTechniqueSet` loader used for the leading map prefix owns the result;
the M30 mode stops after this record rather than falling through into arbitrary
later bodies.

| Boundary field | Owned F.N.G. value |
| --- | --- |
| Asset index / type | 13 / inline `techset` |
| Name | `,sm2/mc_l_sm_r0c0n0s0` |
| World vertex format / remap | 0 / null |
| Technique pointers | 34 null |
| Identity / alias | 20 / published |
| Inflated boundary | 67,893 |
| Block-0 high-water | 352 |
| Block-4 cursor | 38,134 |
| Registry publication | 20 aliases reserved; 20 defined |
| Next body | inline `techset` at asset index 14 |

The table cell is reserved before the body is entered and published only after
the complete header, bounded name, pointer classification, and zone-frame
unwind succeed. A malformed record makes the whole result unavailable. A
synthetic set with one inline dependency stops before `Load_MaterialTechnique`,
preserving the published XModel but leaving the asset-13 alias undefined. No
retail body bytes or derived asset payload are stored in the repository.

## Milestone 31 consecutive post-XModel technique sets (complete)

The generated loader now continues from the M30 asset-13 publication through
the consecutive inline type-5 run. Every set uses the existing bounded header,
name, pointer classification, zone-stack unwind, registry, and alias rules.
Publication remains per-body and atomic; a malformed later header makes the
whole result unavailable, while a supported earlier set remains published when
the next set stops before a nested `MaterialTechnique`.

The read-only owned profile records this exact run:

| Boundary field | Owned F.N.G. value |
| --- | --- |
| Asset range | 13–20 |
| Bodies entered/completed | 8 / 8 |
| Identities | 20–27 |
| Final set | `,mc_l_hsm_r0c0n0s0` |
| Final inflated boundary | 69,063 |
| Block-0 high-water | 352 |
| Block-4 cursor | 38,268 |
| Registry publication | 27 aliases reserved; 27 defined |
| Next body | inline `XModel` at asset index 21 |

The eight names and body boundaries are recorded in `docs/web-port.md`. No
asset-21 bytes are interpreted, no second XModel is retained or rendered, and
no proprietary byte is added to source or fixtures.

## Milestone 32 second retail XModel boundary (complete)

The generated loader now enters inline type-3 asset 21 after completing the M31
run. A distinct result record owns the second model's fixed header, bounded
name, bone script-string resolutions, classification bytes, and base matrices;
the published first XModel record is never reused or reset.

| Boundary field | Owned F.N.G. value |
| --- | --- |
| Asset index / type | 21 / inline `xmodel` |
| Name | `com_steel_ladder` |
| Bones / roots | 1 / 1 |
| Surfaces / LODs | 3 / 3 |
| Collision surfaces | 1 |
| Radius | approximately 200.696 |
| Memory usage | 24,551 bytes |
| Inflated boundary | 69,335 |
| Name block-4 offset / final cursor | 38,268 / 38,324 |
| Registry publication | 28 aliases reserved; 27 defined |
| Stop | before `Load_XSurfaceArray` |

The asset-21 alias remains undefined, no surface payload is consumed, and the
first model retains identity 19 and its existing renderer-owned draw list. The
repository contains only generated fixtures and metadata assertions, never
owned model bytes.

## Milestone 33 second retail XSurface prefix (complete)

The generated loader now continues asset 21 through the shared XSurface stages
without changing the first model's renderer publication.

| Boundary field | Owned F.N.G. value |
| --- | --- |
| Asset index / name | 21 / `com_steel_ladder` |
| Surface records | 3 / 3 traversed |
| Vertices / triangles | 750 / 488 |
| Rigid lists | 3 |
| Surface payload | 28,236 bytes |
| Per-surface vertices | 422, 200, 128 |
| Per-surface triangles | 296, 128, 64 |
| Material handles | 3 (`inline`, alias, alias) |
| Inflated boundary | 97,571 |
| Block-4 cursor | 39,644 |
| Registry publication | 28 aliases reserved; 27 defined |
| Stop | before `Load_Material` |

Each packed vertex and index range is bounded and hashed as traversal evidence;
none is retained as a renderer payload. The first material body and every later
collision, bone-info, and physics dependency remain untouched. A freely
generated three-surface fixture exercises the same ordering, and a mismatched
second-surface pointer/count pair fails without exposing a partial result.

This is shared XModel parser work, not a model viewer. The next boundary is to
reuse the checked material and remaining dependency stages for the active model
so asset 21 can publish, then turn the top-level XModel handling into a
repeatable loader for the models that precede `GfxWorld`.

## Milestone 34 complete second retail XModel dependencies (complete)

The active-model seam now covers the remainder of `Load_XModel`. The owned
asset-21 result is:

| Boundary field | Owned F.N.G. value |
| --- | --- |
| Model / identity | `com_steel_ladder` / 32 |
| Material / identity | `mc/mtl_steel_ladder` / 31 |
| Material technique-set identity | 24 |
| Material textures / constants / state bits | 3 / 2 / 7 |
| Published image identities | 28, 29, 30 |
| Collision surfaces / triangles | 1 / 296 |
| Collision payload | 14,252 bytes |
| Bone-info hash | `0x604bd5f6` |
| Physics dependencies | null preset and geom list; complete |
| Inflated publication boundary | 112,348 |
| Block-4 cursor | 54,188 |
| Registry assets / aliases | 32 / 33 reserved and defined |
| Next body | inline `xmodel` asset 22 |

All three material handles resolve to identity 31. The two repeated tokens are
`0x40009af1`; owned traversal shows that this canonical material alias targets
the published material's checked texture-table allocation. Resolution remains
strictly typed and bounded, and malformed generated aliases publish no partial
world result.

No second-model geometry is retained or rendered.

## Milestone 35 bounded consecutive XModel collection (complete)

The first/second result split has been replaced by a bounded `worldXModels`
collection. The generated loader uses one active collection index and repeats
the complete M34 dependency path while consecutive inline type-3 assets remain.
Entry zero is the initial renderer selection, while every eligible model may
retain packed first-LOD vertex/index bytes under the aggregate ceiling.

The owned asset-22 result is:

| Boundary field | Owned F.N.G. value |
| --- | --- |
| Model / identity | `com_steel_ladder_top` / 33 |
| Collection entries | 3 (assets 12, 21, and 22) |
| Material identities | 31, 31, 31, 31 |
| Surfaces / vertices / triangles | 4 / 660 / 420 |
| Rigid lists / surface payload | 4 / 25,008 bytes |
| Collision surfaces / triangles | 1 / 228 |
| Collision payload | 10,988 bytes |
| Bone-info hash | `0x499d1ece` |
| Physics dependencies | null preset and geom list; complete |
| Inflated publication boundary | 148,660 |
| Block-4 cursor | 66,676 |
| Registry assets / aliases | 33 / 34 reserved and defined |
| Next body | inline technique-set asset 23 |

Asset 22 reuses the material published for asset 21, so it adds no material or
image bodies. Eligible later models now retain bounded first-LOD render payloads
under one aggregate ceiling. Generated coverage
uses a third zero-surface model to prove repeatable completion, rejects invalid
third-model bounds without publishing a partial collection, and enforces an
explicit `maxWorldXModels` ceiling.

## Milestone 36 reusable XModel loader dispatch (complete)

The canonical `WorldXModelLoader` mode now drives one bounded complete XModel
operation from the shared supported-asset dispatcher. It handles consecutive
models and models separated by technique-set runs; `WorldXModelCollection`
remains an alias for compatibility with earlier callers. Each result records
whether renderer payload is available and whether it is currently selected;
entry zero is only the initial browser policy.

The launcher selector lists retained models and switches the renderer-owned
draw list through a narrow exported operation without reparsing the fastfile.
Material and image aliases that point at an earlier model are copied into
deduplicated resolved catalogs for the referring model, so the new material
queue can load the selected model's typed color maps. A shared 16 MiB retention ceiling
bounds the collection, and models beyond the available budget remain valid
inventory entries but are disabled in the selector.

Generated coverage exercises `XModel -> technique sets -> XModel -> technique
set`, in addition to the existing three-model repeat, zero-surface, malformed
third-model, and collection-ceiling cases. This proves that XModel completion
returns to a reusable dispatcher rather than a first/second/consecutive special
case.

The owned traversal now enters asset 23 after publishing all three reached
XModels:

| Field | Value |
| --- | --- |
| Asset / name | 23 / `sm2/mc_unlit` |
| Technique references | 16 null / 2 inline / 0 shared / 16 alias |
| Header/name boundary | 148,821 inflated bytes |
| Block-4 cursor | 66,689 |
| Registry assets | 33 published |
| Registry aliases | 35 reserved / 34 defined |
| Stop | first nested `Load_MaterialTechnique` |

Asset 23 remains unpublished, so no partial technique set or alias escapes the
dependency boundary. M37 should implement the reusable nested technique loader
and return to the same top-level dispatcher. Full support for the 315 pre-world
XModels cannot be claimed until the intervening asset classes can also be
traversed to reach them.

## Milestone 37 reusable MaterialTechnique dependencies (complete)

The owned asset-23 dependency result is:

| Field | Value |
| --- | --- |
| Parent / identity | `sm2/mc_unlit` / 34 |
| Parent references | 16 null / 2 inline / 0 shared / 16 alias |
| Slot 4 | `vertcol_simple_fog_dtex`, 1 pass, VS 201 DWORDs, PS 77 DWORDs, 5 arguments |
| Slot 28 | `wireframe_solid_dtex`, 1 pass, VS 74 DWORDs, PS 29 DWORDs, 1 argument |
| Dependency boundaries | 150,210 and 150,864 inflated bytes |
| Parent publication boundary | 150,864 inflated bytes |

The reusable operation then completes assets 24–32. Asset 32,
`mc_effect_falloff_add_nofog`, publishes as identity 43 at inflated offset
166,717. The dispatcher next enters XModel asset 33,
`com_studio_light_on`; its 15 surface headers and bounded dependencies complete,
but the current typed registry cannot yet resolve the first valid image alias
reached by its material. The parser stops at `Load_GfxImage(alias)` with the
fourth model unpublished. At this boundary 33 top-level assets are complete,
the block-4 cursor is 89,360, and the registry contains 43 assets with 50
aliases reserved / 48 defined.

Automated fixtures contain only generated shader and structure bytes. They
prove two-dependency completion, parent publication after the second dependency,
dispatcher return, invalid-second-shader rejection, and fail-closed handling of
incomplete dependency data.

## Milestone 38 reusable GfxImage aliases (complete)

M38 accounts for the four-byte block-4 insertion cell emitted by
`DB_InsertPointer` for every shared `GfxImage` texture-load definition. This
corrects the logical arena addresses used by later image aliases while keeping
the registry type checked. The asset-33 alias at token `0x40015c45` now lands on
the already published third texture pointer and resolves to
`tripod_studio_light_col`, identity 46.

The owned result after resuming the dispatcher is:

| Field | Value |
| --- | --- |
| Asset 33 | `com_studio_light_on`, identity 54, five materials / five resolved images |
| Asset 33 boundary | 257,898 inflated bytes |
| Additional image variant | `floodlight_beam`, format `0x16`, zero inline resource bytes |
| Asset 34 | `com_drop_rope`, identity 59, three resolved images |
| Asset 34 boundary | 374,026 inflated bytes |
| Next parent | asset 35, `mil_sandbag_desert_single_flat`, unpublished |
| Stop | inline `Load_PhysPreset` |
| Completed top-level assets | 35 |
| Last published registry snapshot | 59 assets; 63 aliases reserved / 62 defined |
| Block-4 cursor | 183,868 |

The partial asset-35 prefix has three surfaces, 243 vertices, 334 triangles,
one completed material with two images, 236 collision triangles, and completed
bone info. Those child results do not publish the parent XModel. Generated
fixtures verify both successful shared-image alias resolution and rejection of
an undefined alias before publication.

## Milestone 39 reusable PhysPreset dependencies (complete)

The reusable XModel operation now follows the generated type-1 asset loader for
inline and shared `PhysPreset` references. It accounts for the 44-byte block-0
record, the optional shared-pointer insertion cell in block 4, and the two
block-4 strings. The body must contain inline name and sound-prefix tokens,
finite numeric fields, a canonical Boolean, and zero padding. Registration and
both applicable pointer aliases occur only after the full record and strings
complete.

The owned result advances asset 35 to its next dependency:

| Field | Value |
| --- | --- |
| Parent | asset 35, `mil_sandbag_desert_single_flat`, unpublished |
| Preset / identity | `sandbag` / 63, published |
| Sound alias prefix / type | empty / 0 |
| Mass / bounce / friction | 20 / 0.01 / 0.3 |
| Bullet / explosive force scale | 0.6 / 0.25 |
| Body block-0 offset | 220 |
| Name / sound block-4 offsets | 183,872 / 183,880 |
| Inflated boundary | 397,206 |
| Registry assets | 63 |
| Registry aliases | 65 reserved / 64 defined |
| Block-4 cursor | 183,881 |
| Stop | inline `Load_PhysGeomList` |

The first throughput batch subsequently traverses that geometry list and
publishes asset 35 as XModel identity 64 at inflated offset 397,694. Its one
brush-backed geom contains eight sides, eight planes, 40 adjacent-edge bytes,
and 488 bytes of checked physics payload.

Traversal no longer stops after each boundary. A reusable checked block-4
array-slice resolver now covers every retained XModel skeleton-array class:
script-string indices, parent indices, quaternions, translations, part
classifications, base matrices, and bone-info bytes. This publishes asset 114
and continues across the remaining XModel / technique-set run, including
comma-prefixed engine-owned map-type-zero image placeholders.

The next reusable family traverses `FxEffectDef` headers and their generated
element order: velocity samples, visual-state samples, visual arrays, material
or XModel dependencies, effect-name references, and trails. The first owned FX
asset, `props/watermelon_splat`, publishes at index 381 / identity 1242 with one
mark element and four engine-owned materials. `props/watermelon` publishes at
index 382 / identity 1250 with six elements, three inline materials, prior
aliases, and four nested empty comma-prefixed XModels. Those empty models are
accepted only as engine-owned placeholders; ordinary XModels retain the strict
LOD and bounds checks.

The dispatcher then resumes the established XModel and technique-set loaders.
The canonical RawFile operation follows the native 12-byte header, block-4
name, and `len + 1` buffer ordering and publishes top-level assets 395 and 396
as identities 1290 and 1291. The owned records are
`aitype/ally_blackkit_shtgn_winchester.gsc` (length 1,781) and
`character/character_sp_sas_ct_benjamin.gsc` (length 201). The common
dispatcher then publishes XModel asset 397,
`body_complete_sp_sas_ct_benjamin`, as identity 1311 with 15 surfaces and four
LODs. The M39 checkpoint therefore completes assets 0 through 397 and stops
before inline RawFile asset 398. It contains 270 published XModels, two
published FX effects, and 1,311 registry assets with all 1,311 aliases defined.
The retained inflated-prefix ceiling is 64 MiB, while every logical pointer
target remains constrained by its declared block size and current high-water
mark.

## Milestone 40 RawFile continuation and shared FX materials (complete)

The one-off post-RawFile XModel stop has been removed. A generated
`RawFile -> XModel -> RawFile` fixture proves that each completed body returns
to the common dispatcher, and collection/payload ceilings still fail
atomically. The owned run publishes four additional canonical RawFiles at
assets 398, 400, 402, and 404 (identities 1312, 1314, 1316, and 1318) while
preserving stable owned storage behind each canonical `RawFile` pointer.

Native `Load_FxElemVisuals` reaches `Load_MaterialHandle`, so FX visuals no
longer use a separate name-only material parser. They reuse the checked
material/texture/image/constant/state-bit operation already used by XModels.
Typed aliases and optional shared insertion cells publish only after the full
material dependency succeeds. The FX header now matches native DB semantics:
`elemDefs` is a non-null presence field, and runtime `totalSize` metadata does
not control serialized traversal. Generated coverage includes a nested FX
material with an inline engine-owned `GfxImage`.

The owned result completes top-level assets 0-436. It contains six canonical
RawFiles, 278 published XModels, 11 FX effects, and 1,367 assets with all 1,367
aliases defined. XModel asset 436, `viewmodel_knife`, publishes as identity
1367 at inflated offset 28,658,479; block 0 remains at high-water 352 and block
4 reaches cursor 8,260,512. The next untouched body is inline type-2
`XAnimParts` asset 437. There are 335 serialized top-level assets remaining
before the first `GfxWorld` at asset 772.

## Milestone 41 canonical XAnimParts publication (complete)

The implementation first inventoried the generated native path and uses it as
the behavioral reference. `Load_XAnimPartsPtr` consumes the pointer field in
the caller's stream, pushes block 0, accepts null, inline (`-1`), shared
(`-2` plus a block-4 insertion cell), or a prior typed alias, and publishes only
after `Load_XAnimParts` returns. The body loader consumes the 88-byte canonical
header in block 0, pushes block 4, and traverses this exact order:

1. XString name.
2. `boneCount[9]` script-string names and `notifyCount` notify records.
3. Optional delta part, translation, and quaternion payloads.
4. Byte, short, int, random-short, random-byte, and random-int arrays.
5. Animation indices, using bytes below 256 frames and ushorts otherwise.

All pointer fields in the main packed-array graph are native presence fields:
any non-null value allocates at the current checked block-4 position. They are
not incorrectly restricted to `-1`. Translation and quaternion delta indices
are inline flexible tails after their canonical headers; frame arrays remain
separate aligned allocations. The temporary traversal owner allocates enough
stable storage for those tails and publishes pointers only through the real
Kisak `XAnimParts`, `XAnimDeltaPart`, `XAnimPartTrans`, and
`XAnimDeltaPartQuat` structures. It does not implement playback or DObj work.

Synthetic coverage exercises shared insertion and later alias conversion,
low- and high-frame index widths, bone/notify tables, all packed array classes,
both delta encodings, zero-length presence allocation, copy-stable lifetime,
and atomic limit/script-string/alias failures. The canonical type graph now
lives in renderer-free `src/xanim/xanim_types.h`, shared with native
`xanim.h`. Both the 16-test Win32/MSVC suite and the 16-test Wasm suite pass.

The owned Killhouse run publishes 21 consecutive XAnimParts bodies:

| Field | Value |
| --- | --- |
| Asset range / identities | 437-457 / 1368-1388 |
| First | `viewmodel_winchester_idle`, 0 frames, 79 bones, 1 notify, 1,593 payload bytes |
| First boundary | 28,660,186 inflated bytes |
| Last | `viewmodel_winchester_ads_down`, 12 frames, 1 bone, 1 notify, 105 payload bytes |
| Last boundary | 28,774,997 inflated bytes |
| Completed top-level assets | 458 (indices 0-457) |
| Registry | 1,388 assets / 1,388 defined aliases |
| Next boundary | inline type-23 `WeaponDef` asset 458 |

The native weapon inventory establishes why this is the next genuinely
unsupported family: `Load_WeaponDefPtr` has the familiar block-0 pointer and
publication envelope, but `Load_WeaponDef` traverses a 2,168-byte canonical
record followed by a large ordered block-4 graph. That graph includes 16 gun
models, a hand model, 33 animation-name strings, script-string maps, FX,
numerous sound-alias names, an optional 29-entry bounce-sound array, materials,
16 world models, projectile dependencies, and two accuracy-graph pairs with
inline-or-prior pointer forms. WeaponDef must be extracted and loaded as a
canonical Kisak type; a name-only browser substitute would not preserve the
native contract.

### Native `WeaponDef` loader inventory (complete)

The authoritative path is `Load_XAssetHeader` -> `Load_WeaponDefPtr` ->
`Load_WeaponDef` in `src/database/db_load.cpp`. Type 23 is the canonical
`ASSET_TYPE_WEAPON` value `0x17`. `WeaponDef` is now isolated in the
renderer-free `src/bgame/weapon_types.h` shared header and retains its checked
Win32/Wasm size of `0x878` (2,168) bytes.
`weaponDefFields` in `src/bgame/bg_weapons_load_obj.cpp` independently records
the offsets of the externally named fields and agrees with that layout.

The pointer loader uses the normal asset-publication envelope:

| Root token | Native behavior |
| --- | --- |
| `0` | Null weapon pointer; no body or publication. |
| `0xffffffff` (`-1`) | Align block 0 to four bytes, load one inline body, then publish it with `Load_WeaponDefAsset`. |
| `0xfffffffe` (`-2`) | As above, but reserve a four-byte block-4 insertion cell before loading the body and fill it only after publication. |
| Other nonzero | Convert a previously defined alias cell; no new body is read. |

Asset 458 is an inline top-level body, so its 2,168 header bytes are read from
block 0. `Load_WeaponDef` then pushes block 4 for the complete nested graph.
Returning through `Load_WeaponDefPtr` pops block 0 back to its entry cursor, as
with the other top-level temporary bodies, while retaining its high-water
extent. `DB_AddXAsset(ASSET_TYPE_WEAPON, ...)` is not called until every child
below has completed. If the root used `-2`, its insertion cell is populated
only after that call returns.

The fixed header contains the following dependency-bearing regions. Offsets
are decimal Win32 offsets from the start of the 2,168-byte body.

| Starting offset(s) | Header fields | Count / kind |
| ---: | --- | --- |
| `0`, `4`, `8` | internal, display, and overlay names | 3 `XString` fields |
| `12, 16, ..., 72`; `76` | `gunXModel`, `handXModel` | 16 model handles plus 1 |
| `80, 84, ..., 208`; `212` | `szXAnims`, `szModeName` | 33 animation strings plus 1 |
| `216, 218, ..., 230`; `232, ..., 262`; `264, ..., 294` | hide tags and notetrack key/value maps | 8 + 16 + 16 script-string indices |
| `332`, `336` | view/world muzzle flash | 2 FX handles |
| `340, 344, ..., 516` | pickup-through-putaway sound fields | 45 sound-name references |
| `520` | `bounceSound` | Optional direct/inline array of 29 sound-name references |
| `524, 528, 532, 536` | shell/last-shot eject effects | 4 FX handles |
| `540`, `544` | reticle center/side | 2 material handles |
| `700, 704, ..., 760`; `764, 768, 772, 776` | world models and clip/rocket/knife models | 16 model handles plus 4 |
| `780`, `788` | HUD and ammo-counter icons | 2 material handles |
| `804`, `812`, `832` | ammo, clip, and shared-cap names | 3 `XString` fields |
| `1072`, `1076`, `1304`, `1316` | overlay, kill, and dpad icons | 4 material handles |
| `1340` | alternate-weapon name | 1 `XString` |
| `1412` | projectile model | 1 model handle |
| `1420`, `1428`, `1432`, `1436` | explosion/dud FX and sounds | 2 FX plus 2 sound-name references |
| `1704`, `1732`, `1736` | trail/ignition FX and ignition sound | 2 FX plus 1 sound-name reference |
| `1900, 1904, ..., 1936` | two accuracy-graph names, four knot pointers, and two pairs of counts | 2 strings plus 4 optional `vec2` arrays |
| `2012`, `2016`, `2036`, `2152`, `2156` | use/drop hints, script, and rumble names | 5 `XString` fields |

The exact block-4 traversal order is:

1. Internal, display, and overlay `XString` values.
2. Sixteen gun-model handles, then the hand-model handle.
3. Thirty-three animation `XString` values, then the mode name.
4. Eight hide-tag, 16 notetrack-key, and 16 notetrack-value script strings.
5. View and world flash FX handles.
6. The 45 scalar sound-name references, in header order from `pickupSound`
   through `putawaySoundPlayer`.
7. The optional 29-entry `bounceSound` array.
8. Four eject FX, two reticle materials, 16 world-model handles, then the
   world-clip, rocket, knife, and world-knife models.
9. HUD and ammo-counter materials; ammo, clip, and shared-cap strings;
   overlay/low-resolution-overlay, kill, and dpad materials.
10. Alternate-weapon string and projectile model.
11. Projectile explosion/dud FX, explosion/dud sound names, trail/ignition FX,
    and ignition sound name.
12. Accuracy graph zero name, current knots, and original knots; then graph
    one name, current knots, and original knots.
13. Use-hint, drop-hint, script, fire-rumble, and melee-impact-rumble strings.

Across the fixed and optional graph this is 38 model handles, 10 FX handles,
8 material handles, 48 scalar `XString` fields, 48 scalar sound-name
references, 40 fixed script-string translations, an optional 29-entry sound
array, and four optional knot arrays. These counts describe loader calls, not
necessarily non-null retail dependencies.

The child fields do not all share one pointer-token grammar:

| Field class | Accepted native forms | Serialized consequence |
| --- | --- | --- |
| Model, FX, and material handles | null, `-1`, `-2`, or prior alias | Inline assets are allocated in block 0; `-2` reserves a block-4 alias cell before the child body. |
| Direct `XString` | null, `-1`, or direct zone pointer | `-1` reads a NUL-terminated byte string at the current block-4 cursor; there is no `-2` insertion form. |
| Sound-name reference | null, `-1`, or direct pointer to an `XString` cell | `-1` first allocates and reads a four-byte `XString` cell, then resolves the resulting name through `DB_FindXAssetHeader(ASSET_TYPE_SOUND, name)`. It is not an inline `snd_alias_list_t` body. |
| `bounceSound` | null, `-1`, or direct zone pointer | `-1` aligns block 4 to four bytes, reads 29 pointer cells, then applies the sound-name operation to each cell. |
| Accuracy knots | null, `-1`, or direct zone pointer | `-1` aligns block 4 to four bytes and reads `count * 8` bytes of `vec2` data. There is no `-2` form. |
| Script strings | fixed 16-bit indices already inside the header | No extra bytes are read; each index is translated through the zone's previously loaded script-string list. |

Both current and original knot arrays for graph zero use
`accuracyGraphKnotCount[0]`; both arrays for graph one use
`accuracyGraphKnotCount[1]`. `originalAccuracyGraphKnotCount` is retained in
the canonical record but is not consulted by this generated loader. A checked
port must reject negative counts, multiplication overflow, payload-limit
excesses, invalid script-string indices, unsupported sentinels, undefined
aliases, and out-of-range direct pointers before exposing any parent result.

The implementation handoff is therefore:

- isolate the canonical `WeaponDef` declaration and its enum dependencies in a
  renderer-free shared header rather than introduce `RetailWeaponDef`;
- decode the fixed 32-bit wire record separately from its stable canonical
  in-memory owner;
- reuse the existing typed XModel, Material, and FX operations in the order
  above;
- add the native sound-name lookup contract rather than treating those fields
  as inline sound assets;
- publish the canonical weapon and define any root insertion cell only after
  the entire dependency graph succeeds.

### Canonical `WeaponDef` loader and dependency slice (partial)

The reusable dispatcher now implements the root `-1`, root `-2` insertion-cell,
and prior-alias envelope for type 23. It decodes every non-pointer span from the
2,168-byte wire header into the canonical `WeaponDef`, validates all 40 fixed
script-string indices, traverses the 48 direct `XString` fields in native order,
and owns all four current/original accuracy-knot arrays. Publication and both
root aliases remain undefined until that complete supported graph succeeds.

Synthetic native coverage proves scalar preservation, the interleaving of graph
names and arrays, the native rule that both current and original arrays use
`accuracyGraphKnotCount`, stable ownership across result copies, and atomic
failure for invalid script indices, payload excess, undefined root aliases, and
unpublished child assets.

The next slice exposes renderer-free canonical XModel, Material, draw-surface,
and FX header ABIs from the existing native declarations. Their checked loaders
now own stable canonical top-level objects, and WeaponDef prior aliases resolve
to those exact typed pointers in the generated order. Direct WeaponDef XStrings
also resolve prior retained zone strings rather than assuming every alias came
from an earlier weapon.

Sound fields now implement the native two-level contract: a non-null field is
an XString-pointer cell, not an inline sound body, and its resolved name is
passed to an injected `ASSET_TYPE_SOUND` database lookup. The loader covers
inline and prior name cells, direct string payload aliases, the optional
29-cell bounce array, reused bounce arrays, bounded retained ownership, and
atomic lookup failure. Synthetic coverage proves XModel/Material/FX pointer
identity and sound lookup order without manufacturing placeholder assets.

This is deliberately still partial. Inline `-1`/`-2` XModel, Material, or FX
children inside WeaponDef remain explicit `WeaponDependencyUnsupported`
failures until the existing child state machines gain a WeaponDef return path.
The owned Killhouse traversal resolves its canonical child and prior XString
aliases and now stops at `WeaponSoundLookupFailed` because the standalone web
diagnostic has no native/common-zone sound catalog. Connecting that real
catalog—not replacing names with dummy sound objects—is the next asset-458
boundary.
