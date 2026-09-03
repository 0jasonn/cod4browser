# Native SP reference bootstrap, 2026-09-02

Evidence level: **compiles and enters native main-menu code**. Original/native/
browser visual parity, native gameplay and authored mission completion remain
unverified. The user stopped Computer Use with Escape during visual inspection;
no further native-window input was sent. The native application was left open.

## Changes

- Native renderer headers now consume the existing canonical world types instead
  of redeclaring them. Existing 32-bit size/offset assertions remain, including
  `sizeof(GfxWorld) == 0x2DC`. Windows uses the shared critical-section declarations.
- `common_runtime_state.cpp` owns the previously duplicated runtime functions and
  weapon state on both targets. Native SP/MP conditional behavior is retained.
- Image load definitions use a fixed 32-bit format value; conversion to D3D's
  enum occurs at the native renderer boundary. Missing direct includes and the
  native OpenAL `LONG` declaration are corrected. XAnim reference counters use
  C++20 atomic operations with the same sequential ordering as Win32 Interlocked.
- Native fastfile and mod paths honor `fs_basepath`, allowing the executable to
  remain in the build directory while loading a separate owned installation.
- Native output location and OpenAL selection are configurable. Runtime DLL
  copying can be disabled without changing the historical default.

No game asset or runtime DLL was fetched or copied into the build or site.
The existing tracked Steam SDK DLL was resolved in place through process PATH;
the owned installation supplied Bink in place. The Miles build links, but its
imports require five functions absent from this Steam installation's `mss32.dll`:
`AIL_init_sample@8`, `AIL_sample_channel_levels@20`, `AIL_sample_channel_count@8`,
`AIL_set_sample_channel_levels@20`, `AIL_sample_stage_property@28`.
The runnable reference therefore uses the existing source-built OpenAL backend.
Its pre-existing cinematic audio limitation prevents treating it as a complete
retail audio reference.

## Reproduction

Run `tools/build_native_sp.ps1`. It pins Visual Studio 18 2026 / Win32,
MSVC tools 14.51.36231 (compiler 19.51.36256.0), and Windows SDK 10.0.28000.0 in
`tools/native_toolchain.json`. This host used VS bundled CMake 4.3.1-msvc1.
The web toolchain's CMake 4.2.0-rc3 did not recognize this newer MSVC compiler.

The script downloads only the public
[Microsoft.DXSDK.D3DX 9.29.952.8 SDK package](https://www.nuget.org/packages/Microsoft.DXSDK.D3DX/9.29.952.8),
verifies SHA-256 `ead0906ae8a26c18a7525da7490127a2110f7c58f18293738283e30e97c6ea4b`,
and keeps it with its license under ignored `.tools/`. See Microsoft's
[D3DX documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dx).
Existing source dependency pins resolved to OpenAL Soft 1.25.2
(`b2c48f7718ef3fcf67921a8b6534c4914e328970`) and Tracy v0.12.2
(`c556831ddc6fe26d2fce01c14c97205a9dad46d5`).

Launch `build/native-sp/bin/Release/KisakCOD-sp.exe` with:

```text
+set fs_basepath "C:\Program Files (x86)\Steam\steamapps\common\Call of Duty 4"
+set fs_homepath "C:\Users\Jason\Documents\cod4browser\build\native-sp-profile"
+set r_fullscreen 0 +set r_mode 1280x720 +set logfile 2 +set com_introPlayed 1
```

These are one command line. Working directory must be the owned installation:
early native localization reads its `localization.txt` before engine filesystem
initialization. Process PATH includes that installation, the SDK's
`build/native/release/bin/x86`, and the repository's existing `deps/steamsdk`.
Native Steam initialization creates its ordinary four-byte `steam_appid.txt`
marker in the working directory. Config writes and console logs used the
separate `fs_homepath`; no original config was edited by this task.

Keep fixed settings when native startup offers automatic reconfiguration.
The final native log loaded `code_post_gfx`, `ui` and `common`, created a
1280x720 Direct3D9 window with 4x AA, and executed main-menu UI. It still logs
missing dvars and an unknown `stopRefresh` UI script. Native gamma was 1.00265,
sensitivity 2.07407, audio 44 kHz / volume 0.042328. These are configured values,
not proof of displayed/audio equivalence. Hardware and owned data hashes remain
in [the Steam inventory](steam-reference-2026-09-02.json).

Native executable SHA-256:
`4cbddb286700138d29864d9e62c1b9f17101dd30690f509d7b5172c5cf15ef3f`.

## Verification and remaining failures

- Clean pinned native OpenAL build and incremental rebuild: passed. Native Miles
  build also linked; its launch was rejected by the loader as described above.
- Release production and diagnostics Wasm builds / runtime-prefix checks: passed.
- Host Clang and Wasm/Node: canonical asset ABI, world scene and lighting tests,
  three passed on each target.
- Isolated diagnostic Chromium 149.0.7827.55 on port 8140: 12 smoke and 40 remainder
  passed, five optional retail cases skipped. Inherited retail variables were
  explicitly cleared.
- Production Chromium on port 8141: boot and canonical mount-error cases, two
  passed. API/export and size checks passed (3,311,005-byte Wasm).
- Separate owned Chrome 152.0.7977.65 main-menu/persistence test, port 8142:
  first attempt failed the diagnostic host's 15-second mount timeout during
  concurrent native compilation. The quiet rebuild-free repeat mounted, reached
  menu assertions, changed settings and rendered Killhouse, then failed at
  `retail_ui_persistence.spec.mjs:183`: pointer lock remained unset after the
  canvas click. Later save/Continue assertions in that test did not run.
  A subsequent focused reproduction captured `WrongDocumentError` from both
  raw and fallback pointer-lock requests even with DOM focus. Bringing the
  persistent tab to the front before the player's click fixed the test;
  engine input and assertions are unchanged. The final probe-free Chrome
  152.0.7977.65 rerun on port 8143 passed in 1.8 minutes, including pause,
  resume, profile persistence and Airplane save/load/Continue after page reload.
  This resolves the pointer-lock test failure. It does not establish authored
  mission completion or Continue after a new browser process. Private final
  log: `build/goal-pointer-final.log`.
- `git diff --check`: passed; existing LF/CRLF conversion warnings remain.

Production Wasm SHA-256:
`a959537cd20e8c77c7e75c98e085e35fa56b11bd4102d8ea28f9df4b1c580353`.
Diagnostics Wasm SHA-256:
`b1c8224f663a0179ba0324b35c9be5c40da78adda21852fe25300191b9d7f48b`.

Private logs are under `build/goal-native-*.log`; native startup is recorded in
`build/native-sp-profile/main/console.log`. Failed browser traces are under
`build/native-shared-retail-results/` and `build/native-shared-retail-recheck/`.
No screenshots, traces or retail bytes are committed.
