The `.seed` files in `corpus/` are synthetic GPL-3.0 fixtures authored for this
repository. They contain no game data. Regenerate byte-for-byte with
`python tests/fuzz/generate_corpus.py tests/fuzz/corpus`; SHA-256 identities are
printed by the generator. The DEFLATE seed uses a manually encoded stored
block, so its identity does not depend on a compression library version.

The harness exercises IWI parsing/RGBA decoding and IWD central/local headers
and streaming member decoding. Seeds include successful bitmap, DXT, stored
and deflated paths, malformed sizes/counts, truncation and CRC failure. Runtime
checks cover decoder buffer bounds and failed-image publication rollback.
These are not fastfile, save or cinematic fuzzers. See the existing
[test inventory](../../docs/web-test-inventory.md) for budgets and evidence.
