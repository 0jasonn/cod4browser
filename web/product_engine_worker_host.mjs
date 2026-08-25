import { WebAudioDriver } from "./web_audio_driver.mjs";
import {
    DEFAULT_REQUEST_TIMEOUT_MS,
    ENGINE_PROTOCOL_VERSION,
    EngineWorkerError,
    PRODUCT_HOST_EVENTS,
    protocolError,
} from "./engine_protocol.mjs";

const ENGINE_FILESYSTEM_LOCK = "kisakcod-web-engine-filesystem-v1";
const HOME_WRITER_LOCK = "kisakcod-web-home-writer-v1";

export function createEngineWorkerHost(canvas, {
    onLog,
    onAbort,
    requestTimeoutMs = DEFAULT_REQUEST_TIMEOUT_MS,
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

    function rpc(type, payload = {}, transfer = [], timeoutMs = requestTimeoutMs)
    {
        if ((shuttingDown && type !== "shutdown") || disposed) {
            return Promise.reject(new EngineWorkerError(protocolError(
                "WORKER_SHUTTING_DOWN", type, "The engine Worker is shutting down.")));
        }
        if (!Number.isFinite(timeoutMs) || timeoutMs <= 0 || timeoutMs > 120_000) {
            return Promise.reject(new RangeError("Worker request timeout must be 1..120000 ms."));
        }
        const id = allocateRequestId();
        const promise = new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                if (!pending.delete(id)) return;
                reject(new EngineWorkerError(protocolError(
                    "REQUEST_TIMEOUT", type,
                    `The engine Worker did not answer within ${timeoutMs} ms.`, true)));
            }, timeoutMs);
            pending.set(id, { resolve, reject, timeout });
        });
        try {
            worker.postMessage({ protocolVersion: ENGINE_PROTOCOL_VERSION, type, id, ...payload }, transfer);
        } catch (error) {
            const request = pending.get(id);
            pending.delete(id);
            clearTimeout(request?.timeout);
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
        mountAssets(manifest) {
            return serializeFilesystemMutation(async () => {
                await flushMountedFilesystem();
                await acquireLeases();
                try {
                    const result = await rpc("mountAssets", { manifest });
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
        probeAsset(kind, buffers, metadata = {}) {
            const transferred = buffers.map((bytes) => Uint8Array.from(bytes).buffer);
            return rpc("probeAsset", { kind, buffers: transferred, metadata }, transferred);
        },
        submitCanonicalCommand(command) {
            return rpc("submitCanonicalCommand", { command });
        },
        resize(width, height) { return rpc("resize", { width, height }); },
        input(event) { return rpc("input", { event }); },
        runtimeStatus() { return rpc("runtimeStatus"); },
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
                    audioDriver.dispose();
                    worker.terminate();
                    rejectPending(protocolError(
                        "WORKER_TERMINATED", "shutdown", "The engine Worker was terminated."));
                }
            })();
            return disposePromise;
        },
    };
    globalThis.addEventListener("pagehide", () => { void facade.dispose(); }, { once: true });
    return Object.freeze(facade);
}
