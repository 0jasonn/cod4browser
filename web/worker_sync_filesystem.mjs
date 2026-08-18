const APP_DIRECTORY = "kisakcod-web";
const IMPORTS_DIRECTORY = "imports";
const BRIDGE_GLOBAL = "__KISAKCOD_WEB_FS_BRIDGE__";
const SYNC_GLOBAL = "__KISAKCOD_SYNC_FS__";

const STATUS = Object.freeze({
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

function normalizeLogicalPath(path)
{
    if (typeof path !== "string" || path.length === 0 || path.includes("\0")) {
        return null;
    }
    const normalized = path.replaceAll("\\", "/").replace(/^\.\//u, "");
    const segments = normalized.split("/");
    if (normalized.startsWith("/") || /^[a-z]:\//iu.test(normalized) ||
        segments.some((segment) => !segment || segment === "." || segment === "..")) {
        return null;
    }
    return segments.join("/").toLocaleLowerCase("en-US");
}

async function childDirectory(root, segments)
{
    let current = root;
    for (const segment of segments) {
        current = await current.getDirectoryHandle(segment);
    }
    return current;
}

export function createWorkerSyncFilesystem()
{
    const files = new Map();
    const directories = new Set([""]);
    const directoryEntries = new Map([["", new Map()]]);
    const descriptors = new Map();
    let nextDescriptor = 1;
    let mountedImport = null;
    let module = null;
    const testControl = Object.create(null);

    function closeAll()
    {
        descriptors.clear();
        for (const file of files.values()) {
            try {
                file.access.close();
            } catch {
                // A closing Worker or a revoked storage bucket may already
                // have invalidated the handle.
            }
        }
        files.clear();
        directories.clear();
        directories.add("");
        directoryEntries.clear();
        directoryEntries.set("", new Map());
        mountedImport = null;
    }

    function addDirectory(logicalPath)
    {
        if (directories.has(logicalPath)) return;
        const segments = logicalPath.split("/");
        const name = segments.pop();
        const parent = segments.join("/");
        addDirectory(parent);
        if (files.has(logicalPath) || directoryEntries.get(parent).has(name)) {
            throw new Error(`The validated manifest contains a file/directory conflict: ${logicalPath}.`);
        }
        directories.add(logicalPath);
        directoryEntries.set(logicalPath, new Map());
        directoryEntries.get(parent).set(name, Object.freeze({
            name,
            type: "directory",
            size: 0,
        }));
    }

    function addFileEntry(file)
    {
        const segments = file.logicalPath.split("/");
        const name = segments.pop();
        const parent = segments.join("/");
        addDirectory(parent);
        if (directories.has(file.logicalPath) || files.has(file.logicalPath) ||
            directoryEntries.get(parent).has(name)) {
            throw new Error(`The validated manifest contains a duplicate path: ${file.logicalPath}.`);
        }
        directoryEntries.get(parent).set(name, Object.freeze({
            name,
            type: "file",
            size: file.size,
        }));
    }

    async function mount(manifest)
    {
        if (!manifest || typeof manifest.importId !== "string" ||
            !Array.isArray(manifest.files)) {
            throw new TypeError("A validated import manifest is required.");
        }
        if (!navigator.storage?.getDirectory) {
            throw new Error("Worker OPFS is unavailable.");
        }

        closeAll();
        const root = await navigator.storage.getDirectory();
        const importDirectory = await childDirectory(root, [
            APP_DIRECTORY,
            IMPORTS_DIRECTORY,
            manifest.importId,
        ]);
        try {
            for (const entry of manifest.files) {
                const logicalPath = normalizeLogicalPath(entry?.path);
                if (!logicalPath || !Number.isSafeInteger(entry?.size) || entry.size < 0) {
                    throw new Error("The validated manifest contains an invalid file entry.");
                }
                const segments = logicalPath.split("/");
                const directory = await childDirectory(importDirectory, segments.slice(0, -1));
                const handle = await directory.getFileHandle(segments.at(-1));
                if (typeof handle.createSyncAccessHandle !== "function") {
                    throw new Error("Synchronous OPFS access handles are unavailable in this Worker.");
                }
                // The engine never writes imported installation data.  A read-only
                // access handle preserves that contract and, on implementations
                // which support the mode option, permits other engine tabs to hold
                // the same imported file concurrently.
                let access;
                try {
                    access = await handle.createSyncAccessHandle({ mode: "read-only" });
                } catch (error) {
                    if (!(error instanceof TypeError)) throw error;
                    access = await handle.createSyncAccessHandle();
                }
                const size = access.getSize();
                if (size !== entry.size) {
                    access.close();
                    throw new Error(`Persisted size changed for ${logicalPath}.`);
                }
                const mounted = Object.freeze({ logicalPath, size, access });
                try {
                    addFileEntry(mounted);
                } catch (error) {
                    access.close();
                    throw error;
                }
                files.set(logicalPath, mounted);
            }
            mountedImport = manifest.importId;
            return { fileCount: files.size };
        } catch (error) {
            closeAll();
            throw error;
        }
    }

    function lookup(path)
    {
        const logicalPath = normalizeLogicalPath(path);
        return logicalPath ? files.get(logicalPath) ?? null : null;
    }

    function normalizeDirectoryPath(path)
    {
        if (typeof path !== "string" || path.includes("\0")) return null;
        let normalized = path.replaceAll("\\", "/");
        while (normalized.startsWith("./")) normalized = normalized.slice(2);
        normalized = normalized.replace(/\/+$/u, "");
        if (normalized === "" || normalized === ".") return "";
        return normalizeLogicalPath(normalized);
    }

    function readMounted(file, offset, destination)
    {
        if (!Number.isSafeInteger(offset) || offset < 0 ||
            offset > file.size) {
            return -1;
        }
        const readable = Math.min(destination.byteLength, file.size - offset);
        if (readable === 0) return 0;
        return file.access.read(destination.subarray(0, readable), { at: offset });
    }

    function controlledPath(name, logicalPath)
    {
        const configured = normalizeLogicalPath(testControl[name] ?? "");
        return configured !== null && configured !== "" && configured === logicalPath;
    }

    function installForModule(wasmModule)
    {
        module = wasmModule;
        globalThis[BRIDGE_GLOBAL] = Object.freeze({
            stat(requestId, path) {
                const file = lookup(path);
                module._KisakWeb_CompleteFsStat(
                    requestId >>> 0,
                    file ? STATUS.SUCCESS : mountedImport ? STATUS.NOT_FOUND : STATUS.NOT_READY,
                    file?.size ?? 0,
                );
                return true;
            },
            read(requestId, path, offset, length, destination, capacity) {
                const file = lookup(path);
                if (!file) {
                    module._KisakWeb_CompleteFsRead(
                        requestId >>> 0,
                        mountedImport ? STATUS.NOT_FOUND : STATUS.NOT_READY,
                        0,
                    );
                    return true;
                }
                if (normalizeLogicalPath(path) ===
                    normalizeLogicalPath(testControl.failReadPath ?? "")) {
                    module._KisakWeb_CompleteFsRead(requestId >>> 0, STATUS.IO_ERROR, 0);
                    return true;
                }
                if (!Number.isInteger(length) || length <= 0 || length > capacity ||
                    destination <= 0 || destination > module.HEAPU8.byteLength ||
                    length > module.HEAPU8.byteLength - destination) {
                    module._KisakWeb_CompleteFsRead(requestId >>> 0, STATUS.INVALID_ARGUMENT, 0);
                    return true;
                }
                const view = module.HEAPU8.subarray(destination, destination + length);
                const bytesRead = readMounted(file, offset >>> 0, view);
                module._KisakWeb_CompleteFsRead(
                    requestId >>> 0,
                    bytesRead === length ? STATUS.SUCCESS :
                        bytesRead < 0 ? STATUS.INVALID_RANGE : STATUS.IO_ERROR,
                    Math.max(0, bytesRead),
                );
                return true;
            },
            cancel() { return true; },
            invalidate: closeAll,
            dispose: closeAll,
        });

        globalThis[SYNC_GLOBAL] = Object.freeze({
            stat(path) {
                const logicalPath = normalizeDirectoryPath(path);
                if (logicalPath === null) return null;
                const file = files.get(logicalPath);
                if (file) return { type: "file", size: file.size };
                if (directories.has(logicalPath)) return { type: "directory", size: 0 };
                return null;
            },
            list(path) {
                const logicalPath = normalizeDirectoryPath(path);
                if (logicalPath === null) return null;
                if (controlledPath("failSyncListPath", logicalPath)) return null;
                const entries = directoryEntries.get(logicalPath);
                if (!entries) return null;
                return [...entries.values()].sort((left, right) =>
                    left.name.localeCompare(right.name, "en-US"));
            },
            open(path) {
                const file = lookup(path);
                if (!file || controlledPath("failSyncOpenPath", file.logicalPath)) return -1;
                const descriptor = nextDescriptor++;
                descriptors.set(descriptor, { file, position: 0 });
                return descriptor;
            },
            size(descriptor) {
                return descriptors.get(descriptor)?.file.size ?? -1;
            },
            seek(descriptor, offset) {
                const open = descriptors.get(descriptor);
                if (!open || !Number.isSafeInteger(offset) || offset < 0 || offset > open.file.size) {
                    return false;
                }
                if (controlledPath("failSyncSeekPath", open.file.logicalPath)) return false;
                open.position = offset;
                return true;
            },
            read(descriptor, destination, length) {
                const open = descriptors.get(descriptor);
                if (!open || !Number.isInteger(destination) || destination <= 0 ||
                    !Number.isInteger(length) || length < 0 ||
                    destination > module.HEAPU8.byteLength ||
                    length > module.HEAPU8.byteLength - destination) {
                    return -1;
                }
                if (controlledPath("failSyncReadPath", open.file.logicalPath)) return -1;
                const bytesRead = readMounted(
                    open.file,
                    open.position,
                    module.HEAPU8.subarray(destination, destination + length),
                );
                if (bytesRead >= 0) open.position += bytesRead;
                return bytesRead;
            },
            close(descriptor) {
                return descriptors.delete(descriptor);
            },
        });
    }

    function setTestControl(values)
    {
        Object.assign(testControl, values);
    }

    return Object.freeze({ mount, unmount: closeAll, installForModule, setTestControl });
}
