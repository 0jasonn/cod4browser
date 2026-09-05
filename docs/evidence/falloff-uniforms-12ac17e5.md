# Conditional distance-falloff uniform uploads

Continuation from `a646c09c`, recorded 2026-08-31. `12ac17e5` changes only
`ApplyWorldMaterialState`: upload the three falloff vectors when the batch uses
`VertexColorDistanceFalloff`. The vertex shader reads them only in material
mode 4, which requires that technique. Other modes ignore them. Every
distance-falloff draw still uploads all three vectors, including after another
technique or context restoration; there is no remembered-value tracker.

All four callers (world, static model, dynamic/FX, and sun flare) use the same
guard. Canonical material selection and constants, finite-value validation,
draw order, camera/static culling, and independent sun/spot caster passes are
unchanged. This is permanent backend uniform handling, not engine state or a
new intermediate representation. No shader arithmetic or other state changes.

## Comparison

Numeric evidence (archived in Git) compares clean source builds
`a646c09c` and `12ac17e5`. Both used Chrome 152.0.7977.64 headless, Ryzen 7
7800X3D, 1440 x 1000, fresh Playwright-owned persistent profiles, portable local
installation import and `map cargoship`. Each window collected 120 completed
gameplay frames after 30 drawn world frames, without gameplay input. Durations
were 6.728 s and 6.522 s. No build overlapped a profile, and neither window had
page errors or DOM focus/visibility transitions.

| CPU interval | Before mean ms | After mean ms | Before p95 ms | After p95 ms |
| --- | ---: | ---: | ---: | ---: |
| Dynamic material setup | 1.747 | 1.248 | 2.175 | 1.630 |
| Dynamic texture setup | 2.257 | 2.233 | 2.875 | 2.915 |
| Dynamic draw submission | 0.433 | 0.429 | 0.565 | 0.570 |
| Dynamic-model draw total | 6.993 | 6.417 | 8.530 | 7.915 |
| World draw | 3.062 | 2.918 | 4.085 | 3.720 |
| Static-model draw | 1.772 | 1.663 | 2.455 | 2.245 |
| Renderer backend | 22.564 | 21.630 | 25.795 | 24.065 |
| Scene construction | 27.148 | 26.222 | 30.485 | 29.630 |
| Profiled total CPU | 54.423 | 52.471 | 59.430 | 58.120 |

Dynamic material mean decreased 28.58%, and dynamic-model drawing 8.24%.
The reduction supports removing unused uploads. Scene construction also fell
despite being unmodified: do not attribute the total CPU difference entirely
to this guard or claim a general FPS improvement. These windows were slower
than the previous texture milestone's windows, further limiting cross-run
comparisons. No new timing fields or instrumentation were added.

Submitted world surfaces stayed 13,125. Mean camera-visible world surfaces
were 5010.233 -> 5146.875, static-instance draws 1699.300 -> 1745.992, dynamic
batches 1390.567 -> 1389.833, and shadow draw calls 9518.375 -> 9517.917.
The authored scene moves, so workloads are similar rather than identical.
Headless execution, active profiling/GPU queries and host variation limit
attribution; there was no profiling-disabled benchmark or visual comparison.

## Checks and limits

- Built and ran the existing Win32 Debug `web_renderer_world_scene_tests`
  fixture with CTest `--test-dir build/portable-tests-msvc18-win32 -C Debug
  -R '^web_renderer_world_scene_tests$' --output-on-failure --timeout 20`.
  1/1 passed (0.03 s test / 0.05 s total). It checks canonical distance-falloff
  technique selection and exact material constants, alongside its existing
  command ordering, validation and shadow-membership checks. No new test was
  added. This native fixture does not execute the GL upload guard or shader;
  their exclusive-use contract was checked in source. The retail profiles do
  not establish coverage of distance-falloff technique transitions.
- Two diagnostic Release builds passed (12.876 s baseline, 14.556 s after),
  each followed by one successful 120-frame profile. Existing sample-count,
  nonnegative timing and nested-interval checks passed.
- One final `tools/build_web.ps1 -Configuration Release` passed (13.186 s).
  All builds passed the existing runtime-prefix check. Existing toolchain and
  compiler warnings remain. No build, test or profile retry was needed.
- `git diff --check` and caller/shader inspection passed. Both task browsers
  and the serving process were closed; Playwright handled its profile cleanup.

No broad suite, mission check, screenshot, lifecycle test or playability
promotion ran. Committed evidence includes only metadata and numeric aggregates,
not retail assets, asset logs or installation paths.

Next: measure repeated view/projection setup in the dynamic camera passes
before deciding whether to move those uploads out of individual draws. Keep
sun-query/sprite overrides and depth-hack ordering explicit; no global state
cache is justified by this change.

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/falloff-uniforms-12ac17e5.json`.
