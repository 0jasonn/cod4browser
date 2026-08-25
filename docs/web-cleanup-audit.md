# Web product cleanup audit

## Baseline

The cleanup started from `web-port` commit
`1a9ad9981982ca0a173954fc784247092ff3348c` on 2026-08-25. The clean local
working branch is `codex/web-product-cleanup`.

Pinned and observed tools:

| Tool | Version |
| --- | --- |
| Emscripten | 6.0.6 (`ce75e06884093bcefb86a6b8fd56a5d62a4cc245`) |
| emsdk | `9981799f744be74ac67b1c1813ff172f63be0630` |
| CMake | 4.2.0-rc3 |
| Ninja | 1.13.2 |
| Node.js | 24.18.0 |
| npm | 11.16.0 |
| Playwright | 1.61.1 |
| Python | 3.14.6 |

The baseline was configured as Release with Ninja, the pinned Emscripten
toolchain, `KISAK_PLATFORM=web`, and `KISAK_PORTABLE_TESTS_ONLY=OFF`. There
was no diagnostics option. `KisakCOD-web` linked `kisak_web_gate2_oracle`
unconditionally.

## Baseline production artifact

The generated directory was not cleaned before the audit. `kisakcod-next.mjs`
was a stale, unconfigured file and is recorded because serving the directory
would still expose it.

| File | Bytes |
| --- | ---: |
| `asset_store.mjs` | 54,736 |
| `engine_worker_host.mjs` | 8,229 |
| `engine_worker.mjs` | 9,528 |
| `filesystem_bridge.mjs` | 8,064 |
| `index.html` | 4,851 |
| `kisakcod-next.mjs` (stale) | 196,449 |
| `kisakcod.mjs` | 186,104 |
| `kisakcod.wasm` | 3,730,562 |
| `launcher.mjs` | 47,103 |
| `styles.css` | 5,280 |
| `web_audio_driver.mjs` | 19,526 |
| `worker_sync_filesystem.mjs` | 23,939 |
| **Total served directory** | **4,294,371** |
| **JavaScript modules** | **553,678** |

`wasm-dis` reported 263 minified Wasm imports and 38 low-level Wasm exports.
The generated JavaScript exposed these 23 named web entry points:

```text
_KisakWeb_CancelArchiveJob
_KisakWeb_CancelQcommonRuntime
_KisakWeb_CancelRetailCensus
_KisakWeb_CanonicalFsFileSize
_KisakWeb_CanonicalFsListCount
_KisakWeb_CanonicalFsReadHash
_KisakWeb_CanonicalFsWriteRename
_KisakWeb_CompleteFsRead
_KisakWeb_CompleteFsStat
_KisakWeb_ProbeFastfileHeader
_KisakWeb_ProbeIwd
_KisakWeb_ProbeLocalization
_KisakWeb_QueueKeyEvent
_KisakWeb_QueueMouseMove
_KisakWeb_StartArchiveJob
_KisakWeb_StartCanonicalDbRuntimeCheck
_KisakWeb_StartQcommonRuntime
_KisakWeb_StartRetailCensus
_KisakWeb_SubmitCanonicalCommand
_KisakWeb_TestAudioProxyPcm
_KisakWeb_TestLoseWebGLContext
_KisakWeb_TestRestoreWebGLContext
_KisakWeb_TestSetAaSamples
```

The Release build and strict canonical runtime-prefix check passed. The
existing Emscripten portable-test build passed 37/37 tests. The first browser
smoke run passed 17/18 tests; `canonical_filesystem.spec.mjs` lost a newly
written home file across reload. The configured native-test directory was
incomplete: 7 tests ran successfully and 29 executables were absent, so it is
not native regression evidence. The browser remainder tier was not run before
the release-blocking persistence failure was addressed. No retail-data test
was run.

The baseline link map is `build/web/kisakcod-baseline.map` (111,470 bytes).
It is an ignored build artifact, not a source artifact.

## Source classification

Headers inherit the classification of their matching translation unit unless
noted otherwise.

| Classification | `src/web` units |
| --- | --- |
| Canonical adapter | `web_canonical_gfxworld.cpp`, `web_client_server_lifecycle.cpp`, `web_database_filesystem.cpp`, `web_engine_filesystem.cpp`, `web_renderer_frontend.cpp`, `web_system_files.cpp` |
| Browser platform implementation | `web_assertive.cpp`, `web_browser_bindings.cpp`, `web_cinematic.cpp`, `web_cooperative_scheduler.cpp`, `web_engine_scheduler.cpp`, `web_filesystem.cpp`, `web_openal_proxy.cpp`, `web_shader_compatibility.cpp`, `web_system.cpp`, `web_thread_context.cpp`, `web_worker_filesystem.cpp` |
| Production product code | `web_asset_probe.cpp`, `web_main.cpp` |
| Renderer backend/platform implementation | `web_renderer.cpp`, `web_renderer_code_mesh.cpp`, `web_renderer_dobj_lod.cpp`, `web_renderer_dobj_scene.cpp`, `web_renderer_fx_model_scene.cpp`, `web_renderer_lighting.cpp`, `web_renderer_mark_fragments.cpp`, `web_renderer_mark_mesh.cpp`, `web_renderer_particle_cloud_scene.cpp`, `web_renderer_static_model_scene.cpp`, `web_renderer_surface.cpp`, `web_renderer_world_scene.cpp` |
| Diagnostics | `web_archive_job.cpp`, `web_engine_asset.cpp`, `web_engine_surface.cpp`, `web_qcommon_preinit.cpp`, `web_qcommon_runtime.cpp`, `web_renderer_comparison.cpp` |
| Gate 2 diagnostic/oracle | `web_retail_census_job.cpp`, `web_retail_fastfile_census.cpp`, `web_retail_load_clipmap.cpp`, `web_retail_load_comworld.cpp`, `web_retail_load_gfxworld.cpp`, `web_retail_load_image.cpp`, `web_retail_load_lightdef.cpp`, `web_retail_load_weapon.cpp`, `web_sound_alias_catalog.cpp` |
| Temporary convergence code | `web_engine_world_surface.cpp`, `web_fastfile_source_stream.cpp`, `web_fastfile_world_surface.cpp`, `web_fastfile_zone_registry.cpp`, `web_fastfile_zone_stream.cpp` |
| Test support | `web_gate2_killhouse_oracle.h`, `web_renderer_surface_storage.h` |
| Apparently unreachable | None proven. Filename-based deletion is not justified. |

`web_canonical_gfxworld.cpp` remains a canonical publication adapter even
though its baseline map contribution is small. `web_renderer.cpp` currently
contains both the production WebGL2 backend and diagnostic comparison/test
controls; that mixed ownership is a cleanup target, not evidence that the
backend itself is diagnostic.

## Conditional-candidate reachability

Repository references were searched and the Release link map was inspected.
The counts below are literal source-path occurrences in the map; they are
reachability evidence, not call counts.

| Candidate | Map hits | Decision at baseline |
| --- | ---: | --- |
| `web_filesystem.cpp` | 15 | Retain as the active browser async filesystem adapter. |
| `web_fastfile_source_stream.cpp` | 11 | Move behind diagnostics with its proof pipeline; do not delete. |
| `web_fastfile_world_surface.cpp` | 41 | Move behind diagnostics with the synthetic surface path; do not delete. |
| `web_fastfile_zone_registry.cpp` | 36 | Preserve as temporary convergence evidence until canonical DB parity replaces its tests. |
| `web_fastfile_zone_stream.cpp` | 11 | Preserve with the temporary zone-stream diagnostic tests. |
| `web_engine_world_surface.cpp` | 23 | Move behind diagnostics with the converted-surface proof; do not delete. |
| `web_canonical_gfxworld.cpp` | 3 | Retain in production as the canonical `GfxWorld` publication boundary. |
| `web_renderer_comparison.cpp` | 14 | Isolate as diagnostics after its call sites are compile-gated. |
| `web_retail_fastfile_census.cpp` | 650 | Preserve only in Gate 2 diagnostics. |
| `web_sound_alias_catalog.cpp` | 15 | Preserve only with the Gate 2 oracle. |

No candidate is deleted by this audit. Production/diagnostic target separation
must prove the production link map no longer contains the isolated sources and
must keep their native/Wasm tests available.

## Cleanup priorities established by evidence

1. Make browser-home persistence awaitable and error-reporting; the baseline
   test demonstrated real data loss.
2. Enforce one writable-home owner across tabs.
3. Version and validate the host/Worker protocol and settle all requests on
   failures and shutdown.
4. Build production without Gate 2, proof jobs, test exports, generic Worker
   operations, monkey patches, and `filesystem_bridge.mjs`.
5. Preserve the isolated diagnostics as a separately built target/site before
   renderer ownership extraction.

Renderer decomposition, memory-budget changes, campaign expansion, and audio
budget changes are intentionally downstream of those correctness and artifact
boundaries.
