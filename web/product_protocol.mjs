import { ENGINE_PROTOCOL_VERSION } from "./worker_transport.mjs";
export {
    DEFAULT_REQUEST_TIMEOUT_MS,
    ENGINE_PROTOCOL_VERSION,
    EngineWorkerError,
    protocolError,
} from "./worker_transport.mjs";
export const MAX_REQUEST_TIMEOUT_MS = 120_000;

export const PRODUCT_REQUIRED_EXPORTS = Object.freeze([
    "_malloc", "_free", "_KisakWeb_MountCanonicalRuntime", "_KisakWeb_ProbeLocalization",
    "_KisakWeb_ProbeFastfileHeader", "_KisakWeb_ProbeIwd", "_KisakWeb_SubmitCanonicalCommand",
    "_KisakWeb_QueueKeyEvent", "_KisakWeb_QueueCharEvent", "_KisakWeb_SetClipboardText",
    "_KisakWeb_QueueMouseMove",
]);

/** @param {unknown} value */
export function validateProductModule(value)
{
    if (!value || typeof value !== "object" || !(Reflect.get(value, "HEAPU8") instanceof Uint8Array) ||
        PRODUCT_REQUIRED_EXPORTS.some((name) => typeof Reflect.get(value, name) !== "function") ||
        Object.keys(value).some((name) => name.startsWith("_KisakWeb_") && !PRODUCT_REQUIRED_EXPORTS.includes(name))) {
        throw Object.assign(new Error("The production engine has missing or unexpected Wasm exports. Reinstall a matching complete package."), {
            code: "WASM_EXPORT_CONTRACT",
        });
    }
}

/**
 * @typedef {{type?: string, key?: number, down?: boolean, character?: number,
 *   characters?: number[], x?: number, y?: number, dx?: number, dy?: number}} ProductInput
 * @typedef {{protocolVersion?: number, type?: string, id?: number, canvas?: unknown,
 *   manifest?: object, kind?: string, buffers?: ArrayBuffer[], metadata?: object,
 *   command?: string, width?: number, height?: number, event?: ProductInput}} ProductRequest
 * @typedef {{type: "key", key: number, down: boolean} |
 *   {type: "char", character: number} | {type: "clipboard", characters: number[]} |
 *   {type: "mouse-move", x: number, y: number, dx: number, dy: number}} ValidatedInput
 * @typedef {{protocolVersion: number, type: "init", canvas: unknown} |
 *   {protocolVersion: number, type: "input-event", event: ValidatedInput} |
 *   ({protocolVersion: number, id: number} & (
 *   {type: "mountAssets", manifest: object} |
 *   {type: "probeAsset", kind: "localization" | "iwd" | "fastfile", buffers: ArrayBuffer[], metadata: object} |
 *   {type: "submitCanonicalCommand", command: string} |
 *   {type: "resize", width: number, height: number} |
 *   {type: "flushAndUnmount" | "checkpoint" | "runtimeStatus" | "shutdown"}))} ValidatedProductRequest
 */

export const PRODUCT_OPERATIONS = new Set([
    "init",
    "mountAssets",
    "flushAndUnmount",
    "checkpoint",
    "probeAsset",
    "submitCanonicalCommand",
    "resize",
    "runtimeStatus",
    "shutdown",
]);

export const PRODUCT_ONE_WAY_OPERATIONS = new Set(["input-event"]);

export const PRODUCT_HOST_EVENTS = new Set([
    "kisakcod:display",
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
    "kisakcod:renderer-aa",
    "kisakcod:renderer-surface",
    "kisakcod:renderer-surface-draw",
    "kisakcod:renderer-scene-view",
    "kisakcod:renderer-scene-frame",
]);

/** @param {string} operation @param {string} message @returns {never} */
function invalid(operation, message)
{
    throw Object.assign(new TypeError(message), { code: "INVALID_PAYLOAD", operation });
}

/**
 * @param {ProductRequest | null} message
 * @param {{isCanvas?: (value: unknown) => boolean}} [options]
 * @returns {ValidatedProductRequest}
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
    if (!PRODUCT_OPERATIONS.has(operation) && !PRODUCT_ONE_WAY_OPERATIONS.has(operation)) {
        throw Object.assign(new Error(`Unknown Worker operation: ${operation}.`), {
            code: "UNKNOWN_OPERATION", operation: operation ?? "unknown",
        });
    }
    if (operation === "init") {
        if (!isCanvas(message.canvas)) invalid(operation, "A transferable canvas is required.");
        return { protocolVersion: message.protocolVersion, type: "init", canvas: message.canvas };
    }
    if (operation === "input-event") {
        return { protocolVersion: message.protocolVersion, type: "input-event", event: validateInput(operation, message.event) };
    }
    if (typeof message.id !== "number" || !Number.isInteger(message.id) ||
        message.id < 1 || message.id > 0xffff_ffff) {
        invalid(operation, "Worker request IDs must be unsigned non-zero 32-bit integers.");
    }
    const identity = { protocolVersion: message.protocolVersion, id: message.id };
    switch (operation) {
    case "mountAssets":
        if (!message.manifest || typeof message.manifest !== "object" ||
            Array.isArray(message.manifest)) invalid(operation, "An asset manifest is required.");
        return { ...identity, type: operation, manifest: message.manifest };
    case "probeAsset": {
        if ((message.kind !== "localization" && message.kind !== "iwd" && message.kind !== "fastfile") ||
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
        return { ...identity, type: operation, kind: message.kind, buffers: message.buffers, metadata: message.metadata };
    }
    case "submitCanonicalCommand":
        if (typeof message.command !== "string") invalid(operation, "An engine command is required.");
        return { ...identity, type: operation, command: message.command };
    case "resize":
        if (typeof message.width !== "number" || typeof message.height !== "number" ||
            ![message.width, message.height].every(Number.isInteger) ||
            message.width < 1 || message.height < 1 ||
            message.width > 16384 || message.height > 16384) {
            invalid(operation, "Canvas dimensions must be 1..16384.");
        }
        return { ...identity, type: operation, width: message.width, height: message.height };
    case "flushAndUnmount":
    case "checkpoint":
    case "runtimeStatus":
    case "shutdown":
        return { ...identity, type: operation };
    default: return invalid(operation, "Unhandled Worker operation.");
    }
}

/** @param {string} operation @param {ProductInput | undefined} input @returns {ValidatedInput} */
function validateInput(operation, input)
{
    if (input?.type === "key" && typeof input.key === "number" &&
        Number.isInteger(input.key) &&
        input.key > 0 && input.key < 0xDF && typeof input.down === "boolean") {
        return { type: "key", key: input.key, down: input.down };
    }
    if (input?.type === "char" && typeof input.character === "number" &&
        Number.isInteger(input.character) &&
        input.character > 0 && input.character <= 255) {
        return { type: "char", character: input.character };
    }
    if (input?.type === "clipboard" &&
        Array.isArray(input.characters) && input.characters.length > 0 &&
        input.characters.length <= 4095 && input.characters.every((character) =>
            Number.isInteger(character) && character >= 32 &&
            character <= 255 && character !== 127)) {
        return { type: "clipboard", characters: input.characters };
    }
    const values = [input?.x, input?.y, input?.dx, input?.dy];
    if (input?.type === "mouse-move" &&
        typeof input.x === "number" && typeof input.y === "number" &&
        typeof input.dx === "number" && typeof input.dy === "number" &&
        values.every((value) => typeof value === "number" &&
            Number.isInteger(value) && Math.abs(value) <= 1_000_000)) {
        return { type: "mouse-move", x: input.x, y: input.y, dx: input.dx, dy: input.dy };
    }
    return invalid(operation, "The input event is invalid.");
}
