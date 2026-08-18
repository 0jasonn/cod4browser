# Gate 3 canonical startup-zone chain

## Engine-owned request order

The browser no longer starts the real database with a handwritten single-zone
request. `CL_SetFastFileNames` supplies the single-player renderer
configuration and the shared `R_LoadGraphicsAssetZones` implementation builds
the same ordered `DB_LoadXAssets` request as native Kisak:

```text
code_post_gfx (alloc 2)
ui            (alloc 8)
common        (alloc 4)
```

Localized and mod entries retain their native conditional positions. The web
entry adapter supplies no zone names and does not seek or preselect an asset.
All files open through the Worker-mounted synchronous filesystem path.

## Owned English retail observation

The normal request completes all three prerequisite zones through
`DB_LoadXZone`, XFile inflate, canonical streams, generated loaders, and real
pool/hash publication:

| Zone | Ordered assets | Final asset | Final stream offsets |
| --- | ---: | --- | --- |
| `code_post_gfx.ff` | 1,639 | 1,638, RawFile `code_post_gfx` | `[0,0,0,0,407412,0,0,4224,480]` |
| `ui.ff` | 35 | 34 | `[0,0,0,0,1267176,0,0,0,0]` |
| `common.ff` | 6,502 | 6,501, RawFile `common` | `[0,0,0,0,28021740,0,0,438944,76704]` |

The retained cross-zone registry records 9,637 real publication events and
ends at entry 9,652 with 23,115 free entries. The complete chain encounters no
unsupported generated family and reports no generated-load failure. A failed
zone terminates the remaining batch, matching the native error boundary rather
than allowing later requests to obscure the first failure.

Gate 2 remains frozen and opt-in. This prerequisite milestone does not call its
census. The continuation now resumes from the engine path after renderer
prerequisite loading: a real `map` command reaches `SV_SpawnServer`, opens
`killhouse.ff` through the Worker filesystem, and begins canonical generated
traversal as recorded in
[`canonical-map-lifecycle.md`](canonical-map-lifecycle.md).

That continuation now reaches and publishes Killhouse GfxWorld asset 772
through the normal DB, matches the frozen Gate 2 world/geometry observations,
and draws its bounded surface through WebGL2. The next ordered asset is
GameWorldSp 773. Generated-loader expansion is paused there while the runtime
pivots into the real runtime owners. Production now compiles and wires the
actual `CM_LoadMap` and fastfile `Com_LoadWorld` path behind successful map DB
completion. Their canonical singleton identities are `&cm` and `&comWorld`;
the renderer world pool likewise publishes into `&s_world`. The current
ordered map-zone stop intentionally prevents those calls until ClipMap has
been published, and browser evidence asserts that no premature lifecycle event
occurs.

## Runtime pivot architecture boundary

The production Wasm target now links the client, cgame, game/server, effects,
ragdoll, physics, sound, collision, save, script, XAnim, and DObj source
closures with no undefined symbols. Exact x86/Wasm tests enter the bounded
runtime owners. The Worker startup adapter deliberately remains at
`CL_InitRef` plus the shared renderer zone request: entering the complete
native `FS_InitFilesystem -> CL_Init` sequence currently reaches host
search-path/IWD behavior that is not backed by the Worker manifest.

The next architecture decision is therefore to make the existing synchronous
Worker mount implement canonical filesystem search paths, directory
enumeration, and minizip-backed IWD access. This must not introduce MEMFS
copies, hard-coded zones, or a second browser-owned asset registry. Gate 2 and
both shrink-only prefix files remain frozen/no-growth regression boundaries
while that integration is designed.

## Differential and browser evidence

`canonical_startup_zone_tests` runs the shared configuration/request code on
MSVC x86 and Wasm and prints the same normalized order and flags. The existing
generated-loader differential executable remains byte-for-byte identical
across those targets. The owned Playwright test asserts the three per-zone
completion checkpoints and aggregate canonical registry state. A dedicated
MSVC x86/Wasm differential additionally executes real `CM_LoadMap` followed by
real `Com_LoadWorld`, verifies the three subsystem-owned DB pool addresses,
and emits an exact identical lifecycle/checksum/Hunk trace.
