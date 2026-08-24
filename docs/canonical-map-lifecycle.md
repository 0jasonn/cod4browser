# Canonical single-player map lifecycle

This inventory follows the current Kisak SP sources after the renderer startup
zone request. It is the authority for the browser continuation; the old web
bootstrap does not select the next subsystem or a map zone.

## Native call sequence

```text
Com_Init
  -> Com_Init_Try_Block_Function
     -> Com_InitXAssets -> DB_InitThread
     -> filesystem/config/hunk initialization
     -> Scr_InitVariables -> Scr_Init -> XAnimInit -> DObjInit
     -> SV_Init -> SV_AddOperatorCommands
     -> CL_InitOnceForAllClients
     -> CL_Init(0)
        -> Con_Init
        -> CL_InitInput
        -> client dvars and commands
        -> CL_InitRef
           -> CL_SetFastFileNames
           -> R_ConfigureRenderer
     -> SND_InitDriver -> R_InitThreads
     -> CL_InitRenderer
        -> R_BeginRegistration -> R_Init -> R_InitHardware
           -> R_LoadGraphicsAssets
              -> DB_LoadXAssets(code_post_gfx, ui, common)
     -> sound client registration -> SND_Init
        -> SND_InitEntChannels
           -> DB-owned soundaliases/channels.def from code_post_gfx
     -> SV_InitServerThread
     -> PMem_EndAlloc("$init", high)
     -> DB_SetInitializing(false)
     -> Com_Init returns

normal user/test command: "map <name>"
  -> Cbuf_ExecuteBuffer / Cmd_ExecuteSingleCommand
  -> server-command dispatch
  -> SV_Map_f
     -> SV_Cmd_ArgvBuffer
     -> lowercase and FS_ConvertPath normalization
     -> SV_SpawnServer(normalizedName, savegame)
        -> DB_ResetZoneSize
        -> CL_InitLoad
        -> CL_MapLoading
        -> canonical client/server shutdown and restart
        -> CL_StartLoading
        -> SV_LoadLevelAssets(normalizedName)
           -> DB_LoadXAssets({name, alloc=8, free=8}, 1, sync=0)
              -> DB_LoadXZone
              -> Worker filesystem open of zone/<language>/<name>.ff
              -> ordered generated loaders and DB publication
        -> Com_GetBspFilename("maps/<name>.d3dbsp")
        -> CM_LoadMap
        -> Com_LoadWorld
        -> save-system and SV_InitGameProgs closure
```

`CL_Init`, `SV_LoadLevelAssets`, and `SV_Map_f` have shared source owners. The
production target invokes full `CL_Init`, shared `SV_Init`, `CL_InitRef`,
`SV_AddOperatorCommands`, `SV_Map_f`, and the real map-loading slice of
`SV_SpawnServer`. The post-filesystem continuation also establishes the native
player-profile owner before any save path is constructed. No map name is stored
in those owners. A command supplied at
runtime as `map KiLlHoUsE` passes through the real command tables, normalizes to
`killhouse`, and reaches the database with allocation flags `8/8` and
`sync=0`. The matching x86/Wasm differential exercises the same command and
request construction.

The Worker is single threaded, so the browser platform adapter preserves the
native database-thread wait semantics by processing type-0 startup zones while
`$init` remains open, deferring the type-1 `common` request, and resuming it
after `DB_SetInitializing(false)`. Engine calls remain synchronous-looking;
this adaptation uses neither Asyncify nor Promise propagation into engine
code.

## Client initialization dependency classification

| Native `CL_Init` segment | Classification | Browser convergence |
| --- | --- | --- |
| console state, client state, dvar and command registration | canonical portable Kisak | compiled, covered by the native/Wasm contract, and entered by Worker startup |
| `CL_InitInput` | browser platform boundary | DOM keyboard, pointer-lock deltas, and visible-cursor canvas coordinates cross a bounded Worker queue into canonical `CL_KeyEvent` / `CL_MouseEvent`; cgame `CL_Input`, usercmd creation, server think, and `Pmove` remain native owners. Gamepad input remains future work. |
| `CL_InitRef` configuration | canonical portable Kisak | shared configuration and renderer request construction now execute in production |
| `R_ConfigureRenderer`, `CL_InitRenderer` | renderer frontend boundary | the Worker consumes `CL_InitRef` configuration at the native asset-load boundary, publishes renderer prerequisites, completes frontend registration, and only then enters `SND_Init` |
| `SCR_Init`, UI command setup | canonical portable plus renderer frontend | lifecycle boundary is linked; fuller frontend behavior remains gated |
| sound/cinematic calls | sound/browser-policy boundary | gate playback while retaining lifecycle state and explicit unlock/resume |
| demo and remote-server commands | offline-unneeded feature | may remain gated during local SP bring-up |

Canonical `FS_InitFilesystem` now enumerates the Worker mount and opens IWDs
through the synchronous primitive layer while retaining C++ search-path
ownership. Full `CL_Init` is reached, including renderer startup and the
keyboard/pointer-lock/absolute-cursor input boundary; gamepad and fuller UI
policy remain.

## Local server dependency classification

| Native segment | Classification | Browser convergence |
| --- | --- | --- |
| `SV_Init`, map command registration and dvars | canonical portable Kisak | required before accepting `map` |
| command tokenization/forwarding | canonical qcommon | server-command portion now compiles in the differential slice |
| `SV_Map_f` normalization/savegame selection | canonical portable Kisak | lightweight shared owner; savegame branch retained |
| `CL_InitLoad`, `CL_MapLoading`, `CL_StartLoading` | canonical client lifecycle | required; cinematics/audio may be platform-gated |
| server clear/restart/startup | canonical local SP server | required; no fake server state |
| internet/network transport | not required for local SP | do not compile merely to request a local map |
| `SV_LoadLevelAssets` | canonical DB boundary | lightweight shared owner; map name remains runtime input |
| `CM_LoadMap` | canonical portable Kisak | the actual `cm_load.cpp` owner is in the production target and requires the canonical `clipMap_t` singleton published by the completed map-zone load |
| `Com_LoadWorld` | canonical portable Kisak | the fastfile owner and global `comWorld` singleton are linked immediately after collision load |
| save system | canonical portable Kisak | actual `savememory.cpp`, `savememory_init.cpp`, and `memfile.cpp` owners are compiled; both 1,572,864-byte global buffers and demo-save clearing have exact x86/Wasm evidence |
| `Scr_InitVariables`, `Scr_Init`, `XAnimInit`, `DObjInit` | canonical portable Kisak | actual free lists, VM stack/temp value, 4,096-entry XAnim ring, notetrack string, and DObj duplicate-parts string initialization run in native order before `SV_Init` |
| `SV_InitGameProgs -> SV_InitGameVM -> G_InitGame` | reached local-server closure | the real server/game units execute in the production Wasm target through script compilation, entity/level initialization, `G_LoadLevel`, and the first server frame, with no manufactured server or game state |

## Current proof boundary

The production runtime now completes `code_post_gfx`, `ui`, and `common`, then
accepts the real map command and follows
`SV_Map_f -> SV_SpawnServer -> SV_LoadLevelAssets -> DB_LoadXAssets ->
DB_LoadXZone`. The normal Worker filesystem opens
`zone/english/killhouse.ff`; no browser DB entry point supplies the map name.

The owned retail observation interns all 892 script strings and traverses the
entire 1,684-entry XAsset list through GfxWorld asset 772, GameWorldSp 773,
MapEnts, ClipMap, and final RawFile asset 1,683 `killhouse`. It atomically
publishes `maps/killhouse.d3dbsp` through the real Kisak DB. Address-independent
world counts, inflated offset `86,162,172`, and
the bounded surface selection (`6077`, 2,009 vertices, 128 triangles) match the
frozen Gate 2 observation. The real DB resolves that surface's Material pointer
to `wc/me_ground_mud1`; Gate 2 recorded `wc/decal_porterjustice8`. The oracle is
unchanged and the discrepancy remains explicit.

The existing bounded world adapter separately consumes the DB-owned `GfxWorld`,
submits 2,009 vertices and 384 indices, and records its WebGL2 oracle draw. It
is not called by the cgame frame path and cannot satisfy the game-driven-frame
milestone. No second world representation is retained. Generated-loader convergence now
ends naturally at asset 1,683 with stream offsets
`[0,522928,0,0,47286243,0,0,26535904,3840644]` and no generated failure.

The production target now compiles the actual `cm_load.cpp` owner and continues
the real `SV_SpawnServer` body through `CM_LoadMap` and `Com_LoadWorld` only
after `DB_LoadXAssets` reports a completely successful map-zone load. The DB
singleton pools use the subsystem-owned addresses `&cm`, `&comWorld`, and
`&s_world`; placeholder storage no longer stands in for those three runtime
objects. `CM_LoadMap` requires
`DB_FindXAssetHeader(ASSET_TYPE_CLIPMAP, name).clipMap == &cm`, initializes the
main, backend, two worker, and server collision thread-data records, marks the
singleton in use, and publishes its checksum. `Com_LoadWorld` then requires
the lookup result to be `&comWorld`.

An exact MSVC x86/Wasm differential invokes those real owners and records
`Scr_InitVariables > Scr_Init > XAnimInit > DObjInit > CM_LoadMap >
Com_LoadWorld > SaveMemory initialization`, including identical VM free-list,
XAnim ring, DObj string, checksum, save-buffer, collision-thread, and Hunk
allocation semantics. Production initialization now owns the native 10 MiB
fastfile Hunk through the platform physical-memory boundary. The owned retail
run publishes ClipMap into native `&cm` and executes `CM_LoadMap`,
`Com_LoadWorld`, `SV_InitGameProgs`, `SV_InitGameVM`, `G_InitGame`,
`G_LoadLevel`, `CL_InitCGame`, and `CG_Init` through their canonical owners.
The save, script-VM, XAnim, and DObj initialization owners run before the local
server path. Canonical `FS_InitFilesystem`
now consumes the existing Worker mount for search paths, directory
enumeration, and IWD/minizip access without copying the installation into
MEMFS or creating a second browser asset registry. Full `CL_Init` is also
reached. The rejected post-`CG_Init` Worker call was traced to a missing native
player-profile continuation and is resolved without bypassing save-path
construction. Renderer-owned startup zones now publish before canonical sound
initialization, preventing the prior missing-`channels.def` error state. The
browser pump is wired through native SP ordering: `SV_Frame ->
CL_RunOncePerClientFrame -> CL_Frame -> SCR_UpdateScreen -> cgame view
construction -> R_RenderScene`. This matters for Killhouse because its first
cgame view is linked to a canonical two-second descending mover and initially
sees only the sky pass; treating an empty opaque visibility set as a valid
frontend submission lets authoritative server/game time advance normally.
Chrome records the accepted canonical map command, `CG_Init complete`,
`CL_InitCGame complete`, and a `game-driven frame` whose view names
`maps/killhouse.d3dbsp`. Once the mover enters the world view, the renderer
frontend batches the base world's 8,064 opaque surfaces into 431,747 retained
vertices and 793,188 32-bit indices across 514 canonical material/lightmap
batches. Surface indices are canonical local indices rebased by each surface's
`firstVertex`; canonical base and lightmap UVs remain attached to each vertex.
The backend consumes the DB-published `Material` and `GfxImage` identities,
reads external base IWI members through canonical `FS`/IWD ownership, and
retains the transient DB lightmap load definitions at the native `Load_Texture`
platform boundary. Chrome renders 429 batches with the three canonical L8
lightmap atlases, ten base-texture-only batches, and retains backend fallback
for unsupported identities. Basic material culling, depth, alpha, blending,
and sampler state is applied per batch. The path does not invoke the bounded
Gate 2 adapter or a browser-owned world representation.

The pump refreshes native `com_frameTime` from the browser monotonic clock and
accumulates real elapsed milliseconds before advancing the game at no more than
125 Hz. OffscreenCanvas Worker callbacks may run more often than display
refresh; callbacks within the same integer millisecond render without inventing
a server step. The pump does not duplicate cgame's `CL_Input` or the server's
`CL_WritePacket`. Browser WASD events therefore become normal bound commands
and player `Pmove`, while pointer-lock deltas flow through the existing
sensitivity, usercmd, prediction, and cgame-camera path. Cursor visibility
remains a main-thread DOM operation requested by the Worker rather than touching
`OffscreenCanvas.style`; showing it releases pointer lock and sends scaled
canvas coordinates back through canonical `CL_MouseEvent` for menus. The web
platform publishes absolute/relative mode from the canonical key catchers rather
than inferring it from system-cursor visibility, and a browser-consumed Escape
lock exit is forwarded once to `CL_KeyEvent`.

Single-player `PM_GroundTrace` retains the canonical quarter-unit probe and
uses a one-unit walkable-support fallback only for an ordinary, non-separating
player when that primary probe misses. This covers the sub-unit standing-hull
settle gap exposed by Killhouse collision and integer velocity snapping without
affecting linked startup movement or genuine ledge departures. The shared
ground-detachment state reset is no longer accidentally hidden behind the
multiplayer-only jump-animation branch. In Chrome, the initial concrete spawn
and nearby incline remain attached to `ENTITYNUM_WORLD`, and releasing movement
settles velocity to zero with no subsequent position drift.
