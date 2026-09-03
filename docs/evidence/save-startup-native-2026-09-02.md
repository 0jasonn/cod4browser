# Start-level capture and native save-image loading — 2026-09-02

Browser save requests now wait for a completed frame of their canonical map
when the save is committed before rendering starts. `Com_GetBspFilename`
derives the expected world name; the existing backend checks that name against
the presented scene before reading pixels. The frame pump retries pending
requests after drawing. Map unload and renderer shutdown cancel pending
captures, so a later map cannot supply their pixels. Device loss leaves them
waiting for recovery. The existing complete-header comparison still rejects
deleted/replaced saves before writing the encoded JPEG.

The queue holds at most four capture requests, coalescing repeated filenames;
four active codec jobs remain the independent codec bound. If every capture
slot is occupied, a new request replaces slot zero. Save serialization and
gameplay state remain canonical and are unaffected by an absent screenshot.
An immediate quit before any matching frame, or a world unload before capture,
can still leave a valid save with no image. Codec work that has started is
drained by the existing durable shutdown.

Native `Material_RegisterRawImage` now reads the bounded JPEG through Kisak's
filesystem and decodes it using the already pinned D3DX renderer dependency.
The [Microsoft API](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxcreatetexturefromfileinmemoryex)
accepts in-memory JPEGs; the wrapper requests one managed mip level and verifies
the returned 512x512 A8R8G8B8 texture rather than assuming the requested format.
The shared JPEG preflight runs before decoding. A single material uses the
existing `rgp.rawImage`, and texture memory goes through `Image_TrackTexture`
and `Image_Release`. Replacement and device shutdown/reset synchronize the
render thread, unbind old textures and invalidate the shared save-menu cache.
The native capture hook now also synchronizes before front-buffer readback.
No native library or game data is added to the browser artifact.

## Verification

- Native SP OpenAL Release builds with the pinned MSVC/D3DX toolchain.
- Native Win32 Release `r_savegame_image_win32_tests`, `r_image_resample_tests`
  and `ui_savegames_tests`: three pass. The new test creates an invisible
  64x64 D3D9 window/device and a synthetic red/green texture. It encodes a JPEG
  in memory, decodes through the production wrapper, checks channel order,
  orientation and opacity, releases the texture and resets the device eight
  times. Null-device and malformed/truncated inputs are rejected. No game
  process or retail images are used by this test.
- Production and diagnostic Release builds and runtime-prefix checks pass.
  Static checks pass. Chromium 149.0.7827.55 passes 43 production tests
  (port 8018, 12.0 seconds), 12 smoke tests (port 8158, 7.8 seconds) and
  44 remainder tests (port 8159, 17.4 seconds), with five optional retail skips.
  The JPEG browser test also checks that the actual world-unload boundary
  cancels a capture waiting for an unrendered world. Synthetic browser suites
  run serially with inherited retail variables cleared.
- Owned Chrome 152.0.7977.65, diagnostic Release on port 8157: the final
  menu/input/profile/save/Continue/Quit/restart test passes in 2.2 minutes.
  It verifies the automatic Airplane start-level
  save has a valid JPEG before the diagnostic devsave is issued. The existing
  save/profile/reload/Continue/Quit sequence remains required. A private
  `start-level-thumbnail.jpg` read back from durable OPFS was inspected: it is
  upright and shows the aircraft interior, characters and HUD rather than a
  loading/menu/empty frame. This observes the native start-level-save boundary,
  not a later authored objective checkpoint or mission completion.

The optional native JPEG test uses the public SDK already installed by
`tools/build_native_sp.ps1`. Configure the existing native test build from the
repository root with `-DKISAK_D3DX_TEST_ROOT=<absolute path to the pinned
.tools/microsoft.dxsdk.d3dx.9.29.952.8/build/native directory>`, build
`r_savegame_image_win32_tests` in Release, then run its CTest entry. The default
portable test configuration remains independent of the DirectX SDK. The test
copies only Microsoft's public D3DX runtime into its ignored output directory.
All committed fixtures are synthetic code licensed with this repository.

Production Wasm SHA-256:
`34041b1c90ced7a2b1bfa7a4f9966fa412f02344a8a326bd344f4e460ad1fca1`.
Diagnostic Wasm SHA-256:
`fecffd1eb8c977bf590fc76ac389c18dcb51475d28e08359929fb5713aa82df9`.
Unchanged budgets pass: 3,327,973 B Wasm, 356,469 B JavaScript, 3,695,494 B site,
24 raw / 9 application exports and 19 files. Logs/private output are under
ignored `build/goal-save-startup-*` and `build/save-startup-retail-results`.

## Remaining fidelity evidence

The native decoder/device test establishes this platform boundary, not the
appearance of the full native game menu. Native front-buffer screenshot timing,
browser pre-display-gamma capture, authored checkpoint framing/intro fades,
locale formatting and complete menu transitions still need original Steam ->
native Kisak -> browser visual comparison. This work adds no authored mission
completion or new campaign classification. Cinematics, room reverb, remaining
material differences, language profiles and active-campaign qualification remain
on the full fidelity roadmap.
