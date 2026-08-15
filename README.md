# KisakCOD

## About the project
An open source fully-buildable reimplementation of Call of Duty 4

Aimed towards mod developers and COD4 enthusiasts.

## Browser port

The `web-port` branch contains a browser target with a legal local-asset
boundary. It compiles existing KisakCOD/ODE physics math plus portable
command/dvar and bounded ZIP32/IWD slices to WebAssembly, then runs them through
a monotonic, non-blocking browser frame pump beside a minimal WebGL2 capability
renderer. Users can select the exact English F.N.G. profile from their own
installation—`localization.txt`, 21 stock IWDs, three startup fastfiles, and
`killhouse.ff`—stream it into private browser storage, and restore it later.
After a validated import, a cooperative qcommon shell initializes bounded
startup memory, an event queue, and the startup command/dvar set, then checks
all 26 files through the asynchronous VFS before stopping at an explicit
pre-database state.

The runnable browser slice asynchronously mounts the imported IWD behind a narrow
read-only engine filesystem, verifies representative stored and deflated
members, and loads one bounded `images/*.iwi` member through a callback-scoped
4 MiB cache. A portable decoder accepts strict uncompressed BGRA and compressed
DXT1/DXT3/DXT5 IWI slices, converts the selected base mip to RGBA8, and submits it through a renderer
interface that keeps WebGL objects private. The engine side now converts one
bounded surface from D3D-free mirrors of upstream's 44-byte `GfxWorldVertex` and
16-byte `srfTriangles_t` layouts. A freely generated fixture selects four world
vertices and six local indices through non-zero source offsets, decodes native
`0xAARRGGBB` color, and applies an explicit affine world-to-clip projection
before submitting the existing indexed renderer contract. The renderer copies
that converted description and recreates both surface and texture after context
loss; valid wavelet/cubemap/volume or otherwise unsupported IWI layouts remain non-fatal
metadata results. It uses the pinned Emscripten zlib 1.3.2 port rather than the
legacy native zlib copy. Automated browser, portable C++, and fuzz-harness
inputs are generated synthetic data only. The same renderer seam now also
receives one bounded surface from the first fully published retail XModel in
`killhouse.ff`. This is a retail model-surface render, not retail map/BSP
compatibility, general material rendering, a general virtual filesystem, or a
playable game build.

Milestone 8 completes the bounded upstream fastfile/zone inventory and
implements its first portable extraction path. The canonical browser fixture
builds and zlib-compresses a complete unauthenticated `IWffu100` version-5
synthetic file, then the checked decoder extracts one base/unlayered `GfxWorld`
surface and minimal named material before the existing converter submits it to
WebGL2. The fixture preserves nonzero source slicing: six shared vertices and
twelve shared indices surround the selected four-vertex/six-index quad at raw
`firstVertex = 1` and `baseIndex = 3`.

The decoder validates the nine logical blocks,
alignment-without-compressed-padding rule, 32-bit pointer and alias tokens,
exact world/vertex/surface layouts, allocation/count limits, material alias,
and malformed input before publishing owned output.

Milestone 9 made that original strict single-asset extraction resumable without
widening its accepted format. A job owns the complete source allocation and
advances through inflation and traversal with hard per-step ceilings of 64 KiB
and 64 records. The browser runs exactly one such step per RAF callback; the
synchronous API drives the same job to completion. A successful result is
available through a one-shot atomic `TakeResult`, while failure or an early
take leaves the caller's destination unchanged. The logical-block model tracks
both the current cursor and its high-water mark so block 0 obeys the upstream
temporary-storage rewind rule.

Milestone 10 widens only that synthetic boundary to one exact two-record
top-level table, read completely before either body: an inline-shared
`Material` followed by an inline `GfxWorld`. The material is copied to stable
job-owned metadata before block 0 rewinds, and block-4 alias `0x40000011`
resolves both the later `MaterialMemory` handle and surface handle to the same
job-local identity after the world reuses the material's temporary bytes. The
fixture has 1246 inflated bytes, a 732-byte block-0 high-water extent, a
persistent 380-byte block 4, and 1112 declared zone bytes.

The cursor logic is now an explicit portable nine-block stream machine with
bounded frame depth, cumulative arena/string/alias/delayed work, block-1 zero
fill, block-0 rewind, persistent immediate blocks, and alignment that emits no
compressed padding. Because the generated loader never selects delayed blocks
2 or 3 in this slice, a separate synthetic microfixture specifies their checked
FIFO replay.

Milestone 11 extracts stable asset registration and source delivery from that
surface-specific state. A reusable registry owns names, sequential job-local
identities, and checked alias cells with deterministic publish, resolve,
unload, and reset behavior. A separate bounded source queue copies one producer
chunk at a time, applies backpressure, and requires an explicit final marker.
The parser can now pause across split prefix and zlib input, then resume without
owning one complete compressed file. The browser proves the path by feeding the
synthetic fixture in 37-byte chunks across RAF callbacks and publishing exact
feed/byte counters and `source-wait` lifecycle events.

Milestone 12 expands only the legal installation and file-access boundary. Its
schema-2 manifest allowlists 26 paths: `localization.txt`, base IWDs `iw_00`
through `iw_13`, English localized IWDs `iw00` through `iw06`, the
`code_post_gfx`/`ui`/`common` startup zones, and the F.N.G. `killhouse` zone.
Selection validates bounded localization, ZIP, and 14-byte IWff/zlib windows;
OPFS persistence and restore verify every exact path and size. The asynchronous
read-only VFS can stat and read any allowlisted file in bounded, cancellable
chunks without placing a complete multi-gigabyte input in Wasm memory.

At the M12 boundary this was not retail fastfile traversal or a playable-game claim. Reading
`killhouse.ff` through the VFS does not change the synthetic world extraction
generation, and no user-owned `.ff` reaches the strict two-record decoder. A
read-only audit of the legally owned Steam installation confirmed the exact M12
profile and version-5/zlib headers; no proprietary file was copied into the
repository or used as an automated fixture.

Milestone 13 completes the portable pre-database qcommon shell. Each browser
frame advances at most one startup action. The job performs memory, event, and
command/dvar initialization followed by a stat and bounded header read for each
of the 26 profile paths: 55 actions and 148 input bytes in the successful path.
It supports cancellation and fresh generations, publishes typed path-specific
failures, and explicitly reports that Asyncify, pthreads, and retail zone
traversal are disabled. At M13, DXT IWI decoding, the broader asset graph, and
a real map remained later hurdles.

Milestone 14 replaced the hand-written per-frame job chain with one fixed-capacity
cooperative scheduler. Filesystem completions, qcommon, archive work, engine
assets, commands, synthetic world extraction, and renderer submission run in a
stable order; M15 adds the retail census as the eighth task and raises the
current envelope as described below. A 12-ms inter-task wall-time check and
generational handles prevent stale cancellation, repeated budget denial emits
starvation diagnostics, and reservation violations quarantine the offending
task. Sampled browser traces expose the schedule without changing the existing
world-surface or renderer output.

Milestone 15 added the first deliberately bounded retail fastfile traversal.
After qcommon startup, the browser reads `code_post_gfx.ff` through the local
VFS, validates its unsigned version-5/zlib/XFile envelope, traverses its script
strings and complete asset-header table, publishes a type census, and stops
before asset body zero. The current eight-task schedule reserves 266,254 bytes
and 267 records inside 320-KiB/320-record/eight-call frame ceilings. A read-only
check of the user's Steam file found 1,639 headers ending at inflated offset
14,942; asset zero is an inline technique set.

Milestone 16 follows that first body through its technique, pass, vertex
declaration, vertex-shader metadata, and bounded D3D9 bytecode, then stops before
the native `Load_CreateMaterialVertexShader` side effect. The real read-only
boundary is `sm2/2d` to slot 4 to `vertcol_simple.hlsl`, a 103-DWORD program
ending at inflated offset 15,673. No incomplete asset is published, no retail
shader is executed or rendered, and no proprietary byte is copied into tests or
committed.

Milestone 17 replaces both native D3D9 shader-creation calls for that first pass
with one strict portable compatibility record. A bounded decoder inventories
VS 1.1/PS 2.0 instructions and CTAB register bindings, then selects the explicit
GLSL ES 3.00 pair `webgl2.vertcol_simple2d.v1` only when the paired contracts and
vertex routing all match. It consumes the three material arguments and
`vertcol_simple2d` technique name before publishing asset zero atomically. The
owned Steam file reaches inflated offset 15,950 with one completed asset; actual
WebGL2 program creation and retail material/image binding remain later work.

Milestone 18 makes that selected record renderer-owned. The browser compiles and
links only the port's compiled-in GLSL, validates all three attributes and three
uniforms, binds identity matrices and texture unit zero, and completes an
indexed draw through `webgl2.vertcol_simple2d.v1`. Publication is atomic,
binding failure keeps the bootstrap renderer active, cancellation retires stale
state, and context restoration recompiles the program from bounded CPU-owned
descriptions. The shader contract comes from the owned COD4 fastfile; the drawn
surface and fallback/test texture are still synthetic.

Milestone 19 adds bounded DXT1/DXT3/DXT5 IWI decoding, including exact COD4 mip
ordering, edge clipping, alpha modes, malformed-layout rejection, and atomic
RGBA8 publication under the existing 4 MiB recovery ceiling. The asynchronous
IWD path can now decode and bind the owned archive's deterministic
`images/$black.iwi` entry to the M18 sampler, and the launcher reports the joint
shader/image state. Synthetic tests contain no game data. Geometry and the
surface material identity were still synthetic at that boundary.

Milestone 20 completes that dependency boundary. The generated-loader reader
now accepts exactly the owned `code_post_gfx.ff` prefix of two inline technique
sets followed by material `ui_cursor`. It resolves the material's normal
technique-set alias, validates one non-water texture record and inline 2D
`GfxImage` named `3_cursor3`, and publishes four stable registry identities only
after the complete supported prefix succeeds. The archive job then requests
`images/3_cursor3.iwi` exactly—never the alphabetically first image—and sends it
through the existing bounded DXT/RGBA8 renderer path. A freely generated browser
fixture proves the same selection while retaining a lower-sorting decoy image.
The current indexed surface remains synthetic; no retail map geometry,
lightmaps, visibility, general material traversal, or gameplay is claimed.

Milestone 21 adds a second table-only reader mode for the owned F.N.G.
`killhouse.ff`. The browser streams only the compressed prefix needed to
validate its XFile metadata, script strings, and complete asset table, records
the stable table-order hash and the first `GfxWorld` entry, then stops before
asset body zero. The owned inventory contains 1,684 assets; the world is inline
at index 772 and all 772 preceding entries are also inline. This proves that a
correct generated-loader path cannot jump directly to world bytes. The first
body is a technique set, making general retail technique-set traversal the next
bounded dependency milestone. The synthetic browser fixture contains no game
data and verifies both successful inventory and fail-closed missing-world input.

Milestone 22 enters `killhouse.ff` asset body zero without widening the map
claim. The bounded 148-byte `MaterialTechniqueSet` is
`,sm2/mc_l_sm_r0c0s0`; it has world-vertex format zero, no remap, and 34 null
technique pointers. Its name and dependency array complete at inflated offset
30,915, after which the zone registry assigns identity 1 and publishes the
asset-zero table alias atomically. The parser then stops before inline asset
one, which is another technique set. Dependency-bearing synthetic sets remain
unpublished at the first technique pointer, and malformed headers fail closed.
No retail shader or map geometry is executed.

Milestone 23 generalizes that checked loader across consecutive inline map
technique sets. The owned F.N.G. prefix contains 12 zero-dependency sets at
asset indices 0–11; all 12 receive stable identities and defined table aliases
before traversal stops at inline `XModel` asset 12. Synthetic coverage proves
later dependency stops, different-type stops, and fail-closed malformed later
headers. No model body or retail map geometry is parsed or rendered.

Milestone 24 enters that first retail `XModel`,
`ch_street_wall_light_01_off`, with a checked 220-byte header and bounded
skeleton-prefix loader. It retains one resolved bone, six declared surfaces,
three LOD ranges, bounds, collision counts, and memory metadata, then stops
before `Load_XSurfaceArray`. The model alias remains unpublished until its
nested surface and later dependencies are implemented; no retail model geometry
is parsed or rendered.

Milestone 25 follows all six of that model's `XSurface` dependencies through
their packed vertices, rigid lists, collision trees, and triangle indices. The
owned prefix accounts for 754 vertices, 524 triangles, six rigid lists, 44
collision nodes, and 284 leaves, then retains six ordered material handles and
stops before the first inline `Material`. Large retail geometry payloads are
hashed rather than retained, the XModel alias remains unpublished, and no
retail model is rendered yet.

Milestone 26 completes that first model's generated-loader dependency chain.
It loads two inline materials and their bounded image/constant/state tables,
resolves four material aliases and one image alias through the typed zone
registry, validates 96 collision triangles and one bone-info record, and proves
that both physics references are null. Only then is XModel identity 19
published at inflated offset 67,723 with all 19 reserved aliases defined.
Malformed aliases, collision bounds, and bone info fail closed.

Milestone 27 retains only surface zero from that published model when it fits
the existing 4,096-vertex/12,288-index renderer ceiling. A D3D-free converter
decodes the 32-byte packed vertex records, including half-float UVs and native
vertex color, validates every local index, and fits the two largest spatial
axes into a deterministic orthographic clip-space view. The selected material
handle must resolve to a published identity before the existing renderer
atomically replaces its synthetic surface. The owned surface is 368 vertices
and 252 triangles and selects material identity 16. Conversion, shader-binding,
or backend failure preserves the bootstrap surface. This makes a real owned
model silhouette visible, although it still uses the current startup sampler;
it is not a GfxWorld/map render or playable scene.

Milestones 28 and 29 resolve each retained first-LOD surface through its typed
material and semantic-2 color-map identity, discover the exact IWI members in
the user's base archives, and submit a bounded multi-draw preview. The owned
first LOD contains two draws, 385 combined vertices, 828 indices, and two DXT1
textures. Renderer recovery retains the shared geometry and every texture slot;
this remains an isolated orthographic XModel preview rather than a map renderer.

Milestone 30 resumes the generated top-level loader after the completed XModel
and publishes exactly one additional typed boundary. Owned asset 13 is inline
technique set `,sm2/mc_l_sm_r0c0n0s0`; all 34 technique pointers are null, so
identity 20 and its table alias publish at inflated offset 67,893. The parser
stops before another inline technique set at asset 14. Malformed input exposes
no partial result, and a dependency-bearing fixture stops before the nested
`MaterialTechnique` with the new alias undefined.

Milestone 31 generalizes that boundary across the consecutive post-XModel
technique-set run. The owned profile publishes eight zero-dependency sets at
assets 13–20 with identities 20–27, ending at inflated offset 69,063 with all
27 registry aliases defined. The next untouched body is inline `XModel` asset
21. Synthetic coverage proves a two-set run, a later dependency that preserves
only the already published prefix, and a malformed later header that exposes
no partial result. No second model body or new renderer payload is entered.

Milestone 32 enters asset 21 without replacing the retained/rendered first
model. The owned second XModel is `com_steel_ladder`: one root bone, three
surfaces, three LODs, one collision surface, and a checked skeleton prefix
ending at inflated offset 69,335. Its table alias is reserved but remains
undefined while traversal stops before `Load_XSurfaceArray`. Malformed second
headers expose no result, and an unsupported skeleton dependency preserves the
published first model. No ladder geometry is retained or rendered yet.

Milestone 33 reuses the checked XSurface loader for asset 21. All three
`com_steel_ladder` surfaces now traverse in generated-loader order: 750
vertices, 488 triangles, three rigid lists, and 28,236 bounded payload bytes.
The three ordered material handles are retained and traversal stops before the
first inline `Material` at inflated offset 97,571. The first model remains the
only renderer publication; second-model packed geometry is hashed, not retained
or submitted. This is a step toward a reusable full XModel loader, not a model
viewer.

Milestone 34 completes asset 21 through the shared material/image,
collision-surface, bone-info, and null-physics stages. The owned
`com_steel_ladder` publishes as identity 32 at inflated offset 112,348; its
single material `mc/mtl_steel_ladder` is identity 31 and resolves three images
with identities 28–30. Its collision surface contributes 296 checked triangles
and 14,252 bounded bytes. The first model remains the only renderer consumer.
The next serialized body is inline XModel asset 22, making a repeatable XModel
collection/loop the next boundary.

See [docs/web-port.md](docs/web-port.md) for the pinned toolchain, build steps,
current boundary, validation limits, and next milestone, and see
[docs/fastfile-zone-inventory.md](docs/fastfile-zone-inventory.md) for the
serialized evidence and strict subset. Original Call of Duty 4 data and native
Bink, Miles, and Steam binaries are not part of the browser build or automated
fixtures, and selected data is never uploaded.

To run an existing browser build, keep this command open and visit
`http://127.0.0.1:8000` in Chrome:

```powershell
python .\tools\serve_web.py --directory .\build\web\site
```

![licimg](./GPLv3_Logo.png)

### Development Blog
Learn about the Development of KisakCOD here: [https://lwss.github.io/Duty-Of-Kisak/](https://lwss.github.io/Duty-Of-Kisak/)

## Current Requirements
- Windows OS
- Visual Studio 2022
- CMake >= 3.16
- [DirectX SDK 2010](https://www.microsoft.com/en-us/download/details.aspx?id=6812)
- Steam with a copy of [Call of Duty 4](https://store.steampowered.com/app/7940/Call_of_Duty_4_Modern_Warfare_2007/)


## How to build
1) Install the above requirements and Clone repo
2) Open a terminal and run `generate-project.bat`
3) Open .sln projects that are generated in `build-sp`, `build-mp`, and `build-dedi` respectively. 
4) Copy COD4 Game files to `bin/(BUILD_TYPE)/*` (Don't try to cherry-pick them, small files like localization.txt are needed)
5) Copy `deps/binklib/binkw32.dll` as well ^^
6) Copy all files in `deps/msslib/dlls/*` ^^ 
7) Copy `deps/steamsdk/steam_api.dll`  ^^
8) Run the game via Visual Studio play button or just the .exe


```
Keep in Mind: This is a ~20 year old game with some known exploits. We will try to fix these as we become aware of them.
However, there is a non-zero chance of some type of binary exploitation when playing online. Use a sandbox (Sandboxie?) for peace of mind. 
```

## Known Issues
(Use the **[issues](https://github.com/SwagSoftware/KisakCOD/issues)** section)

## Troubleshooting
- ***Can't Connect to Dedicated Server*** :
  -  Check `net_ip` and `net_port`, the server will increment the port if the preferred one isn't available but the client won't sweep upwards.
 - ***DLL Error upon launch*** :
   - You didn't copy over the necessary runtime DLL's

## FAQ
- Can we use AI in this project?
  - Yes you can, but you're still responsible for whatever you commit. In general, you should have the AI be assisting you, and not carrying you. We have started using AI to help de-bug, and it's been extremely helpful.

## Credits and Special Thanks
- ***All Original COD4 Developers (for creating one of the best games of all time)***
- https://github.com/PJayB/jk3src (Jedi Academy fork with .sln)
- https://github.com/voron00/CoD2rev_Server - Useful yacc code for the gsc scripting here
- https://github.com/shiversoftdev/BO3Enhanced - Viewed as reference code for some of the Steam API Auth
- [RAD Game Tools](https://www.radgametools.com/) for their Bink and Miles Sound System libraries.
- [ODE Physics](https://www.ode.org/) COD4 uses a modified version of this physics engine.


## Discord
[Join the KisakCOD Discord](https://discord.gg/9uqntRWMA3)
