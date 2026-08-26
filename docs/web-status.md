# Web product status

## Demonstrated

The production Release artifact is an explicit Worker-hosted offline
single-player slice. With locally supplied, legally owned English assets, the
canonical runtime has been observed through DB publication, ClipMap,
server/game, local client/cgame, Killhouse world frames, static and dynamic
models, HUD, keyboard/mouse input, effects, and Web Audio.

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
  Other detected SP zones are recorded but not claimed compatible.
- Native Bink, Miles, Steam, raw UDP, and native DLLs are not shipped.
- Cinematics currently complete as explicit visible omissions.

## Known gaps

Gamepad, full cinematic playback, advanced audio/EAX parity, multiplayer
transport, unvalidated campaign maps, and remaining renderer/material
families remain. The renderer reports retained/GPU-estimate memory telemetry,
but its 800 MiB decoded recovery ceiling has not been reduced because no
post-change Killhouse/CargoShip retail transition and visual evidence was
available during cleanup.

See [campaign-compatibility.md](campaign-compatibility.md) for claim scope and
[web-roadmap.md](web-roadmap.md) for ordered follow-up work.
