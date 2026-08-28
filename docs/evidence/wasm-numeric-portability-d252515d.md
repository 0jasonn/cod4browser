# Wasm numeric-portability audit at `d252515d`

## Scope and method

This is a focused audit of decompiler-style floating-point representation
tricks in the offline single-player runtime. It is not a mechanical rewrite.
The audit searched the shared/SP game, cgame, client, server, script, UI,
qcommon, universal, Effects, and DynEntity sources for:

- `long double` storage assumptions;
- `double *` / `float *` pointer puns;
- float-related `reinterpret_cast` expressions;
- serialized or stack-backed alignment assumptions; and
- union-style floating representations.

The broad searches report 365 `long double`/double-pun textual matches and
356 float-pun/reinterpret textual matches. These populations overlap and are
not defect counts. They include declarations, disabled code, deliberate
serialized field access, and repeated reads of the same storage.

The portable test records the ABI difference that makes the confirmed family
unsafe: Windows native uses an 8-byte `long double`, while Emscripten uses a
16-byte `long double`. Writing only the first 8 bytes through `double *` does
not create a valid Wasm `long double`.

## Classification

| Classification | Sites/families | Finding |
| --- | --- | --- |
| safe | `scr_vm.cpp:4157-4183`; disabled `cl_input.cpp:1133-1223` block | Pointer-to-`uintptr_t` range comparisons do not inspect floating representation. The earlier mouse-move decompile is excluded by `#if 0`. |
| native-only | `qcommon/msg_mp.cpp`; `qcommon/net_chan_mp.cpp`; Win32 timing/platform code | Multiplayer transport is not in the initial offline Wasm target. Win32 platform implementations are not linked into the web build. No browser behavior change was made. |
| portable | `universal/timing.cpp:11-52`; `universal/profile.cpp:849`; `game/actor.cpp:2101` | Ordinary typed arithmetic or signatures do not depend on overwriting part of a `long double`. |
| suspicious | `game/g_scr_main.cpp:6208-9333`; `game/actor_fields.cpp:313-326,884-984`; `game/g_client_script_cmd.cpp:3231-3296`; `game/g_vehicle_path.cpp:652-712`; `game/g_hudelem.cpp:1211-1236` | Campaign-reachable script time, field, selector, vehicle-angle, and HUD conversions still contain 8-byte/`long double` representation assumptions. Each requires an intended-behavior comparison and native/Wasm test before replacement. |
| suspicious | `cgame/cg_servercmds.cpp:53-2218`; `client/cl_input.cpp:1445-1500`; `cgame/cg_view.cpp`; `cgame/cg_newdraw.cpp`; `cgame/cg_modelpreviewer.cpp`; `cgame/cg_compass.cpp` | Active client/cgame parsing, time conversion, view, compass, and UI paths contain the same family or alignment-sensitive decompiler casts. They remain queued for behavioral isolation rather than bulk editing. |
| suspicious, debug-only | `game/pathnode.cpp:1069-1112`; model preview and debug-only branches in the cgame/client files | These casts still rely on the decompiled representation and are not portable, but the paths are not campaign-priority behavior. |
| suspicious | `ui/ui_shared_obj.cpp:3545-4011`; `script/scr_vm.cpp:1557-1605`; `qcommon/msg.cpp:459-1384`; `universal/q_shared.cpp:321-348` | UI expression-stack storage assumes double alignment/layout inside an enum array. Script-bytecode readers assume aligned typed storage. Message and endian helpers deliberately access serialized representations but still need aliasing/alignment proof. |
| confirmed Wasm issue, previously corrected | `game/g_spawn.cpp:96-109`; `game/actor_orientation.cpp:88-123`; `game/actor.cpp:1043-1065`; `game/g_scr_main.cpp:7133-7136`; `cgame/cg_pose.cpp:104-115` | Direct conversion/math replaced partial `long double` writes in map spawn floats, actor angle wrapping, prone blend rounding, earthquake duration, and turret yaw wrapping. Commits: `268756c5`, `7f067177`, `7252d127`, `5c3b0fce`, `82d55132`. |
| confirmed Wasm issue, corrected here | `game/g_scr_main.cpp:1488-1536,5708-5715` | `getdvarfloat`, `getdebugdvarfloat`, `floor`, and `ceil` used 8-byte pointer writes/reads through 16-byte Wasm `long double` objects. The direct operations match the existing multiplayer implementation and preserve native COD semantics. |

The largest remaining textual groups are `g_scr_main.cpp` (59 matches),
`cg_servercmds.cpp` (51), `ui_shared_obj.cpp` (40), `cg_view.cpp` (18),
`cg_newdraw.cpp` (17), `cg_modelpreviewer.cpp` (16),
`actor_fields.cpp` (15), `cl_input.cpp` (14), `cl_main.cpp` (12), and
`turret.cpp` (11). A match remains suspicious until its live path, storage
contract, and expected result are proven; it is not automatically a defect.

## Corrected behavior

The SP script builtins now call a narrow typed math seam:

```text
string dvar value -> atof -> float -> Scr_AddFloat
script float -> floor/ceil -> float -> Scr_AddFloat
```

This is the same mathematical behavior used by
`game_mp/g_scr_main_mp.cpp`. No browser-owned script or mission state was
introduced.

## Focused verification

| Check | Result |
| --- | --- |
| Native Clang `qcommon_math_tests` | PASS |
| Emscripten/Wasm `qcommon_math_tests.cjs` | PASS |
| Diagnostics Release Wasm build | PASS |
| Strict canonical runtime-prefix check | PASS |
| `git diff --check` before commit | PASS (line-ending notices only) |

The behavioral cases cover positive and negative dvar parsing, the existing
`atof` trailing-text behavior, integral inputs, fractional inputs, and
positive/negative `floor` and `ceil` boundaries. The same source-backed helper
is called by production SP code and both test platforms.

## Next audit boundary

The next highest-value candidates are the remaining SP script millisecond
conversions, followed by cgame server-command float parsing. They should be
handled as separate, behavior-proven fixes. The UI expression evaluator needs
its own typed-stack design/test decision and must not be folded into a broad
search-and-replace.
