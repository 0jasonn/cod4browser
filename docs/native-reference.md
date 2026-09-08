# Native single-player reference

The pinned Windows reference compiles and enters native main-menu code with
owned data. Native gameplay, complete menu/visual/audio comparison and authored
mission completion remain unverified. Browser acceptance is recorded in
[campaign compatibility](campaign-compatibility.md).

## Build

```powershell
.\tools\build_native_sp.ps1
```

Run from the repository root with Visual Studio 18 2026 Community installed at
`C:\Program Files\Microsoft Visual Studio\18\Community`, or pass
`-VisualStudioDirectory`. Install the versions in
[`native_toolchain.json`](../tools/native_toolchain.json): Win32,
MSVC tools 14.51.36231 and Windows SDK 10.0.28000.0. The verified compiler was
19.51.36256.0 with VS-bundled CMake 4.3.1-msvc1; the older web CMake did not
recognize that compiler. The script builds Release `KisakCOD-sp`, enables the
existing OpenAL backend and disables runtime DLL copying.

The only SDK download is public Microsoft.DXSDK.D3DX 9.29.952.8, verified
against its pinned SHA-256 and stored under ignored `.tools/` with its license.
OpenAL Soft and Tracy use the existing source dependency pins. Build products
stay under ignored `build/native-sp`; `-BuildDirectory` can select another
output directory. Never copy or download proprietary game/runtime files into
the repository or browser site.

## Launch with owned files

From a fresh PowerShell at the repository root, substitute your owned path:

```powershell
$repo = (Get-Location).Path
$retail = 'D:\Games\Call of Duty 4'
$env:PATH = "$retail;$repo\.tools\microsoft.dxsdk.d3dx.9.29.952.8\build\native\release\bin\x86;$repo\deps\steamsdk;$env:PATH"
Push-Location -LiteralPath $retail
& "$repo\build\native-sp\bin\Release\KisakCOD-sp.exe" `
    +set fs_basepath $retail `
    +set fs_homepath "$repo\build\native-sp-profile" `
    +set r_fullscreen 0 +set r_mode 1280x720 +set logfile 2 +set com_introPlayed 1
Pop-Location
```

The working directory must be the owned installation: early localization reads
`localization.txt` before engine filesystem startup. `fs_basepath` directs
fastfile/mod reads there; separate `fs_homepath` keeps profiles/config/logs in
ignored output (`build/native-sp-profile/main/console.log`). The process uses
owned Bink, public D3DX and the existing `deps/steamsdk` runtime in place via
PATH. Native Steam initialization creates its ordinary `steam_appid.txt`
marker in the working directory. Keep fixed settings if startup offers
automatic reconfiguration.

## Limits and focused checks

The verified startup loaded `code_post_gfx`, `ui` and `common`, created a
Direct3D9 window and executed the main menu. Remaining missing-dvar and
`stopRefresh` messages were observed. Steam app 7940/build 2737681 supplied the
owned reference inventory; recorded config values and hashes establish neither
difficulty nor visible fidelity.

The Miles target links but this installation's `mss32.dll` lacks five imported
functions, so the runnable reference uses OpenAL. Its pre-existing cinematic
audio limitation prevents treating native OpenAL as a complete retail audio
reference. Browser cinematic audio has a separate implementation.

For the optional synthetic native JPEG/device check, set `KISAK_D3DX_TEST_ROOT`
in the portable-test CMake configuration to the absolute path of
`.tools/microsoft.dxsdk.d3dx.9.29.952.8/build/native`, build
`r_savegame_image_win32_tests` in Release and run its CTest entry. It uses a
synthetic D3D9 device/texture and copies only public D3DX into ignored output.
Default portable tests need no DirectX SDK. Passing that boundary does not
qualify full native save-menu appearance or screenshot timing.
