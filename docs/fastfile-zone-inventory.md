# Fastfile and `GfxWorld` zone inventory

## Status and scope

This document records the Milestone 8 inventory of the upstream
fastfile/database path needed to recover one bounded `GfxWorld` surface, the
strict portable decoder implemented from that inventory, the Milestone 9
resumable execution boundary, and Milestone 10's exact supported-prefix
multi-asset contract. The decoder compiles into the browser target and consumes
a freely generated two-asset synthetic fastfile: one top-level `Material`
followed by one `GfxWorld`. It is not a general fastfile loader and does not
establish retail-map compatibility.

`src/web/web_fastfile_world_surface.h` and
`src/web/web_fastfile_world_surface.cpp` implement the subset defined below.
They reject every unsupported branch deterministically and expose owned output
through a one-shot atomic result handoff. `src/web/web_engine_surface.cpp`
generates the canonical input, advances its extraction once per browser frame,
passes the selected surface to the existing world-surface converter, and
submits the converted quad to the renderer. No user-owned fastfile reaches this
path. The existing legal importer remains limited to
`localization.txt` and `main/iw_00.iwd`; it does not select, copy, mount, or
parse `.ff` files.

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

The decoder still receives one complete in-memory file. The resumable job
advances it once per RAF callback with bounded per-frame CPU work; this is not
yet a resumable browser-file source or permission to accept user-owned data.
Successful extraction returns only selected owned vertex/index slices, source
metadata, block sizes, a bounded material name, and a stable job-local material
identity. The runtime then calls `WebEngine_ConvertWorldSurface` and
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

## Next design boundary: Milestone 11

Milestone 11 should separate stable multi-asset arena, alias, and registration
lifetime from the surface-specific extractor, including deterministic reset and
unload behavior. It should also add a resumable source boundary so inflation can
pause for bounded source chunks instead of requiring `Begin` to own one complete
compressed allocation. Existing per-step/cumulative limits and atomic result
handoff remain part of the contract.

Keep that milestone synthetic-only and supported-prefix driven. It must not add
a generic skip mechanism, accept another type implicitly, or route user-owned
fastfiles into the decoder. General generated-loader traversal and bounded
DXT1/DXT3/DXT5 IWI decode are the next real format hurdles, but they remain
separate from M11's arena/identity/source-streaming seam.
