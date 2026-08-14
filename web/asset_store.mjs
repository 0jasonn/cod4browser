const DATABASE_NAME = "kisakcod-web";
const DATABASE_VERSION = 1;
const METADATA_STORE = "metadata";
const ACTIVE_IMPORT_KEY = "active-import";
const APP_DIRECTORY = "kisakcod-web";
const IMPORTS_DIRECTORY = "imports";
const STORAGE_LOCK_NAME = "kisakcod-web-assets";
const STORAGE_CHANNEL_NAME = "kisakcod-web-assets";
const MANIFEST_SCHEMA = 1;
const MAX_ADAPTER_READ = 1024 * 1024;
const PROBE_WINDOW_SIZE = 4096;
const ZIP_TAIL_WINDOW_SIZE = 22 + 0xffff;

export const REQUIRED_ASSETS = Object.freeze([
    Object.freeze({
        path: "localization.txt",
        minimumSize: 1,
        maximumSize: 4095,
        label: "Localization configuration",
    }),
    Object.freeze({
        path: "main/iw_00.iwd",
        minimumSize: 100,
        maximumSize: 512 * 1024 * 1024,
        label: "Base asset archive",
    }),
]);

const PROBE_ERRORS = new Map([
    [1, "The browser passed an invalid probe window to WebAssembly."],
    [2, "The selected file is too large for this ZIP32 milestone."],
    [10, "localization.txt must fit the engine's 4 KiB localization buffer."],
    [11, "localization.txt is empty, malformed UTF-8, or has no localization entries."],
    [12, "localization.txt names a language this engine build does not support."],
    [20, "main/iw_00.iwd does not begin with a complete ZIP local-file header."],
    [21, "main/iw_00.iwd has no valid ZIP end-of-central-directory record."],
    [22, "Multi-disk ZIP archives are not supported for IWD files."],
    [23, "ZIP64 archives are outside this milestone's IWD boundary."],
    [24, "main/iw_00.iwd contains no archive entries."],
    [25, "main/iw_00.iwd declares an invalid central-directory range."],
    [26, "The IWD central-directory probe window is inconsistent."],
    [27, "main/iw_00.iwd has an invalid central-directory header."],
    [28, "The first IWD local and central-directory entries do not agree."],
    [29, "Encrypted IWD entries are not supported."],
    [30, "The first IWD entry uses an unsupported compression method."],
]);

export class AssetImportError extends Error
{
    constructor(code, message, options = {})
    {
        super(message, options);
        this.name = "AssetImportError";
        this.code = code;
    }
}

function importError(code, message, cause)
{
    return new AssetImportError(code, message, cause ? { cause } : undefined);
}

function normalizeSelectionPath(rawPath)
{
    if (typeof rawPath !== "string" || rawPath.length === 0) {
        throw importError("UNSAFE_PATH", "The selected folder contains an empty file path.");
    }
    const path = rawPath.replaceAll("\\", "/");
    if (path.startsWith("/") || /^[a-z]:\//i.test(path)) {
        throw importError("UNSAFE_PATH", "Absolute paths are not accepted from asset selections.");
    }
    const segments = path.split("/");
    if (segments.some((segment) =>
        segment.length === 0 || segment === "." || segment === ".." ||
        /[\u0000-\u001f\u007f]/u.test(segment))) {
        throw importError("UNSAFE_PATH", "The selected folder contains an unsafe relative path.");
    }
    return segments.join("/");
}

function requireFileLike(file, path)
{
    if (!file || !Number.isSafeInteger(file.size) || file.size < 0 ||
        typeof file.slice !== "function" || typeof file.stream !== "function") {
        throw importError("INVALID_FILE", `${path} is not a readable browser File.`);
    }
    return file;
}

export function entriesFromFileList(fileList)
{
    const files = Array.from(fileList ?? []);
    if (files.length === 0) {
        return null;
    }

    let selectedRoot = null;
    const selectedByPath = new Map();
    for (const file of files) {
        if (!file.webkitRelativePath) {
            throw importError(
                "MISSING_RELATIVE_PATH",
                "This browser did not preserve folder-relative paths for the selection.",
            );
        }
        const selectionPath = normalizeSelectionPath(file.webkitRelativePath);
        const segments = selectionPath.split("/");
        if (segments.length < 2) {
            throw importError("INVALID_ROOT", "Select the installation folder itself.");
        }
        const root = segments.shift().toLocaleLowerCase("en-US");
        if (selectedRoot === null) {
            selectedRoot = root;
        } else if (selectedRoot !== root) {
            throw importError(
                "INVALID_ROOT",
                "Select one installation folder, not a parent containing multiple folders.",
            );
        }

        const relativePath = segments.join("/");
        const foldedPath = relativePath.toLocaleLowerCase("en-US");
        if (!REQUIRED_ASSETS.some((requirement) => requirement.path === foldedPath)) {
            continue;
        }
        if (selectedByPath.has(foldedPath)) {
            throw importError(
                "DUPLICATE_PATH",
                `The selected folder contains conflicting paths for ${relativePath}.`,
            );
        }
        selectedByPath.set(foldedPath, file);
    }

    const localization = selectedByPath.get("localization.txt");
    if (!localization) {
        throw importError(
            "MISSING_MANIFEST",
            "Select the Call of Duty 4 installation folder containing localization.txt.",
        );
    }
    const archive = selectedByPath.get("main/iw_00.iwd");
    if (!archive) {
        throw importError(
            "MISSING_MANIFEST",
            "The selected installation is missing main/iw_00.iwd.",
        );
    }

    return new Map([
        ["localization.txt", requireFileLike(localization, "localization.txt")],
        ["main/iw_00.iwd", requireFileLike(archive, "main/iw_00.iwd")],
    ]);
}

async function getRequiredHandle(directory, kind, name)
{
    try {
        return kind === "directory"
            ? await directory.getDirectoryHandle(name)
            : await directory.getFileHandle(name);
    } catch (error) {
        if (error?.name === "NotFoundError") {
            throw importError(
                "MISSING_MANIFEST",
                `The selected installation is missing ${name}.`,
                error,
            );
        }
        throw error;
    }
}

export async function entriesFromDirectoryHandle(directory)
{
    if (!directory || typeof directory.getFileHandle !== "function" ||
        typeof directory.getDirectoryHandle !== "function") {
        throw importError("INVALID_PICKER", "The browser returned an invalid directory handle.");
    }
    const localizationHandle = await getRequiredHandle(directory, "file", "localization.txt");
    const mainHandle = await getRequiredHandle(directory, "directory", "main");
    const archiveHandle = await getRequiredHandle(mainHandle, "file", "iw_00.iwd");
    const [localization, archive] = await Promise.all([
        localizationHandle.getFile(),
        archiveHandle.getFile(),
    ]);
    return new Map([
        ["localization.txt", requireFileLike(localization, "localization.txt")],
        ["main/iw_00.iwd", requireFileLike(archive, "main/iw_00.iwd")],
    ]);
}

function selectFromInput(input)
{
    if (!(input instanceof HTMLInputElement) || input.type !== "file") {
        throw importError("INVALID_PICKER", "The portable folder picker is unavailable.");
    }
    return new Promise((resolve, reject) => {
        const finish = (callback) => {
            input.removeEventListener("change", handleChange);
            input.removeEventListener("cancel", handleCancel);
            callback();
        };
        const handleChange = () => finish(() => {
            try {
                resolve(entriesFromFileList(input.files));
            } catch (error) {
                reject(error);
            } finally {
                input.value = "";
            }
        });
        const handleCancel = () => finish(() => resolve(null));
        input.addEventListener("change", handleChange, { once: true });
        input.addEventListener("cancel", handleCancel, { once: true });
        input.click();
    });
}

export async function selectInstallEntries(fallbackInput)
{
    if (typeof globalThis.showDirectoryPicker === "function") {
        try {
            const directory = await globalThis.showDirectoryPicker({
                id: "kisakcod-install",
                mode: "read",
            });
            return await entriesFromDirectoryHandle(directory);
        } catch (error) {
            if (error?.name === "AbortError") {
                return null;
            }
            throw error;
        }
    }
    return selectFromInput(fallbackInput);
}

function requestResult(request)
{
    return new Promise((resolve, reject) => {
        request.addEventListener("success", () => resolve(request.result), { once: true });
        request.addEventListener("error", () => reject(request.error), { once: true });
    });
}

function transactionResult(transaction)
{
    return new Promise((resolve, reject) => {
        transaction.addEventListener("complete", resolve, { once: true });
        transaction.addEventListener("abort", () => reject(transaction.error), { once: true });
        transaction.addEventListener("error", () => reject(transaction.error), { once: true });
    });
}

function openDatabase()
{
    return new Promise((resolve, reject) => {
        const request = indexedDB.open(DATABASE_NAME, DATABASE_VERSION);
        request.addEventListener("upgradeneeded", () => {
            if (!request.result.objectStoreNames.contains(METADATA_STORE)) {
                request.result.createObjectStore(METADATA_STORE);
            }
        });
        request.addEventListener("success", () => resolve(request.result), { once: true });
        request.addEventListener("error", () => reject(request.error), { once: true });
        request.addEventListener("blocked", () => reject(
            importError("DATABASE_BLOCKED", "Asset metadata storage is blocked by another tab."),
        ), { once: true });
    });
}

async function databaseGet(database, key)
{
    const transaction = database.transaction(METADATA_STORE, "readonly");
    const completed = transactionResult(transaction);
    const value = await requestResult(transaction.objectStore(METADATA_STORE).get(key));
    await completed;
    return value;
}

async function databasePut(database, key, value)
{
    const transaction = database.transaction(METADATA_STORE, "readwrite");
    const completed = transactionResult(transaction);
    transaction.objectStore(METADATA_STORE).put(value, key);
    await completed;
}

async function databaseDelete(database, key)
{
    const transaction = database.transaction(METADATA_STORE, "readwrite");
    const completed = transactionResult(transaction);
    transaction.objectStore(METADATA_STORE).delete(key);
    await completed;
}

function validateImportId(importId)
{
    if (typeof importId !== "string" ||
        !/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(importId)) {
        throw importError("INVALID_METADATA", "Stored asset metadata contains an invalid import ID.");
    }
    return importId;
}

function validateStoredManifest(manifest)
{
    if (!manifest || typeof manifest !== "object" || Array.isArray(manifest) ||
        manifest.schema !== MANIFEST_SCHEMA || !Array.isArray(manifest.files) ||
        manifest.files.length !== REQUIRED_ASSETS.length) {
        throw importError("INVALID_METADATA", "Stored asset metadata has an invalid shape.");
    }
    const importId = validateImportId(manifest.importId);
    const sizes = new Map();
    for (const entry of manifest.files) {
        if (!entry || typeof entry !== "object" || Array.isArray(entry) ||
            typeof entry.path !== "string" || !Number.isSafeInteger(entry.size)) {
            throw importError("INVALID_METADATA", "Stored asset file metadata is malformed.");
        }
        const requirement = REQUIRED_ASSETS.find(({ path }) => path === entry.path);
        if (!requirement || sizes.has(entry.path) || entry.size < requirement.minimumSize ||
            entry.size > requirement.maximumSize) {
            throw importError("INVALID_METADATA", "Stored asset file metadata is inconsistent.");
        }
        sizes.set(entry.path, entry.size);
    }
    if (REQUIRED_ASSETS.some(({ path }) => !sizes.has(path))) {
        throw importError("INVALID_METADATA", "Stored asset metadata is incomplete.");
    }
    return { importId, sizes };
}

function validateSelectedEntries(entries)
{
    if (!(entries instanceof Map)) {
        throw importError("INVALID_SELECTION", "The asset selection is not a canonical file map.");
    }
    let totalSize = 0;
    for (const requirement of REQUIRED_ASSETS) {
        const file = requireFileLike(entries.get(requirement.path), requirement.path);
        if (file.size < requirement.minimumSize || file.size > requirement.maximumSize) {
            throw importError(
                "INVALID_SIZE",
                `${requirement.path} has an unexpected size (${file.size.toLocaleString()} bytes).`,
            );
        }
        totalSize += file.size;
    }
    return totalSize;
}

function findEocd(tail)
{
    const view = new DataView(tail.buffer, tail.byteOffset, tail.byteLength);
    for (let position = tail.byteLength - 22; position >= 0; position -= 1) {
        if (view.getUint32(position, true) !== 0x06054b50) {
            continue;
        }
        const commentLength = view.getUint16(position + 20, true);
        if (position + 22 + commentLength === tail.byteLength) {
            return {
                centralSize: view.getUint32(position + 12, true),
                centralOffset: view.getUint32(position + 16, true),
                entriesDeclared: view.getUint16(position + 10, true),
            };
        }
    }
    return null;
}

async function readWindow(file, offset, length)
{
    if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(length) ||
        offset < 0 || length < 0 || offset > file.size) {
        throw importError("INVALID_RANGE", "An invalid persistent-file read was requested.");
    }
    return new Uint8Array(await file.slice(offset, Math.min(file.size, offset + length)).arrayBuffer());
}

function callWasmProbe(module, functionName, buffers, buildArguments)
{
    const probe = module?.[functionName];
    if (typeof probe !== "function" || typeof module?._malloc !== "function" ||
        typeof module?._free !== "function") {
        throw importError("WASM_API", "The WebAssembly asset-probe API is unavailable.");
    }

    const allocations = [];
    try {
        for (const buffer of buffers) {
            const pointer = module._malloc(Math.max(1, buffer.byteLength));
            if (!pointer) {
                throw importError("WASM_MEMORY", "WebAssembly could not allocate an asset probe window.");
            }
            allocations.push({ pointer, buffer });
        }
        const heap = module.HEAPU8;
        if (!heap) {
            throw importError("WASM_API", "The WebAssembly byte heap is unavailable.");
        }
        for (const { pointer, buffer } of allocations) {
            if (buffer.byteLength > 0) {
                heap.set(buffer, pointer);
            }
        }
        const pointers = allocations.map(({ pointer }) => pointer);
        return probe(...buildArguments(pointers));
    } finally {
        for (const { pointer } of allocations) {
            module._free(pointer);
        }
    }
}

function assertProbeSucceeded(result)
{
    if (result !== 0) {
        throw importError(
            `PROBE_${result}`,
            PROBE_ERRORS.get(result) ?? `The WebAssembly asset probe failed with code ${result}.`,
        );
    }
}

async function probeLocalization(module, file)
{
    const bytes = await readWindow(file, 0, file.size);
    const result = callWasmProbe(
        module,
        "_KisakWeb_ProbeLocalization",
        [bytes],
        ([data]) => [data, bytes.byteLength, file.size],
    );
    assertProbeSucceeded(result);
    const text = new TextDecoder("utf-8", { fatal: true }).decode(bytes);
    return text.split(/\r?\n/u, 1)[0];
}

async function probeIwd(module, file)
{
    const headLength = Math.min(file.size, PROBE_WINDOW_SIZE);
    const tailLength = Math.min(file.size, ZIP_TAIL_WINDOW_SIZE);
    const tailOffset = file.size - tailLength;
    const [head, tail] = await Promise.all([
        readWindow(file, 0, headLength),
        readWindow(file, tailOffset, tailLength),
    ]);
    const eocd = findEocd(tail);
    const centralOffset = eocd?.centralOffset ?? 0;
    const requestedCentralLength = Math.min(eocd?.centralSize ?? 0, PROBE_WINDOW_SIZE);
    const central = centralOffset <= file.size
        ? await readWindow(file, centralOffset, requestedCentralLength)
        : new Uint8Array();
    const result = callWasmProbe(
        module,
        "_KisakWeb_ProbeIwd",
        [head, tail, central],
        ([headPointer, tailPointer, centralPointer]) => [
            headPointer,
            head.byteLength,
            tailPointer,
            tail.byteLength,
            tailOffset,
            centralPointer,
            central.byteLength,
            centralOffset,
            file.size,
        ],
    );
    assertProbeSucceeded(result);
    return {
        entriesDeclared: eocd?.entriesDeclared ?? 0,
    };
}

async function copyFileToHandle(source, destinationHandle, reportBytes)
{
    const writable = await destinationHandle.createWritable({ keepExistingData: false });
    const reader = source.stream().getReader();
    try {
        while (true) {
            const { done, value } = await reader.read();
            if (done) {
                break;
            }
            await writable.write(value);
            reportBytes(value.byteLength);
        }
        await writable.close();
    } catch (error) {
        await reader.cancel(error).catch(() => {});
        if (typeof writable.abort === "function") {
            await writable.abort().catch(() => {});
        }
        throw error;
    } finally {
        reader.releaseLock();
    }
}

export function createBrowserAssetStore(module, { onState = () => {} } = {})
{
    let database = null;
    let importsRoot = null;
    let activeManifest = null;
    let persistenceGranted = null;
    let operationActive = false;
    let pendingExternalSync = false;
    const readSources = new WeakSet();
    const storageChannel = typeof BroadcastChannel === "function"
        ? new BroadcastChannel(STORAGE_CHANNEL_NAME)
        : null;

    const emit = (detail) => onState({
        persistenceGranted,
        ...detail,
    });

    async function withStorageLock(callback, mode = "exclusive")
    {
        return navigator.locks.request(
            STORAGE_LOCK_NAME,
            { mode },
            callback,
        );
    }

    function broadcastChange()
    {
        storageChannel?.postMessage({ type: "changed" });
    }

    function scheduleExternalSync()
    {
        if (!pendingExternalSync || operationActive) {
            return;
        }
        pendingExternalSync = false;
        queueMicrotask(() => initialize().catch(() => {}));
    }

    async function ensureBackend()
    {
        if (!globalThis.indexedDB || typeof navigator.storage?.getDirectory !== "function" ||
            typeof navigator.locks?.request !== "function") {
            throw importError(
                "STORAGE_UNSUPPORTED",
                "This browser does not provide OPFS, IndexedDB, and Web Locks for safe local imports.",
            );
        }
        database ??= await openDatabase();
        if (!importsRoot) {
            const root = await navigator.storage.getDirectory();
            const appRoot = await root.getDirectoryHandle(APP_DIRECTORY, { create: true });
            importsRoot = await appRoot.getDirectoryHandle(IMPORTS_DIRECTORY, { create: true });
        }
    }

    async function importDirectory(importId, create = false)
    {
        validateImportId(importId);
        return importsRoot.getDirectoryHandle(importId, { create });
    }

    async function fileHandleFor(importId, path, create = false)
    {
        const directory = await importDirectory(importId, create);
        if (path === "localization.txt") {
            return directory.getFileHandle("localization.txt", { create });
        }
        if (path === "main/iw_00.iwd") {
            const main = await directory.getDirectoryHandle("main", { create });
            return main.getFileHandle("iw_00.iwd", { create });
        }
        throw importError("UNSAFE_PATH", `The asset adapter does not allow ${path}.`);
    }

    async function removeImport(importId)
    {
        if (!importsRoot || !importId) {
            return;
        }
        validateImportId(importId);
        try {
            await importsRoot.removeEntry(importId, { recursive: true });
        } catch (error) {
            if (error?.name !== "NotFoundError") {
                throw error;
            }
        }
    }

    async function listImportNames()
    {
        const names = [];
        for await (const [name] of importsRoot.entries()) {
            names.push(name);
        }
        return names;
    }

    async function collectGarbage(preserveImportId = null)
    {
        if (preserveImportId !== null) {
            validateImportId(preserveImportId);
        }
        const names = await listImportNames();
        for (const name of names) {
            if (name !== preserveImportId) {
                await importsRoot.removeEntry(name, { recursive: true });
            }
        }
    }

    function isDefinitiveStoredDataError(error)
    {
        return error?.name === "NotFoundError" || error?.name === "TypeMismatchError" ||
            (error instanceof AssetImportError && (
                error.code === "INVALID_METADATA" || error.code === "INVALID_SIZE" ||
                error.code === "PERSISTED_SIZE" || error.code.startsWith("PROBE_")
            ));
    }

    async function probeStoredImport(importId, recordedManifest = null)
    {
        const localizationHandle = await fileHandleFor(importId, "localization.txt");
        const archiveHandle = await fileHandleFor(importId, "main/iw_00.iwd");
        const [localization, archive] = await Promise.all([
            localizationHandle.getFile(),
            archiveHandle.getFile(),
        ]);
        validateSelectedEntries(new Map([
            ["localization.txt", localization],
            ["main/iw_00.iwd", archive],
        ]));

        if (recordedManifest) {
            const { importId: recordedId, sizes: recordedSizes } =
                validateStoredManifest(recordedManifest);
            if (recordedId !== importId) {
                throw importError("INVALID_METADATA", "Stored asset metadata names the wrong import.");
            }
            if (recordedSizes.get("localization.txt") !== localization.size ||
                recordedSizes.get("main/iw_00.iwd") !== archive.size) {
                throw importError("PERSISTED_SIZE", "Persisted asset sizes no longer match the manifest.");
            }
        }

        const [language, archiveProbe] = await Promise.all([
            probeLocalization(module, localization),
            probeIwd(module, archive),
        ]);
        return {
            language,
            archiveProbe,
            files: REQUIRED_ASSETS.map((requirement) => ({
                path: requirement.path,
                size: requirement.path === "localization.txt" ? localization.size : archive.size,
            })),
        };
    }

    async function probeSelectedEntries(entries)
    {
        const localization = entries.get("localization.txt");
        const archive = entries.get("main/iw_00.iwd");
        const [language, archiveProbe] = await Promise.all([
            probeLocalization(module, localization),
            probeIwd(module, archive),
        ]);
        return { language, archiveProbe };
    }

    async function initialize()
    {
        const previousManifest = activeManifest;
        emit({
            state: "checking",
            message: "Checking local browser storage",
            manifest: previousManifest,
        });
        try {
            await ensureBackend();
            if (typeof navigator.storage?.persisted === "function") {
                persistenceGranted = await navigator.storage.persisted().catch(() => false);
            }
        } catch (error) {
            const wrapped = error instanceof AssetImportError
                ? error
                : importError(
                    "STORAGE_FAILED",
                    "Browser asset storage could not be opened. Reload to retry.",
                    error,
                );
            const state = wrapped.code === "STORAGE_UNSUPPORTED" ? "unsupported" : "failed";
            emit({
                state,
                message: wrapped.message,
                error: wrapped.code,
                retained: Boolean(previousManifest),
                manifest: previousManifest,
            });
            return { state, error: wrapped, retained: Boolean(previousManifest) };
        }

        try {
            return await withStorageLock(async () => {
                const stored = await databaseGet(database, ACTIVE_IMPORT_KEY);
                if (!stored) {
                    activeManifest = null;
                    try {
                        await collectGarbage();
                    } catch (error) {
                        const wrapped = importError(
                            "CLEANUP_FAILED",
                            "Abandoned browser asset data could not be removed. Reload to retry.",
                            error,
                        );
                        emit({ state: "failed", message: wrapped.message, error: wrapped.code });
                        return { state: "failed", error: wrapped };
                    }
                    emit({ state: "empty", message: "No local installation has been imported" });
                    return { state: "empty" };
                }

                let importId = null;
                try {
                    ({ importId } = validateStoredManifest(stored));
                    const probe = await probeStoredImport(importId, stored);
                    activeManifest = {
                        ...stored,
                        language: probe.language,
                        files: probe.files,
                        archiveProbe: probe.archiveProbe,
                    };
                    await collectGarbage(importId).catch(() => {});
                    emit({
                        state: "ready",
                        source: "restored",
                        message: "Persisted installation reopened and verified",
                        manifest: activeManifest,
                    });
                    return { state: "ready", manifest: activeManifest };
                } catch (error) {
                    activeManifest = null;
                    const wrapped = error instanceof AssetImportError
                        ? error
                        : importError(
                            "RESTORE_FAILED",
                            "The persisted installation could not be reopened. Reload to retry.",
                            error,
                        );

                    if (!isDefinitiveStoredDataError(error)) {
                        emit({
                            state: "failed",
                            message: wrapped.message,
                            error: wrapped.code,
                            retained: true,
                            manifest: stored,
                        });
                        return { state: "failed", error: wrapped, retained: true };
                    }

                    try {
                        if (importId) {
                            await collectGarbage(importId);
                            await removeImport(importId);
                        } else {
                            await collectGarbage();
                        }
                        await databaseDelete(database, ACTIVE_IMPORT_KEY);
                        broadcastChange();
                    } catch (cleanupError) {
                        const cleanupFailure = importError(
                            "CLEANUP_FAILED",
                            "Invalid browser asset data could not be removed. Reload to retry.",
                            cleanupError,
                        );
                        emit({
                            state: "failed",
                            message: cleanupFailure.message,
                            error: cleanupFailure.code,
                            retained: true,
                            manifest: stored,
                        });
                        return { state: "failed", error: cleanupFailure, retained: true };
                    }
                    emit({ state: "invalid", message: wrapped.message, error: wrapped.code });
                    return { state: "invalid", error: wrapped };
                }
            });
        } catch (error) {
            const wrapped = error instanceof AssetImportError
                ? error
                : importError(
                    "STORAGE_FAILED",
                    "Browser asset storage could not be refreshed. Reload to retry.",
                    error,
                );
            activeManifest = previousManifest;
            emit({
                state: "failed",
                message: wrapped.message,
                error: wrapped.code,
                retained: Boolean(previousManifest),
                manifest: previousManifest,
            });
            return { state: "failed", error: wrapped, retained: Boolean(previousManifest) };
        }
    }

    async function requestPersistence()
    {
        if (typeof navigator.storage?.persist !== "function") {
            persistenceGranted = false;
            return false;
        }
        try {
            persistenceGranted = await navigator.storage.persist();
        } catch {
            persistenceGranted = false;
        }
        return persistenceGranted;
    }

    async function importEntries(entries)
    {
        if (operationActive) {
            throw importError("BUSY", "Another asset-store operation is already running.");
        }
        operationActive = true;
        let previousManifest = activeManifest;
        let stagedImportId = null;
        try {
            await ensureBackend();
            const totalSize = validateSelectedEntries(entries);
            emit({
                state: "validating",
                message: "Checking selected file structure before copying",
                copiedBytes: 0,
                totalBytes: totalSize,
                progress: 0,
                manifest: previousManifest,
            });
            await probeSelectedEntries(entries);

            return await withStorageLock(async () => {
                const storedBeforeImport = await databaseGet(database, ACTIVE_IMPORT_KEY);
                previousManifest = activeManifest?.importId === storedBeforeImport?.importId
                    ? activeManifest
                    : storedBeforeImport;
                if (typeof navigator.storage?.estimate === "function") {
                    const estimate = await navigator.storage.estimate();
                    if (Number.isFinite(estimate.quota) && Number.isFinite(estimate.usage) &&
                        estimate.quota - estimate.usage < totalSize) {
                        throw importError(
                            "QUOTA",
                            "Browser storage does not have enough free space for the selected files.",
                        );
                    }
                }

                stagedImportId = crypto.randomUUID();
                await importDirectory(stagedImportId, true);
                let copiedBytes = 0;
                let lastReportedBytes = 0;
                let lastReportTime = 0;
                const reportBytes = (bytes) => {
                    copiedBytes += bytes;
                    const now = performance.now();
                    const reportInterval = Math.max(1024 * 1024, totalSize / 100);
                    if (copiedBytes !== totalSize && now - lastReportTime < 100 &&
                        copiedBytes - lastReportedBytes < reportInterval) {
                        return;
                    }
                    lastReportTime = now;
                    lastReportedBytes = copiedBytes;
                    emit({
                        state: "importing",
                        message: "Copying selected files into private browser storage",
                        copiedBytes,
                        totalBytes: totalSize,
                        progress: totalSize > 0 ? copiedBytes / totalSize : 0,
                        manifest: previousManifest,
                    });
                };
                emit({
                    state: "importing",
                    message: "Preparing private browser storage",
                    copiedBytes: 0,
                    totalBytes: totalSize,
                    progress: 0,
                    manifest: previousManifest,
                });

                for (const requirement of REQUIRED_ASSETS) {
                    const destination = await fileHandleFor(stagedImportId, requirement.path, true);
                    await copyFileToHandle(entries.get(requirement.path), destination, reportBytes);
                }

                emit({
                    state: "importing",
                    message: "Reopening and validating persisted files",
                    copiedBytes,
                    totalBytes: totalSize,
                    progress: 1,
                    manifest: previousManifest,
                });
                const probe = await probeStoredImport(stagedImportId);
                const manifest = {
                    schema: MANIFEST_SCHEMA,
                    importId: stagedImportId,
                    importedAt: new Date().toISOString(),
                    language: probe.language,
                    files: probe.files,
                    archiveProbe: probe.archiveProbe,
                };
                await databasePut(database, ACTIVE_IMPORT_KEY, manifest);
                activeManifest = manifest;
                stagedImportId = null;
                await collectGarbage(manifest.importId).catch(() => {});
                emit({
                    state: "ready",
                    source: "selection",
                    message: "Installation persisted and verified",
                    manifest,
                });
                broadcastChange();
                return manifest;
            });
        } catch (error) {
            if (stagedImportId) {
                await withStorageLock(() => removeImport(stagedImportId)).catch(() => {});
            }
            const wrapped = error instanceof AssetImportError
                ? error
                : importError("IMPORT_FAILED", "The selected files could not be imported.", error);
            emit({
                state: "failed",
                message: wrapped.message,
                error: wrapped.code,
                retained: Boolean(previousManifest),
                manifest: previousManifest,
            });
            throw wrapped;
        } finally {
            operationActive = false;
            scheduleExternalSync();
        }
    }

    async function clear()
    {
        if (operationActive) {
            throw importError("BUSY", "Another asset-store operation is already running.");
        }
        operationActive = true;
        let storedManifest = activeManifest;
        let activeFilesRemoved = false;
        try {
            await ensureBackend();
            await withStorageLock(async () => {
                storedManifest = await databaseGet(database, ACTIVE_IMPORT_KEY);
                emit({
                    state: "clearing",
                    message: "Removing the browser-local asset copy",
                    manifest: storedManifest,
                });

                let importId = null;
                try {
                    importId = storedManifest?.importId
                        ? validateImportId(storedManifest.importId)
                        : null;
                } catch {
                    importId = null;
                }
                if (importId) {
                    // Remove abandoned staging directories first. If that fails,
                    // the committed import and its pointer remain available for retry.
                    await collectGarbage(importId);
                    await removeImport(importId);
                    activeFilesRemoved = true;
                } else {
                    await collectGarbage();
                    activeFilesRemoved = true;
                }
                await databaseDelete(database, ACTIVE_IMPORT_KEY);
                activeManifest = null;
                emit({ state: "empty", message: "Local imported files were removed" });
                broadcastChange();
            });
        } catch (error) {
            if (activeFilesRemoved) {
                activeManifest = null;
            }
            const wrapped = error instanceof AssetImportError
                ? error
                : importError(
                    "CLEAR_FAILED",
                    activeFilesRemoved
                        ? "The files were removed, but browser metadata cleanup must be retried."
                        : "The browser-local asset copy could not be removed. Reload and retry.",
                    error,
                );
            emit({
                state: "failed",
                message: wrapped.message,
                error: wrapped.code,
                retained: !activeFilesRemoved && Boolean(storedManifest),
                manifest: activeFilesRemoved ? null : storedManifest,
            });
            throw wrapped;
        } finally {
            operationActive = false;
            scheduleExternalSync();
        }
    }

    async function openFile(path)
    {
        if (!activeManifest) {
            throw importError("NOT_READY", "No validated installation is active.");
        }
        const handle = await fileHandleFor(activeManifest.importId, path);
        return handle.getFile();
    }

    async function openSource(path)
    {
        await ensureBackend();
        return withStorageLock(async () => {
            if (!activeManifest) {
                throw importError("NOT_READY", "No validated installation is active.");
            }

            const active = validateStoredManifest(activeManifest);
            const storedManifest = await databaseGet(database, ACTIVE_IMPORT_KEY);
            if (!storedManifest) {
                throw importError("NOT_READY", "No validated installation is active.");
            }
            const stored = validateStoredManifest(storedManifest);
            if (stored.importId !== active.importId) {
                throw importError(
                    "STALE_SOURCE",
                    "The active browser asset import changed while the file was being opened.",
                );
            }

            const recordedSize = stored.sizes.get(path);
            if (recordedSize === undefined) {
                throw importError("UNSAFE_PATH", `The asset adapter does not allow ${path}.`);
            }
            const handle = await fileHandleFor(stored.importId, path);
            const file = await handle.getFile();
            if (file.size !== recordedSize) {
                throw importError(
                    "PERSISTED_SIZE",
                    "Persisted asset sizes no longer match the manifest.",
                );
            }

            const source = Object.freeze({
                importId: stored.importId,
                path,
                size: file.size,
                lastModified: file.lastModified,
            });
            readSources.add(source);
            return source;
        }, "shared");
    }

    async function readSource(source, { offset = 0, length = undefined } = {})
    {
        if (!source || typeof source !== "object" || !readSources.has(source)) {
            throw importError("INVALID_SOURCE", "The asset read source is invalid or foreign.");
        }

        const requestedLength = length ?? source.size - offset;
        if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(requestedLength) ||
            offset < 0 || requestedLength < 0 || requestedLength > MAX_ADAPTER_READ ||
            offset > source.size || requestedLength > source.size - offset) {
            throw importError(
                "INVALID_RANGE",
                `Asset reads must stay in range and at or below ${MAX_ADAPTER_READ} bytes.`,
            );
        }

        await ensureBackend();
        return withStorageLock(async () => {
            const storedManifest = await databaseGet(database, ACTIVE_IMPORT_KEY);
            if (!storedManifest) {
                throw importError(
                    "STALE_SOURCE",
                    "The browser asset import was removed before the read completed.",
                );
            }
            const stored = validateStoredManifest(storedManifest);
            if (stored.importId !== source.importId ||
                stored.sizes.get(source.path) !== source.size) {
                throw importError(
                    "STALE_SOURCE",
                    "The browser asset import changed before the read completed.",
                );
            }

            const handle = await fileHandleFor(source.importId, source.path);
            const file = await handle.getFile();
            if (file.size !== source.size) {
                throw importError(
                    "PERSISTED_SIZE",
                    "Persisted asset sizes no longer match the active read source.",
                );
            }
            return readWindow(file, offset, requestedLength);
        }, "shared");
    }

    async function stat(path)
    {
        const file = await openFile(path);
        return { path, size: file.size, lastModified: file.lastModified };
    }

    async function read(path, { offset = 0, length = undefined } = {})
    {
        const file = await openFile(path);
        const requestedLength = length ?? file.size - offset;
        if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(requestedLength) ||
            offset < 0 || requestedLength < 0 || requestedLength > MAX_ADAPTER_READ ||
            offset > file.size || requestedLength > file.size - offset) {
            throw importError(
                "INVALID_RANGE",
                `Asset reads must stay in range and at or below ${MAX_ADAPTER_READ} bytes.`,
            );
        }
        return readWindow(file, offset, requestedLength);
    }

    storageChannel?.addEventListener("message", (event) => {
        if (event.data?.type !== "changed") {
            return;
        }
        pendingExternalSync = true;
        scheduleExternalSync();
    });

    return Object.freeze({
        initialize,
        requestPersistence,
        importEntries,
        clear,
        stat,
        read,
        openSource,
        readSource,
        get manifest() { return activeManifest; },
    });
}
