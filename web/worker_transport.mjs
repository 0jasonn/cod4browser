// Shared Worker transport and filesystem leases. Each host owns its
// operation/event allowlists, timeout policy, and recovery.
export const ENGINE_PROTOCOL_VERSION = 1;
export const DEFAULT_REQUEST_TIMEOUT_MS = 15_000;

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
    clearTimeout(request.timeout ?? undefined);
    clearTimeout(request.absoluteTimeout ?? undefined);
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
