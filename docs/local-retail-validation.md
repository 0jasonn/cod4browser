# Local retail validation

This validation is local-only. It reads files from a legally owned English
Call of Duty 4 installation through the browser picker; it does not copy them
into the repository or CI artifacts. Close other cod4browser tabs first so the
test can acquire the exclusive writable-home lease.

After the pinned toolchain, npm dependencies, and Chromium are installed, run:

```powershell
.\tools\validate_web_retail.ps1 -RetailRoot 'D:\Games\Call of Duty 4'
```

The command builds the diagnostic artifact because forced WebGL context loss
is deliberately unavailable to the production protocol. The test otherwise
uses the normal importer and canonical runtime. It requires both
`killhouse.ff` and `cargoship.ff` and checks:

- installation import, persistence, and canonical mount;
- canonical `map killhouse` command acceptance and 120 submitted world frames;
- keyboard movement and a gameplay action that reaches Web Audio;
- `writeconfig`, awaited Worker shutdown, reload, and persisted config exec;
- canonical `map cargoship` acceptance and 120 submitted world frames;
- forced WebGL2 context loss, recovery, resumed CargoShip frames, and renderer
  memory telemetry.

The command is intentionally skipped unless `KISAK_COD4_RETAIL_ROOT` is set.
Record the exact commit, browser version, matrix result, and renderer-memory
events before changing the recovery budget or compatibility matrix.
