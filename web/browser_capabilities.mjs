const capability = (label, available, status = "required") => ({ label, available,
    status: available === null ? "not-checked" : available ? status : "unsupported" });

async function probeWorkerCapabilities(canvas)
{
    let offscreen;
    try {
        offscreen = canvas.transferControlToOffscreen();
    } catch {
        return { offscreenCanvas: false, syncAccessHandle: null };
    }
    const worker = new Worker(new URL("./capability_probe_worker.mjs", import.meta.url), {
        type: "module",
    });
    try {
        const reply = new Promise((resolve) => {
            worker.onmessage = ({ data }) => resolve({
                offscreenCanvas: data?.offscreenCanvas === true &&
                    data.canvas instanceof OffscreenCanvas,
                syncAccessHandle: data?.syncAccessHandle === true,
            });
            worker.onerror = () => resolve({
                offscreenCanvas: false,
                syncAccessHandle: false,
            });
        });
        try {
            worker.postMessage({ canvas: offscreen }, [offscreen]);
        } catch {
            return { offscreenCanvas: false, syncAccessHandle: null };
        }
        return await Promise.race([reply, new Promise((resolve) => setTimeout(resolve,
            3_000, { offscreenCanvas: false, syncAccessHandle: false }))]);
    } finally { worker.terminate(); }
}

export async function detectBrowserCapabilities()
{
    const canvas = document.createElement("canvas");
    let webgl2;
    try { webgl2 = Boolean(canvas.getContext("webgl2")); } catch { webgl2 = false; }
    const required = [
        ["wasm", "WebAssembly", typeof WebAssembly === "object"], ["webgl2", "WebGL 2", webgl2],
        ["worker", "Worker", typeof Worker === "function"], ["offscreenCanvas", "OffscreenCanvas", typeof OffscreenCanvas === "function" && typeof canvas.transferControlToOffscreen === "function"],
        ["indexedDb", "IndexedDB", typeof indexedDB === "object"], ["opfs", "OPFS", typeof navigator.storage?.getDirectory === "function"],
        ["webLocks", "Web Locks", typeof navigator.locks?.request === "function"],
        ["broadcastChannel", "BroadcastChannel", typeof BroadcastChannel === "function"],
        ["webAudio", "Web Audio", typeof AudioContext === "function"], ["pointerLock", "pointer lock", typeof canvas.requestPointerLock === "function" && typeof document.exitPointerLock === "function"],
    ];
    const capabilities = Object.fromEntries(required.map(([name, label, available]) =>
        [name, capability(label, available)]));
    const missing = required.filter(([, , available]) => !available).map(([name]) => name);
    capabilities.persistentStorage = capability("persistent storage",
        typeof navigator.storage?.persist === "function" &&
        typeof navigator.storage?.persisted === "function", "optional");
    const workerProbe = missing.length ? null
        : await probeWorkerCapabilities(document.createElement("canvas"));
    if (workerProbe && !workerProbe.offscreenCanvas) {
        capabilities.offscreenCanvas = capability("transferable OffscreenCanvas", false);
        missing.push("offscreenCanvas");
    }
    capabilities.opfsSyncAccess = capability("Worker OPFS sync access",
        workerProbe?.syncAccessHandle ?? null);
    if (capabilities.opfsSyncAccess.available === false) missing.push("opfsSyncAccess");
    return { supported: !missing.length, capabilities, missingRequired: missing };
}
