import { WebAudioDriver } from "./web_audio_driver.mjs";
import {
    DEFAULT_REQUEST_TIMEOUT_MS,
    ENGINE_PROTOCOL_VERSION,
    EngineWorkerError,
    MAX_REQUEST_TIMEOUT_MS,
    PRODUCT_HOST_EVENTS,
    protocolError,
} from "./engine_protocol.mjs";

const ENGINE_FILESYSTEM_LOCK = "kisakcod-web-engine-filesystem-v1";
const HOME_WRITER_LOCK = "kisakcod-web-home-writer-v1";

export function createEngineWorkerHost(canvas, {
    onLog,
    onAbort,
    requestTimeoutMs = DEFAULT_REQUEST_TIMEOUT_MS,
    managePageLifecycle = true,
} = {})
{
    if (!canvas || typeof canvas.transferControlToOffscreen !== "function") {
        throw new Error("This browser does not support a Worker-owned OffscreenCanvas.");
    }

    const worker = new Worker(new URL("./engine_worker.mjs", import.meta.url), {
        type: "module",
        name: "kisakcod-engine",
    });
    const audioDriver = new WebAudioDriver({
        onDiagnostic: (message) => onLog?.(`[kisakcod-web] ${message}`, "warn"),
    });
    audioDriver.attachGestureResume(canvas);

    const pending = new Map();
    let nextRequestId = 1;
    let releaseFilesystemLease = null;
    let filesystemLeaseCompletion = null;
    let releaseHomeWriterLease = null;
    let homeWriterLeaseCompletion = null;
    let filesystemMutation = Promise.resolve();
    let mounted = false;
    let shuttingDown = false;
    let disposed = false;
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

    function rpc(type, payload = {}, transfer = [], {
        timeoutMs = requestTimeoutMs,
        signal,
    } = {})
    {
        if ((shuttingDown && type !== "shutdown") || disposed) {
            return Promise.reject(new EngineWorkerError(protocolError(
                "WORKER_SHUTTING_DOWN", type, "The engine Worker is shutting down.")));
        }
        if (!Number.isFinite(timeoutMs) || timeoutMs <= 0 ||
            timeoutMs > MAX_REQUEST_TIMEOUT_MS) {
            return Promise.reject(new RangeError(
                `Worker request timeout must be 1..${MAX_REQUEST_TIMEOUT_MS} ms.`));
        }
        if (signal?.aborted) {
            return Promise.reject(new DOMException("The Worker request was aborted.", "AbortError"));
        }
        const id = allocateRequestId();
        const promise = new Promise((resolve, reject) => {
            const abort = () => {
                if (!pending.delete(id)) return;
                clearTimeout(timeout);
                reject(new DOMException("The Worker request was aborted.", "AbortError"));
            };
            const timeout = setTimeout(() => {
                if (!pending.delete(id)) return;
                signal?.removeEventListener("abort", abort);
                reject(new EngineWorkerError(protocolError(
                    "REQUEST_TIMEOUT", type,
                    `The engine Worker did not answer within ${timeoutMs} ms.`, true)));
            }, timeoutMs);
            pending.set(id, { resolve, reject, timeout, signal, abort });
            signal?.addEventListener("abort", abort, { once: true });
        });
        try {
            worker.postMessage({ protocolVersion: ENGINE_PROTOCOL_VERSION, type, id, ...payload }, transfer);
        } catch (error) {
            const request = pending.get(id);
            pending.delete(id);
            clearTimeout(request?.timeout);
            request?.signal?.removeEventListener("abort", request.abort);
            request?.reject(error);
        }
        return promise;
    }

    const handleMessage = (event) => {
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
            if (!PRODUCT_HOST_EVENTS.has(message.name)) {
                rejectPending(protocolError(
                    "EVENT_NOT_ALLOWED", "event", `Worker event is not allowed: ${message.name}.`));
                break;
            }
            globalThis.dispatchEvent(new CustomEvent(message.name, { detail: message.detail }));
            break;
        case "audio-command": audioDriver.handleCommand(message); break;
        case "cursor":
            canvas.style.cursor = message.visible === true ? "default" : "none";
            globalThis.dispatchEvent(new CustomEvent("kisakcod:cursor", {
                detail: { visible: message.visible === true },
            }));
            break;
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
    };
    const handleError = (event) => {
        const error = protocolError(
            "WORKER_ERROR", "worker", event.error?.message ?? event.message ?? "Worker failed.");
        rejectReady(new EngineWorkerError(error));
        rejectPending(error);
    };
    const handleMessageError = () => rejectPending(protocolError(
        "MESSAGE_ERROR", "message", "The engine Worker sent an unreadable message."));
    worker.addEventListener("message", handleMessage);
    worker.addEventListener("error", handleError);
    worker.addEventListener("messageerror", handleMessageError);

    const offscreen = canvas.transferControlToOffscreen();
    worker.postMessage({
        protocolVersion: ENGINE_PROTOCOL_VERSION,
        type: "init",
        canvas: offscreen,
    }, [offscreen]);

    async function acquireLeases()
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
            throw Object.assign(new Error("Another tab owns the writable browser profile."), {
                code: "HOME_WRITER_CONFLICT",
            });
        }

        let markFilesystemAcquired;
        let releaseFilesystem;
        const filesystemAcquired = new Promise((resolve) => { markFilesystemAcquired = resolve; });
        const filesystemHeld = new Promise((resolve) => { releaseFilesystem = resolve; });
        try {
            filesystemLeaseCompletion = navigator.locks.request(
                ENGINE_FILESYSTEM_LOCK,
                { mode: "shared" },
                async () => {
                    releaseFilesystemLease = releaseFilesystem;
                    markFilesystemAcquired();
                    await filesystemHeld;
                },
            );
            await filesystemAcquired;
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
        const completions = [filesystemLeaseCompletion, homeWriterLeaseCompletion]
            .filter(Boolean);
        const releases = [releaseFilesystemLease, releaseHomeWriterLease]
            .filter(Boolean);
        filesystemLeaseCompletion = null;
        homeWriterLeaseCompletion = null;
        releaseFilesystemLease = null;
        releaseHomeWriterLease = null;
        for (const release of releases) release();
        await Promise.all(completions);
    }

    function serializeFilesystemMutation(callback)
    {
        const result = filesystemMutation.catch(() => {}).then(callback);
        filesystemMutation = result.catch(() => {});
        return result;
    }

    async function flushMountedFilesystem()
    {
        if (!mounted) {
            await releaseLeases();
            return { mounted: false };
        }
        try {
            return await rpc("flushAndUnmount");
        } finally {
            mounted = false;
            await releaseLeases();
        }
    }

    const facade = {
        ready,
        mountAssets(manifest, options) {
            return serializeFilesystemMutation(async () => {
                await flushMountedFilesystem();
                await acquireLeases();
                try {
                    const result = await rpc("mountAssets", { manifest }, [], {
                        timeoutMs: options?.timeoutMs ?? requestTimeoutMs,
                    });
                    mounted = true;
                    return result;
                } catch (error) {
                    await releaseLeases();
                    throw error;
                }
            });
        },
        flushAndUnmount() {
            return serializeFilesystemMutation(flushMountedFilesystem);
        },
        probeAsset(kind, buffers, metadata = {}, options) {
            const transferred = buffers.map((bytes) => Uint8Array.from(bytes).buffer);
            return rpc("probeAsset", { kind, buffers: transferred, metadata },
                transferred, options);
        },
        submitCanonicalCommand(command, options) {
            return rpc("submitCanonicalCommand", { command }, [], {
                timeoutMs: options?.timeoutMs ?? requestTimeoutMs,
            });
        },
        resize(width, height, options) {
            return rpc("resize", { width, height }, [], {
                timeoutMs: options?.timeoutMs ?? requestTimeoutMs,
            });
        },
        input(event, options) {
            return rpc("input", { event }, [], {
                timeoutMs: options?.timeoutMs ?? requestTimeoutMs,
            });
        },
        runtimeStatus(options) { return rpc("runtimeStatus", {}, [], options); },
        dispose() {
            if (disposePromise) return disposePromise;
            shuttingDown = true;
            disposePromise = (async () => {
                try {
                    await filesystemMutation.catch(() => {});
                    await rpc("shutdown");
                } finally {
                    mounted = false;
                    disposed = true;
                    await releaseLeases();
                    if (managePageLifecycle) {
                        globalThis.removeEventListener("pagehide", handlePageHide);
                    }
                    worker.removeEventListener("message", handleMessage);
                    worker.removeEventListener("error", handleError);
                    worker.removeEventListener("messageerror", handleMessageError);
                    audioDriver.dispose();
                    rejectPending(protocolError(
                        "WORKER_TERMINATED", "shutdown", "The engine Worker was terminated."));
                    worker.terminate();
                }
            })();
            return disposePromise;
        },
    };
    const handlePageHide = () => { void facade.dispose(); };
    if (managePageLifecycle) {
        globalThis.addEventListener("pagehide", handlePageHide, { once: true });
    }
    return Object.freeze(facade);
}
