# Localized installation paths

The importer now derives localized IWD and fastfile paths from the validated
`localization.txt` marker. Both folder pickers, selected-file validation,
persisted metadata and restoration use that language. Other languages' files,
multiplayer fastfiles and proprietary runtime binaries remain excluded. English
profile IDs and schema-3 imports are unchanged.

`web/asset_profile.mjs` remains a platform file-selection profile. It does not
translate game strings or add an asset registry. The existing bounded Wasm
localization probe retains its 15-name allowlist, matching Kisak's names; no
new JavaScript language registry was added. The canonical filesystem selects
localized IWDs and owns `loc_language`. `DB_PlatformBuildZonePath` now calls
`SEH_GetCurrentLanguage`/`SEH_GetLanguageName` instead of hardcoding English, so
the existing database opens the corresponding `zone/<language>/*.ff` files.

Restoration compares the file's validated marker with its recorded language
before checking the language-specific inventory. A same-size marker change
from French to German is rejected as invalid metadata before mounting.

## Execution

- Native MSVC 14.51.36231 Win32 and Emscripten 6.0.6/Node 24.18.0 builds of
  `gate3_db_stream_trace_tests` pass. The added boundary check asks the platform
  path builder for English, French and German using test-supplied SEH values;
  the existing XFile stream/publication checks also remain passing.
- Chromium 149.0.7827.55, diagnostic Release: the 18 asset tests pass on isolated
  port 8184. The existing directory-picker test now uses German fixtures and
  verifies exact file accesses, canonical DB open at
  `zone/german/code_post_gfx.ff`, and persisted reload. The French portable case
  checks the same DB boundary, profile identity, foreign-language exclusion and
  reload. Its added same-size corruption check passes on port 8185.
- Diagnostic routine smoke: 12 passed on port 8187. Remainder: 49 passed,
  six optional retail skips on port 8188. Static checks and all 83 Node protocol
  tests pass. Retail environment variables were cleared for synthetic runs.
- Production browser suite: 43 passed on port 8189 in Chromium 149.0.7827.55.
  Its existing Quit/restart case now boots the French synthetic installation,
  verifies the native `path` command reports `Current language: french`, saves
  configuration, quits durably and starts the canonical runtime again.
- Owned English installation, production Release, Chrome 152.0.7977.65
  headless, port 8186: the existing Killhouse reverb device test passes after
  import, canonical startup, real map loading and audio playback. This is an
  English regression check; it adds no mission-completion evidence.

Production Wasm SHA-256:
`43b0bcbf5739c9133fd76e51c34aac8bb4ce81c1ad2703e35d9f4398affca243`.
Diagnostic Wasm SHA-256:
`6e65cd3ccea6b3431d7a473437659effc84f552ca18506ec4c4fcb2039c89c46`.
Build/test logs are `build/goal-languages-*.log`; private retail output is under
`test-results/8186`. Synthetic fixture provenance remains in
`tests/browser/install_fixture.mjs` and `synthetic_iwd.mjs`.

## Remaining evidence

Only English retail files are locally available. French/German checks use
original synthetic fixtures with localized paths and markers; they do not
establish translated menu rendering, glyph encodings, dialogue, cinematics or
gameplay fidelity. Other identifiers in the engine's allowlist are untested.
The profile retains its required 14 base and seven localized archives, startup
zones and Killhouse. Non-English retail inventories still require verification
against legally owned installations; no retail compatibility is inferred from
marker acceptance.

The unchanged production size gate still fails: main Wasm 3,708,317 bytes
(budget 3,332,379), JavaScript 761,250 (357,646), site 4,567,634 (3,701,082).
File and application-export checks passed before the size failure. The earlier
cinematic/reverb size increase remains unresolved; no budget was raised.
The exhaustive browser duplicates were not run.
