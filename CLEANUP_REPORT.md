# Web product cleanup report

## Scope and revisions

- Starting SHA: `1a9ad9981982ca0a173954fc784247092ff3348c`
- Ending implementation SHA: `a3a8004d207f144444f2fbf0dbaf51bd2446657b`
- Working branch: `codex/web-product-cleanup`
- Date: 2026-08-25

The ending SHA is the code/test endpoint immediately before this report-only
documentation commit. No remote branch was pushed or reset.

## Build environment

| Tool | Version |
| --- | --- |
| Emscripten | 6.0.6 (`ce75e06884093bcefb86a6b8fd56a5d62a4cc245`) |
| emsdk | `9981799f744be74ac67b1c1813ff172f63be0630` |
| CMake | 4.2.0-rc3 |
| Ninja | 1.13.2 |
| MSVC | 19.51.36256.0, Visual Studio 18 2026 |
| Node.js / npm | 24.18.0 / 11.16.0 |
| Playwright | 1.61.1, Chromium |
| Python | 3.14.6 |

Production was configured as Release with the pinned Emscripten toolchain,
Ninja, `KISAK_PLATFORM=web`, `KISAK_PORTABLE_TESTS_ONLY=OFF`, and
`KISAK_WEB_DIAGNOSTICS=OFF`. Diagnostics used the same configuration with
`KISAK_WEB_DIAGNOSTICS=ON`. The fresh host-native portable suite used the
MSVC x64 Release generator.

## Architectural result

The default `KisakCOD-web` target now contains the canonical runtime plus
browser platform adapters. `KisakCOD-web-diagnostics` separately retains Gate
2, convergence oracles, synthetic proofs, renderer comparison, test controls,
and generic diagnostic calls. Production startup no longer starts archive,
engine-asset, census, or synthetic-world proof jobs and does not require a
synthetic `GfxWorld` to boot.

Production host/Worker communication is protocol version 1 with the named
operations `init`, `mountAssets`, `flushAndUnmount`, `probeAsset`,
`submitCanonicalCommand`, `resize`, `input`, `runtimeStatus`, and `shutdown`.
Payloads and ranges are validated at the Worker boundary. Requests are
Promise-based, timed out, cancellable where meaningful, collision-safe after
ID wrap, and rejected on Worker/protocol/message/shutdown failure. Errors have
stable code, operation, message, recovery classification, and optional detail.

The browser home filesystem now has one exclusive writer across tabs. Its
awaited, retryable flush stops new writes, snapshots dirty open descriptors,
coalesces path generations, persists all mutation chains, closes imported
handles, and releases leases only after success. Total home bytes are updated
incrementally. Asset-store and input owners have idempotent disposal, and app
shutdown closes storage/coordinator resources before terminating the Worker.

CMake uses semantic canonical-runtime, platform, renderer, audio, diagnostic,
and test inventories with a configuration-time duplicate-TU check. The
production link map is generated and scanned in CI. WebGL context ownership
and callback teardown moved into a focused renderer context unit; map/context/
shutdown assertions and retained-memory telemetry were added without changing
canonical renderer-front-end ownership or world traversal.

The offline-SP asset profile is version 1, defaults to English/Killhouse,
records other selected SP zones, and excludes `mp_*` and `*_mp` zones.
Cinematic omission is explicit, structured, visible, and tested. Web Audio has
a 64 MiB decoded-PCM budget, 128-buffer per-source queue limit, unreferenced
LRU eviction, and underrun/overrun/eviction telemetry.

## Production removals and moves

Removed from the production artifact/target:

- Gate 2 census/load sources and `kisak_web_gate2_oracle` linkage;
- `web_archive_job`, `web_engine_asset`, `web_engine_surface`, qcommon proof
  fixtures, fastfile proof streams, sound-catalog oracle, and renderer
  comparison execution;
- synthetic fastfile/GfxWorld startup and physics/qcommon smoke execution;
- generic Wasm function calls, test controls, `_KisakWeb_Test*` exports,
  WebGL monkey patches, and Playwright evidence state;
- `filesystem_bridge.mjs` and stale/unexpected served files.

Preserved elsewhere:

- Gate 2/oracle and proof C++ remain in the diagnostic target;
- diagnostic launcher, host, Worker, protocol, and controls remain in the
  diagnostic site;
- `filesystem_bridge.mjs` moved to `tests/browser/support`;
- useful rationale from `src/staged_changes.patch` moved to
  `docs/staged-patch-retirement.md`, then the patch was removed.

No proprietary asset, key, executable, DLL, or retail-derived fixture was
added. No uncertain canonical adapter was deleted based on its filename.

## Conditional candidates

| Candidate | Result and reason |
| --- | --- |
| `web_filesystem.cpp` | Retained in production as the active browser async filesystem adapter. |
| `web_fastfile_source_stream.cpp` | Retained in diagnostics/tests as differential proof infrastructure. |
| `web_fastfile_world_surface.cpp` | Retained in diagnostics/tests with the synthetic surface proof. |
| `web_fastfile_zone_registry.cpp` | Retained outside production until canonical DB parity replaces its tests. |
| `web_fastfile_zone_stream.cpp` | Retained outside production with zone-stream differential tests. |
| `web_engine_world_surface.cpp` | Retained in production at the backend-neutral canonical `GfxWorld` conversion seam. |
| `web_canonical_gfxworld.cpp` | Retained in production as canonical DB-to-renderer publication. |
| `web_renderer_comparison.cpp` | Moved to diagnostics; still useful for visual/parity evidence. |
| `web_retail_fastfile_census.cpp` | Moved to the frozen Gate 2 diagnostic oracle. |
| `web_sound_alias_catalog.cpp` | Moved with its Gate 2 oracle consumer. |

Baseline link-map hit counts and source classifications are in
`docs/web-cleanup-audit.md`.

## Production artifact measurements

### Before

| File | Bytes |
| --- | ---: |
| `asset_store.mjs` | 54,736 |
| `engine_worker_host.mjs` | 8,229 |
| `engine_worker.mjs` | 9,528 |
| `filesystem_bridge.mjs` | 8,064 |
| `index.html` | 4,851 |
| `kisakcod-next.mjs` (stale served file) | 196,449 |
| `kisakcod.mjs` | 186,104 |
| `kisakcod.wasm` | 3,730,562 |
| `launcher.mjs` | 47,103 |
| `styles.css` | 5,280 |
| `web_audio_driver.mjs` | 19,526 |
| `worker_sync_filesystem.mjs` | 23,939 |

### After

| File | Bytes |
| --- | ---: |
| `asset_profile.mjs` | 2,502 |
| `asset_store.mjs` | 52,445 |
| `engine_protocol.mjs` | 6,151 |
| `engine_worker_host.mjs` | 13,666 |
| `engine_worker.mjs` | 9,489 |
| `index.html` | 3,674 |
| `kisakcod.mjs` | 133,053 |
| `kisakcod.wasm` | 3,180,909 |
| `launcher.mjs` | 10,926 |
| `product_input_controller.mjs` | 3,461 |
| `styles.css` | 5,376 |
| `web_audio_driver.mjs` | 23,524 |
| `worker_sync_filesystem.mjs` | 26,252 |

| Measure | Before | After | Change |
| --- | ---: | ---: | ---: |
| Wasm | 3,730,562 B | 3,180,909 B | -549,653 B (-14.73%) |
| JavaScript modules | 553,678 B | 281,469 B | -272,209 B (-49.16%) |
| Total served site | 4,294,371 B | 3,471,428 B | -822,943 B (-19.16%) |
| Named web entry points | 23 | 9 | -14 (-60.87%) |
| Low-level Wasm exports | 38 | 24 | -14 (-36.84%) |

The nine remaining named web entry points are `CompleteFsRead`,
`CompleteFsStat`, `MountCanonicalRuntime`, `ProbeFastfileHeader`, `ProbeIwd`,
`ProbeLocalization`, `QueueKeyEvent`, `QueueMouseMove`, and
`SubmitCanonicalCommand`. Baseline and production link maps were generated in
ignored build directories; CI preserves the production map as build evidence.

## Memory evidence

The renderer now reports decoded source bytes, estimated GPU texture bytes,
geometry, recovery copies, shader/program cache, temporary upload buffers,
and the configured budget at lifecycle boundaries. The existing 800 MiB
decoded-recovery ceiling is intentionally unchanged. No legally owned retail
asset run was available to measure Killhouse/CargoShip peak memory and visual
recovery before and after an optimization, so accepting a lower cap would
violate the evidence requirement.

Web Audio previously had only a 16 MiB per-upload guard and no global decoded
budget. It now enforces 64 MiB globally and 128 queued buffers per source.
Synthetic tests verify eviction/accounting and that an over-budget upload is
rejected. These are configured limits and synthetic behavior evidence, not a
claim about retail peak audio memory.

## Validation results

| Validation | Outcome |
| --- | --- |
| Production Release build and strict runtime-prefix check | Passed |
| Production artifact/link-map/export/size boundary | Passed: 13 files, 3,180,909 B Wasm, 281,469 B JS, 3,471,428 B site, 24 exports |
| Production Playwright | 2/2 passed |
| Diagnostic Release build and strict runtime-prefix check | Passed |
| Browser smoke | 18/18 passed |
| Browser non-overlapping remainder | 47 passed, 4 skipped, 0 failed |
| Node protocol/profile/lifecycle tests | 4/4 passed |
| ESLint and `tsc --checkJs` protocol check | Passed |
| Fresh Wasm portable differential suite | 37/37 passed |
| Fresh MSVC x64 native portable suite | 29/29 passed |
| Local retail validation spec discovery | 1 skipped as expected without `KISAK_COD4_RETAIL_ROOT` |
| `git diff --check` | Passed before every commit and final handoff |

The original baseline smoke result was 17/18: a dirty open home file was lost
on reload. Final durability coverage includes that path plus queued same-path
writes, rename/remove shutdown, injected persistence failure and retry, schema
handling, quota reporting where testable, Worker restart, and two-tab writer
conflict.

## Local retail validation

No local retail-asset validation was performed during this cleanup. No retail
root was supplied, and none was searched for or inferred. The new opt-in
command is:

```powershell
.\tools\validate_web_retail.ps1 -RetailRoot 'D:\Games\Call of Duty 4'
```

It covers import/mount, canonical Killhouse command acceptance, sustained
world frames, keyboard input, gameplay Web Audio, config checkpoint, awaited
shutdown/reload persistence, Killhouse-to-CargoShip transition, context loss/
recovery, resumed frames, and renderer-memory events. It uses diagnostics only
for forced context controls, which remain deliberately absent from production.

## Remaining risks and roadmap

- CargoShip and other campaign zones remain unvalidated until the local retail
  matrix passes; discovery/header probing is not compatibility.
- The 800 MiB renderer recovery cap remains high. Reduce it only after the
  local transition, recovery, visual, and peak-memory evidence is captured.
- `asset_store.mjs` still owns several closely related import/repository
  responsibilities. The profile and input owners were extracted and all
  lifecycle leaks were closed; further splitting should follow an actual
  ownership or testing need, not file-size targets.
- Remaining renderer/material families and advanced audio parity need retail
  observations and focused tests.
- Gamepad and full cinematic playback are not implemented. Cinematics skip
  explicitly rather than failing silently.
- Multiplayer remains out of the offline product and requires a separately
  designed WebSocket/WebTransport relay; browser Wasm cannot use COD4 UDP.

The ordered follow-up is in `docs/web-roadmap.md`; claim scope is in
`docs/campaign-compatibility.md`.

## Commit list

```text
0d7d1214 fix(web-fs): make shutdown persistence durable
b7f6cead docs(web): record cleanup baseline and source classification
a4cfc419 build(web): split diagnostics from production
19b0a179 ci(web): enforce production artifact boundary
c7c45191 refactor(web-renderer): isolate WebGL context ownership
231d466a feat(web-media): bound audio and expose cinematic omissions
41052d15 refactor(web): validate the product worker protocol
0862b194 refactor(web): make product module ownership disposable
8130ef90 perf(web-renderer): report retained memory budgets
51dd788d fix(web-assets): restore native picker profile import
a3a8004d test(web): add local retail validation matrix
```
