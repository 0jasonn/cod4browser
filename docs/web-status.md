# Web product status

## Demonstrated

The production Release artifact is an explicit Worker-hosted offline
single-player slice. With locally supplied, legally owned English assets, the
canonical runtime has been observed through DB publication, ClipMap,
server/game, local client/cgame, Killhouse and CargoShip world frames, static
and dynamic models, HUD, keyboard/mouse input, effects, and Web Audio.

The 2026-08-26 roadmap execution preserved the Phase 0 non-retail baseline,
then completed a clean, local two-map retail validation at
`ac063bb20cbc4027497841322d87c2069d736939`. Killhouse and CargoShip both
passed canonical DB/cgame/world-frame checks, at least 60 seconds of sustained
frames, keyboard/mouse input, audio, configuration checkpoint/reload, and
no-fatal-error checks. The Killhouse-to-CargoShip transition retired all
1,521,922,580 B of old-map aggregate CPU recovery before new-world publication.
Forced WebGL context recovery restored frames and input in 1,779.19 ms. This is
local evidence against legally owned assets; hosted CI remains synthetic. See
the [execution report](../WEB_ROADMAP_EXECUTION_REPORT.md) and
[structured record](evidence/retail-phase1-ac063bb2.json).

The first Phase 3 campaign batch then validated Blackout at clean source SHA
`6be926cb4e78693f9f6e638c348b0ee0f908b45f`. Its canonical ClipMap, world,
server, game, client, and cgame stages completed; the first world frame arrived
in 11,144.985 ms; and 73 frames were observed over 60,022.685 ms. Gameplay
input and audio, a configuration checkpoint/reload, CargoShip-to-Blackout and
Blackout-to-Killhouse retirement, forced context recovery in 2,149.595 ms, and
clean shutdown/reload all passed. This remains local evidence against legally
owned assets, not hosted-CI coverage. See the
[Phase 3 structured record](evidence/retail-phase3-6be926cb.json).

Blackout exposed a browser-only 20,000 static-model-instance ceiling at the
portable renderer boundary. Commit `164fc1f2` replaced it with the native IW3
65,536-instance cardinality and added focused native/Wasm coverage. Commit
`6be926cb` also made the retail validator await the canonical Worker filesystem
mount before issuing map commands; that is validator hardening, not a parallel
filesystem or runtime workaround.

Production contains only named Worker operations and a versioned protocol.
The writable home filesystem has one cross-tab owner and an awaited,
retryable `flushAndUnmount` path that persists dirty open files before handles
and leases are released. Page lifecycle hooks are best effort; explicit async
shutdown owns durability.

The separate diagnostics build uses the production runtime sources plus
browser-only test exports and telemetry. The retired Gate 2 loader, proof jobs,
renderer comparison, synthetic world extraction, and scheduler are gone.

## Product boundaries

- WebGL2 is the renderer backend; the Kisak frontend and canonical asset
  identities remain authoritative.
- Imported retail files never leave browser-private storage and are not part
  of CI or repository fixtures.
- The default versioned profile is English offline SP with Killhouse required.
  Blackout is also validated; other detected SP zones are recorded but not
  claimed compatible.
- Native Bink, Miles, Steam, raw UDP, and native DLLs are not shipped.
- Cinematics currently complete as explicit visible omissions.

## Known gaps

Gamepad, full cinematic playback, advanced audio/EAX parity, multiplayer
transport, remaining unvalidated campaign maps, and remaining renderer/material
families remain. The renderer reports disjoint retained/GPU-estimate memory
telemetry. Phase 2 keeps the working previous-map eviction and the 800 MiB
(838,860,800 B) **per retained-image-pool admission cap**. This is not an
aggregate decoded-recovery ceiling: Killhouse's static-model pool reached
817,908,800 B (97.50%), leaving only 20,952,000 B of headroom. The unchanged
2,013,724,672 B Wasm capacity after unload is monotonic allocator capacity,
not retained-map evidence. No new memory optimization was justified by the
measured transition.

The current production Release boundary passes with 3,170,512 B of Wasm
(3,324,821 B budget), 339,533 B of application JavaScript (341,401 B budget),
and 3,520,576 B across 17 site files (3,676,856 B budget), with 24 raw Wasm
exports and 9 named application exports. No artifact budget or export allowlist
changed.

See [campaign-compatibility.md](campaign-compatibility.md) for claim scope and
[web-roadmap.md](web-roadmap.md) for ordered follow-up work.
