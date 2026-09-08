# Transient FX light boundary

Consolidated 2026-09-09 from the implementation evidence beginning at `a2d54b7d`.
Canonical FX owns emission and lifetime; native `GfxLight`, entity, pose and
material identities remain authoritative. Browser code owns only the portable
renderer boundary. Qualification below names historical execution and its limits.

## Behavior and native reference

`r_dynamiclights_core.h` shares native light construction, importance selection
and projected tangent-sphere scissor math with `r_scene.cpp`/`r_light.cpp`.
Native precondition assertions remain. The frontend clears lights at
`R_ClearScene` and world unload, keeps the native 32-submission ceiling, applies
camera culling and `r_dlightLimit` (at most four lights), and suppresses
submission under `r_fullbright`. Spot origin/radius offset, cone, brightness,
added-light-zero identity and near-plane bias stay canonical.

Loaded material techniques 21/22 execute between lit and emissive camera regions.
Native state bits, base/detail/normal images, cone/exponent, diffuse scale,
attenuation and vertex fog feed the existing backend. The image bank retains
`light_dynamic` and its recovery source. Recognized `l_omni_*`/`l_spot_*` base,
normal and detail families render; unsupported families or missing retained maps
are skipped. Feature-set selection follows native's 20-token policy, including
leading-comma aliases and fallback to the source when a variant is absent.

Native `R_DrawPointLitSurfsCallback` clears destination alpha per light. The
encountered state `18128928/e0040048` uses `ONE_MINUS_DST_ALPHA`; omitting that
clear suppresses illumination. The backend preserves RGB/depth while clearing
alpha, and applies the native tangent-sphere scissor to both clear and receiver
draws. Coverage is rejected within a light and accumulates across lights. Pixel
checks use RGBA8 scene targets; an opaque browser framebuffer cannot qualify
this destination-alpha behavior.

Owned shaders were inspected read-only in memory with system D3DDisassemble.
No proprietary bytecode, images, fastfiles or screenshots are committed. The
[pinned Steam inventory](steam-reference-2026-09-02.json) identifies the installation.

| Shader | Bytecode SHA-256 |
| --- | --- |
| `l_omni_r0c0.hlsl` | `9d56cce09285c41f9c65e5649612141df21e7f279b181c26cb6a22f76ff4eed3` |
| `l_spot_r0c0.hlsl` | `05a8c13112ce064a517a373562130638f82834ca47931535ab58e9e9d79b800f` |
| `l_omni_t0c0n0.hlsl` | `b654199f143cd15c96088d66321aaca1cbef03d2be8420abeae773792fbd8081` |
| `l_spot_t0c0n0.hlsl` | `f12f925b5fd5a53864e11feae01ce9244f267f0e52014dcfc52a19352fb6ea5f` |
| `l_omni_b0c0.hlsl` | `56247a61f54dceade56643c858c7b4fa195b777ffb88ce5dabbfc7101321dcdc` |
| `lp_omni_tc0_dtex_sm2.hlsl` (vertex) | `f8eb4546d7a05716bb7f073ff08079c1ef2bf8f62da18043c8fc43d8a990af80` |

## Receiver bounds and ordering

Receiver predicates use canonical bounds rather than geometry-derived estimates.
Native `R_CalcSpotLightPlanes`, `R_ComputeSpotLightCrossDirs`, `R_BoxInPlanes`
and the BSP omni callback share the portable math. Bounds copying, invalid-input
rejection and command publication remain atomic.

| Family | Canonical receiver boundary |
| --- | --- |
| BSP | Camera DPVS plus original per-surface AABBs; exact shifted spot planes or omni sphere/box test before adjacent index ranges merge. |
| Static XModels | Original instance bounds and canonical IDs after LOD packing. A per-pass mask draws contiguous selected runs from the existing instance buffer; shadow passes overwrite it with their own selection. |
| Rigid DObj / FX model / DynEntity model | Pose origin with XModel radius, scaled FX radius, or DynEntityPose origin/radius respectively. Spot selection uses native sphere/plane math; omni uses the radius-sum rule. |
| Scene / DynEntity brush | Current `GfxBrushModel::writable` world bounds copied before physics advances. The receiver does not transform these bounds again. |
| Animated DObj | Selected-LOD bone mask before `CG_DObjCalcPose`; shared `XBoneInfo` AABB transform preserves coefficient signs, accumulation order and `viewOffset`. Each surface receives the unioned entity box. |

Spot receivers share native `R_SpotLightIsAttachedToDobj`: attached scene DObjs
and render flag 8 are excluded, with native's separate rigid single-model
eligibility preserved. Omni eligibility is unchanged.

One receiver list per light combines BSP, static and dynamic families and
excludes code meshes, marks, particle clouds and sun billboards. Surface types
are 0 (BSP), 2/5 (static rigid/skinned), 6 (brush), and 7/8/9 (XModel
rigid/rigid-skinned/skinned). `R_ReverseSortDrawSurfs` complements packed bits
54–59, then sorts the 64-bit key ascending. Remaining fields retain native
high-to-low order: surface type, primary light, prepass, material, custom index,
reflection probe and object ID.

Non-BSP keys inherit the material's primary-light/probe fields, then set surface
type, object ID and DObj depth hack, sharing construction with native model/brush
callers. Camera keys cannot substitute: instance light/probe overrides can
reverse receiver material order. BSP retains its world surface-material policy.

Native low-bit object IDs are word offsets into transient `frontEndDataOut`
allocations and do not cross the portable boundary. Equal keys therefore use
stable frontend order. Exact native tie order remains unqualified, especially
for overlapping translucent or destination-alpha emissive geometry. Do not build
a parallel native draw buffer or invent synthetic IDs; retain native IDs when
the packer crosses the shared boundary, or when a matched defect identifies a
smaller required seam.

## Camera and shadow ownership

Camera DPVS completes before dynamic assembly. Cgame maintains canonical
`sceneEntCellBits` banks, including 128 words per cell and local-client offsets.
The bounded shared BSP walk calls real `BoxOnPlaneSide`; link updates use 128
bytes of temporary scratch and replace bits only after a valid complete walk.
Invalid walks preserve prior bits and mark the link unavailable for rejection;
solid-space unlinking retains native's distinct fallback behavior.

A synchronous observer at native cell dispatch tests linked DObj spheres and
scene-brush boxes against the portal-clipped planes, retaining cgame's movement
radius. Queued portals have a fresh clipped set with zero frustum prefix;
the camera cell uses the complete camera set as its prefix. Repeated paths OR
admission. DynEntity uses its world-owned model/brush banks, individual word
strides and MSB-first IDs, full cell planes and the native zero-plane rule.
FX models use native direct sphere/frustum rejection. Static marks consume the
canonical camera mask; camera visibility is reset per view.

Animated DObjs then test selected posed-bone boxes against the full cell planes
and exact BSP membership through shared `R_BoundsInCell`. Sphere/box camera
predicates preserve native non-positive tangent rejection. Pose selection uses
`CG_DObjCalcPose`'s existing skeleton reuse; there is no extra pose/plane cache.
`CG_UsedDObjCalcPose` and `CG_CullIn` retain evaluated/visible status, including a
previous admission if a later path rejects. DObj lighting runs only after pose
visibility rejection. DynEntity model/brush commands precede physics, matching
native `R_RenderScene`; marks still expand afterward.

The first eligible transient spot competes with primary lights for the existing
four 512×512 shadow maps. Native shadow history owns admission, fades and
replacement; see [current convergence](../web-port-convergence.md#shared-renderer-and-transport-helpers).
BSP casters use exact shifted spot planes without camera DPVS. Static casters
reuse the native camera/light receiver mask. Dynamic model/brush casters use
their exact receiver predicates after camera/cell admission, including DObj
exclusions. `r_spotLightSModelShadows` and `r_spotLightEntityShadows` gate families
independently. A separate spot-caster bit carries shadow-map state; neither sun
flags nor BSP sun bitsets substitute. Sun cascades retain their matrix partitions
and primary spots retain authored `GfxShadowGeometry` membership.

## Execution and remaining qualification

Historical differential checks establish these bounded seams:

- 4,096 native spotlight directions produce 98,304 exactly matching plane
  coefficients; 98,304 native box/plane and sphere/box decisions agree.
- Selected-bone bounds match 4,096 scalar-order cases. Receiver keys match
  1,024 inputs for each of six types; reverse sorting matches 4,096 packed keys.
- DynEntity sphere and box cull loops each match 1,024 independent scalar cases.
  Native/Wasm checks also cover portal clipping, bank/client isolation,
  post-pose membership, unlink/reset, failed-walk rollback, invalid command
  publication, DObj pose-use flags and untouched lighting for culled models.
- Synthetic GPU pixels cover additive overlap, cone direction, alpha thresholds,
  blended alpha squared, attenuation, AG normals, fog, destination-alpha reset,
  receiver rejection/contact and recovery. Caster tests distinguish hidden BSP
  casters from receivers, exact spot planes from broad projection boxes, static
  receiver-mask reuse and independent sun/spot flags.

The final broad results recorded during these continuations were native SP and
both browser Release builds, 41/41 native and 40/40 Wasm CTests, 101/101 Node,
static checks, 44/44 production browser, 10/10 smoke and 59 remainder passes
with nine optional retail skips. Native/Wasm results predate the final
JavaScript-only watchdog repair; they were not rerun for that repair. Current
broader qualification belongs to the [test inventory](../web-test-inventory.md).

Historical owned Chrome 152/D3D11 Killhouse checks passed illumination,
32-submit/4-draw/zero limits, spot-shadow darkening versus `r_spotLightShadows 0`,
clearing and real WebGL context recovery. A portal run observed 44 linked DObjs
with nine admitted and 49 brushes with seven admitted. A separate fixture
requires positive DynEntity rendering through a diagnostics-only view of an
existing linked model; it neither moves the player nor advances scripts.
There are no DynEntity brushes in that fixture, so that family has synthetic
native/Wasm evidence only. The isolated post-pose check passed in 47.2 seconds;
a concurrent watchdog-repair check passed in 55.4 seconds.

Later shadow-history work admits the test spotlight while time advances before
pausing comparisons. That run passed lighting, shadows, clearing, recovery and
limits but failed the final positive DObj post-pose assertion. Complete current
fixture qualification remains unresolved; historical passes do not erase it.

Installed Chrome/D3D11 also exposed ANGLE translating overloaded sampler helpers
into colliding HLSL signatures. Unique helper names repaired the reproduced
first-draw failure without changing sampling. Three other unchanged exact-pixel
assertions recorded one-byte differences; default Chromium passes do not prove
native/browser rounding equivalence. A context-recovery comparison takes a fresh
unlit reference after its deliberate cgame frame advance, and baseline captures
must agree before light injection; pixel thresholds remain unchanged.

The mount-progress repair reports successful synchronous native reads to the
existing watchdog: diagnostic/production stall limits remain 15/30 seconds and
the absolute operation cap five minutes. Duplicate, malformed and unrelated
progress cannot extend a request; timeout cleanup retains filesystem leases
until Worker ownership resolves. Details belong to the
[platform protocol](../web-architecture.md#product-protocol).

Authored light timing/appearance, moving-entity and mark alignment, all material
families, transparency, special vision and manual gameplay remain unverified.
No campaign classification or frame-time improvement follows from these checks.
Repeated GPU spans sum into one stage result, but active gameplay profiling and
per-stage CPU attribution still require qualification.

Superseded per-edit results, artifact hashes and size-budget failures remain in
Git; none qualifies a later artifact. Retrieve the complete pre-consolidation
record without changing the checkout:

```powershell
git show 4bca1760f95edb60c362926fa944e96dbcae3f2a:docs/evidence/transient-lights-2026-09-02.md
```
