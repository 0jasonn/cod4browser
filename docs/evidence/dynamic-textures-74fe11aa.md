# Dynamic draw texture setup

Continuation from `eb5f7839`, recorded 2026-08-31. `5cb7ee48` adds three nested
dynamic-model draw intervals; `74fe11aa` skips consecutive identical texture
binding sets. Both profile source trees were clean. See the
numeric comparison (archived in Git).

## Implementation and boundary

The baseline placed 2.041 ms in six texture setups per dynamic batch, 1.187 ms
in material state and 0.320 ms in draw submission. Native `R_SetSampler` already
avoids redundant texture/state changes. At the WebGL boundary, however,
`BindWorldTexture` sets sampler parameters on the texture object itself. Skipping
individual units can change the result when two units alias one texture.

The backend now remembers only the last complete six-texture/four-sampler set
within the dynamic drawing block. An identical set needs no rebinding. A change
to any field reapplies all six calls in their original order, including the two
fixed lightmap samplers. This preserves existing alias behavior; it does not
correct or reinterpret sampler sharing. The small value lives on the stack,
allocates no heap storage and is recreated for each frame. It survives the
ordinary/depth-hack loop transition because that transition changes no textures.

The helper's contract requires exclusive ownership of these six 2D bindings
and their texture parameters between calls. Current sun visibility queries
change no textures, reflections use cube textures on unit 8, and spot-shadow
receivers bind their separate depth textures on unit 14. The block resets the
active unit before leaving. If a future draw path mutates the tracked bindings
or their texture parameters, reset the local tracker before the next call.

No batches are reordered or dropped. World/static-model drawing, canonical
camera culling, depth-hack ordering, independent sun/spot caster passes,
validation, resource publication and context recovery remain unchanged.
The optimization also applies to FX batches sharing this dynamic buffer;
the new nested timers follow the existing dynamic-model bucket and exclude
FX buckets and the sun-query branch. This is permanent backend state handling,
not a new engine representation or persistent GPU/pose cache.

## Observed comparison

Both runs used Chrome 152.0.7977.64 headless, Ryzen 7 7800X3D, 1440 x 1000,
fresh Playwright-owned persistent profiles and portable local-installation
import. `map cargoship` ran without gameplay input. Each window began after
30 drawn world frames and collected 120 completed gameplay frames. Durations
were 5.252 s before and 5.295 s after. No build overlapped a profile; no page
errors or DOM focus/visibility transitions occurred.

| CPU interval | Before mean ms | After mean ms | Before p95 ms | After p95 ms |
| --- | ---: | ---: | ---: | ---: |
| Dynamic texture setup | 2.041 | 1.538 | 2.810 | 1.965 |
| Dynamic material state | 1.187 | 1.133 | 1.635 | 1.430 |
| Dynamic draw submission | 0.320 | 0.296 | 0.435 | 0.395 |
| Dynamic-model draw total | 5.299 | 4.710 | 6.165 | 5.495 |
| Renderer backend | 16.553 | 16.076 | 18.210 | 17.635 |
| Scene construction | 21.894 | 22.765 | 23.395 | 24.925 |
| Profiled total CPU | 41.928 | 42.346 | 44.890 | 46.440 |

Texture-setup mean decreased 24.62%; dynamic-model draw mean decreased 11.12%.
Total CPU increased slightly as scene/DObj work varied. No whole-frame or FPS
improvement is claimed. The three nested intervals do not cover all draw-loop
work: uniforms, image lookup, view/depth setup and profiling overhead remain
outside them. They must not be added to their parent a second time. Timers
compile out of production and are inactive outside a diagnostic capture.

Frame-wide texture bindings averaged 17,868.917 -> 15,738.908. Dynamic batches
were 1389.600 -> 1390.900, world surfaces submitted stayed 13,125, camera-visible
world surfaces were 4668.975 -> 4698.217, and static-instance draws were
1594.167 -> 1603.383. Sun/spot shadow draw calls stayed 9,515 per frame.
The moving authored scene makes these similar workloads, not identical traces.
Headless execution, active profiling/GPU queries and host variation limit
attribution. There was no profiling-disabled benchmark or visual assessment.

## Validation

- Built and ran only Win32 Debug `web_renderer_surface_tests`, with CTest
  `--test-dir build/portable-tests-msvc18-win32 -C Debug -R
  '^web_renderer_surface_tests$' --output-on-failure --timeout 20`.
  1/1 passed (9 fixture checks, 0.04 s test / 0.06 s total). The added check
  executes the actual binding helper and compares simulated unit bindings and
  texture-object sampler state against unconditional ordered binding. It covers
  every key field changing, repeats, conflicting aliases, returns to earlier
  sets, first zero-valued input and reset for a new pass. It does not execute
  GL, loss/recovery, or the sun-query path.
- Two diagnostic Release builds passed (15.821 s and 13.208 s), each followed
  by one successful 120-frame profile. The runner verified all new fields have
  120 nonnegative samples and their sum fits within the parent interval on
  every frame, with 0.001 ms tolerance. Existing scene/submission checks passed.
- One final `tools/build_web.ps1 -Configuration Release` passed (15.312 s).
  All builds passed the existing runtime-prefix check. Existing toolchain and
  compiler warnings remain. No build, test or profile retry was needed.
- Source/caller inspection and `git diff --check` passed. Both task browsers
  and the serving process were closed; Playwright cleaned its own profiles.

No broad suite, mission check, mandatory image capture, lifecycle test or
playability promotion was performed. Committed evidence contains only metadata
and numeric aggregates, with no retail assets, asset logs or installation paths.

Next task: inspect the remaining per-batch material/uniform setup in the dynamic
draw loop. Material state alone still averages 1.133 ms. Measure reuse before
adding another tracker, and retain draw order, canonical culling and independent
shadows. A global GL-state cache or sampler-object conversion is not required
by this result.

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/dynamic-textures-74fe11aa.json`.
