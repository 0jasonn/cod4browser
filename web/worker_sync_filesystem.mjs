const APP_DIRECTORY = "kisakcod-web";
const IMPORTS_DIRECTORY = "imports";
const HOME_DIRECTORY = "home";
const BRIDGE_GLOBAL = "__KISAKCOD_WEB_FS_BRIDGE__";
const SYNC_GLOBAL = "__KISAKCOD_SYNC_FS__";
const MAX_HOME_FILE_BYTES = 64 * 1024 * 1024;
const MAX_HOME_TOTAL_BYTES = 128 * 1024 * 1024;

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

async function childDirectory(root, segments, create = false)
{
    let current = root;
    for (const segment of segments) {
        current = await current.getDirectoryHandle(segment, { create });
    }
    return current;
}

export function createWorkerSyncFilesystem(faults = null)
{
    const files = new Map();
    const directories = new Set([""]);
    const directoryEntries = new Map([["", new Map()]]);
    const homeFiles = new Map();
    const homeDirectories = new Set([""]);
    const homeDirectoryEntries = new Map([["", new Map()]]);
    const descriptors = new Map();
    let nextDescriptor = 1;
    let mountedImport = null;
    let homeDirectory = null;
    let homeLoaded = false;
    let persistChain = Promise.resolve();
    const pendingPersistence = [];
    const persistenceObservers = new Set();
    let acceptingWrites = true;
    let flushPromise = null;
    let homeBytes = 0;
    let module = null;

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

    function addHomeDirectory(logicalPath)
    {
        if (homeDirectories.has(logicalPath)) return;
        const segments = logicalPath.split("/");
        const name = segments.pop();
        const parent = segments.join("/");
        addHomeDirectory(parent);
        if (homeFiles.has(logicalPath) ||
            homeDirectoryEntries.get(parent).has(name)) {
            throw new Error(`The browser home path contains a file/directory conflict: ${logicalPath}.`);
        }
        homeDirectories.add(logicalPath);
        homeDirectoryEntries.set(logicalPath, new Map());
        homeDirectoryEntries.get(parent).set(name, {
            name,
            type: "directory",
            size: 0,
        });
    }

    function publishHomeFile(file)
    {
        const segments = file.logicalPath.split("/");
        const name = segments.pop();
        const parent = segments.join("/");
        addHomeDirectory(parent);
        if (homeDirectories.has(file.logicalPath)) {
            throw new Error(`The browser home path contains a file/directory conflict: ${file.logicalPath}.`);
        }
        homeFiles.set(file.logicalPath, file);
        homeDirectoryEntries.get(parent).set(name, {
            name,
            type: "file",
            get size() { return file.size; },
        });
    }

    async function loadHomeDirectory(directory, relative = "", budget = { bytes: 0, files: 0 },
        report = null)
    {
        for await (const [name, handle] of directory.entries()) {
            const logicalPath = normalizeLogicalPath(
                relative ? `${relative}/${name}` : name);
            if (!logicalPath) continue;
            if (handle.kind === "directory") {
                addHomeDirectory(logicalPath);
                await loadHomeDirectory(handle, logicalPath, budget, report);
                continue;
            }
            if (handle.kind !== "file" || ++budget.files > 8191) {
                throw new Error("The browser home path exceeds its file-count limit.");
            }
            const blob = await handle.getFile();
            if (blob.size > MAX_HOME_FILE_BYTES ||
                budget.bytes + blob.size > MAX_HOME_TOTAL_BYTES) {
                throw new Error("The browser home path exceeds its storage recovery limit.");
            }
            const bytes = new Uint8Array(await blob.arrayBuffer());
            budget.bytes += bytes.byteLength;
            publishHomeFile({
                logicalPath,
                bytes,
                size: bytes.byteLength,
                version: 0,
                persistedVersion: 0,
            });
            report?.({
                phase: "recovering-home",
                filesProcessed: budget.files,
                bytesProcessed: budget.bytes,
            });
        }
        homeBytes = budget.bytes;
    }

    async function initializeHome(root, report)
    {
        if (homeLoaded) return;
        const appDirectory = await childDirectory(root, [APP_DIRECTORY], true);
        homeDirectory = await childDirectory(appDirectory, [HOME_DIRECTORY], true);
        await loadHomeDirectory(homeDirectory, "", { bytes: 0, files: 0 }, report);
        homeLoaded = true;
    }

    async function persistHomeFile(file, logicalPath, bytes, version)
    {
        if (!homeDirectory) return;
        await faults?.beforePersist?.(logicalPath);
        const segments = logicalPath.split("/");
        const name = segments.pop();
        const directory = await childDirectory(homeDirectory, segments, true);
        const handle = await directory.getFileHandle(name, { create: true });
        const writable = await handle.createWritable();
        await writable.write(bytes);
        await writable.truncate(bytes.byteLength);
        await writable.close();
        const current = homeFiles.get(logicalPath);
        if (current === file && file.logicalPath === logicalPath &&
            current.version === version) current.persistedVersion = version;
    }

    function drainPersistence()
    {
        persistChain = persistChain.catch(() => {}).then(async () => {
            while (pendingPersistence.length > 0) {
                const operation = pendingPersistence[0];
                await operation.run();
                pendingPersistence.shift();
                for (const observer of persistenceObservers) observer(operation.progress);
            }
        });
        return persistChain;
    }

    function schedulePersistence(run, progress = {
        phase: "persisting", files: 0, bytes: 0,
    })
    {
        const operation = { run, progress };
        pendingPersistence.push(operation);
        const running = drainPersistence();
        void running.catch(() => {});
        return running;
    }

    function scheduleHomeFilePersistence(file)
    {
        const logicalPath = file.logicalPath;
        const snapshot = file.bytes.slice(0, file.size);
        const version = file.version;
        return schedulePersistence(
            () => persistHomeFile(file, logicalPath, snapshot, version),
            { phase: "persisting", files: 1, bytes: snapshot.byteLength },
        );
    }

    async function drainPersistenceWithProgress(report)
    {
        let filesProcessed = 0;
        let bytesProcessed = 0;
        const observer = (progress) => {
            filesProcessed += progress.files;
            bytesProcessed += progress.bytes;
            report?.({
                phase: progress.phase,
                filesProcessed,
                bytesProcessed,
            });
        };
        persistenceObservers.add(observer);
        try {
            await drainPersistence();
        } finally {
            persistenceObservers.delete(observer);
        }
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

    async function mount(manifest, report = null)
    {
        if (!manifest || typeof manifest.importId !== "string" ||
            !Array.isArray(manifest.files)) {
            throw new TypeError("A validated import manifest is required.");
        }
        if (!navigator.storage?.getDirectory) {
            throw new Error("Worker OPFS is unavailable.");
        }

        closeAll();
        acceptingWrites = true;
        const totalBytes = manifest.files.reduce((sum, entry) =>
            sum + (Number.isSafeInteger(entry?.size) && entry.size >= 0 ? entry.size : 0), 0);
        report?.({
            phase: "preparing",
            filesProcessed: 0,
            bytesProcessed: 0,
            totalFiles: manifest.files.length,
            totalBytes,
        });
        const root = await navigator.storage.getDirectory();
        await initializeHome(root, report);
        const importDirectory = await childDirectory(root, [
            APP_DIRECTORY,
            IMPORTS_DIRECTORY,
            manifest.importId,
        ]);
        try {
            let filesProcessed = 0;
            let bytesProcessed = 0;
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
                ++filesProcessed;
                bytesProcessed += size;
                report?.({
                    phase: "mounting",
                    filesProcessed,
                    bytesProcessed,
                    totalFiles: manifest.files.length,
                    totalBytes,
                });
            }
            mountedImport = manifest.importId;
            report?.({
                phase: "complete",
                filesProcessed,
                bytesProcessed,
                totalFiles: manifest.files.length,
                totalBytes,
            });
            return { fileCount: files.size };
        } catch (error) {
            closeAll();
            throw error;
        }
    }

    function lookup(path)
    {
        const logicalPath = normalizeLogicalPath(path);
        return logicalPath
            ? homeFiles.get(logicalPath) ?? files.get(logicalPath) ?? null
            : null;
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
        if (file.bytes) {
            destination.set(file.bytes.subarray(offset, offset + readable));
            return readable;
        }
        return file.access.read(destination.subarray(0, readable), { at: offset });
    }

    function ensureHomeCapacity(file, required)
    {
        if (!Number.isSafeInteger(required) || required < 0 ||
            required > MAX_HOME_FILE_BYTES) return false;
        if (required <= file.bytes.byteLength) return true;
        let capacity = Math.max(256, file.bytes.byteLength);
        while (capacity < required) capacity = Math.min(MAX_HOME_FILE_BYTES, capacity * 2);
        const replacement = new Uint8Array(capacity);
        replacement.set(file.bytes.subarray(0, file.size));
        file.bytes = replacement;
        return true;
    }

    function openWritable(path, append)
    {
        if (!acceptingWrites) return -1;
        const logicalPath = normalizeLogicalPath(path);
        if (!logicalPath) return -1;
        const segments = logicalPath.split("/");
        const parent = segments.slice(0, -1).join("/");
        if (!homeDirectories.has(parent) || directories.has(logicalPath) ||
            homeDirectories.has(logicalPath)) return -1;
        let file = homeFiles.get(logicalPath);
        if (!file) {
            file = {
                logicalPath,
                bytes: new Uint8Array(256),
                size: 0,
                version: 0,
                persistedVersion: -1,
            };
            publishHomeFile(file);
            faults?.onDirty?.();
        } else if (!append) {
            homeBytes -= file.size;
            file.size = 0;
            ++file.version;
            faults?.onDirty?.();
        }
        const descriptor = nextDescriptor++;
        descriptors.set(descriptor, {
            file,
            position: append ? file.size : 0,
            writable: true,
        });
        return descriptor;
    }

    function removeHomePath(path)
    {
        if (!acceptingWrites) return false;
        const logicalPath = normalizeLogicalPath(path);
        const file = logicalPath ? homeFiles.get(logicalPath) : null;
        if (!file) return false;
        if ([...descriptors.values()].some((open) => open.file === file)) return false;
        homeFiles.delete(logicalPath);
        homeBytes -= file.size;
        const segments = logicalPath.split("/");
        const name = segments.pop();
        homeDirectoryEntries.get(segments.join("/"))?.delete(name);
        void schedulePersistence(async () => {
            if (!homeDirectory) return;
            try {
                const directory = await childDirectory(homeDirectory, segments);
                await directory.removeEntry(name);
            } catch (error) {
                if (error?.name !== "NotFoundError") throw error;
            }
        }, { phase: "removing", files: 1, bytes: 0 });
        faults?.onDirty?.();
        return true;
    }

    function renameHomePath(from, to)
    {
        if (!acceptingWrites) return false;
        const sourcePath = normalizeLogicalPath(from);
        const destinationPath = normalizeLogicalPath(to);
        const source = sourcePath ? homeFiles.get(sourcePath) : null;
        if (!source || !destinationPath || sourcePath === destinationPath ||
            [...descriptors.values()].some((open) => open.file === source)) return false;
        const destinationSegments = destinationPath.split("/");
        destinationSegments.pop();
        const destinationParent = destinationSegments.join("/");
        if (!homeDirectories.has(destinationParent) ||
            homeDirectories.has(destinationPath)) return false;
        if (homeFiles.has(destinationPath) &&
            !removeHomePath(destinationPath)) return false;
        const sourceSegments = sourcePath.split("/");
        const sourceName = sourceSegments.pop();
        homeFiles.delete(sourcePath);
        homeDirectoryEntries.get(sourceSegments.join("/"))?.delete(sourceName);
        source.logicalPath = destinationPath;
        ++source.version;
        publishHomeFile(source);
        scheduleHomeFilePersistence(source);
        // The destination snapshot is durable before the obsolete source is
        // removed from OPFS because both operations share persistChain.
        void schedulePersistence(async () => {
            if (!homeDirectory) return;
            try {
                const directory = await childDirectory(homeDirectory, sourceSegments);
                await directory.removeEntry(sourceName);
            } catch (error) {
                if (error?.name !== "NotFoundError") throw error;
            }
        }, { phase: "removing", files: 1, bytes: 0 });
        faults?.onDirty?.();
        return true;
    }

    function scheduleDirtyOpenFiles()
    {
        const dirty = new Set();
        for (const open of descriptors.values()) {
            if (open.writable && open.file.version !== open.file.persistedVersion) {
                dirty.add(open.file);
            }
        }
        for (const file of dirty) scheduleHomeFilePersistence(file);
    }

    function persistenceSummary()
    {
        const persisted = [...homeFiles.values()].filter(
            (file) => file.persistedVersion === file.version);
        return {
            filesPersisted: persisted.length,
            bytesPersisted: persisted.reduce((sum, file) => sum + file.size, 0),
        };
    }

    async function checkpoint(report = null)
    {
        report?.({ phase: "snapshotting", filesProcessed: 0, bytesProcessed: 0 });
        scheduleDirtyOpenFiles();
        await drainPersistenceWithProgress(report);
        const summary = persistenceSummary();
        report?.({
            phase: "complete",
            filesProcessed: summary.filesPersisted,
            bytesProcessed: summary.bytesPersisted,
        });
        return summary;
    }

    function flushAndUnmount(report = null)
    {
        if (flushPromise) return flushPromise;
        acceptingWrites = false;
        flushPromise = (async () => {
            report?.({ phase: "snapshotting", filesProcessed: 0, bytesProcessed: 0 });
            scheduleDirtyOpenFiles();
            await drainPersistenceWithProgress(report);
            const summary = persistenceSummary();
            report?.({
                phase: "closing-handles",
                filesProcessed: summary.filesPersisted,
                bytesProcessed: summary.bytesPersisted,
            });
            closeAll();
            report?.({
                phase: "unmounting",
                filesProcessed: summary.filesPersisted,
                bytesProcessed: summary.bytesPersisted,
            });
            report?.({
                phase: "complete",
                filesProcessed: summary.filesPersisted,
                bytesProcessed: summary.bytesPersisted,
            });
            return summary;
        })();
        return flushPromise.finally(() => { flushPromise = null; });
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
                if (faults?.reject?.("read", normalizeLogicalPath(path))) {
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
                const file = homeFiles.get(logicalPath) ?? files.get(logicalPath);
                if (file) return { type: "file", size: file.size };
                if (homeDirectories.has(logicalPath) || directories.has(logicalPath)) {
                    return { type: "directory", size: 0 };
                }
                return null;
            },
            list(path) {
                const logicalPath = normalizeDirectoryPath(path);
                if (logicalPath === null) return null;
                if (faults?.reject?.("list", logicalPath)) return null;
                const importedEntries = directoryEntries.get(logicalPath);
                const writableEntries = homeDirectoryEntries.get(logicalPath);
                if (!importedEntries && !writableEntries) return null;
                const merged = new Map(importedEntries ?? []);
                for (const [name, entry] of writableEntries ?? []) merged.set(name, entry);
                return [...merged.values()].map((entry) => ({
                    name: entry.name,
                    type: entry.type,
                    size: entry.size,
                })).sort((left, right) =>
                    left.name.localeCompare(right.name, "en-US"));
            },
            open(path) {
                const file = lookup(path);
                if (!file || faults?.reject?.("open", file.logicalPath)) return -1;
                const descriptor = nextDescriptor++;
                descriptors.set(descriptor, { file, position: 0, writable: false });
                return descriptor;
            },
            openWrite(path, append = false) {
                return openWritable(path, Boolean(append));
            },
            size(descriptor) {
                return descriptors.get(descriptor)?.file.size ?? -1;
            },
            seek(descriptor, offset) {
                const open = descriptors.get(descriptor);
                if (!open || !Number.isSafeInteger(offset) || offset < 0 || offset > open.file.size) {
                    return false;
                }
                if (faults?.reject?.("seek", open.file.logicalPath)) return false;
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
                if (faults?.reject?.("sync-read", open.file.logicalPath)) return -1;
                const bytesRead = readMounted(
                    open.file,
                    open.position,
                    module.HEAPU8.subarray(destination, destination + length),
                );
                if (bytesRead >= 0) open.position += bytesRead;
                return bytesRead;
            },
            write(descriptor, source, length) {
                if (!acceptingWrites) return -1;
                const open = descriptors.get(descriptor);
                if (!open?.writable || !Number.isInteger(source) || source <= 0 ||
                    !Number.isInteger(length) || length < 0 ||
                    source > module.HEAPU8.byteLength ||
                    length > module.HEAPU8.byteLength - source) return -1;
                const required = open.position + length;
                const growth = Math.max(0, required - open.file.size);
                if (homeBytes + growth > MAX_HOME_TOTAL_BYTES ||
                    !ensureHomeCapacity(open.file, required)) return -1;
                open.file.bytes.set(
                    module.HEAPU8.subarray(source, source + length), open.position);
                open.position = required;
                open.file.size = Math.max(open.file.size, required);
                homeBytes += growth;
                ++open.file.version;
                faults?.onDirty?.();
                return length;
            },
            close(descriptor) {
                const open = descriptors.get(descriptor);
                if (!open) return false;
                descriptors.delete(descriptor);
                if (open.writable) scheduleHomeFilePersistence(open.file);
                return true;
            },
            mkdir(path) {
                if (!acceptingWrites) return false;
                const logicalPath = normalizeDirectoryPath(path);
                if (logicalPath === null || files.has(logicalPath) ||
                    homeFiles.has(logicalPath)) return false;
                addHomeDirectory(logicalPath);
                void schedulePersistence(() => childDirectory(
                    homeDirectory,
                    logicalPath ? logicalPath.split("/") : [],
                    true,
                ), { phase: "persisting", files: 1, bytes: 0 });
                faults?.onDirty?.();
                return true;
            },
            remove(path) { return removeHomePath(path); },
            rename(from, to) { return renameHomePath(from, to); },
        });
    }

    return Object.freeze({
        mount,
        unmount: closeAll,
        checkpoint,
        flushAndUnmount,
        installForModule,
    });
}
