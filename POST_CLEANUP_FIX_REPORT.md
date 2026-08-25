# Post-cleanup corrective validation

## Baseline

- Branch: `codex/fix-web-renderer-map-load`.
- Starting SHA, recorded before corrective changes: `232f030b2b90920936ea4096c55cb4b7b64e088f` (`fix(web-renderer): preserve context across map loads`).
- Ending validated implementation/documentation SHA: `b7d73385ea2372f39e71e7b4e95ee05e04b8ad0a`. This is the parent of the report-only commit; the final pushed report commit is stated in the handoff.
- Toolchain: Emscripten 6.0.6 (`ce75e06884093bcefb86a6b8fd56a5d62a4cc245`), CMake 4.2.0-rc3, Node 24.18.0, npm 11.16.0, Playwright 1.61.1, TypeScript 7.0.2, and ESLint 10.9.0.
- Configuration: C++20, `KISAK_PLATFORM=web`, Release production with `KISAK_WEB_DIAGNOSTICS=OFF`, and a separate Release diagnostics build. The Worker-hosted, single-threaded Wasm architecture and WebGL2 backend remain intact.
- Initial production artifact: 3,180,929-byte Wasm, 281,469 bytes of JavaScript, 3,471,448-byte served site, 13 files, 24 raw Wasm exports, and nine named application exports.
- Initial test state: production and diagnostics Release builds passed; production Playwright passed 2/2; diagnostic smoke passed 18/18; diagnostic remainder passed 47 with 4 skipped; Wasm portable passed 37/37; native Clang portable passed 29/29. The existing Visual Studio cache could not regenerate because CMake 4.2.0-rc3 does not know the installed MSVC 19.51 feature table, so the supported native Clang cache was used.

No production/diagnostics boundary, canonical engine ownership, Worker architecture, WebGL2 ownership, product protocol, writable-profile exclusivity, Gate 2 isolation, cinematic omission, audio budget, or diagnostic denylist was removed.

## Issue disposition

| Prompt issue | Result | Evidence |
| --- | --- | --- |
| 1. Complete production input | **CONFIRMED** | The product controller handled a limited keyboard map and relative motion, but had no mouse-button, wheel, absolute-pointer, pointer-lock-loss, or full held-input lifecycle. Product and diagnostics now use the same production-safe core and product-site tests exercise it. |
| 2. Filesystem writer-lease safety | **CONFIRMED** | `flushAndUnmount()` cleared `mounted` and released leases from `finally`; mount failures and timeouts also released leases without proving the Worker had stopped. The explicit lifecycle now retains ownership through retryable/unknown states and terminates uncertainty before release. |
| 3. Production/diagnostics CI boundary | **CONFIRMED** | The old manually maintained identities had drifted from actual source names. The checker now reads the canonical `KISAK_WEB_DIAGNOSTIC_SOURCES` inventory and enforces an exact named application-export set. |
| 4. Production size budgets | **CONFIRMED** | Previous thresholds were not ratcheted to the cleaned artifact. Three independent byte caps now use exact final-baseline-plus-5% ceilings and print current, cap, byte difference, and percentage difference. |
| 5. Renderer map unload | **CONFIRMED** | Backend map cleanup preserved the context, but frontend world readiness, pointers, cached submissions, material references, and map flags could outlive the unload boundary. Frontend-owned `R_UnloadWorld` now resets them before backend map-resource release. |
| 6. Map-transition regression coverage | **CONFIRMED** | There was no deterministic frontend/backend A-to-B lifecycle test. A native/Wasm test now proves world A publication, unload, context preservation, world B publication, second unload, and full shutdown. |
| 7. High-frequency input transport | **CONFIRMED** | Each motion used request IDs, a pending map, timeout, acknowledgement, and Promise. Lossless transitions now use a validated ordered one-way message, while relative deltas are summed and sent at most once per animation frame. Transport failure is surfaced instead of swallowed. |
| 8. Checkpoint durability | **CONFIRMED** | Clean unmount was the only production host durability path; page lifecycle alone could not guarantee completion. Mounted profiles now checkpoint without releasing ownership, coalesce in-flight dirtiness into one follow-up, and retry after surfaced failure. |
| 9. Persistence/quota UX | **CONFIRMED** | Persistent-storage and quota state was not honestly visible in the product. The launcher now shows grant state, usage, quota, percentage, pressure warnings, retry, and re-import recovery without claiming guaranteed durability. |
| 10. Static checking | **CONFIRMED** | Major runtime modules were outside lint/checkJs coverage. ESLint now covers the product modules and shared runtime code; strict checkJs covers typed contracts/input/checkpoint code and gradual checkJs covers the larger asset, filesystem, audio, Worker, host, and launcher modules. |
| 11. Asset-profile polish | **ALREADY FIXED** | The audited head already used a versioned English/Killhouse baseline, discovered additional SP zones, and excluded MP zones. The regression test was retained; no untested language claim was added. |
| 12. 800 MiB recovery limit | **NOT REPRODUCED** | No supplied retail transition measurements supported changing the 800 MiB decoded recovery ceiling. It remains unchanged as required. |

## Input

The starting product controller omitted all mouse buttons and wheel pulses, omitted absolute menu coordinates, did not react to `pointerlockchange`, and only released keyboard keys on blur. It also swallowed Worker-input failure with `.catch(() => {})`.

`input_controller_core.mjs` is now the single basic DOM input implementation used by product and diagnostics. It maps canonical letters, digits, punctuation, function/navigation/modifier/Caps Lock/Pause/numpad keys; five mouse buttons; wheel-up/down pulses; pointer-lock relative motion; and canvas-relative absolute motion. It handles engine-requested cursor mode, click-to-lock, Escape/lock loss, blur, visibility loss, and disposal. Held keys and buttons are released exactly once, and temporary wheel input cannot remain held.

Key/button/wheel transitions remain lossless and ordered through a validated one-way `input-event`. Relative motion sums X/Y deltas once per animation frame, preserving signs and total physical motion without an acknowledgement RPC. A failed transport stops delivery, clears local held state, marks the runtime unavailable, and publishes a structured product error.

The production Playwright suite covers keyboard, punctuation, function/navigation/modifier/numpad mappings; MOUSE1/MOUSE2/middle/additional buttons; wheel pulses; pointer-lock acquisition/loss; relative and absolute movement; focus release; and disposal.

## Filesystem ownership

At the starting SHA, the host represented ownership with loose `mounted`/lease variables. `flushMountedFilesystem()` released both leases in `finally`, even after rejection or timeout. `mountAssets()` also released on any RPC error. Because an RPC timeout only rejected the host Promise, an old Worker could still finish and write after another tab obtained the profile lock.

The host now uses explicit states: `UNMOUNTED`, `ACQUIRING`, `MOUNTING`, `MOUNTED`, `FLUSHING`, `FLUSH_FAILED_RETRYABLE`, `UNKNOWN_AFTER_TIMEOUT`, `TERMINATING`, and `TERMINATED`. Mount and flush have dedicated 60-second policies rather than inheriting lightweight request timeouts.

- Explicit mount failure cleans Worker state before releasing leases.
- Recoverable flush/checkpoint failure retains the mounted profile and writer lease for retry.
- Mount/flush timeout enters unknown state, blocks further operations, terminates the Worker, confirms termination, and only then releases leases.
- Replies carry the Worker generation; old-generation replies cannot settle or mutate a restarted session.
- Successful flush releases ownership only after the Worker confirms persistence, handle cleanup, and unmount.

Fifteen lifecycle tests prove successful and failed mount/flush, late timeout replies, retry, second-tab exclusion/acquisition, crash recovery, stale-generation rejection, and the observable terminate-before-release ordering. Checkpoint tests additionally prove that checkpointing never releases the writer lease.

## Renderer lifecycle

Normal map unload is frontend-owned. `R_UnloadWorld` clears world-ready state, canonical world pointers, cached scene submissions, material/resource references, and map-specific flags, then invokes `WebRenderer_UnloadWorldResources`. The backend deletes map textures, geometry/buffers/VAOs, lighting/shadow state, visibility resources, recovery sources, and other map-scoped resources while retaining the canvas, WebGL context, context-loss handlers, and reusable process renderer state.

Full renderer shutdown remains separate and destroys backend-global resources, handlers, caches, and context ownership. Lifecycle assertions reject publication over retained frontend state, impossible unload state, failure to clear the frontend pointer, failure to release backend map state, loss of context during partial unload, and incomplete full shutdown.

The new `web_renderer_frontend_lifecycle_tests` runs identically under native Clang and Wasm: publish A, create map resources, unload A, prove map state released and context generation preserved, publish B, unload B, then fully shut down and prove global context ownership is released.

## Boundary and CI

The boundary checker derives 19 forbidden diagnostic translation units from the canonical CMake `KISAK_WEB_DIAGNOSTIC_SOURCES` inventory, including `web_fastfile_source_stream.cpp` and `web_fastfile_world_surface.cpp`. It checks the production linker map and the 15-file served-site allowlist.

The exact named application-export allowlist is:

```text
_KisakWeb_CompleteFsRead
_KisakWeb_CompleteFsStat
_KisakWeb_MountCanonicalRuntime
_KisakWeb_ProbeFastfileHeader
_KisakWeb_ProbeIwd
_KisakWeb_ProbeLocalization
_KisakWeb_QueueKeyEvent
_KisakWeb_QueueMouseMove
_KisakWeb_SubmitCanonicalCommand
```

Generated Emscripten runtime exports are counted separately. Final production contains 24 raw exports and exactly nine application exports.

Final measured production sizes and exact 5% ceilings are:

| Artifact | Current bytes | Budget bytes | Headroom bytes | Current below budget |
| --- | ---: | ---: | ---: | ---: |
| Wasm | 3,181,009 | 3,340,060 | 159,051 | 4.76% |
| Application JavaScript | 305,454 | 320,727 | 15,273 | 4.76% |
| Total served site | 3,496,591 | 3,671,421 | 174,830 | 4.76% |

The production site contains exactly 15 allowlisted files. No generic Wasm invocation, test export, diagnostic control, or diagnostic translation unit was reintroduced.

## Persistence and product UX

The product protocol now has a bounded `checkpoint` operation. The Worker persists dirty writable-home snapshots while staying mounted and retaining all ownership. The host shares an active request where possible, records dirtiness that arrives during the operation, and runs exactly one non-overlapping follow-up. Failure leaves ownership intact, is shown in product state, and permits a later retry.

`visibilitychange` checkpoints a dirty profile when the page becomes hidden. `pagehide` remains best-effort cleanup; it is not treated as the primary durability mechanism. The product displays persistent-storage grant state, usage/quota/percentage when supported, warns only for non-persistent or at-least-80%-full storage, and exposes retry/re-import actions. A persistence-request race discovered by the exhaustive suite was fixed so stale storage status cannot replace an in-progress picker or validation result.

## Final test matrix

| Gate | Pass | Fail | Skip | Notes |
| --- | ---: | ---: | ---: | --- |
| Production Release build | 1 | 0 | 0 | Site and runtime-prefix check produced successfully. |
| Diagnostics Release build | 1 | 0 | 0 | Final source rebuilt; diagnostics site and runtime-prefix check produced successfully. |
| Production artifact boundary/export/size gate | 1 | 0 | 0 | 15 files, 24 raw exports, 9 exact application exports, 19 diagnostic source identities. |
| Production Playwright | 5 | 0 | 0 | Narrow production site, including full shared input, storage UX, and dead transport. |
| Diagnostic browser smoke | 18 | 0 | 0 | Non-overlapping smoke tier. |
| Diagnostic browser remainder | 47 | 0 | 7 | Non-smoke/non-native-authoritative remainder; retail-dependent cases skipped. |
| Node protocol/lifecycle | 23 | 0 | 0 | Includes 15 filesystem lifecycle cases and checkpoint/profile/protocol cases. |
| Wasm portable | 38 | 0 | 0 | Includes synthetic renderer frontend lifecycle. |
| Native Clang portable | 30 | 0 | 0 | Includes the same synthetic renderer lifecycle. |
| ESLint | 1 | 0 | 0 | All configured production/runtime and Node test modules, zero diagnostics. |
| Strict checkJs profile | 1 | 0 | 0 | Typed protocol/input/checkpoint/profile modules. |
| Gradual checkJs runtime profile | 1 | 0 | 0 | Asset store, filesystem, audio, Worker, host, and launcher modules. |
| `git diff --check` | 1 | 0 | 0 | Clean before report generation and repeated after the report. |

The first exhaustive diagnostic remainder run exposed two asset-selection failures caused by persistence status replaying a stale `empty` state. No test was removed or weakened: the race was fixed, the two focused cases passed 2/2, and the complete remainder rerun passed 47 with 7 intentional skips.

## Retail validation

`KISAK_COD4_RETAIL_ROOT` was not provided and the filesystem was not searched for retail data.

**Retail Killhouse/CargoShip validation was not performed. No compatibility claim is made for CargoShip from this corrective run.**

No proprietary asset or captured proprietary data was added to source control.

## Remaining work

- Run the documented owned-retail Killhouse-to-CargoShip transition, sustained frames, memory/resource telemetry, forced context loss/recovery, and persistence checks when `KISAK_COD4_RETAIL_ROOT` is explicitly supplied.
- Re-evaluate the 800 MiB decoded recovery ceiling only after that transition supplies evidence that a lower design remains safe.
- Generalise and validate language profiles separately; this run intentionally retains the tested English/Killhouse baseline and makes no non-English support claim.
