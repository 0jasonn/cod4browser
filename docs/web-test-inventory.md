# Web test inventory after the Ponytail cleanup

Updated 2026-09-03. The Win32 MSVC portable configuration contains 34 tests;
the direct-Wasm configuration contains 35, including its platform-specific
cgame timescale check. This cleanup preserves every portable test registration.
Shared CMake loops replace repeated Node flags, output suffixes and registration.
The startup fixture now supplies its missing timescale globals, and the
primary-light test receives the portable-header and Node setup needed to run.
The unchanged particle-cloud rollback fixture receives Wasm heap growth and
exception catching so its allocations and failure checks can execute.

The earlier reduction in commit `7881f54b7fcc863ce98a5ad6987696909bc238e6`
removed tests for deleted browser-only implementations. Those tests are not a
specification for the canonical runtime.

| Removed test target | Removed implementation | Current replacement coverage |
| --- | --- | --- |
| `filesystem.spec.mjs` (three cases) | Unused asynchronous stat/read bridge and immutable read-source tokens | `canonical_filesystem.spec.mjs`, Worker durability tests, Node filesystem lifecycle tests and product lease tests exercise the active synchronous Worker filesystem and import ownership. |
| Shader branch of `asset_parsers_fuzz` | Unused bootstrap shader substitution parser | No replacement by design. The harness retains active IWD/IWI parser coverage; browser boot and context-recovery tests exercise the actual renderer. |
| `archive.spec`, `web_fastfile_source_stream_tests`, `web_fastfile_zone_stream_tests`, `web_fastfile_zone_registry_tests`, `retail_fastfile_dispatcher_fuzz` | Parallel retail archive/source/zone parser and dispatcher | Canonical IWD and filesystem behavior: `iwd_archive_tests`, `web_asset_probe_tests`, `gate3_db_stream_trace_tests`, and `canonical_filesystem.spec`. A state-owning canonical XFile fuzz harness remains a post-merge task; the deleted dispatcher is not restored. |
| `engine_asset.spec`, `web_fastfile_world_surface_tests` | Extracted browser asset records and synthetic world proof | `canonical_asset_abi_tests`, `gate3_db_pool_tests`, `gate3_db_stream_trace_tests`, renderer frontend tests, `gate3_db_worker.spec`, and the opt-in retail canonical DB suite. |
| `gate2_oracle.mjs`, `retail_census.spec`, `web_retail_fastfile_census_tests` | Gate 2 census/oracle | No direct replacement by design. Canonical DB stream/publication tests own correctness; production boundary checks ensure Gate 2 stays absent. |
| `qcommon.spec`, `web_qcommon_preinit_tests` | Browser-only qcommon shell and staged startup controls | `canonical_runtime_prefix_tests` on native/Wasm plus `canonical_runtime_prefix.spec` through the Worker runtime. |
| `scheduler.spec`, `web_cooperative_scheduler_tests` | Deleted proof-job/cooperative scheduler | `responsiveness.spec` uses a bounded diagnostic slow-command hook and callback telemetry to verify main-thread responsiveness, input acceptance, frame recovery, and non-recursive pump progress. |
| `web_renderer_comparison_tests` | Deleted renderer-comparison implementation | Portable surface/world/lighting/model/effect renderer tests plus browser initial-pipeline and terminal context-recovery failure coverage in `boot.spec`. |

CI runs native portable, sanitized parser-fuzz smoke, Windows MSVC portable,
direct-Wasm portable, Node lifecycle/protocol, static/syntax, production and
diagnostic builds, production boundary, product Playwright, diagnostic smoke,
and the non-overlapping diagnostic remainder on direct `codex/**` pushes and
pull requests.

## Current cleanup verification

Verified on 2026-09-03 with MSVC 19.51, Emscripten 6.0.6, Node 24.18.0
and Chromium 149.0.7827.55:

| Check | Result |
| --- | --- |
| Native SP, Release production and diagnostic Wasm builds | Passed, including runtime-prefix build checks |
| Portable tests | 34 native and 35 Wasm passed |
| Node lifecycle/protocol and JavaScript static checks | 83 tests passed; syntax, lint and both type-check tiers passed |
| Product browser suite | 44 passed |
| Diagnostic smoke and synthetic remainder | 10 and 50 passed; six opt-in retail cases skipped in the isolated synthetic remainder |
| Parser fuzz smoke | 256 executions passed with Clang ASan/UBSan; the local Windows Debug runtime was used to match the bundled libFuzzer |
| Documentation | 27 superseded Markdown files removed; all current local links and pinned Git retrieval paths resolve |

The production artifact checks reach the existing size gate after passing the
file/export/diagnostic boundary checks. Wasm is 3,703,471 B against a 3,332,379 B
budget; JavaScript is 752,138 B against 357,646 B; the site is 4,553,676 B against
3,701,082 B. Byte budgets and the raw-export cap remain unchanged. The exact
application-export allowlist drops only the two retired filesystem callbacks.

The CI SDK step was checked against the pinned package hash, include/library
paths and PowerShell parser locally. Remote GitHub Actions and Linux CI were
not run in this session.

An additional remainder invocation inherited `KISAK_COD4_RETAIL_ROOT` and
completed with 52 passed and four failed:

- The local validation matrix rejected the uncommitted cleanup at its
  clean-source guard.
- Gate 3 loaded/rendered the owned map but timed out on the exact camera/geometry
  predicate at `gate3_retail_db.spec.mjs:249`.
- The cinematic case observed a terminated Worker instead of completed mounting
  at `retail_cinematic.spec.mjs:55`.
- Transient omni lighting increased the captured mean by 0.231, below the
  required value of 1 at `retail_ui_persistence.spec.mjs:912`.

The isolated synthetic remainder above cleared the retail variable. The retail
assertions were not changed, and these results do not establish retail parity
or whether those failures predate the cleanup. Logs and failure traces remain
under ignored `build/cleanup-remainder.log` and `test-results/8173/`.
