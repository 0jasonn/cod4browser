# Browser save thumbnails — 2026-09-02

Canonical PC save commits now request a paired 512x512 quality-90 JPEG after
writing the save. The browser reads its last completed scene composite,
uses the shared native RGB resampler, flips WebGL's bottom-up rows, and passes
only pixels to the browser's JPEG codec. The original save serialization and
profile paths remain in Kisak C++.

`web_savegame_image.cpp` owns the platform codec boundary. Encoding and decoding
run asynchronously in the existing Worker using OffscreenCanvas and
createImageBitmap; there is no new dependency, thread or production export.
At most four codec jobs are admitted. JPEG input is capped at 2 MiB and its
8-bit, 512x512 frame header is checked before decoding. Decoder failures retain
the canonical `unknownsave` fallback.

An encoding completion rereads and compares the complete saved header before
publishing, so deleted or replaced saves cannot receive a stale image. An
overwrite first removes its old JPEG. The shared UI updates only matching
entries in the current profile and invalidates pending/ready material caches.
Filesystem checkpoint and shutdown wait for admitted codec work before draining
writes; shutdown prevents new codec jobs. Short writes in `FS_WriteFileToDir`
now remove their partial file from the requested directory.

The selected JPEG publishes into one canonical `Material`/`GfxImage` pair.
Its backend texture reuses the existing bounded UI-image retention and context
recovery path. Selection generations reject obsolete decode completions; a
temporarily full queue retries on the next UI frame. The singleton is the
platform implementation of `Material_RegisterRawImage`, not an engine asset
model. Native Kisak already reserves `rgp.rawImage`; converging native raw-image
loading with this API remains work.

The first private menu capture exposed two shared UI omissions: the Date
column requested the map-name field, and `Item_Text_Paint` ignored the
`textsavegame` flag, printing its parser marker. The feeder now supplies the
date; the text path requests the selected canonical description through
`UI_GetSavegameInfo`. Centered dynamic descriptions also recompute text extents
on selection changes. The feeder/info functions now live alongside save
metadata in the shared source so native and Wasm regression tests execute them.

## Verification

- Native Win32 Release and Wasm/Node `r_image_resample_tests` and
  `ui_savegames_tests`: two tests pass in each target. These exercise 12,474
  resampling cases, JPEG-header truncation/size/dimension/component rejection,
  bounded save metadata, the actual date feeder/selected description, missing
  and ready images, profile ownership, overwrite/delete cache updates and
  late image discovery after opening the menu.
- Static checks and all 83 protocol tests pass. The filesystem test holds
  both checkpoint and shutdown behind a pending codec completion, then verifies
  the completed image write is present after remount.
- Native SP OpenAL Release and production/diagnostic Release builds pass,
  including both canonical runtime-prefix checks. Native is build evidence;
  no native save screenshot was visually qualified.
- Chromium 149.0.7827.55: 43 production tests pass (port 8018, 10.3 seconds),
  12 diagnostic smoke tests pass (port 8155, 7.0 seconds), and 44 diagnostic
  remainder tests pass with five optional retail skips (port 8156, 16.1 seconds).
  The new JPEG test verifies red/green orientation through encode/decode,
  canonical image upload, actual draw pixels and context loss/recovery.
  Suites ran serially; inherited retail variables were cleared. Exhaustive
  native-covered browser duplicates were not needed.
- Owned Chrome 152.0.7977.65, diagnostic Release, port 8154: the existing
  main-menu/input/profile/save/Continue/Quit/restart test passes in 2.2 minutes.
  Added checks require a valid JPEG larger than 1,000 bytes, raw-image
  publication before and after page reload, an actual selected UI image, and
  correct date/description feeder output. The private screenshot was inspected:
  the Airplane scene is upright, the Date column reads `2026-09-02`, and the
  literal `savegameinfo` marker is absent. Console menu opening is test setup;
  this is not a comparison of the complete shipped menu transition with Steam.
- The first owned attempt exposed an incorrect use of `FS_FileExists`, which
  only checks the game directory. Saves are under `players`; the shared UI now
  uses `FS_FOpenFileRead` search paths. Its native/Wasm fixture explicitly makes
  `FS_FileExists` fail for player files. A separate discarded run fetched an
  empty Wasm while this task rebuilt during reload. The final passing run used
  a completed, unchanged artifact. No existing assertion was weakened.

Production Wasm SHA-256:
`c38b6851248002a89e6bb90fe94b09b07f42c7ebffd7586544dbfc3fe7d7ff18`.
Diagnostic Wasm SHA-256:
`f8729c9fbf1ee2f19fc66ad8cff93f4333004ceb9839ba314c9fc9708b9fc433`.
Unchanged product limits pass: 3,327,213 B Wasm, 356,469 B JavaScript,
3,694,734 B site, 24 raw / 9 application exports and 19 files.
Logs and private retail images remain in ignored `build/goal-thumbnail-*`
and `build/thumbnail-retail-results`.

## Limits and next acceptance

The subsequent [start-level/native milestone](save-startup-native-2026-09-02.md)
implements deferred initial capture and native raw-image loading. The following
paragraphs record the boundary at this earlier milestone.

A save before the first presented game frame currently keeps the missing-image
fallback; the owned Airplane startup autosave exposed this case. Queue capture
at the next valid frame with the same canonical save/world identity before
claiming all authored checkpoints have screenshots. A saturated encode queue
also leaves a usable save without an image.

Native SP compiles the save hook and its existing GDI+ JPEG writer, but native
raw-image loading is still a pre-existing stub and native capture has not been
visually qualified. Browser capture uses the scene composite before the final
display-gamma pass. Compare screenshot timing, orientation, gamma, framing and
shipped menu appearance against the pinned Steam game; the native convention
and synthetic pixel checks alone cannot establish presentation parity.

Date/description localization, fresh-browser-process Continue, authored
checkpoints and campaign completion remain unverified. No campaign
classification changes. All committed fixtures are code-generated synthetic
data licensed with this repository; retail images remain in ignored output.
