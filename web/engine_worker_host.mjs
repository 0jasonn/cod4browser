import { WebAudioDriver } from "./web_audio_driver.mjs";
import {
    DEFAULT_REQUEST_TIMEOUT_MS,
    ENGINE_PROTOCOL_VERSION,
    EngineWorkerError,
    HOST_EVENTS,
    protocolError,
} from "./engine_protocol.mjs";

const EXPORTED_COMMANDS = [
    "_KisakWeb_StartArchiveJob", "_KisakWeb_CancelArchiveJob",
    "_KisakWeb_StartQcommonRuntime", "_KisakWeb_CancelQcommonRuntime",
    "_KisakWeb_StartRetailCensus", "_KisakWeb_CancelRetailCensus",
    "_KisakWeb_StartCanonicalDbRuntimeCheck",
    "_KisakWeb_SubmitCanonicalCommand",
    "_KisakWeb_TestAudioProxyPcm",
    "_KisakWeb_TestLoseWebGLContext", "_KisakWeb_TestRestoreWebGLContext",
    "_KisakWeb_TestSetAaSamples",
];
const ENGINE_FILESYSTEM_LOCK = "kisakcod-web-engine-filesystem-v1";
const HOME_WRITER_LOCK = "kisakcod-web-home-writer-v1";

export function createEngineWorkerHost(canvas, {
    onLog,
    onAbort,
    onAudioDiagnostic,
    requestTimeoutMs = DEFAULT_REQUEST_TIMEOUT_MS,
} = {})
{
    if (!canvas || typeof canvas.transferControlToOffscreen !== "function") {
        throw new Error("This browser does not support a Worker-owned OffscreenCanvas.");
    }
    const observeInput = globalThis.__KISAKCOD_WORKER_TEST_CONFIG__?.observeInput === true;
    const worker = new Worker(new URL("./engine_worker.mjs", import.meta.url), {
        type: "module",
        name: "kisakcod-engine",
    });
    const audioDriver = new WebAudioDriver({
        onDiagnostic: (message) => {
            onAudioDiagnostic?.(message);
            onLog?.(`[kisakcod-web] ${message}`, "warn");
        },
        onPlaybackStarted: (detail) => {
            globalThis.dispatchEvent(new CustomEvent("kisakcod:audio-playback", {
                detail: { ...detail, state: "started", source: "canonical-openal-web-audio" },
            }));
        },
    });
    // Gesture listeners only unlock the platform device. Keyboard/mouse
    // ownership remains in the existing input forwarding path below.
    audioDriver.attachGestureResume(canvas);
    const pending = new Map();
    let nextRequestId = 1;
    let releaseFilesystemLease = null;
    let filesystemLeaseCompletion = null;
    let releaseHomeWriterLease = null;
    let homeWriterLeaseCompletion = null;
    let unmounting = null;
    let filesystemMutation = Promise.resolve();
    let disposed = false;
    let shuttingDown = false;
    let disposePromise = null;
    let resolveReady;
    let rejectReady;
    const ready = new Promise((resolve, reject) => {
        resolveReady = resolve;
        rejectReady = reject;
    });

    function rejectPending(error)
    {
        const failure = error instanceof EngineWorkerError
            ? error
            : new EngineWorkerError(error);
        for (const request of pending.values()) {
            clearTimeout(request.timeout);
            request.signal?.removeEventListener("abort", request.abort);
            request.reject(failure);
        }
        pending.clear();
    }

    function allocateRequestId()
    {
        for (let attempts = 0; attempts <= 0xffff_ffff; ++attempts) {
            const id = nextRequestId;
            nextRequestId = nextRequestId === 0xffff_ffff ? 1 : nextRequestId + 1;
            if (!pending.has(id)) return id;
        }
        throw new EngineWorkerError(protocolError(
            "REQUEST_ID_EXHAUSTED", "request", "No Worker request IDs are available."));
    }

    function rpc(type, payload = {}, transfer = [], { signal, timeoutMs = requestTimeoutMs } = {})
    {
        if ((shuttingDown && type !== "shutdown") || disposed) {
            return Promise.reject(new EngineWorkerError(protocolError(
                "WORKER_SHUTTING_DOWN", type, "The engine Worker is shutting down.")));
        }
        if (!Number.isFinite(timeoutMs) || timeoutMs <= 0 || timeoutMs > 120_000) {
            return Promise.reject(new RangeError("Worker request timeout must be 1..120000 ms."));
        }
        if (signal?.aborted) {
            return Promise.reject(new DOMException("The Worker request was aborted.", "AbortError"));
        }
        const id = allocateRequestId();
        const promise = new Promise((resolve, reject) => {
            const abort = () => {
                const request = pending.get(id);
                if (!request) return;
                pending.delete(id);
                clearTimeout(request.timeout);
                reject(new DOMException("The Worker request was aborted.", "AbortError"));
            };
            const timeout = setTimeout(() => {
                const request = pending.get(id);
                if (!request) return;
                pending.delete(id);
                signal?.removeEventListener("abort", abort);
                reject(new EngineWorkerError(protocolError(
                    "REQUEST_TIMEOUT", type,
                    `The engine Worker did not answer within ${timeoutMs} ms.`, true)));
            }, timeoutMs);
            pending.set(id, { resolve, reject, timeout, signal, abort, operation: type });
            signal?.addEventListener("abort", abort, { once: true });
        });
        try {
            worker.postMessage({ protocolVersion: ENGINE_PROTOCOL_VERSION, type, id, ...payload }, transfer);
        } catch (error) {
            const request = pending.get(id);
            pending.delete(id);
            clearTimeout(request?.timeout);
            signal?.removeEventListener("abort", request?.abort);
            request?.reject(error);
        }
        return promise;
    }

    worker.addEventListener("message", (event) => {
        const message = event.data;
        if (["ready", "reply", "event", "startup-error"].includes(message?.type) &&
            message.protocolVersion !== ENGINE_PROTOCOL_VERSION) {
            const error = protocolError(
                "PROTOCOL_VERSION", "message",
                "The engine Worker returned an incompatible protocol version.");
            rejectReady(new EngineWorkerError(error));
            rejectPending(error);
            return;
        }
        switch (message?.type) {
        case "ready": resolveReady(message); break;
        case "reply": {
            const request = pending.get(message.id);
            if (!request) break;
            pending.delete(message.id);
            clearTimeout(request.timeout);
            request.signal?.removeEventListener("abort", request.abort);
            if (message.error) request.reject(new EngineWorkerError(message.error));
            else request.resolve(message.result);
            break;
        }
        case "event":
            if (!HOST_EVENTS.has(message.name)) {
                rejectPending(protocolError(
                    "EVENT_NOT_ALLOWED", "event", `Worker event is not allowed: ${message.name}.`));
                break;
            }
            globalThis.dispatchEvent(new CustomEvent(message.name, { detail: message.detail }));
            break;
        case "audio-command":
            audioDriver.handleCommand(message);
            break;
        case "cursor": {
            const visible = message.visible === true;
            canvas.style.cursor = visible ? "default" : "none";
            globalThis.dispatchEvent(new CustomEvent("kisakcod:cursor", {
                detail: { visible },
            }));
            break;
        }
        case "mouse-mode":
            globalThis.dispatchEvent(new CustomEvent("kisakcod:mouse-mode", {
                detail: { absolute: message.absolute === true },
            }));
            break;
        case "log": onLog?.(message.message, message.level); break;
        case "abort": onAbort?.(message.reason); break;
        case "startup-error": {
            const error = new EngineWorkerError(message.error);
            rejectReady(error);
            rejectPending(error);
            break;
        }
        default: break;
        }
    });
    worker.addEventListener("error", (event) => {
        const error = protocolError(
            "WORKER_ERROR", "worker", event.error?.message ?? event.message ?? "Worker failed.");
        rejectReady(new EngineWorkerError(error));
        rejectPending(error);
    });
    worker.addEventListener("messageerror", () => rejectPending(protocolError(
        "MESSAGE_ERROR", "message", "The engine Worker sent an unreadable message.")));

    const offscreen = canvas.transferControlToOffscreen();
    worker.postMessage({
        protocolVersion: ENGINE_PROTOCOL_VERSION,
        type: "init",
        canvas: offscreen,
        testConfig: globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ ?? null,
    }, [offscreen]);

    async function acquireFilesystemLease()
    {
        if (releaseFilesystemLease || !navigator.locks?.request) return;
        let markHomeAcquired;
        let releaseHome;
        const homeAcquired = new Promise((resolve) => { markHomeAcquired = resolve; });
        const homeHeld = new Promise((resolve) => { releaseHome = resolve; });
        homeWriterLeaseCompletion = navigator.locks.request(
            HOME_WRITER_LOCK,
            { mode: "exclusive", ifAvailable: true },
            async (lock) => {
                if (!lock) {
                    markHomeAcquired(false);
                    return;
                }
                releaseHomeWriterLease = releaseHome;
                markHomeAcquired(true);
                await homeHeld;
            },
        );
        if (!await homeAcquired) {
            await homeWriterLeaseCompletion;
            homeWriterLeaseCompletion = null;
            const error = new Error("Another tab owns the writable browser profile.");
            error.code = "HOME_WRITER_CONFLICT";
            throw error;
        }
        let markAcquired;
        let release;
        const acquired = new Promise((resolve) => { markAcquired = resolve; });
        const held = new Promise((resolve) => { release = resolve; });
        try {
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
        } catch (error) {
            releaseHomeWriterLease?.();
            await homeWriterLeaseCompletion;
            releaseHomeWriterLease = null;
            homeWriterLeaseCompletion = null;
            throw error;
        }
    }

    async function releaseLeases()
    {
        const release = releaseFilesystemLease;
        const completion = filesystemLeaseCompletion;
        const releaseHome = releaseHomeWriterLease;
        const homeCompletion = homeWriterLeaseCompletion;
        releaseFilesystemLease = null;
        filesystemLeaseCompletion = null;
        releaseHomeWriterLease = null;
        homeWriterLeaseCompletion = null;
        release?.();
        releaseHome?.();
        await Promise.all([completion, homeCompletion].filter(Boolean));
    }

    async function releaseMountedFilesystem()
    {
        if (unmounting) return unmounting;
        unmounting = (async () => {
            try {
                await rpc("unmount");
            } finally {
                await releaseLeases();
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
        resize(width, height) { return rpc("resize", { width, height }); },
        input(event) {
            if (observeInput) {
                // Boundary-only observability for browser input tests; canonical
                // CL_KeyEvent remains the sole consumer of gameplay input.
                globalThis.dispatchEvent(new CustomEvent("kisakcod:input", {
                    detail: { ...event },
                }));
            }
            return rpc("input", { event });
        },
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
        checkpoint() { return rpc("checkpoint"); },
        dispose() {
            if (disposePromise) return disposePromise;
            shuttingDown = true;
            disposePromise = (async () => {
                try {
                    await serializeFilesystemMutation(async () => {
                        await rpc("shutdown");
                        await releaseLeases();
                    });
                } finally {
                    disposed = true;
                    await releaseLeases();
                    audioDriver.dispose();
                    worker.terminate();
                    rejectPending(protocolError(
                        "WORKER_TERMINATED", "shutdown", "The engine Worker was terminated."));
                }
            })();
            return disposePromise;
        },
    };
    facade.audioDriver = audioDriver;
    for (const functionName of EXPORTED_COMMANDS) {
        facade[functionName] = (...arguments_) => facade.call(functionName, ...arguments_);
    }
    globalThis.addEventListener("pagehide", () => { void facade.dispose(); }, { once: true });
    return Object.freeze(facade);
}
