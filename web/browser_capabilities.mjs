const capability = (label, status, available) => ({ label, available,
    status: available === null ? "not-checked" : available ? status : "unsupported" });

async function workerHasSyncAccess()
{
    const worker = new Worker(new URL("./capability_probe_worker.mjs", import.meta.url),
        { type: "module" });
    try {
        return await Promise.race([
            new Promise((resolve) => {
                worker.onmessage = (event) => resolve(event.data?.syncAccessHandle === true);
                worker.onerror = () => resolve(false);
            }),
            new Promise((resolve) => setTimeout(() => resolve(false), 3_000)),
        ]);
    } finally { worker.terminate(); }
}

export async function detectBrowserCapabilities()
{
    const canvas = document.createElement("canvas");
    let webgl2 = false;
    try { webgl2 = Boolean(canvas.getContext("webgl2")); } catch {}
    /** @type {[string, string, boolean][]} */
    const required = [
        ["wasm", "WebAssembly", typeof WebAssembly === "object"],
        ["webgl2", "WebGL 2", webgl2],
        ["worker", "Worker", typeof Worker === "function"],
        ["offscreenCanvas", "OffscreenCanvas", typeof OffscreenCanvas === "function" && typeof canvas.transferControlToOffscreen === "function"],
        ["indexedDb", "IndexedDB", typeof indexedDB === "object"],
        ["opfs", "OPFS", typeof navigator.storage?.getDirectory === "function"],
        ["webAudio", "Web Audio", typeof AudioContext === "function"],
        ["pointerLock", "pointer lock", typeof canvas.requestPointerLock === "function" && typeof document.exitPointerLock === "function"],
    ];
    const capabilities = Object.fromEntries(required.map(([name, label, available]) =>
        [name, capability(label, "required", available)]));
    let missingRequired = required.filter(([name]) =>
        capabilities[name].status === "unsupported").map(([name]) => name);
    capabilities.persistentStorage = capability("persistent storage", "optional",
        typeof navigator.storage?.persist === "function" && typeof navigator.storage?.persisted === "function");
    capabilities.opfsSyncAccess = capability("Worker OPFS sync access", "required",
        missingRequired.length ? null : await workerHasSyncAccess());
    if (capabilities.opfsSyncAccess.status === "unsupported") {
        missingRequired = [...missingRequired, "opfsSyncAccess"];
    }
    return { supported: missingRequired.length === 0, capabilities, missingRequired };
}
