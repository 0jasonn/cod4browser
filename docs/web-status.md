# Web product status

Updated 2026-09-04. This is the single current status page; the
[roadmap](web-roadmap.md) owns priorities and the
[convergence inventory](web-port-convergence.md) owns system classification.

## Verification and reference baseline

Browser text now reaches canonical `CL_CharEvent` alongside physical keys.
The shipped profile field and console accept ordinary/shifted text, repeats,
Backspace, Home/End, Delete and Insert. Native editing and gameplay repeat
policy remain shared. Owned English input passes in Chromium 149.0.7827.55.
Windows-1252 is supported. Trusted paste events now supply the first bounded
line to canonical `Sys_GetClipboardData` and `Field_Paste`; an owned console
command passes through the shipped Worker and executes. Menu edit fields keep
their native handling of control characters. A trusted canvas press now
focuses an editable text sink while preserving canvas pointer lock. Served
Chromium commits composed `é€😀` once as native bytes 233/128 and rejects the
unsupported code point. Actual Windows IME candidate UI, other code pages,
arbitrary clipboard reads and localized glyph qualification remain open.

Graphics controls now share native sampler and image-quality policy with D3D.
Anisotropy limits, mip-filter overrides and filtering disable affect WebGL
parameters across 2D images, sky/reflection cubemaps, model-lighting volumes
and animated water; normal/specular/detail gate the existing shader features. Native
semantic/no-picmip policy selects authored color/normal/specular mip levels
before decode/upload. Native/Wasm tests cover memory thresholds, no-picmip,
selected DXT/wavelet levels, malformed full input and non-power-of-two chains.
The 19,200-case sampler trace also matches unmodified native `R_SetTexFilter`
from `8be61213`. Synthetic filtering/normal/detail pixels, selected mip colors,
upload sizes and texture recreation pass. Sampler state and normal pixels also
survive actual context loss/recovery.

Technique-set selection now follows the native 20-token feature-name policy.
The frontend reevaluates canonical sets after atomic zone publication and when
normal, specular, detail, z-feather, outdoor, tweak or shadow selection changes.
Available variants keep their DB identity, missing variants fall back to the
source set, and leading-comma aliases inherit the selected canonical target.
Native and Wasm tests cover name-token boundaries and the complete pointer
update. A bounded owned Killhouse render selects 201 variants among 165
shader-model-3 sets; `r_normal 0` updates 221 sets on the next frame and
restoring the setting triggers another update. This check loads and renders the
map without player input or objective progression. Unknown shader families and
general multipass equivalence remain unverified.

Owned Killhouse production loading, renderer restart and fresh-runtime
persistence pass without player routes or objective changes. The production
test measures the full-quality scene, returns through canonical disconnect,
then clicks the shipped Graphics → Texture Settings controls to select Manual
and Normal color/normal/specular resolution. The menu's native
`r_applyPicmip` action now maps to the existing `vid_restart` path. At
static-model submission, `decodedTextureSourceBytes` falls from 1,153,857,573 B
to 168,912,933 B after Apply and remains 168,912,933 B after Quit/Start game.
The GPU texture estimate falls from 1,174,304,805 B to 189,360,165 B. These are
renderer estimates in Chrome 152.0.7977.65 on the named NVIDIA host, not measured
hardware VRAM or active-gameplay performance. Encoded recovery sources remain
complete. Captures show the opening scene, not matched Steam/native fidelity.
Native mip bias now reaches shader sampling, including explicit reflection LOD,
with positive/negative and fractional-mip pixels and context recovery verified.
Its cheat/non-archived policy is unchanged; implicit bias follows the GL
implementation's LOD limit. Broader authored material variants remain open.

An owned paused Killhouse view also verifies real material controls: disabling
specular changes 2,685 pixels and disabling normal maps changes 2,875 pixels
(RGB channel change greater than one; repeated baseline noise is zero).
Restoring either control restores the baseline. With both disabled, real
context loss/restoration preserves the captured PNG byte-for-byte; re-enabling both
restores the original PNG. This is a narrow ground-facing view with the native
pause overlay, not a matched reference scene or gameplay acceptance.

The frame pump now uses shared native `Com_ErrorCleanup` after owned startup,
including error localization, temporary-memory/parser/command cleanup and
native recoverable/fatal policy. Owned menu error recovery, Killhouse loading,
in-map error recovery and another Killhouse load pass in Chromium. This found
and fixed an upstream `Dvar_InfoString` callback reading a four-byte mask from
a one-byte argument: stale stack flags overflowed server info and lost the map
name on reload. Native/Wasm flag-selection checks pass with Release assertions
enabled. Pre-mount errors remain terminal. Malformed-load rollback, nested
errors and broader transition recovery remain open.

The active XFile fixture now runs 328 deterministic mutation cases with native
engine assertions enabled in Release. It recompresses synthetic RawFile,
PhysPreset and Material/image/water seeds, varies all nine allocation blocks
in both PMem directions, and checks alias identity, image-before-material
publication, incomplete-parent rejection and pool retirement while preserving
an unrelated zone. Win32 and Wasm match: 173 accepted, 155 rejected, nine
partially published failures, normalized trace `99b9d10c`. This found an absent
scratch block reaching `DB_AllocStreamPos`'s non-null assertion; the web stream
boundary now reports malformed input before that assertion. Native behavior
and assertions are preserved. The fixture explicitly unloads the test zone;
its broader adapted asset-family paths do not establish agreement with the
original native `db_load.cpp` path. A focused 32-bit MSVC oracle now compiles
the real `db_load.cpp` RawFile routine behind a test-only source guard. It and
the adapted generated routine pass the same direct fixture and assertions for
inline `-1`, insert-pointer `-2`, a block-4 alias, final-pointer replacement
at publication, serialized read consumption and logical stream coordinates.
The byte-reader and publication callbacks are fixture-owned; full native
inflate/registry comparison and more asset families remain open. The adapted
oracle passes on Win32 and Wasm; production compiles the unsliced sources and
its Wasm hash is unchanged by this milestone.

A companion test now uses the real browser zone coordinator, PMem allocator
and single-Worker DB scheduler. Across 80 partially published RawFile failures
in both allocation directions, automatic cleanup restores existing overrides,
returns new entries and typed-pool slots, reclaims PMem and preserves a separate
zone with identical flags. Valid retries publish successfully; reused zone
indices also unload correctly. This reproduced and repaired two defects:
failed zones were left published, and numeric zone-order freeing violated
native PMem's reverse allocation order. Generated-load failure and stream
bounds now belong to the XFile/stream owners, removing the diagnostic flag
that kept a subsequent valid load failed. Forty-three additional failures
publish replacements for the compiled `ClipMap`, `ComWorld`, `GameWorldSp` and
`GfxWorld` singleton pools before a malformed trailing RawFile rejects the
candidate. The coordinator restores each surviving singleton's body, name,
hash, zone identity and in-use state, retires the candidate and reclaims its
PMem allocation; valid replacements then commit in native and Wasm. The
`GfxWorld` and `ComWorld` unload hooks remain untouched on rollback and execute
once against the old owner on commit. A bounded two-fastfile request also rolls back atomically: file one
publishes a replacement ClipMap, file two exhausts the native two-entry
`MapEnts` pool, and both request zones retire together while the pre-request
singleton returns. A companion native/Wasm check uses the real
`scr_stringlist`: shared zone owners and repeated default owners survive
partial release, the final owner retires the string, and a live zone-0 default
suppresses the coarse whole-user shutdown. Broader request graphs, non-world
device hooks and broader native-game loader oracle coverage remain open.

Image rollback now also restores retained source bytes. The old name-keyed
cache let a rejected Material's published image replace a surviving image's
payload even after its canonical body was restored. `GfxTexture` now carries
an opaque platform resource handle, so native copies/overrides/defaults retain
the right bytes. DB completion/unload collects only unreferenced resources.
The unchanged 256 MiB source budget rejects a new load before copying when it
would exceed the limit; it cannot evict surviving resources during a failed
load. Forty repeated image failures, retry, stable default copies after zone
release, and budget rejection before reading payload pass in native/Wasm.
This also found and fixed a material mark-walk crash: semantic 11 contains a
`water_t`, whose `image` must be marked, matching native Kisak. The test now
uses stable interned string identities, while the companion real-string check
and this default-copy unload case cover reference lifetime. Device side effects
remain outside its coverage.

Placement-only FX/DynEntity models no longer discard deformed surfaces. Native
Kisak draws these from authored `verts0` with object placement; the existing
browser transform now admits them too. Native/Wasm checks cover mixed surfaces,
transformed vertices, identity and malformed-surface rollback. Matching authored
FX visuals remain unverified; animated DObj skinning is unchanged.

Audio source offsets and processed buffers now follow actual Web Audio device
time. The Worker proxy waits for validated feedback rather than advancing its
own wall clock. Source generations and absolute queue ordinals reject stale
feedback after pause/resume, seek, reuse or unqueue; one snapshot in flight
bounds the queue during synchronous Worker stalls. Native/Wasm and Node checks
cover delayed delivery, pitch changes, queue retirement and device failure.
Served Chromium holds static and three-buffer playback beyond their durations,
then suspends the actual AudioContext: logical playback waits, remains frozen
while suspended and completes after resume. Movie time now follows cumulative
played PCM; one pending video frame feeds its audio ahead of presentation.
Owned Killhouse playback waits through held audio delivery and AudioContext
suspension, resumes, and recovers its frame after simulated WebGL resource loss. Host-stall
coverage also passes: 900 ms blocked plus 150 ms recovery advances video 367 ms
through the queued audio in that run (433 ms with the subsequent planar
renderer). Hardware output latency, arbitrary audio tails and
native/Steam audiovisual comparison
remain open; no-audio/device-failure playback retains a wall-time fallback.
The single-threaded Worker now starts a canonically selected loading movie
before `SV_SpawnServer`, then uses the existing `nextmap` handoff to run the
same requested map after the movie ends. This replaces the former post-load
playback, which exposed active gameplay, mission audio and HUD underneath the
movie. The forced `ui_autoContinue` override from the pre-cinematic bootstrap
has also been removed, restoring the native pregame hold. Owned production
`map killhouse` starts `killhouse_load` in 299 ms, presents changing movie-only
frames for 37,624 ms, then loads the map and starts the authored fade 2,722 ms
after the movie ends. The diagnostic run starts in 375 ms and records no
`SV_SpawnServer` or game-driven frame before movie completion. Standalone
production completion remains 37,518 ms. An initial diagnostic
attempt let the canvas unlock click reach canonical input after the movie
command, stopping the skippable movie. The fixture now waits for that input
receipt before starting playback; all timing and skip assertions remain intact.

Current working-tree gates (base `8be61213`): native SP, Release production and
diagnostic builds/runtime-prefix checks, static checks, 41 native CTest,
40 Wasm CTest, 93 Node, 10 smoke, 59 synthetic remainder (nine retail skips),
44 production browser and the focused owned text/quality/recovery/cinematic/graphics tests pass. One Node
run concurrent with builds failed two short wall-clock watchdog cases; the
unchanged suite passed on recheck. No assertion or timeout was relaxed.
The XFile milestone similarly hit the short maximum-dirty-age Node checkpoint
test during compilation; all 93 tests passed unchanged after the builds ended.
The final owned graphics run also hit the diagnostic mount's 15-second Worker
watchdog while the routine tiers ran concurrently. Its trace reaches prerequisite
zone completion before termination; the unchanged test passes alone in 52.5 s.
Mount latency under contention remains a recovery/performance finding, and the
watchdog is unchanged.

The RawFile oracle continuation rebuilt native SP and both Release browser
targets with their runtime-prefix checks. All 41 configured Win32 tests
(including the optional D3DX save-image check), 40 Wasm tests, 44 production
browser cases, 10 smoke cases and 59 remainder cases pass; nine owned-data
cases skip in the synthetic remainder. The final strengthened oracle checks
also pass on both Win32 loaders and Wasm. No owned-data or gameplay check was
run for this test-only milestone. The production file/export boundaries pass
before the unchanged 3,332,379-byte budget rejects the byte-identical
3,729,180-byte Wasm; the existing measured budget proposal remains unapproved.

After the DB retry/image-resource changes, the native SP and both browser builds,
38 native/39 Wasm tests, all 93 Node tests, static checks and the 10/55/44
browser tiers pass again. Isolated owned-data checks also pass: production
picmip restart/new-runtime persistence in 57.5 s, and menu/in-map error recovery
with subsequent Killhouse loads in 50.0 s. The owned paused graphics check also
passes again, preserving PNG bytes through context recovery and restoring the
original baseline when both controls are re-enabled. Their scope is loading,
rendering and recovery; manual campaign acceptance remains unverified. The size
gate below still fails.

Particle-cloud dimensions, view-X orientation and directed stretching now
follow the shared native axis calculation and inspected authored corner math.
The former browser implementation fails the new regression. The shared helper
matches 4,096 unmodified native inputs exactly; 12 native-verified matrix cases
exercise 147,456 corners across three views in both native and Wasm. Native SP,
production/diagnostic builds, 38 native/39 Wasm, 93 Node, static and 10/55/44
browser tiers pass after this correction. The owned AC130 check reaches a real
world frame but observes no `FxParticleCloud` submission in its 60-second
stationary window; its cloud assertion fails, so its later context-recovery
check does not run. This is not owned-cloud or thermal-fidelity acceptance.
Unimplemented shader families remain open. Outdoor masking and native-shaped
cloud-center distribution are implemented at the canonical world/renderer
boundary as described below. Private probe data and failure evidence stay under ignored
`build/parity-audit/`; no shader data or temporary probe command ships.

Recognized soft particles now read authored feather, fog, additive, angle-falloff
and eye-offset arguments. A separate native-style FloatZ prepass uses canonical
lit/decal geometry and depth-pass state, including alpha rejection and signed
viewmodel depth. Synthetic pixels verify all variants, near/intersection fades,
packed depth precision, resizing and the `r_zFeather` control. These checks also
found a real recovery defect: Emscripten's event lookup lacked the Worker canvas
mapping. The platform mapping is fixed, and diagnostic hooks now use
`WEBGL_lose_context`; all five graphics tests pass genuine loss/restoration.
Earlier checks that invoked the handlers directly proved resource reconstruction,
not browser context-event delivery. Matched Steam/native soft-particle fidelity
and arbitrary shader families remain unverified.

The owned paused graphics-control test also passes with the actual extension.
SHA-256 comparisons confirm identical PNGs before/after recovery with both
controls disabled, and identical baseline/restored PNGs after re-enabling them.
This replaces the earlier simulated recovery evidence for that narrow scene.

An isolated owned Killhouse load/render check passes FloatZ enable/disable and
real context restoration alongside 4x MSAA in Chromium 149 (44.7 s). The retained
canonical world frame reaches generation 11 and resource generation 2; the
inspected restored capture shows the opening movie and title overlay. This
proves target routing and recovery, not a visible authored particle comparison.
No movement, objective changes or mission-completion input was used.

After the soft-particle and event-mapping changes, native SP and both Release
browser builds/runtime-prefix checks pass. Fresh full portable builds pass
38 native and 39 Wasm tests; 93 Node tests and static checks pass. Chromium's
routine tiers pass 10 smoke, 56 synthetic remainder (nine optional retail skips)
and 44 product tests. The initial real-loss graphics run failed all four
event-recovery assertions before the platform fix; the unchanged five-test
graphics suite then passed. The exhaustive duplicate suite, remote CI and
original/native visual comparisons were not run for this milestone.

The inspected distortion pass now resolves the lit scene before emissive draws,
projects canonical packed normals/tangents with authored scale and vertex colour,
and rejects screen offsets crossing foreground depth. `r_distortion` disables
native resolved-post-sun groups; `r_zFeather` remains independent. Native/Wasm
binding checks and synthetic GPU pixels pass, including signed depth, basis
directions, projective W, colour/alpha, MSAA and real context restoration.
The first MSAA case produced a black snapshot after source reuse; explicitly
submitting the resolve with `glFlush` fixes it without a GPU completion wait.
The retained snapshot is also checked after the source is cleared and redrawn.
Unknown shader variants and matched original/native heat-haze fidelity remain
unverified. An initial Killhouse routing check rendered the canonical world but
did not activate the distortion snapshot during its 30-second window; that
assertion failed and the later recovery check did not run. CargoShip also
renders after its opening movie, with canonical rain, lightning, cigar smoke
and other FX logged, but its 30-second distortion-snapshot assertion likewise
fails. The inspected capture shows the opening helicopter interior; no recognized
distortion effect was observed. Both failed checks and traces are retained
privately. Owned distortion control/recovery and matched appearance remain
unverified; no gameplay route or effect/progression injection was used.

After this distortion change, native SP and production/diagnostic Release
builds with runtime-prefix checks pass. All 38 native, 39 Wasm, 93 Node,
static, 10 smoke, 57 synthetic remainder (nine retail skips) and 44 production
browser checks pass. The six graphics cases include snapshot resize/recreation
and MSAA source-reuse coverage. The production boundary still passes file,
export and diagnostic separation checks before failing unchanged byte budgets.
No remote CI, exhaustive duplicate suite or original/native visual comparison
was run. During manual testing, compare heat-haze direction and strength near
foreground geometry and the effect of `r_distortion` with Steam;
manual gameplay and mission completion remain outside this implementation run.

Authored outdoor particle-cloud materials now retain the canonical
`GfxWorld::outdoorImage` and lookup matrix through the world command. The
generated `$outdoor` image reuses the canonical recoverable image path, while
each expanded quad carries its original center in the particle-cloud normal
slot. Exact seven-argument shader recognition selects the outdoor pass; it uses
native linear/clamp sampler state and preserves RGB while masking alpha when the
lookup height exceeds the cloud center. Synthetic GPU cases distinguish two
lookup columns, verify lower/equal/higher heights and colour modulation, and pass
after an actual WebGL context loss. Command publication is atomic for invalid
matrices, and native/Wasm tests verify the retained center and authored binding
layout. The earlier stationary owned AC130 check saw no `FxParticleCloud`, so
matched authored appearance and an owned recovery scene remain unverified; no
campaign route or effect/progression injection was used.

Cloud center construction now shares native cell-position arithmetic. At
renderer registration the browser builds one retained 8x8x16 lattice, consumes
exactly three CRT samples per cell in native x/y/z order, and maps the wider CRT
result into the original inclusive 15-bit range before applying the native cell
formula. Seeded native/Wasm tests verify all center bounds, the 3,072-sample
consumption, repeatability after reseeding, and variation after regeneration.
Native continues to use its platform CRT, so this establishes matching lifecycle
and distribution semantics rather than matching cross-runtime sequences.

After the texture-menu and canonical scene-admission changes, native SP and
production/diagnostic Release builds with runtime-prefix checks pass. All 41
native, 40 Wasm, 101 Node, static, 10 smoke, 59 synthetic remainder (nine
optional retail skips), and 44 production browser checks pass. Focused owned
texture-menu and Gate 3 checks also pass. The production boundary passes its
file, export and diagnostic checks before the unchanged byte budget fails. No
remote CI, exhaustive duplicate suite, campaign playthrough or original/native
visual comparison was run.

Gate 3 now passes its complete startup, publication, geometry, frame, keyboard,
pointer-lock and lifecycle assertions. The earlier 8,064 / 431,747 / 793,188
expectation covered the legacy contiguous `surfaceCountNoDecal` prefix. Commit
`498289c7` replaced that prefix with the canonical lit/decal/emissive DPVS
ranges; their complete Killhouse coverage emits 8,475 surfaces / 445,369
vertices / 823,464 indices after the same single sky-surface exclusion. The
independently derived delta is 411 surfaces, 13,622 vertices and 30,276 indices,
and the existing native noncontiguous-range fixture verifies that canonical
ranges override the prefix. The Gate 3 fixture deliberately combines four
owned fastfiles with synthetic IWDs, so its exact 796-batch, 8/339-image
fallback inventory is a loader/lifecycle boundary rather than material-fidelity
evidence. Production owned-archive checks cover the latter boundary separately.

Source ZIPs now exclude legacy Bink/Miles/Steam SDK directories and native
binaries using `.gitattributes`, with a CI archive guard. The guard rejects
synthetic SDK entries, renamed executable content and retail archive suffixes.
Product packaging now rejects subdirectories as well as unexpected files.
Native reference CI disables DLL copying and no longer uploads the inherited
`bin` directory; its compile checks remain. The remote workflow was not run.
Local dependencies and inherited history remain intact; use the documented
source-release process, not a raw clone. The source ZIP used for local policy
validation was HEAD with working-tree attributes, not corresponding source for
these uncommitted binaries.

The product file/export/diagnostic checks pass, then unchanged size budgets
fail: Wasm 3,757,999 / 3,332,379 B; JavaScript 770,681 / 357,646 B; site
4,627,160 / 3,701,082 B. Character and clipboard setters are the input
allowlist additions; export cap and byte budgets remain unchanged. See the
measured +5% proposal in [architecture](web-architecture.md#build-products).
The concrete audit work for text input, renderer controls and encountered
passes, audio/movie clocks, recovery, active XFile validation, distribution
and measured resource behavior is implemented. Broader mission-specific
qualification remains on the roadmap. Manual gameplay, mission completion and
original/native visual/audio acceptance belong to the user and remain
explicitly unverified by this phase.

AC130 now renders its real scripted gunship scene after correcting empty-grid
lighting and the browser dynamic geometry capacity. The frame wrapper also
reports latched canonical errors instead of leaving a frozen game marked
running. CargoShip → AC130 passes a 60-second stationary diagnostic run;
production Chrome also renders the mission. Captures were visually inspected,
but Steam/native thermal fidelity and mission completion remain unverified.
See [AC130 evidence](evidence/ac130-rendering-2026-09-03.md).
AC130 milestone gates: five focused C++ tests on native and Wasm, static checks, 83 Node
tests, 12 smoke, 51 remainder (six optional retail skips), and 44 production
browser tests pass. The existing production size gate still fails unchanged.

Video Mode now selects an actual render resolution through canonical
`vid_restart`; Screen Refresh Rate reports `Browser controlled`. Resize updates
the native UI and cgame viewport together. The redundant database-thread spawn
and missing backend reinitialization are repaired. Production Chrome 152 passes
the shipped Apply confirmation, durable mode restoration and the native
save/restart/load sequence in Killhouse. See
[display evidence](evidence/display-options-2026-09-03.md).
Display milestone gates: static checks, 83 protocol tests, native SP build, 12 smoke,
50 remainder (six optional retail skips), 44 production browser tests and
separate owned Chrome 152 display/gamma checks pass.
The existing production size gate still fails; its budgets are unchanged.

The shipped Brightness slider affects menu, HUD and world pixels through the
existing final display pass. Native/Wasm ramp traces match. Steam display
equivalence remains unverified. See [gamma evidence](evidence/display-gamma-2026-09-03.md).

The cinematic platform implementation now decodes imported Bink movies with
source-built FFmpeg and presents them through canonical cinematic materials
and Y/Cr/Cb/A code images. World/brush, static-model, DObj and UI draws resolve
retained R8 planes at draw time, with native colour coefficients, linear chroma
filtering and alpha. Recovery no longer requires an active 2D scene. Synthetic
GPU colour/alpha/replacement/recovery and native/Wasm material selection pass;
authored in-world scene fidelity remains unverified. The owned Killhouse intro passes production Chrome playback and
natural completion, plus diagnostic pause/resume and native skipping. This establishes movie
playback, not authored campaign completion or Steam audiovisual parity. The
unchanged production size gate fails with the added decoder. See
[cinematic scope](cinematic-codec.md) and
[current execution evidence](evidence/cinematics-2026-09-02.md).

The verification repair passes static checks, 81 protocol tests, 12 diagnostic
smoke tests, 39 remainder tests (4 optional retail skips), and production
boot/canonical mount-error checks. The synthetic picker now reaches a completed
mount, and canonical errors cross the asynchronous mount boundary correctly.
See [execution and limits](evidence/verification-repair-2026-09-02.md).

Saved-screen flashbang/shellshock commands now capture and blend in the existing
WebGL2 UI pass, retaining canonical timing and intensities. Synthetic pixel
checks and an owned-retail Killhouse material/composite check pass; original
game visual/timing comparison remains unverified. See
[saved-screen evidence](evidence/saved-screen-2026-09-02.md).

FX point-lighting queries now use the canonical world's light grid and sun
color instead of constant white. The existing portable lighting helper passes
focused host-native and Wasm checks; the final browser gates remain green.
See [point-lighting evidence and limits](evidence/average-lighting-2026-09-02.md).

Transient FX omni/spot submissions now retain canonical `GfxLight` construction
and importance selection and draw the loaded additive material passes before
emissive geometry. Pixel checks cover attenuation, cone direction, normal maps,
vertex fog and destination-alpha coverage. The first visible shadow-capable
spot retains native identity and near-plane bias, competes with primary spots,
and samples its 512x512 map. Its BSP caster list uses native's exact shifted
spot planes without camera DPVS; static casters reuse the camera/light receiver
mask; and dynamic model/brush casters reuse their exact receiver predicate.
Static and dynamic child shadow dvars are honored independently, while sun
partitions and authored primary-spot membership retain their existing paths.
Each light's alpha clear and receiver draws use native's tangent-sphere screen
scissor. A separate owned Killhouse check measures shadow darkening against
`r_spotLightShadows 0`, clears back to baseline and survives context recovery.
DObj spot receivers now reuse native FX attachment identity and render flag 8
exclusions, preserving native's rigid single-model exception. Native/Wasm
submission tests and Chromium spot/omni pixels pass; 41 Win32, 40 Wasm,
44 production, 10 smoke and 59 remainder cases pass (nine retail skips).
Native SP and both browser builds pass. Authored light appearance was not
requalified in that change. BSP receiver ranges now also combine camera DPVS
with native spot-plane and omni-sphere bounds tests before batching. The math
matches 4,096 native spotlight plane sets and 98,304 bounds cases exactly;
the same 41/40/44/10/59 routine gates pass after integration. Full stencil
ordering and Steam visual
comparison remain open. See [light evidence](evidence/transient-lights-2026-09-02.md).

The BSP check also exposed an independent shader backend failure on installed
Chrome 152.0.7977.77: ANGLE translated overloaded 3D/cube/shadow helpers into
colliding HLSL signatures, then failed dynamic pixel-executable compilation
at the first draw. Unique helper names fix the reproduced transient-light
pixel failure without changing sampling or suppressing GL errors. Five of
eight focused Chrome cases pass; three unchanged graphics assertions still
report one-byte differences (127 versus 128). The default Chromium gates pass.
The owned Killhouse light/shadow/context-recovery check passes on Chrome/D3D11
in 37.5 seconds. Its fixture now proves clearing before recovery advances
animation, then uses a fresh unlit reference for the recovered pose; the
existing pixel thresholds are retained. This is a bounded render check,
not authored campaign acceptance.

Static-model transient receivers now use native bounds and camera visibility
through canonical instance IDs after LOD packing. The existing per-pass mask
selects contiguous runs from the retained instance buffer; no new geometry or
GPU instance buffer is added. Native SP and both browser builds, 41 Win32/40
Wasm tests and 44 production/10 smoke/59 remainder cases pass (nine retail
skips). The owned Chrome lighting/shadow/clearing/context-recovery check passes
in 45.8 seconds. Full authored-effect fidelity remains unverified.

Rigid DObj, FX-model and DynEntity-model transient receivers now use the
canonical model/pose sphere rather than visiting every camera-visible model.
Spotlight plane tests are shared with native; omni selection preserves the
radius-sum rule. Native/Wasm checks cover radius ownership and scene-kind
reset, and Chrome/D3D11 pixels cover rejection and tangent acceptance.
Native SP and both Release browser builds pass, as do 41 native/40 Wasm,
44 production/10 smoke/59 remainder tests (nine retail skips). The owned
Chrome light/shadow/clear/context-recovery check passes in 44.6 seconds.
Authored comparisons remain open; later work below supersedes its ordering limit.

Animated DObj receivers now share native's selected-bone bounds transform.
The frontend builds the current LOD bone mask before `CG_DObjCalcPose`, unions
canonical `XBoneInfo` boxes with `viewOffset`, and copies the complete entity
box to every DObj material pass. A 4,096-case exact scalar-order comparison
passes on native and Wasm, along with selected-mask, rotated-box, view-offset,
invalid-data and atomic-publication checks. Synthetic Chrome pixels exercise
actual spot/omni material rejection and contact. No skinned-vertex bounds or
pose cache replaces canonical ownership. Authored Steam/browser comparison remains open.

Transient-light receiver draws now form one native-style list per light rather
than fixed world/static/dynamic phases. Canonical material keys carry surface
types 0 for BSP, 2/5 for static models, 6 for brushes and 7/8/9 for model
families; code meshes, marks, particle clouds and sun billboards are excluded.
The complete list sorts with the exact `R_ReverseSortDrawSurfs` primary-sort
transformation before the existing destination-alpha pass. A 4,096-key oracle,
producer tests and the synthetic WebGL material/coverage suite pass on native,
Wasm and Chromium. Non-BSP keys now share native receiver construction rather
than reusing camera keys: instance primary-light/probe fields cannot reverse
the material order. A 1,024-input oracle for each of six surface types checks
field preservation and depth hack. Native SP and both browser Release builds,
native CTest 41/41, Wasm CTest 40/40, product 44/44, smoke 10/10 and remainder
59 with nine optional skips pass. The owned Chrome light/shadow/context-recovery
check passes in 56.4 seconds. Equal-key object-ID order and authored Steam/browser
appearance remain unverified; the approved production size gate still fails.

Camera DPVS now completes before dynamic scene assembly. A shared bounded BSP
walk requires DObj and DynEntity spheres plus scene and DynEntity brush boxes
to overlap the portal-visible cell mask; shared plane predicates then apply
native's non-positive tangent rejection. FX models follow native's direct
sphere/frustum rule, and static mark generation consumes the canonical camera
mask. Focused native/Wasm tests cover visible, invisible and split cells plus
sphere/box contacts. Animated DObjs now perform native's second camera-box test
on selected posed-bone bounds before skinning, including its non-positive
tangent rejection. The diagnostics build/runtime-prefix check, synthetic
transient material pixels and owned paused Killhouse lighting/shadow/recovery
check pass; the linked-portal run used Chrome 152/D3D11 and completed in
44.4 seconds, observing 44 linked DObjs (nine admitted) and 49 linked brushes
(seven admitted) on its first frame.
Linked DObj and scene-brush admission now uses native's exact per-cell clipped
planes. Cgame links maintain the original world-owned cell-bit banks; shared
`BoxOnPlaneSide` determines BSP branches. Single-word leaves, client offsets,
kind changes, unlinking and invalid-walk rollback pass focused native/Wasm
checks. DynEntity models/brushes now also use canonical cell links and full
per-cell plane admission through shared native/web helpers. Linked animated
DObjs now also test selected posed-bone boxes against every cell plane and
exact BSP cell membership before skinning. Native/browser `R_BoundsInCell`
queries share a bounded walk. The renderer now calls canonical pose-use and
visible-pose updates, retaining visible status if a later portal path rejects
an already admitted pose.
The isolated owned Chrome light/shadow/context-recovery check passes in 47.2
seconds and asserts execution of this post-pose path. An earlier concurrent
run hit the 15-second mount request deadline while the Worker still reported
`common.ff` loading progress. The filesystem adapter now reports successful
native mount reads to a shared progress watchdog. Diagnostics retains a
15-second stall deadline, production retains 30 seconds, and both enforce a
five-minute absolute cap. Duplicate, malformed and unrelated progress cannot
extend a request. The owned Chrome light/shadow/context-recovery check passes
in 55.4 seconds alongside the routine browser suites. Node checks pass 101/101,
static checks and both Release builds pass, production browser tests pass
44/44, smoke 10/10, and remainder 59 with nine optional retail skips. Native and
Wasm CTests were not rerun for this JavaScript-only fix; their preceding results
remain below. Manual animation/visibility remains unverified.
DynEntity scalar culling matches 1,024 synthetic cases for each native sphere
and box loop. The owned Chrome light/shadow/recovery check passes in 46.1
seconds and requires positive DynEntity rendering using a diagnostics-only
renderer view of an existing linked model. This fixture correction removes
dependence on the paused authored camera without moving the player or advancing
scripts. The fixture has no DynEntity brushes; those have native/Wasm evidence
only. The native SP reference builds. Native CTest passes 41/41,
Wasm CTest 40/40, production browser tests 44/44, smoke 10/10, and the routine
retail-free remainder tier 59 passed with nine optional retail skips. Manual
gameplay acceptance was not run.

The frame-order correction now builds DynEntity model/brush commands before
physics updates, as native `R_RenderScene` does, while marks still expand
afterward. DObj lighting runs only after pose visibility rejection. A synthetic
native/Wasm regression pairs a culled animated DObj with a visible lit DObj,
checks that the culled handle stays untouched, and preserves the visible atlas
even when the rejected submission has an invalid lighting origin. The test
failed on the former ordering and passes after the change. Moving-entity and
mark alignment during manual gameplay remain unverified. The corrected build
passes native 41/41, Wasm 40/40, production 44/44, smoke 10/10, and remainder
59 passed/nine optional skips. The owned Chrome lighting/recovery check passes
in 49.6 seconds. An earlier remainder invocation inherited retail-data settings
and was stopped after retail setup failed; the reported remainder result is
the corrected retail-free invocation.

`Material_Duplicate` now registers canonical dynamic menu aliases instead of
returning the source handle for an unknown alias. The first registration copies
the native material body plus state, texture and constant tables; the alias is
owned by the active DB zone and releases its auxiliary storage at unload.
Native and Wasm recovery tests verify distinct identity, independence from
source-table mutation and removal with the source zone. Shipped map-info visual
comparison remains unverified.

The local Steam build is [pinned by hashes and configured settings](evidence/steam-reference-2026-09-02.json).
The native SP reference now builds with pinned MSVC and the existing OpenAL
backend, loads the owned startup fastfiles and enters main-menu code. Native
visual/gameplay verification remains open. The separate browser retail rerun
now passes pointer lock, pause/resume, profiles and Airplane save/Continue after
page reload. Activating the persistent test tab before clicking fixed the
pointer-lock failure without changing input behavior or assertions. See
[native bootstrap evidence](evidence/native-reference-2026-09-02.md).
No original/native/browser campaign comparison or mission completion has been
established by this work.

Canonical Quit now stops at the browser frame boundary, writes the current
profile and flushes it before returning to a launcher with a Start game action.
Retryable save failures keep the stopped runtime available. Production tests
verify a last-command volume change is durable, locks are released and startup
can remount the installation. Static, 81 protocol, 43 production browser,
12 smoke and 40 remainder tests pass (5 optional retail skips). See
[Quit evidence and limits](evidence/browser-quit-2026-09-02.md). A separate owned
Chrome 152 diagnostic run also passes Quit from Airplane and restart into the
main menu after its save/Continue checks.

The previously discarded text styles, subtitle glow and timing parameters now
reach shared native text code. Console-ring text is rendered, color escapes
change glyph colors, and the native shadow, monospace, cursor and reveal/decay
paths generate WebGL2 UI quads. Native Win32 and Wasm checks plus a browser
command/pixel check pass. Native SP still links; Release builds remain inside
existing size budgets. Routine verification is 81 protocol, 43 production
browser, 12 smoke and 41 remainder passes (5 optional retail skips). Steam
visual equivalence remains unverified. See [text evidence](evidence/text-presentation-2026-09-02.md).

Save menus now retain header descriptions, maps and timestamps, bound loading
to the native display-index capacity, and select paired profile image paths
without trusting embedded filenames. Native/Wasm regression checks also pass
for the shared screenshot resampler and its corrected end-of-image read.
Save commits now capture the rendered scene into a bounded JPEG, wait for its
durable write, and decode it into the selected canonical UI image. An owned
Airplane image survives page reload and appears in the shipped Load Game menu.
Visual inspection also exposed the wrong Date-column field and an unresolved
description marker; both now use the shared save metadata. Initial saves now
wait for a rendered frame from the matching map; native raw-image loading uses
Kisak's reserved image and passes a native decoder/device-reset test. Complete
native menu and Steam presentation comparison remain open. See
[startup/native evidence](evidence/save-startup-native-2026-09-02.md),
[thumbnail evidence](evidence/save-thumbnails-2026-09-02.md)
and the earlier [metadata/resampler checks](evidence/save-presentation-2026-09-02.md).
Current thumbnail milestone gates pass: native/Wasm save tests, static checks,
83 protocol, 43 production browser, 12 smoke and 44 remainder tests (5 optional
retail skips), plus the separate owned Chrome 152 save/menu/Continue/Quit run.

## Real-time gameplay

The import profile and database file adapter now select localized paths instead
of requiring English. French portable and German directory-picker fixtures
reach canonical database opens and survive reload; changed language markers are
rejected. The owned English Killhouse regression still passes. Non-English
retail inventories, text/fonts, dialogue and gameplay remain unqualified. See
[localized import evidence](evidence/localized-imports-2026-09-02.md).

The Web Audio device now applies both canonical three-band EQ stages and
removes them on bypass. Offline PCM checks cover all five filter types,
stereo/queue continuity and live updates; an owned Killhouse probe attaches a
verified -6 dB bell to a playing sound through canonical console settings.
SND still owns every EQ parameter. The inherited disabled default, Steam DSP
comparison and authored update transients remain open. See
[EQ device evidence](evidence/browser-eq-2026-09-02.md).

The existing OpenAL reverb DSP now passes native/Wasm differential checks for
all 26 room presets at five sample rates. The shared preset header also builds
in native SP. Canonical room and wet changes now reach an AudioWorklet through
the existing Web Audio driver, with browser PCM tests for routing, stereo,
position, EQ, live updates and reset. Authored campaign transitions, callback
cost and Steam audio comparison remain unqualified. A production Chrome 152
Killhouse run verifies the real map's room/wet settings, console-driven fades,
nonzero wet output and restoration. Current regression checks pass (12 smoke,
48 remainder with six optional skips, 43 production, 83 protocol), while the
unchanged production size gate still fails after cinematic/reverb additions. See
[reverb boundary and evidence](browser-reverb.md).

Unintended slow motion had two causes: a `long double`/`double` alias in the
canonical slow-command parser corrupted normal speed in Wasm, and the browser
plus native frame-time caps discarded elapsed time beyond 100 ms. The shared
parser now converts numerically; the browser selects the native five-second
long-stall ceiling. Deliberate script slow motion and opt-in profiling
`fixedtime` remain supported. See [the parser proof](evidence/canonical-timescale-2026-09-02.md)
and [the browser timing evidence](evidence/browser-frame-time-2026-09-02.md).

## Current campaign boundary

Shipped New Game now has observed natural Killhouse rifle, timed-shooting and
sidearm/melee training, three authored checkpoints and Resume Game in a fresh
browser process. The next observed objective is to locate Captain Price; the
course, mission completion, CargoShip and following transition remain unverified.
This gameplay exposed a Save and Quit crash after saving: the frame lacked a
live canonical disconnect jump target. The browser now unwinds that frame,
shares native client/server shutdown and UI reload, and resumes the checkpoint
without losing its Worker. Two production cycles and the separate owned Chrome
retail regression pass. Current routine gates pass: 43 production, 12 smoke and
44 remainder tests, with five optional retail skips. See
[training and disconnect evidence](evidence/campaign-training-disconnect-2026-09-02.md).

Canonical SP menu startup now runs before a map is active. A legal-retail
Release-diagnostics run loaded 69 shipped menus, opened `main`, Options,
Profiles, and Load Game through Kisak's command/UI owners, then loaded
Killhouse and verified the shipped pause and in-game menus. Escape paused and
resumed through the existing key queue. The reached Wasm defect was an invalid
SP `int()`-to-`void()` command callback cast; SP now matches MP's typed
`openmenu` callback. See [the focused evidence](evidence/canonical-menu-lifecycle-2026-09-01.md).

The top-left objective notification is also canonical end to end. The normal
server spawn loads `ui/ingame.txt`; a synthetic diagnostic configstring then
proved current/completed parsing, localized objective text reaching the
renderer command boundary, and the existing timed fade/hide behavior. See
[the objective evidence](evidence/canonical-objective-notification-2026-09-01.md).

Canonical dvar/config persistence now also runs in native order. The mounted
runtime restores the full shared dvar and key-command tables, executes the
active profile's `config.cfg` after retail RawFiles are available, and calls
`Com_WriteConfiguration` from the frame loop. A fresh legal-retail run proved
all seven dvar value families, representative ROM/cheat/archive/latch flags,
`seta`/`toggle`/`reset`/`bind`, config creation, and archive/binding survival
across a page and Worker restart. See
[the config evidence](evidence/canonical-dvar-config-2026-09-01.md).

Canonical player profiles now create, enumerate, select, isolate archived
configuration, survive a page/Worker restart, and delete through the shipped
UI and shared Kisak profile owner. The only new storage operation is recursive
directory removal at the Worker filesystem boundary; profile identity and
configuration remain native engine state. The reached Wasm dvar-domain callback
ABI mismatch was corrected at its shared typed call site. See
[the profile evidence](evidence/canonical-profile-lifecycle-2026-09-01.md).

An earlier canonical save/load menu persistence run made
an Airplane `devsave`, discovered and selected it through the shipped feeder,
loaded it through the real menu script, and reopened it after a full page and
Worker restart through Continue. Health, weapon, ammo, objective state, and
position matched; other profiles saw an empty list; exact canonical deletion
removed only the test-owned save. Its screenshot path used the shipped fallback;
the later thumbnail work above replaces that omission. Reached fixes restored
the canonical version dvars and a 1 MiB web
platform stack for native-scale load depth. See
[the save/load evidence](evidence/canonical-save-load-2026-09-01.md).

The playable offline slice already exists across six legally validated maps.
Those runs demonstrate canonical movement, aiming, firing, weapons, audio,
transitions, and Airplane save/load continuity. Building another player
controller or simulated movement path is out of scope. The obsolete mission
route author/replay system and its waypoint/progression simulation have been
deleted; `village_assault` progression automation remains retired.

The dynamic DObj convergence path is complete through `838e047c`. A headless
Release-diagnostics observe-only run based on that commit now establishes
`scoutsniper` as **RENDERS**: all canonical lifecycle stages completed, the
first real world frame arrived, and 3,601 stationary frames ran over 60.013
seconds with no page, WebGL, or lifecycle error. No input was injected and no
headed/manual visual inspection occurred, so this is not a `FUNCTIONAL`,
`PLAYABLE`, or visual-correctness claim. See
[the stationary evidence](evidence/scoutsniper-stationary-838e047c.md).

The requested canonical SP UI and persistence convergence task is complete.
The subsequent `ac130` renderer work is recorded above; it adds stationary
rendering evidence without extending the persistence or mission-flow claim.

## Renderer evidence index

The [convergence inventory](web-port-convergence.md) records current ownership
and implementation. These commit-bound measurements remain historical; they
do not establish current campaign performance or new compatibility claims.

| Area | Evidence |
| --- | --- |
| Dynamic geometry handoff | [the historical evidence and limits](evidence/dynamic-geometry-ownership-af601efe.md) |
| Renderer optimization audit completed | [evidence and limits](evidence/dynamic-primary-light-linkage-4ed38a84.md) |
| Dynamic opaque draw-order milestone | [evidence and limits](evidence/dynamic-opaque-sort-c8c4f335.md) |
| Dynamic spot-shadow caster milestone | [evidence and limits](evidence/dynamic-spot-shadows-9a253c6a.md) |
| Dynamic sun-cascade visibility milestone | [evidence and limits](evidence/dynamic-sun-partitions-6ece6ee9.md) |
| BSP sun-cascade visibility milestone | [evidence and limits](evidence/bsp-sun-partitions-a23850aa.md) |
| Static spot-shadow membership milestone | [evidence and limits](evidence/static-spot-membership-26b3dc98.md) |
| Static-instance upload milestone | [evidence and limits](evidence/static-instance-uploads-ac8b00ca.md) |
| Static sun-shadow partition milestone | [evidence and limits](evidence/static-sun-partitions-cc4af645.md) |
| DObj conversion and dynamic shadow milestone | [evidence and limits](evidence/dobj-conversion-30e34cff.md) |
| Retained-renderer milestone | [the evidence](evidence/retained-renderer-49af3948.md) |
| Seeded brush shader-hash optimization | [the evidence](evidence/seeded-brush-hashes-06ad8004.md) |
| Paused profiling exposes geometry variation | [the evidence](evidence/paused-copy-qualification-cd85e18e.md) |
| Camera/time qualification | [the evidence](evidence/controlled-renderer-552a468d.md) |
| Brush cost investigation | [the evidence](evidence/brush-costs-f15c3dc9.md) |
| Direct DObj vertex emission | [the evidence](evidence/dobj-emission-fb596702.md) |
| Renderer CPU-efficiency milestone | [the completed milestone](evidence/renderer-cpu-milestone-e4db91df.md) |
| Conditional falloff uniforms | [the evidence](evidence/falloff-uniforms-12ac17e5.md) |
| Dynamic draw texture setup | [the texture-state evidence](evidence/dynamic-textures-74fe11aa.md) |
| Dynamic geometry staging | [the staging evidence](evidence/dynamic-staging-9403a899.md) |
| Particle-cloud append optimization | [the comparison](evidence/cloud-append-ae37e80c.md) |
| Scene-construction measurement | [the scene profile](evidence/scene-stages-2ce03241.md) |
| DObj hashing optimization | [the comparison](evidence/dobj-hash-d8661476.md) |
| Focused DObj measurement (`946dc918`) | [the stage profile](evidence/dobj-stages-946dc918.md) |
| World-camera visibility | [the world visibility evidence](evidence/world-camera-visibility-2026-08-31.md) |
| Canonical static-camera visibility (`317fc12f`) | [the visibility evidence](evidence/static-camera-visibility-2026-08-31.md) |
| Renderer handoff (`a44119df`, from `bf5ec1e2`) | [the renderer record](evidence/renderer-efficiency-2026-08-31.md) |
| Cleanup baseline at `bf5ec1e2` | [cleanup evidence](evidence/cleanup-renderer-2026-08-31.md) |
| Historical automated evidence | [the comparison](evidence/retail-profile-93451ec5.md), [the regression record](evidence/retail-six-map-regression-39de3d6d.json) |

The [campaign matrix](campaign-compatibility.md) owns map classifications and
save/load claim scope. Earlier milestone narratives are in [Git history](history/README.md).

## Product boundaries

WebGL2 and the Worker/DOM/storage/audio adapters are platform-owned. Kisak owns
assets, game state, filesystem semantics, and the renderer frontend. Production
and diagnostics remain separate artifacts. Imported retail files remain local;
proprietary binaries and data are never distributed. The source-built FFmpeg
codec implements movie playback behind Kisak's cinematic API; missing imported
movies remain explicit omissions. WebGL2, single-threaded Wasm and the offline
single-player scope remain unchanged.
