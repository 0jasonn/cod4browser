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
`submitCanonicalCommand`, `resize`, `input`, `runtimeStatus`, and `shutdown`.
The Worker validates payloads/ranges at its boundary and returns structured
errors. The host supplies timeouts, supports `AbortSignal` for non-mutating
probe/status work, avoids request-ID collisions after wrap, and rejects all
pending work on protocol, Worker, message, or shutdown failure.

## Storage and shutdown

Imported assets coordinate shared reads. Browser home data has an exclusive
Web Lock owner. Dirty generations coalesce path persistence and prevent stale
writes from winning. Shutdown stops new work, persists dirty open descriptors,
awaits all mutation chains, closes imported handles, releases leases, closes
the asset-store BroadcastChannel and IndexedDB connection, removes listeners,
and terminates the Worker last.

## Build products

`KisakCOD-web` and `build/web/site` are production. With
`KISAK_WEB_DIAGNOSTICS=ON`, `KisakCOD-web-diagnostics` and
`build/web-diagnostics/site-diagnostics` expose browser-only test controls and
telemetry. Both artifacts compile the same runtime sources; diagnostic exports
cannot enter production.
