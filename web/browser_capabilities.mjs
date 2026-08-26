const PROBE_REPLY = "kisak-capability-probe-result";

const capability = (label, available, status = "required", probeFailure = null) => ({
    label,
    available,
    status: available === null ? "not-checked" : available ? status : "unsupported",
    ...(probeFailure ? {
        code: probeFailure.code,
        message: probeFailure.message,
        detail: probeFailure.detail,
    } : {}),
});

function failure(capabilityName, code, message, error = null)
{
    const detail = error
        ? `${typeof error?.name === "string" ? error.name : "Error"}: ${
            typeof error?.message === "string" ? error.message : String(error)}`
        : "";
    return { supported: false, capability: capabilityName, code, message, detail };
}

function failedProbe(result)
{
    return {
        offscreenCanvas: result.capability === "offscreenCanvas" ? false : null,
        syncAccessHandle: result.capability === "opfsSyncAccessHandle" ? false : null,
        failure: result,
    };
}

function validCapabilityResult(value, expectedCapability)
{
    return value && typeof value === "object" &&
        typeof value.supported === "boolean" &&
        value.capability === expectedCapability &&
        typeof value.code === "string" &&
        typeof value.message === "string" &&
        typeof value.detail === "string";
}

export async function probeWorkerCapabilities(canvas, {
    WorkerConstructor = globalThis.Worker,
    setTimeoutFn = globalThis.setTimeout,
    clearTimeoutFn = globalThis.clearTimeout,
    timeoutMs = 3_000,
} = {})
{
    let offscreen;
    try {
        offscreen = canvas.transferControlToOffscreen();
    } catch (error) {
        return failedProbe(failure("offscreenCanvas", "OFFSCREEN_CANVAS_TRANSFER_FAILED",
            "Transferring an OffscreenCanvas to the capability Worker failed.", error));
    }

    let worker;
    try {
        worker = new WorkerConstructor(
            new URL("./capability_probe_worker.mjs", import.meta.url), { type: "module" });
    } catch (error) {
        return failedProbe(failure("worker", "WORKER_CONSTRUCTION_FAILED",
            "Constructing the browser capability Worker failed.", error));
    }

    try {
        return await new Promise((resolve) => {
            let settled = false;
            let timeout = null;
            const finish = (result) => {
                if (settled) return;
                settled = true;
                if (timeout !== null) clearTimeoutFn(timeout);
                resolve(result);
            };
            timeout = setTimeoutFn(() => finish(failedProbe(failure("worker",
                "CAPABILITY_PROBE_TIMEOUT",
                "The browser capability Worker did not respond in time."))), timeoutMs);
            worker.onmessage = ({ data }) => {
                if (data?.type !== PROBE_REPLY ||
                    !validCapabilityResult(data.offscreenCanvas, "offscreenCanvas") ||
                    !validCapabilityResult(data.opfsSyncAccess, "opfsSyncAccessHandle")) {
                    finish(failedProbe(failure("worker", "MALFORMED_PROBE_REPLY",
                        "The browser capability Worker returned malformed data.")));
                    return;
                }
                const probeFailure = !data.offscreenCanvas.supported
                    ? data.offscreenCanvas
                    : !data.opfsSyncAccess.supported ? data.opfsSyncAccess : null;
                finish({
                    offscreenCanvas: data.offscreenCanvas.supported,
                    syncAccessHandle: data.opfsSyncAccess.supported,
                    failure: probeFailure,
                });
            };
            worker.onerror = (event) => finish(failedProbe(failure("worker",
                "CAPABILITY_WORKER_FAILED",
                "The browser capability Worker failed.", event?.error)));
            try {
                worker.postMessage({ canvas: offscreen }, [offscreen]);
            } catch (error) {
                finish(failedProbe(failure("offscreenCanvas",
                    "OFFSCREEN_CANVAS_TRANSFER_FAILED",
                    "Posting the OffscreenCanvas to the capability Worker failed.", error)));
            }
        });
    } finally {
        try { worker.terminate(); } catch { /* The probe result is already authoritative. */ }
    }
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
    if (workerProbe?.failure?.capability === "worker") {
        capabilities.worker = capability("Worker", false, "required", workerProbe.failure);
        if (!missing.includes("worker")) missing.push("worker");
    }
    if (workerProbe && !workerProbe.offscreenCanvas &&
        workerProbe.failure?.capability === "offscreenCanvas") {
        capabilities.offscreenCanvas = capability("transferable OffscreenCanvas", false,
            "required", workerProbe.failure);
        if (!missing.includes("offscreenCanvas")) missing.push("offscreenCanvas");
    }
    capabilities.opfsSyncAccess = capability("Worker OPFS sync access",
        workerProbe?.syncAccessHandle ?? null, "required",
        workerProbe?.failure?.capability === "opfsSyncAccessHandle"
            ? workerProbe.failure : null);
    if (capabilities.opfsSyncAccess.available === false) missing.push("opfsSyncAccess");
    return {
        supported: !missing.length,
        capabilities,
        missingRequired: missing,
        probeFailure: workerProbe?.failure ?? null,
    };
}
