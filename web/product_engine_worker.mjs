import { createWorkerSyncFilesystem } from "./worker_sync_filesystem.mjs";
import {
    ENGINE_PROTOCOL_VERSION,
    PRODUCT_HOST_EVENTS,
    protocolError,
} from "./engine_protocol.mjs";

if (typeof globalThis.CustomEvent !== "function") {
    globalThis.CustomEvent = class CustomEvent extends Event {
        constructor(type, options = {})
        {
            super(type, options);
            this.detail = options.detail;
        }
    };
}

for (const name of PRODUCT_HOST_EVENTS) {
    globalThis.addEventListener(name, (event) => {
        globalThis.postMessage({
            protocolVersion: ENGINE_PROTOCOL_VERSION,
            type: "event",
            name,
            detail: event.detail,
        });
    });
}

const filesystem = createWorkerSyncFilesystem();
let module = null;
let state = "starting";
let resolveInitialization;
const initialization = new Promise((resolve) => { resolveInitialization = resolve; });

function reply(id, operation, result, error = null)
{
    globalThis.postMessage({
        protocolVersion: ENGINE_PROTOCOL_VERSION,
        type: "reply",
        id,
        operation,
        result,
        error,
    });
}

function requireSafeSize(value, name)
{
    if (!Number.isSafeInteger(value) || value < 0) {
        throw Object.assign(new TypeError(`${name} must be a non-negative safe integer.`), {
            code: "INVALID_PAYLOAD",
        });
    }
    return value;
}

async function withBuffers(buffers, callback)
{
    if (!Array.isArray(buffers)) {
        throw Object.assign(new TypeError("Asset probe buffers are required."), {
            code: "INVALID_PAYLOAD",
        });
    }
    const allocations = [];
    try {
        for (const buffer of buffers) {
            if (!(buffer instanceof ArrayBuffer)) {
                throw Object.assign(new TypeError("Asset probe data must be transferable buffers."), {
                    code: "INVALID_PAYLOAD",
                });
            }
            const bytes = new Uint8Array(buffer);
            const pointer = module._malloc(Math.max(1, bytes.byteLength));
            if (!pointer) throw Object.assign(new Error("Wasm probe allocation failed."), {
                code: "WASM_MEMORY",
            });
            allocations.push({ pointer, bytes });
            if (bytes.byteLength > 0) module.HEAPU8.set(bytes, pointer);
        }
        return await callback(allocations);
    } finally {
        for (const allocation of allocations) module._free(allocation.pointer);
    }
}

async function probeAsset(kind, buffers, metadata = {})
{
    return withBuffers(buffers, (items) => {
        const size = (index) => items[index]?.bytes.byteLength ?? -1;
        const pointer = (index) => items[index]?.pointer ?? 0;
        switch (kind) {
        case "localization":
            if (items.length !== 1) throw Object.assign(new TypeError("Localization needs one buffer."), { code: "INVALID_PAYLOAD" });
            return module._KisakWeb_ProbeLocalization(
                pointer(0), size(0), requireSafeSize(metadata.fileSize, "fileSize"));
        case "iwd":
            if (items.length !== 3) throw Object.assign(new TypeError("IWD needs three buffers."), { code: "INVALID_PAYLOAD" });
            return module._KisakWeb_ProbeIwd(
                pointer(0), size(0), pointer(1), size(1),
                requireSafeSize(metadata.tailOffset, "tailOffset"),
                pointer(2), size(2),
                requireSafeSize(metadata.centralOffset, "centralOffset"),
                requireSafeSize(metadata.fileSize, "fileSize"));
        case "fastfile":
            if (items.length !== 1) throw Object.assign(new TypeError("Fastfile needs one buffer."), { code: "INVALID_PAYLOAD" });
            return module._KisakWeb_ProbeFastfileHeader(
                pointer(0), size(0), requireSafeSize(metadata.fileSize, "fileSize"));
        default:
            throw Object.assign(new TypeError(`Unknown asset probe: ${kind}.`), {
                code: "INVALID_PAYLOAD",
            });
        }
    });
}

function submitCanonicalCommand(command)
{
    const text = String(command).trim();
    const bytes = new TextEncoder().encode(`${text}\0`);
    if (!text || bytes.byteLength > 1024) {
        throw Object.assign(new RangeError("Engine commands must be 1..1023 UTF-8 bytes."), {
            code: "INVALID_PAYLOAD",
        });
    }
    const pointer = module._malloc(bytes.byteLength);
    if (!pointer) throw Object.assign(new Error("Wasm command allocation failed."), {
        code: "WASM_MEMORY",
    });
    try {
        module.HEAPU8.set(bytes, pointer);
        return module._KisakWeb_SubmitCanonicalCommand(pointer);
    } finally {
        module._free(pointer);
    }
}

globalThis.addEventListener("message", (event) => {
    const message = event.data;
    if (!message || typeof message !== "object") return;
    void (async () => {
        try {
            if (message.protocolVersion !== ENGINE_PROTOCOL_VERSION) {
                throw Object.assign(new Error("Unsupported engine protocol version."), {
                    code: "PROTOCOL_VERSION",
                });
            }
            if (message.type === "init") {
                if (!(message.canvas instanceof OffscreenCanvas)) {
                    throw Object.assign(new TypeError("A transferable canvas is required."), {
                        code: "INVALID_PAYLOAD",
                    });
                }
                globalThis.__KISAKCOD_OFFSCREEN_CANVAS__ = message.canvas;
                resolveInitialization();
                return;
            }
            if (!module) throw Object.assign(new Error("The engine is not ready."), {
                code: "ENGINE_NOT_READY",
            });

            switch (message.type) {
            case "mountAssets": {
                try {
                    const mounted = await filesystem.mount(message.manifest);
                    module._KisakWeb_MountCanonicalRuntime();
                    await filesystem.checkpoint();
                    state = "mounted";
                    reply(message.id, message.type, { mounted, runtime: true });
                } catch (error) {
                    await filesystem.flushAndUnmount().catch(() => {});
                    state = "ready";
                    throw error;
                }
                break;
            }
            case "flushAndUnmount":
                reply(message.id, message.type, await filesystem.flushAndUnmount());
                state = "ready";
                break;
            case "probeAsset": {
                const result = await probeAsset(message.kind, message.buffers, message.metadata);
                await filesystem.checkpoint();
                reply(message.id, message.type, result);
                break;
            }
            case "submitCanonicalCommand": {
                const result = submitCanonicalCommand(message.command);
                await filesystem.checkpoint();
                reply(message.id, message.type, result);
                break;
            }
            case "resize":
                if (!Number.isInteger(message.width) || !Number.isInteger(message.height) ||
                    message.width < 1 || message.height < 1 ||
                    message.width > 16384 || message.height > 16384) {
                    throw Object.assign(new RangeError("Canvas dimensions must be 1..16384."), {
                        code: "INVALID_PAYLOAD",
                    });
                }
                module.canvas.width = message.width;
                module.canvas.height = message.height;
                reply(message.id, message.type, true);
                break;
            case "input": {
                const input = message.event;
                if (input?.type === "key" && Number.isInteger(input.key)) {
                    module._KisakWeb_QueueKeyEvent(input.key, input.down ? 1 : 0);
                } else if (input?.type === "mouse-move" &&
                    [input.x, input.y, input.dx, input.dy].every(Number.isInteger)) {
                    module._KisakWeb_QueueMouseMove(input.x, input.y, input.dx, input.dy);
                } else {
                    throw Object.assign(new TypeError("The input event is invalid."), {
                        code: "INVALID_PAYLOAD",
                    });
                }
                reply(message.id, message.type, true);
                break;
            }
            case "runtimeStatus":
                reply(message.id, message.type, { state });
                break;
            case "shutdown":
                state = "stopping";
                reply(message.id, message.type, await filesystem.flushAndUnmount());
                state = "stopped";
                break;
            default:
                throw Object.assign(new Error(`Unknown Worker operation: ${message.type}.`), {
                    code: "UNKNOWN_OPERATION",
                });
            }
        } catch (error) {
            const details = error?.stack ?? String(error);
            reply(message.id, message?.type ?? "unknown", null, protocolError(
                error?.name === "QuotaExceededError"
                    ? "STORAGE_QUOTA"
                    : error?.code ?? "OPERATION_FAILED",
                message?.type ?? "unknown",
                error?.message ?? String(error),
                error?.name === "QuotaExceededError",
                details,
            ));
        }
    })();
});

try {
    await initialization;
    const { default: createKisakCOD } = await import("./kisakcod.mjs");
    module = await createKisakCOD({
        canvas: globalThis.__KISAKCOD_OFFSCREEN_CANVAS__,
        locateFile(path) { return new URL(path, import.meta.url).href; },
        print(message) { globalThis.postMessage({ type: "log", level: "info", message }); },
        printErr(message) { globalThis.postMessage({ type: "log", level: "error", message }); },
        onAbort(reason) { globalThis.postMessage({ type: "abort", reason: String(reason) }); },
    });
    filesystem.installForModule(module);
    state = "ready";
    globalThis.postMessage({ protocolVersion: ENGINE_PROTOCOL_VERSION, type: "ready" });
} catch (error) {
    state = "failed";
    globalThis.postMessage({
        protocolVersion: ENGINE_PROTOCOL_VERSION,
        type: "startup-error",
        error: protocolError(
            "STARTUP_FAILED", "initialize", error?.message ?? String(error), false,
            error?.stack ?? String(error)),
    });
}
