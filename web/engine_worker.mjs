import { createWorkerSyncFilesystem } from "./worker_sync_filesystem.mjs";

const forwardedEvents = [
    "kisakcod:archive", "kisakcod:archive-progress", "kisakcod:qcommon", "kisakcod:retail-census",
    "kisakcod:renderer-shader", "kisakcod:schedule", "kisakcod:engine-asset",
    "kisakcod:renderer-texture", "kisakcod:engine-world-surface",
    "kisakcod:renderer-surface", "kisakcod:renderer-surface-draw",
    "kisakcod:state", "kisakcod:frame",
    "kisakcod:system", "kisakcod:engine", "kisakcod:runtime",
    "kisakcod:database",
    "kisakcod:canonical-gfxworld",
    "kisakcod:canonical-runtime-prefix",
    "kisakcod:engine-lifecycle",
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
    globalThis.addEventListener(name, (event) => {
        globalThis.postMessage({ type: "event", name, detail: event.detail });
    });
}

const filesystem = createWorkerSyncFilesystem();
const testControl = Object.create(null);
let module = null;
let resolveInitialization;
const initialization = new Promise((resolve) => { resolveInitialization = resolve; });

function reply(id, result, error = null)
{
    globalThis.postMessage({ type: "reply", id, result, error });
}

function installWorkerTestControls()
{
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
            switch (message.type) {
            case "init":
                globalThis.__KISAKCOD_OFFSCREEN_CANVAS__ = message.canvas;
                Object.assign(testControl, message.testConfig ?? {});
                installWorkerTestControls();
                resolveInitialization();
                break;
            case "mount": reply(message.id, await filesystem.mount(message.manifest)); break;
            case "unmount": filesystem.unmount(); reply(message.id, true); break;
            case "probe": reply(message.id, await probe(
                message.functionName, message.buffers, message.argumentLayout)); break;
            case "call": {
                const fn = module?.[message.functionName];
                if (typeof fn !== "function") throw new Error(`Missing export ${message.functionName}.`);
                reply(message.id, fn(...(message.arguments ?? [])));
                break;
            }
            case "test-control":
                Object.assign(testControl, message.values ?? {});
                filesystem.setTestControl(message.values ?? {});
                reply(message.id, true);
                break;
            case "resize":
                if (module?.canvas) {
                    module.canvas.width = message.width;
                    module.canvas.height = message.height;
                }
                break;
            default: break;
            }
        } catch (error) {
            reply(message.id, null, error?.message ?? String(error));
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
    globalThis.postMessage({ type: "ready" });
} catch (error) {
    globalThis.postMessage({ type: "startup-error", error: error?.stack ?? String(error) });
}
