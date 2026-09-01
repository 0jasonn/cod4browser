# Scoutsniper stationary campaign probe

Recorded 2026-09-01 from dirty work based on `838e047c`, using the Release
diagnostics build and headless Chrome 152.0.7977.65 on the documented Windows
11 / Ryzen 7 7800X3D / RTX 3070 Ti reference host. The legally owned retail
root remained local and is not recorded here.

The observe-only `scoutsniper` run used the existing campaign validator. It
loaded CargoShip first, transitioned normally, completed canonical database,
ClipMap/world, server/game, client/cgame initialization, and produced its first
real world frame 9,547.870 ms after the map command. No keyboard, mouse, fire,
waypoint, coordinate, objective, or synthetic gameplay state was injected.

The stationary window remained visible and focused for 60,012.685 ms and
recorded 3,601 world frames. Frame intervals averaged 16.667 ms with 17.480 ms
p95. This headless observe-only timing does not establish gameplay performance
or visual correctness. The compatibility label is **RENDERS**, not
`FUNCTIONAL` or `PLAYABLE`.

The 300-frame profile completed with no missing samples. Average attributed
CPU was 6.996 ms total: 4.060 ms renderer backend, 2.838 ms cgame/frontend,
1.983 ms scene construction, 1.424 ms spot-shadow drawing, 0.881 ms sun-shadow
drawing, 0.786 ms DObj building, and 0.756 ms dynamic-model drawing. These are
attribution values, not evidence of a bottleneck requiring optimization.

Steady-state Wasm capacity was 955,514,880 bytes, allocator in-use memory was
813,555,320 bytes, retained recovery data was 414,917,420 bytes, estimated GPU
textures were 1,086,721,340 bytes, geometry was 75,769,044 bytes, and the
largest temporary upload was 16,777,216 bytes. No page error, WebGL error,
canonical lifecycle failure, renderer fallback, or static-model submission
failure was observed. Local-light telemetry reported 63 exact spot batches and
zero fallbacks; static-model telemetry reported 2,141 batches and zero
failures.

No runtime fix or focused regression was added because the probe exposed no
compatibility defect. The next evidence-gated stationary probe is `ac130`, with
particular attention to thermal rendering and unusual materials.
