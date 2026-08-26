# Web product architecture

## Runtime ownership

```text
launcher / install UI (main thread)
    -> versioned named messages
engine Worker + synchronous filesystem view
    -> canonical Kisak DB, game, cgame, renderer frontend
portable draw commands
    -> Worker-owned OffscreenCanvas / WebGL2
OpenAL-compatible commands
    -> main-thread Web Audio
```

The browser owns only platform boundaries: page lifecycle, file selection,
OPFS/IndexedDB coordination, Worker hosting, WebGL2, Web Audio, and input event
translation. `XAsset`, `XModel`, `Material`, `GfxWorld`, game/cgame, collision,
and script behavior remain canonical Kisak owners.

## Product protocol

Every request and startup handshake carries protocol version 1. Production
accepts only `init`, `mountAssets`, `flushAndUnmount`, `probeAsset`,
`checkpoint`, `submitCanonicalCommand`, `resize`, `input`, `runtimeStatus`, and
`shutdown`. Input uses the separate one-way `input-event` message: the host
validates and posts it without allocating an RPC request ID, while the Worker
validates it again before calling the canonical key or mouse queue.
The Worker validates payloads/ranges at its boundary and returns structured
errors. The host supplies timeouts, supports `AbortSignal` for non-mutating
probe/status work, avoids request-ID collisions after wrap, and rejects all
pending work on protocol, Worker, message, or shutdown failure.

## Storage and shutdown

Imported assets coordinate shared reads. Browser home data has an exclusive
Web Lock owner. Filesystem mutations enter one strict FIFO persistence chain;
path barriers plus object-identity and version checks prevent an old completion
from marking a replacement durable. Shutdown stops new work, persists dirty
open descriptors, awaits the mutation chain, closes imported handles, releases
leases, closes the asset-store BroadcastChannel and IndexedDB connection,
removes listeners, and terminates the Worker last.

## Input coordinates and lifecycle

Canonical `UI_Component::MouseEvent` treats `x >= screenWidth` or
`y >= screenHeight` as outside, so absolute browser coordinates are pixel
indices clamped to `0..width-1` and `0..height-1`. CSS coordinates are scaled
to canvas backing pixels, including device-pixel ratio. Zero-sized resize
transients emit no absolute event. Relative motion is frame-coalesced, but its
pending callback and deltas are cancelled on blur, hidden visibility,
controller disposal, or fatal transport failure; held keys and buttons still
receive their release events while delivery remains available.

## Build products

`KisakCOD-web` and `build/web/site` are production. With
`KISAK_WEB_DIAGNOSTICS=ON`, `KisakCOD-web-diagnostics` and
`build/web-diagnostics/site-diagnostics` expose browser-only test controls and
telemetry. Both artifacts compile the same runtime sources; diagnostic exports
cannot enter production.
