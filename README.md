# KisakCOD

## About the project
An open source fully-buildable reimplementation of Call of Duty 4

Aimed towards mod developers and COD4 enthusiasts.

## Browser port

The `web-port` branch contains a browser target with a legal local-asset
boundary. It compiles existing KisakCOD/ODE physics math plus portable
command/dvar and bounded ZIP32/IWD slices to WebAssembly, then runs them through
a monotonic, non-blocking browser frame pump beside a minimal WebGL2 capability
renderer. Users can select `localization.txt` and `main/iw_00.iwd` from their own
installation, stream them into private browser storage, and restore them on a
later visit.

The runnable browser slice asynchronously mounts the imported IWD behind a narrow
read-only engine filesystem, verifies representative stored and deflated
members, and loads one bounded `images/*.iwi` member through a callback-scoped
4 MiB cache. A portable decoder accepts one strict uncompressed IWI slice,
converts its serialized BGRA mip to RGBA8, and submits it through a renderer
interface that keeps WebGL objects private. The engine side now converts one
bounded surface from D3D-free mirrors of upstream's 44-byte `GfxWorldVertex` and
16-byte `srfTriangles_t` layouts. A freely generated fixture selects four world
vertices and six local indices through non-zero source offsets, decodes native
`0xAARRGGBB` color, and applies an explicit affine world-to-clip projection
before submitting the existing indexed renderer contract. The renderer copies
that converted description and recreates both surface and texture after context
loss; valid compressed or otherwise unsupported IWI formats remain non-fatal
metadata results. It uses the pinned Emscripten zlib 1.3.2 port rather than the
legacy native zlib copy. Automated browser, portable C++, and fuzz-harness
inputs are generated synthetic data only. This proves conversion from an
authentic engine-world representation, not retail map/BSP/fastfile compatibility,
material rendering, a general virtual filesystem, or a playable game build.

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

This remains synthetic-only, not a general decoder or retail-compatibility
claim. Milestone 11 is tightly scoped to a stable multi-asset arena/alias/
registry seam plus resumable source streaming; it does not yet add general
generated-loader traversal. DXT IWI decoding and the broader asset graph remain
later hurdles. A read-only audit of a legally owned retail installation may
confirm framing and format prevalence, but that proves framing only: no
proprietary file is copied or committed, and retail `.ff` input remains
rejected. The browser importer is unchanged and still accepts only
`localization.txt` and `main/iw_00.iwd` from a user-selected installation.

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
