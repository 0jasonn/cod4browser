# Web-port convergence report

## Purpose

This report tracks whether the browser target is converging on KisakCOD engine
code or accumulating browser-only engine replacements. It is an architectural
inventory, not a claim that listed code already boots the game.

The governing rule is:

> Browser-specific intermediate representations exist only at genuine platform
> boundaries. Prefer compiling and executing KisakCOD engine behavior wherever
> the browser platform allows it.

The desired path is:

```text
COD4 fastfile
     -> Kisak database/asset loader
     -> Kisak XAsset / XModel / Material / GfxWorld
     -> Kisak client and game systems
     -> Kisak renderer frontend
     -> portable draw commands
     -> WebGL2 backend
```

## Classification

| Status | Meaning |
| --- | --- |
| `SHARED KISAK` | The web target compiles the same portable Kisak implementation used by native targets. |
| `MODIFIED KISAK` | Kisak code or behavior is shared but has a narrow, intentional portability adaptation. |
| `WEB PLATFORM IMPLEMENTATION` | Browser-owned code at a genuine OS, API, or graphics-backend boundary. This category is permanent. |
| `TEMPORARY WEB SUBSTITUTE` | Bootstrap or validation code standing in for an engine system. It needs a convergence or retirement path. |
| `NATIVE ONLY` | A native implementation exists but cannot be used as-is because it owns a platform-specific boundary. |
| `NOT COMPILED` | Kisak source exists in the repository but is not part of `KisakCOD-web`. |

A component may have a canonical native implementation and a temporary web
substitute at the same time. The table calls this out rather than treating the
substitute as convergence.

## Snapshot

Snapshot baseline: branch `web-port`, through the canonical Worker/database,
server/game, and local-client/cgame lifecycle and generated RawFile, PhysPreset,
XModel, XAnimParts, WeaponDef,
StringTable, MaterialTechniqueSet, Material, GfxImage, water, LocalizeEntry,
SoundCurve, sound aliases, LoadedSound, Font, FxEffectDef, FxImpactTable,
GfxLightDef, MenuList, Menu, ComWorld, and GfxWorld publication. The normal
Kisak DB now traverses all 1,684 Killhouse assets, publishes the native
`clipMap_t` owner, and continues through `CM_LoadMap`, `Com_LoadWorld`,
`G_InitGame`, `G_LoadLevel`, `SV_InitGameVM`, and `CG_Init`. Direct Win32 x86
and Wasm GameWorldSp traces are byte-identical again. The split Worker startup
now initializes the canonical player profile before save-path consumers and
preserves native renderer-before-sound ordering, so `channels.def` is resolved
from renderer-owned `code_post_gfx` instead of leaving `com_errorEntered` set.
The browser pump now preserves real SP frame ordering through `SV_Frame ->
CL_RunOncePerClientFrame -> CL_Frame -> SCR_UpdateScreen`. `R_RenderScene`
constructs the canonical view/projection matrix, traverses renderer-owned
`GfxWorld` base-world range, skips the separate sky pass, and emits portable
material-aware indexed batches. Chrome records the canonical `refdef_s` for
`maps/killhouse.d3dbsp`, followed by a successful WebGL2 draw of 8,064 opaque
surfaces, 431,747 retained vertices, and 793,188 32-bit indices in 514 batches
after the scripted start mover descends into the world view. Of those batches,
429 consume canonical base textures plus the three DB-owned L8 lightmap atlases
and ten use base textures only; four of 147 referenced images retain canonical
identity behind the explicit backend fallback. Gate 2 remains a separate frozen
oracle and is not invoked by this path.

The same cgame frame now submits the world-owned static-model population and
the first-person DObj without introducing a preview object model. The WebGL2
boundary retains 238 canonical `XModel` identities as 359 shared XSurface
batches (68,684 vertices, 135,492 indices, and 12,188 placements). The
first-person path calls canonical DObj pose evaluation and skinning each frame;
Chrome records one weapon/viewhands DObj containing two models, five surfaces,
4,845 posed vertices, and 18,300 indices. Canonical 2D callbacks also retain HUD
quads and font glyphs, including the `gamefonts_pc` and `devfonts` `TS_2D`
atlases. A clean Chrome run shows the weapon, hands, center reticle, readable
HUD/ammo, compass, and mission text together, then records `mouse1 -> +attack`
with no UI catcher and visibly advances the canonical weapon/camera recoil.

The production web target now links the canonical client/server map-lifecycle
slice in addition to its existing runtime and platform units. The runtime
prefix includes real `common.cpp`, canonical
dvar and command implementations, real physical memory, database-initializing
state, string-list/memory support, constant config strings, and shared qcommon
utilities. This is only a source-inventory
baseline; it is not
a quality metric by itself because filesystem, lifecycle, and WebGL code should
remain platform-owned.

The last successful prerequisite traversal completes all 6,502 ordered assets
in `common.ff`. Its type-7 run begins at asset 4,778 and retains 1,723 sound
table rows backed by 1,716 unique canonical `snd_alias_list_t` objects. The
cross-zone index then lets `killhouse.ff` publish its canonical type-23
`WeaponDef` run beginning at asset 458 (`winchester1200`). Ordered traversal no
longer stops at that validation boundary: it completes assets 0-771 and
continues from that exact retained state through the complete generated-loader
order for `GfxWorld` 772. The published result contains 218 technique sets, 325
XModels (including five inline static-model dependencies), 60 FX effects, 146
canonical `XAnimParts`, 10 WeaponDefs, 21 canonical RawFiles, and the canonical
type-12 `ComWorld` for `maps/killhouse.d3dbsp` with 24 primary lights. Asset 705
is the canonical `GfxLightDef` `light_point_linear`; its attenuation pointer
resolves to canonical `GfxImage` identity 1,773. Assets 706-771 are already
supported technique sets. The world contains 448,962 vertices, 829,539 indices,
8,694 surfaces, three cells, three lightmaps, and 12,255 static models. Its
final inflated cursor is 86,162,172. The normal DB records stream offsets
`[0,509664,0,0,37146694,0,0,21693664,3128676]` at the next ordered asset;
Gate 2 independently records block-0 high-water 8,389,392, block-1 high-water
509,456, block-4 cursor 37,147,366, and 2,371 assets with 2,479 defined aliases.
The world counts and selected geometry agree. Its material label does not:
Gate 2 records `wc/decal_porterjustice8`, while the real DB pointer names
`wc/me_ground_mud1`. The oracle is frozen. No seek, rewind-to-world, or
reconstructed registry is used.
Ordered generated traversal now continues through GameWorldSp 773, MapEnts,
the native ClipMap singleton and every remaining family to RawFile asset 1,683
`killhouse`. The completed list has stream offsets
`[0,522928,0,0,47286243,0,0,26535904,3840644]`, no generated-loader failure,
and final publication in zone 4 at asset-entry 13,243.
RawFiles 395, 396, 398, 400, 402, and 404 are published through the canonical
Kisak type. XModel, Material, and FX publications now also expose stable
canonical top-level objects, while their retained nested census/dependency
records remain temporary convergence scaffolding. Preview render graphs have
been removed.

### Runtime/oracle separation

Normal launcher startup mounts the validated import, starts qcommon, and runs
the canonical database/runtime check. It leaves the census, archive probe,
engine-asset proof, canonical retail world, and bounded Killhouse draw idle.
`runtime.startGate2Oracle()` explicitly enables that retained diagnostic path;
every dependent browser test requests it. The oracle remains linked for now so
the same production artifact can prove compatibility, but its sources are
isolated in the `kisak_web_gate2_oracle` static library.

Gate 2-only production sources are the retail census/load dispatcher,
archive/engine-asset job chain, sound catalog/job, and their bounded renderer
proof. They are frozen regression infrastructure. Canonical DB/runtime sources
must not call them. The eventual split is a runtime artifact without that
library plus a diagnostic/test artifact that retains it, once canonical DB can
reproduce the same asset/world evidence.

### Prefix, build, and test ownership

`db_runtime_prefix.cpp` and `common_gate3_prefix.inl` carry source-level
shrink/no-growth contracts. Their exact functions, native owners, platform
hook, and deletion path are listed in
[`canonical-runtime-prefix-inventory.md`](canonical-runtime-prefix-inventory.md).
All eight duplicated DB lifecycle functions and the browser start hook have moved out of
`db_runtime_prefix.cpp`; the real `db_registry.cpp` translation unit now owns
the lifecycle browser-SP compile slice, while
`web_client_server_lifecycle.cpp` owns the narrow runtime command export. The
prefix retains only trace/failure state.

The production target and strict runtime check reuse one
`kisak_web_runtime_prefix` object library. The strict executable compiles only
its dedicated test translation unit instead of recompiling thirteen runtime
units. Compatible native/Wasm portable tests similarly reuse source-stream,
zone-stream, registry, surface, shader, and semantic-trace libraries;
sanitizer/fuzzer objects remain separately instrumented.

Routine browser ownership is split into disjoint `@smoke` and
`not @smoke and not @native-covered` tiers. Parser corruption, stream bounds,
PMem/pool exhaustion, generated-loader semantics, ABI, and deterministic
traces are authoritative in direct native/Wasm tests. Browser E2E retains one
Worker failure-propagation proof and owns lifecycle, storage, bridge, canvas,
and WebGL seams. The exhaustive browser suite remains explicitly available.

## System inventory

| System | Current status | Evidence and convergence action |
| --- | --- | --- |
| Emscripten build and packaging | `WEB PLATFORM IMPLEMENTATION` | Explicit `web` target, pinned toolchain, generated site, and HTTP server. Keep isolated from native target selection. |
| Browser launcher and legal asset selection | `WEB PLATFORM IMPLEMENTATION` | DOM file picker, minimum retail-profile validation, complete selected-root manifest, and restore UI are genuine browser responsibilities. The host persists generic logical paths; it does not select runtime search paths or packs. |
| OPFS/IndexedDB asset persistence | `WEB PLATFORM IMPLEMENTATION` | Keep storage asynchronous in the host and hidden behind the filesystem boundary. |
| Wasm filesystem bridge | `WEB PLATFORM IMPLEMENTATION` / reached read-only closure | The engine Worker mounts every validated logical-root entry through synchronous OPFS access handles and exposes stat/type, direct-child enumeration, open, size, seek, read, and close primitives. Import IDs and browser handles remain platform-only. Canonical `FS_InitFilesystem` now consumes this boundary and owns every search path and IWD; writable home storage remains future work. |
| Browser lifecycle, logging, and timing | `WEB PLATFORM IMPLEMENTATION` | Event-loop and page-lifecycle handling remain in `src/web`. `common.cpp` now owns engine print formatting while `Sys_Print`/`Sys_Error` remain narrow platform sinks. |
| System/thread context | `WEB PLATFORM IMPLEMENTATION` / partial | KisakCOD Wasm now runs in a dedicated engine Worker. The first DB model is synchronous within that Worker but enters a distinct logical `THREAD_CONTEXT_DATABASE`, preserves separate value slots plus wake/completion ordering, and reports no render/server thread or pthread concurrency. |
| Cooperative scheduler | `TEMPORARY WEB SUBSTITUTE` | It currently advances bootstrap jobs and protects the main thread. Retain it for the present traversal, but do not spread its state machines through shared engine code. Long term, stage browser I/O outside a Worker-hosted synchronous-looking engine. |
| Command system | `MODIFIED KISAK` / reached filesystem execution | Production Wasm and strict trace tests compile canonical `src/qcommon/cmd.cpp`; `cmd_core.cpp` is retired from all build lists. Native registration/lookup, tokenization, argument access, buffering, `wait`, startup commands, removal lifetime, and the shared SP map-command dispatch slice are compared under Win32 x86 and Wasm. Once canonical `FS_InitFilesystem` is live, production selects the canonical fastfile/disk `exec` path; the renderer/devgui configuration chain executes without unwinding the command-buffer guard. Production also accepts runtime `map` input through those tables and reaches the normal DB boundary. Script/client forwarding, remaining developer tooling, and autocomplete stay gated with their owning subsystems. |
| Dvar system | `MODIFIED KISAK` / partial | Production Wasm now compiles canonical `src/universal/dvar.cpp` and `dvar_cmds.cpp`; `dvar_core.cpp` is retired from that target. Persistence, file parsing, localization-only commands, and tracking are gated beyond the current prefix and must return with their owning subsystems. |
| qcommon startup | `MODIFIED KISAK` / reached lifecycle plus `TEMPORARY WEB SUBSTITUTE` oracle | Real `common.cpp` executes through `PMem_Init`, `DB_SetInitializing(true)`, `$init`, `Com_InitXAssets`, and `DB_InitThread`. The Worker continuation executes canonical `FS_InitFilesystem` at its native pre-Hunk point, establishes the canonical local player profile, initializes native Hunk/profiling/script/XAnim/DObj/server owners, and runs full canonical `CL_Init`. Renderer-owned startup zones publish before `SND_Init`, matching native ownership and making `soundaliases/channels.def` available without a synthetic fallback. Native engine operations remain synchronous-looking without Asyncify or Promises. |
| Physical memory | `MODIFIED KISAK` / platform-owned backing | Production Wasm compiles canonical `physicalmemory.cpp` and its fixed 128 MiB two-ended arena. It also initializes the native-sized 10 MiB fastfile Hunk through `com_memory_hunk.cpp`, preserving high/low allocation direction, alignment, zero-fill, and collision checks. Only page-aligned backing acquisition is browser-owned. Native/Wasm tests compare alignment, cursor order, named-scope reset, failure, and the first-map Hunk consumer. |
| Database initialization | `MODIFIED KISAK` / reached map closure | The web target compiles real `db_registry.cpp` ownership for `DB_BuildOSPath`, `DB_TryLoadXFileInternal`, `DB_TryLoadXFile`, `DB_Thread`, `DB_LoadXZone`, `DB_LoadZone_f`, `DB_InitThread`, and `DB_LoadXAssets`. Shared client configuration and renderer request construction submit `code_post_gfx`, `ui`, and `common` in native order; all 8,176 startup assets complete. A real runtime `map` command then submits `killhouse`, interns 892 script strings, and completes all 1,684 assets through asset 1,683 without seeking or a generated-loader failure. Narrow synchronous file and Sys context adapters own the platform boundary. |
| Canonical database asset ABI | `SHARED KISAK` / partial | `RawFile`, `XAssetHeader`, `XAssetType`, and `XAsset` remain renderer-free. The canonical 44-byte `PhysPreset` is shared from `physics/phys_preset.h`. Canonical Material/TechniqueSet records, texture and constant definitions, state bits, water, flexible techniques/passes, shader definitions, and arguments are shared from `gfx_d3d/material_types.h`; GfxImage/load-definition records remain in `gfx_image_types.h`. Database-facing menu, item, window, type-data, and expression records now live in lightweight `ui/ui_asset_types.h`, avoiding the unrelated gameplay/parser graph formerly pulled in through `ui_shared.h`. Native x86/Wasm tests cover exact 32-bit sizes and offsets. Canonical `XAnimParts`, `WeaponDef`, `LocalizeEntry`, XModel, FX, collision, world, and light structures remain shared lightweight ABI declarations. |
| IWD/ZIP reading | `MODIFIED KISAK` / canonical runtime reached plus oracle | Normal Worker startup uses Kisak `FS_LoadZipFile` and `qcommon/unzip.cpp` with zlib through the generic file primitive. Canonical C++ owns central-directory indexing, hashes, checksums, clones, member inflate, lookup, and lifetime. Gate 2 remains an independent bounded oracle. |
| IWI decoding | `MODIFIED KISAK` / canonical renderer path reached | The strict decoder handles retail ARGB/DXT IWI members and canonical DB load definitions, including the bounded 2048-square L8 lightmap atlas case and compressed 2048x1024 images that expand to 8 MiB of RGBA8. Streaming and U/V clamp policy bits are accepted because they do not change the 2D payload layout; cubemap, volume, legacy-normal, unknown-layout, malformed, and over-budget inputs still fail closed. The WebGL backend reads external `images/<GfxImage name>.iwi` through canonical FS/IWD ownership when a fastfile load definition intentionally has no pixels. |
| Fastfile framing and zone stream machine | `TEMPORARY WEB SUBSTITUTE` | It accurately models blocks, rewind/high-water behavior, pointer classes, aliases, and bounded streaming. Use it as differential evidence and migrate reusable mechanics toward the Kisak DB loader. |
| Asset registry | `MODIFIED KISAK` / partial plus `TEMPORARY WEB SUBSTITUTE` oracle | All generated families reached across the three startup prerequisites consume the canonical 32-bit asset-entry pool, per-type pools, free chain, normalized hash table, and zone ownership through `DB_AddXAsset`/`DB_LinkXAssetEntry`. The owned chain records 9,637 publications through entry 9,652, with free entries 32,752 -> 23,115. Gate 2 remains a frozen oracle and is not called by the generated path. |
| Retail loader dispatcher | `TEMPORARY WEB SUBSTITUTE` / frozen Gate 2 oracle | `web_retail_fastfile_census.*` remains the orchestration vehicle, but normal startup no longer executes it. The source has an explicit freeze contract and is isolated with its diagnostic dependencies in `kisak_web_gate2_oracle`; canonical DB code must not call it. It remains linked only until canonical DB can reproduce equivalent asset/world evidence. |
| `clipMap_t` asset loading | `MODIFIED KISAK` / canonical reached closure | The dedicated family transcribes the 284-byte `Load_clipMap_t` record, block-4 child order, block-1 zero-fill dynamic client allocations, root insertion/alias handling, bounded ownership, and atomic canonical publication for `col_map_sp`/`col_map_mp`. Synthetic MSVC/Wasm coverage exercises empty and populated child graphs under one-byte traversal budgets. Retail traversal now publishes directly into native `&cm`; real `CM_LoadMap` consumes that owner. Type 12 `com_map` remains correctly distinct and is handled by the canonical ComWorld family. |
| `ComWorld` asset loading | `MODIFIED KISAK` / canonical reached closure | `db_generated_comworld.cpp` owns native root null/inline/shared/prior-alias handling, the canonical body, name and 68-byte primary-light array, per-light `defName` XStrings, checked bounds, and final real-DB publication. Production publishes into the subsystem-owned `&comWorld`, and `com_world_runtime.cpp` supplies the real fastfile lookup/unload owner used after `CM_LoadMap`. Native x86/Wasm fixtures and runtime lookup evidence are exact. The normal Killhouse run publishes asset 704; `web_retail_load_comworld.*` remains frozen Gate 2 evidence only. |
| `GfxLightDef` asset loading | `MODIFIED KISAK` / canonical reached closure plus Gate 2 oracle | `db_generated_light.cpp` now owns normal generated loading: four-byte root cells, null/inline/shared/prior aliases, block-0 16-byte bodies, block-4 names, embedded 8-byte light images, canonical `GfxImage*` dependencies, insertion cells, and final-only real DB publication. Retail startup publishes two LightDefs. The independent `web_retail_load_lightdef.*` path remains frozen Gate 2 evidence for Killhouse asset 705; no rendering, attenuation evaluation, shadow behavior, or primary-light linking is implemented. |
| `XAnimParts` asset loading | `MODIFIED KISAK` / canonical reached closure | `db_generated_xanim.cpp` now follows native `Load_XAnimPartsPtr` / `Load_XAnimParts`: block-0 body allocation, shared/insertion/prior root forms, block-4 name and payload scope, ScriptStrings, notifies, exact array order, low/high-frame index widths, and flexible delta translation/quaternion storage. It publishes through the real DB pool/hash/free chain. The complete prerequisite chain records 1,017 XAnimParts publications. Gate 2 retains independent Killhouse evidence only. |
| `WeaponDef` asset loading | `MODIFIED KISAK` / canonical reached closure | `db_generated_weapon.cpp` owns the canonical 2,168-byte body and native order for 48 XStrings, 38 XModel dependencies, ten FX handles, eight Materials, 48 sound-name cells, 40 ScriptStrings, the 29-entry bounce array, and four accuracy arrays. Root null/shared/insertion/prior forms and final-only publication use real DB ownership. The complete prerequisite chain records 17 WeaponDef publications; Gate 2's Killhouse definitions remain an independent oracle. |
| `StringTable` asset loading | `MODIFIED KISAK` / canonical reached closure | The canonical special-case root semantics are preserved: null, `-1` inline, or ordinary pointer conversion without a block-0 push or `-2` insertion. The 16-byte body, name, row/column value table, XStrings, overflow checks, and final publication use the real type-32 pool. The complete prerequisite chain publishes two StringTables. |
| Sound alias loading and catalog | `MODIFIED KISAK` / partial | The prerequisite dispatcher mirrors the native list/header/component order and owns canonical `snd_alias_list_t`, `snd_alias_t`, `SoundFile`, `LoadedSound`, `SndCurve`, and `SpeakerMap` metadata in the publishing zone. `web_sound_alias_catalog.*` remains a case-insensitive ownership/index seam: it stores pointers to those objects, retains the common-zone owner, collapses native DB name aliases, and uses the indexed canonical `null` sound for native missing-sound fallback. It neither copies nor synthesizes sound records. No payload playback or audio runtime behavior is implemented. |
| `LocalizeEntry` asset loading | `MODIFIED KISAK` / canonical reached closure | The normal generated DB path now owns the renderer-free eight-byte ABI, block-0 body, block-4 value/name XStrings, null/inline/shared/prior root forms, insertion cells, real pool/hash/zone ownership, and final-only publication. Owned `code_post_gfx.ff` publishes 1,248 LocalizeEntry assets before continuing naturally through later families. Gate 2 retains independent oracle evidence only. |
| Sound asset loading | `MODIFIED KISAK` / canonical reached closure | The normal generated path owns SndCurve, sound-alias list/entry arrays, streamed SoundFile names, SpeakerMap, LoadedSound payloads and real publication. `DB_PlatformSetLoadedSoundData` is the narrow temporary audio-payload seam and exposes no browser concepts to DB code. The native dispatcher intentionally performs no work for SndDriverGlobals, which is preserved exactly. |
| Font asset loading | `MODIFIED KISAK` / canonical reached closure | The generated path owns the 24-byte Font body, font-name XString, two canonical MaterialHandle dependencies, bounded Glyph array and final type-19 publication. Retail publishes nine Fonts before advancing to FX. |
| FX asset loading | `MODIFIED KISAK` / canonical reached closure | The generated path owns the 32-byte effect, 252-byte element graph, velocity/visual samples, Material/sound/effect/XModel visuals, trails, root aliases and final type-25 publication. Inline XModel visuals now reuse the canonical generated XModel family. Retail publishes `misc/missing_fx` after its nested Material and GfxImage, then advances to FxImpactTable. |
| FxImpactTable asset loading | `MODIFIED KISAK` / canonical reached closure | `db_generated_fx_impact.cpp` owns the eight-byte root body, fixed 12-entry table, 29 non-flesh plus four flesh FxEffectDef handles per entry, aliases, insertion cells, child-before-parent ordering, and final type-26 publication. Retail publishes `default` before advancing to LightDef. |
| MenuList/Menu asset loading | `MODIFIED KISAK` / canonical reached closure | Dedicated menu and expression units preserve the 12-byte list, 284-byte Menu, 372-byte items, windows, recursive key handlers, type-specific list/edit/multi records, statement/operand trees, Material and sound dependencies, root aliases, insertion cells, Menu-before-MenuList publication, and native item-parent reparenting. The complete prerequisite chain records five MenuList and 141 Menu publications. |
| `XModel` | `MODIFIED KISAK` / canonical reached closure | Dedicated generated units own the canonical 220-byte model, skeleton arrays, XSurface block-7 vertices/block-8 indices, rigid collision trees, Materials, model collision surfaces, bone information, PhysPreset, and physical geometry/brush graph. Root null/shared/insertion/prior forms and dependency-before-parent publication use real DB ownership. The complete prerequisite chain records 37 XModel publications; Gate 2 retains separate broader Killhouse evidence only. |
| `Material` and techniques | `MODIFIED KISAK` / partial | The canonical generated path owns Material and TechniqueSet bodies, names, tables, techniques/passes, shader records/bytecode, texture/image and water dependencies, constants, state bits, pointer aliases, insertion cells, and dependency-ordered final DB publication. Renderer-disabled hooks retain canonical signatures without D3D objects. The complete prerequisite chain records 395 Material and 192 TechniqueSet publications. Gate 2's broader records remain oracle-only. |
| `GfxImage` | `MODIFIED KISAK` / canonical renderer consumption reached; native backend `NATIVE ONLY` | The generated family loads 36-byte image bodies, XString names, texture load definitions/payloads, aliases, insertion cells, and canonical DB publication. At the native `Load_Texture` boundary, Web copies transient block-zero payloads into bounded platform storage before clearing the GPU union; external-pixel images retain their canonical name/metadata and are resolved through FS/IWD at submission. WebGL texture creation and context recovery remain backend-owned and canonical `GfxImage*` identity is retained even when decoding falls back. |
| `GfxWorld` | `MODIFIED KISAK` / canonical reached closure | `db_generated_gfxworld.cpp` follows the native generated closure through names, indices, images, cells/portals, lightmaps/grid, vertices/layers, models, shadow/light regions, DPVS static/dynamic, block-1 runtime allocations, and nested canonical dependencies before final real-DB publication into the renderer-owned `&s_world`. Native x86/Wasm fixtures are byte-for-byte identical. The normal Killhouse run publishes asset 772 with Gate 2-matching structural counts and inflated offset. `WebEngine_BuildGfxWorldSurface` consumes that DB-owned object through a final-publication platform notification and WebGL2 draws surface 6077 without a browser world model. The frozen Gate 2 material label differs from the real DB Material pointer and remains recorded rather than normalized away. `web_retail_load_gfxworld.*` is oracle-only. |
| XModel/model preview scene | `RETIRED` | Removed after Gate 2: no selectable-model UI/state, retained preview geometry, preview camera/projection, preview material bridge, or multi-draw command path remains. Canonical XModel loading/publication and dependencies remain available to `GfxWorld`, `WeaponDef`, FX, and later runtime consumers. |
| Renderer frontend | `MODIFIED KISAK` / textured, lightmapped gameplay scene reached | The production Wasm target links the real client/cgame/effects/ragdoll/physics closure against a narrow renderer-frontend platform implementation. `R_RenderScene` validates canonical `refdef_s`, constructs Kisak view/projection matrices, traverses `s_world.models[0].surfaceCountNoDecal` in canonical order, excludes the separate sky pass, and emits 514 material-aware batches with canonical `Material*`, base `GfxImage*`, state bits, sampler state, lightmap index, base UV, and lightmap UV. It additionally groups `GfxStaticModelDrawInst` placements by canonical `XModel`, retains shared LOD XSurfaces, and consumes cgame's first-person `R_AddDObjToScene` submission through canonical pose evaluation and rigid/weighted skinning. EffectsCore `R_AddCodeMeshDrawSurf` now converts packed canonical sprite/beam spans at the renderer seam, appends them in submission order to the retained dynamic command, and carries canonical Material/GfxImage state, UVs, RGBA vertex color, alpha blending, and depth behavior through WebGL2. The minimum 2D callback set retains stretch/quad/rotated-ST images and basic/subtitle/cursor/effects text as canonical Material/Font identities. Chrome verifies the world, 238 static models, posed weapon/viewhands, reticle, readable HUD/ammo, and recoil together; unsupported techniques retain identity behind backend fallback. |
| WebGL2 backend and context recovery | `WEB PLATFORM IMPLEMENTATION` | Permanent platform boundary. It converts D3D9 NDC depth `[0,1]` to WebGL `[-1,1]`, retains 32-bit indices, uploads canonical external IWI and DB load-definition pixels, combines base and primary L8 lightmap textures, and applies the encountered Killhouse cull, depth, color-write, alpha-test/blend, addressing, and filtering states per material batch. Static XSurface geometry is instanced from canonical placements, posed DObj geometry is refreshed per frame, and the 2D pass uses canonical image/font atlases with alpha blending. Unsupported image/technique identities use the backend fallback without being rewritten to a synthetic material. GPU handles remain backend-only and all texture/geometry objects are recreated after context loss. |
| D3D9 renderer backend | `NATIVE ONLY` | Retain for native builds and use as behavioral reference; do not compile Direct3D objects into Wasm. |
| Shader compatibility | `MODIFIED KISAK` / `WEB PLATFORM IMPLEMENTATION` boundary | Native material/shader contracts should remain canonical; selecting or translating to built-in GLSL belongs at the backend seam. |
| ODE math | `SHARED KISAK` | `src/physics/ode/odemath.cpp` is compiled directly. Expand shared ODE/collision code based on compile inventory and measured needs. |
| Collision and `CM_LoadMap` | `SHARED KISAK` / reached runtime owner | Production compiles actual `cm_load.cpp`; the DB ClipMap singleton pool is `&cm`, and successful retail map DB completion continues through real `CM_LoadMap`. It initializes canonical collision thread data and publishes checksum/in-use state, followed by real `Com_LoadWorld`. An exact MSVC x86/Wasm differential invokes the owner and matches all normalized results. |
| xanim and DObj runtime | `SHARED KISAK` / first-person pose consumed | `xanim_init.cpp` and `dobj_init.cpp` own the actual 4,096-entry `XAnimInfo` free ring, `end` notetrack ScriptString, and duplicate-parts ScriptString initialization. Lightweight runtime headers expose the same canonical structs without importing D3D. Production executes them in native `Com_Init` order; exact x86/Wasm evidence covers the ring and handles. The renderer frontend now invokes canonical `CG_DObjCalcPose`, derives base-to-current skin matrices, and submits rigid and weighted first-person XSurfaces while preserving DObj/XModel/Material identity. General entity/effect DObj submission remains future closure. |
| Script VM and `Scr_Init` | `SHARED KISAK` / reached game closure | Actual variable-range setup, `AllocValue`, `Scr_Init`, `Scr_VM_Init`, and `Scr_Settings` run in production. Canonical VM/compiler/anim public state layouts were moved to renderer-free headers, not replaced. Retail execution resolves RawFiles/scripts through canonical DB/FS, compiles the game scripts, and completes `G_InitGame` and `G_LoadLevel` with native script-string and VM ownership. A pointer-range portability correction preserves the native eval-stack/main-stack distinction on Wasm. |
| Client and `CL_Init` | `MODIFIED KISAK` / reached lifecycle owner | `cl_initialization.cpp` owns the canonical `CL_Init` body and client globals, and a Win32 x86/Wasm differential runs its normalized contract identically. Production enters full `CL_Init` after canonical filesystem initialization and later completes `CL_InitCGame`. No browser-owned client state substitutes for it. |
| cgame and `CG_Init` | `SHARED KISAK` / playable renderer milestone proved | The linked cgame closure executes through real `CL_InitCGame -> CG_Init` for the local SP client. Once active, the browser scheduler supplies only elapsed time while canonical `SV_Frame -> CL_RunOncePerClientFrame -> CL_Frame -> SCR_UpdateScreen` owns state advancement, weapon pose, HUD construction, and view construction. Chrome records the successful material/lightmap world draw, static XModels, posed weapon/viewhands, canonical 2D HUD/font stream, and visible attack recoil for `maps/killhouse.d3dbsp`. Gate 2 is not invoked. |
| game and `SV_Init` | `MODIFIED KISAK` / reached live game-frame closure | Shared owners preserve `SV_Init`, `SV_Map_f`, map-command registration, `SV_SpawnServer`, and `SV_LoadLevelAssets`. The retail path continues through real `CM_LoadMap`, `Com_LoadWorld`, save initialization, `SV_InitGameProgs`, `SV_InitGameVM`, `G_InitGame`, five canonical settle frames, direct-connect/client-enter-world, `G_LoadLevel`, and ongoing `SV_Frame` calls. Those frames advance the scripted Killhouse start mover and publish snapshots before cgame constructs each view. Entity, level, server, and script state remain canonical Kisak owners. |
| Input | `MODIFIED KISAK` / movement, look, and fire reached plus `WEB PLATFORM IMPLEMENTATION` | The main-thread launcher maps DOM keyboard, mouse buttons, wheel, and pointer-lock deltas into a bounded Worker queue. Wasm drains it through canonical `CL_KeyEvent` and `CL_MouseEvent`; native bindings, `CG_DrawActiveFrame -> CL_Input`, usercmd creation, `SV_Frame` consumption, prediction, `Pmove`, and weapon state remain authoritative. Fresh browser profiles receive WASD/Space/Shift/Mouse defaults only when those keys are unbound. The browser pump refreshes native `com_frameTime` and accumulates real Worker elapsed time before advancing gameplay at no more than 125 Hz, so faster-than-vsync callbacks neither manufacture server milliseconds nor distort snapped movement. SP `PM_GroundTrace` retains its canonical primary probe, adds a bounded walkable-support check for the sub-unit Killhouse standing-hull gap, and shares ground detachment state outside the MP-only jump-animation branch. Chrome proves grounded spawn/incline traversal, zero velocity and position drift after release, camera rotation, player-origin movement, and `mouse1` bound to `+attack` producing visible weapon/camera recoil with `keyCatchers=0`. Cursor visibility is marshalled back to the DOM host instead of accessing `OffscreenCanvas.style`; gamepad collection remains future work. |
| Audio | `MODIFIED KISAK` / loaded-sound bridge plus `WEB PLATFORM IMPLEMENTATION` | Canonical `SND_InitDriver -> renderer registration -> SND_Init` ordering runs in the Worker and keeps alias selection, channel state, attenuation, playback IDs, and LoadedSound PCM in Kisak. The KISAK_WEB OpenAL-compatible proxy mirrors only driver-visible source/buffer state and sends bounded typed PCM commands to main-thread `WebAudioDriver`, which owns AudioContext/AudioBuffer/AudioNode resources and gesture resume. Loaded 16-bit mono/stereo fire/impact playback is covered; streaming, reverb, and full OpenAL parity remain unsupported. |
| Networking | `NOT COMPILED` / future `WEB PLATFORM IMPLEMENTATION` | Offline single-player first. Any later multiplayer requires a framed WebSocket/WebTransport relay; browsers cannot use raw COD4 UDP. |
| Bink cinematics, Miles, and Steam | `NATIVE ONLY` | Feature-gate them. Use browser-compatible video/audio/auth paths or graceful omission without shipping native proprietary binaries. |
| Database semantic trace | `MODIFIED KISAK` / partial | The Gate 3 runtime trace now records list boundaries, ordered interned strings, asset index/type/name, pointer class, publication boundaries, pool/entry indices, free counts, canonical hash, zone, nine final offsets, and failure stage without addresses. Win32 x86 and Wasm execute the same extracted generated closure and print identical normalized results. |
| Portable parser tests | `MODIFIED KISAK` / partial | Empty-list, string-list, RawFile, PhysPreset, TechniqueSet, Material, image, water, LocalizeEntry, SndCurve, sound alias, LoadedSound, Font, FX, FxImpactTable, GfxLightDef, MenuList, Menu, item/type-data, and expression fixtures cover pointer forms, dependency ordering, payloads, direct/interior XStrings, malformed streams/tokens/counts, pool/entry exhaustion, and failure-before-publication. The same direct executable runs under Win32 x86 and Emscripten; browser tests cover the Worker filesystem boundary. The census dispatcher fuzz target remains independent regression evidence. |
| Playwright browser tests | `WEB PLATFORM IMPLEMENTATION` | Routine smoke/remainder tiers are disjoint and focus on Worker, storage, lifecycle, bridge, canvas, and WebGL behavior. `@native-covered` duplicates run only in the explicit exhaustive suite. Synthetic assets only. |
| Synthetic CI | `MODIFIED KISAK` / platform verification | GitHub Actions builds and tests native Linux, sanitized parser/dispatcher fuzz targets, Win32 MSVC, Emscripten/Node differential contracts, and disjoint Playwright smoke/remainder tiers. It builds one Release site, tests that exact site, then uploads it; strict undefined-symbol checking reuses the runtime-prefix objects. No retail assets are fetched or embedded. |

## Convergence gates

### Checkpoint 1: shared ABI and trace vocabulary (complete)

- Canonical database-facing asset declarations no longer require the full
  Direct3D-heavy xanim include graph.
- Wasm verifies the original 32-bit `RawFile`, `XAssetHeader`, and `XAsset`
  layout.
- The current loader emits normalized top-level begin, publication, and
  boundary events with an explicit ceiling and deterministic hash.
- The trace intentionally contains no process addresses or graphics handles.
- The native producer hook is present and the portable observer projection
  passes under MSVC. Full native generated-loader execution remains pending the
  monolithic target's SDK/runtime prerequisites and a legally owned test
  environment; the projection is not mislabeled as that run.

### Checkpoint 2: canonical RawFile publication (complete)

- Native `Load_XAssetArrayCustom` establishes the top-level trace context and
  `Load_RawFilePtr` emits logical block coordinates around atomic publication.
- The web loader follows the same header/name/payload ordering and exposes a
  canonical `RawFile` whose pointers have stable owned lifetime.
- Explicit count, name, individual payload, aggregate retained-byte, and trace
  ceilings keep failure atomic.
- The synthetic contract test covers consecutive RawFiles, collection ceilings,
  and `RawFile -> XModel -> RawFile` dispatcher return. The owned Killhouse run
  publishes six canonical RawFiles through asset 404, then continues through
  XModel, technique-set, and FX dependencies to asset 436.
- FX visuals now reuse the checked XModel material/image dependency loader.
  Shared material insertion cells and typed aliases publish only after complete
  dependency success. Native `FxEffectDef::elemDefs` is treated as a presence
  field and `totalSize` does not size database traversal.

### Checkpoint 3: canonical XAnimParts publication (complete)

- The renderer-free canonical type header is shared by native xanim code and
  the portable database path; no permanent `RetailXAnim*` object model was
  introduced.
- Synthetic coverage mirrors `-1`, `-2`, insertion-cell, and prior-alias
  pointer forms; bone and notify tables; every packed data array; both index
  widths; flexible delta translation/quaternion payloads; zero-length presence
  fields; bounded ownership; and atomic failure.
- Both Win32/MSVC and Wasm suites pass after the shared change.
- The owned Killhouse run publishes XAnimParts assets 437-457 as identities
  1368-1388 before entering the WeaponDef dependency graph.

### Checkpoint 4: canonical WeaponDef dependency handles (complete for the pre-`com_map` prefix)

- Renderer-free canonical declarations for XModel, Material, the draw-surface
  key, and the FX header are shared by native headers and the portable loader;
  32-bit Win32/Wasm ABI tests cover their exact sizes and key offsets.
- Existing checked XModel, Material, and FX loaders expose stable canonical
  top-level objects. Weapon handles resolve prior typed registry aliases to
  those exact pointers in generated-loader order; inline child bodies still
  fail explicitly rather than forking the loaders.
- `Load_snd_alias_list_name` semantics are modeled as an XString-pointer cell
  followed by an injected `ASSET_TYPE_SOUND` name lookup. Direct cells, inline
  names, the 29-entry bounce array, reused cells, lookup failure, and atomic
  publication are covered synthetically. No placeholder sound asset is made.
- Generic FX Material visuals now preserve native block-4 pointer-cell alias
  semantics. Asset 4,098 is no longer a special boundary, and a synthetic
  regression covers a normal token that dereferences an earlier patched
  Material visual cell.
- The owned `common.ff` run completes all 6,502 assets, including all 1,723
  type-7 rows beginning at 4,778. It publishes 1,716 unique canonical sound
  objects into the cross-zone index while retaining serialized and DB aliases
  in the zone result.
- The owned common-to-Killhouse run publishes WeaponDef 458 through normal
  case-insensitive lookup. Pickup and ammo-pickup fields point at the exact
  common objects; a genuinely absent NPC alias follows native behavior to the
  indexed zone-owned `null` sound. No audio playback behavior was added.
- Generic XString conversion now models a native address rather than requiring
  an exact string-start key. Weapon animation fields can point into a prior
  XAnim name and retain the intended suffix. Bounded compatibility offsets are
  monotonic and inferred only from generated WeaponDef ordering, replacing the
  earlier unsafe arbitrary-string calibration.
- Ordered traversal continues through nine more WeaponDefs and all intervening
  technique-set, XModel, FX, XAnimParts, and RawFile rows. It publishes type-12
  `com_map` asset 704, type-17 `lightdef` asset 705, the 66 technique sets at
  706-771, and canonical `GfxWorld` 772. No direct seek or reconstructed
  world-body entry is used.

### Gate 1: finish the pre-GfxWorld dependency graph

- The native type-23 `Load_WeaponDefPtr` / `Load_WeaponDef` contract at asset
  458 is inventoried, including its canonical 2,168-byte layout, exact block-4
  dependency order, sound-name indirections, dynamic arrays, aliases, and
  atomic publication envelope. The canonical root/body, scalar, XString,
  script-string, accuracy-array, prior canonical child-alias, and sound-name
  indirection slices are implemented. The general prerequisite-zone owner,
  canonical sound publication, cross-zone index, and owned asset-458 rerun are
  complete. The native `Load_clipMap_ptr` / `Load_clipMap_t` family is isolated
  and publishes canonical `clipMap_t` for asset types 10/11. The dedicated
  ComWorld family publishes Killhouse asset 704. The dedicated LightDef and
  reusable image families now publish asset 705 and its canonical attenuation
  dependency. Gate 1 reaches the natural pre-world boundary at asset 772.
- Preserve block cursors, high-water marks, insertion cells, aliases, dependency
  order, and atomic publication.
- Do not seek directly to or enter asset 772 until the pre-world state receives
  architectural review.
- For each new family, inventory the corresponding Kisak generated/native
  loader before designing a representation.
- Add a semantic trace that can be compared with a native Kisak run where
  practical.

### Gate 2: canonical GfxWorld publication

- Complete. The full serialized graph loads into canonical Kisak structures,
  with wire decoding and ownership kept separate from the in-memory type.
- Publication is final-only and records exact block/registry state plus semantic
  stage checkpoints. Strict count and retained-byte ceilings guard every array.
- One bounded real Killhouse `GfxSurface` is copied directly from canonical
  `vd.vertices`, `indices`, `dpvs.surfaces`, and `Material*` into the existing
  portable WebGL2 contract. GPU resources remain renderer-owned.
- Stop viewer growth here and begin the Gate 3 runtime pivot.

### Gate 3: runtime pivot

Once Gate 2 renders enough geometry to prove the pipeline, stop broadening the
viewer. Prioritize compile inventories and vertical initialization slices for:

The current [`canonical runtime prefix inventory`](canonical-runtime-prefix-inventory.md)
classifies every remaining temporary DB/common owner and its retirement path.
The earlier Gate 3 checkpoints are retained under `docs/history/`.

The second runtime slice is complete: `common.cpp` reaches real `PMem_Init`,
`DB_SetInitializing(true)`, and `PMem_BeginAlloc("$init", 1)`. Both platforms
then reached the `Com_InitXAssets` call boundary before
`DB_InitThread`/`Sys_SpawnDatabaseThread`, with 14 stages, three startup-line
segments, six commands, 22 prefix dvars, and identical 128 MiB arena state.
Canonical `dvar.cpp` and `cmd.cpp` have replaced `dvar_core.cpp` and
`cmd_core.cpp` in production. No filesystem Promise, Asyncify path,
census-as-database call or post-boundary subsystem was added.

The third runtime slice is described in the historical
[`Gate 3 Worker/database inventory`](history/gate-3-worker-database-inventory.md).
The browser now uses the target main-thread launcher -> dedicated engine Worker
-> Wasm -> synchronous engine filesystem shape. Database work is synchronous
inside that Worker with a distinct logical DB context; inventory found no
current correctness requirement for a pthread or second Worker. Canonical pool
state and zone request ordering initialize before the normal logical zone path.
The follow-on
[`Gate 3 XFile streaming inventory`](history/gate-3-xfile-streaming-inventory.md)
now records incremental inflate, PMem block allocation, stream initialization,
and the former generated-loader boundary. The next
[`Gate 3 generated-loader inventory`](history/gate-3-generated-loader-inventory.md)
records the earlier shared generated list/string/asset/RawFile extraction.
The current
[`DB registry and PhysPreset inventory`](gate-3-db-registry-physpreset-inventory.md)
records lifecycle ownership, matching Win32 x86/Wasm PhysPreset publication,
and the owned retail traversal.
The follow-on
[`MaterialTechniqueSet inventory`](gate-3-material-technique-set-inventory.md)
records the exact generated child graph, renderer-disabled post-load boundary,
matching synthetic publication, and the new retail Material boundary.
The current [`Material inventory`](gate-3-material-inventory.md) records the
Material, image, and water generated closures, exact ABI/stream ordering,
matching synthetic ownership, and the retail LocalizeEntry boundary.
The [`LocalizeEntry inventory`](gate-3-localize-inventory.md) records the exact
eight-byte body/XString closure; the completed first retail zone totals 1,351
LocalizeEntry publications. The current
[`Sound and Font inventory`](gate-3-sound-font-inventory.md) records the next
four canonical families. The [`FX inventory`](gate-3-fx-inventory.md) records
the canonical element graph, explicit inline-XModel boundary, and the new
FxImpactTable retail blocker. The follow-on
[`Impact, Light, and Menu inventory`](gate-3-impact-light-menu-inventory.md)
records those canonical closures and the lightweight UI ABI extraction. The
current
[`canonical asset coverage map`](gate-3-asset-coverage.md) separates normal DB
ownership from the remaining Gate 2-only families and temporary seams.

The former `DB_TryLoadXFileInternal -> CreateFileA` boundary is now a narrow
platform open over the Worker mount. The double-buffered reader, inflate setup,
XFile block table, zone streams, generated prefix, RawFile, PhysPreset,
MaterialTechniqueSet, Material, GfxImage, water, LocalizeEntry, SoundCurve,
sound alias, LoadedSound, Font, FxEffectDef, FxImpactTable, GfxLightDef,
MenuList, Menu, XModel, WeaponDef, XAnimParts, and StringTable publication now
use the canonical DB path. Shared client and renderer code now submit the
native `code_post_gfx`, `ui`, `common` prerequisite request. The owned chain
completes all 8,176 ordered assets and records 9,637 publications, ending
naturally at RawFile `common` with block offsets
`[0,0,0,0,28021740,0,0,438944,76704]`.

The runtime continuation does not invent a browser map owner. Production now
executes shared `SV_Init`, `SV_Map_f`, the map-loading portion of real
`SV_SpawnServer`, and `SV_LoadLevelAssets`. A command supplied as
`map KiLlHoUsE` passes through the real command tables, normalizes to
`killhouse`, constructs `{name, alloc=8, free=8}, sync=0`, and opens
`zone/english/killhouse.ff` through the normal Worker filesystem. The real DB
interns 892 script strings and traverses the 1,684-entry XAsset list through
canonical GfxWorld asset 772, GameWorldSp 773, MapEnts and ClipMap, then every
remaining ordered family through RawFile 1,683 `killhouse`. The list ends with
stream offsets `[0,522928,0,0,47286243,0,0,26535904,3840644]` and no generated
failure. Gate 2 remains an independent frozen oracle used only to compare the
published DB world and bounded render result.

Generated-loader convergence has reached canonical `ComWorld`, `GfxWorld`,
GameWorldSp, MapEnts and ClipMap, and the real DB-owned bounded world has
rendered both through the frozen Gate 2 oracle and through the actual
server/game -> client/cgame -> renderer frontend -> WebGL2 path. The runtime
pivot compiles and executes `CM_LoadMap`, `Com_LoadWorld`, the save system,
script VM, XAnim, DObj, client, cgame, game, effects, ragdoll, physics, and
sound closures. The retail browser run reaches `G_InitGame`, completes
`G_LoadLevel`, `SV_InitGameVM`, `SV_InitGameProgs`, `CL_InitCGame`, and
`CG_Init`, then advances ongoing server snapshots into cgame without a
browser-owned script/game/server state. The Worker-filesystem decision is now
implemented: canonical
`FS_InitFilesystem` consumes generic enumeration and read primitives,
discovers the generated fixture IWDs, validates `fileSysCheck.cfg` through
minizip, and continues into the DB-owned runtime without a second browser
asset registry.

1. `Com_Init` and the real qcommon lifecycle (reached through the owned prefix).
2. `DB_LoadXZone` and canonical asset ownership (reached through Killhouse end).
3. canonical `FS_InitFilesystem` over the Worker mount (reached).
4. `CM_LoadMap` and `Com_LoadWorld` after native `&cm` publication (reached).
5. `G_InitGame`, `G_LoadLevel`, `CL_InitCGame`, and `CG_Init` (reached).
6. Actual cgame view/camera and indexed world submission through the renderer
   frontend and WebGL2 (reached).
7. Browser keyboard/pointer-lock input and canonical player movement (reached);
   gamepad and audio-policy completion remain for the offline playable slice.

The preferred long-term host is a main-thread launcher/file picker plus a
dedicated Worker containing KisakCOD Wasm, synchronous-style engine filesystem
operations, and OffscreenCanvas/WebGL2. This direction does not itself justify
Asyncify or pthreads.

## Differential test contract

Where a native reference path can consume the same legal synthetic fixture,
compare semantic events rather than implementation details. The minimum useful
trace is:

```text
asset index and type
asset name
stream position and active block
allocation and insertion-cell offsets
pointer classification and alias target
dependency enter/leave order
publication order
nested asset counts
surface and index counts
material references
texture metadata
failure stage and location
```

Normalize real addresses and backend handles into stable logical identities.
A test should fail if native and web paths publish different semantic results,
even when both parsers individually report success.

## Trend indicators

Update this section when a milestone changes architectural ownership.

| Indicator | Required direction | Current reading |
| --- | --- | --- |
| Shared or narrowly modified Kisak code in the web target | Increase | Improving: 45 of 77 production translation units are outside `src/web`, now including real `db_registry.cpp`, the DB file-platform seam, generated RawFile/PhysPreset/TechniqueSet/Material/Image/water/LocalizeEntry/Sound/Font/FX/Impact/Light/Menu loading, and canonical registry publication. |
| Browser-only engine substitutes | Decrease after their validation purpose is met | High but falling: `dvar_core.cpp` and `cmd_core.cpp` are retired from production. The VFS qcommon oracle, retail DB traversal, and temporary nested asset records remain substitutes. The XModel preview frontend is retired. |
| Permanent browser platform code | Stable and isolated | Good: launcher, storage, lifecycle, filesystem bridge, DB texture-upload boundary, and WebGL2 material/lightmap resources remain under explicit platform ownership. |
| Native engine systems not compiled | Decrease sharply after the GfxWorld proof | High but falling: qcommon, the full Killhouse generated-family order, filesystem, collision, script VM, xanim/DObj, server/game, client/cgame, effects, ragdoll, physics, sound, and canonical renderer-dvar owners now compile and execute through gameplay. Static XModels and the first-person dynamic DObj are submitted; remaining renderer gaps are general entity/effect draws and deliberately deferred shader/postprocessing features, not a parallel browser world or camera owner. |
| Native-vs-web semantic comparisons | Increase | The Gate 3 startup closure matches under Win32 x86 and Wasm through RawFile, PhysPreset, TechniqueSet, Material, nested/top-level images, water, LocalizeEntry, SoundCurve, sound aliases, LoadedSound, Font, FX, Impact, Light, MenuList, and Menu. Fixtures cover `-1`, `-2`, insertion, aliases, direct/inline/interior XStrings, dependency ordering, payloads, final-only publication, failure atomicity, and deterministic pool/free-chain deltas. |
| Viewer-only feature work | Stop after world proof | Retired: the canonical world-to-WebGL2 seam is proven and the XModel preview UI, state, bridge, retained geometry, and multi-draw path have been removed. |

## Update rule

For each substantial milestone, update the snapshot, affected rows, and trend
indicators. Record whether it:

- compiles more Kisak code,
- replaces or retires a temporary substitute,
- introduces a justified permanent platform implementation,
- adds a native-vs-web semantic comparison, or
- leaves convergence unchanged and why.
