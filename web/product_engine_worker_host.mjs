import {
    createFilesystemLeases,
    createRequestIdAllocator,
    rejectWorkerRequests,
    settleWorkerReply,
} from "./worker_transport.mjs";
import { WebAudioDriver } from "./web_audio_driver.mjs";
import {
    DEFAULT_REQUEST_TIMEOUT_MS,
    ENGINE_PROTOCOL_VERSION,
    EngineWorkerError,
    MAX_REQUEST_TIMEOUT_MS,
    PRODUCT_HOST_EVENTS,
    protocolError,
} from "./product_protocol.mjs";

const FILESYSTEM_STALL_TIMEOUT_MS = 30_000;
const FILESYSTEM_ABSOLUTE_TIMEOUT_MS = 5 * 60_000;
const MAX_FILESYSTEM_TIMEOUT_MS = 10 * 60_000;

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
    onLog = undefined,
    onAbort = undefined,
    requestTimeoutMs = DEFAULT_REQUEST_TIMEOUT_MS,
    filesystemStallTimeoutMs = FILESYSTEM_STALL_TIMEOUT_MS,
    filesystemAbsoluteTimeoutMs = FILESYSTEM_ABSOLUTE_TIMEOUT_MS,
    mountTimeoutMs = undefined,
    flushTimeoutMs = undefined,
    managePageLifecycle = true,
    onFilesystemState = undefined,
    onFilesystemProgress = undefined,
    onFilesystemDirty = undefined,
    onFilesystemLifecycleEvent = undefined,
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
    const allocateRequestId = createRequestIdAllocator(pending);
    const leases = createFilesystemLeases(lockManager, emitFilesystemLifecycle);
    let filesystemMutation = Promise.resolve();
    let checkpointPromise = null;
    let checkpointQueued = false;
    /** @type {string} */
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

    function emitFilesystemLifecycle(event)
    {
        try {
            onFilesystemLifecycleEvent?.(event);
        } catch (error) {
            onLog?.(`[kisakcod-web] Filesystem lifecycle observer failed: ${error}`, "warn");
        }
    }

    function validateFilesystemTimeout(timeoutMs, name)
    {
        if (!Number.isFinite(timeoutMs) || timeoutMs <= 0 ||
            timeoutMs > MAX_FILESYSTEM_TIMEOUT_MS) {
            throw new RangeError(`${name} timeout must be 1..${MAX_FILESYSTEM_TIMEOUT_MS} ms.`);
        }
    }
    validateFilesystemTimeout(filesystemStallTimeoutMs, "Filesystem stall");
    validateFilesystemTimeout(filesystemAbsoluteTimeoutMs, "Filesystem absolute");
    if (filesystemAbsoluteTimeoutMs < filesystemStallTimeoutMs) {
        throw new RangeError("Filesystem absolute timeout must be at least the stall timeout.");
    }
    if (mountTimeoutMs !== undefined) validateFilesystemTimeout(mountTimeoutMs, "Mount stall");
    if (flushTimeoutMs !== undefined) validateFilesystemTimeout(flushTimeoutMs, "Flush stall");

    function clearRequestTimers(request)
    {
        clearTimeout(request.timeout);
        clearTimeout(request.absoluteTimeout);
    }


    /**
     * @param {string} type
     * @param {object} [payload]
     * @param {Transferable[]} [transfer]
     * @param {{timeoutMs?: number, signal?: AbortSignal,
     *   stallTimeoutMs?: number, absoluteTimeoutMs?: number}} [options]
     */
    function rpc(type, payload = {}, transfer = [], {
        timeoutMs = requestTimeoutMs,
        signal,
        stallTimeoutMs,
        absoluteTimeoutMs,
    } = {})
    {
        if ((shuttingDown && type !== "shutdown") || disposed || workerUnavailable) {
            return Promise.reject(new EngineWorkerError(protocolError(
                "WORKER_SHUTTING_DOWN", type, "The engine Worker is shutting down.")));
        }
        const usesProgressWatchdog = stallTimeoutMs !== undefined;
        if (!usesProgressWatchdog && (!Number.isFinite(timeoutMs) || timeoutMs <= 0 ||
            timeoutMs > MAX_REQUEST_TIMEOUT_MS)) {
            return Promise.reject(new RangeError(
                `Worker request timeout must be 1..${MAX_REQUEST_TIMEOUT_MS} ms.`));
        }
        if (usesProgressWatchdog) {
            try {
                validateFilesystemTimeout(stallTimeoutMs, "Filesystem stall");
                validateFilesystemTimeout(absoluteTimeoutMs, "Filesystem absolute");
            } catch (error) {
                return Promise.reject(error);
            }
            if (absoluteTimeoutMs < stallTimeoutMs) {
                return Promise.reject(new RangeError(
                    "Filesystem absolute timeout must be at least the stall timeout."));
            }
        }
        if (signal?.aborted) {
            return Promise.reject(new DOMException("The Worker request was aborted.", "AbortError"));
        }
        const id = allocateRequestId();
        const promise = new Promise((resolve, reject) => {
            const expire = (message) => {
                if (!pending.delete(id)) return;
                clearRequestTimers(request);
                signal?.removeEventListener("abort", abort);
                reject(new EngineWorkerError(protocolError(
                    "REQUEST_TIMEOUT", type, message, true)));
            };
            const abort = () => {
                if (!pending.delete(id)) return;
                clearRequestTimers(request);
                reject(new DOMException("The Worker request was aborted.", "AbortError"));
            };
            const request = {
                resolve, reject, timeout: null, signal, abort,
                absoluteTimeout: null,
                stallTimeoutMs,
                generation: workerGeneration,
            };
            request.timeout = setTimeout(() => expire(usesProgressWatchdog
                ? `The engine Worker made no filesystem progress for ${stallTimeoutMs} ms.`
                : `The engine Worker did not answer within ${timeoutMs} ms.`),
            usesProgressWatchdog ? stallTimeoutMs : timeoutMs);
            if (usesProgressWatchdog) {
                request.absoluteTimeout = setTimeout(() => expire(
                    `The filesystem operation exceeded its ${absoluteTimeoutMs} ms absolute limit.`),
                absoluteTimeoutMs);
            }
            pending.set(id, request);
            signal?.addEventListener("abort", abort, { once: true });
        });
        try {
            worker.postMessage({ protocolVersion: ENGINE_PROTOCOL_VERSION, type, id, ...payload }, transfer);
        } catch (error) {
            const request = pending.get(id);
            pending.delete(id);
            if (request) clearRequestTimers(request);
            request?.signal?.removeEventListener("abort", request.abort);
            request?.reject(error);
        }
        return promise;
    }

    const handleMessage = (event) => {
        const message = event.data;
        if (["ready", "reply", "event", "startup-error", "filesystem-progress",
            "filesystem-dirty"].includes(message?.type) &&
            message.protocolVersion !== ENGINE_PROTOCOL_VERSION) {
            const error = protocolError(
                "PROTOCOL_VERSION", "message",
                "The engine Worker returned an incompatible protocol version.");
            rejectReady(new EngineWorkerError(error));
            rejectWorkerRequests(pending, error);
            return;
        }
        switch (message?.type) {
        case "ready": resolveReady(message); break;
        case "reply":
            settleWorkerReply(pending, message, workerGeneration);
            break;
        case "filesystem-progress": {
            const request = pending.get(message.id);
            if (!request || request.generation !== workerGeneration ||
                request.stallTimeoutMs === undefined) break;
            clearTimeout(request.timeout);
            request.timeout = setTimeout(() => {
                if (!pending.delete(message.id)) return;
                clearRequestTimers(request);
                request.signal?.removeEventListener("abort", request.abort);
                request.reject(new EngineWorkerError(protocolError(
                    "REQUEST_TIMEOUT", message.operation,
                    `The engine Worker made no filesystem progress for ${request.stallTimeoutMs} ms.`,
                    true)));
            }, request.stallTimeoutMs);
            onFilesystemProgress?.(message.progress);
            break;
        }
        case "filesystem-dirty":
            if (!workerUnavailable) onFilesystemDirty?.();
            break;
        case "event":
            if (!PRODUCT_HOST_EVENTS.has(message.name)) {
                rejectWorkerRequests(pending, protocolError(
                    "EVENT_NOT_ALLOWED", "event", `Worker event is not allowed: ${message.name}.`));
                break;
            }
            if (message.name === "kisakcod:state" && message.detail?.state === "quitting") {
                audioDriver.dispose();
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
            rejectWorkerRequests(pending, error);
            break;
        }
        default: break;
        }
    };
    const handleError = (event) => {
        const error = protocolError(
            "WORKER_ERROR", "worker", event.error?.message ?? event.message ?? "Worker failed.");
        rejectReady(new EngineWorkerError(error));
        rejectWorkerRequests(pending, error);
        void recoverWorkerOwnership("worker-error", new EngineWorkerError(error));
    };
    const handleMessageError = () => {
        const error = protocolError(
            "MESSAGE_ERROR", "message", "The engine Worker sent an unreadable message.");
        rejectWorkerRequests(pending, error);
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

    function workerOwnershipUnknown(error)
    {
        return [
            "REQUEST_TIMEOUT", "WORKER_ERROR", "MESSAGE_ERROR", "WORKER_TERMINATED",
            "MOUNT_CLEANUP_FAILED", "FILESYSTEM_OWNERSHIP_UNKNOWN",
        ].includes(error?.code);
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
            rejectWorkerRequests(pending, protocolError(
                "WORKER_TERMINATED", operation,
                "The engine Worker was terminated to end uncertain filesystem ownership."));
            emitFilesystemLifecycle("workerTerminationStarted");
            worker.terminate();
            emitFilesystemLifecycle("workerTerminated");
            await leases.release();
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
            await leases.release();
            return { mounted: false };
        }
        if (filesystemState !== FILESYSTEM_STATES.MOUNTED &&
            filesystemState !== FILESYSTEM_STATES.FLUSH_FAILED_RETRYABLE) {
            throw Object.assign(new Error(
                `Cannot flush the filesystem while it is ${filesystemState}.`), {
                code: "FILESYSTEM_STATE",
            });
        }
        setFilesystemState(FILESYSTEM_STATES.FLUSHING);
        try {
            const result = await rpc("flushAndUnmount", {}, [], {
                stallTimeoutMs: flushTimeoutMs ?? filesystemStallTimeoutMs,
                absoluteTimeoutMs: filesystemAbsoluteTimeoutMs,
            });
            setFilesystemState(FILESYSTEM_STATES.UNMOUNTED);
            await leases.release();
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
                let mountRequestStarted = false;
                try {
                    await leases.acquire();
                    setFilesystemState(FILESYSTEM_STATES.MOUNTING);
                    mountRequestStarted = true;
                    const result = await rpc("mountAssets", { manifest }, [], {
                        stallTimeoutMs: options?.stallTimeoutMs ?? options?.timeoutMs ??
                            mountTimeoutMs ?? filesystemStallTimeoutMs,
                        absoluteTimeoutMs: options?.absoluteTimeoutMs ??
                            filesystemAbsoluteTimeoutMs,
                    });
                    setFilesystemState(FILESYSTEM_STATES.MOUNTED);
                    return result;
                } catch (error) {
                    if (mountRequestStarted && error?.code !== "MOUNT_FAILED_CLEAN") {
                        await recoverWorkerOwnership("mountAssets", error);
                    } else {
                        setFilesystemState(FILESYSTEM_STATES.UNMOUNTED);
                        await leases.release();
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
            if (options?.signal?.aborted) {
                return Promise.reject(new DOMException(
                    "The input event was aborted.", "AbortError"));
            }
            if (shuttingDown || disposed || workerUnavailable) {
                return Promise.reject(new EngineWorkerError(protocolError(
                    "WORKER_SHUTTING_DOWN", "input-event",
                    "The engine Worker is shutting down.")));
            }
            try {
                worker.postMessage({
                    protocolVersion: ENGINE_PROTOCOL_VERSION,
                    type: "input-event",
                    event,
                });
                return Promise.resolve(true);
            } catch (error) {
                return Promise.reject(error);
            }
        },
        checkpoint(options) {
            if (checkpointPromise) {
                checkpointQueued = true;
                return checkpointPromise;
            }
            const operation = serializeFilesystemMutation(async () => {
                if (filesystemState !== FILESYSTEM_STATES.MOUNTED &&
                    filesystemState !== FILESYSTEM_STATES.FLUSH_FAILED_RETRYABLE) {
                    throw Object.assign(new Error("The writable browser profile is not mounted."), {
                        code: "FILESYSTEM_NOT_MOUNTED",
                    });
                }
                let result;
                do {
                    checkpointQueued = false;
                    try {
                        result = await rpc("checkpoint", {}, [], {
                            stallTimeoutMs: options?.stallTimeoutMs ?? options?.timeoutMs ??
                                flushTimeoutMs ?? filesystemStallTimeoutMs,
                            absoluteTimeoutMs: options?.absoluteTimeoutMs ??
                                filesystemAbsoluteTimeoutMs,
                        });
                    } catch (error) {
                        if (workerOwnershipUnknown(error)) {
                            await recoverWorkerOwnership("checkpoint", error);
                        }
                        throw error;
                    }
                } while (checkpointQueued);
                return result;
            });
            checkpointPromise = operation;
            operation.finally(() => {
                if (checkpointPromise === operation) checkpointPromise = null;
            }).catch(() => {});
            return operation;
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
                        await rpc("shutdown", {}, [], {
                            stallTimeoutMs: flushTimeoutMs ?? filesystemStallTimeoutMs,
                            absoluteTimeoutMs: filesystemAbsoluteTimeoutMs,
                        });
                        setFilesystemState(FILESYSTEM_STATES.UNMOUNTED);
                    }
                } catch (error) {
                    await recoverWorkerOwnership("shutdown", error);
                } finally {
                    disposed = true;
                    if (!workerUnavailable) await leases.release();
                    if (managePageLifecycle) {
                        globalThis.removeEventListener("pagehide", handlePageHide);
                    }
                    worker.removeEventListener("message", handleMessage);
                    worker.removeEventListener("error", handleError);
                    worker.removeEventListener("messageerror", handleMessageError);
                    audioDriver.dispose();
                    rejectWorkerRequests(pending, protocolError(
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
