import { createWorkerSyncFilesystem } from "./worker_sync_filesystem.mjs";
import {
    ENGINE_PROTOCOL_VERSION,
    HOST_EVENTS,
    protocolError,
} from "./engine_protocol.mjs";

const forwardedEvents = [
    "kisakcod:archive", "kisakcod:archive-progress", "kisakcod:qcommon", "kisakcod:retail-census",
    "kisakcod:renderer-shader", "kisakcod:schedule", "kisakcod:engine-asset",
    "kisakcod:renderer-texture", "kisakcod:engine-world-surface",
    "kisakcod:renderer-aa", "kisakcod:test-webgl-aa",
    "kisakcod:renderer-surface", "kisakcod:renderer-surface-draw",
    "kisakcod:renderer-scene-view", "kisakcod:renderer-scene-frame",
    "kisakcod:renderer-fx", "kisakcod:renderer-comparison",
    "kisakcod:renderer-comparison-record",
    "kisakcod:state", "kisakcod:frame",
    "kisakcod:system", "kisakcod:engine", "kisakcod:runtime",
    "kisakcod:database",
    "kisakcod:canonical-gfxworld",
    "kisakcod:canonical-runtime-prefix",
    "kisakcod:engine-lifecycle",
    "kisakcod:canonical-filesystem",
    "kisakcod:cinematic",
    "kisakcod:renderer-memory",
    "kisakcod:renderer-lifecycle",
];

if (typeof globalThis.CustomEvent !== "function") {
    globalThis.CustomEvent = class CustomEvent extends Event {
        constructor(type, options = {}) {
            super(type, options);
            this.detail = options.detail;
        }
    };
}
for (const name of forwardedEvents) {
    if (!HOST_EVENTS.has(name)) throw new Error(`Worker event is not allowed: ${name}.`);
    globalThis.addEventListener(name, (event) => {
        globalThis.postMessage({
            protocolVersion: ENGINE_PROTOCOL_VERSION,
            type: "event",
            name,
            detail: event.detail,
        });
    });
}

const testControl = Object.create(null);
const faultControlNames = Object.freeze({
    read: "failReadPath",
    list: "failSyncListPath",
    open: "failSyncOpenPath",
    seek: "failSyncSeekPath",
    "sync-read": "failSyncReadPath",
});
function normalizeFaultPath(path)
{
    return String(path ?? "").replaceAll("\\", "/")
        .replace(/^\.\//u, "").toLocaleLowerCase("en-US");
}
const filesystem = createWorkerSyncFilesystem({
    async beforePersist() {
        if (testControl.failPersistence === true) {
            throw new DOMException(
                "Injected browser home persistence failure.", "QuotaExceededError");
        }
    },
    reject(operation, logicalPath) {
        const configured = normalizeFaultPath(testControl[faultControlNames[operation]]);
        return configured !== "" && configured === logicalPath;
    },
});
let module = null;
let resolveInitialization;
const initialization = new Promise((resolve) => { resolveInitialization = resolve; });

function reply(id, operation, result, error = null)
{
    globalThis.postMessage({
        protocolVersion: ENGINE_PROTOCOL_VERSION,
        type: "reply",
        id,
        result,
        error,
        operation,
    });
}

function installWorkerTestControls()
{
    const canvasPrototype = globalThis.OffscreenCanvas?.prototype;
    if (canvasPrototype && !canvasPrototype.__kisakcodWorkerContextTestControls) {
        Object.defineProperty(canvasPrototype,
            "__kisakcodWorkerContextTestControls", { value: true });
        const getContext = canvasPrototype.getContext;
        canvasPrototype.getContext = function(type, attributes) {
            if (testControl.observeAa && type === "webgl2") {
                globalThis.dispatchEvent(new CustomEvent("kisakcod:test-webgl-aa", {
                    detail: {
                        operation: "create-context",
                        antialias: attributes?.antialias === true,
                    },
                }));
            }
            return getContext.call(this, type, attributes);
        };
    }
    const prototype = globalThis.WebGL2RenderingContext?.prototype;
    if (!prototype || prototype.__kisakcodWorkerTestControls) return;
    Object.defineProperty(prototype, "__kisakcodWorkerTestControls", { value: true });

    const shaderSource = prototype.shaderSource;
    prototype.shaderSource = function(shader, source) {
        if (testControl.failInitialShader && !testControl.initialShaderFailureConsumed) {
            testControl.initialShaderFailureConsumed = true;
            return shaderSource.call(this, shader, "forced invalid shader source");
        }
        return shaderSource.call(this, shader, source);
    };
    const getAttribLocation = prototype.getAttribLocation;
    prototype.getAttribLocation = function(program, name) {
        if (testControl.failCompatibilityBinding && name === "a_texcoord0") return -1;
        return getAttribLocation.call(this, program, name);
    };
    const bufferData = prototype.bufferData;
    prototype.bufferData = function(...arguments_) {
        if (testControl.failSurfaceRestore && !testControl.surfaceRestoreFailureConsumed &&
            arguments_[0] === 0x8893) {
            testControl.surfaceRestoreFailureConsumed = true;
            return bufferData.call(this, arguments_[0], -1, arguments_[2]);
        }
        return bufferData.apply(this, arguments_);
    };
    const renderbufferStorageMultisample = prototype.renderbufferStorageMultisample;
    prototype.renderbufferStorageMultisample = function(
        target, samples, internalFormat, width, height) {
        if (testControl.observeAa) {
            globalThis.dispatchEvent(new CustomEvent("kisakcod:test-webgl-aa", {
                detail: {
                    operation: "renderbuffer-storage-multisample",
                    target, samples, internalFormat, width, height,
                },
            }));
        }
        return renderbufferStorageMultisample.call(
            this, target, samples, internalFormat, width, height);
    };
    const blitFramebuffer = prototype.blitFramebuffer;
    prototype.blitFramebuffer = function(...arguments_) {
        if (testControl.observeAa) {
            globalThis.dispatchEvent(new CustomEvent("kisakcod:test-webgl-aa", {
                detail: {
                    operation: "blit-framebuffer",
                    source: arguments_.slice(0, 4),
                    destination: arguments_.slice(4, 8),
                    mask: arguments_[8],
                    filter: arguments_[9],
                },
            }));
        }
        return blitFramebuffer.apply(this, arguments_);
    };
    const getParameter = prototype.getParameter;
    prototype.getParameter = function(parameter) {
        if (parameter === 0x8D57 && Number.isInteger(testControl.maxAaSamples)) {
            return Math.max(1, testControl.maxAaSamples);
        }
        return getParameter.call(this, parameter);
    };
}

async function probe(functionName, buffers, argumentLayout)
{
    const fn = module?.[functionName];
    if (typeof fn !== "function") throw new Error("The requested Wasm probe is unavailable.");
    const allocations = [];
    try {
        for (const buffer of buffers) {
            const bytes = new Uint8Array(buffer);
            const pointer = module._malloc(Math.max(1, bytes.byteLength));
            if (!pointer) throw new Error("Wasm probe allocation failed.");
            allocations.push({ pointer, bytes });
            if (bytes.byteLength) module.HEAPU8.set(bytes, pointer);
        }
        const pointers = allocations.map((entry) => entry.pointer);
        const arguments_ = argumentLayout.map((item) =>
            item.kind === "pointer" ? pointers[item.index] : item.value);
        return fn(...arguments_);
    } finally {
        for (const allocation of allocations) module._free(allocation.pointer);
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
            switch (message.type) {
            case "init":
                globalThis.__KISAKCOD_OFFSCREEN_CANVAS__ = message.canvas;
                Object.assign(testControl, message.testConfig ?? {});
                installWorkerTestControls();
                resolveInitialization();
                break;
            case "mount": reply(message.id, message.type, await filesystem.mount(message.manifest)); break;
            case "unmount": reply(message.id, message.type, await filesystem.flushAndUnmount()); break;
            case "checkpoint": reply(message.id, message.type, await filesystem.checkpoint()); break;
            case "shutdown": reply(message.id, message.type, await filesystem.flushAndUnmount()); break;
            case "probe": {
                const result = await probe(
                    message.functionName, message.buffers, message.argumentLayout);
                await filesystem.checkpoint();
                reply(message.id, message.type, result);
                break;
            }
            case "call": {
                const fn = module?.[message.functionName];
                if (typeof fn !== "function") throw new Error(`Missing export ${message.functionName}.`);
                const result = fn(...(message.arguments ?? []));
                await filesystem.checkpoint();
                reply(message.id, message.type, result);
                break;
            }
            case "test-control":
                Object.assign(testControl, message.values ?? {});
                reply(message.id, message.type, true);
                break;
            case "resize":
                if (!Number.isInteger(message.width) || !Number.isInteger(message.height) ||
                    message.width < 1 || message.height < 1 ||
                    message.width > 16384 || message.height > 16384) {
                    throw Object.assign(new RangeError("Canvas dimensions must be 1..16384."), {
                        code: "INVALID_PAYLOAD",
                    });
                }
                if (module?.canvas) {
                    module.canvas.width = message.width;
                    module.canvas.height = message.height;
                }
                reply(message.id, message.type, true);
                break;
            case "input": {
                const input = message.event;
                if (!input || !module) {
                    throw Object.assign(new TypeError("A ready engine input event is required."), {
                        code: "INVALID_PAYLOAD",
                    });
                }
                if (input.type === "key") {
                    module._KisakWeb_QueueKeyEvent?.(input.key, input.down ? 1 : 0);
                } else if (input.type === "mouse-move") {
                    module._KisakWeb_QueueMouseMove?.(
                        input.x, input.y, input.dx, input.dy);
                }
                reply(message.id, message.type, true);
                break;
            }
            default:
                throw Object.assign(new Error(`Unknown Worker operation: ${message.type}.`), {
                    code: "UNKNOWN_OPERATION",
                });
            }
        } catch (error) {
            const detail = typeof error?.stack === "string" ? error.stack
                : typeof error?.message === "string" ? error.message
                    : String(error);
            reply(message.id, message?.type ?? "unknown", null, protocolError(
                error?.name === "QuotaExceededError"
                    ? "STORAGE_QUOTA"
                    : error?.code ?? "OPERATION_FAILED",
                message?.type ?? "unknown",
                error?.message ?? String(error),
                error?.name === "QuotaExceededError",
                detail,
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
    globalThis.postMessage({ protocolVersion: ENGINE_PROTOCOL_VERSION, type: "ready" });
} catch (error) {
    globalThis.postMessage({
        protocolVersion: ENGINE_PROTOCOL_VERSION,
        type: "startup-error",
        error: protocolError(
            "STARTUP_FAILED", "initialize", error?.message ?? String(error), false,
            error?.stack ?? String(error)),
    });
}
