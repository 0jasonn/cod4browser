# Web-port merge-readiness report

Date: 2026-08-26 (Europe/London)

## Verdict

The corrective implementation is safe to open as a pull request against
`web-port`: **YES**.

The implementation tree passed every locally applicable correctness, build,
browser, boundary, and repository gate. The only skipped browser tests were the
two explicitly retail-tagged tests because no `KISAK_COD4_RETAIL_ROOT` was
supplied. Native Clang and libFuzzer were unavailable on this Windows host; the
direct `codex/**` CI path retains both Linux Clang portable coverage and the
256-run sanitized parser-fuzz smoke.

This is merge readiness for the browser-port infrastructure, not a claim that a
campaign-ready release was validated without user-owned retail data.

## Baseline

| Field | Recorded value |
| --- | --- |
| Starting branch | `codex/fix-web-renderer-map-load` |
| Starting SHA | `7881f54b7fcc863ce98a5ad6987696909bc238e6` |
| `origin/web-port` starting SHA | `1a9ad9981982ca0a173954fc784247092ff3348c` |
| Starting merge-base | `1a9ad9981982ca0a173954fc784247092ff3348c` |
| Validated ending implementation SHA | `23281957f85108657db499228ebbbbb2e7aca302` |
| `origin/web-port` integrated SHA | `1a9ad9981982ca0a173954fc784247092ff3348c` |
| Final merge-base before this report | `1a9ad9981982ca0a173954fc784247092ff3348c` |

The report itself is the final documentation-only commit, so its commit cannot
self-reference its own SHA. The final handoff records the branch HEAD after the
report commit. All executable validation was run on the implementation SHA
above; repository-only checks are rerun after committing this file.

Toolchain:

- Emscripten 6.0.6 (`emcc`/`em++`, pinned under `.tools/emsdk`)
- CMake 4.2.0-rc3 and Ninja 1.13.2
- Node.js 24.18.0 and npm 11.16.0
- Playwright 1.61.1 with Chromium
- TypeScript 7.0.2 and ESLint 10.9.0
- MSVC 19.51.36256.0, Win32, Windows SDK 10.0.28000
- Bundled Clang tooling 24 was present, but no native Clang or ClangCL compiler
  toolset was installed; WSL was also unavailable

## Ponytail cleanup preservation

The cleanup remains intact. No deleted proof architecture was restored.

| Removed architecture | Final state |
| --- | --- |
| Gate 2 oracle/census | Absent from production sources and build targets |
| Parallel retail asset loaders | Absent; canonical filesystem/XFile/DB path remains authoritative |
| Synthetic fastfile/world proof pipeline | Absent from production startup |
| Obsolete proof jobs | Absent |
| Old scheduler framework | Absent; a narrow slow-command diagnostic hook supplies responsiveness evidence |
| Renderer comparison implementation | Absent; only boundary rejection assertions remain |
| Redundant input wrapper | Absent; shared `input_controller_core.mjs` remains |
| Redundant DB filesystem facade | Absent; Worker filesystem and canonical FS path remain |

`docs/web-test-inventory.md` maps the removed test targets to their removed
implementations and current replacement coverage. Synthetic test fixtures that
exercise canonical parsers remain test-only and contain no retail-derived data.

## Issue disposition

| Prompt issue | Disposition | Result |
| --- | --- | --- |
| Browser-home persistence ordering | **CONFIRMED AND FIXED** | Cross-operation path coalescing was removed. Mutations are strict FIFO, and durable completion is tied to file identity, path generation, and version. |
| Checkpoint summary integrity | **CONFIRMED AND FIXED** | Summaries count only current durable file identities; direct remount tests prove the reported result. |
| Partial mount cleanup ownership | **CONFIRMED AND FIXED** | Clean, cleanup-failed, and ownership-unknown outcomes are explicit. Uncertainty forces termination before lease release. |
| Diagnostic lease/unmount parity | **CONFIRMED AND FIXED** | Diagnostics now enforce the same termination ordering, generation isolation, and second-writer exclusion. |
| Installation selection during mount | **CONFIRMED AND FIXED** | A latest-intent drain loop makes the newest ready selection win and makes clear/dispose cancellation deterministic. |
| Missing Web Locks | **CONFIRMED AND FIXED** | Required before Worker creation or import. |
| BroadcastChannel policy | **CONFIRMED AND FIXED** | Required and documented; destructive coordination has no silent no-channel mode. |
| Transferable OffscreenCanvas and synchronous OPFS probing | **CONFIRMED AND FIXED** | Startup performs real temporary-Worker transfer and Worker-side sync-handle probes, then cleans them up. |
| Structured capability failures | **CONFIRMED AND FIXED** | Capability failures are typed, user-facing, and occur before product Worker/import setup. |
| Renderer surface-restore terminal failure coverage | **CONFIRMED AND FIXED** | Compact diagnostic coverage asserts failed/non-resident/no-resume behavior. |
| Initial renderer-pipeline failure coverage | **CONFIRMED AND FIXED** | Coverage asserts zero successful frames, no later running state, and no false recovery cycle. |
| Scheduler-removal responsiveness concern | **NOT REPRODUCED** | A deliberately slow canonical command exposed the long callback while main-thread turns and input continued; rendering recovered without recursive scheduling. No scheduler was restored. |
| Queued relative mouse movement after lifecycle loss | **CONFIRMED AND FIXED** | Blur, hidden, dispose, and fatal transport cancel RAF and discard accumulated deltas while retaining held-input release. |
| Absolute coordinate convention | **CONFIRMED AND FIXED** | Canonical UI evidence establishes pixel indices; clamping is `0..width-1`/`0..height-1`, with zero-size suppression and DPR tests. |
| Web Audio partial stream-unqueue mutation | **CONFIRMED AND FIXED** | The full prefix and count are validated before one atomic queue/accounting mutation. |
| Web Audio partial source-position update | **CONFIRMED AND FIXED** | X, Y, and Z must all be finite before any position state changes. |
| Production artifact headroom | **CONFIRMED AND FIXED** | A versioned, commit-bound 5% baseline replaced near-zero ad hoc headroom without weakening export or file allowlists. |
| Direct-push CI and deleted-test inventory | **CONFIRMED AND FIXED** | `codex/**` triggers the complete workflow; the 29-test portable inventory and deleted-target mapping are documented. |
| Canonical XFile fuzz follow-up | **DEFERRED WITH REASON** | The authoritative boundary owns file/auth/compression/PMem/stream state and is not a small isolated harness. Existing IWI, IWD/zlib, and shader fuzzing remains; the deleted retail dispatcher was not restored. |
| Nested repository ZIP | **CONFIRMED AND FIXED** | Tracked `cod4browser-887f1c8.zip` was removed and `cod4browser-*.zip` is ignored. |
| Documentation drift | **CONFIRMED AND FIXED** | Current protocol, input, persistence, browser requirements, convergence status, licence link, and historical-report labels now match the implementation. |
| Latest `origin/web-port` integration | **ALREADY SAFE** | The fetched target SHA equals the merge-base and is already an ancestor; no empty merge commit or conflicts were needed. |
| Retail validation without an explicit root | **DEFERRED WITH REASON** | Policy required no machine search, path inference, download, or new compatibility claim. Two `@retail` tests skipped explicitly. |

Acceptance criteria 1–7 are covered by the persistence evidence below; 8–12
by ownership evidence; 13–17 by selection/capability tests; 18–21 by renderer
and responsiveness evidence; 22–25 by Node/input/audio tests; 26–34 by the
matrix and hygiene checks; and 35–39 by the integration and merge result.

## Persistence evidence

All ten tests instantiate the real `createWorkerSyncFilesystem()` against the
OPFS test abstraction and then inspect a fresh filesystem instance. Result:
**10 passed, 0 failed, 0 skipped**.

| Required sequence | Exact result |
| --- | --- |
| Remove/recreate/remount | `test/a.cfg` remounted as `new`; no absent-file false success |
| Rename-over-existing/remount | `config.cfg` remounted as `new`; `config.tmp` was absent |
| Rapid atomic replacement | Two replacements queued before persistence retained the final snapshot |
| Blocked-queue replacement | Same-path replacement remained behind the blocking earlier mutation |
| Active old write plus remove/recreate | New identity survived completion of the older in-flight write |
| Rename source/destination barriers | Both affected path generations preserved their global order |
| Failure/retry | Injected persistence failure retained the queue; retry durably wrote the replacement |
| Checkpoint summary integrity | `filesPersisted`/`bytesPersisted` matched the final OPFS contents |
| `flushAndUnmount()` | Used the same ordered drain and survived remount |
| Worker restart | Every adversarial mutation sequence passed through a fresh instance |

Implementation invariants:

- every persistence mutation has one FIFO sequence position;
- writes are snapshots of a specific identity/path generation/version;
- remove, recreate, and rename cannot be crossed by a later same-path write;
- completion updates durable state only if the snapshot still names the current
  logical file;
- retry retains the failed mutation and all later mutations in order.

## Ownership evidence

Worker mount classification tests: **5 passed**. Diagnostic lifecycle tests:
**7 passed**. The wider product-host lifecycle group also passed as part of the
64-test Node suite.

| Required case | Exact result |
| --- | --- |
| Failure before filesystem ownership | Classified clean only after cleanup proves no ownership |
| Canonical mount failure after filesystem mount | Classified ownership-unknown; host blocks operations |
| Checkpoint failure after runtime mount | Classified ownership-unknown |
| Partial-mount cleanup succeeds | Emits a clean mount failure and permits release |
| Partial-mount cleanup fails | Emits explicit cleanup-failed, never clean |
| Terminate-before-release | Event ordering proves Worker termination precedes writer-lease release |
| Diagnostic parity | Clean, failure, timeout, crash, exclusion, and handoff cases all pass |
| Second-tab exclusion | Second writer remains blocked throughout uncertain ownership and acquires only after termination |
| Stale generation | Late replies/progress from the failed Worker cannot mutate the replacement session |

Production Playwright independently verified that a second browser tab cannot
acquire the writable home profile while the first owns it.

## Product lifecycle and capabilities

The product mount controller has **6 focused tests**, all passing: delayed A
then B, newest selection publication, stale A suppression, clear during mount,
failed A followed by B, and disposal without queued remount.

The product browser suite verified failure before Worker/import setup for
missing Web Locks, required BroadcastChannel, transferable OffscreenCanvas,
OPFS, and Worker `SyncAccessHandle`. A successful probe transfers a real canvas
to a temporary Worker and performs the synchronous filesystem operation there.

## Renderer and responsiveness

| Evidence | Exact result |
| --- | --- |
| Surface recovery failure | Renderer stayed failed; retained surface stayed non-resident; successful frame count did not advance; no running state returned |
| Initial pipeline failure | Zero successful frames; no later running or misleading renderer-lost/recovery state |
| Slow completion/command | Diagnostics-only canonical command completed and published callback-duration telemetry |
| Event-loop responsiveness | Input and multiple main-thread turns were observed while the slow Worker callback ran |
| Frame recovery | Later frame ticks and rendering resumed; ticks remained monotonic and unique |
| Scheduling safety | No runaway recursive frame scheduling was observed; old scheduler framework remains deleted |

The slow-command hook is diagnostics-only (`_KisakWeb_TestSlowNextCommand`) and
is absent from the production export surface.

## Input and audio evidence

- Input lifecycle: **6 Node tests passed** for blur, hidden, disposal, fatal
  transport, canonical pixel-index bounds, and zero-sized canvas suppression.
- Production DPR/edge scaling remained covered by the product Playwright matrix.
- Audio mutation atomicity: **2 focused Node tests passed**. A queue `A,B,C`
  rejected `A,X` without removing `A`; non-finite X/Y/Z rejected the entire
  source-position update.

## Final test matrix

All results below were produced after fetching and proving the current
`origin/web-port` was already contained.

| Gate | Pass | Fail | Skip / unavailable | Result |
| --- | ---: | ---: | ---: | --- |
| Node protocol/lifecycle/filesystem/input/audio | 64 | 0 | 0 | PASS |
| `node --check` | 17 modules | 0 | 0 | PASS |
| ESLint | all `web/*.mjs` and `tests/node/*.mjs` | 0 errors | 0 | PASS |
| Strict checkJs | 4 modules | 0 | 0 | PASS |
| Gradual/runtime checkJs | 7 modules | 0 | 0 | PASS |
| Native Clang portable | 0 | 0 | unavailable locally | NOT RUN: no Clang/ClangCL compiler or WSL |
| Native MSVC Win32 portable | 29 | 0 | 0 | PASS, fresh build |
| Direct Wasm portable | 29 | 0 | 0 | PASS, fresh build |
| Fuzz smoke | 0 local runs | 0 | unavailable locally | NOT RUN locally; CI runs Clang ASan/UBSan/libFuzzer with `-runs=256` |
| Production Release Web build | 1 | 0 | 0 | PASS |
| Diagnostics Release Web build | 1 | 0 | 0 | PASS |
| Production Playwright | 26 | 0 | 0 | PASS |
| Diagnostic smoke Playwright | 12 | 0 | 0 | PASS |
| Diagnostic remainder Playwright | 34 | 0 | 2 expected retail | PASS |
| Production boundary | 1 gate | 0 | 0 | PASS: 17 files, 24 raw exports, exact 9 application exports |
| Retail validation | 0 | 0 | 2 | SKIPPED: no explicitly supplied legal retail root |
| `git diff --check` | 1 | 0 | 0 | PASS |

Portable test durations were 1.975 seconds for MSVC CTest and 0.731 seconds
for direct-Wasm CTest. Production Playwright completed 26/26 in 6.1 seconds;
diagnostic smoke completed 12/12 in 7.4 seconds; diagnostic remainder completed
34 pass plus 2 skips in 13.9 seconds.

There were no unexpected skips.

## Artifact measurements

Byte baselines were measured at starting SHA `7881f54b` with the same pinned
toolchain. The diagnostics baseline was reconstructed in an isolated detached
worktree whose checkout leaf had the same length as `cod4browser`, avoiding
generated absolute-path size skew.

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| Production Wasm | 3,166,358 B | 3,166,496 B | +138 B (+0.004%) |
| Production JavaScript | 318,133 B | 325,143 B | +7,010 B (+2.204%) |
| Production total site | 3,494,619 B | 3,501,767 B | +7,148 B (+0.205%) |
| Diagnostics Wasm | 3,170,300 B | 3,170,519 B | +219 B (+0.007%) |
| Diagnostics JavaScript | 323,060 B | 326,184 B | +3,124 B (+0.967%) |
| Diagnostics total site | 3,504,355 B | 3,507,265 B | +2,910 B (+0.083%) |

Observed build-script timing:

| Build | Starting-SHA cold reconstruction | Final-tree warmed rebuild |
| --- | ---: | ---: |
| Production Release | 274.623 s | 11.920 s |
| Diagnostics Release | 266.086 s | 13.177 s |

The timing rows document observed runs, not a performance comparison: the
starting-SHA worktrees had cold object caches, while the final verification
rebuilt configured trees. The byte measurements and boundary results are the
deterministic artifact evidence.

Approved production baseline and 5% budgets:

| Boundary | Approved bytes | Budget bytes | Current headroom |
| --- | ---: | ---: | ---: |
| Wasm | 3,166,496 | 3,324,821 | 158,325 |
| JavaScript | 325,143 | 341,401 | 16,258 |
| Total site | 3,501,767 | 3,676,856 | 175,089 |

`tools/web_product_size_baseline.json` is bound to implementation commit
`7a32cba07527d1179001fc088d72244eaab00411`, the post-correctness tree used to
approve the byte baseline. The checker prints current, baseline, budget, byte
difference, and percentage difference while continuing to enforce the exact
site/export allowlists, diagnostic-source exclusion, and 24-export raw cap.

## Repository hygiene

- `git diff --check origin/web-port...HEAD`: pass.
- Tracked repository ZIPs: none.
- Tracked COD4 retail data (`*.ff`, `*.iwd`, `*.bik`): none.
- Generated production site: no retail data or native runtime binaries; covered
  by production Playwright and the exact site-file allowlist.
- Untracked repository archives matching `cod4browser-*.zip` are ignored.

The upstream baseline and `origin/web-port` both contain unchanged native
Bink/Miles/Steam SDK and redistributable files under `deps/`. They are not COD4
retail assets and are never copied into the browser site. Removing or replacing
those native-only dependencies is a repository-wide licensing/native-build
decision, not a browser-correctness change, and was not mixed into this pass.

## Merge result

| Field | Result |
| --- | --- |
| `origin/web-port` SHA integrated | `1a9ad9981982ca0a173954fc784247092ff3348c` |
| Integration method | Already an ancestor; no unnecessary merge commit |
| Conflicts resolved | None |
| Tests rerun after integration proof | Complete locally applicable matrix above |
| Direct changes pushed to `web-port` | None |
| Feature branch clean | Verified after the final report commit |
| Safe to open PR | **YES** |

## Remaining risks

### Merge blockers

None found in the locally applicable matrix. The pull request CI must still be
allowed to complete its Linux Clang and sanitized fuzz jobs because those
toolchains were unavailable on this host.

### Release blockers

- No current retail campaign validation was run. A user must explicitly supply
  a legally owned `KISAK_COD4_RETAIL_ROOT`, after which Killhouse/CargoShip,
  input, audio, persistence, transition memory, and context recovery must be
  rerun before any campaign-ready release claim.

### Future product work

- Build a small synthetic fuzz seam at the authoritative canonical XFile
  boundary without restoring the deleted Gate 2 dispatcher. The seam must
  preserve file/auth/compression/PMem/stream ownership and cover header, block
  table, stream bound, asset count, pointer-fixup, and generated-loader errors.
- Decide repository-wide handling of inherited native Bink/Miles/Steam SDK and
  redistributable files separately from the browser target, preserving native
  build documentation and licensing constraints.
