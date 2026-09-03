// Transport bookkeeping only. Each host owns its operation/event allowlists,
// timeout policy, recovery, and filesystem leases.
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
