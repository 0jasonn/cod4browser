# Web port convergence inventory

Updated 2026-09-05. This page owns system classification; see
[current status](web-status.md) and [the active roadmap](web-roadmap.md) for
evidence and priorities. Earlier chronology is available in
[Git history](../README.md#historical-records).

## Runtime flow

```text
user-owned COD4 files
  -> browser storage / synchronous Worker file primitives
  -> Kisak filesystem and database loaders
  -> XAsset / XModel / Material / GfxWorld
  -> Kisak client, server, game, cgame, script, collision and animation
  -> renderer frontend
  -> portable draw commands
  -> WebGL2 backend
```

Production and diagnostics compile the same runtime sources. Diagnostics add
browser test controls and telemetry through `KISAK_WEB_DIAGNOSTICS`; they no
longer contain a second asset loader, world model, scheduler, or renderer
oracle.

## Shared Kisak systems

| System | Current ownership |
| --- | --- |
| Common startup | Canonical `Dvar_Init` and the strict `Com_Init` prefix run in native order; the host mounts browser storage at the filesystem boundary, then the continuation restores canonical common-command and version-dvar registration before script/server/client startup. |
| Dvars/config | Canonical dvar types, domains, reset/current/latched values, flags, command handlers, key bindings, `Com_WriteConfigToFile`, and profile-relative `config.cfg` own settings. The Worker continuation restores `CL_InitKeyCommands` before filesystem/profile config execution; the browser frame pump only calls the shared `Com_WriteConfiguration` owner. |
| Profiles | Canonical `Com_*PlayerProfile` and `UI_*PlayerProfile` functions own profile identity, active-marker parsing, list/feeder selection, config replay, and deletion. Browser diagnostics only select fixed synthetic entries through those owners. |
| Filesystem | Canonical search paths, IWD/minizip behavior, config/profile calls and synchronous engine-facing operations use Worker file primitives. Recursive profile deletion maps to one durable Worker/OPFS tree-removal primitive. |
| Installation language | The platform import profile derives paths from the validated localization marker; canonical FS/SEH selects localized IWDs and the DB file adapter now uses that selection instead of hardcoded English. French/German synthetic import, DB open and reload pass. Localized retail text/audio remain unqualified. See [evidence](evidence/localized-imports-2026-09-02.md). |
| Database | Canonical XFile stream, allocation blocks, generated loaders, pointer aliases, registry pools, dependency ordering and final publication own runtime assets, including native-compatible leading-comma asset-stub resolution and zone-owned dynamic menu material aliases. Original Win32 `db_load.cpp` independently verifies Material/Image and XModel/PhysPreset loading against adapted Win32/Wasm: publication-time block traces, aliases, partial-zone rollback, resource lifetime and retry match. The test shares canonical registry/PMem and portable device services; native device uploads and arbitrary malformed nested assets remain outside that comparison. |
| World/runtime | Canonical `GfxWorld`, collision, server/game, client/cgame, script VM, XAnim/DObj, effects, ragdoll, physics and sound code are in the browser link closure. |
| Frame order | The browser supplies elapsed time; canonical `Com_ModifyMsec`, `SV_Frame`, client frame work and `SCR_UpdateScreen` advance gameplay. Browser startup selects `com_maxFrameTime=5000`, and the platform pump uses the same long-stall ceiling instead of losing time on every frame longer than 100 ms. Script/developer timescales and opt-in `fixedtime` remain canonical. |
| SP UI | Canonical `CL_StartHunkUsers`, `CL_InitUI`, `UI_Init`, shipped MenuLists, `UI_SetActiveMenu`, `UI_Refresh`, and renderer 2D commands own main/options/profile/load/pause menus. Disconnected browser frames continue through `CL_Frame` and `SCR_UpdateScreen`; UI-only backend frames are valid before any `GfxWorld` exists, so startup presents the shipped main menu rather than a DOM substitute. |
| Text presentation | `r_text.cpp` and `r_text_cmds.cpp` move native glyph lookup, layout, color codes, shadows, subtitle glow, cursor blinking and reveal/decay logic into sources compiled by native and web targets. Canonical fonts/materials remain authoritative. The browser only provides scene/cursor clocks, SP color lookup and quad submission; console bytes pass through the native ring-copy routine. See [text evidence](evidence/text-presentation-2026-09-02.md). |
| Objectives | Native server configstrings, `CG_ParseObjectiveChange`, `objectiveInfo_t`, `objectiveinfo`, canonical localization, timed menu visibility, and renderer text/quads own objective display. Typewriter notifications use `CG_GameMessage` and the canonical console message windows; the filesystem continuation calls `Con_InitChannels` before profiles/client startup so notification and subtitle filters can route their messages. Diagnostics exercise both the objective parser and a freely usable game-message string through the shipped HUD. |
| Fresh-map randomness | Shared `SV_GetMapRandomSeed` serves native and browser spawn paths; optional cheat dvar `sv_mapSeed` defaults to the original clock seed. Game RNG and save/demo restoration remain canonical. |
| Renderer frontend | Kisak world, model, effect and UI state is translated only at the portable draw-command boundary. Native IW3's bounded 65,536 static-model cardinality is preserved across that seam. |
| Display gamma | Canonical `r_gamma` owns the setting; unchanged native ramp/correction functions are shared in `r_gamma.cpp`. The WebGL2 final pass implements the display-device boundary after world effects and all 2D, including menu-only frames. Save/feedback captures remain pre-display. Native/Wasm ramps and shipped slider/world pixels pass; Steam monitor-response parity is unverified. See [evidence](evidence/display-gamma-2026-09-03.md). |
| Display modes/restart | Platform `web_display.cpp` registers canonical resolution/refresh enums and owns canvas/device sizing. Shared client/UI viewport helpers and `CG_InitViewDimensions` consume native `vidConfig_t`. Shared `CL_Vid_Restart_f` keeps the existing process-owned DB executor; browser registration recreates WebGL resources. Shipped Apply, persistence and native in-game save/restart/load pass in Chrome; mission state stays in Kisak. See [evidence](evidence/display-options-2026-09-03.md). |
| Saved-screen effects | Canonical cgame owns flashbang/shellshock lifetime and intensities. Ordered renderer commands carry capture, native timer slots and packed colors into the WebGL2 UI pass; a GPU-only feedback texture is platform-owned and invalidated on resize/context loss. Synthetic pixels and a loaded retail material/composite pass are verified; authored effect fidelity remains unverified. See [evidence](evidence/saved-screen-2026-09-02.md). |
| FX lighting queries | Canonical `FX_CalculatePackedLighting` now receives world light-grid/sun samples through the existing renderer lighting helper, following native average-query selection and quantization. Host-native/Wasm checks pass; original-game visual comparison is pending. This adapted query retires with the portable grid helper when native backend compilation replaces it. See [evidence](evidence/average-lighting-2026-09-02.md). |
| Transient FX lights | `r_dynamiclights_core.h` shares canonical `GfxLight` construction, importance partitioning and projected tangent-sphere scissor calculation between native and web callers. FX owns emission/lifetime; the frontend clears per-scene submissions, applies camera visibility and `r_dlightLimit`, preserves added-light-zero spot identity and near-plane bias, then passes selected `GfxLight` records to WebGL2. The backend uses retained images, canonical material technique/state tables and destination-alpha coverage. A shadow-capable first spot competes with primary lights for the four existing shadow maps and its dynamic pass samples that map. BSP caster ranges use exact shifted light planes without camera visibility; static casters reuse the canonical camera/light receiver mask; dynamic model and brush casters reuse their exact receiver predicate and DObj exclusions. Static/entity child dvars independently gate those families; sun matrices and authored primary-spot lists remain separate. Dynamic DObj/DynEntity spheres and brush boxes require a portal-visible cell and native camera-plane acceptance before assembly, animated DObjs retest their post-pose box before skinning, and FX models use native direct frustum culling. One per-light receiver list globally follows native reverse material/surface-key order and excludes nonreceiver scene kinds. Synthetic pixels and owned Killhouse shadow-toggle/clear/context-recovery checks pass. Equal-key object-ID order and authored-effect fidelity remain open. See [evidence](evidence/transient-lights-2026-09-02.md). |
| Input | Browser events enter canonical key/character/mouse queues, bindings, native paste/edit fields, usercmd creation and movement/weapon code. |
| Audio | Canonical mixer and OpenAL-facing state feed a browser Web Audio device boundary. Both three-band EQ stages cross that boundary; SND retains parameter/entchannel ownership, while the device computes IIR coefficients and owns filter nodes. Canonical room/wet changes now reach the existing OpenAL reverb DSP in an AudioWorklet with shared native presets. Native/Wasm differential and browser PCM routing tests pass; authored campaign transitions, callback cost and Steam comparison remain open. See [EQ evidence](evidence/browser-eq-2026-09-02.md) and [reverb scope/evidence](browser-reverb.md). |
| Save/persistence | Canonical game serialization, shared `ui_savegames.cpp`, feeders, menu scripts, Continue, and deletion own saves and gameplay state. Bounded header metadata supplies the Date column and dynamic description. Save commits request 512x512 JPEGs through native shared resampling; the Worker codec owns only temporary pixels/bytes. C++ checks save identity before persistence and publishes one canonical raw `Material`/`GfxImage`, using existing backend UI texture retention/recovery. Captures wait for a frame of the matching map and cancel on unload; shutdown drains admitted codec jobs before closing files. Native decoding uses reserved `rgp.rawImage`, canonical texture accounting and the existing D3DX dependency. Owned Airplane start-level/save capture, reload and menu display plus native decoder/device checks are verified; complete native menu and Steam comparison remain open. See [latest evidence](evidence/save-startup-native-2026-09-02.md). |

The [retained-renderer milestone](evidence/retained-renderer-49af3948.md) moves
brush submission closer to native separation of immutable `GfxBrushModel`
geometry and current placement. Only GPU-ready mesh/material resources and their
recovery copies live in the backend; canonical entities, poses, visibility and
light animation remain in Kisak. World retirement frees these resources, and
context restoration recreates GPU objects. Optional effects still account for
brush geometry in the logical budget. AC130 exceeded the old 250k/500k
vertex/index capacity; the bounded 500k/1m capacity retains that accounting
and admits the authored scene. One logical command is bounded at 40 MB,
excluding other renderer resources and staging/recovery copies.

Sun-depth range joining preserves original triangle order and membership without
using camera culling. Texture reuse is bounded and resets each frame/pass; it
preserves texture aliases and adds no persistent GL cache. Resource/remap lifetime
and the verified limitations are recorded in [the boundary notes](renderer-retained-resources.md).

## Modified Kisak seams

Placement-only FX/DynEntity XModels now retain deformed surfaces. Native
`R_SkinXModel` classifies these as `SF_XMODEL_RIGID_SKINNED`, and
`R_TessXModelRigidSkinnedDrawSurfList` uploads authored `verts0` with the object
placement; it does not apply DObj bone weights. The browser's existing vertex
expansion already implements that transform, so the erroneous deformed-surface
drop is removed. Native/Wasm tests compare mixed rigid/deformed surfaces under
rotation, scale and translation, preserve material/model identity, and still
reject malformed later surfaces atomically. This is a source-backed boundary
repair, not an owned-effect visual match. Animated DObj skinning is unchanged.

| Seam | Why it differs |
| --- | --- |
| `database/db_file_platform.cpp` | Maps DB file operations to the Worker filesystem. |
| `database/db_generated_image_platform.*` | Retains transient LoadDefs behind opaque `GfxTexture` resource handles at the native upload boundary. Canonical image copies/overrides/defaults own lifetime; completion/unload collects unreferenced handles. The 256 MiB cap rejects admission before copying; live sources are not evicted. |
| `qcommon/common_runtime_commands.cpp` | Keeps the post-mount common-command continuation canonical and separate from browser hosting. |
| `game/g_scr_main.cpp` | Typed void callbacks discard C++ helper return values explicitly. This replaces incompatible function-pointer casts that trapped during actor model precaching in Wasm; script results remain owned by the canonical VM stack. |
| `cgame/cg_servercmds.cpp` | The shared slow-command parser converts decimal scales numerically instead of aliasing `long double` storage as `double`; this preserves normal-speed and scripted scale values in Wasm. |
| `web_client_server_lifecycle.cpp` | Continues synchronous-looking native startup after the main-thread host mounts user files, including canonical version dvars and diagnostic-only calls through real profile/save UI owners. |
| `web_main.cpp` | Admits disconnected-but-running client frames so canonical pre-map UI and config work run in native order. Its live frame jump target uses shared native error cleanup, shutdown and UI owners; the browser owns the non-blocking pump and terminal Worker teardown. |
| `web_canonical_gfxworld.cpp` | Observes final DB publication only; canonical `R_RenderScene` owns world rendering. The obsolete proof submission is removed. |
| `web_renderer_frontend.cpp` | Converts canonical renderer state into backend-neutral commands. |
| `web_system*.cpp` | Supplies browser timing, frame pump, files (including recursive directory deletion), events and thread-context behavior. |

Changes in these seams should preserve native behavior unless the browser
platform makes that behavior impossible.

## Permanent web platform implementations

| Boundary | Implementation |
| --- | --- |
| Host/Worker split | DOM, picker and persistent-storage ownership stay on the main thread; Wasm and OffscreenCanvas run in a Worker. |
| Storage | OPFS/IndexedDB-backed import, validation, atomic replacement and synchronous Worker reads. File System Access is optional. |
| Rendering | Exact encountered `vertcol_mul_fog` pass/argument selection retains canonical Material identity and group order, using the existing colorMap and per-pass state decoder. Unknown multipass families remain unqualified. WebGL2 context, buffers, textures, shaders, render targets, context recovery and presentation. GPU handles stay private to the backend. The shared 2D image pools retain the selected canonical encoded source and use the existing image decoder, including canonical IWI wavelet formats, transiently for initial upload and context restoration; this is recovery data at the platform boundary, not a second asset model or parser. |
| Input host | Pointer lock, keyboard/mouse normalization, trusted paste snapshot/cache transport, focus release and cursor mode. |
| Browser controls | User-initiated document fullscreen and an accessible recovery modal own only browser presentation, checkpoint retry and installation management. Canonical mouse mode determines ordinary visibility; Shift+Escape remains available during renderer-only gameplay. Fullscreen preserves canonical resolution/aspect policy. |
| Audio device | AudioContext policy, buffers/nodes and PCM scheduling. Source offset/completion use `AudioContext.currentTime`; validated generation-tagged feedback replaces the proxy wall clock. Absolute queue ordinals survive unqueue/feedback crossing in flight. One snapshot can be in flight, sampled at 25 ms while the host is available; synchronous Worker work cannot accumulate feedback messages. Startup remains suspended and muted until an intentional canvas gesture resumes it; installation-picker interaction does not unlock sound. |
| Main loop | Non-blocking Emscripten frame pump with a re-entry guard. Loading keepalive uses a bounded Asyncify yield to present native loading UI and cinematic frames inside synchronous-looking map/DB stacks. Worker RPCs wait for those stacks; no pthread requirement. See [measured rationale and cost](cinematic-codec.md). |
| Wasm stack | The web linker reserves 1 MiB, matching native Windows scale for canonical nested map/save loading rather than rewriting shared call chains. |
| Cinematics | Existing `R_Cinematic_*` callers drive the platform FFmpeg Bink decoder, canonical Y/Cr/Cb/A code images, and OpenAL PCM queue. The canonical single-pass `cinematic.hlsl` material uses retained R8 planes with native colour coefficients and filtered chroma; world/brush, static-model, DObj and UI draws bind current planes at draw time. Recovery also works without 2D submission; authored in-world scene fidelity remains unverified. Video follows cumulative device-played PCM, including unqueued buffers; one decoder-owned pending frame feeds audio before presentation. Owned delay/suspension and WebGL recovery pass. Map loading and immutable world/static-model graphics registration run during the intro; camera visibility and dynamic geometry remain frame-owned. Log-panel layout is batched per animation frame to preserve page-thread audio delivery. Native briefing/pregame paint the DB-backed loading bar and hold gameplay until completion/skip. Movie identity and subsequent game actions stay in Kisak. See [codec scope and remaining qualification](cinematic-codec.md). |

## Control classification

| Control family | Classification |
| --- | --- |
| Reached SP main/options/profile/load/pause controls | Shared canonical dvars and commands. The focused menu trace rejects missing references. |
| FPS/developer HUD | `R_BeginFrame` feeds shared `CG_CalculateFPS` once per display frame, restoring `cg_drawFPS`, `cg_drawFPSLabels` and the existing developer font/corner controls. `cg_drawFPS 2` still lacks backend triangle/batch statistics; its FPS sampling works. |
| Material HUD | Shared `r_material_pick.cpp` replaces the web `R_PickMaterial` stub. `cg_drawMaterial 1/2/3` uses native collision masks, material names and surface/content flags; buffer bounds are checked. |
| Scene visibility | Camera masks honor `r_drawWorld`/`r_drawSModels`, including shadow casters. Native submission gates restore `r_drawEntities`/`r_drawBModels`; `r_drawXModels` controls rigid scene models, FX models and DynEntity models, preserving animated DObjs. `r_drawDecals` also gates generated marks. `r_drawWater` freezes animation, matching its native meaning. |
| Sun effects | `r_drawSun` gates sprite/flare submission. `r_sun_from_dvars` calls shared `R_SetSunFromDvars`, enabling its 21 sprite/flare/blind/glare/material/direction controls with native units and conversions. |
| Sun lighting and shadows | The frontend honors both `sm_enable` and `sm_sunEnable` while retaining the canonical sun direction/color when maps are disabled. World and moving-brush sun materials use authored primary-lightmap visibility without a shadow map. Their slope-space normals and SM3 direct specular lobe follow the locally inspected `lm_sun_*` / `lm_hsm_sun_*` shader arithmetic, including `r_specularColorScale`; model DXT5nm decoding remains separate. |
| Pickup sheen | Shared `CG_Item` marks pickup submissions with `GFX_RENDERFX_PICKUP`; weapon names/materials do not determine eligibility. WebGL2 adds a periodic warm sheen to those model fragments after lighting and before fog, preserving texture contrast, depth/alpha and clearing the uniform after each draw. Canonical scene time drives the pulse; held/decorative instances sharing the material are unaffected. The user-provided 2026-09-05 reference clip has peaks at approximately 0.40, 1.43 and 2.43 seconds and near-additive RGB changes; a continuous 1 Hz cosine replaces the guessed 2.5-second pulse/hold and white view-angle blend. This is a reference-calibrated browser presentation effect, not recovered original-game shader arithmetic. Native D3D ignores the metadata bit. |
| `ui_sp_unlock` | Deliberate stock-retail dangling menu reference. Native COD4 1.7 emits the same `openmenuondvar` warning; no guessed browser dvar is registered. |
| D3D9/Win32 renderer controls | Native-only where they configure APIs absent from WebGL2; browser renderer capability controls remain platform-owned and are not aliases pretending to be native dvars. |
| Miles, Bink, and Steam controls | Native DLL integrations remain unavailable. Web Audio and the source-built FFmpeg codec provide device behavior behind existing sound/cinematic APIs; Steam integration remains omitted. |
| Multiplayer and dedicated-server controls | Not compiled into the initial offline SP target. They return only with the documented browser transport/server milestone, not as inert SP dvars. |

The owned Killhouse `@retail-pickup` check reaches the real `CG_Item` path and
captures the same rifle in quiet, rising, bright and quiet-again phases. The sheen is
visible on the rifle alone; its table retains its normal shading. Portable DObj
checks also distinguish instances sharing one material and check pulse timing.

The owned Killhouse `@retail-dvars` check exercises FPS/label and material-HUD
on/off submission, visible world/static/entity/rigid-model counts and shared sun
unit conversions in Chromium. FPS pixels were also inspected. Native SP and
Release production/diagnostic Wasm builds pass, along with the 10 browser smoke
and 60 remainder checks (12 optional checks skipped without retail inputs).
Authored water/decal/sun visual comparisons and detailed backend statistics
remain separate evidence gaps.

## Shared renderer and transport helpers

`r_dynamiclights_core.h` now constructs non-BSP receiver keys for both native
scene/static-model light builders and the browser backend. These keys inherit
the canonical material fields and set only surface type, object ID and depth
hack; camera instance light/probe state no longer changes transient receiver
order. Native object IDs remain native draw-buffer coordinates. The browser
still uses stable frontend order for unresolved equal-key object-ID ties.
Dynamic model, DObj and brush IDs are offsets into native's transient
`frontEndDataOut` surface storage, which the browser does not duplicate.
Synthetic IDs would not establish exact cross-family order; retain them only
through a future shared native surface packer or a smaller demonstrated seam.

`web_renderer_material_lookup.h` shares identical image/constant table searches
across world, static-model, and DObj rendering. Technique selection and water
handling remain local. `worker_transport.mjs` shares request bookkeeping and
filesystem lease acquisition/release, progress validation and bounded request
watchdogs. Production and diagnostic protocols, timeout defaults and recovery
policy remain host-owned. Both hosts enforce the same shared import lease and
exclusive writable-profile lease. Native runtime mount reports actual reads
through a scoped synchronous filesystem observer, preserving canonical loader
ownership while distinguishing active loading from a stalled Worker.

The native `UnitQuatToAxis` wrapper, FX models and particle clouds now use the
same arithmetic in `qcommon_math.h`; each caller retains its existing input
validation. Lighting-atlas sizing uses bounded C++20 `std::bit_ceil`.

Particle-cloud axis policy now lives in shared `gfx_d3d/r_particle_cloud.h`,
called by native `RB_CreateParticleCloud2dAxis` and the browser draw adapter.
The adapter follows native view-X sign, unnormalized endpoint epsilon,
radius-scaled projection and the authored shader's `UV - 0.5` corner offsets.
The native signed threshold and projected-length minimum are preserved,
including its negative-quadrant fallback. An unmodified `8be61213` native
oracle matches 4,096 shared-axis inputs exactly and verifies 12 matrix cases;
native/Wasm tests check 147,456 corners across three camera orientations.
Placement rotation/scale affect centers, not billboard dimensions. Canonical
FX still owns each `GfxParticleCloud`; no new asset representation was added.
Native and web use the same cell-position helper for the 8x8x16 lattice. Web
registration consumes the native three samples per cell in x/y/z order, maps
its wider CRT output into the original 32,768 inclusive buckets, and retains
the 1,024 centers until renderer re-registration. Native keeps its own CRT and
original formula, so this restores lifecycle, sample count, range and cell
arithmetic without claiming identical sequences for the same seed across CRTs.
Broader cloud/thermal material semantics remain compatibility work.

Canonical `GfxWorld::outdoorImage` and `outdoorLookupMatrix` now cross the
immutable world-scene command and are retained atomically with the world. The
generated `$outdoor` image uses the existing canonical image-resource path;
context recovery reconstructs it from the same retained load definition. Cloud
quad expansion stores each original particle center in the vertex normal slot,
which native particle-cloud geometry otherwise leaves unused, so this adds no
vertex bytes or browser-owned engine representation. An exact authored
`particle_cloud_outdoor.hlsl` binding match selects the WebGL pass, which samples
the lookup image with native sampler state `0x62` and keeps the inclusive native
height comparison. A missing lookup skips the recognized outdoor-only cloud
instead of displaying it everywhere. Native/Wasm command and binding checks plus
synthetic GPU pixels cover lookup coordinates, both height outcomes, equality,
colour/alpha behavior and real context restoration. The prior stationary owned
AC130 run submitted no particle cloud, so authored appearance and recovery in an
encountered cloud scene remain unverified.

Soft-particle decoding reads retained canonical Material passes, shader arguments
and slot-1 FloatZ state. Recognized z-feather variants consume a platform-owned
signed-depth prepass with native lit/decal ownership, alpha rejection, near fade,
fog, additive colour, angle falloff and eye offset. No material or FX identity
is duplicated. Native `Vec3UnpackUnitVec` and the code-mesh adapter now share
the exact packed-normal arithmetic in `qcommon_math.h`; code meshes preserve
the normal for authored angle falloff and the tangent for distortion. Native trails leave the unused binormal
sign unwritten, so this adapter does not read it. RGBA8 float-bit
storage, target lifetime and WebGL event registration remain platform work.
The actual loss extension exposed and verifies a repaired OffscreenCanvas event
mapping; prior directly invoked handlers proved reconstruction only.

The recognized distortion pass reads canonical scale, sampler bindings and
technique flags, projects the tangent/normal basis and rejects offsets crossing
foreground depth. A renderer-owned post-lighting colour snapshot supplies native
`RESOLVED_POST_SUN`; it is resolved before emissive draws and recovered/resized
with the other GL targets. Native material-group disabling follows `r_distortion`.
Snapshot submission needs `glFlush` for the observed Chromium MSAA reuse case;
there is no production readback or new engine asset. Unknown distortion variants
and arbitrary material passes remain unimplemented.

The unused asynchronous filesystem adapters and completion exports are retired;
canonical filesystem calls use `web_worker_filesystem` and synchronous Worker
primitives. The unused bootstrap texture/substitution APIs and shader decoder
are also retired. Canonical material rendering, fallback resources and context
recovery remain platform-owned. The uncompiled `dvar_core.cpp` copy is removed;
`dvar.cpp` remains the dvar implementation. These removals add no replacement
engine model. See [test inventory](web-test-inventory.md) for retained coverage.

The single-surface proof is retired. Its relevant finite lightmap-coordinate
check lives in `WebRenderer_BuildWorldSceneCommand`; canonical vertex layout
assertions remain in `gfx_world_types.h`. Proof-only projection/mirror assertions
have no consumer. Surface APIs used by UI, overlays, and actual rendering remain.

Canonical camera visibility now runs through `gfx_d3d/r_dpvs_core.*`: extracted
native view setup/reset, portal clipping/queueing and static AABB traversal use
`GfxWorld`, `GfxViewParms` and `DpvsGlobals`. Native adapters retain worker jobs
and debug drawing; the engine Worker dispatches static cells synchronously.
There are no fake renderer globals or browser culling algorithms.

Every submitted view recomputes camera slot 0. Completion is explicit, including
all-zero results; loaded bytes and prior world/view results are never accepted
as completion. The backend snapshots the mask and repacks camera groups when
visibility or LOD changes, using `canonicalInstanceIndex` across light groups.
Shadow LOD ranges remain intact in the first half of the existing instance
buffer; camera ranges occupy the second half. This adds one instance-capacity
region in CPU/GPU storage plus a byte mask, without another GPU buffer. See
[the static-model visibility record](evidence/static-camera-visibility-2026-08-31.md)
for bounded synthetic execution and production compile evidence, not retail
visual or performance claims.

Static sun shadows now apply a separate light-space visibility mask over that
first-half LOD packing. Canonical `GfxStaticModelInst` AABBs cross the portable
boundary; each near/far matrix overwrites one backend byte per retained instance,
then only contiguous visible runs are submitted. Camera visibility is never
consulted, off-camera casters remain eligible, and spot shadows retain authored
canonical membership. This is the native-shaped partition seam, not a browser
engine model or pause cache. The
[upload follow-up](evidence/static-instance-uploads-ac8b00ca.md) retains the
24-byte AABBs only in CPU source/shadow-packing vectors. The 72-byte GPU record
contains shader and LOD/cull data, and visibility-only updates transfer only its
camera-packed half. Canonical `GfxStaticModelInst`, DPVS, and authored light
membership remain authoritative. See
[the partition evidence](evidence/static-sun-partitions-cc4af645.md).

Authored static spot membership now follows the same packed-mask shape. The
[spot milestone](evidence/static-spot-membership-26b3dc98.md) translates the
selected light's sorted `GfxShadowGeometry::smodelIndex` records once, then all
model surface batches scan the resulting LOD-packed bytes. As in
`R_AddAllStaticModelSurfacesSpotShadow`, instances with authored flag bit 0
are excluded even when present in that candidate list. Visible BSP receivers
determine the competing primary lights, matching native camera submission.
Static material surfaces also require `gameFlags & 0x40` for spot passes,
matching `R_SkinStaticModelsShadowForLod`; a shadow technique alone does not
authorize casting.
Authored BSP shadow membership also retains shadow-only surfaces outside the
lit/decal/emissive camera ranges. A portable range flag prevents those blockers
from entering camera draws, including when PVS marks them visible; index-buffer
coverage and authored per-light membership remain validated.
Camera DPVS, sun
partition bytes, canonical asset identity, and GPU resource ownership remain
separate.

The owned Killhouse booth comparison at eye position `(3568, -929, 65)`
isolated the erroneous diagonal floor shadow to static spotlight casters.
Restoring the material flag check removes it with `sm_enable`, `sm_sunEnable`,
and `sm_spotEnable` all enabled. The sampled floor region retains 98.9% of its
authored-lighting brightness, versus 30.6% before the material correction.
The optional `@retail-shadows` browser check retains this camera and compares
that region with spot maps on/off; synthetic native tests cover both instance
and material exclusions. This establishes the shadow correction, not complete
native/browser pixel parity for every material or highlight.
Validation passed the three native world/static-model/DObj test executables,
the Chromium smoke (10) and remainder (60, with 13 optional skips) tiers,
the owned-data HUD/dvar/booth regression, and the production quit/restart
check. Both diagnostic and production Release builds completed.

The weapon-room comparison at eye position `(3231, -802, 64)`, yaw 96 and
pitch -9, exposed two additional platform omissions. The weapon wall's primary
spotlight (17) referenced two `wc/shadowcaster` BSP surfaces after the camera
ranges; dropping them erased the upper-wall and beam shadows. Uploading these
authored blockers restores the pattern with `sm_enable`, `sm_sunEnable` and
`sm_spotEnable` enabled and `sm_maxLights 4`. The upper-wall sample is now
99.0% of its baked-lighting brightness, compared with 458% before the correction.
Static and dynamic XModels now receive local primary diffuse and SM3 specular
lighting through the existing backend. Canonical light definitions supply the
attenuation image and sampler; model-volume alpha supplies authored visibility
until a selected spot map replaces it. Static materials select the canonical
instanced spot/omni technique. Diffuse and specular scales remain independent,
and ambient model lighting is doubled before direct lighting is added, matching
the owned native shader. The sampled AK magazine is 3.0 times its ambient-only
brightness. World spotlight materials also retain their SM3 direct highlight.
The optional owned-data regression compares both wall shadows and model lighting;
synthetic native tests cover shadow-only upload/camera exclusion, local technique
selection and attenuation sampler changes. These comparisons do not establish
complete native/browser pixel parity for all materials.
Validation passed four native renderer test executables, 10 Chromium smoke
tests, 60 remainder tests (13 optional skips), the owned-data HUD/dvar/booth/
weapon-wall regression, and production quit/restart. Diagnostic and production
Release builds both completed.

Spot-map selection now shares `GfxShadowedLightHistory` and the native
retirement/reselection arithmetic in `r_shadowed_light_history.h`. The four
slots retain outgoing visible lights while `sm_spotShadowFadeTime` fades them;
replacement lights fade in after a slot becomes available. Camera-invisible
lights release their slots immediately, as native does. History is per local
client and resets with world/context resources. The backend passes each entry's
fade through the native baked/live visibility interpolation for world and model
materials, including values below 0.5. This fixes the immediate table-shadow
switch when light 14 loses its rank as the view moves from `(3280, -1000, 64)`
toward `(3280, -1100, 64)` and turns from yaw 115 to 90. Synthetic shared-code
checks cover retirement, reversal and replacement; the owned browser regression
requires intermediate visibility on that camera path. This restores native
shadow-budget transitions; it does not increase `sm_maxLights` or permanently
pin a scene-specific light.
The owned HUD/dvar/booth/weapon-wall regression, including the table's fade
transition, passed in Chromium, and the user confirmed the popping was resolved
after reloading the production build. Native SP Release compilation, the 12-case
native surface/state executable, 10 browser smoke tests, 60 remainder tests
(13 optional skips) and production quit/restart also passed.
The optional transient-light fixture now admits its spotlight while scene time
is running before freezing visual comparisons, as native history cannot admit a
new light at a paused timestamp. Its illumination, shadow-toggle, clearing,
context-recovery and light-limit assertions passed, but the complete test failed
at the final positive DObj post-pose diagnostic assertion. That separate
actor-visibility evidence remains unresolved; the assertion is unchanged.

The [BSP sun milestone](evidence/bsp-sun-partitions-a23850aa.md) carries canonical
`GfxSurface` AABBs with retained spans and builds each cascade's light-space
selection independently. Camera DPVS is absent, range holes and alpha-tested
boundaries remain explicit, and adjacent opaque spans can still merge.
Dynamic sun draws now retain one world-space AABB after DObj/DynEnt assembly or
brush placement and test it against each sun matrix independently. This
platform-equivalent seam covers the five native dynamic families without
retaining entity state; it costs 24 bytes per flattened draw and one indexed
bounds scan per dynamic submission. See
[the dynamic sun evidence](evidence/dynamic-sun-partitions-6ece6ee9.md).
Dynamic spot draws now use the same retained bounds independently against each
selected perspective light matrix. DObj, DynEnt XModel, moving-brush, and
DynEnt-brush materials require the remap-aware build-shadowmap technique and
carry its cull/alpha state; camera and sun bytes remain absent. The
[primary-light closure](evidence/dynamic-primary-light-linkage-4ed38a84.md)
restores canonical entity/DynEnt visibility bits and authored light-region
hulls, then builds a selected-light mask before the existing matrix test.
Missing canonical arrays fall back conservatively to matrix-only selection.

The [dynamic geometry ownership milestone](evidence/dynamic-geometry-ownership-af601efe.md)
removes the remaining full CPU copy at the frontend/backend boundary. The
frontend transfers only final numeric vertex/index vector storage; the backend
retains its existing validation, GPU upload, recovery data and atomic command
publication. Failure returns ownership to the caller. World and static-model
commands, camera DPVS, primary-light linkage, and independent shadow selections
are unchanged. A measured inactive-buffer WebGL candidate was reverted after it
failed to reduce upload cost. Further work now begins at shared DObj
pose/lighting/skinning behavior rather than expanding platform GPU ownership.

The [canonical weighted-basis milestone](evidence/dobj-primary-basis-82b4de10.md)
makes the portable DObj skinner follow Kisak's native scalar and SSE rule:
positions blend across influences, while normals and tangents use the primary
bone. This deletes a browser-only behavioral divergence and its secondary basis
matrix work. Pose generation, packed decoding, finite checks, LOD/hide policy,
static-model camera culling and independent shadow selection are unchanged.

The [DObj lighting-handle milestone](evidence/dobj-lighting-cache-a16fb9f2.md)
uses canonical `cpose_t::lightingHandle` identity to reuse exact-origin
light-grid results. The platform retains only numeric sample payloads and
clears them on world unload; the per-frame atlas still follows submitted DObj
order. Changed origins recompute, and the handle now also reaches canonical
mark generation. Exact-work diagnostics observed 87.3% lower DObj lighting
time without changing static culling or independent shadow selection.

The [dynamic DObj convergence milestone](evidence/dobj-rigid-decode-3e65c7d0.md)
measures matrix, weighted and rigid skinning separately, then removes repeated
packed-attribute decoding from the dominant rigid path. The bounded cache owns
only immutable decoded vertex values and source identity until world unload;
current canonical bone matrices still produce every final vertex. Exact-work
diagnostics observed 22.3% lower rigid skinning, 8.4% lower DObj build and 6.6%
lower renderer-frontend time. A GPU rigid-placement candidate was rejected
because it changed shadow partition work and raised total time. Static-model
camera culling and independent sun/spot shadow selection remain intact.

Dynamic camera submission now mirrors Kisak's material draw-surface ordering
inside proven-safe opaque runs. Blended, depth-equal/disabled, no-color, FX,
sun, and other state-sensitive batches remain append-order anchors, and shadow
draw order is untouched. The platform retains one numeric key per batch and
one 32-bit camera-order index per dynamic draw; no engine identity or geometry
copy is added. See
[the dynamic order evidence](evidence/dynamic-opaque-sort-c8c4f335.md).

World camera visibility now extends that same canonical call through AABB
surface tests and cell cull groups, including native decal selection. It resets
the complete DB-allocated `staticSurfaceCount` camera mask, and checks sorted
surface ranges/indices and cull-group references before use. Shadow view bytes
and authored caster membership are untouched.

World draw commands retain each emitted canonical surface ID and its original
index-buffer span inside a merged material batch. The backend validates and
retains those spans, then builds contiguous visible camera runs on every view
submission. Empty completed masks produce no world camera draws; unavailable
or incorrectly sized results fail submission. Sun batches and authored spot
caster ranges keep the original index buffer, independent of camera visibility.
Static-model packing and LOD policy are unchanged.

This is permanent draw-command boundary metadata, not another world model.
It costs 44 bytes per emitted surface, plus up to 16 bytes per surface for
camera-run capacity and one reused 16-byte-per-range sun scratch vector. Camera
submission scans emitted surfaces once; sun drawing scans them once per
cascade. No extra geometry buffer, index upload, mask cache or JS visibility
state is added. World replacement clears runs; unload releases all vectors;
context restoration reuses retained geometry/spans. Retire the spans only if
canonical frontend draw commands directly provide the backend ranges. See
[world visibility evidence](evidence/world-camera-visibility-2026-08-31.md).

Diagnostics add five DObj CPU measurements (total build and four disjoint
substages) to the existing frame profiler. Production compiles them out. Pose,
lighting, and skinning ownership is unchanged; no DObj pose or geometry cache was added.
The [first focused stage profile](evidence/dobj-stages-946dc918.md), from a
120-frame headless CargoShip window, places geometry construction at 59.02% of
DObj build time. The subsequent [DObj-only hash deletion](evidence/dobj-hash-d8661476.md)
removes unused bytecode scans without changing canonical material identity or
world/static-model hashing. Matching short profiles observed 7.192 -> 6.347 ms
in geometry construction; no comparable foreground FPS result is claimed.

Six additional diagnostic intervals partition non-DObj scene work into setup,
command assembly, dynamic image resolution, dynamic submission, camera DPVS and
view submission. They extend existing telemetry only; canonical ownership and
the frontend/backend seam are unchanged. Upload timing nests within submission,
and all timers compile out of production. The
[focused scene profile](evidence/scene-stages-2ce03241.md) places 13.801 ms in
assembly and 4.638 ms in dynamic submission, prompting the finer attribution
below. No cache or runtime model was added.

Assembly diagnostics now distinguish physics/mark preparation, brush/model
work and command appends, with a nested particle-cloud append interval. The
[cloud-append comparison](evidence/cloud-append-ae37e80c.md) identified repeated
exact vector reservations as the dominant assembly cost. Removing those three
reservations lets standard vectors grow normally while preserving validation,
atomic logical rollback, draw order and canonical ownership. This remains a
platform-owned command-storage detail; no shared engine behavior or persistent
cache was added. Observed cloud append fell from 9.676 to 0.865 ms. Spare vector
capacity and the higher observed dynamic-submission cost need consideration in
the next optimization; no general FPS or memory reduction is claimed.

The [dynamic-staging continuation](evidence/dynamic-staging-9403a899.md) separates
CPU command copy, GPU resource setup and publication. Two backend-owned staging
vectors reuse prior dynamic geometry allocations, while published data remains
intact until upload succeeds. The portable staging-copy helper consumes already
validated spans; validation and canonical state ownership are unchanged. Unload
releases staging, and context recovery still consumes only published geometry.
This is platform-owned command storage, not a pose or geometry-result cache.
Keep it while the measured copy-tail reduction justifies the approximately
17 MiB staging capacity; retire it if frontend/backend geometry ownership can
be transferred directly without weakening publication or recovery semantics.
No whole-frame speedup or peak-memory reduction was measured.

The [dynamic texture continuation](evidence/dynamic-textures-74fe11aa.md) avoids
reapplying an identical six-texture/four-sampler set within the dynamic draw
block. It preserves the order of all six calls whenever any field changes,
because GL sampler parameters belong to texture objects and units can alias.
This is permanent backend-owned state handling, with a stack-local value reset
each frame and no canonical state, new resource or persistent cache. The helper
requires exclusive ownership of those bindings/parameters during the block;
reset it if a future path changes them. World/static culling and independent
shadow passes are unchanged. Diagnostic material/texture/draw intervals measured
a local texture-setup reduction, without a whole-frame CPU improvement.

The [falloff-uniform guard](evidence/falloff-uniforms-12ac17e5.md) keeps all three
falloff uploads on distance-falloff draws and omits them for other techniques,
whose shaders do not read those values. The shared backend material helper
owns this permanent uniform policy; canonical constants, validation and shader
arithmetic stay unchanged. No tracker or intermediate representation is added.
Observed dynamic material CPU cost fell in a focused comparison, with no
general FPS or visual-compatibility claim.

The [completed renderer CPU milestone](evidence/renderer-cpu-milestone-e4db91df.md)
adds pass-local backend state reuse for projections, material inputs and dynamic
feature flags, plus shadow-partition alpha/cull state. It also omits unused
detail and model-lighting uniform uploads. These are permanent GL-boundary
details: canonical commands, identities, validation, culling and independent
caster ranges remain unchanged. No GPU resources, persistent cache or engine
representation is added. Immutable batch/matrix contents and explicit reset
after direct overrides are required; new material inputs must extend the key.
Production frame timing observed a modest gain with overlapping run ranges.
The local renderer milestone is closed; canonical scene construction is the
next measured CPU priority.

The [direct DObj emission change](evidence/dobj-emission-fb596702.md) now constructs
vertices in the private replacement vector, removing a temporary vertex copy.
This remains a permanent platform draw-command detail, with no new retained
state or intermediate object model. Canonical pose, skinning, LOD and hide policy
remain shared inputs; validation and whole-command publication are preserved.
Focused execution covers emitted values and rejection without publication.
This prompted the brush construction/append measurement below.

The [brush cost investigation](evidence/brush-costs-f15c3dc9.md) retained diagnostic
attribution and a world/brush output oracle. Both proposed runtime changes were
reverted after production timing failed to support them. At that milestone,
geometry, hashing, technique selection and batch merge policy were unchanged;
no new cache, allocation policy or engine representation was added. Retained artifacts now
support interleaved comparisons before further optimization.

The [controlled timing follow-up](evidence/controlled-renderer-552a468d.md)
moves `Com_ModifyMsec` outside the temporary common.cpp compile gate and calls
it in the browser pump at the native pre-server boundary. Its single native
body owns fixedtime, time scaling and clamping for both paths; the browser
still owns only nonblocking callback admission. No prefix function was copied.
Canonical pause/free-move commands and existing refdef events now qualify a
paused renderer benchmark. The Node runner owns only test orchestration and
comparison; it does not own game state or a replay format. Exact sampled camera,
time and world-count matching is demonstrated, not complete dynamic state or
active-gameplay determinism. Culling and independent shadow paths are unchanged.

The [paused-copy follow-up](evidence/paused-copy-qualification-cd85e18e.md)
aligns diagnostic profiling with that scene and records actual geometry work.
It reproduced different index/upload totals across fresh loads of the same
Wasm, despite matching camera and draw counts. The name-copy experiment was
reverted; this milestone adds test orchestration and qualification only, with
no net engine/renderer changes. Resolving the remaining variation belongs in
canonical model/LOD and seed/save/replay behavior, not a browser game-state copy.

The [seeded brush follow-up](evidence/seeded-brush-hashes-06ad8004.md) partitions
accepted dynamic/UI geometry using four diagnostic-only counters. Variation was
in dynamic commands; the shared optional map seed produced matching measured
workloads across fresh loads. The runner owns only orchestration and metadata.
It does not author model, pose, random-generator or replay state in JavaScript.
The brush draw-command builder now reuses only the preceding shader hashes in
a stack record scoped to one synchronous build. This adds no persistent pointer,
heap allocation or global cache. Technique/material selection, output geometry,
atomic publication, canonical culling and independent shadows are unchanged.

## Temporary compatibility seams

| Seam | Retirement condition |
| --- | --- |
| Diagnostic launcher and C++ test exports | Keep only controls that exercise browser-only failure/recovery behavior; do not add product behavior here. |
| Normalized runtime telemetry | Retain while it catches ownership/order regressions; delete fields once no test or operational diagnosis consumes them. |

The former Gate 2 retail census, custom fastfile traversal, synthetic world
extraction, cooperative qcommon shell, scheduler, renderer comparison, and
proof jobs are retired. Canonical runtime tests replace their useful coverage.

## Native-only or excluded systems

| System | Browser position |
| --- | --- |
| Direct3D 9 and Win32 window/input code | Native-only; WebGL2 owns the browser backend. |
| Bink, Miles and Steam binaries | Never linked or distributed. |
| Raw UDP networking | Impossible in browsers; multiplayer requires an explicit gateway/transport design. |
| Dedicated server and Radiant | Excluded from the initial Wasm client target. |
| Loose editor/import development paths | Native-only unless a concrete browser workflow needs them. |

## Remaining work

The DObj conversion milestone keeps canonical pose generation, LOD/hide masks,
material resolution and lighting inputs unchanged. Final vertices are assembled
in one skinning pass, and only empty numeric geometry capacity survives frontend
submission. Selective web LTO exposes the shared Kisak packing and quaternion
helpers to that loop; no browser copies of their implementations were added.
Dynamic opaque sun ranges now use the same range-joining implementation as world
shadows, with placement/buffer identity preserved. See
[renderer resource ownership](renderer-retained-resources.md#dobj-conversion-and-dynamic-sun-ranges).
This is a renderer-boundary improvement, not expanded gameplay compatibility.
The [delivered evidence](evidence/dobj-conversion-30e34cff.md) records focused
checks, exact logical-work qualification, recovery and one final Release,
separating diagnostic DObj-stage reductions from the observed 7.54% production
pair-mean reduction and its control drift.

Dynamic DObj pose, weighted basis, lighting reuse, rigid transformation,
culling and shadow submission now converge on canonical Kisak behavior. The
final measured rigid path retains immutable packed-attribute decoding while
preserving the existing draw command and exact shadow work. This closes the
current renderer-frontend optimization path; further renderer changes should
respond to evidence from broader canonical gameplay rather than extend the
bootstrap as a standalone viewer.

The playable offline slice is already demonstrated across six campaign maps.
The mission-route author/replay and simulated-progression layer is retired with
no replacement. The first observe-only expansion run establishes `scoutsniper`
as `RENDERS` after canonical lifecycle completion and a 60-second stationary
window; it makes no visual, functional, or playable claim. See
[the stationary evidence](evidence/scoutsniper-stationary-838e047c.md).

- Compare the rendered `ac130` scene and close only measured thermal/material gaps,
  without introducing browser asset types or simulated gameplay.
- Qualify the source-built cinematic path against Steam: playback timing,
  synchronization, recovery, and in-world materials. Missing movies still
  report an explicit omission.
- Add gamepad input when it becomes a product requirement.
- Continue measuring the encoded-source recovery policy on later campaign
  batches. Consider source deduplication or reload from imported storage only
  if current retained-source or recovery-latency evidence justifies another
  strategy; profile streaming and Worker scheduling before considering
  pthreads.
- Design and document a gateway before compiling multiplayer transport code.

## Historical retail and memory evidence

Historical map classifications and their sources are consolidated in
[web-status.md](web-status.md) and [the campaign matrix](campaign-compatibility.md).
They were not rerun during this cleanup. Mission progression is not a prerequisite.

### Encoded recovery sources

Commit `c66d41e1` changed one recovery strategy. The world, static-model,
dynamic-model, and UI 2D pools validate each selected image through the canonical encoded layout and decode
once for its initial WebGL upload (the later `919f8c27` change), but they no
longer retain that decoded RGBA allocation. They retain either the exact DB
load-definition metadata and payload, the complete IWI member read through the
canonical filesystem, or the tiny raw `$white` pixels. Context restoration
decodes one image at a time through the existing Kisak image decoder and then
releases that temporary decoded buffer.

Sky/reflection cubes, lighting atlases, water, render targets, and other
supplemental paths were not converted by this strategy. Telemetry consequently
distinguishes:

- `textureRecoverySourceBytes`: actual CPU texture sources retained for
  recovery;
- `decodedTextureSourceBytes`: logical decoded texture size used for admission
  and GPU estimates, not retained RGBA bytes for the converted pools;
- `recoveryCopyBytes`: actual texture recovery sources plus retained geometry
  and shader/program recovery estimates; and
- `temporaryUploadBytes`: the largest transient decoded upload observed.

The unchanged 800 MiB limit remains a decoded-byte admission limit for each
shared image pool, not an aggregate actual-recovery budget. It was neither
raised nor arbitrarily lowered.

The A-F classes are A (irreplaceable), B (cheaply regenerable), C
(reloadable/redecodable from canonical imported source), D (duplicated by
content), E (process-global), and F (map-local):

| Resource | Classes | Lifecycle/ownership |
| --- | --- | --- |
| Retained LoadDef/IWI 2D recovery sources | B/C/F; LoadDef copies are also D while cached | Map-local renderer-backend recovery data derived from canonical DB or filesystem sources; discarded on map retirement. |
| Transient decoded 2D pixels | B | Recreated one image at a time for upload or context recovery and then released; the measured peak was 16,777,216 B. |
| Decoded supplemental texture recovery | B/C/F | Existing map-local cube, lighting, water, and related paths; not changed by `c66d41e1`. |
| DB LoadDef source resources | C/D/E | Process-global table bounded to 256 MiB at the canonical image upload seam, with lifetime owned by canonical primary/override image references. Renderer recovery copies duplicate selected payloads. Unreferenced resources are collected at DB completion/unload; admission cannot evict a live source. |
| Retained geometry and portable draw commands | B/F | Regenerable map-local renderer data. |
| GPU map textures and buffers | B/F | Regenerable backend resources; destroyed and recreated without becoming engine state. |
| Render targets and fixed pipeline programs | B/E | Intentionally process-global backend resources. |
| Audio data | B/C, predominantly F | Canonical audio feeding the browser device boundary. |
| Wasm linear-memory capacity | Not a retained-resource class | Allocator capacity, reported separately from renderer ownership. |

No class-A retained resource was observed. Content identity across distinct
image assets has not been measured; class D above is limited to the traced
LoadDef cache/recovery copy of the same payload.

On the same headed-Chrome Killhouse comparison point, the instrumentation-only
implementation and encoded-source implementation measured:

| Metric | Decoded retention | Encoded sources | Reduction |
| --- | ---: | ---: | ---: |
| Aggregate renderer recovery | 1,417,257,708 B | 506,423,759 B | 64.27% |
| Allocator bytes in use | 1,731,297,456 B | 820,736,432 B | 52.59% |
| Wasm linear-memory capacity | 1,809,121,280 B | 989,921,280 B | 45.28% |
| Program break | 1,808,785,408 B | 901,816,320 B | 50.14% |

The final clean Chrome Killhouse run at `f5229806` reached its first frame in
5,710.910 ms and reported 1,419,469,864 B logical decoded textures,
427,163,511 B actual retained texture sources, 396,164,047 B encoded image
sources, 518,433,483 B aggregate renderer recovery, 1,440,467,016 B estimated
GPU textures, 91,269,972 B geometry, and 989,921,280 B Wasm capacity. Edge
reached the Killhouse first frame in 5,693.485 ms and reported 518,844,075 B
aggregate recovery at the same 989,921,280 B capacity.

Re-decoding trades some restoration latency for the memory reduction. Chrome
restored CargoShip in 1,357.375 ms, Blackout in 1,790.265 ms, and returned
Killhouse in 1,960.945 ms. Edge measured 1,349.035 ms, 1,849.430 ms, and
1,893.305 ms respectively. Every
recovery resumed real world frames and gameplay input, so the strategy passed
the clean Chrome and Edge acceptance matrices without asset corruption or a
map-lifecycle regression. Continue recording this latency rather than treating
the memory saving as free.

## Verification scope

Use the [test inventory](web-test-inventory.md) for current validation tiers
and known limits. Native/Wasm parser tests own semantics that do not require a
browser boundary; retail checks require legally owned local files and are
never routine CI fixtures. Historical measurements do not establish current
campaign performance or original-game fidelity.
