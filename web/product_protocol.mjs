export const ENGINE_PROTOCOL_VERSION = 1;
export const DEFAULT_REQUEST_TIMEOUT_MS = 15_000;
export const MAX_REQUEST_TIMEOUT_MS = 120_000;

/**
 * @typedef {{type?: string, key?: number, down?: boolean,
 *   x?: number, y?: number, dx?: number, dy?: number}} ProductInput
 * @typedef {{protocolVersion?: number, type?: string, id?: number, canvas?: unknown,
 *   manifest?: object, kind?: string, buffers?: ArrayBuffer[], metadata?: object,
 *   command?: string, width?: number, height?: number, event?: ProductInput}} ProductRequest
 */

export const PRODUCT_OPERATIONS = new Set([
    "init",
    "mountAssets",
    "flushAndUnmount",
    "probeAsset",
    "submitCanonicalCommand",
    "resize",
    "input",
    "runtimeStatus",
    "shutdown",
]);

export const PRODUCT_HOST_EVENTS = new Set([
    "kisakcod:state",
    "kisakcod:frame",
    "kisakcod:system",
    "kisakcod:engine",
    "kisakcod:runtime",
    "kisakcod:database",
    "kisakcod:canonical-gfxworld",
    "kisakcod:canonical-runtime-prefix",
    "kisakcod:engine-lifecycle",
    "kisakcod:canonical-filesystem",
    "kisakcod:cinematic",
    "kisakcod:renderer-memory",
    "kisakcod:renderer-shader",
    "kisakcod:renderer-texture",
    "kisakcod:renderer-aa",
    "kisakcod:renderer-surface",
    "kisakcod:renderer-surface-draw",
    "kisakcod:renderer-scene-view",
    "kisakcod:renderer-scene-frame",
]);

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

/** @param {string} operation @param {string} message @returns {never} */
function invalid(operation, message)
{
    throw Object.assign(new TypeError(message), { code: "INVALID_PAYLOAD", operation });
}

/**
 * @param {ProductRequest | null} message
 * @param {{isCanvas?: (value: unknown) => boolean}} [options]
 * @returns {ProductRequest}
 */
export function validateProductRequest(message, {
    /** @param {unknown} value */
    isCanvas = (value) => typeof OffscreenCanvas === "function" &&
        value instanceof OffscreenCanvas,
} = {})
{
    if (!message || typeof message !== "object" || Array.isArray(message)) {
        invalid("unknown", "Worker requests must be objects.");
    }
    const operation = message.type ?? "unknown";
    if (message.protocolVersion !== ENGINE_PROTOCOL_VERSION) {
        throw Object.assign(new Error("Unsupported engine protocol version."), {
            code: "PROTOCOL_VERSION", operation: operation ?? "unknown",
        });
    }
    if (!PRODUCT_OPERATIONS.has(operation)) {
        throw Object.assign(new Error(`Unknown Worker operation: ${operation}.`), {
            code: "UNKNOWN_OPERATION", operation: operation ?? "unknown",
        });
    }
    if (operation === "init") {
        if (!isCanvas(message.canvas)) invalid(operation, "A transferable canvas is required.");
        return message;
    }
    if (typeof message.id !== "number" || !Number.isInteger(message.id) ||
        message.id < 1 || message.id > 0xffff_ffff) {
        invalid(operation, "Worker request IDs must be unsigned non-zero 32-bit integers.");
    }
    switch (operation) {
    case "mountAssets":
        if (!message.manifest || typeof message.manifest !== "object" ||
            Array.isArray(message.manifest)) invalid(operation, "An asset manifest is required.");
        break;
    case "probeAsset": {
        if (typeof message.kind !== "string" ||
            !["localization", "iwd", "fastfile"].includes(message.kind) ||
            !Array.isArray(message.buffers) || message.buffers.length < 1 ||
            message.buffers.length > 3 ||
            message.buffers.some((buffer) => !(buffer instanceof ArrayBuffer)) ||
            !message.metadata || typeof message.metadata !== "object" ||
            Array.isArray(message.metadata)) {
            invalid(operation, "The asset probe payload is invalid.");
        }
        const expectedBuffers = /** @type {Record<string, number>} */ ({
            localization: 1, iwd: 3, fastfile: 1,
        })[message.kind ?? ""];
        if (message.buffers.length !== expectedBuffers) {
            invalid(operation, `${message.kind} probes require ${expectedBuffers} buffer(s).`);
        }
        break;
    }
    case "submitCanonicalCommand":
        if (typeof message.command !== "string") invalid(operation, "An engine command is required.");
        break;
    case "resize":
        if (typeof message.width !== "number" || typeof message.height !== "number" ||
            ![message.width, message.height].every(Number.isInteger) ||
            message.width < 1 || message.height < 1 ||
            message.width > 16384 || message.height > 16384) {
            invalid(operation, "Canvas dimensions must be 1..16384.");
        }
        break;
    case "input": {
        const input = message.event;
        const validKey = input?.type === "key" && typeof input.key === "number" &&
            Number.isInteger(input.key) &&
            input.key >= 0 && input.key <= 0xffff && typeof input.down === "boolean";
        const values = [input?.x, input?.y, input?.dx, input?.dy];
        const validMouse = input?.type === "mouse-move" &&
            values.every((value) => typeof value === "number" &&
                Number.isInteger(value) && Math.abs(value) <= 1_000_000);
        if (!validKey && !validMouse) invalid(operation, "The input event is invalid.");
        break;
    }
    default:
        break;
    }
    return message;
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
