# Transient FX light boundary — 2026-09-02

Working tree based on `a2d54b7d`, following verification, saved-screen and
average-lighting repairs. Canonical FX's formerly discarded omni/spot calls
now reach the WebGL2 scene renderer. No FX, entity or gameplay state moved
into JavaScript.

## Behavior and native reference

- `r_dynamiclights_core.h` extracts native `GfxLight` construction and the
  importance partition used by `r_scene.cpp`/`r_light.cpp`. Native and web
  callers share it. Existing native precondition assertions remain. Spot
  origin/radius offset, cone, brightness and shadow intent stay canonical.
- The frontend clears submitted lights at `R_ClearScene` and world unload,
  retains the native 32-submission ceiling, culls against camera planes and
  uses native importance selection and `r_dlightLimit` (up to four lights).
  `r_fullbright` suppresses light submission. Selected records remain
  `GfxLight`; the existing image bank owns the `light_dynamic` attenuation
  image and its context-recovery source.
- The backend executes loaded material technique 21/22 passes between lit
  and emissive camera regions. Native state bits, base/detail/normal images,
  cone/exponent, diffuse scaling, attenuation sampling and vertex fog feed
  the existing shader/backend boundary. The 2026-09-03 continuation also
  applies the selected transient spot-shadow comparison in that pass. No new
  asset representation exists.
- Native `R_DrawPointLitSurfsCallback` clears destination alpha before each
  light. The encountered state `18128928/e0040048` uses
  `ONE_MINUS_DST_ALPHA`, so omitting that clear suppressed almost all light.
  The backend clears alpha while preserving scene RGB and depth. It now ports
  native's projected tangent-sphere rectangle and keeps both the clear and
  receiver draws inside the corresponding WebGL scissor. This
  also preserves coverage rejection within one light and accumulation across
  separate lights. Pixel tests use RGBA8, as the scene targets do; the opaque
  browser framebuffer cannot test destination-alpha behavior.
- Repeated GPU query spans for one frame/stage are summed into one result
  when ready. Draw counts retain their world/model/FX categories. Full active
  gameplay profiling and per-stage CPU accounting for the new light pass
  remain to be qualified.

Owned shaders were inspected read-only in memory with system D3DDisassemble.
No proprietary bytecode, fastfiles, images or screenshots are committed.
The inventory remains [the pinned Steam installation](steam-reference-2026-09-02.json).
Representative shader bytecode SHA-256 values:

| Shader | SHA-256 |
| --- | --- |
| `l_omni_r0c0.hlsl` | `9d56cce09285c41f9c65e5649612141df21e7f279b181c26cb6a22f76ff4eed3` |
| `l_spot_r0c0.hlsl` | `05a8c13112ce064a517a373562130638f82834ca47931535ab58e9e9d79b800f` |
| `l_omni_t0c0n0.hlsl` | `b654199f143cd15c96088d66321aaca1cbef03d2be8420abeae773792fbd8081` |
| `l_spot_t0c0n0.hlsl` | `f12f925b5fd5a53864e11feae01ce9244f267f0e52014dcfc52a19352fb6ea5f` |
| `l_omni_b0c0.hlsl` | `56247a61f54dceade56643c858c7b4fa195b777ffb88ce5dabbfc7101321dcdc` |
| `lp_omni_tc0_dtex_sm2.hlsl` (vertex) | `f8eb4546d7a05716bb7f073ff08079c1ef2bf8f62da18043c8fc43d8a990af80` |

## Execution

- Pinned Emscripten 6.0.6, Node 24.18.0/npm 11.16.0. Production and diagnostics
  Release builds and both canonical runtime-prefix checks pass.
- Focused `web_renderer_lighting_tests` pass with host Clang 24 x64 and
  Emscripten/Wasm Node. Synthetic checks cover construction, spot offset/cone,
  brightness and native priority partitioning. This is shared helper evidence,
  not execution of the full native client, whose earlier build failure remains.
- Bundled headless Chromium 149.0.7827.55, isolated diagnostic port 8137:
  synthetic light pixels pass for additive overlap, opposite spot direction,
  alpha test threshold, blended alpha squared, attenuation, AG normal decode,
  interpolated vertex fog and destination-alpha coverage/reset. Routine smoke
  **12 passed**; remainder **40 passed / 5 optional retail skipped**. Inherited
  retail variables and the browser-channel override were cleared explicitly.
- Separate owned Killhouse test, persistent headless Chrome 152.0.7977.65,
  port 8138: **1 passed**. After the opening fade, canonical pause holds the
  scene. Synthetic renderer inputs visibly illuminate world geometry and the
  character, obey 32/4/zero limits, survive real WebGL context loss/restoration
  and clear back to baseline. The test checks screenshot RGB means, not only
  submitted draws. Private images were visually inspected; an earlier stable
  capture returned exactly to baseline after clearing. It is a renderer check,
  not authored FX, gameplay performance or mission-completion evidence.
- Production size/API gate passes: **3,310,938-byte Wasm**, 24 raw exports,
  9 application exports. Production boot and canonical mount-error checks:
  **2 passed** separately on port 8139 with bundled headless Chromium.

Final Wasm SHA-256: production
`3318693eeb061176fae7a9f58e0472163a38dda3b434910b063b61205ffc35b6`;
diagnostics `ff87386ce2e573a2f54620be4ffb2791122c98a8a68808f25e25d74c4c75caf6`.

The 2026-09-03 continuation shares the native scissor calculation with the
host/Wasm lighting test, carries added-light-zero identity and its exact spot
near-plane offset through the renderer seam, and lets that transient candidate
compete with primary spots for `sm_maxLights`. At that stage its 512x512 map
conservatively selected retained world, static-model and dynamic caster bounds.
The owned Killhouse diagnostic on isolated port 8037
passes: the selected transient slot visibly darkens the synthetic spot relative
to `r_spotLightShadows 0`, survives real WebGL context loss after one normal
post-restore cgame frame, and retains 32-submit/4-draw/zero-limit behavior.
Private screenshots contain owned data and remain uncommitted.

The final production and diagnostics Release scripts both pass their strict
runtime-prefix checks. The maintained native and Wasm matrices pass 38/38 and 39/39,
along with 96 Node, static, 10 smoke, 59 clean remainder (nine retail skips),
seven graphics and 44 production browser checks. The product boundary accepts
the file set, exports and diagnostics separation before its unchanged size
budget rejects the 3,729,180-byte Wasm. The measured engine gzip is 1,489,003
bytes and SHA-256 is
`a9546c0b7fb190ef193fe9c54d69830dbe8d695d5c3788e59c5cbd3b90868912`.

The 2026-09-04 technique-set continuation ports the native feature-token name
policy into the browser frontend without changing the native renderer. A shared
bounded graph check passes in Win32 and Wasm for available targets,
leading-comma aliases and absent variants. A disposable owned-data probe loads
Killhouse without player input: 165 shader-model-3 sets publish, 201 available
feature variants are selected, and `r_normal 0` updates 221 sets on the next
frame before the setting is restored. The rebuilt production/diagnostic prefix
checks and seven graphics browser cases pass. Unknown shader families and
general multipass equivalence remain open.

## Remaining fidelity work

The DObj receiver boundary now shares native `R_SpotLightIsAttachedToDobj`
from `r_light_attachment.cpp`. At each scene build, the web frontend preserves
`R_AddDObjToScene`'s scene-entity/model split and excludes attached scene DObjs
and those with render flag 8 from transient spot passes. Rigid single-model
submissions retain native's separate eligibility; omni passes remain eligible.
Only the resulting draw flag crosses into the retained WebGL batch.
Win32/Wasm checks exercise inactive, detached, null, different and matching
attachment identities, flag 8, animation-tree classification and next-frame
reset. Chromium pixels prove excluded spots emit no light and omni output is
unchanged. Native SP and both browser builds, 41 Win32/40 Wasm tests, seven
focused graphics cases and 44 production/10 smoke/59 remainder cases pass
(nine retail skips). No new owned-data or authored-effect comparison was run.
The production Wasm is 3,729,410 B, SHA-256
`93c0f11f88696ed9c7a5af8965672d8d7ea05590786e4766b3a33f583a5e4481`;
the unchanged production size gate still fails.

Transient spot shadows and native screen scissor rectangles are implemented.
BSP receiver ranges now use canonical camera visibility, original per-surface
bounds and shared native volume math before merging adjacent index spans.
`R_CalcSpotLightPlanes`, `R_ComputeSpotLightCrossDirs`, `R_BoxInPlanes` and
the BSP omni callback delegate to the same `r_dynamiclights_core.h` routines.
A disposable Win32 oracle compiled the previous implementations from Git,
including their vector operations: 4,096 normalized spotlight directions
produce 98,304 exactly matching plane coefficients; 98,304 bounds cases
match both native box/plane and sphere/box decisions. Permanent Win32/Wasm
tests cover camera exclusion, gaps, near/far contact, inclusive omni tangency,
near-plane offset, radius changes and empty/invalid output. This is predicate
and draw-range evidence, not execution of native's full BSP traversal/sorting.
All 41 native/40 Wasm and 44 production/10 smoke/59 remainder tests pass
(nine retail skips), along with native SP and both browser builds.
The production Wasm is 3,732,023 B, SHA-256
`fffde0591809c09528af5bee3db94bf39b8a053ff947a4e3f6255e1b03242e51`;
existing size budgets remain unchanged and fail. No frame-time gain is claimed.

The initial owned-scene rerun on Chrome 152.0.7977.77 failed before light
assertions with repeated FloatZ `GL_INVALID_OPERATION`. CDP identified ANGLE's
D3D11 dynamic pixel-executable compiler failure; shader translation showed
that 3D/cube/shadow sampler overloads became identical HLSL `uint,float3`
signatures. Unique GLSL helper names repair that boundary while preserving
the sampling expressions. The existing transient pixel test reproduces failure
before the fix and passes afterward on Chrome/D3D11. Five of eight focused
Chrome cases pass; three exact pixel assertions remain unchanged and fail
with one-byte differences in soft alpha and light/mip green output. No native
rounding equivalence is claimed. Temporary inspection hooks were confined to
the ignored diagnostic site and removed before final tests.

The subsequent owned run reaches lighting, shadow toggling and real context
recovery. Its original final clear comparison failed by 0.2043 against a 0.1
threshold because the fixture's explicit 500 ms frame pump changed the
character pose and HUD. The fixture now proves clearing at the original pose
before recovery, then captures a new unlit baseline after that deliberate
advance. The same 0.1 threshold and both recovery-light checks remain.
The corrected test passes on Chrome/D3D11 in 37.5 seconds, including native
attenuation-image sampling, light limits and clearing at both paused poses.
No movement input, objective manipulation or mission completion was performed.

Static-model receivers now use each canonical instance's original bounds and
camera DPVS identity after LOD packing. Native and web share the omni sphere
test and spot plane test. The backend rebuilds its existing per-pass mask,
then draws contiguous selected runs from the same GPU instance buffer. Shadow
passes overwrite that scratch with their own selection before using it.
Native/Wasm checks exercise reordered canonical IDs, camera exclusion,
near-plane contact, moving lights, invalid IDs and failed-mask clearing.
This establishes the receiver predicate/packing boundary; native BSP-tree
traversal and complete draw-order/stencil equivalence are not claimed.
Native SP and both Release browser builds pass, as do all 41 native/40 Wasm
tests and 44 production/10 smoke/59 remainder cases (nine retail skips).
The owned Chrome/D3D11 lighting/shadow/clear/context-recovery test passes
in 45.8 seconds. The current Wasm is 3,733,567 B, SHA-256
`f0253c02161966888764795b3a50f3cf49365d8cbbb6bbe32657bef0b46cbe16`;
the unchanged size gate fails, and the measured proposal remains unapproved.

Rigid DObj, FX-model and DynEntity-model receivers now preserve native sphere
ownership at the draw boundary: pose origin/XModel radius, scaled FX radius,
and DynEntityPose origin/radius respectively. Native and browser share the
sphere/spot-plane test, and omni selection retains the native radius-sum
comparison. Tests distinguish scaled XModel radius from the dynamic pose radius,
verify rigid-versus-animated DObj classification/reset, and cover tangent,
outside-near/far-plane and outside-radius cases. Synthetic WebGL pixels exercise
the actual material-pass rejection and tangent acceptance for spot and omni.
No geometry-derived sphere or persistent receiver cache is introduced.
Native SP and both Release browser builds pass; all 41 native/40 Wasm,
44 production/10 smoke/59 remainder cases pass (nine retail skips).
Chrome 152.0.7977.77/D3D11 passes the sphere material pixels and the owned
Killhouse lighting/shadow/clear/context-recovery check (44.6 seconds).
The final Wasm is 3,736,437 B, SHA-256
`94bcbd4dffe6e51d395eee87bb53b9704a3f09e19e37770e2f608ae4737f99d9`.
The unchanged size gate fails; the architecture's measured proposal remains
unapproved. These checks establish selection and recovery, not authored
visual fidelity, performance improvement or manual gameplay acceptance.

Scene/DynEntity brush receivers now copy canonical `GfxBrushModel::writable`
world bounds into each frame's existing instance command. The cgame and
DynEntity owners continue updating those bounds. The material pass uses the
shared box/spot-plane or box/omni-sphere predicate; receiver selection neither
transforms the world bounds again nor derives them from retained geometry.
Native/Wasm tests cover moved writable bounds, world-space copying, strict
outside spot contact, inclusive omni contact and atomic rejection of invalid
bounds. Four synthetic material-pixel cases exercise the actual brush filter.

Animated-DObj receivers now form the same selected-LOD part mask before pose
evaluation as native `DObjGetSurfaces`. Native and browser share the extracted
`XBoneInfo`/pose AABB transform, including coefficient sign selection,
accumulation order and renderer `viewOffset`. Every emitted surface receives
the unioned scene-entity box. Native/Wasm tests cover a rotated selected bone,
mask forwarding, view offset, atomic invalid-data rejection and 4,096 exact
scalar-order cases. Four synthetic Chrome material pixels cover outside and
contact behavior for both spot and omni lights.
Native SP and both Release browser builds pass, as do 41 native/40 Wasm,
44 production/10 smoke/59 remainder tests (nine retail skips). Chrome
152.0.7977.77/D3D11 passes all receiver material pixels and the owned
Killhouse lighting/shadow/clear/context-recovery check in 41.4 seconds. The
first owned attempt captured its baseline before an authored paused objective
overlay reached the renderer; saved captures identified the adjacent-frame
fixture race. The fixture now requires two baseline captures to agree within
the existing 0.1 threshold before injection. The light assertions are
unchanged, and the corrected run passes.

The final Wasm is 3,741,616 B, SHA-256
`deef0e2cf1aa0c750c761ef1514a352a6b96be341a7b6c8125d94abb0a2b9d1d`.
The unchanged size gate fails and the measured proposal remains unapproved.
These checks establish bounds selection and recovery, not authored visual
fidelity, performance improvement or manual gameplay acceptance.

All known receiver families now have canonical bounds selection. The frontend
computes camera DPVS before dynamic assembly, then rejects DObjs, scene and
DynEntity brushes, and DynEntity models unless their canonical sphere/box
touches a portal-visible cell and survives the native camera-plane predicate.
FX models use native's direct sphere/frustum test. Static-model mark expansion
now consumes the same camera mask instead of an all-visible scratch array.
Native still applies the portal-clipped plane set for each individual cell and
retests animated DObjs with their updated pose box. The DObj command builder now
performs that second camera-box rejection after canonical pose evaluation and
before skinning. Linked DObjs and scene brushes now consume the per-cell
portal-clipped plane dispatch as described below. DynEntity now uses canonical
cell links and full cell planes; animated post-pose per-cell bounds admission
is also implemented below.

The backend now constructs one receiver command list per light from BSP ranges,
static-model instance runs and the eligible dynamic model/brush families. It
excludes code meshes, marks, particle clouds and sun billboards, matching the
native point-light list's scene-family boundary. Each retained material key
carries native surface type 0 (BSP), 2/5 (static rigid/skinned), 6 (brush) or
7/8/9 (XModel rigid/rigid-skinned/skinned). The complete list uses the exact
`R_ReverseSortDrawSurfs` transformation: complement packed bits 54-59 and sort
the resulting 64-bit value ascending. This preserves descending primary-sort
bands, then the remaining packed fields in native high-to-low bit order:
surface type, primary light, prepass, material, custom index, reflection probe
and object ID. The existing destination-alpha receiver pass consumes that
order.

Non-BSP receiver keys now use the material's own primary-light and reflection
fields. Previously the backend reused its camera key, overwriting both fields
from the instance: two otherwise ordered materials could reverse when the
first instance had a higher primary-light index. Native `R_AddBModelSurfaces`,
`R_AddXModelSurfaces`, `R_AddDObjSurfaces` and static-model light construction
instead inherit the material, then set surface type, object ID and DObj depth
hack. These native callers and the browser model/brush receiver list now share
that construction. BSP keys retain the world surface-material policy; camera
ordering is unchanged. The existing native/Wasm oracle covers 1,024 packed
inputs for each of six receiver surface types, preserves untouched fields,
checks depth-hack wrapping and reproduces the camera-light ordering reversal.
The native SP reference and both Release browser builds pass after this fix;
native CTest passes 41/41 and Wasm CTest 40/40. Production browser tests pass
44/44, smoke 10/10, and remainder 59 with nine optional retail skips. The owned
Chrome light/shadow/context-recovery check passes in 56.4 seconds. This verifies
the changed boundary and the checked rendering path, not matched-scene fidelity
or manual gameplay. The measured production artifact below includes this fix;
its approved size gate still fails, and no budget was changed.

A high/low-word oracle matches 4,096 deterministic packed keys on native and
Wasm. Producer tests cover every emitted surface type, including a static LOD
with a skipped deformed sibling, and reject nonreceiver scene kinds. The rebuilt
diagnostics renderer compiles and its Chromium transient-light pixel suite
passes. The production build and all 44 product browser tests pass.

Transient spot-shadow caster selection now follows the native family-specific
paths instead of the earlier projection-matrix approximation. BSP surfaces use
the exact shifted spot-light planes and deliberately ignore camera visibility,
matching `R_AllowBspSpotLightShadows`. Static-model casters reuse the camera and
light receiver mask from native's receiver callback. Scene/DynEntity DObjs,
XModels and brushes reuse the same exact sphere, posed-box or writable-box light
predicate as their receiver pass after camera/cell admission, including DObj
attachment and render-flag exclusions. `r_spotLightSModelShadows` and
`r_spotLightEntityShadows` independently suppress their native child families.
Sun cascades retain matrix partitioning and primary spots retain authored
`GfxShadowGeometry` membership. A separate spot-caster bit preserves build-
shadowmap state without borrowing the static sun game flag or BSP sun bitset.

Permanent tests distinguish a camera-hidden BSP caster from its excluded
receiver, distinguish exact shifted-plane rejection from a broad projection
box, verify static receiver-mask reuse, invalid/omni atomic clearing, and prove
a static material can cast a transient spot shadow while its sun flag is clear.
The final clean builds and suites pass: native SP, production and diagnostics
Release; 41/41 native CTest; 40/40 Wasm CTest; static checks; 44/44 production,
10/10 smoke and 59 remainder browser cases with nine optional retail skips.
The owned Chrome transient-light/shadow/clear/context-recovery check passes in
55.4 seconds. This exercises the changed path but is not a matched authored-
scene or manual-gameplay comparison.

Production size is 3,757,917 B Wasm, 770,681 B aggregate JavaScript and
4,627,078 B total (gzip: 1,499,684 B Wasm and 1,775,371 B summed site files),
SHA-256
`4b1279539515f3c9e9236a2fdc053ac6b8e22c275b5ee3528bb5fad75b8c93db`.
The unchanged approved budgets still fail. Applying the existing measured 5%
proposal to this artifact gives 3,945,813 B Wasm, 809,216 B JavaScript and
4,858,432 B total; this remains a proposal, not a budget change.

Native draw-surface object IDs are not retained at the portable boundary, so
equal packed keys use stable frontend order rather than claiming exact native
low-bit tie order. That equal-key ordering detail and matched-scene measurement
remain open, particularly for overlapping
translucent geometry and emissive materials that read destination alpha.
Native dynamic model, DObj and brush IDs are word offsets into transient
`frontEndDataOut` surface allocations. The browser deliberately does not build
that parallel native draw buffer, so assigning synthetic portable IDs would not
prove the cross-family order and would create another renderer representation.
Retain an ID only when the native surface packer crosses the shared boundary or
a matched scene demonstrates a tie defect that supplies an exact smaller seam.

The camera-admission continuation shares the native non-positive tangent rule
for spheres and boxes plus a bounded BSP-cell overlap walk. Native and Wasm
tests cover front, back, split and invisible leaves. The diagnostics renderer
and runtime-prefix check pass, as do the synthetic Chromium transient-material
pixels and the owned Chrome 152/D3D11 paused Killhouse
light/shadow/clear/context-recovery check (43.6 seconds). This is camera
admission and bounded render evidence; authored appearance and manual gameplay
acceptance remain unverified.

The post-pose continuation passes the camera planes into the canonical DObj
builder. Animated scene entities now union the selected posed-bone bounds and
reject an outside or tangent box before any skinning geometry is emitted. Native
and Wasm tests cover inside, outside, tangent and malformed plane input while
preserving the prior command on a culled or invalid replacement. At this
handoff, native CTest is 41/41, Wasm CTest is 40/40, the production browser tier
is 44/44, smoke is 10/10 and the retail-free remainder tier is 59 passed with
nine optional retail skips.

The linked-portal continuation restores the missing cell-filter half of
`R_LinkDObjEntity` and `R_LinkBModelEntity`, plus unlink and snapshot clearing.
The world retains its canonical two-bank `sceneEntCellBits` layout, including
128 words per cell and the configured local-client offset. Bounded helpers
call the real native `BoxOnPlaneSide` routine and collect cell membership into
128 bytes of temporary scratch before replacing a link. Invalid node ranges,
planes or traversal limits leave the previous bits intact and mark the browser
link unavailable for portal rejection. A successful walk into solid space
returns unlinked, preserving native's unlinked fallback distinction.

The static camera walk now offers a synchronous observer at its existing cell
dispatch. The browser uses it to admit linked DObj spheres and scene-brush
boxes against `planes + frustumPlaneCount`, combining repeated portal paths.
The cgame link radius, including movement tolerance, is retained for that
sphere test. Queued portals use a fresh clipped set with a zero frustum prefix;
the camera cell uses the complete camera set as its prefix. Synthetic tests
prove that a sphere inside the camera but beside the portal is rejected by the
clipped set, and cover packed one-word leaves, bank replacement, client-row
isolation, solid leaves, unlink and failed-walk rollback. Native `BOOL` in
`BoxOnPlaneSide`'s definition was changed to `int` to match its existing public
declaration without relying on Windows header inclusion; arithmetic is unchanged.

The owned Chrome 152/D3D11 Killhouse rendering check passes in 44.4 seconds,
including shadow toggling, light clearing and context recovery. Its saved
first-frame diagnostic reports `valid=1`, 44 linked DObjs with nine admitted,
and 49 linked scene brushes with seven admitted. This confirms execution of the
new link/portal path; it does not qualify authored appearance or progression.

The subsequent frame-order correction moves DynEntity model/brush draw
construction ahead of `FX_RunPhysics` and `DynEntCl_ProcessEntities`, matching
native `R_RenderScene`, while retaining static/dynamic mark generation after
physics. Placements and receiver bounds are copied before their canonical
owners advance; no pose cache is added. Effects preparation time is excluded
from the model-build timer. DObj lighting now follows post-pose rejection.
A synthetic native/Wasm regression verifies that a culled animated DObj does
not change its lighting handle or disable a visible DObj's light-grid atlas,
including a rejected submission with a nonfinite lighting origin. The former
ordering fails the handle assertion; the corrected ordering passes both cases.
Native 41/41, Wasm 40/40, product 44/44, smoke 10/10 and retail-free remainder
59 passed/nine skipped all pass. The owned Chrome lighting/shadow/recovery
check passes in 49.6 seconds. Its first-frame portal report is `valid=1`,
63/63 linked/admitted DObjs and 49/49 linked/admitted brushes; this invocation
does not demonstrate portal rejection. An initial remainder invocation was
stopped after an inherited retail-data setting caused a retail setup failure;
the result above comes from the corrected retail-free run. Moving-entity and
mark alignment, authored appearance and manual gameplay remain unverified.
The previous production artifact grows by 3,943 B Wasm and 16 B JavaScript.
The current measured artifact and unchanged budget failure are listed above.
After this change the native SP reference builds, native CTest passes 41/41,
Wasm CTest 40/40, production browser tests 44/44, smoke 10/10, and the routine
remainder passes 59 with nine optional retail skips. Both browser builds and
runtime-prefix checks pass; `git diff --check` is clean. The size checker passes
artifact/export/diagnostic separation before failing the unchanged Wasm budget.

The encountered `l_omni_*`/`l_spot_*` base, normal and detail variants are
supported; unrecognized shader families or missing retained maps are skipped.
Original Steam/native/browser authored-light timing, all material families,
transparency and special vision still need comparative execution. No campaign
classification changes. Full offline SP fidelity remains the active goal.


DynEntity camera admission now uses the canonical world-owned model/brush
cell-bit banks and camera visibility bytes. Native `R_FilterDynEntIntoCells`,
unlink/init, and model/brush cell dispatch call the same bounded helpers as the
browser. The shared BSP walk calls `BoxOnPlaneSide` and replaces links only
after a complete valid traversal. Each bank preserves its own word stride and
MSB-first entity IDs. Cell culling tests the full plane set, admits linked
entities with zero planes, and preserves admission from any repeated portal
path. The browser resets only camera visibility each view, consumes it before
DynEntity command expansion and physics, and reports recoverable errors for
invalid storage, collision/world count mismatches or malformed linked data.
The former approximate DynEntity BSP/camera fallback is removed.

Synthetic native/Wasm checks cover cross-word IDs, separate bank strides,
front/back/split relinking, atomic invalid-walk rollback, unlink/reset, queued
portal rejection, full-plane tangent rejection, repeated-path OR, padding-bit
and brush-index rejection. An independent 1,024-case scalar oracle compares
both original native sphere and box cull loops. The native SP reference and
Release production/diagnostics builds pass, as do native 41/41, Wasm 40/40,
production 44/44, smoke 10/10 and remainder 59 passed/nine optional skips.
The owned Killhouse check observes 118 DynEntity models (zero admitted on the
first frame). An initial positive-model assertion failed because the authored
paused camera does not consistently contain a DynEntity. The fixture now
selects an existing linked model and overrides only a local copy of the renderer
view; canonical player position, poses, scripts and camera ownership remain
unchanged. The positive-rendering assertion is retained and passes in Chrome
152/D3D11 (46.1 seconds), along with the lighting/shadow/context-recovery checks.
The renderer view override is diagnostics-only and is disabled in a `finally`
block. A query confirms that a new frame appended the selected entity's batch;
the test cannot pass solely from a prior authored-camera log entry. This proves model-path execution and recovery, not authored placement
or manual gameplay. This fixture has zero DynEntity brushes, so the brush path
has synthetic native/Wasm evidence only. Matched-scene appearance and manual
gameplay remain unverified.


Linked animated DObjs now complete the native post-pose camera-cell test.
After the global camera sphere and cell inner-plane sphere pass, the existing
synchronous cell observer evaluates canonical LOD bones, tests the resulting
box against the full cell plane set, and verifies exact BSP membership before
admission. Native `R_BoundsInCell` and the browser share the bounded BSP walk
used for cell links, including native `BoxOnPlaneSide` arithmetic and axial
splitting. Invalid traversal leaves query output unchanged and triggers a
recoverable error; a valid miss rejects only that path. Multiple paths retain
OR semantics. The observer and command builder share pose selection, relying
on `CG_DObjCalcPose`'s existing per-frame skeleton reuse; no extra portal-plane
buffer or pose cache was added.

The trace also found missing canonical `cullIn` updates. The browser now calls
`CG_UsedDObjCalcPose` after pose evaluation and `CG_CullIn` on admission. Native
and Wasm tests compile the real utility functions and verify evaluated-but-out
state 1, visible state 2, preservation of state 2 on later rejection, unchanged
lighting handles during visibility-only work, and floating-origin bounds.
BSP tests cover front/back/split membership and invalid-walk output rollback.
The native SP reference, native CTest 41/41, Wasm CTest 40/40, production 44/44,
smoke 10/10 and remainder 59 passed/nine optional skips pass.

One owned run failed before map rendering: while builds and browser suites ran
concurrently, the mount host's 15-second request timeout terminated a Worker
that was still publishing progress through `common.ff`. Its boot log is saved
under ignored `build/parity-audit/dobjcell-mount-timeout.txt`. This is a mount
liveness/deadline issue, separate from post-pose rendering. The subsequent fix
reports successful synchronous native-mount reads through the filesystem
adapter, renewing only the host stall deadline. Diagnostics retains 15 seconds
without progress, production retains 30 seconds, and both cap each filesystem
operation at five minutes regardless of progress. Shared request validation
rejects duplicate, malformed, wrong-operation and retired progress. Tests also
verify that the read observer is removed on success/failure and that timeout
cleanup retains filesystem leases until Worker ownership is resolved.

With this fix, the owned Chrome check passes in 55.4 seconds while production,
smoke and remainder suites run concurrently. Full Node checks pass 101/101,
static checks and both Release builds pass; browser results are production
44/44, smoke 10/10 and remainder 59 passed/nine optional skips. Native/Wasm
CTest results above predate this JavaScript-only fix and were not rerun.

The isolated owned Chrome 152/D3D11 Killhouse check passes in 47.2 seconds,
including light/shadow toggles, context recovery and the positive DynEntity
renderer fixture. Its post-pose diagnostic must report at least one evaluated
DObj, so this check does not pass solely on an empty scene path. This is bounded
runtime evidence; authored animation/visibility and manual gameplay remain
unverified. Production size is measured above, and the approved size gate
still fails without any budget change.
