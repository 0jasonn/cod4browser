# DObj conversion and dynamic shadow milestone

Runtime `30e34cff` completes fused DObj conversion, reusable numeric geometry
storage, selective LTO of canonical helpers, and dynamic opaque sun-range merging.
The final diagnostic comparison observes **41.1% lower DObj build time**
and **59.1% lower combined skinning/geometry time**. Fresh production
A/B/B/A means are **15.461 -> 14.296 ms (7.54% lower)**. These are local paused
renderer measurements; they do not establish gameplay FPS or pixel equivalence.

## Implementation and ownership

Skinning writes directly into the final vertex span; index emission constructs
one span per surface. Numeric vertex/index capacity is recycled only after the
backend has synchronously copied the command, and is released on world unload.
No canonical pointers, poses, visibility, lighting or completed commands are
cached. Invalid input still leaves the published destination unchanged.

Web-only selective LTO exposes the DObj/lighting adapters and existing Kisak
packing/quaternion helpers to the optimizer. It enables cross-file optimization
without duplicating those helpers or disabling exceptions and finite checks.
No native source behavior, Wasm SIMD requirement or browser capability was added.

Dynamic sun ranges reuse the world range-joining implementation. Only contiguous
opaque ranges sharing the same buffer and placement merge; cutouts, omitted
casters, depth-hack/FX exclusions, gaps and brush-instance changes break a run.
Static camera culling, static shadow instances, authored spot membership and
independent sun rendering are unchanged.

## Controlled production result

Both control runs use the saved milestone-start production artifact (runtime
`49af3948`, unchanged by documentation/tool commits through `9c2a3a42`). Both
candidate runs use the one final Release at `30e34cff`. Exact artifact and raw
record hashes are in the numeric evidence (archived in Git).

Fresh headless Chrome 152.0.7977.64 profiles on the same Ryzen 7 7800X3D host use
1440x1000 windows, seed 1, fixedtime 16, the same paused camera and six canonical
view checkpoints. Each timing window spans 300 frames (five 60-frame spans).
`com_maxfps 0` is set only for these benchmark sessions; the product's default
60 FPS setting and web 125 Hz safety ceiling remain unchanged. The profiler is
compiled out. No builds or competing task benchmarks overlapped these windows.

| Run, execution order | Mean frame interval, ms |
| --- | ---: |
| A1 control | 16.059 |
| B1 candidate | 14.329 |
| B2 candidate | 14.262 |
| A2 control | 14.863 |

Both candidate means are below both controls. The controls drift from 16.059 to
14.863 ms, so the pair-mean reduction is an observation, not a precision guarantee.
Span percentiles are not per-frame latency percentiles. No new default-cap
production comparison was run.

## Diagnostic attribution and work equality

All 120 final candidate samples match both valid controls in camera/time,
world/static/dynamic work, indices and uploads. Actual shadow submissions fall
12,382 -> 11,008; merged ranges rise 4,114 -> 5,488. Their sum stays 16,496:
**1,374 additional sun draw calls are avoided per frame**. Submitted indices
remain 4,816,164 and buffer uploads 3,964,368 bytes. Static instance draws stay
1,872 and world camera surfaces stay 5,471.

| Profiled CPU interval | Closing control, ms | Final candidate, ms |
| --- | ---: | ---: |
| DObj build | 6.074 | 3.576 |
| Skinning plus geometry | 4.301 | 1.759 |
| Lighting | 1.111 | 1.147 |
| Index emission | 0.339 | 0.197 |

The candidate was profiled before production A/B/B/A; the closing control after
it. The earlier valid control measured 6.371 ms in DObj build. Host/JIT variation
is visible: profiler-inactive diagnostic intervals were 20.324 ms for that first
control, 17.998 ms for the final candidate and 16.576 ms for the closing control.
These diagnostic windows do not establish a whole-frame gain; use production
A/B/B/A for that observation. The separate vertex-emission interval is now zero
because it is included in skinning, so only combined intervals compare directly.

Fusion/recycling alone showed only about 5% lower DObj build time in the first
comparison. Selective LTO, shadow merging and span index emission followed.
All intermediate numeric records are retained in the evidence. An initial control
overlapping a diagnostic build was explicitly excluded and repeated.

## Validation and limits

- One focused Win32 Debug target, `web_renderer_dobj_submission_tests`, passed
  four iterations, including the final index change (1/1, 0.03 s). Synthetic,
  repository-authored fixtures cover one to four weights, rigid lists, changing
  poses, attributes, index order, malformed-data atomicity, storage reuse,
  shadow placement/cutouts/gaps/overflow and the existing world range adapter.
  Pose/quaternion/packing dependencies are stubbed; this is not full animation
  or packed-asset parity coverage and does not inject allocation failure.
- Four diagnostic Release builds passed. The final recovery check exercised the
  existing explicit loss/restore hooks: generation 1 -> 2 and twelve matching
  resumed work samples. This does not inject an actual GPU driver loss.
- One final production Release passed in 17.328 s, including the existing
  canonical runtime-prefix check. No runtime edits followed it.
- The ordinary workload comparator remains strict. The new explicit shadow-range
  comparison normalizes only submitted-plus-merged caster counts, then requires
  all other work to match. Negative checks reject corrupted indices, uploads,
  static selection and caster totals. All final browser windows had no page errors.
- No broad suite, mission check, screenshot or compatibility promotion was run.
  Frontend numeric capacity is additional retained CPU storage; existing backend
  memory counters do not include it. No total-memory reduction is claimed.

Reproduce with `tools/profile_web_renderer.mjs`, the configured owned-asset
environment, and only the selected generated site served on port 8051. Use
`fixedtime recovery` for diagnostics, `fixedtime uncapped` for production, and
`tools/renderer_workload.mjs --shadow-ranges` for final diagnostic qualification.
Retail data, installation paths and runtime logs remain outside version control.

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/dobj-conversion-30e34cff.json`.
