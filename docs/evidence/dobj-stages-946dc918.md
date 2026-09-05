# Focused CargoShip DObj stage profile

Recorded 2026-08-31 from clean `946dc91815b7158cd8c9ab67218217cef9b8ddf1`.
The user requested the next profiling task after world-camera filtering.
This is measurement and a next-step recommendation, not an optimization.
Sanitized numeric evidence (archived in Git) contains no installation
path, retail file contents, asset names or source data.

## Result

Geometry construction is the largest measured DObj substage in this window.
Means and p95 are milliseconds per completed gameplay frame; percentages divide
stage means by the DObj build mean, not by total frame time.

| Stage | Mean ms | p95 ms | Share of DObj build |
| --- | ---: | ---: | ---: |
| Geometry construction | 7.192 | 8.015 | 59.02% |
| Skinning and bone-matrix preparation | 2.810 | 3.270 | 23.06% |
| Lighting | 1.341 | 1.580 | 11.01% |
| Canonical pose calculation | 0.300 | 0.360 | 2.46% |
| Unassigned DObj work / timer gaps | 0.543 | 0.730 | 4.45% |
| **DObj build total** | **12.186** | **14.005** | **100%** |

The four substages are disjoint and nested inside the DObj build. Unassigned
work is calculated per frame before aggregation; percentile values cannot be
added. Geometry includes vertex conversion/validation, index emission, material
lookup and draw-batch construction, so this profile does not isolate any one
of those operations.

Scene construction averaged 32.288 ms. DObj building accounts for about 37.7%
of that interval, leaving about 20.102 ms outside this DObj build timer.
Geometry is therefore the first DObj target, not a demonstrated majority of
whole-scene or whole-frame cost. Pose caching is not justified by these results.

## Method and execution

- One current diagnostic build: `tools/build_web.ps1 -Configuration Release
  -Diagnostics`, passed in 76.223 s including its existing canonical runtime
  prefix check. Existing compiler/toolchain warnings remain.
- Local generated diagnostic site served on loopback port 8051; fresh isolated
  persistent Chrome profile, using the normal portable folder picker and
  canonical filesystem mount. The original installation was unchanged and
  browser copies stayed outside the repository.
- Chrome 152.0.7977.64, **headless**, 1440 x 1000 viewport, Ryzen 7 7800X3D,
  approximately 32 GiB RAM. No renderer/GPU comparison is claimed.
- Submitted `map cargoship`, waited for 30 drawn world frames, then called the
  existing `_KisakWeb_TestBeginFrameProfileWithTimeout(120, 30000)` export.
  No gameplay input was sent; authored scene motion/scripts continued normally.
- Collected all 120 completed gameplay-frame samples in 7.112 s. Existing
  aggregation helpers produced every DObj field; no page errors were reported.
  Page visibility/focus stayed true with no recorded transitions. These DOM
  checks do not make headless Chrome a foreground benchmark.
- The normal profiler's rotating GPU queries remained enabled, but this task
  did not wait for all GPU results or interpret them. There was no independent
  profiling-disabled window, so profiling overhead and clean FPS are unknown.

Capture setup required corrections. The first headed, nonpersistent context
closed before import completed and produced no samples; its closure cause was
not established. A headless nonpersistent retry reported import failure. The
one-off driver was changed to use the persistent context already used by the
repository's retail harness; that run imported and completed the profile. No
product import code or assertions were changed. The driver stayed in ignored
`build/`; no permanent profiling framework was added.
The task browser and loopback server were closed. Removing the isolated
temporary browser profile was blocked by execution policy, so its local browser
copy remains in the host's temporary directory; it is not part of this commit.

No native/browser test suite, production rebuild, screenshot, mission action,
progression, context-loss, replay, save/death/restart or campaign matrix ran.
This demonstrates a running diagnostic CargoShip profile, not newly verified
retail visuals, playability, compatibility classification or a measured speedup.
Headless mode, the short opening-scene window and active profiling limit
comparisons with historical headed 300-frame/60-second captures.

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/dobj-stages-946dc918.json`.
