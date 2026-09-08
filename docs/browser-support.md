# Browser support policy

The current product target is a Chromium-class browser profile, determined by
features rather than the user-agent string. Startup requires WebAssembly with
Promise Integration (JSPI) and native exception handling, WebGL2, dedicated
Workers, a transferable OffscreenCanvas, IndexedDB, OPFS,
synchronous OPFS access from a Worker, Web Locks, BroadcastChannel, Web Audio,
and pointer lock. A persistent-storage request is optional; the launcher warns
when persistence is not granted because the browser may evict imported local
files.

The launcher checks this profile before creating the engine Worker or opening
the asset store. Missing requirements produce an explicit unsupported-browser
state and list the unavailable APIs. The synchronous OPFS check runs in a
short-lived, non-engine Worker because that API is Worker-scoped.

The Wasm gate requires both `WebAssembly.Suspending` and
`WebAssembly.promising`, plus `WebAssembly.Tag` for exception handling. The
pinned Emscripten toolchain still describes JSPI as experimental. Canonical
map loads remain synchronous-looking while a platform `emscripten_sleep(0)`
yield presents loading UI and receives audio feedback; the frame pump awaits
the suspended Wasm callback before scheduling another frame. This requires
neither pthreads nor cross-origin isolation. See the
[loading behavior and qualification](cinematic-codec.md).

Chrome and Edge should only be called validated when the production browser
suite has passed in those branded channels. Firefox and Safari are not
currently declared supported; this is an untested status, not a claim that a
specific release can never satisfy the feature gate.

## Local package contract

The selected distribution milestone is a complete local-server package.
After extracting it, run `python qualify_web_release.py verify .`, then
`start-local.cmd` on Windows or `python serve_web.py --directory site --port 8000`.
Python 3.11 or newer must already be installed. Open `http://127.0.0.1:8000`
in the same browser profile on every launch. The server binds loopback and serves only
the generated site; it needs no remote service. Hosted cold-offline startup
is not supported and there is no service worker.

Keep the previous verified package when updating. Stop the server and all game
tabs, verify the replacement in a separate directory, then start it on the
same host/port. A failed verification leaves the old directory intact. Rollback
means restarting the previous package at that origin. Changing `localhost`
to `127.0.0.1`, changing port or using another browser/profile changes storage
identity. No save-format migration or cross-version campaign compatibility is
promised by replacing the shell.

Browser controls and the Quit screen offer **Export stored saves**. Quit the
game and close other game tabs first to release the home writer lease. Export
can enumerate malformed journals and oversized homes without mounting them.
Download names use `__` for separators; retain the displayed original path.
It exports durable stored files, which may exclude pending failed writes.
Wait for downloads to finish before closing the dialog or starting recovery.
Raw export changes nothing. **Download restorable backup** packages a valid
home's files as `.kisak-home`, preserving original relative paths, byte counts
and SHA-256 checksums. It excludes imported installation files and empty
directories. **Restore selected backup** validates the entire destination set
and refuses any existing path, including identical files. Required parent
directories are recreated. For a raw export from an invalid home, **Restore
selected raw file** accepts one file and its explicit lower-case home-relative
path. Use a fresh browser profile if the original home is oversized or invalid;
keep its original data and exported files until recovery has been checked.

Restores first stage complete verified copies, then commit a recovery record
before adding files to the home. **Resume interrupted recovery**, or the next
engine mount, finishes a committed restore. Corrupt staging or conflicting
recovery records stop with a raw-exportable error. Closing the dialog cancels
before commit; after commit, publication finishes while retaining the writer
lease. Closing the browser leaves committed recovery resumable on restart.
Never clear the original home as a recovery step. Backup format/version errors,
path/count/size limits, corrupt payloads and overwrite conflicts fail clearly.
This is file recovery; Kisak remains responsible for save-format compatibility.

The 2026-09-06 synthetic production suite ran on Windows with Playwright 1.61.1
Chromium 149.0.7827.55. Same-origin/profile full browser restart tests cover
OPFS rename and restore recovery after journal publication, partial staged
writes and injected quota failure, followed by two clean reopens. The product
backup/raw-file reimport journey also survives full browser restart in the same
profile/origin while all external requests are blocked and loopback is allowed.
Separate synthetic packaging checks run the shipped server through replacement
and rollback at one loopback address. Interrupted extraction and a mismatched
source revision fail verification while preserving the old package. Actual
release-version rollback and cross-version campaign acceptance remain open.
These tests do not establish physical power-loss durability, branded Chrome/Edge
acceptance, or the full
owned-install → play/save → disconnected restart → Continue journey. That
manual journey and a qualified source/site package remain release prerequisites.
