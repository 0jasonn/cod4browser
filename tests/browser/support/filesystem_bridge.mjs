export const WEB_FS_STATUS = Object.freeze({
    SUCCESS: 0,
    PENDING: 1,
    NOT_READY: 2,
    INVALID_ARGUMENT: 3,
    NO_REQUEST_SLOTS: 4,
    INVALID_RANGE: 5,
    NOT_FOUND: 6,
    STALE_SOURCE: 7,
    IO_ERROR: 8,
    PROTOCOL_ERROR: 9,
    CANCELLED: 10,
});

const BRIDGE_GLOBAL = "__KISAKCOD_WEB_FS_BRIDGE__";
const UINT32_MAX = 0xffff_ffff;

function isUint32(value, { allowZero = true } = {})
{
    return Number.isInteger(value) && value >= (allowZero ? 0 : 1) && value <= UINT32_MAX;
}

function statusForError(error)
{
    switch (error?.code) {
    case "NOT_READY":
        return WEB_FS_STATUS.NOT_READY;
    case "INVALID_SOURCE":
    case "UNSAFE_PATH":
        return WEB_FS_STATUS.INVALID_ARGUMENT;
    case "INVALID_RANGE":
        return WEB_FS_STATUS.INVALID_RANGE;
    case "STALE_SOURCE":
    case "PERSISTED_SIZE":
        return WEB_FS_STATUS.STALE_SOURCE;
    default:
        return error?.name === "NotFoundError"
            ? WEB_FS_STATUS.NOT_FOUND
            : WEB_FS_STATUS.IO_ERROR;
    }
}

export function installBrowserFilesystemBridge(module, assetStore)
{
    if (!module || typeof module !== "object" ||
        typeof module._KisakWeb_CompleteFsStat !== "function" ||
        typeof module._KisakWeb_CompleteFsRead !== "function") {
        throw new TypeError("The WebAssembly filesystem completion API is unavailable.");
    }
    if (!assetStore || typeof assetStore.openSource !== "function" ||
        typeof assetStore.readSource !== "function") {
        throw new TypeError("The browser asset store does not provide immutable read sources.");
    }

    const previousBridge = globalThis[BRIDGE_GLOBAL];
    if (previousBridge && typeof previousBridge.dispose === "function") {
        previousBridge.dispose();
    }

    const operations = new Map();
    const sources = new Map();
    let disposed = false;

    function isLive(operation)
    {
        return !disposed && operation.live &&
            operations.get(operation.requestId) === operation;
    }

    function beginOperation(requestId, kind)
    {
        if (disposed || !isUint32(requestId, { allowZero: false }) ||
            operations.has(requestId)) {
            return null;
        }
        const operation = { requestId, kind, live: true };
        operations.set(requestId, operation);
        return operation;
    }

    function takeOperation(operation)
    {
        if (!isLive(operation)) {
            return false;
        }
        operation.live = false;
        operations.delete(operation.requestId);
        return true;
    }

    function completeStat(operation, status, fileSize = 0)
    {
        if (!takeOperation(operation)) {
            return;
        }
        module._KisakWeb_CompleteFsStat(operation.requestId, status, fileSize);
    }

    function completeRead(operation, status, bytesRead = 0)
    {
        if (!takeOperation(operation)) {
            return;
        }
        module._KisakWeb_CompleteFsRead(operation.requestId, status, bytesRead);
    }

    function failOperation(operation, status)
    {
        if (operation.kind === "stat") {
            completeStat(operation, status);
        } else {
            completeRead(operation, status);
        }
    }

    function stat(requestId, path)
    {
        const operation = beginOperation(requestId, "stat");
        if (!operation) {
            return false;
        }

        void (async () => {
            try {
                if (typeof path !== "string" || path.length === 0) {
                    completeStat(operation, WEB_FS_STATUS.INVALID_ARGUMENT);
                    return;
                }
                const source = await assetStore.openSource(path);
                if (!isLive(operation)) {
                    return;
                }
                if (!isUint32(source?.size)) {
                    completeStat(operation, WEB_FS_STATUS.PROTOCOL_ERROR);
                    return;
                }
                sources.set(path, source);
                completeStat(operation, WEB_FS_STATUS.SUCCESS, source.size);
            } catch (error) {
                if (isLive(operation)) {
                    completeStat(operation, statusForError(error));
                }
            }
        })();
        return true;
    }

    function read(requestId, path, offset, length, destination, capacity)
    {
        const operation = beginOperation(requestId, "read");
        if (!operation) {
            return false;
        }

        void (async () => {
            try {
                if (typeof path !== "string" || path.length === 0 ||
                    !isUint32(offset) || !isUint32(length, { allowZero: false }) ||
                    !isUint32(destination, { allowZero: false }) || !isUint32(capacity) ||
                    length > capacity || offset > UINT32_MAX - length) {
                    completeRead(operation, WEB_FS_STATUS.INVALID_ARGUMENT);
                    return;
                }

                const source = sources.get(path);
                if (!source) {
                    completeRead(operation, WEB_FS_STATUS.NOT_READY);
                    return;
                }
                const bytes = await assetStore.readSource(source, { offset, length });
                if (!isLive(operation)) {
                    return;
                }
                if (!(bytes instanceof Uint8Array) || bytes.byteLength !== length) {
                    completeRead(operation, WEB_FS_STATUS.PROTOCOL_ERROR);
                    return;
                }

                // ALLOW_MEMORY_GROWTH can replace the heap view while OPFS is
                // awaiting.  Never retain a view captured before that await.
                const heap = module.HEAPU8;
                if (!(heap instanceof Uint8Array) || destination > heap.byteLength ||
                    bytes.byteLength > heap.byteLength - destination) {
                    completeRead(operation, WEB_FS_STATUS.PROTOCOL_ERROR);
                    return;
                }

                // JavaScript is single-threaded here: cancellation cannot run
                // between this final token check, the copy, and completion.
                if (!isLive(operation)) {
                    return;
                }
                heap.set(bytes, destination);
                completeRead(operation, WEB_FS_STATUS.SUCCESS, bytes.byteLength);
            } catch (error) {
                if (!isLive(operation)) {
                    return;
                }
                const status = statusForError(error);
                if (status === WEB_FS_STATUS.STALE_SOURCE) {
                    sources.delete(path);
                }
                completeRead(operation, status);
            }
        })();
        return true;
    }

    function cancel(requestId)
    {
        const operation = operations.get(requestId);
        if (operation) {
            operation.live = false;
            operations.delete(requestId);
        }

        // Absence is also a safe acknowledgement: either dispatch never
        // created a token, or completion/invalidation already retired it.
        return true;
    }

    function retireOperations(status)
    {
        const pending = Array.from(operations.values());
        for (const operation of pending) {
            failOperation(operation, status);
        }
    }

    function invalidate()
    {
        if (disposed) {
            return;
        }
        sources.clear();
        retireOperations(WEB_FS_STATUS.STALE_SOURCE);
    }

    function dispose()
    {
        if (disposed) {
            return;
        }
        sources.clear();
        retireOperations(WEB_FS_STATUS.CANCELLED);
        disposed = true;
        if (globalThis[BRIDGE_GLOBAL] === bridge) {
            delete globalThis[BRIDGE_GLOBAL];
        }
    }

    const bridge = Object.freeze({
        stat,
        read,
        cancel,
        invalidate,
        dispose,
    });
    globalThis[BRIDGE_GLOBAL] = bridge;
    return bridge;
}
