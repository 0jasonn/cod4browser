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
     -> sound client registration and SV_InitServerThread
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
production target currently invokes shared `SV_Init`, `CL_InitRef`,
`SV_AddOperatorCommands`, `SV_Map_f`, and the real map-loading slice of
`SV_SpawnServer`. Full `CL_Init` is compiled and differentially tested but is
not entered by the Worker startup adapter yet. No map name is stored in those
owners. A command supplied at
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
| console state, client state, dvar and command registration | canonical portable Kisak | compiled and covered by the native/Wasm contract; awaits full Worker startup |
| `CL_InitInput` | browser platform boundary | canonical call is compiled with a bounded browser-disabled implementation; keyboard, pointer-lock, and gamepad input remain future work |
| `CL_InitRef` configuration | canonical portable Kisak | shared configuration and renderer request construction now execute in production |
| `R_ConfigureRenderer`, `CL_InitRenderer` | renderer frontend boundary | the Worker consumes the `CL_InitRef` configuration at the native asset-load boundary; full registration awaits canonical filesystem/client startup |
| `SCR_Init`, UI command setup | canonical portable plus renderer frontend | lifecycle boundary is linked; fuller frontend behavior remains gated |
| sound/cinematic calls | sound/browser-policy boundary | gate playback while retaining lifecycle state and explicit unlock/resume |
| demo and remote-server commands | offline-unneeded feature | may remain gated during local SP bring-up |

The first large client architecture boundary is now earlier: canonical
`FS_InitFilesystem` must enumerate the Worker mount and open IWDs through the
existing synchronous platform layer. Only then can the runtime enter full
`CL_Init` and its input/renderer boundaries without browser-owned client or
asset state.

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
| `SV_InitGameProgs -> SV_InitGameVM -> G_InitGame` | linked local-server closure | the real server/game units compile in the production Wasm target; the owned browser run remains fail-closed earlier because ClipMap is not yet published, with no manufactured server or game state |

## Current proof boundary

The production runtime now completes `code_post_gfx`, `ui`, and `common`, then
accepts the real map command and follows
`SV_Map_f -> SV_SpawnServer -> SV_LoadLevelAssets -> DB_LoadXAssets ->
DB_LoadXZone`. The normal Worker filesystem opens
`zone/english/killhouse.ff`; no browser DB entry point supplies the map name.

The owned retail observation interns all 892 script strings and traverses the
1,684-entry XAsset list through GfxWorld asset 772. It records 2,371 canonical
publications and atomically publishes `maps/killhouse.d3dbsp` through the real
Kisak DB. Address-independent world counts, inflated offset `86,162,172`, and
the bounded surface selection (`6077`, 2,009 vertices, 128 triangles) match the
frozen Gate 2 observation. The real DB resolves that surface's Material pointer
to `wc/me_ground_mud1`; Gate 2 recorded `wc/decal_porterjustice8`. The oracle is
unchanged and the discrepancy remains explicit.

The existing bounded world adapter consumes the DB-owned `GfxWorld` directly,
submits 2,009 vertices and 384 indices, and records the subsequent WebGL2 draw.
No second world representation is retained. Generated-loader convergence stops
at the next ordered asset, 773, type 13 (`GameWorldSp`), with stream offsets
`[0,509664,0,0,37146694,0,0,21693664,3128676]`.

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
fastfile Hunk through the platform physical-memory boundary. The owned retail run still
stops at `GameWorldSp` asset 773 before canonical ClipMap publication, and the
browser assertion proves that neither collision nor common-world loading is
entered prematurely. Once the remaining map-zone assets publish, the wired
path will cross this boundary without a browser continuation or fabricated
asset state. The save, script-VM, XAnim, and DObj initialization owners are now
compiled and run before the local server path. Attempting the full native
startup exposed the next architecture decision earlier than game startup:
`FS_InitFilesystem` must consume the existing Worker mount for search paths,
directory enumeration, and IWD/minizip access. It must not copy the legal
installation into MEMFS or create a second browser asset registry. Once that
boundary is canonical, full `CL_Init`, post-ClipMap `CM_LoadMap`, local game
startup, and `CL_InitCGame`/`CG_Init` can execute through their linked owners.
