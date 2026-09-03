# Verification repair — 2026-09-02

Working tree based on `a2d54b7d`; no campaign compatibility promotion.

The seven implicit parameter types in Worker transport and missing
`sunShadowMergedRanges` workload fixture reproduced. Transport now describes
the shared pending-request contract. The workload test retains the required
counter and also rejects its omission; validation was not relaxed.

The native-picker test could pass alone before its asynchronous engine mount
finished, but failed in the remainder tier. Its duplicate fixture omitted
`fileSysCheck.cfg`. Reusing the shared fixture and waiting for a completed
mount exposed further obsolete startup inputs: missing `configure_mp.csv`,
truncated `ui.ff`, and absent `soundaliases/channels.def`. The shared synthetic
fixture now supplies those through ordinary IWD/RawFile loading. The expected
archive-entry total increases from 21 to 22 for the additional configuration
file; archive count is still 21. Initial mount and reload are both asserted.

The opaque error was a separate product defect: `Com_Init` had returned before
the imported-files continuation called `Com_Error`, leaving no live canonical
`setjmp` boundary. The mount entry now establishes that boundary and throws
the canonical error text across the existing Worker protocol. Ownership and
cleanup classification remain unchanged. Deliberately missing the filesystem
check reports `fileSysCheck.cfg` in both diagnostic and production launchers.

## Execution

- Pinned Node 24.18.0 / npm 11.16.0; `npm.cmd ci` and
  `npx.cmd playwright install chromium` completed.
- `npm.cmd run check:web:static`: passed.
- `npm.cmd run test:protocol`: 81 passed.
- Release diagnostics and Release production built with Emscripten 6.0.6;
  both canonical runtime-prefix checks passed.
- Bundled headless Chromium 149.0.7827.55, diagnostic site on isolated port
  8137: picker plus malformed-install regression 2 passed; routine smoke
  12 passed; non-overlapping remainder 38 passed / 3 optional retail skips.
- Production site on isolated port 8138: production boot and actual malformed
  canonical mount regressions 2 passed.
- Retail environment variables were explicitly removed for synthetic runs.
  Playwright now refuses an occupied server port, avoiding accidental reuse
  of another artifact. Existing user servers were left running.

No exhaustive browser duplicates or retail gameplay run was used for these
claims. This proves boot, import/mount validation and error handling, not
authored mission completion or Steam fidelity.

## Reference work

[Steam reference inventory](steam-reference-2026-09-02.json) pins the local
Steam build 2737681 with SHA-256 hashes, active graphics/audio/input config and
host hardware. Difficulty is absent from that config and remains unverified.
Settings are file evidence, not inspected visual/functional outcomes.

The existing native SP build was retried using its configured Visual Studio
18 Community CMake. Compilation fails on pre-existing duplicate canonical
types (`CriticalSection`, `FastCriticalSection`, `GfxWorld`, among others)
and missing declarations in native translation units. No native executable
or native/Steam gameplay comparison was established. The narrow Wasm error
boundary does not compile into that target. Repair native header/build
convergence separately before claiming the three-way reference is runnable.
