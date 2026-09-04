// Shared Worker transport and filesystem leases. Each host owns its
// operation/event allowlists, timeout policy, and recovery.
export const ENGINE_PROTOCOL_VERSION = 1;
export const DEFAULT_REQUEST_TIMEOUT_MS = 15_000;

export const FILESYSTEM_ABSOLUTE_TIMEOUT_MS = 5 * 60_000;

/** @param {number} timeoutMs @param {string} name */
export function validateFilesystemTimeout(timeoutMs, name)
{
    const maximum = 10 * 60_000;
    if (!Number.isFinite(timeoutMs) || timeoutMs <= 0 || timeoutMs > maximum) {
        throw new RangeError(`${name} timeout must be 1..${maximum} ms.`);
    }
}

/** @param {any} request */
export function clearRequestTimers(request)
{
    clearTimeout(request.timeout);
    clearTimeout(request.absoluteTimeout);
}

/**
 * @param {any} request
 * @param {{timeoutMs: number, stallTimeoutMs?: number, absoluteTimeoutMs?: number}} options
 * @param {(message: string) => void} expire
 */
export function armWorkerRequestTimeout(request, options, expire)
{
    const { timeoutMs, stallTimeoutMs, absoluteTimeoutMs } = options;
    request.stallTimeoutMs = stallTimeoutMs;
    request.refreshStall = () => {
        clearTimeout(request.timeout);
        request.timeout = setTimeout(() => expire(stallTimeoutMs !== undefined
            ? `The engine Worker made no filesystem progress for ${stallTimeoutMs} ms.`
            : `The engine Worker did not answer within ${timeoutMs} ms.`),
        stallTimeoutMs ?? timeoutMs);
    };
    request.refreshStall();
    if (stallTimeoutMs !== undefined) {
        request.absoluteTimeout = setTimeout(() => expire(
            `The filesystem operation exceeded its ${absoluteTimeoutMs} ms absolute limit.`),
        absoluteTimeoutMs);
    }
}

/** @param {any} request @param {any} message @param {number} generation */
export function acceptFilesystemProgress(request, message, generation)
{
    const progress = message.progress;
    if (!request || request.generation !== generation ||
        request.operation !== message.operation || request.stallTimeoutMs === undefined ||
        !progress || typeof progress.phase !== "string" || !progress.phase.length ||
        progress.phase.length > 64 ||
        !Number.isSafeInteger(progress.filesProcessed) || progress.filesProcessed < 0 ||
        !Number.isSafeInteger(progress.bytesProcessed) || progress.bytesProcessed < 0) return false;
    const previous = request.lastProgress;
    if (previous?.phase === progress.phase &&
        progress.filesProcessed <= previous.filesProcessed &&
        progress.bytesProcessed <= previous.bytesProcessed) return false;
    request.lastProgress = { ...progress };
    request.refreshStall();
    return true;
}

/** @param {{id: number, type: string}} message */
export function createFilesystemProgressReporter(message)
{
    let lastSent = 0;
    let lastPhase = "";
    let lastFiles = 0;
    let lastBytes = 0;
    return (/** @type {{phase: string, filesProcessed: number, bytesProcessed: number}} */ progress) => {
        const now = Date.now();
        const phaseChanged = progress.phase !== lastPhase;
        const files = progress.filesProcessed - lastFiles;
        const bytes = progress.bytesProcessed - lastBytes;
        if (!phaseChanged && (files <= 0 && bytes <= 0 ||
            files < 64 && bytes < 16 * 1024 * 1024 && now - lastSent < 250)) return;
        lastSent = now;
        lastPhase = progress.phase;
        lastFiles = progress.filesProcessed;
        lastBytes = progress.bytesProcessed;
        globalThis.postMessage({
            protocolVersion: ENGINE_PROTOCOL_VERSION,
            type: "filesystem-progress", id: message.id, operation: message.type, progress,
        });
    };
}

/**
 * @param {string} code
 * @param {string} operation
 * @param {string} message
 * @param {boolean} [recoverable]
 * @param {unknown} [details]
 */
export function protocolError(code, operation, message, recoverable = false, details)
{
    return { code, operation, message, recoverable, ...(details === undefined ? {} : { details }) };
}

export class EngineWorkerError extends Error
{
    /** @param {any} error */
    constructor(error)
    {
        super(error?.message ?? "The engine Worker request failed.");
        this.name = "EngineWorkerError";
        this.code = error?.code ?? "WORKER_ERROR";
        this.operation = error?.operation ?? "unknown";
        this.recoverable = error?.recoverable === true;
        if (error?.details !== undefined) this.details = error.details;
    }
}

/**
 * @typedef {object} PendingRequest
 * @property {number} generation
 * @property {(value: any) => void} resolve
 * @property {(error: Error) => void} reject
 * @property {ReturnType<typeof setTimeout> | null} timeout
 * @property {ReturnType<typeof setTimeout> | null} [absoluteTimeout]
 * @property {AbortSignal} [signal]
 * @property {() => void} abort
 */

/** @param {Map<number, PendingRequest>} pending */
export function createRequestIdAllocator(pending)
{
    let nextRequestId = 1;
    return () => {
        for (let attempts = 0; attempts <= 0xffff_ffff; ++attempts) {
            const id = nextRequestId;
            nextRequestId = nextRequestId === 0xffff_ffff ? 1 : nextRequestId + 1;
            if (!pending.has(id)) return id;
        }
        throw new EngineWorkerError(protocolError(
            "REQUEST_ID_EXHAUSTED", "request", "No Worker request IDs are available."));
    };
}

/** @param {PendingRequest} request */
function releaseRequest(request)
{
    clearRequestTimers(request);
    request.signal?.removeEventListener("abort", request.abort);
}

/**
 * @param {Map<number, PendingRequest>} pending
 * @param {unknown} error
 */
export function rejectWorkerRequests(pending, error)
{
    const failure = error instanceof EngineWorkerError ? error : new EngineWorkerError(error);
    for (const request of pending.values()) {
        releaseRequest(request);
        request.reject(failure);
    }
    pending.clear();
}

/**
 * @param {Map<number, PendingRequest>} pending
 * @param {{id: number, error?: unknown, result?: unknown}} message
 * @param {number} generation
 */
export function settleWorkerReply(pending, message, generation)
{
    const request = pending.get(message.id);
    if (!request || request.generation !== generation) return;
    pending.delete(message.id);
    releaseRequest(request);
    if (message.error) request.reject(new EngineWorkerError(message.error));
    else request.resolve(message.result);
}

const ENGINE_FILESYSTEM_LOCK = "kisakcod-web-engine-filesystem-v1";
const HOME_WRITER_LOCK = "kisakcod-web-home-writer-v1";

/**
 * Imported files are shared; writable home data has one owner across tabs.
 * @param {Pick<LockManager, "request"> | null | undefined} lockManager
 * @param {(state: string) => void} emitFilesystemLifecycle
 */
export function createFilesystemLeases(lockManager, emitFilesystemLifecycle)
{
    /** @type {(() => void) | null} */
    let releaseFilesystemLease = null;
    /** @type {Promise<void> | null} */
    let filesystemLeaseCompletion = null;
    /** @type {(() => void) | null} */
    let releaseHomeWriterLease = null;
    /** @type {Promise<void> | null} */
    let homeWriterLeaseCompletion = null;
    async function acquire()
    {
        if (releaseFilesystemLease || !lockManager?.request) return;

        const homeAcquired = Promise.withResolvers();
        /** @type {PromiseWithResolvers<void>} */
        const homeHeld = Promise.withResolvers();
        homeWriterLeaseCompletion = lockManager.request(
            HOME_WRITER_LOCK,
            { mode: "exclusive", ifAvailable: true },
            async (lock) => {
                if (!lock) {
                    homeAcquired.resolve(false);
                    return;
                }
                releaseHomeWriterLease = homeHeld.resolve;
                homeAcquired.resolve(true);
                emitFilesystemLifecycle("writerLeaseAcquired");
                await homeHeld.promise;
            },
        );
        if (!await homeAcquired.promise) {
            await homeWriterLeaseCompletion;
            homeWriterLeaseCompletion = null;
            throw Object.assign(new Error("Another tab owns the writable browser profile."), {
                code: "HOME_WRITER_CONFLICT",
            });
        }

        /** @type {PromiseWithResolvers<void>} */
        const filesystemAcquired = Promise.withResolvers();
        /** @type {PromiseWithResolvers<void>} */
        const filesystemHeld = Promise.withResolvers();
        try {
            filesystemLeaseCompletion = lockManager.request(
                ENGINE_FILESYSTEM_LOCK,
                { mode: "shared" },
                async () => {
                    releaseFilesystemLease = filesystemHeld.resolve;
                    filesystemAcquired.resolve();
                    await filesystemHeld.promise;
                },
            );
            await filesystemAcquired.promise;
        } catch (error) {
            releaseHomeWriterLease?.();
            await homeWriterLeaseCompletion;
            releaseHomeWriterLease = null;
            homeWriterLeaseCompletion = null;
            throw error;
        }
    }

    async function release()
    {
        const releasedWriter = Boolean(releaseHomeWriterLease);
        const completions = [filesystemLeaseCompletion, homeWriterLeaseCompletion]
            .filter(Boolean);
        const releases = [releaseFilesystemLease, releaseHomeWriterLease]
            .filter(Boolean);
        filesystemLeaseCompletion = null;
        homeWriterLeaseCompletion = null;
        releaseFilesystemLease = null;
        releaseHomeWriterLease = null;
        for (const release of releases) release?.();
        await Promise.all(completions);
        if (releasedWriter) emitFilesystemLifecycle("writerLeaseReleased");
    }

    return { acquire, release };
}

/** Accept only complete, bounded Web Audio device snapshots. @param {any} message */
export function acceptAudioPlayback(message) {
    if (message?.type !== "audio-playback" || message.version !== 1 ||
        !Array.isArray(message.sources) || message.sources.length > 54) return false;
    const states = Object.create(null);
    const uint = (/** @type {any} */ value) => Number.isInteger(value) && value >= 0 && value <= 0xffff_ffff;
    for (const source of message.sources) {
        if (!source || !uint(source.sourceId) || source.sourceId < 1 || source.sourceId > 54 ||
            states[source.sourceId] || !uint(source.generation) || !uint(source.processed) ||
            !Number.isFinite(source.offset) || source.offset < 0 ||
            ![0, 0x1012, 0x1013, 0x1014].includes(source.state)) return false;
        states[source.sourceId] = source;
    }
    /** @type {any} */ (globalThis).__KISAKCOD_AUDIO_PLAYBACK__ = states;
    return true;
}
