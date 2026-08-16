# KisakCOD Browser Port

## Mission

This repository is for porting [SwagSoftware/KisakCOD](https://github.com/SwagSoftware/KisakCOD), the GPL-3.0 C++20 reimplementation of Call of Duty 4, to modern web browsers.

Treat this as a platform port, not a source-to-Wasm conversion exercise. Keep portable game and engine behavior close to upstream while placing browser-specific rendering, input, storage, audio, timing, and networking behind explicit platform boundaries.

The repository is based on upstream commit `1c03702cbe176e9274e486d295edcd035b3c2b5f` with full upstream history. The first isolated `KisakCOD-web` bootstrap compiles ODE physics math, creates a WebGL2 shader pipeline, and runs a browser frame loop without retail assets.

## Legal and asset boundaries

- KisakCOD is GPL-3.0. Preserve its license, copyright notices, source availability obligations, and attribution when importing or modifying upstream code.
- Never commit, package, host, or fetch proprietary Call of Duty 4 game data, Steam components, CD keys, or the Bink and Miles binaries from the original installation.
- Browser users must provide files from a legally owned installation. Asset import, validation, and local persistence belong in the product; copyrighted assets do not belong in this repository or its test fixtures.
- Use synthetic or freely licensed fixtures in automated tests. Document their origin and license.
- Do not suggest unofficial game downloads or attempt to bypass ownership or authentication checks.

## Upstream baseline

Upstream currently uses CMake and C++20 and builds separate multiplayer, single-player, dedicated-server, and Radiant targets. Its root configuration defaults to `win32`; the client includes a Direct3D 9 renderer and Windows platform code. Native Bink, Miles, and Steam DLLs cannot be linked into WebAssembly. ODE, Speex, zlib, shared game code, and the existing optional OpenAL backend are better portability candidates.

When importing upstream:

- Preserve upstream history where practical and record the exact commit used as the initial baseline.
- Establish and document a working native build before large browser-only changes, when the required legal game files and native toolchain are available.
- Keep upstreamable correctness fixes separate from browser adaptation work.
- Prefer adding a `web` platform next to the existing platform implementations over scattering `__EMSCRIPTEN__` conditionals through shared code.
- Do not perform broad mechanical rewrites before a compile inventory identifies the actual blockers.

## Porting strategy

Use an offline single-player client as the first end-to-end target unless the user explicitly changes the product goal. Multiplayer adds a protocol boundary because browsers cannot open the raw UDP sockets used by COD4. Exclude the dedicated server and Radiant from initial Wasm builds.

Work in vertical milestones:

1. Import and pin upstream; capture licensing and a reproducible native baseline.
2. Add an Emscripten `web` target that compiles portable engine code while feature-gating the renderer, networking, Steam, Bink, voice, and other native-only integrations.
3. Boot in a served browser page with logging, timing, commands, memory initialization, and a non-blocking main loop.
4. Let the user select a local COD4 installation, validate required files, and persist them locally with a browser storage abstraction. Prove localization and one representative archive can be read before attempting bulk ingestion.
5. Replace the Direct3D renderer behind a renderer boundary. Prove canvas creation, clear, triangle, texture upload, and one map surface in that order. WebGL2 is the compatibility-first default for the first slice; choose WebGPU only through an explicit design decision backed by a shader and feature inventory.
6. Add pointer-lock mouse input, keyboard/gamepad input, audio unlock/resume, save data, and enough UI/gameplay for a playable offline slice.
7. Address cinematics, streaming, performance, threading, and memory budgets with measurements.
8. Add multiplayer only with a documented WebSocket/WebTransport gateway or compatible server-side transport adaptation. Never imply that browser code can directly use COD4 UDP.

Keep each milestone runnable. A small browser boot that is continuously tested is more useful than a large branch that only compiles after every native subsystem has been rewritten.

## Engine convergence direction

Treat upstream KisakCOD/IW3 structures and behavior as canonical. The browser
port must converge toward the real database, asset, client, game, collision,
animation, and script systems rather than grow a parallel browser-specific IW3
engine.

The target data flow is:

```text
COD4 fastfile
     -> Kisak database/asset loader
     -> Kisak XAsset / XModel / Material / GfxWorld
     -> Kisak client and game systems
     -> renderer frontend
     -> portable draw commands
     -> WebGL2 backend
```

- Keep `XModel` as `XModel`, `Material` as `Material`, and `GfxWorld` as
  `GfxWorld`. Translate them only at a genuine platform boundary, such as the
  renderer backend or browser storage API.
- Treat the current `Retail*`, extracted-surface, census, and preview records as
  temporary validation scaffolding. Do not make them the permanent engine
  object model or add unrelated gameplay behavior to them.
- Preserve native DB stream, allocation-block, pointer-alias, dependency-order,
  and atomic-publication semantics wherever practical. Continue through every
  required pre-world asset family; never seek directly to `GfxWorld` when
  earlier stream state is required.
- Once a correctly published `GfxWorld` can render enough real Killhouse
  geometry to prove the frontend/backend seam, pause renderer expansion. Pivot
  to compiling and integrating substantially more real Kisak runtime code,
  prioritizing `Com_Init`, `DB_LoadXZone`, `CL_Init`, `CG_Init`, `SV_Init`,
  `Scr_Init`, `CM_LoadMap`, game, cgame, xanim, collision, and the script VM.
  Do not turn the bootstrap into a polished standalone asset viewer.
- Browser asynchrony belongs at the platform boundary. Keep portable engine
  operations synchronous-looking. The preferred long-term shape is a DOM and
  file-picker host on the main thread with KisakCOD Wasm, a synchronous-style
  filesystem, and OffscreenCanvas/WebGL2 in a dedicated Worker. Do not enable
  pthreads without profiling evidence and a documented deployment decision.
- Before adding a browser-only intermediate representation, identify the
  genuine platform boundary that requires it and document its retirement or
  ownership. If no such boundary exists, prefer compiling or adapting the
  Kisak implementation.

Maintain `docs/web-port-convergence.md` as a current inventory of shared,
modified, platform, temporary, native-only, and uncompiled systems. Every
substantial milestone should move at least one relevant system toward shared
Kisak behavior or explain why a platform-owned implementation is permanent.

## Browser platform rules

- Main loop: do not block the browser event loop. Use the Emscripten main-loop API or an equally explicit frame pump. Avoid Asyncify unless a measured, documented need justifies its cost.
- Rendering: isolate `gfx_d3d` concepts from Direct3D objects before translating them. Do not expose WebGL/WebGPU handles throughout shared game code.
- Files: keep engine filesystem calls behind an adapter. Separate temporary packaged files from user-imported and persistent data. Treat File System Access API support as optional; provide a portable fallback such as OPFS/IndexedDB.
- Audio: start from the OpenAL-capable path where feasible, but handle browser autoplay policy and audio-context suspension explicitly.
- Video: feature-gate Bink and replace it with a browser-compatible path or a graceful omission. Do not ship the native codec DLL.
- Networking: browser clients have no raw sockets. Keep transport framing independent from the engine protocol so a relay can be tested separately.
- Threads: begin single-threaded where possible. Wasm pthreads require shared memory and cross-origin isolation headers; enabling them is a deployment decision as well as a compiler flag.
- Data model: audit 32-bit assumptions, pointer/integer casts, packing, endianness, inline assembly, SIMD, filesystem case sensitivity, and synchronous I/O. Use fixed-width types at serialized boundaries.
- JavaScript boundary: keep bindings narrow, typed, and owned by the platform layer. Avoid a frontend framework until the launcher or asset workflow demonstrates a need for one.
- Security: treat imported assets, server packets, console input, and archive metadata as untrusted. Add bounds checks and avoid exposing arbitrary host paths or eval-like bridges.

## Build and dependency conventions

- Use reproducible, pinned Emscripten and JavaScript tool versions. Record them in versioned configuration rather than relying on a developer's global environment.
- Use out-of-tree build directories such as `build/web`; never commit CMake output, Wasm build products, imported retail assets, caches, or credentials.
- Keep native and browser target selection explicit. Do not make the web toolchain silently change native target behavior.
- Prefer source-built, browser-compatible dependencies with licenses suitable for GPL-3.0 distribution. Record replacements for every removed native library.
- Serve browser builds over HTTP for testing; do not rely on `file://` behavior.
- Bootstrap the local pinned toolchain with `tools/bootstrap_web_toolchain.ps1`.
- Build the debug Wasm target with `tools/build_web.ps1`.
- Serve only the generated site with `python tools/serve_web.py --directory build/web/site`.
- Run the tagged browser smoke suite with `npm.cmd run test:browser` after
  `npm.cmd ci` and browser installation. Run `npm.cmd run test:browser:full`
  for substantial browser-platform milestones. The intentionally redundant
  `npm.cmd run test:browser:all` tier is diagnostic-only.
- The toolchain lives under ignored `.tools/`; do not activate it permanently or commit it.

## Testing expectations

- Add host-native unit tests for portable parsers, serialization, math, command handling, and filesystem logic wherever they do not require the game runtime.
- Add browser tests for boot, persistent storage, input events, audio lifecycle, WebGL/WebGPU capability failures, and useful error messages for invalid asset selections.
- Expand differential tests between native Kisak and the web target. Feed the
  same synthetic input to both paths where practical and compare semantic
  traces such as asset type/name, stream position and block, pointer aliases,
  dependency order, nested counts, surfaces, material references, and texture
  metadata. Frame compatibility work as making the web result match native
  Kisak rather than independently specifying IW3 behavior.
- Test with synthetic malformed archives and network messages. Porting old native code into a browser increases the importance of fuzzing and strict bounds validation.
- For rendering work, retain a deterministic small scene or capture and compare it deliberately; tolerate only documented platform variance.
- Run the narrowest relevant checks while iterating, then the tagged browser
  smoke suite before ordinary handoffs. Run the curated full browser suite for
  substantial browser-platform milestones and release-facing changes. Direct
  native/Wasm parser tests are authoritative for cases tagged
  `@native-covered`; do not routinely rerun their browser duplicates. Report
  exactly what ran and what could not run.
- "Compiles," "boots," and "playable" are separate milestones. State which one was demonstrated and in which browser.

## Change discipline

- Inspect nearby upstream code and build lists before editing; source inclusion is largely controlled by CMake file inventories.
- Make the smallest coherent platform seam, then move one subsystem through it. Preserve native behavior unless a change is intentionally cross-platform.
- Keep generated files out of hand-written patches. Do not edit vendored or imported retail data.
- Preserve unrelated user changes and avoid destructive Git operations.
- Document architectural choices and browser limitations in the repository when they become real, especially renderer selection, asset storage, transport, memory sizing, and cross-origin isolation.
- Update this file when verified setup or test commands replace the bootstrap assumptions above.
