# Saved-screen renderer boundary — 2026-09-02

Working tree based on `a2d54b7d`. This proves framebuffer commands and material
arithmetic, not authored flashbang timing or campaign fidelity.

`CG_DrawShellShock*` remains the canonical effect owner. The four previously
empty `R_AddCmdSaveScreen*` / `R_AddCmdBlendSavedScreenShock*` frontend calls now
emit ordered capture/draw commands. The existing WebGL2 UI pass executes them
between surrounding draws. There is one GPU feedback texture and four native
timer slots, with no JavaScript effect state or CPU framebuffer copy.

Blur uses native scene-time subtraction, expiry, exponential fade capped at
0.99, and rounded byte alpha. Flash uses the canonical packed whiteout/grab
values and the loaded material's additive blend state. The saved texture uses
RGB8 because both effects consume RGB and WebGL rejects copying the opaque
default framebuffer into RGBA8. Captures also work from the scene composite.
Texture memory is included conservatively at four bytes per pixel. Map unload
and shutdown release it; resize and context loss invalidate history, and the
next effect captures the current view. This recovery policy is a browser
adaptation; it cannot recover pre-loss GPU pixels.

The owned installation's shader programs were inspected read-only in memory
with the system D3D disassembler. No shader binary or game data is included:

| Program | Bytecode SHA-256 | Arithmetic |
| --- | --- | --- |
| `shell_shock` sm3 | `61b91aa9ec0c83e6765a7460e1c70b4258731e12e1db09d6969f21f5596ea43b` | Saved RGB multiplied by vertex RGB, then 25% luminance desaturation using 0.299/0.587/0.114; vertex alpha. |
| `shell_shock_flashed` sm3 | `80a2aa43431859f5f90bebd8a78ff1c65d82740b03215c72e4432bb9fef3c521` | Saved RGB times vertex alpha plus vertex RGB; alpha one. |

Native Win32's section-save entry is itself unimplemented. The browser honors
the declared normalized rectangle, preserving pixels outside it. Synthetic
coverage of that operation is not evidence of original split-screen parity.

## Execution

- Pinned Emscripten 6.0.6 Release production and diagnostics builds and both
  canonical runtime-prefix checks passed.
- Static checks and all 81 protocol tests passed.
- Bundled headless Chromium 149.0.7827.55, diagnostic port 8137: smoke 12 passed;
  non-overlapping remainder 39 passed / 4 optional retail skips. The new
  synthetic saved-screen case checks capture order, top/bottom orientation,
  flash additive blending and whiteout, native blur cap/expiry/negative time,
  feedback, section saves, capture-only frames, unload, resize and context
  recovery. Pixel tolerance is two byte units for UNORM/blend rounding.
- Production port 8138: actual canonical mount-error and diagnostic-API-free
  boot tests 2 passed. Production API/size boundary passed: 3,296,036-byte Wasm,
  24 raw exports, 9 application exports. The test-only pixel command is absent.
- Separate headless Chrome 152.0.7977.65, diagnostic port 8139: owned retail
  material test 1 passed. A temporary persistent profile imported the local
  installation, loaded Killhouse through the canonical command, then proved
  capture-before-overlay and loaded-material feedback from the scene composite.
  The initial incognito attempt failed in import before mounting; the existing
  persistent retail fixture resolved it. No importer compatibility claim follows
  from the failed incognito attempt.
- Synthetic runs explicitly cleared inherited retail variables and used
  isolated servers. No exhaustive duplicate suite was run.

Tested Wasm hashes: production
`10dfa51ebc6a9802e99eed150c1cc3cdc3efab2bf11e270c71a312d20ee02653`;
diagnostics `b85cee2ebd4b9833ffb384031300e22bd51bf2a09bb234d3e0f7df3f5009a978`.

Original Steam visual comparison, natural flashbang/shellshock timing in
gameplay, and native Kisak execution remain unverified. The retail check uses
synthetic color overlays on the real loaded world; it does not qualify mission
flow, combat, performance, or broader shader families.
