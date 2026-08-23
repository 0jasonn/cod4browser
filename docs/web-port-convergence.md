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
`GfxWorld` lit/decal/emissive camera ranges, emits portable material-aware
indexed batches, and supplies the canonical `s_world.skyImage` to a WebGL2
cubemap pass. Chrome records the canonical `refdef_s` for
`maps/killhouse.d3dbsp`, followed by a successful WebGL2 draw of 8,475 canonical
surfaces, 445,369 retained vertices, and 823,464 32-bit indices in 581 batches
after the scripted start mover descends into the world view. Of those batches,
125 draws (3,448 surfaces) use the native non-normal directional-lightmap
equation, 413 draws (4,819 surfaces) use its DXT5nm slope-space normal variant,
22 draws (58 surfaces) use native framebuffer multiplication, 14 draws (74
surfaces) use the native premultiplied additive effect, and five draws (67
surfaces) use base textures only. Two draws (nine
`wc/com_crater_blacktop` surfaces) retain canonical identity behind explicit
image fallback. Gate 2 remains a separate frozen
oracle and is not invoked by this path.

The same cgame frame now submits the world-owned static-model population,
canonical ordinary and first-person DObjs, and live canonical model/brush
DynEntities without introducing a preview object model. This restores map
movers such as the Killhouse firing targets and exit door. The WebGL2 boundary
retains 238 canonical `XModel` identities as 359
shared XSurface batches (68,684 vertices, 135,492 indices, and 12,188
placements). Commit `748112cc` retains ordinary entity DObjs through the fixed
native 512-entry scene array; commit `de695b46` selects the canonical
`XModelGetLodForDist` result from cpose/view origins so this broader submission
does not force every model through LOD 0. Release Chrome records 64 DObjs, 102
models, 147 surfaces, 16,435 posed vertices, and 33,672 indices in the first
dynamic scene, with a visibly posed Gaz actor and continued scene submission.
The first-person path still calls canonical DObj pose evaluation and skinning
each frame. Canonical 2D callbacks also retain HUD
quads and font glyphs, including the `gamefonts_pc` and `devfonts` `TS_2D`
atlases. A clean Chrome run shows the weapon, hands, center reticle, readable
HUD/ammo, compass, and mission text together, then records `mouse1 -> +attack`
with no UI catcher and visibly advances the canonical weapon/camera recoil.
The gameplay presentation path now also submits EffectsCore smoke/code meshes,
rigid ejected-shell XModels, and clipped persistent world marks through one
ordered dynamic command. Live MP5 fire records a textured metal/concrete impact
mark, material-specific impact audio, and retained 44.1 kHz weapon PCM. The
encountered `floodlight_beam` BGR8 image is decoded instead of drawing a flat
blended fallback polygon.

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
| OPFS/IndexedDB asset persistence | `WEB PLATFORM IMPLEMENTATION` | Keep storage asynchronous in the host and hidden behind the filesystem boundary. Validated installation files remain immutable; the separate bounded browser home tree restores from and persists to OPFS. |
| Wasm filesystem bridge | `WEB PLATFORM IMPLEMENTATION` / reached read/write closure | The engine Worker mounts every validated logical-root entry through read-only synchronous OPFS access handles and exposes stat/type, direct-child enumeration, open, size, seek, read, and close primitives. A separate synchronous-looking writable overlay owns directory creation, truncate/append, bounded writes, remove, and rename for canonical `fs_homepath` calls, snapshots closed files to OPFS asynchronously, and restores them before engine mount. Import IDs and browser handles remain platform-only. Canonical `FS_InitFilesystem`, `FS_CreatePath`, and the ordinary save/config APIs consume this boundary without Asyncify. |
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
| IWI decoding | `MODIFIED KISAK` / canonical renderer path reached | The strict decoder handles retail ARGB, BGR8, A8L8, and DXT IWI members and canonical DB load definitions, including the bounded 2048-square L8 lightmap atlas case, complete 2048-square DXT3/DXT5 mip chains under the shared 8 MiB compressed-member ceiling, compressed 2048x1024 images that expand to 8 MiB of RGBA8, and bounded six-face cubemaps in canonical `+X,-X,+Y,-Y,+Z,-Z` order. Streaming, U/V clamp, and compressed-texture legacy-normal policy bits are accepted because they do not change the payload layout; volume, unknown-layout, malformed, and over-budget inputs still fail closed. The WebGL backend reads external `images/<GfxImage name>.iwi` through canonical FS/IWD ownership when a fastfile load definition intentionally has no pixels. |
| Fastfile framing and zone stream machine | `TEMPORARY WEB SUBSTITUTE` | It accurately models blocks, rewind/high-water behavior, pointer classes, aliases, and bounded streaming. Use it as differential evidence and migrate reusable mechanics toward the Kisak DB loader. |
| Asset registry | `MODIFIED KISAK` / partial plus `TEMPORARY WEB SUBSTITUTE` oracle | All generated families reached across the three startup prerequisites consume the canonical 32-bit asset-entry pool, per-type pools, free chain, normalized hash table, and zone ownership through `DB_AddXAsset`/`DB_LinkXAssetEntry`. The owned chain records 9,637 publications through entry 9,652, with free entries 32,752 -> 23,115. Gate 2 remains a frozen oracle and is not called by the generated path. |
| Retail loader dispatcher | `TEMPORARY WEB SUBSTITUTE` / frozen Gate 2 oracle | `web_retail_fastfile_census.*` remains the orchestration vehicle, but normal startup no longer executes it. The source has an explicit freeze contract and is isolated with its diagnostic dependencies in `kisak_web_gate2_oracle`; canonical DB code must not call it. It remains linked only until canonical DB can reproduce equivalent asset/world evidence. |
| `clipMap_t` asset loading | `MODIFIED KISAK` / canonical reached closure | The dedicated family transcribes the 284-byte `Load_clipMap_t` record, block-4 child order, block-1 zero-fill dynamic client allocations, root insertion/alias handling, bounded ownership, and atomic canonical publication for `col_map_sp`/`col_map_mp`. Synthetic MSVC/Wasm coverage exercises empty and populated child graphs under one-byte traversal budgets. Retail traversal now publishes directly into native `&cm`; real `CM_LoadMap` consumes that owner. Type 12 `com_map` remains correctly distinct and is handled by the canonical ComWorld family. |
| `ComWorld` asset loading | `MODIFIED KISAK` / canonical reached closure | `db_generated_comworld.cpp` owns native root null/inline/shared/prior-alias handling, the canonical body, name and 68-byte primary-light array, per-light `defName` XStrings, checked bounds, and final real-DB publication. Production publishes into the subsystem-owned `&comWorld`, and `com_world_runtime.cpp` supplies the real fastfile lookup/unload owner used after `CM_LoadMap`. Native x86/Wasm fixtures and runtime lookup evidence are exact. The normal Killhouse run publishes asset 704; `web_retail_load_comworld.*` remains frozen Gate 2 evidence only. |
| `GfxLightDef` asset loading | `MODIFIED KISAK` / canonical reached closure plus Gate 2 oracle | `db_generated_light.cpp` now owns normal generated loading: four-byte root cells, null/inline/shared/prior aliases, block-0 16-byte bodies, block-4 names, embedded 8-byte light images, canonical `GfxImage*` dependencies, insertion cells, and final-only real DB publication. Retail startup publishes two LightDefs. The independent `web_retail_load_lightdef.*` path remains frozen Gate 2 evidence for Killhouse asset 705; no rendering, attenuation evaluation, shadow behavior, or primary-light linking is implemented. |
| `XAnimParts` asset loading | `MODIFIED KISAK` / canonical reached closure | `db_generated_xanim.cpp` now follows native `Load_XAnimPartsPtr` / `Load_XAnimParts`: block-0 body allocation, shared/insertion/prior root forms, block-4 name and payload scope, ScriptStrings, notifies, exact array order, low/high-frame index widths, and flexible delta translation/quaternion storage. It publishes through the real DB pool/hash/free chain. The complete prerequisite chain records 1,017 XAnimParts publications. Gate 2 retains independent Killhouse evidence only. |
| `WeaponDef` asset loading | `MODIFIED KISAK` / canonical reached closure | `db_generated_weapon.cpp` owns the canonical 2,168-byte body and native order for 48 XStrings, 38 XModel dependencies, ten FX handles, eight Materials, 48 sound-name cells, 40 ScriptStrings, the 29-entry bounce array, and four accuracy arrays. Root null/shared/insertion/prior forms and final-only publication use real DB ownership. The complete prerequisite chain records 17 WeaponDef publications; Gate 2's Killhouse definitions remain an independent oracle. |
| `StringTable` asset loading | `MODIFIED KISAK` / canonical reached closure | The canonical special-case root semantics are preserved: null, `-1` inline, or ordinary pointer conversion without a block-0 push or `-2` insertion. The 16-byte body, name, row/column value table, XStrings, overflow checks, and final publication use the real type-32 pool. The complete prerequisite chain publishes two StringTables. |
| Sound alias loading and catalog | `MODIFIED KISAK` / partial | The prerequisite dispatcher mirrors the native list/header/component order and owns canonical `snd_alias_list_t`, `snd_alias_t`, `SoundFile`, `LoadedSound`, `SndCurve`, and `SpeakerMap` metadata in the publishing zone. `web_sound_alias_catalog.*` remains a case-insensitive ownership/index seam: it stores pointers to those objects, retains the common-zone owner, collapses native DB name aliases, and uses the indexed canonical `null` sound for native missing-sound fallback. Fastfile alias publication validates curve knot storage and atomically resolves null/malformed pointers to the canonical two-knot default before sound selection; valid DB curve identity and values remain unchanged. It neither copies nor synthesizes sound records. No payload playback or audio runtime behavior is implemented. |
| `LocalizeEntry` asset loading | `MODIFIED KISAK` / canonical reached closure | The normal generated DB path now owns the renderer-free eight-byte ABI, block-0 body, block-4 value/name XStrings, null/inline/shared/prior root forms, insertion cells, real pool/hash/zone ownership, and final-only publication. Owned `code_post_gfx.ff` publishes 1,248 LocalizeEntry assets before continuing naturally through later families. Gate 2 retains independent oracle evidence only. |
| Sound asset loading | `MODIFIED KISAK` / canonical reached closure | The normal generated path owns SndCurve, sound-alias list/entry arrays, streamed SoundFile names, SpeakerMap, LoadedSound payloads and real publication. `DB_PlatformSetLoadedSoundData` passes transient fastfile payloads through `SND_SetData`, matching the native sound-owned copy/resample boundary before stream loading advances. The native dispatcher intentionally performs no work for SndDriverGlobals, which is preserved exactly. |
| Font asset loading | `MODIFIED KISAK` / canonical reached closure | The generated path owns the 24-byte Font body, font-name XString, two canonical MaterialHandle dependencies, bounded Glyph array and final type-19 publication. Retail publishes nine Fonts before advancing to FX. |
| FX asset loading | `MODIFIED KISAK` / canonical reached closure | The generated path owns the 32-byte effect, 252-byte element graph, velocity/visual samples, Material/sound/effect/XModel visuals, trails, root aliases and final type-25 publication. Inline XModel visuals now reuse the canonical generated XModel family. Retail publishes `misc/missing_fx` after its nested Material and GfxImage, then advances to FxImpactTable. |
| FxImpactTable asset loading | `MODIFIED KISAK` / canonical reached closure | `db_generated_fx_impact.cpp` owns the eight-byte root body, fixed 12-entry table, 29 non-flesh plus four flesh FxEffectDef handles per entry, aliases, insertion cells, child-before-parent ordering, and final type-26 publication. Retail publishes `default` before advancing to LightDef. |
| MenuList/Menu asset loading | `MODIFIED KISAK` / canonical reached closure | Dedicated menu and expression units preserve the 12-byte list, 284-byte Menu, 372-byte items, windows, recursive key handlers, type-specific list/edit/multi records, statement/operand trees, Material and sound dependencies, root aliases, insertion cells, Menu-before-MenuList publication, and native item-parent reparenting. The complete prerequisite chain records five MenuList and 141 Menu publications. |
| `XModel` | `MODIFIED KISAK` / canonical reached closure | Dedicated generated units own the canonical 220-byte model, skeleton arrays, XSurface block-7 vertices/block-8 indices, rigid collision trees, Materials, model collision surfaces, bone information, PhysPreset, and physical geometry/brush graph. Root null/shared/insertion/prior forms and dependency-before-parent publication use real DB ownership. The complete prerequisite chain records 37 XModel publications; Gate 2 retains separate broader Killhouse evidence only. |
| `Material` and techniques | `MODIFIED KISAK` / partial | The canonical generated path owns Material and TechniqueSet bodies, names, tables, techniques/passes, shader records/bytecode, texture/image and water dependencies, constants, state bits, pointer aliases, insertion cells, and dependency-ordered final DB publication. WebGL2 registers as IW3's shader-model-3 renderer before zone loading and keeps published non-`sm2/` technique sets self-remapped, matching the native max-graphics choice instead of forcing every material through its SM2 alias. Named leading-comma Material, Image, and TechniqueSet references resolve to the already-published same-name canonical assets only at the renderer-evaluation seam; the 2D frontend resolves the same native reference stubs, allowing weapon reticles to use their shared canonical textures without replacing cgame spread geometry. The portable boundary translates the dominant world and XModel SM3 environment-specular families while unimplemented shader families remain explicit convergence work. Each canonical world submission now logs its encountered native technique, vertex-shader, and pixel-shader variants in descending surface-frequency order and retains both shader identities/hashes in the normalized comparison records for live-browser inspection; the inventory includes portable selection, alpha test, blend state, and native state identity rather than guessing the next family from material names. The complete prerequisite chain records 395 Material and 192 TechniqueSet publications. Gate 2's broader records remain oracle-only. |
| `GfxImage` | `MODIFIED KISAK` / canonical renderer consumption reached; native backend `NATIVE ONLY` | The generated family loads 36-byte image bodies, XString names, texture load definitions/payloads, aliases, insertion cells, and canonical DB publication. At the native `Load_Texture` boundary, Web copies transient block-zero payloads into bounded platform storage before clearing the GPU union; external-pixel images retain their canonical name/metadata and are resolved through FS/IWD at submission. The browser filesystem, IWI decoder, and retained texture input share an 8 MiB per-member ceiling so authored 2048-square DXT3/DXT5 mip chains remain intact; decoded RGBA8 output keeps its independent 16 MiB ceiling. Comma-prefixed serialized reference bodies resolve to the already-published same-name image only at renderer evaluation, preserving DB ownership while allowing world and DObj materials to consume the prerequisite-zone payload. WebGL texture creation and context recovery remain backend-owned and canonical `GfxImage*` identity is retained even when decoding falls back. |
| `GfxWorld` | `MODIFIED KISAK` / canonical reached closure | `db_generated_gfxworld.cpp` follows the native generated closure through names, indices, images, cells/portals, lightmaps/grid, vertices/layers, models, shadow/light regions, DPVS static/dynamic, block-1 runtime allocations, and nested canonical dependencies before final real-DB publication into the renderer-owned `&s_world`. Native x86/Wasm fixtures are byte-for-byte identical. The normal Killhouse run publishes asset 772 with Gate 2-matching structural counts and inflated offset. `WebEngine_BuildGfxWorldSurface` consumes that DB-owned object through a final-publication platform notification and WebGL2 draws surface 6077 without a browser world model. The frozen Gate 2 material label differs from the real DB Material pointer and remains recorded rather than normalized away. `web_retail_load_gfxworld.*` is oracle-only. |
| XModel/model preview scene | `RETIRED` | Removed after Gate 2: no selectable-model UI/state, retained preview geometry, preview camera/projection, preview material bridge, or multi-draw command path remains. Canonical XModel loading/publication and dependencies remain available to `GfxWorld`, `WeaponDef`, FX, and later runtime consumers. |
| Renderer frontend | `MODIFIED KISAK` / textured, lightmapped, shadowed, model-lit gameplay and canonical marks reached | The production Wasm target links the real client/cgame/effects/ragdoll/physics closure against a narrow renderer-frontend platform implementation. `R_RenderScene` validates canonical `refdef_s`, constructs Kisak view/projection matrices, and traverses the canonical DPVS lit, decal, and emissive ranges in native stage order rather than treating `surfaceCountNoDecal` as a contiguous endpoint. It emits 581 material-aware batches with canonical `Material*`, technique identity, base `GfxImage*`, state bits, sampler state, lightmap index, base UV, and lightmap UV. Sun-enabled world draws prefer canonical `TECHNIQUE_LIT_SUN_SHADOW`; caster membership comes directly from `GfxWorldDpvsStatic::surfaceCastsSunShadow`. Static instances sample the canonical `GfxWorld::lightGrid` at their native bounds centers or retain encoded ground lighting. Ordinary and first-person `R_AddDObjToScene` submissions retain the caller's lighting origin, canonical pose evaluation, cpose/view-origin LOD selection delegated to `XModelGetLodForDist`, and rigid/weighted position plus normal skinning. EffectsCore code-mesh, XModel, particle-cloud, and persistent mark submissions remain appended in canonical order and do not enter the model-lighting branch. The mark-fragment seam clips canonical `GfxWorld` world-brush, moving-brush, static-XModel, and rigid animated-DObj receiver triangles against the six native mark planes and retains Material/lightmap/model-context identity; particle marks remain a later family. Unsupported/deformed standalone FX model surfaces, invalid/over-capacity clouds, broader material families, and deferred shader/postprocess features remain compatibility gaps. The minimum 2D callback set retains canonical Material/Font identities and publishes at `R_EndFrame`, including UI-only fullscreen and script-popup frames. |
| WebGL2 backend and context recovery | `WEB PLATFORM IMPLEMENTATION` | Permanent platform boundary. It converts D3D9 NDC depth `[0,1]` to WebGL `[-1,1]`, retains 32-bit indices, uploads canonical external IWI and DB load-definition pixels (including encountered BGR8, A8L8, L8, and X8R8G8B8 images), draws the canonical world sky cubemap from the current view axes/FOV, and mirrors the encountered Killhouse secondary-directional lightmap decode. Dominant `lm_*s0_sm3` world passes retain semantic-8 specular maps, `envMapParms`, and each surface's canonical reflection probe. The GLSL boundary reproduces the recovered D3D9 world arithmetic and the encountered `lp_*s0_sm3` XModel arithmetic: world slope normals or model DXT5nm tangent-space normals, view reflection, specular-alpha probe LOD, RGBM multiplication, authored Fresnel, and model reflection added after `base*vertex*modelLighting*2`. Static model batches split by canonical reflection-probe identity; DObjs choose from the published BSP cell probe list. Live Killhouse retains 545 SM3-specular world batches out of 762 with all 320 world images supported. The first sun-shadow slice owns a 1024-square depth target, native-default 0.25-world-unit near sampling, view-centred texel snapping, world-bound depth extents, alpha-tested canonical world/static-XModel/ordinary-DObj/DynEntity/moving-brush casters, and four-comparison PCF. Encountered `lm_sm_sun_*` receivers add the native `N dot sun * sunDiffuse` term under that real depth map; the far partition and exact per-partition static/entity DPVS culling remain explicit gaps. Encountered `lm_spot_*` world batches preserve their per-surface light identity, both lightmaps, authored L8 falloff, cone, radius, color, and normal basis, and reproduce the native baked-plus-spot diffuse arithmetic; spot-shadow and omni variants remain gaps. The encountered `water_l_sun` pass retains canonical `water_t` frequency inputs and each surface's reflection-probe image, runs the shared IW3 FFT/amplitude conversion, and reproduces the native parallax, multi-scale normal, Fresnel water color, reflection, sun-specular, and fog arithmetic. Model draws additionally upload the native 4x4x4-per-entry RGBA8 model-lighting volume layout. First-person DObj batches preserve native `renderFxFlags & 2`, use `r_znear_depthhack`, and draw in the reserved viewmodel depth range. Static entries are per instance; ordinary DObj and live DynEntity model entries are refreshed from their retained/canonical lighting origins. Dynamic brush batches preserve their canonical materials and directional lightmaps; their `GfxDrawSurf` primary-light indices select techniques from the same immutable world light table, and encountered spot-lit moving surfaces reuse the authored primary lightmap, L8 falloff, cone, and normal-map translation. The frontend advances the canonical five-slot campaign fog state and the backend applies the scripted exponential visibility/color blend to world and model geometry. The resolved RGBA8 scene consumes the exact `R_UpdateColorManipulation` constants and canonical `refdef.blurRadius` before 2D; a second target composites the sharp HUD before the final display-mapping pass. `R_SetColorMappings` remains capability-correct: browser registration reports `deviceSupportsGamma=false`, so `r_gamma` is retained and tested but does not incorrectly darken the composited canvas. Glow and depth-aware DOF remain later work. Canonical cull, depth, color-write, alpha-test/blend, addressing, and filtering states remain per batch. GPU handles stay backend-only; 2D/cubemap textures, water fields, reflection probes, 3D lighting volumes, shadow/post-effect targets, geometry, and instance buffers are recreated after context loss. |
| D3D9 renderer backend | `NATIVE ONLY` | Retain for native builds and use as behavioral reference; do not compile Direct3D objects into Wasm. |
| Shader compatibility | `MODIFIED KISAK` / `WEB PLATFORM IMPLEMENTATION` boundary | Native material/shader contracts should remain canonical; selecting or translating to built-in GLSL belongs at the backend seam. |
| ODE math | `SHARED KISAK` | `src/physics/ode/odemath.cpp` is compiled directly. Expand shared ODE/collision code based on compile inventory and measured needs. |
| Collision and `CM_LoadMap` | `SHARED KISAK` / reached runtime owner | Production compiles actual `cm_load.cpp`; the DB ClipMap singleton pool is `&cm`, and successful retail map DB completion continues through real `CM_LoadMap`. It initializes canonical collision thread data and publishes checksum/in-use state, followed by real `Com_LoadWorld`. An exact MSVC x86/Wasm differential invokes the owner and matches all normalized results. |
| xanim and DObj runtime | `SHARED KISAK` / canonical ordinary and first-person poses consumed | `xanim_init.cpp` and `dobj_init.cpp` own the actual 4,096-entry `XAnimInfo` free ring, `end` notetrack ScriptString, and duplicate-parts ScriptString initialization. Lightweight runtime headers expose the same canonical structs without importing D3D. Production executes them in native `Com_Init` order; exact x86/Wasm evidence covers the ring and handles. The renderer frontend invokes canonical `CG_DObjCalcPose`, derives base-to-current skin matrices, delegates cpose/view-origin LOD thresholds to canonical `XModelGetLodForDist`, and submits rigid and weighted ordinary and first-person XSurfaces while preserving DObj/XModel/Material identity. It also consumes live canonical DynEntity client poses/visibility for rigid XModels and brush models, rather than inventing browser entity ownership. Broader entity/material families, skinned FX models, and campaign coverage remain future compatibility work. |
| Script VM and `Scr_Init` | `SHARED KISAK` / reached game closure | Actual variable-range setup, `AllocValue`, `Scr_Init`, `Scr_VM_Init`, and `Scr_Settings` run in production. Canonical VM/compiler/anim public state layouts were moved to renderer-free headers, not replaced. Retail execution resolves RawFiles/scripts through canonical DB/FS, compiles the game scripts, and completes `G_InitGame` and `G_LoadLevel` with native script-string and VM ownership. A pointer-range portability correction preserves the native eval-stack/main-stack distinction on Wasm. |
| Client and `CL_Init` | `MODIFIED KISAK` / reached lifecycle owner | `cl_initialization.cpp` owns the canonical `CL_Init` body and client globals, and a Win32 x86/Wasm differential runs its normalized contract identically. Production enters full `CL_Init` after canonical filesystem initialization and later completes `CL_InitCGame`. No browser-owned client state substitutes for it. |
| cgame and `CG_Init` | `SHARED KISAK` / playable renderer milestone proved | The linked cgame closure executes through real `CL_InitCGame -> CG_Init` for the local SP client. Once active, the browser scheduler supplies only elapsed time while canonical `SV_Frame -> CL_RunOncePerClientFrame -> CL_Frame -> SCR_UpdateScreen` owns state advancement, weapon pose, HUD construction, and view construction. `Q_random` maps wider platform `RAND_MAX` values back to COD's native 15-bit buckets before recoil consumes them; this prevents Wasm libc values from producing six-figure weapon angular velocities. Chrome records the successful material/lightmap world draw, static XModels, posed weapon/viewhands, canonical 2D HUD/font stream, and bounded hip/ADS attack recoil for `maps/killhouse.d3dbsp`. Gate 2 is not invoked. |
| game and `SV_Init` | `MODIFIED KISAK` / reached live game-frame closure | Shared owners preserve `SV_Init`, `SV_Map_f`, map-command registration, `SV_SpawnServer`, and `SV_LoadLevelAssets`. The retail path continues through real `CM_LoadMap`, `Com_LoadWorld`, save initialization, `SV_InitGameProgs`, `SV_InitGameVM`, `G_InitGame`, five canonical settle frames, direct-connect/client-enter-world, `G_LoadLevel`, and ongoing `SV_Frame` calls. Those frames advance the scripted Killhouse start mover and publish snapshots before cgame constructs each view. Entity, level, server, and script state remain canonical Kisak owners. Because the browser Worker cannot create the native server-demo save worker, the existing `SV_SaveHistory` unit runs synchronously when that platform capability reports unavailable; history buffers are written/freed before reuse and native worker/event behavior is unchanged. |
| Input | `MODIFIED KISAK` / movement, look, fire, reload, melee, and weapon-cycle gameplay proved plus `WEB PLATFORM IMPLEMENTATION` | The main-thread launcher maps DOM keyboard, mouse buttons, wheel, and pointer-lock deltas into a bounded Worker queue. Wasm drains it through canonical `CL_KeyEvent` and `CL_MouseEvent`; native bindings, `CG_DrawActiveFrame -> CL_Input`, usercmd creation, `SV_Frame` consumption, prediction, `Pmove`, and weapon state remain authoritative. Fresh browser profiles receive WASD/Space/Shift/Mouse defaults plus `r -> +reload`, `v -> +melee`, wheel-up -> `weapnext`, and wheel-down -> `weapprev` only when those keys are unbound. The browser pump refreshes native `com_frameTime` and accumulates real Worker elapsed time before advancing gameplay at no more than 125 Hz, so faster-than-vsync callbacks neither manufacture server milliseconds nor distort snapped movement. SP `ClientThink_real` rounds `g_gravity` without reinterpreting a platform-dependent `long double`, preserving the canonical 800-unit player gravity on Wasm. `PM_GroundTrace` retains its canonical primary probe; its bounded sub-unit Killhouse support fallback settles the hull onto the contact before publishing grounded state, preventing hovering and slope-projected upward flight. Chrome proves grounded spawn/incline traversal, gravity-driven falls and stable landings, zero velocity and position drift after release, camera rotation, player-origin movement, weapon switching, ammo consumption, reload clip-out/clip-in, and canonical attack/melee bindings crossing the browser boundary with `keyCatchers=0`. Cursor visibility is marshalled back to the DOM host instead of accessing `OffscreenCanvas.style`; gamepad collection remains future work. |
| Audio | `MODIFIED KISAK` / gameplay LoadedSound and stream bridge plus `WEB PLATFORM IMPLEMENTATION` | Canonical `SND_InitDriver -> renderer registration -> SND_Init` ordering runs in the Worker and keeps alias selection, channel state, attenuation, playback IDs, sound-owned LoadedSound PCM, streamed WAV/MP3 decoding, four-chunk refill ownership, channel-volume fades, and master mute in Kisak. The KISAK_WEB OpenAL-compatible proxy mirrors driver-visible source/buffer/queue state and sends bounded typed PCM commands to main-thread `WebAudioDriver`, which schedules gapless stream chunks and owns AudioContext/AudioBuffer/AudioNode resources plus gesture resume. Queue pause/resume retains its intra-buffer offset, stopped streams explicitly restart after a throttled-tab underrun is refilled, and device/map teardown retires queued nodes and buffers before reuse. Stereo 2D sources, including first-person weapon fire, bypass `PannerNode` just as native OpenAL does; only canonically positioned 3D sources enter the spatial graph, avoiding destructive stereo downmix/phase cancellation. Mono 3D sample and stream gain follows the native Miles half-per-output-level convention, preventing material impacts from overpowering the first-person and ambient mix. Release Chrome proves loaded fire/dry-fire/reload/switch/material/movement/UI playback; streamed Killhouse bed/surround ambience, emitters, Gaz dialogue, and flyovers; listener-relative 3D positions; master mute/restore; and fresh audio generations over repeated map loads without stream diagnostics. Reverb, per-speaker channel maps, EQ filters, and full OpenAL parity remain unsupported. |
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
| Native engine systems not compiled | Decrease sharply after the GfxWorld proof | High but falling: qcommon, the full Killhouse generated-family order, filesystem, collision, script VM, xanim/DObj, server/game, client/cgame, effects, ragdoll, physics, sound, and canonical renderer-dvar owners now compile and execute through gameplay. Static XModels, ordinary plus first-person dynamic DObjs, live DynEntity XModels/brushes, code-mesh/shell FX, and clipped world, static-model, moving-brush, and animated-DObj impact marks are submitted through the canonical frontend; remaining renderer gaps are broader material/entity families, particle marks, campaign variance, and deliberately deferred shader/postprocessing features, not a parallel browser world or camera owner. |
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

## Killhouse renderer-parity update (2026-08-22)

The frontend/backend seam now has an opt-in normalized comparison capture. It
records draw order and surface ranges, material and technique names, base and
both lightmap image names, raw and decoded state bits, alpha/blend/depth/cull,
and sampler policy. The retained WebGL command is independently normalized and
compared with the frontend command; neither capture contains GPU handles or
object addresses. A Release Chrome Killhouse run records 581 intended and 581
actual draws, 8,475 surfaces, 538 lightmapped draws, 123 alpha-tested draws,
148 blended draws, and two image-support divergences.

The lighting fix now follows the actual D3D9 program selected by Killhouse.
`R_DrawBspDrawSurfsLit` supplies custom sampler flag `0x04`, so
`lm_r0c0_sm2.hlsl` samples the secondary atlas only. Its two packed lobes use
UV transforms `(u, v * 0.5)` and `(u, v * 0.5 + 0.5)`; their alpha channels
are decoded with the native constants and reciprocal-square-root weight before
the result multiplies `base texture * vertex color`. The former browser-only
`primary + first secondary lobe` approximation and `1.5` clamp are removed.
The primary L8 atlas remains canonical DB data but is neither retained nor
sampled for this pass. No D3D9 sRGB sampler/write state is selected here, so
RGBA8 samples remain direct normalized values; native `r_gamma` is a later
display-ramp concern, not a lightmap decode step. A tested V-coordinate flip
was rejected because it increased black samples and visibly corrupted the
scene.

Canonical SM2 remapping and leading-comma material-family selection reduce
world fallback from 70 draws/443 surfaces in the former 8,064-surface command
to two draws/nine surfaces in the complete canonical ranges. Static XModel
fallback falls from 304 of 359 batches to 29; the remainder is image support,
principally one unsupported format, one malformed load definition, and images
deferred by the bounded 256 MiB static texture-recovery budget. At the time of
this capture, sky/fog, display gamma/vision processing, water-specific shading,
light-grid/model lighting, and the remaining `,crater_blacktop` image were
explicit renderer gaps. Later work closes the sky, fog, and model-lighting
gaps, including lighting for live DynEntity XModels. The corrected world pass keeps
the draw count and three texture fetches
per pixel unchanged versus the previous approximation while avoiding retention
or upload of 28 MiB of expanded primary-lightmap RGBA8 data.

## Native material-family parity update (2026-08-23)

The WebGL2 backend now distinguishes portable execution techniques from the
unchanged canonical Material/TechniqueSet identity. Disassembly of the retail
D3D9 pixel programs established the exact equations used here; no browser-only
material object model was added. The `lm_r0c0n0_sm2` and `lm_t0c0n0_sm2`
families decode both the secondary-lightmap direction and DXT5nm AG surface
normal as slopes, normalize each implicit `(x,y,1)` vector, and combine the two
lightmap lobes before multiplying base and vertex color. Leading-comma
`wc_l_*n0*` aliases use the same backend equivalent while retaining their
native names and state bits. This covers 413 draws and 4,819 Killhouse
surfaces; the existing non-normal equation covers another 125 draws and 3,448
surfaces.

The encountered `mul.hlsl` family now emits the native white-to-texture
control color before fixed-function `ZERO/SRC_COLOR` blending, including the
vertex-alpha control. The encountered `vertcol_simple_add_fog.hlsl` family
applies canonical fog and premultiplies RGB by texture/vertex alpha before its
additive blend. These paths cover 22 draws/58 surfaces and 14 draws/74 surfaces
respectively. Only five ordinary base-texture draws/67 surfaces and the two
known `wc/com_crater_blacktop` fallback draws remain outside those material
families in the captured world command.

Focused native tests fix the numerical slope-space decode and material-family
selection. The normalized frontend/backend comparison also carries normal-map
identity and usage, plus a technique-specific composition description. Release
Chrome validates all 581 intended/retained draws, 8,475 surfaces, 540
directional-lightmapped draws, 123 alpha-tested draws, and 148 blended draws,
with zero fallback or divergent draws. The supplied Steam
screenshots are local visual references only and are not repository assets.

## Canonical near sun-shadow convergence update (2026-08-23)

The frontend now selects native technique slot 9 when `sm_enable` and the
published directional `GfxWorld::sunLight` allow shadows. It retains both
lightmap samplers requested by `lm_sm_sun_*`, the canonical sun direction and
diffuse color, world bounds, and each surface's exact
`GfxWorldDpvsStatic::surfaceCastsSunShadow` bit. The portable command remains a
description of Kisak renderer intent; no WebGL handle or browser light object
crosses the frontend boundary.

The WebGL backend owns a real 1024 by 1024 depth texture and framebuffer. Its
near projection follows the native default 0.25-world-unit sample size, sun
axis construction, view-centred texel snapping, and world-bound depth range.
Canonical caster-bit world batches and material-qualified model batches enter
the depth pass, with authored alpha-test state and base opacity preserved.
Receivers perform the four manual
depth comparisons seen in the retail `lm_sm_sun_*` shader and add
`N dot sun * sunDiffuse * visibility` to the existing baked directional
light. Detailed receivers use their retained DXT5nm normal for the sun dot
product as well as the established native slope-space lightmap equation.

Focused native coverage verifies slot-9 preference, primary/secondary/normal
retention, and caster-bit batch splitting. The Release Wasm build and existing
Chrome Killhouse tab complete without renderer failures: 581 intended and 581
actual draws match with zero fallbacks or divergences.
Slot 9 now covers 432 lightmapped draws and 7,676 world surfaces, including
385 normal-mapped draws and 4,651 surfaces. This is the first real shadow-map
slice, not full native closure: the far cascade and exact native per-partition
static/entity DPVS caster culling remain to be ported.
Shadow textures/programs are rebuilt through the existing context-recovery
path.

## DObj renderer convergence update (commits `748112cc`, `de695b46`)

The renderer frontend now retains ordinary entity DObjs through the canonical
512-entry scene array and selects each model's LOD by delegating to
`XModelGetLodForDist` with cpose/view origins. This closes the prior
first-person-only boundary without adding browser entities, a browser world,
or a duplicate scene system. Release Chrome records 64 DObjs, 102 models, 147
surfaces, 16,435 vertices, and 33,672 indices in the first dynamic scene, with
visibly posed Gaz. The same run retains `FxCodeMesh` (4 batches/44
vertices/66 indices) and `FxXModel` (5 batches/188 vertices/234 indices).
Canonical xanim/DObj ownership remains shared; only the LOD distance policy is
owned by the narrow renderer compatibility seam. Focused native/Wasm coverage
verifies canonical LOD delegation, distance calculation, invalid-input safety,
and highest-LOD fallback.

Trend delta: shared Kisak runtime ownership improves because ordinary DObj
submission now reaches the same canonical frontend path as the weapon DObj;
browser-only engine substitutes are unchanged, and permanent browser code is
still limited to the renderer backend/platform seam. Native systems not yet
compiled remain the deferred shader/postprocess and broader campaign families,
not entity or world replacements.

## Sky and DynEntity renderer convergence update (2026-08-22)

The frontend now reads live model and brush DynEntities directly from the
canonical `DynEntityClient` pose/visibility arrays. Rigid XModels use canonical
LOD selection and the native per-DynEntity primary-light/light-grid inputs;
brush models transform canonical `GfxBrushModel` surface ranges while retaining
their Material and directional-lightmap identities. These are bounded portable
draw commands at the renderer seam, not duplicate browser game entities. This
restores the scripted firing-range targets, the exit-door brush mover, and
other map-owned dynamic props.

The backend now decodes the canonical six-face sky image from either its DB
load definition or external IWI payload, uploads it as a WebGL cubemap, and
reconstructs view rays from the canonical view axes and field of view. It draws
the sky before world geometry and recreates the cubemap after context loss.
Malformed or over-budget cube payloads fail atomically. Focused native tests
cover cube face ordering, rigid model/brush transforms, source tagging,
directional-lightmap retention, DynEntity model-lighting coordinates, atlas
merging, invalid-input safety, and submission limits.

Static-model base-color retention no longer stops at the former 256 MiB
bootstrap ceiling encountered by Killhouse. The bounded scene allowance is
512 MiB, and a malformed or unsupported DB load definition now falls back to
the same canonical external `images/<name>.iwi` lookup native asset ownership
provides before the batch is downgraded. This targets the 29 static-model
fallback batches recorded in the earlier capture rather than accepting their
orientation-colored substitute as a model-rendering result.

The same scene boundary now advances the web frontend's canonical five-slot
fog state with the byte-wise interpolation used by `R_UpdateFrameFog`. World,
static-model, DObj, DynEntity-model, and brush-model pixels use the scripted
start/density/color and radial exponential visibility; HUD and explicitly
vertex-color FX remain unfogged. Instanced static models also carry their
transformed world position into this calculation rather than their local
XSurface vertex position.

This milestone deliberately does not add guessed ambient or sun terms to the
encountered baked world technique. Remaining lighting parity work is the
native material pipeline still absent from the WebGL backend: non-sun
primary-light technique passes, glow/DOF, shadows, and broader post-processing. Campaign
film color manipulation, capability-gated display gamma, and the encountered
model normal-map path now follow the native renderer.

## Campaign film and display mapping convergence update (2026-08-22)

`R_RenderScene` now carries the active canonical `GfxFilm` payload through the
renderer frontend. It mirrors `R_SetFilmInfo`, including the tweak override and
the `r_desaturation`, `r_contrast`, and `r_brightness` adjustments, then emits
the exact color-bias/tint-base/tint-delta constants calculated by
`R_UpdateColorManipulation`. The WebGL backend resolves 3D into RGBA8 and
applies the native `postfx_color` intensity/desaturation/tint equation before
submitting the retained canonical 2D command. A second RGBA8 target preserves
the native order so HUD colors are not film-tinted.

The final pass also models the `R_CalcGammaRamp` exponent, but applies it only
when the canonical `vidConfig.deviceSupportsGamma` capability is true and
`r_ignorehwgamma` is false. The browser registration deliberately reports the
capability false because a composited canvas cannot install D3D9's display
LUT. A Chrome comparison with `r_gamma 0.8` forced in the shader proved that
ignoring this gate substantially over-darkened Killhouse; the capability-
correct neutral pass matches the native unsupported-device behavior.

The focused native lighting test now covers the color-manipulation constants,
invert and disabled forms, plus the exact display-gamma exponent oracle. The
Release Chrome run reached a live Killhouse frame with canonical film values,
fog, the six-face sky, 49 dynamic brush models, and 22 visible DynEntity
XModels; shader compilation, post-effect target creation, and scene drawing
reported no failures. Glow remains disabled by the active Killhouse vision set
in the captured frame, so no browser-only bloom approximation was introduced.

## Native model-lighting convergence update (2026-08-22)

The model-lighting seam follows the native renderer rather than defining a
browser lighting model. Native `R_AddDObjToScene` retains the caller's lighting
origin. Camera surface expansion allocates box model lighting for DObjs and
sphere model lighting for static models, then `R_CalcModelLighting` delegates
to `R_GetLightingAtPoint`. Static instances use the center of canonical
`GfxStaticModelInst::mins/maxs` or their packed ground-lighting sample. Dynamic
DObjs use the retained submission origin. Directional primary lights are
accepted without an influence trace; other primary types preserve the native
`Com_CanPrimaryLightAffectPoint` decision at the frontend boundary.

The portable evaluator consumes the published `GfxWorld::lightGrid` directly.
It preserves the native 32-unit XY and 64-unit Z cell transform, RLE row
selection, eight-corner trilinear weights, fixed-point merging of duplicate
color indices, and the corner-visibility trace through `CM_BoxSightTrace`.
Its output is the canonical 56 RGB direction samples plus primary visibility,
not a browser probe or a Killhouse-specific approximation. Publication is
atomic: a complete static or dynamic lighting atlas is submitted, or model
lighting stays disabled for that command.

At the WebGL platform boundary, every entry is packed into the native 4x4x4
RGBA8 layout in a 256-wide 3D texture. Coordinates use the native entry-center
and scale transforms. Rigid and weighted XSurface normals follow the same
canonical bone transforms and blend weights as positions. The encountered
`lp_t0c0_sm2` and `lp_t0c0n0_sm2` base programs cube-project the normalized
geometric normal, sample the volume, and evaluate `base * vertex * lighting *
2`. Static instances carry per-instance lighting coordinates; the dynamic
volume is rebuilt from the current DObj submissions each frame. The backend
owns all 3D texture handles and recreates them after context loss.

Focused native tests cover light-grid RLE traversal and eight-corner blending,
exact volume packing and coordinates, shader composition, static ground-light
publication, normal retention, and bounded surface copies. A Release Chrome
Killhouse run verifies canonical world lightmaps alongside 12,188 lit static
instances and 64 lit DObjs, including the weapon/viewmodel, with lighting that
changes while moving between dark and bright cells. Keyboard, pointer-lock
mouse, ADS, firing, compass, and HUD continue through the existing real
client/cgame input and 2D paths.

The subsequent material refinement carries packed tangent and binormal sign
from GfxPackedVertex through rigid, weighted, instanced, and DynEntity model
draws. Semantic-5 DXT5 normal images retain only after all base-color images,
preserving base coverage under the bounded recovery allowance. The WebGL
`n0` branch decodes tangent X from alpha, tangent Y from green, reconstructs
positive Z, and perturbs the same normal used for the model-lighting-volume
lookup. COD4 IWI legacy-normal metadata is accepted as native layout metadata
rather than rejected as an unknown payload flag.

The final Release Chrome Killhouse capture retains 337 of 340 static-model
images and 61 of 64 first-frame DObj images while preserving the existing
three static base-material fallbacks. The same existing `127.0.0.1:8000` tab
reaches the first cgame-driven frame with 238 static model types, 12,188
instances, 64 DObjs, 49 dynamic brush models, and 22 visible DynEntity
XModels. Shader compilation, post-effect target creation, and scene drawing
report no failures. Draw topology and batch counts are unchanged; only `n0`
model fragments pay the additional normal-texture sample and tangent-basis
reconstruction.

This milestone still does not add non-sun primary-light technique passes, shadows,
reflections, SSAO, bloom, or other post-processing. Those remain
material-technique or later renderer work; the canonical light-grid data flow,
normal-mapped model lookup, and base model-lighting contract are no longer
gaps.

## Gameplay FX, materials, and audio convergence update (2026-08-22)

EffectsCore now owns the complete demonstrated firing presentation. The
frontend calls `FX_GenerateMarkVertsForWorld` in native ordering, implements
the canonical mark-mesh reserve/write/draw callbacks, and appends those spans
beside existing code-mesh and rigid-XModel FX. The mark-fragment boundary is a
bounded port of native `r_marks.cpp`: it walks published `GfxWorld` receivers,
filters canonical surface/material flags, clips triangles against all six mark
planes, and interpolates native lightmap coordinates and normals. World
traversal is confined to the world brush-model range, so moving brush surfaces
are not treated as static BSP geometry. Native `R_MarkFragments_AddBModel`
collisions transform the planes, direction, origin, and texture axis into
pose-local space and publish the canonical entity-brush context. Static
XModels use the canonical instance bounds, LOD-zero surfaces, placement
transform, material filter, probe/light indices, and world-model surface
context. Rigid animated DObj receivers use the same LOD-zero surface and
hidden-part rules as native: each rigid vertex list is clipped in its bone's
base-pose space, then published with the entity/model/bone context that lets
EffectsCore apply the current pose every frame. Deformed surfaces remain
skipped exactly as in the native mark path. This does not introduce a browser
impact system or a parallel world/model representation.

Focused Wasm-native coverage exercises world receiver clipping and material
rejection, a pose-local moving-brush mark, a transformed static-XModel mark,
and a translated DObj mark whose stored vertices and callback origin remain
bone-local. All preserve their canonical EffectsCore contexts. Existing BSP
impact marks remain visually intact in the Release Chrome runtime.

Release Chrome MP5 fire proves the ordered runtime result: smoke uses
`,gfx_smk_white_atlas`, the ejected shell uses `fx_pistol_shell_blur`, and a
persistent `FxMarkMesh` draws `wc/gfx_impact_metal03` (other shots verified
metal02, concrete03, and plastic/wood/plaster receivers). The same canonical
event consumes ammo and produces material-specific impact aliases. Normal
`XModelGetLodForDist == -1` shell culls are no longer mislabeled as unknown
shaders, eliminating the former per-frame warning flood.

The retained FX loader still exposes comma-prefixed Material dependency stubs
that native `DB_LinkXAssetEntry` would resolve before EffectsCore observes
them. Code-mesh submission now performs only that same-name lookup at the
material-evaluation boundary, leaving DB ownership and the serialized stub
records unchanged. Release Chrome MP5 firing resolves `gfx_smk_white_atlas`,
`gfx_glow_org`, `gfx_explosion_flash_atlas`, and `gfx_muzflash_m16` to their
real 2D images; the former untextured white muzzle polygons are gone while the
canonical Killhouse cgame/world submission remains unchanged.

The encountered translucent floodlight artifacts were not new blend-state
approximations: `floodlight_beam` fell back because its retail bitmap is BGR8.
The portable decoder now accepts exact 2D BGR8 mip chains in native
smallest-to-largest order and X8R8G8B8 load definitions, expands them to opaque
RGBA8, and leaves all existing size, flag, dimension, and atomic-failure bounds
intact. Killhouse now renders shaped light volumes instead of flat rectangles.

The canonical generated sound loader owns each inline/shared LoadedSound byte
payload and fixes the native data pointers only after the complete record is
available. The existing OpenAL/Web Audio boundary receives that real PCM; the
live MP5 selection reports a 164,096-byte, 44.1 kHz, stereo 16-bit sample and
valid playback IDs. Browser evidence includes `weap_mp5_fire_plr`,
`weap_mp5_clipout_plr`, `weap_mp5_clipin_plr`, material-specific bullet
impacts, ambient sounds, and UI transitions. Streaming remains deferred; the
playable offline firing slice no longer depends on a temporary FX or audio
owner.

## DynEntity material and physics-reference convergence update (2026-08-23)

Rigid DynEntity XModel submission now resolves comma-prefixed serialized
Material references at the renderer evaluation boundary and follows the
canonical remapped TechniqueSet, matching the existing DObj and static-XModel
paths. The database continues to own both the reference records and published
assets; the frontend does not introduce replacement prop geometry or a
browser-only material table.

DynEntity impact creation similarly resolves the published PhysPreset by its
canonical asset name, with the owning XModel preset as the native semantic
fallback when a later-zone definition still carries an unusable reference
body. Comma-prefixed serialized dependency names are normalized before lookup,
so `,bucket_metal` and `,bottle_plastic` resolve the authored paint-can, pail,
bottle, plastic-bucket, and coffee-mug physics. Only finite, positive-mass
presets reach ODE. An unresolved browser record is rejected with a warning
instead of reaching the native mass assertion; native behavior remains
unchanged.

The frontend also restores the native `R_RenderScene` physics stage:
`FX_RunPhysics` and `DynEntCl_ProcessEntities` run before their current poses
are collected for scene submission. Previously impacts accumulated forces on
live ODE bodies, but the browser path never advanced either physics world, so
their velocity and rendered pose remained unchanged. Release Chrome Killhouse
verification shows the paint can and fire extinguisher with their authored
textures, an extinguisher moving from `(3402, -817.5, 4)` to approximately
`(3367, -783, 6.6)` under G36C impacts, and the paint can and coffee mug moving
with their resolved canonical presets. Frame pumping continues without the
former `totalMass > 0` abort.

## Canonical water material convergence update (2026-08-23)

Killhouse's three `wc/kh_water_mud` surfaces now select an explicit portable
`WaterLitSun` command from the real `water_l_sun.hlsl` pass instead of treating
`case64blue` as an ordinary base texture. The callback-scoped command carries
the canonical semantic-11 `water_t`, its sampler, the surface reflection-probe
index and `GfxImage`, plus the authored `envMapParms` and `waterColor`
constants. The backend copies those DB-owned inputs before returning; it does
not retain transient fastfile pointers or create a browser water asset model.

The shared `r_water.cpp` frequency, FFT, amplitude, and L8 conversion now has a
platform-neutral output function. Native D3D texture upload remains excluded
from Wasm, while the browser invokes the same math using canonical scene time.
At the WebGL boundary the recovered native SM2 arithmetic performs half-scale
height parallax, weighted 1x/3.7x/13.69x samples, finite-difference normal
reconstruction, probe reflection, authored Fresnel interpolation, 64-power sun
specular, vertex alpha, and the existing canonical campaign fog blend. Water
and reflection textures are backend handles and are rebuilt after context
loss.

Focused Wasm-native tests cover canonical water command selection/input
propagation, the shared zero-spectrum L8 conversion, invalid-grid rejection,
and renderer comparison retention. A Release Chrome Killhouse run reports 581
intended and retained draws; the water record covers surfaces 8,444-8,446,
selects `water-lit-sun` on both sides with zero divergence fields, and reaches
the first cgame frame without WebGL allocation, cubemap, shader, or draw
failure. Remaining command divergences are unrelated unsupported image
fallbacks.

## Canonical renderer image-reference update (2026-08-23)

Serialized comma-prefixed `GfxImage` reference bodies now cross an explicit
renderer-evaluation seam analogous to the existing Material reference seam.
The frontend strips only the reference marker, asks the canonical DB for the
already-published same-name type-6 asset, validates the returned name, and
passes that `GfxImage*` into the portable command. Ordinary images and missing
references preserve their original identity; the backend still owns decoding
and GPU objects.

This resolves both `wc/com_crater_blacktop` world draws from
`,crater_blacktop` and the encountered flashbang, SAW, and M67 DObj image
references without special-casing those assets. Focused Wasm-native coverage
checks reference/pass-through behavior. Release Chrome reports 581 intended
and retained world draws, 8,475 surfaces, zero fallback draws, and zero
frontend/backend divergences. The final first-frame DObj command retains all
80 distinct encountered images. Three static-model load-definition/IWI
metadata failures remain separate decoder inputs, not reference-resolution
failures.

## Max-quality Killhouse image-retention update (2026-08-23)

The browser filesystem, portable IWI decoder, and renderer recovery store now
share a bounded 8 MiB compressed-member ceiling. A complete 2048 by 2048
DXT3/DXT5 mip chain occupies about 5.34 MiB, so the former 4 MiB ceiling
discarded the authored `mtl_uaz_van_col`, `mtl_uaz_van_nml`, and
`ch46e_body_damaged_nml` payloads before WebGL upload. The decoded RGBA8 limit
remains independently bounded at 16 MiB. Wasm-native boundary coverage runs
with a 32 MiB test heap so the near-8-MiB input and its decode can coexist. The
browser oversized-member fixture uses the same 8 MiB constant and proves that
an 8 MiB plus one byte declaration is rejected before any archive read.

A Release build loaded through the existing Chrome session now reports all
341 distinct static-model images retained, while the canonical world comparison
remains 581 intended/actual draws, 8,475 surfaces, zero fallback draws, and
zero divergent draws. The user's legally owned Steam max-graphics Killhouse
captures are local visual references only and are not repository assets. They
set the presentation target beyond structural parity: authored surface detail,
directional contrast, sun shafts/hotspots, fog, shadows, reflection/wetness,
weapon/viewmodel lighting, and final display mapping must converge without
introducing a browser-specific scene representation.

## Canonical static-XModel material-reference update (2026-08-23)

Static XModel command construction now accepts the same narrow Material
resolver used by the DObj and FX-model paths. Resolution occurs only while the
renderer evaluates each canonical `model.materialHandles` entry: a successful
same-name DB lookup supplies the published Material, while null resolution
preserves the serialized pointer. No canonical model, surface, or DB record is
mutated.

This resolves the Killhouse `weapon_m60_mg_setup` reference from
`,mc/mtl_weapon_saw` to the published `mc/mtl_weapon_saw` material and its
canonical image/state/technique data. Focused Wasm-native coverage proves the
resolver seam. Release Chrome now retains 359 of 359 static batches with zero
fallbacks and 341 of 341 images; the world comparison remains 581 intended and
actual draws, 8,475 surfaces, zero fallback draws, and zero divergent draws,
with no WebGL, shader, texture, or framebuffer errors.

## Canonical model sun-shadow caster update (2026-08-23)

The near sun-shadow depth pass now consumes model caster intent carried by the
existing portable draw commands. Static XModel and live DynEntity surfaces use
the native Material `gameFlags & 0x40` shadow qualification; ordinary DObjs
also preserve native sun-shadow DPVS `renderFxFlagsCull=1` admission. Moving
brush batches retain the canonical `surfaceCastsSunShadow` bits already used
by world geometry. First-person depth-hacked batches and EffectsCore
vertex-color families remain excluded from the world shadow pass.

The WebGL-owned shadow program now applies the same static-instance axis,
origin, and scale attributes used by the camera pass before the established
view-centred near projection. Dynamic commands are already CPU-expanded into
world space and enter the same depth target directly. Canonical opacity images
and alpha-test modes remain active for both model families. No model or shadow
object is introduced outside the renderer backend.

Focused Wasm-native tests cover static and DynEntity material qualification and
the DObj render-flag boundary. Release Chrome reports 309 static and 136 first
dynamic-command shadow-caster batches, 359/359 static batches with 341/341
images, and an exact 581/581 world comparison over 8,475 surfaces. A three
second live sample advanced 180 browser frames (59.86 fps), with no WebGL,
shader, texture, or framebuffer errors. The remaining shadow work is the far
partition and exact per-instance/per-partition native DPVS culling.

## Canonical A8L8 image update (2026-08-23)

The bounded image decoder now accepts COD4 IWI format 3 and canonical
`D3DFMT_A8L8` load definitions. It preserves the native serialized
luminance-then-alpha byte order, expands each texel to `L,L,L,A`, follows the
IWI smallest-to-largest mip order for external members, and follows the DB
load-definition largest-to-smallest order for retained payloads. Existing
layout, size, dimension, and failure-atomicity boundaries remain unchanged.

Wasm-native coverage exercises both orders and alpha preservation. After a
Release rebuild, the existing Chrome Killhouse session opens the pause/UI path
without either `gradient_top` or `gradient_bottom` falling back, and the boot
log reports zero backend image fallbacks. The canonical world comparison
remains 581 intended and actual draws over 8,475 surfaces with zero fallback
draws and zero divergent draws.

## Canonical non-sun primary-light technique update (2026-08-23)

The world command now retains the canonical `ComPrimaryLight` values,
`GfxLightDef` attenuation image, sampler state, and each `GfxDrawSurf`
primary-light identity at the renderer boundary. Native `R_SetupMaterial`
selects the lit technique directly from that per-surface identity, so it now
participates in portable world batching rather than being collapsed into a
shared base batch. Killhouse consequently moves from 581 to 585 canonical
material/lightmap/light-identity batches over the same 8,475 surfaces.

The bounded external IWI decoder now also accepts COD4 format 4 (`L8`),
preserves the native smallest-to-largest mip order, and expands each texel to
opaque `L,L,L`. In the owned Killhouse runtime, this resolves the authored
32-by-1 `falloff_linear` image used by every encountered local light. Chrome
reports 24 canonical primary lights: sun index 1, 22 spot lights, no omni
lights, and 178 world batches/380 surfaces assigned to them. Of those, 143
batches/335 surfaces select a real type-10 material technique. The encountered
shader family is `lm_spot_r0c0[_n0]_sm2` plus one
`lm_spot_t0c0n0_sm2` material; two decal materials select the native
`vertcol_mul_fog` type-10 technique. The remaining 35 batches/45 decal or
glass surfaces have no type-10 technique in their canonical remapped set;
the portable command now marks those material groups as
`NativeTechniqueUnavailable`, and the backend follows native
`R_SetupMaterial` failure by skipping them instead of inventing fallback
geometry. All 246 encountered world images decode successfully.

The WebGL translation follows the authored `lm_spot` D3D9 token arithmetic:
it retains both lightmap samplers, decodes the existing two-lobe baked term,
then adds `primary visibility * falloff * saturated spot cone * saturated N dot L *
light diffuse`. Normal variants reconstruct the authored slope-space normal
through the world tangent basis. The exact 32-sample L8 falloff and canonical
light radius, color, direction, cone cosines, and exponent drive the shader;
no map-specific constants or browser-only light model are introduced. Omni
lights and spot-shadow variants remain unencountered/unimplemented.

The backend activates this translation for 132 encountered `lm_spot` batches;
the remainder of the 143 type-10 batches are native multiply passes or do not
carry the complete lightmap/image inputs required by this shader family.
The local-light inventory consequently reports 35 native-skip batches/45
surfaces and zero generic fallback batches.

Focused native image and world-scene tests, the Release build, the 17-test
browser smoke tier, and the non-overlap remainder tier (41 passed, 1 skipped)
pass. Chrome compiles and submits the translated shader without WebGL,
framebuffer, or draw errors. At this renderer milestone the browser home path
was still read-only, so Killhouse's start-level save failed and the valid
same-camera visual capture remained blocked pending the writable-home update
below.

## Browser writable home-path update (2026-08-23)

Canonical filesystem writes now use a separate browser-owned home overlay;
validated installation files and their read-only OPFS access handles remain
immutable. The Worker provides synchronous-looking directory creation,
truncate/append open, bounded read/write/seek, remove, and rename operations to
the existing `FS_*` APIs. Closed home files are snapshotted to a distinct
`kisakcod-web/home` OPFS tree and restored before the next engine mount. A
single file is capped at 64 MiB and the restored/live tree at 128 MiB.

The focused browser filesystem test writes and renames a file through
`FS_FOpenFileWrite`, `FS_Write`, `FS_Rename`, and `FS_FOpenFileRead`, then
reloads the Worker and verifies the same bytes after OPFS restoration. In the
owned Chrome Killhouse run, `G_WriteGame 'killhouse' 'Start Level Save'` now
completes without the former `WriteSaveToDevice`/save-error messages. The live
canvas remains black behind the canonical cursor, so valid same-camera visual
comparison is still blocked by a later render/UI lifecycle issue rather than
filesystem write failure.

## Canonical resolved-scene blur update (2026-08-23)

`R_RenderScene` now carries the canonical `refdef.blurRadius` through the
portable view command. The WebGL post-effect pass scales that 640x480-authored
radius to the active scene target and applies a bounded Gaussian-like disk
kernel to the resolved 3D color before film manipulation and 2D composition.
The HUD and menu remain sharp because the final display-mapping pass receives
a zero blur radius. Invalid negative or non-finite radii fail at the existing
view-command validation boundary, and the new uniform is recovered with the
rest of the backend-owned pipeline after context loss.

The Release build passes its runtime-prefix check. In the existing Chrome
Killhouse session, opening the pause menu reports an active canonical radius
of `5.214538`; the scene behind the menu is blurred while objective text,
pause controls, and the compass remain sharp. No shader, WebGL, or framebuffer
failure is logged. A resumed three-second sample advances 180 frames in
3,013 ms (59.74 fps). Depth-aware near/far/viewmodel DOF remains distinct
future work rather than being approximated by this full-screen blur path.

## Canonical XModel SM3 environment-specular update (2026-08-23)

Renderer evaluation now resolves leading-comma `MaterialTechniqueSet`
references to their already-published canonical same-name sets. Killhouse
contains 96 such references; before this seam their pass pointers remained
null even though the generated DB had correctly published the target assets.
Resolution mutates only the native `remappedTechniqueSet` field at the same
post-publication renderer boundary used by Kisak's renderer selection.

Static and dynamic XModel commands now retain the canonical `lp_*s0_sm3`
pixel-shader identity, semantic-8 specular image and sampler, semantic-5 normal
image, `envMapParms`, shader flags/hash, and reflection-probe identity. Static
instances are grouped by model plus `GfxStaticModelDrawInst::reflectionProbeIndex`.
Dynamic DObjs traverse the published BSP cell tree and select the nearest probe
from that cell's canonical list, matching `R_CalcReflectionProbeIndex` while
bounding invalid traversal cycles. Cubemap GPU handles
remain owned by the WebGL backend and are shared with the already-retained world
probe texture rather than decoded and uploaded every animation frame.

Disassembly of retail `lp_r0c0s0_sm3.hlsl` and
`lp_r0c0n0s0_sm3.hlsl` established the exact ordering. The normal variant
reconstructs a DXT5nm tangent-space normal from alpha/green, reflects the
view vector, selects probe LOD with `-8*specAlpha+6`, multiplies RGBM probe,
specular RGB, and authored Fresnel, then adds that environment term after
`base*vertex*modelLighting*2`. The geometric-normal variant uses the same
environment term without the normal-map fetch.

The bounded static RGBA8 recovery tier is 800 MiB. The full Killhouse
base/normal/specular set measures about 766 MiB after DXT expansion, so the
ceiling retains all encountered max-graphics static material images while
leaving a measured bound. Base and normal images still retain priority before
the specular tier. Dynamic DObj images use their separate bounded tier and
retain the encountered character specular set. Compressed GPU upload or
visibility-driven static retention remains future memory/performance work,
not a fidelity fallback in the current Killhouse slice.

## Canonical vertex distance-falloff material update (2026-08-23)

The frequency-ordered live Killhouse inventory records 39 world material
variants over 8,475 surfaces. The dominant lightmap, normal, SM3 specular,
alpha-test, additive, and multiply families already select explicit portable
techniques. The next generic family was `vertcol_simple_dfalloff_fog`: three
batches covering 44 surfaces, led by `wc/hdrportal_darken`.

A bounded diagnostic disassembly of the legally supplied retail shader
established the exact `vertcol_simple_fog_df.hlsl` vertex arithmetic. The
frontend now retains vertex-shader name/hash alongside the existing pixel
identity and copies canonical `falloffParms`, `falloffBeginColor`, and
`falloffEndColor` material constants. WebGL computes the saturated horizontal
camera-distance factor from the native TEXCOORD0 pair, blends the two authored
colors into vertex RGB, scales vertex alpha, and then uses the existing
`vertcol_simple_fog.hlsl`-equivalent texture/fog path with the unchanged
`DST_COLOR/SRC_COLOR` blend state. The temporary bytecode dump is not retained
and no retail program bytes are repository data.

The world inventory now includes both native vertex- and pixel-shader names,
and normalized comparison records carry both identities and hashes. Focused
Wasm-native tests cover distance-falloff selection/constants and comparison
capture; the renderer world, comparison, static-XModel, and DObj tests pass
after the Release build.
