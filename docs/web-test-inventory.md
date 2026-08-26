# Web test inventory after the Ponytail cleanup

This inventory intentionally approves the portable-suite reduction made by
commit `7881f54b7fcc863ce98a5ad6987696909bc238e6` on 2026-08-26. The current
native and direct-Wasm portable configurations each contain 29 tests. Removed
tests exclusively owned deleted browser-only implementations; they are not a
specification for the canonical runtime.

| Removed test target | Removed implementation | Current replacement coverage |
| --- | --- | --- |
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
