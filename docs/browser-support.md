# Browser support policy

The current product target is a Chromium-class browser profile, determined by
features rather than the user-agent string. Startup requires WebAssembly,
WebGL2, dedicated Workers, a transferable OffscreenCanvas, IndexedDB, OPFS,
synchronous OPFS access from a Worker, Web Audio, and pointer lock. A
persistent-storage request is optional; the launcher warns when persistence is
not granted because the browser may evict imported local files.

The launcher checks this profile before creating the engine Worker or opening
the asset store. Missing requirements produce an explicit unsupported-browser
state and list the unavailable APIs. The synchronous OPFS check runs in a
short-lived, non-engine Worker because that API is Worker-scoped.

Chrome and Edge should only be called validated when the production browser
suite has passed in those branded channels. Firefox and Safari are not
currently declared supported; this is an untested status, not a claim that a
specific release can never satisfy the feature gate.
