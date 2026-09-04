# Web port convergence inventory

Updated 2026-09-04. This page owns system classification; see
[current status](web-status.md) and [the active roadmap](web-roadmap.md) for
evidence and priorities. Earlier chronology is available in
[Git history](history/README.md).

## Current verification boundary

The owned-data Gate 3 acceptance now follows the complete canonical
lit/decal/emissive DPVS ranges introduced by `498289c7`. Its exact Killhouse
geometry is 8,475 surfaces, 445,369 vertices and 823,464 indices: 411 surfaces,
13,622 vertices and 30,276 indices beyond the retired contiguous-prefix
expectation, with the same one sky surface skipped. The test passes startup and
map publication, the first WebGL2 frame, keyboard and pointer-lock input, and
the CM/Com/game/server/client/cgame lifecycle. Its four fastfiles are owned data
while its IWDs are synthetic, so the asserted 796-batch, 8/339-image fallback is
kept separate from full-archive material-fidelity evidence.

DObj transient spot receiver exclusions now use shared native
`gfx_d3d/r_light_attachment.cpp`. The frontend preserves the native scene
entity/model distinction and render flag 8 policy; the WebGL boundary retains
only a receiver draw flag. Native/Wasm identity and next-frame reset checks,
and Chromium spot-versus-omni pixels pass. The backend now globally orders all
eligible receiver families by native's reverse material/surface key. Exact
equal-key object-ID ties and authored-effect comparison remain open; see the existing
[transient-light evidence](evidence/transient-lights-2026-09-02.md).

Transient spot-shadow casters now reuse native admission rather than a shared
projection-box approximation. BSP ranges use exact shifted light planes without
camera visibility; static models reuse the camera/light receiver mask; dynamic
models and brushes reuse their exact receiver predicates and DObj exclusions.
The static/entity child dvars gate their own families. Sun matrix partitions
and authored primary-spot membership remain independent.

BSP transient receivers now combine canonical camera visibility and each
surface's original bounds before coalescing draw ranges. Native and web share
spot-plane construction, strict box/plane contact and inclusive omni-sphere
intersection in `r_dynamiclights_core.h`. The backend retains only index runs,
clears them on failed views/replacement and releases their capacity on unload.
The shared routines match the pre-change native implementation exactly for
4,096 spotlights and 98,304 bounds cases. Permanent native/Wasm tests cover
visibility, near/far contact, near-plane/radius changes, batch gaps and
empty/invalid results. Their ranges participate in the global receiver sort.

Chrome 152.0.7977.77's ANGLE/D3D11 backend exposed a GLSL sampler-overload
collision missed by the bundled Chromium gates: translated 3D, cube and
shadow parameters all became `uint`, producing identical HLSL signatures.
Distinct helper names preserve the same sampling expressions and restore
dynamic pixel-executable compilation. The existing transient-light pixel test
fails before the fix and passes afterward on D3D11. Three additional Chrome
graphics cases still fail unchanged exact-byte assertions with 127 versus 128;
native rounding/fidelity is not inferred from those observations.
The owned Killhouse light/shadow/clear/context-recovery check passes on that
D3D11 backend after correcting its comparison across two different paused
poses. Authored light appearance and campaign acceptance remain unverified.

Static-model transient receivers now combine the original canonical bounds
with camera DPVS addressed by each packed instance's canonical ID. The native
omni callback and browser mask share the sphere/box predicate; spotlights use
the already-shared plane/box predicate. Existing LOD-packed instances supply
contiguous eligible draw runs. The existing visibility scratch is rebuilt for
each light/shadow pass, so no extra geometry copy, instance buffer or retained
mask cache is added. Native/Wasm tests cover reordered IDs, camera exclusion,
near-plane contact, moving lights and failed-mask clearing. Static draws
participate in the same globally sorted receiver list as world and dynamic geometry.

Rigid DObj, FX-model and DynEntity-model transient receivers now carry the
native scene-model sphere through the draw boundary. Rigid DObjs use XModel
radius at the pose origin; FX uses radius times placement scale; DynEntity
uses its current pose radius. Native/browser spot receiver tests share
`SphereInPlanes`, including tangent contact. Omni selection follows native's
squared distance versus radius-sum test. Invalid sphere data is rejected
before retention. Authored comparison remains open.

Scene and DynEntity brushes now carry `GfxBrushModel::writable` world bounds
in each frame's instance command. Those bounds remain owned and updated by
canonical cgame/DynEntity code; the backend does not transform them again or
derive them from retained mesh vertices. Shared spot box/plane and omni
box/sphere predicates filter the existing material pass. Geometry-derived
sun-shadow caster bounds remain separate, transient spot casters reuse these
receiver bounds, and no receiver cache is added.

Animated DObjs now compute the selected LOD bone mask before canonical pose
evaluation. Native `R_UpdateSceneEntBounds` and the browser share the same
per-bone `XBoneInfo` AABB transform in `r_model_pose_bounds.h`; both restore
the renderer view offset and union only selected bones. The resulting one-box
scene-entity receiver is copied to every emitted DObj surface. The backend
applies the shared box/spot-plane or box/omni-sphere predicate. Invalid pose or
bone bounds reject the command atomically. A 4,096-case native/Wasm scalar-order
comparison covers the extracted transform. Camera DPVS now runs before dynamic
assembly: DObj/DynEntity spheres and scene/DynEntity brush boxes must overlap
the portal-visible cell mask and pass the shared native plane predicates, while
FX models use native's direct sphere/frustum rule. After pose evaluation,
animated DObjs also retest their selected-bone AABB against the camera planes
before skinning, matching native's second-stage rejection. Linked DObjs and
scene brushes now also consume each cell's exact portal-plane dispatch before
expansion. Cgame link/unlink and snapshot initialization maintain the native
world-owned `sceneEntCellBits` banks through bounded helpers in `r_dpvs_core`.
The helpers call native `BoxOnPlaneSide`, preserve one-word BSP leaves, client
offsets and relinking between banks, and commit a new link only after the walk
validates. The renderer retains link validity and the cgame-supplied DObj radius
(native `scene.dpvs.entInfo` metadata), not replacement entity or pose state.
Portal admission is local to the current synchronous view and combines repeated
paths. Unavailable scene links retain the conservative overlap fallback.
DynEntity link/unlink and world initialization now maintain the original
`dpvsDyn.dynEntCellBits` model/brush banks. Native and web callers share bounded
link and cell-cull helpers: MSB-first entity IDs, independent bank strides,
full cell planes, zero-plane admission and OR across repeated portal paths.
The browser clears and consumes canonical `dynEntVisData[kind][0]` each view,
checks collision/world entity counts, and rejects malformed links or storage
before a command can render. DynEntity submissions use this mask directly;
they no longer repeat the approximate BSP-overlap test. Linked animated DObjs
now evaluate the selected canonical LOD bones during that cell dispatch, test
the updated box against all cell planes, and query exact BSP cell membership.
Native `R_BoundsInCell` and the browser share the bounded `BoxOnPlaneSide` walk;
malformed walks are distinct from a valid miss. Pose selection is shared with
the command builder, and `CG_DObjCalcPose` retains its native per-frame skeleton
reuse. No extra portal-plane storage, skinned-vertex bounds substitute or
persistent pose cache is introduced. `CG_UsedDObjCalcPose` and `CG_CullIn` now
preserve native pose-use/visibility flags, including repeated rejected paths
that must not undo prior admission.
DObj lighting evaluation now follows post-pose visibility rejection, so a
culled submission cannot mutate its pose-owned lighting handle or disable the
atlas for visible submissions. DynEntity model and brush draws copy canonical
placements and receiver bounds before `FX_RunPhysics` and
`DynEntCl_ProcessEntities`, matching native `R_RenderScene`; static and dynamic
mark generation remains after physics. Effects timing is excluded from the
model-build timer. No additional pose state is retained.

Transient receiver submission now mirrors `R_EmitPointLightPartitionSurfs` at
the renderer boundary. Each light builds one list from BSP ranges, static-model
instance runs and native receiver scene kinds, then sorts the complete list by
the canonical packed `GfxDrawSurf` key with only `primarySortKey` complemented.
World, brush, static rigid/skinned and XModel rigid/rigid-skinned/skinned
families carry native surf types 0, 6, 2/5 and 7/8/9. Code meshes, marks,
particle clouds and sun billboards do not enter the list. A 4,096-key
high/low-word oracle and native/Wasm producer tests pass. Equal packed keys use
stable frontend order because the browser boundary does not retain native's
low object ID; matched Steam/native/browser appearance remains unverified.

Texture sampler decoding now shares `gfx_d3d/r_sampler.h` with native
`R_SetTexFilter`: authored filter bits, player anisotropy limits, mip-mode
overrides and filtering disable produce the same packed native policy.
WebGL translates only that result into device parameters, clamped to the
extension's actual anisotropy limit. The same adapter now updates cubemaps,
model-lighting volumes and animated water at draw time; those targets previously
kept their initial filtering. Native and Wasm match the 19,200-case trace
from unmodified `R_SetTexFilter` at `8be61213`. Normal/specular/detail dvars now
gate existing world/static/dynamic shader flags; transient light passes obey
normal/detail controls. The web frontend also runs the native technique-set
feature-name policy after atomic zone publication and on relevant dvar changes,
using canonical DB lookup for available targets and resolving leading-comma
aliases after target selection. A bounded native/Wasm graph proves target,
alias and missing-variant behavior. An owned Killhouse load selects 201 feature
variants among 165 shader-model-3 sets; disabling normal maps updates 221 sets
on the next frame and restoring the dvar triggers a second update. No gameplay
input or campaign progression was used.
Native image-quality and semantic/no-picmip policy now share
`gfx_d3d/r_image_quality.cpp`; D3D supplies measured memory and WebGL supplies
explicit planning inputs. Existing bounded IWI/load-definition and canonical
wavelet decoding select authored mip levels before allocation/upload. The
retained source recipe owns only the backend's selected mip, not another image
identity. The browser frontend registers the shipped menu's canonical
`r_applyPicmip` command and maps it to the existing renderer restart because
the backend rebuilds bounded retained sources rather than mutating D3D images
in place. Synthetic pixels/residency/recreation and owned Killhouse production
menu/restart/persistence pass. Native mip bias now reaches implicit material/sky/
shadow sampling and explicit reflection LOD. Synthetic mip-colour, fractional
filtering and recovery pixels pass; the native cheat/non-archived semantics
remain. Unknown shader families, arbitrary multipass semantics, broader
material qualification and original/native
comparison remain open. No alternate material or image
model was added.

Owned paused Killhouse specular/normal toggles change rendered pixels, return
to the original baseline, and survive actual context recovery with identical
captures. This qualifies control causality in that narrow paused view; authored
material fidelity remains open.

Browser text now reaches shared `CL_CharEvent`, `Field_CharEvent` and shipped
UI edit fields alongside physical `CL_KeyEvent`. Only layout/byte translation
and event transport are platform-owned; editing, native shortcuts and repeat
policy remain shared. The current adapter supports Windows-1252 text and
committed composition events. A trusted canvas press now focuses an editable,
out-of-tab-order text sink while preserving canvas pointer lock, allowing the
browser to activate dead-key/IME composition without replacing physical game
keys. A trusted paste event installs one bounded line
in a platform cache before character 22 invokes canonical `Field_Paste`; the
owned shipped console path passes. Served Chromium verifies sink focus and
one-time composed Windows-1252 delivery; actual Windows IME candidate UI,
other code pages, arbitrary programmatic clipboard reads and localized retail
glyph qualification remain open. See
[input architecture](web-architecture.md#input-coordinates-and-lifecycle) and
[current checks](web-status.md).

The platform frame wrapper now checks the canonical latched error after its
body, matching `Com_Frame` and preventing a reached renderer failure from
leaving the host marked running. The portable light-grid helper accepts native
`R_InitEmptyLightGrid` and preserves its default-palette/primary-light result.
AC130 supplies current execution evidence for both reached boundaries; its
game, camera, mission and FX state remain canonical. See
[AC130 evidence](evidence/ac130-rendering-2026-09-03.md).

The asynchronous mount continuation now has a live canonical `setjmp` error
boundary after `Com_Init` has returned. `Com_Error` retains its native text
through the Worker cleanup/ownership protocol. The shared synthetic import
fixtures include real startup prerequisites, and the picker test waits for
completed mounting and restoration. See [verification evidence](evidence/verification-repair-2026-09-02.md).

The three-way reference remains incomplete: Steam hashes/settings are pinned,
and native SP now builds and enters main-menu code using the owned startup
fastfiles. Native headers share the canonical world and critical-section types;
common runtime definitions have a single owner, and XAnim counters use C++20
atomics. Native OpenAL avoids the owned Miles DLL's incompatible exports.
See [build, boot and remaining test failures](evidence/native-reference-2026-09-02.md).
No browser/runtime comparison can stand in for original Steam mission-completion
evidence.

Canonical `Com_Quit_f` now requests platform shutdown at the end of the Worker
frame. Canonical config serialization stays in C++; the browser flushes existing
writable handles, stops audio/input, releases leases and terminates the Worker
only after confirmed persistence. Worker destruction owns the final engine heap
release instead of native process teardown. The launcher retains a retry path
for save errors. This is a permanent platform adaptation, not another game
lifecycle model. See [Quit evidence](evidence/browser-quit-2026-09-02.md).

The frame callback now also owns a live canonical `setjmp` target. Shipped
Save and Quit raises `ERR_DISCONNECT` after saving; recovery uses the extracted
native `Com_ShutdownInternal`, SP `SV_Shutdown` and `Com_AssetLoadUI` owners to
retire the game and reload the main menu in the same Worker. Natural Killhouse
training/checkpoints and fresh-browser Resume Game exposed and verify this seam.
`common_error.cpp` now shares native `Com_ErrorCleanup`, error localization,
temporary-memory reset and quit/escalation policy with the web frame pump.
After owned startup completes, `ERR_DROP`, `ERR_DISCONNECT` and
`ERR_SERVERDISCONNECT` use that cleanup; pre-initialization/fatal errors stay
terminal. Browser cleanup supplies synchronous DB completion, fastfile-only
BSP ownership and renderer-list retirement instead of native worker/D3D/debug
socket teardown. The owned error-dialog -> Killhouse -> error-dialog ->
Killhouse reload regression passes. It exposed a shared `Dvar_InfoString`
one-byte/four-byte callback-mask mismatch: stale stack bits overflowed server
info and omitted `mapname`. The flags word is now correctly sized, with
native/Wasm selection checks. The runtime-prefix test keeps assertions enabled
in Release. Malformed-load rollback and error re-entry still need broader
qualification; this is not campaign acceptance. Earlier Save and Quit evidence
remains in the [disconnect record](evidence/campaign-training-disconnect-2026-09-02.md).

The existing canonical DB fixture now also owns a 328-case bounded mutation
run (`canonical_xfile_mutation_tests`, or the stream-test executable with
`--mutate`). It loads recompressed synthetic streams through `DB_LoadXFile`,
the generated loaders and real registry pools. Both PMem directions, all nine
block sizes, malformed counts/truncation, pointer fields, alias identities,
ordered child/parent publication and explicit failed-zone pool retirement
are exercised while an unrelated zone survives. The Win32/Wasm normalized
trace is pinned to `99b9d10c`; engine release assertions remain enabled.
An absent canonical allocation block now takes the web input-failure path
before the native cursor assertion. This shares the existing loader and
stream state rather than introducing a second parser. A 32-bit MSVC oracle now
compiles the actual `db_load.cpp` RawFile routine behind a test-only source
slice. The native routine and adapted generated loader consume the same direct
serialized fixture and match on read order/consumption, inline `-1`,
insert-pointer `-2`, block-4 alias resolution, final-pointer replacement at
publication, asset name/index/type and logical stream coordinates; the adapted
oracle also passes as Wasm. The fixture supplies byte reads and a publication
callback; it does not qualify the native inflate layer or full registry.
The test slices are disabled in production builds. The rebuilt production Wasm
is byte-identical to the pre-oracle artifact (`a9546c0b7fb190ef193fe9c54d69830dbe8d695d5c3788e59c5cbd3b90868912`).
Independent comparison of more asset families and arbitrary alias graphs
remains open.

`canonical_db_zone_recovery_tests` reuses those fixtures with real
`db_registry.cpp` browser orchestration, `physicalmemory.cpp` and
`web_thread_context.cpp`. Its 80 repeated partial RawFile failures prove
automatic override/pool rollback, preservation of same-flag zones, PMem
reclamation at both ends and successful retries. A further 43 failures publish
replacements for the compiled world singleton pools (`clipMap_t`, `ComWorld`,
`GameWorldSp` and `GfxWorld`) before a malformed trailing RawFile is rejected.
The publication transaction restores the prior singleton body, name, hash
ownership, zone identity and in-use state while its nested arrays remain owned
by the surviving PMem scope; candidate registry entries and allocations are
retired, and valid retries commit. `R_UnloadWorld` and `Com_UnloadWorld` counters
prove that rejected replacements do not tear down the surviving renderer/world
owner and that a committed replacement retires the prior owner exactly once. Release
follows PMem's allocation order, including reused zone indices, as native
Kisak's reverse load-order policy requires. XFile failure and stream bounds now
live in their canonical source owners; `db_runtime_prefix.cpp` only observes
them, so stale diagnostic failure cannot block a new load. DB-owned strings
now use the real `scr_stringlist` user with explicit zone/default ownership:
native/Wasm checks prove shared strings survive a partial zone release,
last-owner retirement removes them, and a live zone-0 default prevents the
coarse whole-user shutdown. Broader asset graphs and non-world device side
effects still need qualification. A bounded
two-fastfile request now also rolls back atomically when file one has published
a replacement ClipMap and file two exhausts the `MapEnts` pool: both new zones
are retired together in reverse PMem allocation order and the pre-request
singleton returns. Broader request graphs still need coverage; native-game
loader comparison currently covers only the focused RawFile contract.

Image retention now follows the canonical `GfxTexture` resource boundary:
opaque handles preserve distinct same-name payloads through native image
copies, overrides, default substitution and aliases. Completion/unload collects
resources absent from canonical primary and override entries. Forty rejected
image overrides restore the original bytes without accumulating resources;
valid retries and default copies surviving zone release pass in native/Wasm.
The 256 MiB source cap now rejects admission before copying rather than evicting
live bytes during speculative loads. The native material mark policy also
corrects semantic-11 water dependencies to follow `water_t::image`.
Dynamic menu material aliases now publish through the same registry under the
active zone. Their material record and state, texture and constant tables are
independent copies, while referenced canonical assets retain their existing
identities. Zone retirement removes the alias and frees its auxiliary tables;
native/Wasm recovery checks cover source mutation and unload.

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
| Database | Canonical XFile stream, allocation blocks, generated loaders, pointer aliases, registry pools, dependency ordering and final publication own runtime assets, including native-compatible leading-comma asset-stub resolution and zone-owned dynamic menu material aliases. |
| World/runtime | Canonical `GfxWorld`, collision, server/game, client/cgame, script VM, XAnim/DObj, effects, ragdoll, physics and sound code are in the browser link closure. |
| Frame order | The browser supplies elapsed time; canonical `Com_ModifyMsec`, `SV_Frame`, client frame work and `SCR_UpdateScreen` advance gameplay. Browser startup selects `com_maxFrameTime=5000`, and the platform pump uses the same long-stall ceiling instead of losing time on every frame longer than 100 ms. Script/developer timescales and opt-in `fixedtime` remain canonical. |
| SP UI | Canonical `CL_StartHunkUsers`, `CL_InitUI`, `UI_Init`, shipped MenuLists, `UI_SetActiveMenu`, `UI_Refresh`, and renderer 2D commands own main/options/profile/load/pause menus. Disconnected browser frames continue through `CL_Frame` and `SCR_UpdateScreen`; UI-only backend frames are valid before any `GfxWorld` exists, so startup presents the shipped main menu rather than a DOM substitute. |
| Text presentation | `r_text.cpp` and `r_text_cmds.cpp` move native glyph lookup, layout, color codes, shadows, subtitle glow, cursor blinking and reveal/decay logic into sources compiled by native and web targets. Canonical fonts/materials remain authoritative. The browser only provides scene/cursor clocks, SP color lookup and quad submission; console bytes pass through the native ring-copy routine. See [text evidence](evidence/text-presentation-2026-09-02.md). |
| Objectives | Native server configstrings, `CG_ParseObjectiveChange`, `objectiveInfo_t`, `objectiveinfo`, canonical localization, timed menu visibility, and renderer text/quads own objective notifications. Diagnostics only observe hashed emitted text and inject a freely usable string through the canonical parser. |
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
| Rendering | WebGL2 context, buffers, textures, shaders, render targets, context recovery and presentation. GPU handles stay private to the backend. The shared 2D image pools retain the selected canonical encoded source and use the existing image decoder, including canonical IWI wavelet formats, transiently for initial upload and context restoration; this is recovery data at the platform boundary, not a second asset model or parser. |
| Input host | Pointer lock, keyboard/mouse normalization, trusted paste snapshot/cache transport, focus release and cursor mode. |
| Audio device | AudioContext policy, buffers/nodes and PCM scheduling. Source offset/completion use `AudioContext.currentTime`; validated generation-tagged feedback replaces the proxy wall clock. Absolute queue ordinals survive unqueue/feedback crossing in flight. One snapshot can be in flight, sampled at 25 ms while the host is available; synchronous Worker work cannot accumulate feedback messages. Startup remains suspended and muted until an intentional canvas gesture resumes it; installation-picker interaction does not unlock sound. |
| Main loop | Non-blocking Emscripten frame pump; no Asyncify or pthread requirement. |
| Wasm stack | The web linker reserves 1 MiB, matching native Windows scale for canonical nested map/save loading rather than rewriting shared call chains. |
| Cinematics | Existing `R_Cinematic_*` callers drive the platform FFmpeg Bink decoder, canonical Y/Cr/Cb/A code images, and OpenAL PCM queue. The canonical single-pass `cinematic.hlsl` material uses retained R8 planes with native colour coefficients and filtered chroma; world/brush, static-model, DObj and UI draws bind current planes at draw time. Recovery also works without 2D submission; authored in-world scene fidelity remains unverified. Video follows cumulative device-played PCM, including unqueued buffers; one decoder-owned pending frame feeds audio before presentation. Owned delay/suspension and WebGL recovery pass. Movie identity and subsequent game actions stay in Kisak. See [codec scope and remaining qualification](cinematic-codec.md). |

## Control classification

| Control family | Classification |
| --- | --- |
| Reached SP main/options/profile/load/pause controls | Shared canonical dvars and commands. The focused menu trace rejects missing references. |
| `ui_sp_unlock` | Deliberate stock-retail dangling menu reference. Native COD4 1.7 emits the same `openmenuondvar` warning; no guessed browser dvar is registered. |
| D3D9/Win32 renderer controls | Native-only where they configure APIs absent from WebGL2; browser renderer capability controls remain platform-owned and are not aliases pretending to be native dvars. |
| Miles, Bink, and Steam controls | Native DLL integrations remain unavailable. Web Audio and the source-built FFmpeg codec provide device behavior behind existing sound/cinematic APIs; Steam integration remains omitted. |
| Multiplayer and dedicated-server controls | Not compiled into the initial offline SP target. They return only with the documented browser transport/server milestone, not as inert SP dvars. |

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
model surface batches scan the resulting LOD-packed bytes. Camera DPVS, sun
partition bytes, canonical asset identity, and GPU resource ownership remain
separate.

The [Kisak renderer optimization audit](kisak-renderer-optimization-audit.md)
tracks applicable native techniques to closure. The
[BSP sun milestone](evidence/bsp-sun-partitions-a23850aa.md) carries canonical
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
It costs 40 bytes per emitted surface, plus up to 16 bytes per surface for
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

The 2026-08-31 static-camera continuation uses the bounded checks recorded
in [the visibility evidence](evidence/static-camera-visibility-2026-08-31.md).
The [renderer handoff](evidence/renderer-efficiency-2026-08-31.md) is prior evidence. The
[cleanup record](evidence/cleanup-renderer-2026-08-31.md) is earlier evidence.
No routine full tier or mission-flow gate applies to this work. Native/Wasm parser tests remain
authoritative for cases that do not require a browser boundary; retail checks
require legally owned local files and are never routine CI fixtures.
