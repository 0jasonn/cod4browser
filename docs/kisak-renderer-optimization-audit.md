# Kisak renderer optimization audit

Updated 2026-09-01. This inventory defines the persistent rendering-optimization
goal against authoritative Kisak code. “Brought over” means shared Kisak logic
executes in the web target or the WebGL boundary owns an equivalent technique
with matching canonical inputs and measured validation. Direct3D calls and SMP
plumbing are not copied literally when WebGL2 or the single Worker requires a
different platform mechanism.

Status meanings:

- **Shared**: the web target executes the Kisak implementation.
- **Equivalent**: WebGL2 owns a measured platform form of the same technique.
- **Partial**: part of the native technique is present, with a named gap.
- **Open**: the relevant native technique is not yet represented.
- **Not applicable**: the technique exists only to manage a native API or an
  unsupported deployment mode.

| Kisak technique and authority | Web evidence | Status |
| --- | --- | --- |
| Cell/portal/frustum DPVS for world and static models (`r_dpvs_core.cpp`, `r_dpvs_static.cpp`) | Canonical DPVS runs from `web_renderer_frontend.cpp`; world spans and static canonical indices are consumed by `web_renderer.cpp`. | **Shared** |
| Camera LOD selection and static distance culling (`r_add_staticmodel.cpp`) | `WebRenderer_SelectStaticModelLod` and grouped first/second-half packing retain native LOD/cull inputs. | **Equivalent** |
| Static-model surface cache and grouped draws (`r_staticmodelcache.cpp`, `r_draw_staticmodel.cpp`) | Shared model geometry is retained once and drawn through instanced model/LOD/material batches. | **Equivalent** |
| Sorted draw surfaces and compatible material grouping (`r_drawsurf.cpp`) | World/static commands group compatible opaque geometry. Dynamic commands retain canonical append/depth-hack order; safe opaque reordering has not been audited exhaustively. | **Partial** |
| Dynamic vertex/index buffer pools (`r_buffers.cpp`) | Capacity-backed WebGL buffers and retained numeric staging avoid per-batch allocation and upload combined dynamic geometry once per submission. | **Equivalent** |
| DObj pose, lighting, skinning, and skinned-cache reuse (`r_dobj_skin.cpp`, `r_model_skin*.cpp`, `rb_tess.cpp`) | Canonical pose/lighting execute each frame; direct final-vertex emission and retained numeric capacity remove intermediate conversion without a browser pose cache. Cross-pass reuse is present for the shared skinned dynamic buffer. | **Equivalent** |
| Render/sampler/stream state suppression (`r_setstate_d3d.*`, `r_draw_bsp.cpp`) | Pass-local material, projection, feature, texture, shadow-alpha, shadow-cull, program, and geometry binding state suppress redundant GL calls with explicit reset boundaries. | **Equivalent** |
| BSP sun-cascade visibility (`r_add_bsp.cpp::R_AddAllBspDrawSurfacesRangeSunShadow`) | Web world sun drawing still submits every retained sun-casting range to both cascades; it does not consume `surfaceVisData[partition + 1]`. | **Open** |
| Static-model sun-cascade visibility (`r_add_staticmodel.cpp::R_AddAllStaticModelSurfacesRangeSunShadow`) | Canonical static AABBs produce independent near/far masks and contiguous instanced runs. | **Equivalent** |
| Dynamic scene-entity sun-cascade visibility (`r_dpvs.cpp::R_AddAllSceneEntSurfacesRangeSunShadow`) | Web dynamic sun drawing filters authored caster/material flags but currently submits eligible dynamic ranges to both cascades without native per-partition scene visibility. | **Open** |
| Authored BSP/static spot membership (`r_add_bsp.cpp`, `r_add_staticmodel.cpp`, `GfxWorld::shadowGeom`) | Authored world ranges are retained; `26b3dc98` builds one packed static mask per selected light and reuses instanced runs. | **Equivalent** |
| Dynamic scene-entity spot visibility (`r_dpvs.cpp::R_AddAllSceneEntSurfacesSpotShadow`) | The current spot path has no dynamic submission and no `sceneDObjVisData`/`sceneModelVisData` per-light equivalent. | **Open** |
| Shadow draw sorting and opaque range coalescing (`R_SortDrawSurfs`, grouped static lists) | Adjacent opaque world/dynamic sun ranges merge; static sun/spot instances form contiguous runs. Alpha-tested boundaries and authored order remain explicit. | **Equivalent** |
| Asynchronous sun-flare occlusion (`rb_sky.cpp`) | Double-buffered `GL_ANY_SAMPLES_PASSED_CONSERVATIVE` queries use availability polling and collision fallback. | **Equivalent** |
| Backend resource retention and device recovery | World/static/brush geometry, textures, image sources, and command metadata have explicit unload and WebGL context-recovery ownership. | **Equivalent** |
| Native renderer SMP/backend worker overlap | Kisak Wasm currently owns a single engine Worker and no pthread deployment decision exists. | **Not applicable** until profiling justifies pthreads and cross-origin isolation. |
| D3D9-specific declaration, lock flag, and COM state caches | WebGL VAOs, buffer capacity, binding state, and context restoration own these API boundaries. | **Not applicable** literally; covered by platform equivalents above. |

## Completion requirements

The broad optimization goal remains active while any applicable row is **Open**
or **Partial**. Completion requires:

1. BSP sun partitions consume canonical partition visibility before submission.
2. Dynamic sun and spot casters consume canonical per-view/per-light visibility,
   without deriving caster membership from camera DPVS.
3. The remaining dynamic draw-sort row is either implemented for a proven-safe
   opaque subset or closed with measurements showing no material benefit.
4. Each closure has focused semantic coverage, matched logical-work evidence,
   a production Release, current convergence documentation, and a pushed commit.

The next implementation target is BSP sun-cascade visibility because it is a
direct, high-volume native omission with existing canonical surface identities
and partition matrices. Dynamic shadow visibility follows after its frontend
producer boundary is identified.
