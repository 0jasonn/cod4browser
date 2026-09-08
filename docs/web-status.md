# Web product status and priorities

Updated 2026-09-09. Target: offline single-player using real Kisak systems,
ultimately matching original Steam COD4 (2007). This records implementation and
remaining work; it does not certify the current checkout. See
[ownership](architecture.md), [test results](web-test-inventory.md) and
the [campaign matrix](campaign-compatibility.md) for exact scope.

## Current qualification

The 2026-09-09 source-cleanup working tree passed native SP and both browser
Release builds, 44 native, 43 direct-Wasm, 124 Node, 130 browser and 14 package
tests, with 16 optional browser skips. The unchanged production size/export gate
passed. These are local synthetic checks, not aggregate release or campaign
acceptance. [Execution details](web-test-inventory.md#current-execution-evidence)
record versions, sizes and exclusions. Those results precede this documentation
consolidation; runtime suites were not rerun for Markdown changes.

Historical map evidence qualifies Killhouse and Airplane as `PLAYABLE`,
CargoShip/Blackout/Hunted/Bog A as `FUNCTIONAL`, and Scoutsniper/AC130 as
`RENDERS`; 14 discovered direct SP zones are `UNTESTED`. No mission has
established completion or full retail fidelity. Natural Killhouse training,
checkpoints 1–3 and fresh-browser Resume Game were observed; acceptance stops
at the Captain Price/ladder platform. Airplane save/reload continuity does not
prove objective progression. See the [campaign matrix](campaign-compatibility.md).

The [native SP reference](native-reference.md) builds with pinned MSVC/OpenAL
and enters menu code using owned startup files. Matched original/native/browser
visual and gameplay comparison remains open. Manual gameplay and mission
acceptance belong to the user; engineering work does not depend on completing
a mission. Automated routes and injected objective success are retired.

## Implemented boundaries

| Area | Current implementation | Remaining qualification |
| --- | --- | --- |
| Engine/loading | Canonical `Com_Init`, DB/XFile, ClipMap, server/game, client/cgame and renderer frontend run in a dedicated Worker. JSPI and native Wasm exceptions preserve loading suspension and canonical error recovery. Native loader oracles cover RawFile, Material/Image and XModel/PhysPreset. | Broader multi-zone graphs, malformed nested assets, remaining loader families and non-world device rollback. |
| Storage/distribution | Local imported files, bounded queues, journaled home replacement, backup/raw-file export and non-overwriting restore, same-origin restart and source/dependency receipts. | Linux/hosted aggregate CI, actual release-version update/rollback and campaign save/Continue/offline acceptance. See [browser support](browser-support.md). |
| Input/menus | Pointer lock, keyboard/mouse, focus release, Windows-1252 text/editing and bounded trusted paste feed native owners. Canonical menus, dvars, config and isolated profiles survive restart. | Windows IME candidate UI, other code pages/glyphs, broader clipboard behavior and localized menus. |
| Saves/lifecycle | Shared metadata/feeders, Continue/deletion and bounded canonical thumbnails. Save and Quit uses canonical disconnect cleanup; Quit flushes the profile and preserves a retryable runtime on save failure. | Authored chapter flow, native/Steam presentation and localized dates/descriptions. |
| Display/text | Canonical `vid_restart` applies resolution, user gestures own fullscreen, browser controls refresh, final pass applies brightness. Shared text handles styles, colour, cursor and timed reveal. | Matched Steam display/text response. |
| Audio/video | Canonical SND feeds Web Audio; device feedback owns source completion. OpenAL Soft reverb/EQ and FFmpeg Bink decoding stay behind device boundaries. | Authored room/EQ transitions, callback cost, listening/latency, long stalls and in-world movies. See [audio](browser-reverb.md) and [cinematics](cinematic-codec.md). |

Canonical assets, pose, LOD, visibility, material identity and renderer frontend
remain Kisak-owned. The [renderer guide](renderer-retained-resources.md) records
retained GPU resources, camera/caster admission, ordering, lighting, materials,
effects and context recovery. Exercised shader families and synthetic pixels do
not establish general multipass or original-game fidelity.

Three graphics boundaries remain explicitly unqualified: installed
Chrome/D3D11 recorded three one-byte exact-pixel differences; a later transient
light run passed lighting/shadow/clear/recovery but failed its final positive
DObj post-pose diagnostic; the optional Gate 3 fixture expects older camera-only
geometry and times out. Keep assertions intact. See
[test limits](web-test-inventory.md#known-qualification-limits).

Seven headed production windows recorded on 2026-09-05 covered 1280×720 and
1920×1080, Killhouse context recovery, advancing game/audio clocks and 978.5 MiB
Wasm capacity. CargoShip measured 20.63/17.28 FPS in distinct active scenes.
The floor-facing Killhouse view and near-60 FPS AC130 windows do not qualify
busy gameplay or thermal fidelity. These measurements are historical; profile
current foreground CPU/GPU stages before selecting another optimization.

## Next work

The next milestone is a recoverable, reproducible SP alpha. Proceed in order:

1. Run the repaired Linux native tier and seeded sanitizer CI. The earlier
   Windows sanitizer crash replay and 30-second seeded pass do not qualify Linux.
2. Qualify actual release-version backup/update/rollback and the owned-install →
   play/save → disconnected restart → Continue journey. Synthetic recovery and
   package replacement checks cover narrower boundaries.
3. Capture matched active CargoShip stage/resource measurements before retaining
   a performance change. Include stalls, game/wall time, responsiveness,
   Wasm/GPU/audio memory and repeated transitions. Profile before pthreads.
4. Run aggregate CI from clean committed source and qualify the complete source
   and site package. No aggregate package or alpha has been qualified/published.

For fidelity work, fix an observed engine/platform defect or a named reference
gap: localized input/text; shipped graphics controls and unknown shader families;
transparency, special vision, saved screens, clouds/distortion/soft particles;
audio/cinematic limits above; larger native-loader graphs and resource pressure.
Qualify Firefox/Safari separately from Chrome/Edge; gamepad follows demonstrated
product requirements. Preserve source, export and size gates throughout.

Manual acceptance still needs natural Killhouse course completion, CargoShip
chapter flow and the next authored transition, with objectives, scripted
sequences, checkpoints, death/restart and fresh-browser Continue. Compare
original Steam, native Kisak and browser with matching scenes/settings/difficulty;
then extend coverage to remaining missions and localized installations.

Use focused checks while iterating and the non-overlapping routine
[validation tiers](web-test-inventory.md) before handoff. Update the relevant
current guide when behavior changes; full dated reports are in
[Git history](../README.md#historical-records).
