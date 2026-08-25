import { WebAudioDriver } from "./web_audio_driver.mjs";
import {
    DEFAULT_REQUEST_TIMEOUT_MS,
    ENGINE_PROTOCOL_VERSION,
    EngineWorkerError,
    MAX_REQUEST_TIMEOUT_MS,
    PRODUCT_HOST_EVENTS,
    protocolError,
} from "./product_protocol.mjs";

const ENGINE_FILESYSTEM_LOCK = "kisakcod-web-engine-filesystem-v1";
const HOME_WRITER_LOCK = "kisakcod-web-home-writer-v1";
const FILESYSTEM_OPERATION_TIMEOUT_MS = 60_000;

export const FILESYSTEM_STATES = Object.freeze({
    UNMOUNTED: "unmounted",
    ACQUIRING: "acquiring",
    MOUNTING: "mounting",
    MOUNTED: "mounted",
    FLUSHING: "flushing",
    FLUSH_FAILED_RETRYABLE: "flush-failed-retryable",
    UNKNOWN_AFTER_TIMEOUT: "unknown-after-timeout",
    TERMINATING: "terminating",
    TERMINATED: "terminated",
});

export function createEngineWorkerHost(canvas, {
    onLog,
    onAbort,
    requestTimeoutMs = DEFAULT_REQUEST_TIMEOUT_MS,
    mountTimeoutMs = FILESYSTEM_OPERATION_TIMEOUT_MS,
    flushTimeoutMs = FILESYSTEM_OPERATION_TIMEOUT_MS,
    managePageLifecycle = true,
    onFilesystemState,
    workerFactory = (url, options) => new Worker(url, options),
    audioDriverFactory = (options) => new WebAudioDriver(options),
    lockManager = navigator.locks,
} = {})
{
    if (!canvas || typeof canvas.transferControlToOffscreen !== "function") {
        throw new Error("This browser does not support a Worker-owned OffscreenCanvas.");
    }

    const worker = workerFactory(new URL("./engine_worker.mjs", import.meta.url), {
        type: "module",
        name: "kisakcod-engine",
    });
    const audioDriver = audioDriverFactory({
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
    let filesystemState = FILESYSTEM_STATES.UNMOUNTED;
    let workerGeneration = 1;
    let workerUnavailable = false;
    let recoveryPromise = null;
    let shuttingDown = false;
    let disposed = false;
    let disposePromise = null;
    let resolveReady;
    let rejectReady;
    const ready = new Promise((resolve, reject) => {
        resolveReady = resolve;
        rejectReady = reject;
    });

    function setFilesystemState(state)
    {
        filesystemState = state;
        onFilesystemState?.(state);
    }

    function validateFilesystemTimeout(timeoutMs, name)
    {
        if (!Number.isFinite(timeoutMs) || timeoutMs <= 0 ||
            timeoutMs > MAX_REQUEST_TIMEOUT_MS) {
            throw new RangeError(`${name} timeout must be 1..${MAX_REQUEST_TIMEOUT_MS} ms.`);
        }
    }
    validateFilesystemTimeout(mountTimeoutMs, "Mount");
    validateFilesystemTimeout(flushTimeoutMs, "Flush");

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
        if ((shuttingDown && type !== "shutdown") || disposed || workerUnavailable) {
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
            pending.set(id, {
                resolve, reject, timeout, signal, abort,
                generation: workerGeneration,
            });
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
            if (!request || request.generation !== workerGeneration) break;
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
        void recoverWorkerOwnership("worker-error", new EngineWorkerError(error));
    };
    const handleMessageError = () => {
        const error = protocolError(
            "MESSAGE_ERROR", "message", "The engine Worker sent an unreadable message.");
        rejectPending(error);
        void recoverWorkerOwnership("message-error", new EngineWorkerError(error));
    };
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
        if (releaseFilesystemLease || !lockManager?.request) return;

        let markHomeAcquired;
        let releaseHome;
        const homeAcquired = new Promise((resolve) => { markHomeAcquired = resolve; });
        const homeHeld = new Promise((resolve) => { releaseHome = resolve; });
        homeWriterLeaseCompletion = lockManager.request(
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
            filesystemLeaseCompletion = lockManager.request(
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

    function workerOwnershipUnknown(error)
    {
        return ["REQUEST_TIMEOUT", "WORKER_ERROR", "MESSAGE_ERROR", "WORKER_TERMINATED"]
            .includes(error?.code);
    }

    function recoverWorkerOwnership(operation, error)
    {
        if (recoveryPromise) return recoveryPromise;
        recoveryPromise = (async () => {
            if (error?.code === "REQUEST_TIMEOUT") {
                setFilesystemState(FILESYSTEM_STATES.UNKNOWN_AFTER_TIMEOUT);
            }
            setFilesystemState(FILESYSTEM_STATES.TERMINATING);
            workerUnavailable = true;
            ++workerGeneration;
            rejectPending(protocolError(
                "WORKER_TERMINATED", operation,
                "The engine Worker was terminated to end uncertain filesystem ownership."));
            worker.terminate();
            await releaseLeases();
            setFilesystemState(FILESYSTEM_STATES.TERMINATED);
        })();
        return recoveryPromise;
    }

    function serializeFilesystemMutation(callback)
    {
        const result = filesystemMutation.catch(() => {}).then(callback);
        filesystemMutation = result.catch(() => {});
        return result;
    }

    async function flushMountedFilesystem()
    {
        if (filesystemState === FILESYSTEM_STATES.UNMOUNTED) {
            await releaseLeases();
            return { mounted: false };
        }
        if (![FILESYSTEM_STATES.MOUNTED,
            FILESYSTEM_STATES.FLUSH_FAILED_RETRYABLE].includes(filesystemState)) {
            throw Object.assign(new Error(
                `Cannot flush the filesystem while it is ${filesystemState}.`), {
                code: "FILESYSTEM_STATE",
            });
        }
        setFilesystemState(FILESYSTEM_STATES.FLUSHING);
        try {
            const result = await rpc("flushAndUnmount", {}, [], {
                timeoutMs: flushTimeoutMs,
            });
            setFilesystemState(FILESYSTEM_STATES.UNMOUNTED);
            await releaseLeases();
            return result;
        } catch (error) {
            if (workerOwnershipUnknown(error)) {
                await recoverWorkerOwnership("flushAndUnmount", error);
            } else {
                setFilesystemState(FILESYSTEM_STATES.FLUSH_FAILED_RETRYABLE);
            }
            throw error;
        }
    }

    const facade = {
        ready,
        mountAssets(manifest, options) {
            return serializeFilesystemMutation(async () => {
                if (filesystemState !== FILESYSTEM_STATES.UNMOUNTED) {
                    await flushMountedFilesystem();
                }
                setFilesystemState(FILESYSTEM_STATES.ACQUIRING);
                try {
                    await acquireLeases();
                    setFilesystemState(FILESYSTEM_STATES.MOUNTING);
                    const result = await rpc("mountAssets", { manifest }, [], {
                        timeoutMs: options?.timeoutMs ?? mountTimeoutMs,
                    });
                    setFilesystemState(FILESYSTEM_STATES.MOUNTED);
                    return result;
                } catch (error) {
                    if (workerOwnershipUnknown(error)) {
                        await recoverWorkerOwnership("mountAssets", error);
                    } else {
                        setFilesystemState(FILESYSTEM_STATES.UNMOUNTED);
                        await releaseLeases();
                    }
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
        checkpoint(options) {
            return serializeFilesystemMutation(async () => {
                if (![FILESYSTEM_STATES.MOUNTED,
                    FILESYSTEM_STATES.FLUSH_FAILED_RETRYABLE].includes(filesystemState)) {
                    throw Object.assign(new Error("The writable browser profile is not mounted."), {
                        code: "FILESYSTEM_NOT_MOUNTED",
                    });
                }
                return rpc("checkpoint", {}, [], {
                    timeoutMs: options?.timeoutMs ?? flushTimeoutMs,
                });
            });
        },
        runtimeStatus(options) { return rpc("runtimeStatus", {}, [], options); },
        get filesystemState() { return filesystemState; },
        dispose() {
            if (disposePromise) return disposePromise;
            shuttingDown = true;
            disposePromise = (async () => {
                try {
                    await filesystemMutation.catch(() => {});
                    if (!workerUnavailable) {
                        await rpc("shutdown", {}, [], { timeoutMs: flushTimeoutMs });
                        setFilesystemState(FILESYSTEM_STATES.UNMOUNTED);
                    }
                } catch (error) {
                    await recoverWorkerOwnership("shutdown", error);
                } finally {
                    disposed = true;
                    if (!workerUnavailable) await releaseLeases();
                    if (managePageLifecycle) {
                        globalThis.removeEventListener("pagehide", handlePageHide);
                    }
                    worker.removeEventListener("message", handleMessage);
                    worker.removeEventListener("error", handleError);
                    worker.removeEventListener("messageerror", handleMessageError);
                    audioDriver.dispose();
                    rejectPending(protocolError(
                        "WORKER_TERMINATED", "shutdown", "The engine Worker was terminated."));
                    if (!workerUnavailable) {
                        workerUnavailable = true;
                        ++workerGeneration;
                        worker.terminate();
                        setFilesystemState(FILESYSTEM_STATES.TERMINATED);
                    }
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
