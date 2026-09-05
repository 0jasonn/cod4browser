# Dynamic primary-light linkage milestone

Runtime `4ed38a84` restores Kisak's primary-light linkage for browser dynamic
spot casters. DObj spheres, moving-brush boxes, DynEnt model boxes, and DynEnt
brush boxes now update the canonical `GfxWorld` entity/DynEnt visibility bit
arrays. The port preserves the SP renderer's 2,208-entity view stride, native
spot/omni radius and cone tests, authored `GfxLightRegion` hull rejection, and
nearest non-sun light publication for model DynEnts.

The portable draw boundary retains only entity family and numeric identity.
After the existing spot-light selection, the backend builds a four-bit mask
from the canonical visibility arrays. Each spot pass applies that mask before
its existing light-matrix AABB test. Camera DPVS, both sun matrices, static
model authored membership, draw ordering, and retained geometry remain
independent. Missing canonical visibility storage falls back conservatively to
the previous matrix-only behavior.

## Diagnostic result

The seeded paused CargoShip workload uses fixedtime 16, one stable camera, and
profile views 601-720. The prior `c8c4f335` diagnostic and the linkage candidate
match all 120 work-count samples exactly.

| Recorded work | Control | Candidate |
| --- | ---: | ---: |
| Dynamic camera draws | 1,323 | 1,323 |
| Physical shadow caster draws | 340 | 340 |
| Submitted indices | 1,023,750 | 1,023,750 |
| Dynamic command vertices | 47,876 | 47,876 |
| Dynamic command indices | 124,644 | 124,644 |
| Buffer uploads | 3,964,368 bytes | 3,964,368 bytes |

This view's linked dynamic casters belong to the selected spot lights, so
canonical membership does not reduce its submitted work. The single diagnostic
timing observation moved spot preparation from 0.0095 to 0.0382 ms, dynamic
spot drawing from 0.0835 to 0.0649 ms, and total spot drawing from 1.1833 to
1.1635 ms. Run noise and unchanged work make these attribution values
descriptive only; no performance improvement is claimed.

## Validation and limits

- Focused `web_renderer_primary_light_core_tests` passed (1/1, 0.03 s; 0.06 s
  total). The synthetic fixture proves entity/DynEnt bit indexing, link/unlink,
  nearest model light publication, and authored light-region rejection.
- The diagnostic profile comparator passed 120/120 exact work-count samples.
- Diagnostic Release and runtime-prefix checks passed. The one final production
  Release and runtime-prefix check passed.
- Final production Wasm SHA-256:
  `b5432899aa4a7149cb42d6607895f5dce7a397cacb5fc220a8972e72a4386287`.
- Final diagnostic Wasm SHA-256:
  `810178814dacb48068d15e863c06fa343d76f4296d2cc4a13d6c9c78decb4b0f`.
- No broad suite, mission check, capture, context-loss run, or unrelated
  compatibility promotion ran. Retail assets, paths, and logs remain outside
  version control.

Numeric results and raw hashes are in
the companion record (archived in Git).

Retrieve the archived numeric record with
`git show 3942e819802fbd8f842802ec2c11267def087c14:docs/evidence/dynamic-primary-light-linkage-4ed38a84.json`.
