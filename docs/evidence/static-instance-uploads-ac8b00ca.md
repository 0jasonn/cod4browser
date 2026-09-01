# Static-instance upload milestone

Runtime `ac8b00ca` keeps canonical static-model shadow AABBs in CPU-owned
renderer data and restores the instanced GPU record to 72 bytes. Shadow bounds
remain aligned with the first-half LOD packing and continue to drive independent
near/far light-space selection. Camera DPVS still packs the second half and never
selects sun or authored spot casters.

The same change also narrows visibility-only buffer updates to that camera half.
LOD changes still upload both halves because they repack shadow and camera
instances. Initial publication, descriptor validation, failure atomicity, unload,
and context recovery retain their existing ownership.

## Moving-camera diagnostic result

The seeded paused CargoShip workload alternates canonical camera yaw every ten
views during an exact 120-view diagnostic capture. Eleven visibility transitions
exercise the static instance update. The control artifact was built from clean
`70c08b8f` in an isolated worktree; the runner's `dirty` field describes the
separate orchestration checkout and is not the control artifact provenance.

| Static update | Bytes per transition | Change from control |
| --- | ---: | ---: |
| 96-byte record, both halves (`70c08b8f`) | 1,143,552 | control |
| 72-byte record, both halves (`7312a54d`) | 857,664 | -25.0% |
| 72-byte record, camera half (`ac8b00ca`) | 428,832 | -62.5% |

CargoShip retains 5,956 source instances in this view. The final value is exactly
`5,956 * 72`; the control is `5,956 * 2 * 96`. Across eleven transitions the
static upload falls from 12,579,072 to 4,717,152 bytes, saving 7,861,920 bytes.
The four observed total-upload buckets independently reproduce the 428,832-byte
increment in both camera orientations.

No upload-time or frame-time improvement is claimed. Single-run upload timing
varied despite exact byte counts, and most per-frame bytes belong to dynamic
geometry. This milestone removes deterministic transfer work from camera
visibility changes.

## Validation and limits

- The focused `web_renderer_static_model_scene_tests` target passed (1/1,
  0.08 s) after the final source changes. It covers canonical bound transfer,
  light-space partition contact, LOD and camera packing, and atomic errors.
- Diagnostic Release builds passed for the compact-record and camera-half
  stages. Each moving-camera profile completed 120 samples with zero page errors.
- One final production Release passed with the canonical runtime-prefix check;
  no runtime changes followed it.
- Static instance and bounds sizes are compile-time checked at 72 and 24 bytes.
- No broad suite, mission check, capture, context-loss run, or compatibility
  promotion ran. Retail data, paths, and logs remain outside version control.

The final production Wasm SHA-256 is
`9b74a00fb01384c4e9cf4594bfc8dbcc7c2144ec4514e1cfa9d2da17c90f3b0b`.
The final diagnostic Wasm SHA-256 is
`03bdfb044d513f5cef76f7447871d357e06be2d507b0d603c21d11836a78898a`.
Numeric results and raw-record hashes are in
[the companion record](static-instance-uploads-ac8b00ca.json).

## Recommended next task

Attribute spot-shadow draw cost to world, static, and dynamic caster families in
the same controlled workload. Optimize only the dominant family while retaining
canonical light membership, static camera-culling independence, and current
recovery behavior.
