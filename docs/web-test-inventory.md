# Web test inventory

The build inventories and CI workflow own test registration. Use the
[README](../README.md#browser-validation) for setup and routine commands.

| Tier | Ownership |
| --- | --- |
| Native and direct Wasm | Parser bounds, serialization, math, ABI, database streams/pools/publication, aliases, rollback, canonical runtime contracts and portable renderer policy. `tests/native/CMakeLists.txt` owns registration. |
| Node (`test:protocol`) | Protocol validation, input conversion, Worker transport, import/filesystem lifecycle, checkpoint ownership, audio feedback and renderer workload comparisons. |
| Diagnostic smoke (`test:browser`) | Fast `@smoke` browser-platform checks against `build/web-diagnostics/site-diagnostics`. |
| Diagnostic remainder (`test:browser:remainder`) | Non-smoke, non-product browser boundaries: Worker/page lifecycle, file selection, OPFS, locks, events, WebGL2, audio, recovery and persistence. Clear `KISAK_COD4_RETAIL_ROOT` for an isolated synthetic run. |
| Production (`test:browser:product`) | Shipped launcher, capabilities, filesystem leases, input scaling and mounting against `build/web/site`. |
| Exhaustive (`test:browser:full`) | Explicit full browser run when duplicate boundary evidence addresses a remaining risk. |
| Static/product checks | Syntax, ESLint, JavaScript types, generated-file/export allowlists and size budgets. |
| Parser fuzz | Synthetic malformed IWD/IWI input under sanitizers; no retail fixtures. |
| Owned retail | Opt-in [local validation](local-retail-validation.md), campaign-map, cinematic and UI checks. Private local files only; never hosted CI fixtures. |

CI runs Linux/native, sanitized fuzz, Windows MSVC, direct-Wasm, Node/static,
production and diagnostic builds, product boundary checks, product browser,
diagnostic smoke and remainder tiers. Native/Wasm semantics should not be
routinely repeated in browser tests tagged `@native-covered`.

## Known qualification limits

On Windows, renderer sampler changes also need installed Chrome/D3D11:
set `KISAK_BROWSER_CHANNEL=chrome`, choose an isolated `KISAK_WEB_TEST_PORT`,
and run `npx.cmd playwright test tests/browser/dynamic_lights.spec.mjs`.
The first pixel draw forces ANGLE backend compilation; WebGL link/validation
alone did not catch an earlier HLSL helper-name collision.

The 2026-09-04 Chrome graphics run recorded three exact-pixel failures
(127 versus 128 in soft alpha and light/mip green output); its other five
cases passed. Default Chromium routine tiers passed. These observations do
not establish native/browser fidelity or justify weakening assertions.

Optional retail tiers are not qualified by synthetic passes. A later transient-
light run passed illumination/shadows/clearing/recovery but failed the final
positive DObj post-pose diagnostic; see the [renderer guide](renderer-retained-resources.md).
The 2026-09-07 Gate 3 attempt still expected camera-only geometry from before
`4bee7442` added shadow-only BSP surfaces. Its trace contains 8,480 surfaces,
445,595 vertices, 823,896 indices and 797 batches, reaching generation 53
versus the required 120 before timeout. Assertions remain unchanged; the
optional fixture is unqualified. Clear inherited retail settings for synthetic
tiers; authoritative retail checks also require clean committed source.

## Optimized assertion and ABI matrix

Every target in `tests/native/CMakeLists.txt` inherits `-UNDEBUG` or `/UNDEBUG`
and a forced `test_assertions.h` compile guard, including future registrations.
The deliberately failing child is invoked only through a passing parent that
requires the intended assertion diagnostic and nonzero status. Production
directories retain their existing optimization/assertion policy. The separate
reverb test retains its assertion flags and includes the same guard.

| Matrix | Placement |
| --- | --- |
| Linux x86-64 host-portable | Portable parser/math/platform calculations. Configure prints explicit exclusions for canonical layouts. Primary-light core no longer includes `q_shared.h`. |
| Windows x86 canonical | All portable checks plus 32-bit Kisak layouts and native differential/oracle coverage. |
| Wasm32 | Canonical layouts, portable checks and direct platform/differential coverage; MSVC-only oracle remains native. |
| Windows x64 supplemental | Portable subset; useful independent host evidence, not Linux qualification. |

`r_gamma_tests`, `ui_savegames_tests`, `r_text_tests` and `r_image_quality_tests`
require the canonical 32-bit Windows/Wasm boundary. They are deliberately
registered there; no layout assertions or packing were changed. The static-model
Wasm fixture has a test-only 256 KiB stack with overflow checks after its default
64 KiB stack overflow was reproduced; production stack sizing is unchanged.

The supported Linux sequence remains:

```sh
cmake -S . -B build/audit-portable -G Ninja -DKISAK_PORTABLE_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/audit-portable --parallel 2
ctest --test-dir build/audit-portable --output-on-failure
```

## Seeded parser defense

`tests/fuzz/generate_corpus.py` reproduces 12 synthetic GPL-3.0 seeds and prints
their SHA-256 identities; `tests/fuzz/asset_parsers.dict` supplies format tokens.
The harness reaches valid and malformed IWI parse/decode and IWD member paths,
checks buffer progress and failed-image publication, and reports seeded path
families. It does not fuzz fastfiles, saves or cinematics.
Configure Clang with `KISAK_BUILD_FUZZERS=ON` and `KISAK_PORTABLE_TESTS_ONLY=ON`,
then build `asset_parsers_fuzz`. Run:

```sh
python3 tools/run_asset_fuzz.py build/fuzz/tests/native/asset_parsers_fuzz build/fuzz-run --seconds 30
```

Use a new disposable output directory each time. ASan/UBSan/libFuzzer remain
enabled; PR runs use 30 seconds, scheduled/manual runs 600. Limits are 256 KiB
input, 10 seconds per input, 1 GiB RSS, 512 MiB single allocation and a total
subprocess deadline of budget + 60 seconds. Failure artifacts contain synthetic
inputs/logs only. A successful process must also reach every seeded
success/rejection family; exact coverage counters are not cross-platform gates.

## Current execution evidence

Recorded 2026-09-10 on the local dirty performance working tree based on
`de737a0d`. Release retains `-O2`, full LTO and SIMD disabled. The follow-up
covers adjacent UI batching, FloatZ shading, combined sun/spot error checks,
pass-local lighting/raster/projection reuse, the enlarged texture-parameter memo,
sampler alias resolution, dynamic geometry buffer reuse and removal of unused
production model/technique label copies.
These results qualify the exercised local paths, not a committed release.

| Check | Result |
| --- | --- |
| Native MSVC x86 Release | Retained sampler source: surface and world-scene targets pass, including split/disabled aliases and complete unit/object state comparisons. |
| Direct Wasm Release | The same surface and world-scene targets pass. The larger texture fixture uses heap-backed comparison arrays and in-place memo reset to fit the normal Wasm test stack. |
| Node protocol/platform | 132/132 cases pass; pinned Node 24.18.0 / npm 11.16.0, `npm ci` completed. |
| Production Chromium | 56/56 cases pass. |
| Diagnostic Chromium | Current label cleanup: 10/10 smoke on first attempt, 68 remainder pass, 16 optional skips. Retail inputs disabled. The entire diagnostic site is byte-identical to the tested buffer-reuse build. |
| Installed Chrome/D3D11 | Current label cleanup passes five focused retained-recovery, dynamic atomic-upload, memory-identity and dynamic-light checks, including reused-spare failures and published GPU-byte retention. Earlier exact-pixel checks retain the known baseline one-byte discrepancies detailed below. |
| Owned Cargoship context recovery | Buffer reuse passes actual `WEBGL_lose_context` loss/restoration; all 12 resumed paused samples match canonical work counts and shadow selection. Before/after images are pixel-identical. |
| Product boundary | Label cleanup passes: 4,594,908 Wasm bytes, 324,578 JavaScript bytes, 5,022,845 site bytes; 18 raw / nine application exports, 22 files, 5% size headroom. |
| Release builds | Label-cleanup production and diagnostic snapshots pass their canonical runtime-prefix checks and public build receipt verification. Final documentation refresh preserves the measured engine binaries. |
| Headed production realtime | 1080p Cargoship A/B/B/A: 23.536/21.861/22.051/21.826 ms. Candidate 45.35-45.74 FPS; controls overlap. The average improvement does not establish a stable mission-wide gain or meet the 60 FPS target. |
| Muted headless production realtime | Fresh native-auto-continue A/B/B/A: 21.630/19.294/18.591/21.242 ms. State-reuse candidates 51.83-53.79 submitted FPS, controls 46.23-47.08. Mean frame time improves 11.6%; 60 FPS and full-mission/native parity remain unmet. |
| Matched paused rendering | Earlier state-reuse pair: 120 matching work-count/camera/time samples and complete GPU queries. Pre-recovery image is pixel-identical; post-recovery differs at two pixels by one byte unit. |
| GPU identity reuse | Fresh muted headless A/B/B/A: 21.100/20.752/20.290/20.970 ms, 2.4% shorter mean interval. Candidate paused pair matches 120 work/camera/time samples and completes 120 GPU queries; pre-recovery image differs at two pixels by one byte unit. Actual loss/restoration matches 12 resumed samples and shadow selection. Opening/deck evidence only. |
| Sampler / multi-draw comparison | Fresh muted headless A/B/C/C/B/A: 20.377/19.995/19.883/19.994/21.466/24.518 ms. Large baseline drift prevents a stable new FPS claim. Paused comparison matches 120 work/view samples and 120 GPU results per build; sampler/multi-draw images are identical and differ from the control by one byte in one pixel. Multi-draw was removed for lack of consistent additional benefit. |
| Dynamic buffer reuse | Fresh saved-bridge A/B/B/A: 11.397/11.224/11.107/11.379 ms; 2.0% shorter mean interval and 6.0% less CPU time. Exact save identity and twelve camera/projection/geometry checkpoints match. Separate opening A/B/B is 18.012/17.164/17.274 ms; closing control and retry were rejected before import for changed browser cadence, so opening improvement remains provisional. |
| Buffer-reuse paused rendering | 120 matching work/camera/time samples and 120 complete GPU queries. Control/candidate images differ by one byte in one pixel; candidate recovery is pixel-identical. This dark ship/ocean view has no selected spotlight maps. |
| Subsequent scheduling and label trials | Early animation-callback reservation measured 11.109/11.075/11.243/11.224 ms and was removed for no reliable gain. Production label-copy removal measured 11.145/11.152/11.080/11.185 ms; no additional FPS gain is claimed. Its production paused-camera pair is pixel-identical with matching workload and GL draw/upload counts. |

Logs are under ignored `build/performance/`: `cargo-state-{native,wasm}-build.log`,
`cargo-state2-{smoke,smoke-repeat,remainder,product,chrome,boundary}.log` and
`cargo-state-node.log`. `cargo-auto-comparison.json` records the realtime source
and environment comparison. The recovery capture is
`build/renderer-efficiency-de737a0d-cargo-auto-paused-b.json`.
The GPU-identity candidate reran Node (132/132), production (56/56), smoke,
remainder and product-boundary checks. Its logs use `cargo-identity-*` under
`build/performance/`; realtime and paused comparisons use the same prefix.
The sampler candidate's native/Wasm, Node, browser and boundary logs use
`cargo-alias-*`. The incomplete buffer-reuse comparison uses `cargo-buffer-*`:
21.711/20.694/52.262 ms, with concurrent game load observed after the repeat
candidate and the final control cancelled. That original comparison remains
inconclusive. The repeated and now retained buffer candidate uses
`cargo-buffer2-*`: Node 132/132, production 56/56, five focused installed-Chrome
cases, routine smoke/remainder and product-boundary checks pass. The initial
smoke failure and unchanged successful retry are both retained. Fresh saved-
bridge timings, paused comparisons, GL probes and image results use the same
prefix. Full mission completion and native timing parity remain unmeasured.
The frame-pump trial's four Node and three installed-Chrome checks passed before
removal; its evidence uses `cargo-pump-*`. Current label cleanup reruns Node
(132/132), smoke (10/10), remainder (68 pass/16 optional skips), production
(56/56), five focused installed-Chrome cases and the product boundary. These
logs, fresh saved-bridge comparisons and production images use `cargo-labels-*`.
The removed static-instance VAO trial uses `cargo-instance-*`. Production and
diagnostic builds and three focused installed-Chrome checks pass, including the
new collision/rebind/allocation-failure fixture. Fresh saved-bridge A/B/B/A is
11.279/11.489/11.462/11.302 ms; its fully checked regression prompted removal
of the candidate and extra fixture. The retained runtime and tests remain the
previously qualified label-cleanup version.
The removed UI spare-buffer trial uses `cargo-ui-buffer-*`. Production and
diagnostic builds and seven focused installed-Chrome checks passed before the
opening/deck A/B/B/A (17.487/21.138/17.547/17.244 ms) showed no benefit. Its later
inactive-recovery counter correction was unbuilt and untested; both versions
and the extra fixture were removed. No bridge comparison or broad-suite claim
is made for this discarded candidate.
The removed finite-vertex SIMD trials use `cargo-finite-*`. Both production
and diagnostic builds pass runtime-prefix/receipt verification. V1 passes
18 native and 18 Wasm surface checks plus the installed-Chrome dynamic upload
failure fixture; V2 passes the same 18 Wasm checks after changing only its SIMD
reduction. Fresh deck A/B/B/A comparisons show no stable game improvement, so
both implementations and their extra test were removed. The new isolated deck
checkpoint and 120-frame profile extend evidence past bridge/crew quarters;
developer positioning was used, and full-mission/native parity remain unproven.
After removing both trials, the complete generated production and diagnostic
sites match the tested label snapshots byte-for-byte. The first routine smoke
run passes eight cases and fails two during WebGL2 context creation; the
unchanged one-worker rerun passes all ten. Routine remainder with retail inputs
disabled passes 68 cases and skips 16 optional cases. An inherited retail-root
remainder run was stopped before completion and is not suite evidence. Logs
and initial failure traces use `cargo-finite-restored-*`. No assertion or
timeout was relaxed.
The retained-cloud follow-up adds compact/scalar transformation comparisons to
the existing native/Wasm cloud executable. Browser coverage compares outdoor
cloud shader pixels, retained lattice reuse, invalid constants, a real failed
new-lattice upload, context restoration and unload. Three focused installed-
Chrome cases pass, including the existing dynamic-upload transaction fixture.
Matched paused owned-deck captures preserve logical draws/triangles and recover
after real context loss, with at most one byte of raster difference. The fresh
production deck A/B/B/A passes all comparison checks and reduces mean frame
time by 18%; see `cargo-cloud-*` under ignored `build/performance/` and the
renderer guide for scope. An earlier 4 ms series was incomplete after cadence
changed; its timings are not combined with the accepted 2.78 ms series.
The final native and Wasm cloud executables pass after the fixture cleanup.
Both rebuilt sites are byte-identical to the measured cloud snapshots. Final
smoke passes 10 cases; remainder passes 68 and skips 16, with one startup
failure fetching `engine_protocol.mjs` (`net::ERR_NO_BUFFER_SPACE`) before
engine initialization. That unchanged focused retry passes. Production passes
56 cases and the product boundary check passes. Initial build-wrapper attempts
stopped on PowerShell warning handling and inherited module-path lookup; the
corrected wrapper completes both builds without renderer changes. Logs and
the preserved startup trace use `cargo-cloud-final-*` and
`cargo-cloud-remainder-startup-failure`.
The subsequent discarded brush-placement trial passes its focused installed-
Chrome actual-attribute/recovery test and matched paused deck draw/upload/pixel
checks. Production A/B/B/A passes provenance/workload checks, but its 0.68% mean
difference is not corroborated by the essentially tied paused scene stages.
The implementation and extra fixture were removed; `cargo-brush-*` retains
the private evidence and reversible patch. It adds no retained renderer policy.
The restored production and diagnostic sites are byte-identical to the measured
cloud builds. Their fresh smoke and full routine remainder pass 10 and 69 cases,
respectively, with 16 optional remainder skips and no failures. These runs use
`cargo-brush-restored-*`; production and boundary evidence remains applicable to
the identical site bytes. Subsequent isolated Worker sampling and GL wait probes
complete without page or GL errors, using `cargo-cloud-deck-{cpu,waits}`.
The discarded partial-parameter trial passes native/Wasm world-scene executables
and its expanded bundled-Chromium graphics case. Installed Chrome reaches the
same one-byte normal-lighting mismatch on both candidate and original baseline
fixtures; the baseline cannot run newly added wrap-field checks, so its original
test was restored for that comparison. Production deck A/B/B/A validates all
four runs but shows no gain. The implementation and extra assertions are removed;
logs and the reversible patch use `cargo-parameter-*`. No existing assertion was
relaxed, and no paused or recovery gain is claimed for the rejected trial.
The subsequent early-submission trial compiles in production and completes
four qualified deck windows, but is slower than both controls and is removed.
It retains all existing GL checks; no paused-image or context-recovery claim is
made for this discarded experiment. Evidence uses `cargo-submit-*`.
The retained FloatZ specialization adds direct full/specialized shader pixel
comparisons to the existing soft-particle fixture, covering signed depth,
alpha discard, translated/rotated rigid placement, soft particles, distortion
and actual context recovery. Its focused installed-Chrome test and authored
distortion case pass. The existing soft-particle test's final alpha assertion
returns 127 instead of 128 on both the candidate and the unchanged cloud
baseline; no assertion is weakened (`cargo-depth-focused-initial` and
`cargo-depth-baseline-soft-particles`). Production deck A/B/B/A passes all
comparison checks and reduces mean frame time by 6.9%. Matched paused deck
draw/upload, pixel and actual recovery checks pass; scope and raw artifacts
are recorded in the renderer guide under `cargo-depth-*`.
The separate paused opening comparison also preserves all views/work samples,
with one pixel differing by one byte before and after recovery. Final pinned-
Chromium smoke passes 10 cases and remainder passes 70 with 16 optional skips,
including both the original soft-particle test and the new specialization test.
Production passes 55 cases initially; one home-backup restart times out before
engine startup after Chromium fails to fetch `worker_transport.mjs` with
`net::ERR_NO_BUFFER_SPACE`. Its unchanged focused retry passes. The failure
trace is retained in `cargo-depth-product-startup-failure`; final test logs use
`cargo-depth-final-*`. The production boundary/export/size check passes.
Both final rebuilt sites match the measured depth-specialization snapshots
byte-for-byte. A subsequent fresh production A/B/B/A at the second saved deck
encounter passes all comparison checks and reduces mean interval by 5.0%; the
rejected pre-import cadence launch and successful unchanged retry are retained
under `cargo-depth-deck02-*`. An additional disposable camera-shader probe
shows no gain and adds no product changes. No additional native-parity or
unassisted mission-completion claim follows from these checks.
The retained compact-key camera sorter passes the native and Wasm DObj
submission executables, including 512 comparisons with the original stable-sort
policy. Installed Chrome passes authored distortion/order/context recovery;
fresh production A/B/B/A at both private deck saves and the matched paused
pixel/work/recovery comparison pass. Final pinned-Chromium smoke passes 10,
remainder passes 70 with 16 optional skips, and production passes all 56 cases.
The production export/size boundary check passes. Logs and measurements use
`cargo-sort-*`; the renderer guide records the small measured gain and limits.
The subsequent production material-label removal passes 10 smoke and 56
production cases and the export/size boundary check. Remainder initially passes
69 cases with 16 optional skips; the manifest-repair case encounters Chromium
`page.reload: net::ERR_NO_BUFFER_SPACE` before application checks. Its unchanged
focused retry passes, with the original trace retained under
`cargo-material-label-remainder-failure`. The diagnostic site remains
byte-identical to the qualified compact-sort build. Fresh production first-deck
A/B/B/A and paused pixel/GL-call comparisons pass; the renderer guide records
the small CPU improvement and graphics timing variation. Logs use
`cargo-material-label-*`.
The retained image-index hint passes the new synthetic first-match comparison
using 1,025 canonical identities, covering collisions, distinct pools,
reordering, growth, shrink, clearing and null input before and after context
recovery. Installed Chrome passes this fixture and the existing authored-mip
quality/recovery case. Final pinned-Chromium smoke passes 10, remainder passes
71 with 16 optional skips, and production passes all 56 cases. The production
export/size boundary check passes. Fresh production A/B/B/A at both private
deck saves and paused pixel/work/GL-call/context-recovery comparisons pass.
Logs use `cargo-image-hint-*`; the renderer guide records the small measured
improvement and overlap in later-deck timings.
The subsequent per-program uniform-replay probes preserve material-mode draw
counts, all seven GL error checks per frame and twenty paused-scene images
within one byte per channel. Their instrumentation/selection variants and
timing limits are recorded in the renderer guide under `cargo-*-lazy-*` and
`cargo-material-value-probe-*`. No production shader change or candidate
context-recovery qualification follows from those probes; the image-hint
runtime and its routine-suite results remain current.
The subsequent C++ mode-zero program trial builds in both Release variants and
passes all 15 bundled-Chromium graphics cases, including its additional real-GL
uniform synchronization/invalid-type fallback fixture before and after recovery.
Fresh production deck A/B/B/A passes all workload/provenance comparisons but
increases mean frame time 3.33%. The candidate and extra fixture are removed;
no later-deck or retail paused/recovery qualification is claimed for it. The
reversible patch, snapshots, focused checks and timing comparison use
`cargo-material-native-*` and `renderer-material-native-rejected.patch` under
ignored `build/performance/`.
After removal, fresh smoke/remainder/product checks pass 10/71/56 cases, with
16 optional remainder skips; the production boundary passes. Both rebuilt
sites match every measured image-hint site file byte-for-byte. Restoration
checks use `cargo-material-native-restored-*`. A subsequent disposable scalar
uniform probe preserves draw/error-check counts and four paused images within
one byte per channel. Its small instrumented timing difference does not
qualify an engine change; evidence uses `cargo-scalar-uniform-probe-*`.
The retained empty-shadow-batch change passes 16 focused bundled-Chromium
graphics/lighting cases. Fresh production first/later-deck A/B/B/A passes
workload and provenance comparisons; the later-deck pre-import cadence failure
and fresh successful retry are both retained. The first deck improves 0.92%;
the later deck has no demonstrated gain. Matched paused captures preserve
all draws, work samples and error checks, reduce redundant material calls,
complete 120 GPU queries and match images within one byte before/after actual
recovery. Evidence uses `cargo-shadow-empty-*`; its runtime change only moves
the existing visibility scan ahead of material setup. The renderer guide
records scope, timings and the separate current Worker CPU profiles.
Final smoke/remainder/product checks pass 10/71/56 cases, with 16 optional
remainder skips, and the production boundary passes. Both rebuilt site
inventories match the measured shadow-empty snapshots byte-for-byte. Logs and
verification receipts use `cargo-shadow-empty-final-*`.
The removed early-axis shadow-culling trial passes native/Wasm world and
static-model tests, including 4,096 differential cases and exact clip contacts,
16 focused graphics/lighting browser checks, and paused draw/pixel/recovery
comparisons. Four qualified production deck captures show no renderer-time
gain, so the implementation and extra fixture are removed. Evidence uses
`cargo-shadow-axis-*`; no later-deck speedup is claimed for the discarded trial.
The subsequent private bridge-firefight checkpoint reloads in fresh muted
headless Chrome with receipt, save, camera, geometry, graphics and clock checks.
Its production 60-second baseline measures 8.061 ms mean/13.625 ms p99 at the
new 2.778 ms browser cadence; it is not compared with earlier 4 ms measurements.
The separate diagnostic reload completes all 120 frame/view and GPU samples.
Artifacts use `cargo-bridge-360-forward-a1` and `cargo-bridge-360-profile-a1r`;
the renderer guide records the rejected exploratory attempts and scope. This
adds bridge-firefight coverage, not interior or full-mission qualification.
The removed spotlight-membership bitmap trial passes native/direct-Wasm
world and static-model targets, including 4,096 generated membership checks,
and production/diagnostic Release builds. Four fresh headless muted production
deck captures pass workload, camera, graphics, clock and reviewed source checks
at 2.778 ms cadence. Their 16.074/16.286/16.318/16.373 ms A/B/B/A intervals
show no useful gain; the implementation and extra fixture are removed before
paused/recovery or candidate browser-suite qualification. Evidence uses
`cargo-spot-membership-*`; the renderer guide records the matching retained-build
CPU profile and the separate rejected shadow-attribute probe.
The multi-draw candidate's native/Wasm, Node, focused browser, smoke, remainder,
production and boundary logs use `cargo-multidraw-*`. The original 65-draw fog
stress case could return all-white, indistinguishable from the fixture error
sentinel; the final chunk/tail comparison uses the non-white first pass while
retaining the two-pass pixel/order cases. No existing native pixel assertion
or runtime error check was relaxed.
The candidate and its extra fixtures are now archived, with a reversible patch
under `build/performance/`; multi-draw was removed before the buffer-reuse follow-up.
Its expanded synthetic fixture passed with the real extension, forced fallback,
visibility holes, chunk tail and context recovery before removal.

The preceding state-reuse candidate's initial smoke failure reported engine state `failed` while booting
the AA fixture. It did not recur on the unchanged one-worker rerun; no timeout or
assertion was weakened. Wider installed-Chrome graphics checks on the earlier
UI/FloatZ candidate reproduced three existing one-byte exact-pixel failures on
the clean original artifact too (`cargo-graphics-chrome{,-before}.log`). The
initial transient-light startup timeout passed on the final narrow rerun.
Paused recovery does not establish campaign or native/browser scene fidelity.
Full native/Wasm matrices, static lint/type tiers, source-package unit suites,
Linux/hosted CI, sanitizer fuzz, native gameplay, other missions, optional retail
acceptance and exhaustive browser duplicates were not rerun in this follow-up.
See the [renderer guide](renderer-retained-resources.md#cargoship-follow-up-on-de737a0d)
for timing scope and the unchanged optional transient-light fixture limitation.

The earlier 30-second Windows sanitizer pass, owned loading check and superseded
execution records remain in [Git history](../README.md#historical-records).


The local intro performance driver retains 120 canonical seconds from the first
rendered frame, per-second/phase tails, first-frame CPU, starting/ending graphics
and clock checks. `cargo-weighted-intro-*` contains four successful production
comparisons and one preserved startup-coverage failure (`b2`, first time 305 then
3500); the fresh `b2r` passes unchanged assertions. The removed weighted-decode
trial passes native/Wasm DObj checks and both Release builds but no useful FPS
comparison, so its paused/recovery and broad candidate suites are not run.
`cargo-intro125-diagnostic-a1` drains five separate 120-frame GPU captures.
The separate resolve probe has no useful timing gain, and its final unchanged
control exceeds the one-byte image tolerance; no graphics change is retained.
These are local intro investigations, not stable-125 or whole-mission evidence.

The removed `cargo-floatz-merge-*` trial passes native/direct-Wasm world tests
and both Release builds. Its paired paused checks assert all 120 views/work
samples, complete GPU results, expected draw/binding reductions, unchanged
error checks and actual context recovery. Images remain within the existing
one-byte tolerance. Installed Chrome passes the specialized FloatZ test but
reproduces the authored soft-particle alpha mismatch (127 versus 128) on the
unchanged baseline too; both focused tests pass in pinned Chromium. No assertion
is relaxed. Fresh production intro A/B/B/A rejects the trial as 2.33% slower;
its first `b1` coverage failure is retained and `b1r` passes. Broad trial suites
are not run after rejection. The renderer is restored from its pretrial bytes.

Clock-reset coverage extends `web_frame_profile_tests` in native and direct
Wasm, preserving post-reset elapsed time, real stalls, cap behavior and clock
wraparound. Three headless muted owned-Cargoship startup checks retain every
frame and reject loading catch-up; a separate canonical `map_restart` check
validates 20 seconds after restart. A complete 120-second production intro
passes unchanged coverage, clock, graphics and geometry assertions. These use
`cargo-clock-reset-*`; the first frame now follows native settling near time
300, so the older comparison's time-3500 expectation is not applicable.
Final clock-fix verification passes pinned npm ci, production/diagnostic
Release builds, 10 smoke and 71 remainder tests with 16 optional skips. Both
complete site inventories match the measured snapshots (22/17 files). The
separate final-link startup CPU capture also verifies exact Wasm identity.
Product/exhaustive suites and native gameplay are not rerun for this fix.


Texture-startup verification retains native/direct-Wasm `iwi_image_tests` and
synthetic Wasm DXT A/B/B/A hashes, including 768 clipped-edge cases. The existing
retained-texture browser check now tests cross-pool IWI source reuse, identity
and name rejection, unsupported/source-kind rejection, malformed-byte atomic
failure and pool retirement before and after actual context recovery. The first
fixture incorrectly reached the uninitialized canonical filesystem during a
negative lookup and triggered `fs_searchpaths`; the failure/trace is preserved.
The corrected fixture tests rejection at the same retained-source lookup used
by runtime, leaving the filesystem assertion intact. It and two focused
picmip/mip-bias checks pass in pinned Chromium.

Fresh headless muted production DXT-copy and retained-IWI startup A/B/B/A runs
preserve all coverage/provenance/graphics/clock checks. Both changes have fresh
matched paused work/GL/image comparisons and real context recovery; both full
120-second candidate intros pass, and retained-IWI also passes canonical map
restart. These do not establish stable 125 FPS. Final candidate Release builds,
10 smoke and 71 remainder tests pass with 16 optional skips; product/exhaustive
suites and native gameplay are not rerun. Evidence is under ignored
`cargo-dxt-copy-*`, `cargo-encoded-reuse-*` and `cargo-texture-startup-*`.

Static-instance texture verification extends the existing FloatZ pixel fixture.
It compares the original constant-attribute placement with actual RGBA32F upload
and vertex fetch, including a nonzero instance ID across a row boundary, partial
updates and rejected empty/out-of-range updates. Main and specialized FloatZ
results match before and after context recovery. That check, authored soft
particles and outdoor clouds pass in pinned Chromium after both Release builds.
No native parser or canonical engine behavior changes for this GPU storage seam.

Fresh headless muted clean production full-intro A/B/B/A at the newly observed
4 ms cadence passes every coverage, graphics, clock and provenance check:
12.31677/11.56053/11.52766/12.29654 ms, 6.20% lower mean. This is separate from
the earlier instrumented prototypes and 2.778 ms captures. A fresh paused pair
matches 120 views/work samples and GPU results; actual recovery matches 12 work
samples. Images differ by at most one byte before recovery and are exact after.
The call comparator asserts the exact added sampler/uniform calls and removed
pointer/buffer calls; every draw, error check and other logical counter remains.
Artifacts and measured snapshots use `cargo-instance-texture-cpp-*` under ignored
build. Stable 125 FPS, full-mission performance and native timing parity remain
unproved.

For this retained static-instance change, the routine tiers pass **10 smoke** and
**71 remainder** tests with **16 optional skips**, using one headless muted worker.
Both Release builds and three focused graphics checks pass. Product/exhaustive
duplicates, separate native parser matrices and native gameplay are not rerun.

The subsequent static-shadow multi-draw probe passes exact draw/check counts and
five images within one byte/channel, but overlapping paused timings reject it.
No runtime change or new browser test is retained. Fresh CPU attribution verifies
the exact `instance-texture-symbols` final link; five diagnostic stage captures
complete 120 frames/GPU results each. These are not clean FPS measurements.

The removed four-block skinning specialization passes native/direct-Wasm DObj
tests and both Release builds before clean full-intro A/B/B/A shows a 0.85%
regression (11.56090/11.67767/11.72749/11.64630 ms). Every coverage, graphics,
clock, provenance and environment assertion passes. Original source/tests are
restored exactly; extra malformed-block tests and trial builds remain under
ignored `cargo-skin-block-*`. Its prepared paused/recovery and broad-suite drivers
are unexecuted because the performance trial is rejected.

The later immutable-index `drawRangeElements` probe passes exact draw/check
counts, complete index-buffer equality and five one-byte image comparisons,
but overlapping timings reject it. Its earlier dynamic-index assumption failure
and biased forwarding/image-failure run remain recorded separately under
`cargo-draw-range-*`.

A shadow-bounds vertex-visit trial passes a synthetic direct-Wasm bounds/hash
check, both Release builds and the existing dynamic upload/recovery browser
fixture. The first fixture launch cannot create WebGL2; an unchanged fresh launch
passes. Clean full-intro A/B/B/A passes all 120 populated seconds and existing
source, driver, graphics, clock and environment checks, but mean intervals are
effectively identical at 11.56972/11.56908 ms. Both source/test files are restored
exactly; no extra test or runtime change remains. The prepared paused retail and
broad candidate suites are unexecuted after rejection. Evidence is under ignored
`cargo-shadow-vertex-visits-*` and `shadow-vertex-visit-bench.*`.

The separate `dxt-gpu-*` synthetic check compares 18 DXT1/3/5 images and authored
tail mips against the current Wasm decoder. All RGBA8 control uploads are exact
with no GL errors. Direct S3TC decoding exceeds the one-byte equivalence threshold
(up to six green/seven alpha values on RTX 3070 Ti), so the check intentionally
fails and no runtime upload path is added. Earlier invalid tiny-base/mip-state
probe results remain distinct; this is fidelity evidence, not performance or
native-parity qualification.
