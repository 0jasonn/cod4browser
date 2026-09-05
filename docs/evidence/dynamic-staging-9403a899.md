# Dynamic geometry staging and submission profile

Continuation from `9b36d305`, recorded 2026-08-31. `bbd6edf0` separates dynamic
copy, geometry upload, texture upload and publication. `a542549f` isolates
geometry checks, geometry allocation/copy and batch processing inside command
copy. `9403a899` reuses dynamic vertex/index staging storage. All three profile
source trees were clean; numeric evidence (archived in Git)
includes the exploratory run as well as the matched comparison.

## Finding and implementation

The previous 8.723 ms dynamic-submission average did not reproduce in the first
exploratory profile (5.563 ms). Do not treat that earlier increase as a proven
regression caused by cloud capacity. The narrower baseline measured geometry
allocation/copy at 1.621 ms mean, 0.610 ms median and 3.480 ms p95, exposing
variation within the 4.071 ms command-copy stage.

The backend now retains two staging vectors for dynamic vertices and indices.
Existing geometry validation still runs before copying. A small portable copy
helper fills staging with ordinary vector assignment, reusing capacity when it
fits. The published geometry remains untouched through descriptor validation,
batch/image processing and GPU upload. Only successful submission swaps staging
and retained geometry, then clears the old command's logical sizes. Staging may
change on failure; published geometry does not. This keeps the existing backend
publication order without adding pose caches, GPU-buffer reuse or engine state.

World publication uses the same copy helper with its existing local vectors;
static-model copying is unchanged. World unload releases both staging vectors.
Context restoration continues to read only published geometry. No canonical
asset identity, draw order, finite/index checks, camera culling, LOD behavior,
independent sun/spot shadow policy or native game code changed.

## Observed comparison

Both matched runs used Chrome 152.0.7977.64 headless on the same Ryzen 7 7800X3D,
1440 x 1000, a fresh Playwright-owned persistent profile, the portable local
installation import, and `map cargoship`. After 30 drawn world frames, each
collected 120 completed gameplay frames with identical timing fields and no
gameplay input. Capture duration was 6.052 s before and 6.059 s after. No build
ran during a profile; no page errors or DOM focus/visibility transitions occurred.

| CPU interval | Before mean ms | After mean ms | Before p95 ms | After p95 ms |
| --- | ---: | ---: | ---: | ---: |
| Geometry allocation/copy | 1.621 | 0.815 | 3.480 | 0.955 |
| Geometry validation | 0.904 | 1.001 | 1.120 | 1.130 |
| Batch/image processing | 1.537 | 1.590 | 1.885 | 1.880 |
| Dynamic copy total | 4.071 | 3.415 | 6.395 | 3.970 |
| Geometry resource creation/upload | 1.989 | 1.825 | 3.070 | 3.065 |
| Dynamic submission total | 6.442 | 5.668 | 8.765 | 6.530 |
| Scene construction total | 23.790 | 23.714 | 26.585 | 27.720 |
| Profiled total CPU | 48.688 | 48.686 | 53.770 | 53.555 |

Geometry-copy mean decreased 49.75% and dynamic-submission mean 12.02%.
Geometry-copy median increased from 0.610 to 0.860 ms: this is primarily an
improvement in the slower samples, not every frame. Total CPU was essentially
unchanged; no whole-frame speedup is claimed. DObj and assembly timings varied
upward in the second window.

The post-profile diagnostic snapshot measured 35,572,944 bytes (33.925 MiB)
across retained and staging dynamic-geometry capacities, of which 17,786,160
bytes (16.962 MiB) were staging. That staging capacity persists between frames
and is the memory cost of avoiding repeated allocation. This snapshot is not a
peak-memory measurement or a before/after heap comparison. Existing recovery
byte fields still describe recovery data; the new diagnostic capacity fields
report staging separately so it is not mistaken for required recovery content.

Mean dynamic batches were 1390.767 -> 1390.333, particle batches 22.833 -> 23.875,
indices 5,443,978 -> 5,444,836 and buffer-upload bytes 17,885,023 -> 18,175,989.
Submitted world surfaces stayed 13,125; camera-visible counts varied slightly.
The authored scene continues moving, so the windows have similar workloads,
not identical traces. Headless execution, active profiling/GPU queries and host
variation limit attribution. There was no profiling-disabled benchmark, full
GPU analysis, visual assessment or playability promotion.

## Diagnostic boundaries and checks

Four disjoint dynamic-submission intervals cover command validation/copy and
lighting-atlas preparation; geometry resource creation/upload; image/lighting
texture creation/upload; and old-resource retirement plus publication/reporting.
The three `command*` intervals nest within command copy in a warmed dynamic
scene. They also measure initial world-command publication if that occurs in a
capture, so they are not universally exclusive to dynamic scenes. Upload timers
overlap resource intervals. Do not add nested intervals twice. All new timers
and capacity snapshots compile out of production; inactive captures do not time.

- Built and ran only Win32 Debug `web_renderer_surface_tests`, using
  `ctest --test-dir build/portable-tests-msvc18-win32 -C Debug -R
  '^web_renderer_surface_tests$' --output-on-failure --timeout 20`.
  1/1 passed (8 fixture checks, 0.03 s test / 0.05 s total). The new check runs
  the actual portable copy helper and verifies ownership, allocation reuse for
  fitting replacements, shrinking without stale tails, growth and empty input.
  Existing descriptor, finite/index, draw and owned-copy failure checks remain.
  This fixture does not execute full WebGL failure/context-loss paths or inject
  allocation failures into the new staging helper.
- Three diagnostic Release builds passed: 16.110 s for initial attribution,
  14.132 s for the narrower baseline and 13.925 s for the implementation. Each
  had one successful 120-frame profile. The runner checked sample populations,
  nonnegative intervals and nested interval containment (0.001 ms tolerance).
  The final run also checked the diagnostic staging-capacity snapshot.
- One final `tools/build_web.ps1 -Configuration Release` passed (15.912 s).
  All four builds passed their existing runtime-prefix check. Existing compiler
  and toolchain warnings remain. No build/test/profile retry was needed.
- Source/caller/publication/unload inspection and `git diff --check` passed.
  The aggregation fixture was extended, but not separately run; the profiles
  exercised its production aggregation helper and actual frame-event fields.

No assertions were weakened. No broad suite, mission check, screenshot, replay,
context recovery, unload execution or save/death/restart test ran. All three
task browsers and the server were closed; Playwright handled their temporary
profile cleanup. Earlier retained profiles were not touched. Committed evidence
contains no retail content, asset logs or installation paths.

Next task: inspect redundant material/state work in the dynamic-model draw
pass, which averaged 6.116 ms in this profile. Preserve draw order, culling and
independent shadow passes, and use a focused comparison before claiming gains.
Avoid adding more staging storage or changing GPU allocation policy based on
the submission total alone.

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/dynamic-staging-9403a899.json`.
