import {
    createRequestIdAllocator,
    rejectWorkerRequests,
    settleWorkerReply,
} from "./worker_transport.mjs";
import { WebAudioDriver } from "./web_audio_driver.mjs";
import {
    DEFAULT_REQUEST_TIMEOUT_MS,
    ENGINE_PROTOCOL_VERSION,
    EngineWorkerError,
    HOST_EVENTS,
    protocolError,
} from "./engine_protocol.mjs";

const EXPORTED_COMMANDS = [
    "_KisakWeb_SubmitCanonicalCommand",
    "_KisakWeb_DiagnosticCinematicOmission",
    "_KisakWeb_TestAudioProxyPcm",
    "_KisakWeb_TestSlowNextCommand",
    "_KisakWeb_TestLoseWebGLContext", "_KisakWeb_TestRestoreWebGLContext",
    "_KisakWeb_TestSetAaSamples", "_KisakWeb_TestSubmitSurface",
    "_KisakWeb_TestUnloadWorldResources", "_KisakWeb_TestHeapBytes",
    "_KisakWeb_TestEmitRendererMemory", "_KisakWeb_TestUsingAds",
];
const ENGINE_FILESYSTEM_LOCK = "kisakcod-web-engine-filesystem-v1";
const HOME_WRITER_LOCK = "kisakcod-web-home-writer-v1";

export const DIAGNOSTIC_FILESYSTEM_STATES = Object.freeze({
    UNMOUNTED: "unmounted",
    ACQUIRING: "acquiring",
    MOUNTING: "mounting",
    MOUNTED: "mounted",
    FLUSHING: "flushing",
    UNKNOWN: "ownership-unknown",
    TERMINATING: "terminating",
    TERMINATED: "terminated",
});

export function createEngineWorkerHost(canvas, {
    onLog,
    onAbort,
    onAudioDiagnostic,
    requestTimeoutMs = DEFAULT_REQUEST_TIMEOUT_MS,
    managePageLifecycle = true,
    onFilesystemState,
    onFilesystemLifecycleEvent,
    workerFactory = (url, options) => new Worker(url, options),
    audioDriverFactory = (options) => new WebAudioDriver(options),
    lockManager = navigator.locks,
} = {})
{
    if (!canvas || typeof canvas.transferControlToOffscreen !== "function") {
        throw new Error("This browser does not support a Worker-owned OffscreenCanvas.");
    }
    const observeInput = globalThis.__KISAKCOD_WORKER_TEST_CONFIG__?.observeInput === true;
    const worker = workerFactory(new URL("./engine_worker.mjs", import.meta.url), {
        type: "module",
        name: "kisakcod-engine",
    });
    const audioDriver = audioDriverFactory({
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
    const allocateRequestId = createRequestIdAllocator(pending);
    let releaseFilesystemLease = null;
    let filesystemLeaseCompletion = null;
    let releaseHomeWriterLease = null;
    let homeWriterLeaseCompletion = null;
    let unmounting = null;
    let filesystemMutation = Promise.resolve();
    let filesystemState = DIAGNOSTIC_FILESYSTEM_STATES.UNMOUNTED;
    let workerGeneration = 1;
    let workerUnavailable = false;
    let recoveryPromise = null;
    let disposed = false;
    let shuttingDown = false;
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


    function rpc(type, payload = {}, transfer = [], { signal, timeoutMs = requestTimeoutMs } = {})
    {
        if ((shuttingDown && type !== "shutdown") || disposed || workerUnavailable) {
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
            pending.set(id, {
                resolve, reject, timeout, signal, abort, operation: type,
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
            signal?.removeEventListener("abort", request?.abort);
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
            rejectWorkerRequests(pending, error);
            return;
        }
        switch (message?.type) {
        case "ready": resolveReady(message); break;
        case "reply":
            settleWorkerReply(pending, message, workerGeneration);
            break;
        case "event":
            if (!HOST_EVENTS.has(message.name)) {
                rejectWorkerRequests(pending, protocolError(
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
        testConfig: globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ ?? null,
    }, [offscreen]);

    async function acquireFilesystemLease()
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
                emitFilesystemLifecycle("writerLeaseAcquired");
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
            filesystemLeaseCompletion = lockManager.request(
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
        const releasedWriter = Boolean(releaseHomeWriterLease);
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
        if (releasedWriter) emitFilesystemLifecycle("writerLeaseReleased");
    }

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
            setFilesystemState(DIAGNOSTIC_FILESYSTEM_STATES.UNKNOWN);
            setFilesystemState(DIAGNOSTIC_FILESYSTEM_STATES.TERMINATING);
            workerUnavailable = true;
            ++workerGeneration;
            rejectWorkerRequests(pending, protocolError(
                "WORKER_TERMINATED", operation,
                `The diagnostic Worker was terminated after ${error?.code ?? "uncertain ownership"}.`));
            emitFilesystemLifecycle("workerTerminationStarted");
            worker.terminate();
            emitFilesystemLifecycle("workerTerminated");
            await releaseLeases();
            setFilesystemState(DIAGNOSTIC_FILESYSTEM_STATES.TERMINATED);
        })();
        return recoveryPromise;
    }

    async function releaseMountedFilesystem()
    {
        if (unmounting) return unmounting;
        if (filesystemState === DIAGNOSTIC_FILESYSTEM_STATES.UNMOUNTED) {
            await releaseLeases();
            return { mounted: false };
        }
        if (filesystemState !== DIAGNOSTIC_FILESYSTEM_STATES.MOUNTED) {
            throw Object.assign(new Error(
                `Cannot unmount the diagnostic filesystem while it is ${filesystemState}.`), {
                code: "FILESYSTEM_STATE",
            });
        }
        unmounting = (async () => {
            setFilesystemState(DIAGNOSTIC_FILESYSTEM_STATES.FLUSHING);
            try {
                const result = await rpc("unmount");
                setFilesystemState(DIAGNOSTIC_FILESYSTEM_STATES.UNMOUNTED);
                await releaseLeases();
                return result;
            } catch (error) {
                await recoverWorkerOwnership("unmount", error);
                throw error;
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
                if (filesystemState !== DIAGNOSTIC_FILESYSTEM_STATES.UNMOUNTED) {
                    await releaseMountedFilesystem();
                }
                setFilesystemState(DIAGNOSTIC_FILESYSTEM_STATES.ACQUIRING);
                let mountRequestStarted = false;
                try {
                    await acquireFilesystemLease();
                    setFilesystemState(DIAGNOSTIC_FILESYSTEM_STATES.MOUNTING);
                    mountRequestStarted = true;
                    const result = await rpc("mount", { manifest });
                    setFilesystemState(DIAGNOSTIC_FILESYSTEM_STATES.MOUNTED);
                    return result;
                } catch (error) {
                    if (mountRequestStarted && error?.code !== "MOUNT_FAILED_CLEAN") {
                        await recoverWorkerOwnership("mount", error);
                    } else {
                        setFilesystemState(DIAGNOSTIC_FILESYSTEM_STATES.UNMOUNTED);
                        await releaseLeases();
                    }
                    throw error;
                }
            });
        },
        unmount() {
            return serializeFilesystemMutation(releaseMountedFilesystem);
        },
        mountAssets(manifest) { return facade.mount(manifest); },
        flushAndUnmount() { return facade.unmount(); },
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
        probeAsset(kind, buffers, metadata = {}) {
            const definitions = {
                localization: {
                    functionName: "_KisakWeb_ProbeLocalization",
                    layout: [
                        { kind: "pointer", index: 0 },
                        { kind: "buffer-size", index: 0 },
                        { kind: "metadata", name: "fileSize" },
                    ],
                },
                iwd: {
                    functionName: "_KisakWeb_ProbeIwd",
                    layout: [
                        { kind: "pointer", index: 0 },
                        { kind: "buffer-size", index: 0 },
                        { kind: "pointer", index: 1 },
                        { kind: "buffer-size", index: 1 },
                        { kind: "metadata", name: "tailOffset" },
                        { kind: "pointer", index: 2 },
                        { kind: "buffer-size", index: 2 },
                        { kind: "metadata", name: "centralOffset" },
                        { kind: "metadata", name: "fileSize" },
                    ],
                },
                fastfile: {
                    functionName: "_KisakWeb_ProbeFastfileHeader",
                    layout: [
                        { kind: "pointer", index: 0 },
                        { kind: "buffer-size", index: 0 },
                        { kind: "metadata", name: "fileSize" },
                    ],
                },
            };
            const definition = definitions[kind];
            if (!definition) return Promise.reject(new TypeError(`Unknown asset probe: ${kind}.`));
            const argumentLayout = definition.layout.map((item) => {
                if (item.kind === "pointer") return item;
                if (item.kind === "buffer-size") {
                    return { kind: "value", value: buffers[item.index]?.byteLength ?? -1 };
                }
                return { kind: "value", value: metadata[item.name] };
            });
            return facade.callProbe(definition.functionName, buffers, argumentLayout);
        },
        call(functionName, ...arguments_) {
            return rpc("call", { functionName, arguments: arguments_ });
        },
        testControl(values) { return rpc("test-control", { values }); },
        checkpoint() {
            return serializeFilesystemMutation(async () => {
                try {
                    return await rpc("checkpoint");
                } catch (error) {
                    if (workerOwnershipUnknown(error)) {
                        await recoverWorkerOwnership("checkpoint", error);
                    }
                    throw error;
                }
            });
        },
        get filesystemState() { return filesystemState; },
        dispose() {
            if (disposePromise) return disposePromise;
            shuttingDown = true;
            disposePromise = (async () => {
                try {
                    await filesystemMutation.catch(() => {});
                    if (!workerUnavailable) {
                        await rpc("shutdown");
                        setFilesystemState(DIAGNOSTIC_FILESYSTEM_STATES.UNMOUNTED);
                    }
                } catch (error) {
                    await recoverWorkerOwnership("shutdown", error);
                } finally {
                    disposed = true;
                    if (!workerUnavailable) {
                        workerUnavailable = true;
                        ++workerGeneration;
                        worker.terminate();
                        await releaseLeases();
                        setFilesystemState(DIAGNOSTIC_FILESYSTEM_STATES.TERMINATED);
                    }
                    if (managePageLifecycle) {
                        globalThis.removeEventListener("pagehide", handlePageHide);
                    }
                    worker.removeEventListener("message", handleMessage);
                    worker.removeEventListener("error", handleError);
                    worker.removeEventListener("messageerror", handleMessageError);
                    audioDriver.dispose();
                    rejectWorkerRequests(pending, protocolError(
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
    const handlePageHide = () => { void facade.dispose(); };
    if (managePageLifecycle) {
        globalThis.addEventListener("pagehide", handlePageHide, { once: true });
    }
    return Object.freeze(facade);
}
