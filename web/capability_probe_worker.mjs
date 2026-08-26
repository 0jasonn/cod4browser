const PROBE_REPLY = "kisak-capability-probe-result";

function errorDetail(error)
{
    const name = typeof error?.name === "string" ? error.name : "Error";
    const message = typeof error?.message === "string" ? error.message : String(error);
    return `${name}: ${message}`;
}

function supported(capability)
{
    return { supported: true, capability, code: "OK", message: "Available", detail: "" };
}

function unsupported(capability, code, message, error = null)
{
    return {
        supported: false,
        capability,
        code,
        message,
        detail: error ? errorDetail(error) : "",
    };
}

function randomProbeName()
{
    const suffix = typeof globalThis.crypto?.randomUUID === "function"
        ? globalThis.crypto.randomUUID()
        : `${Date.now()}-${Math.random().toString(16).slice(2)}`;
    return `.kisak-capability-probe-${suffix}`;
}

export async function probeOpfsSyncAccess({
    storage = globalThis.navigator?.storage,
    createProbeName = randomProbeName,
} = {})
{
    if (typeof storage?.getDirectory !== "function") {
        return unsupported("opfsSyncAccessHandle", "OPFS_UNAVAILABLE",
            "Origin-private filesystem access is unavailable.");
    }

    let root = null;
    let name = null;
    let created = false;
    let access = null;
    let failure = null;
    let stage = "entry";
    try {
        root = await storage.getDirectory();
        name = createProbeName();
        const file = await root.getFileHandle(name, { create: true });
        created = true;
        if (typeof file.createSyncAccessHandle !== "function") {
            failure = unsupported("opfsSyncAccessHandle",
                "SYNC_ACCESS_HANDLE_UNAVAILABLE",
                "Worker synchronous OPFS access handles are unavailable.");
        } else {
            stage = "open";
            access = await file.createSyncAccessHandle();
            stage = "operation";
            const size = access.getSize();
            if (!Number.isSafeInteger(size) || size < 0) {
                throw new Error("The synchronous access handle returned an invalid size.");
            }
        }
    } catch (error) {
        failure = unsupported("opfsSyncAccessHandle",
            stage === "open" ? "SYNC_ACCESS_HANDLE_OPEN_FAILED" :
                stage === "operation" ? "SYNC_ACCESS_HANDLE_OPERATION_FAILED" :
                    "OPFS_PROBE_ENTRY_FAILED",
            stage === "open" ? "Opening a Worker synchronous OPFS access handle failed." :
                stage === "operation" ? "A Worker synchronous OPFS operation failed." :
                    "Creating the temporary OPFS capability entry failed.",
            error);
    } finally {
        if (access) {
            try {
                access.close();
            } catch (error) {
                failure ??= unsupported("opfsSyncAccessHandle",
                    "SYNC_ACCESS_HANDLE_CLOSE_FAILED",
                    "Closing the Worker synchronous OPFS access handle failed.", error);
            }
        }
        if (created && root && name) {
            try {
                await root.removeEntry(name);
            } catch (error) {
                failure ??= unsupported("opfsSyncAccessHandle",
                    "OPFS_PROBE_CLEANUP_FAILED",
                    "Removing the temporary OPFS capability entry failed.", error);
            }
        }
    }
    return failure ?? supported("opfsSyncAccessHandle");
}

export function probeTransferredCanvas(canvas)
{
    if (typeof OffscreenCanvas !== "function" || !(canvas instanceof OffscreenCanvas)) {
        return unsupported("offscreenCanvas", "OFFSCREEN_CANVAS_TRANSFER_FAILED",
            "The temporary Worker did not receive an OffscreenCanvas.");
    }
    try {
        const context = canvas.getContext("webgl2");
        if (!context) {
            return unsupported("offscreenCanvas", "OFFSCREEN_WEBGL2_UNAVAILABLE",
                "The transferred OffscreenCanvas could not create a WebGL 2 context.");
        }
        context.clearColor(0, 0, 0, 0);
        context.clear(context.COLOR_BUFFER_BIT);
        return supported("offscreenCanvas");
    } catch (error) {
        return unsupported("offscreenCanvas", "OFFSCREEN_CANVAS_OPERATION_FAILED",
            "The transferred OffscreenCanvas operation failed.", error);
    }
}

const inWorker = typeof WorkerGlobalScope !== "undefined" &&
    globalThis instanceof WorkerGlobalScope;
if (inWorker) {
    globalThis.onmessage = async ({ data }) => {
        const offscreenCanvas = probeTransferredCanvas(data?.canvas);
        const opfsSyncAccess = await probeOpfsSyncAccess();
        globalThis.postMessage({
            type: PROBE_REPLY,
            offscreenCanvas,
            opfsSyncAccess,
        });
    };
}
