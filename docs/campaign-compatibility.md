# Campaign compatibility matrix

Discovery means a selected fastfile passed the bounded header probe and was
recorded in the versioned offline-SP profile. It does not mean the map boots.

| Zone | Discovered by profile | Runtime evidence | Compatibility claim |
| --- | --- | --- | --- |
| `killhouse` | Required | Prior local user-owned run reached sustained world frames, input, HUD, effects, and audio | Validated development slice |
| `cargoship` | Optional SP discovery | Prior import/probe coverage only; no cleanup-run transition evidence | Untested |
| Other non-`mp_*`/non-`*_mp` SP zones | Optional SP discovery | Header probe only when selected | Untested |
| Multiplayer zones | Not admitted | None | Out of offline-product scope |

No proprietary assets are used in hosted CI. Update this matrix only from a
local run using legally owned files and record the exact build/browser.
