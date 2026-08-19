const EXPORTED_COMMANDS = [
    "_KisakWeb_StartArchiveJob", "_KisakWeb_CancelArchiveJob",
    "_KisakWeb_StartQcommonRuntime", "_KisakWeb_CancelQcommonRuntime",
    "_KisakWeb_StartRetailCensus", "_KisakWeb_CancelRetailCensus",
    "_KisakWeb_StartCanonicalDbRuntimeCheck",
    "_KisakWeb_SubmitCanonicalCommand",
    "_KisakWeb_TestLoseWebGLContext", "_KisakWeb_TestRestoreWebGLContext",
];
const ENGINE_FILESYSTEM_LOCK = "kisakcod-web-engine-filesystem-v1";

export function createEngineWorkerHost(canvas, { onLog, onAbort } = {})
{
    if (!canvas || typeof canvas.transferControlToOffscreen !== "function") {
        throw new Error("This browser does not support a Worker-owned OffscreenCanvas.");
    }
    const worker = new Worker(new URL("./engine_worker.mjs", import.meta.url), {
        type: "module",
        name: "kisakcod-engine",
    });
    const pending = new Map();
    let nextRequestId = 1;
    let releaseFilesystemLease = null;
    let filesystemLeaseCompletion = null;
    let unmounting = null;
    let filesystemMutation = Promise.resolve();
    let resolveReady;
    let rejectReady;
    const ready = new Promise((resolve, reject) => {
        resolveReady = resolve;
        rejectReady = reject;
    });

    function rpc(type, payload = {}, transfer = [])
    {
        const id = nextRequestId++;
        const promise = new Promise((resolve, reject) => pending.set(id, { resolve, reject }));
        worker.postMessage({ type, id, ...payload }, transfer);
        return promise;
    }

    worker.addEventListener("message", (event) => {
        const message = event.data;
        switch (message?.type) {
        case "ready": resolveReady(); break;
        case "reply": {
            const request = pending.get(message.id);
            if (!request) break;
            pending.delete(message.id);
            if (message.error) request.reject(new Error(message.error));
            else request.resolve(message.result);
            break;
        }
        case "event":
            globalThis.dispatchEvent(new CustomEvent(message.name, { detail: message.detail }));
            break;
        case "cursor":
            canvas.style.cursor = message.visible ? "default" : "none";
            break;
        case "log": onLog?.(message.message, message.level); break;
        case "abort": onAbort?.(message.reason); break;
        case "startup-error": rejectReady(new Error(message.error)); break;
        default: break;
        }
    });
    worker.addEventListener("error", (event) => {
        rejectReady(event.error ?? new Error(event.message));
    });

    const offscreen = canvas.transferControlToOffscreen();
    worker.postMessage({
        type: "init",
        canvas: offscreen,
        testConfig: globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ ?? null,
    }, [offscreen]);

    async function acquireFilesystemLease()
    {
        if (releaseFilesystemLease || !navigator.locks?.request) return;
        let markAcquired;
        let release;
        const acquired = new Promise((resolve) => { markAcquired = resolve; });
        const held = new Promise((resolve) => { release = resolve; });
        filesystemLeaseCompletion = navigator.locks.request(
            ENGINE_FILESYSTEM_LOCK,
            { mode: "shared" },
            async () => {
                releaseFilesystemLease = release;
                markAcquired();
                await held;
            },
        );
        await acquired;
    }

    async function releaseMountedFilesystem()
    {
        if (unmounting) return unmounting;
        unmounting = (async () => {
            try {
                await rpc("unmount");
            } finally {
                const release = releaseFilesystemLease;
                const completion = filesystemLeaseCompletion;
                releaseFilesystemLease = null;
                filesystemLeaseCompletion = null;
                release?.();
                await completion?.catch(() => {});
            }
        })();
        try {
            return await unmounting;
        } finally {
            unmounting = null;
        }
    }

    function serializeFilesystemMutation(callback)
    {
        const result = filesystemMutation.catch(() => {}).then(callback);
        filesystemMutation = result.catch(() => {});
        return result;
    }

    const facade = {
        ready,
        async mount(manifest) {
            return serializeFilesystemMutation(async () => {
                await releaseMountedFilesystem();
                await acquireFilesystemLease();
                try {
                    return await rpc("mount", { manifest });
                } catch (error) {
                    await releaseMountedFilesystem();
                    throw error;
                }
            });
        },
        unmount() {
            return serializeFilesystemMutation(releaseMountedFilesystem);
        },
        invalidate() {
            return serializeFilesystemMutation(releaseMountedFilesystem);
        },
        resize(width, height) { worker.postMessage({ type: "resize", width, height }); },
        input(event) { worker.postMessage({ type: "input", event }); },
        callProbe(functionName, buffers, argumentLayout) {
            const transferred = buffers.map((bytes) => {
                const copy = Uint8Array.from(bytes);
                return copy.buffer;
            });
            return rpc("probe", { functionName, buffers: transferred, argumentLayout }, transferred);
        },
        call(functionName, ...arguments_) {
            return rpc("call", { functionName, arguments: arguments_ });
        },
        testControl(values) { return rpc("test-control", { values }); },
        dispose() {
            worker.terminate();
            releaseFilesystemLease?.();
            releaseFilesystemLease = null;
            for (const request of pending.values()) {
                request.reject(new Error("The engine Worker was terminated."));
            }
            pending.clear();
        },
    };
    for (const functionName of EXPORTED_COMMANDS) {
        facade[functionName] = (...arguments_) => {
            void facade.call(functionName, ...arguments_).catch((error) => onLog?.(
                `[kisakcod-web] Worker command ${functionName} failed: ${error.message}`,
                "error",
            ));
        };
    }
    return Object.freeze(facade);
}
