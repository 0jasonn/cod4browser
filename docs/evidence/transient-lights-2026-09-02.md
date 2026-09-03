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
  the existing shader/backend boundary. No new asset representation exists.
- Native `R_DrawPointLitSurfsCallback` clears destination alpha before each
  light. The encountered state `18128928/e0040048` uses
  `ONE_MINUS_DST_ALPHA`, so omitting that clear suppressed almost all light.
  The backend now clears alpha while preserving scene RGB and depth. This
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

## Remaining fidelity work

This is an unshadowed transient-light milestone. Native shadow intent is
retained, but transient spot shadow rendering is still absent. Primary world
spot shadows are a separate existing path. Native light-volume receiver lists,
scissor rectangles and full stencil ordering are not yet integrated: the
backend currently visits camera batches and clears the full view's alpha per
light. These differences require correction/measurement before fidelity or
performance promotion, particularly for overlapping translucent geometry and
emissive materials that read destination alpha.

The encountered `l_omni_*`/`l_spot_*` base, normal and detail variants are
supported; unrecognized shader families or missing retained maps are skipped.
Original Steam/native/browser authored-light timing, all material families,
transparency and special vision still need comparative execution. No campaign
classification changes. Full offline SP fidelity remains the active goal.
