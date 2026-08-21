const DATABASE_NAME = "kisakcod-web";
const DATABASE_VERSION = 1;
const METADATA_STORE = "metadata";
const ACTIVE_IMPORT_KEY = "active-import";
const APP_DIRECTORY = "kisakcod-web";
const IMPORTS_DIRECTORY = "imports";
const STORAGE_LOCK_NAME = "kisakcod-web-assets";
const STORAGE_CHANNEL_NAME = "kisakcod-web-assets";
const MANIFEST_SCHEMA = 3;
const MAX_IMPORTED_FILES = 8191;
const MAX_IMPORTED_PATH_BYTES = 255;
// Canonical Kisak filesystem sizes are signed int values even though the
// Worker bridge uses unsigned offsets internally.
const MAX_IMPORTED_FILE_SIZE = 0x7fff_ffff;
const MAX_ADAPTER_READ = 1024 * 1024;
const PROBE_WINDOW_SIZE = 4096;
const ZIP_TAIL_WINDOW_SIZE = 22 + 0xffff;

const BASE_ARCHIVES = Object.freeze(Array.from(
    { length: 14 },
    (_, index) => `main/iw_${String(index).padStart(2, "0")}.iwd`,
));
const LOCALIZED_ARCHIVES = Object.freeze(Array.from(
    { length: 7 },
    (_, index) => `main/localized_english_iw${String(index).padStart(2, "0")}.iwd`,
));
const STARTUP_ZONES = Object.freeze([
    "zone/english/code_post_gfx.ff",
    "zone/english/ui.ff",
    "zone/english/common.ff",
]);
const MAP_ZONE = "zone/english/killhouse.ff";

export const M12_INSTALL_PROFILE = Object.freeze({
    id: "sp-killhouse-english-v1",
    language: "english",
    map: "killhouse",
    baseArchives: BASE_ARCHIVES,
    localizedArchives: LOCALIZED_ARCHIVES,
    startupZones: STARTUP_ZONES,
    mapZone: MAP_ZONE,
});

export const REQUIRED_ASSETS = Object.freeze([
    Object.freeze({
        path: "localization.txt",
        minimumSize: 1,
        maximumSize: 4095,
        label: "Localization configuration",
        kind: "localization",
    }),
    ...BASE_ARCHIVES.map((path) => Object.freeze({
        path,
        minimumSize: 100,
        maximumSize: 512 * 1024 * 1024,
        label: "Base asset archive",
        kind: "iwd",
    })),
    ...LOCALIZED_ARCHIVES.map((path) => Object.freeze({
        path,
        minimumSize: 100,
        maximumSize: 512 * 1024 * 1024,
        label: "English localized asset archive",
        kind: "iwd",
    })),
    ...STARTUP_ZONES.map((path) => Object.freeze({
        path,
        minimumSize: 14,
        maximumSize: 512 * 1024 * 1024,
        label: "Single-player startup fastfile",
        kind: "fastfile",
    })),
    Object.freeze({
        path: MAP_ZONE,
        minimumSize: 14,
        maximumSize: 512 * 1024 * 1024,
        label: "F.N.G. map fastfile",
        kind: "fastfile",
    }),
]);

const REQUIRED_ASSET_PATHS = new Set(REQUIRED_ASSETS.map(({ path }) => path));

const PROBE_ERRORS = new Map([
    [1, "The browser passed an invalid probe window to WebAssembly."],
    [2, "The selected file is too large for this ZIP32 milestone."],
    [10, "localization.txt must fit the engine's 4 KiB localization buffer."],
    [11, "localization.txt is empty, malformed UTF-8, or has no localization entries."],
    [12, "localization.txt names a language this engine build does not support."],
    [20, "The required IWD does not begin with a complete ZIP local-file header."],
    [21, "The required IWD has no valid ZIP end-of-central-directory record."],
    [22, "Multi-disk ZIP archives are not supported for IWD files."],
    [23, "ZIP64 archives are outside this milestone's IWD boundary."],
    [24, "The required IWD contains no archive entries."],
    [25, "The required IWD declares an invalid central-directory range."],
    [26, "The IWD central-directory probe window is inconsistent."],
    [27, "The required IWD has an invalid central-directory header."],
    [28, "The first IWD local and central-directory entries do not agree."],
    [29, "Encrypted IWD entries are not supported."],
    [30, "The first IWD entry uses an unsupported compression method."],
    [40, "A required fastfile does not have a complete IWffu100 header."],
    [41, "Authenticated fastfiles are outside the browser port's legal local-file boundary."],
    [42, "A required fastfile does not use COD4 fastfile version 5."],
    [43, "A required fastfile does not begin with a supported zlib stream."],
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

function isAdditionalSinglePlayerFastfile(path, language = M12_INSTALL_PROFILE.language)
{
    const prefix = `zone/${language}/`;
    if (!path.startsWith(prefix) || path.length <= prefix.length ||
        path.slice(prefix.length).includes("/") || !path.endsWith(".ff")) {
        return false;
    }
    const name = path.slice(prefix.length, -3);
    // Multiplayer fastfiles use both mp_* and *_mp naming families.  Keep the
    // filter name-based so every campaign/SP map remains discoverable without
    // a manifest of map names.
    return !name.startsWith("mp_") && !name.endsWith("_mp");
}

function isSupportedImportedPath(path)
{
    return REQUIRED_ASSET_PATHS.has(path) || isAdditionalSinglePlayerFastfile(path);
}

function retainSupportedEntries(entries)
{
    const supported = new Map();
    for (const [path, file] of entries) {
        if (isSupportedImportedPath(path)) {
            supported.set(path, file);
        }
    }
    return supported;
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
        if (selectedByPath.has(foldedPath)) {
            throw importError(
                "DUPLICATE_PATH",
                `The selected folder contains conflicting paths for ${relativePath}.`,
            );
        }
        if (new TextEncoder().encode(foldedPath).byteLength > MAX_IMPORTED_PATH_BYTES) {
            throw importError("UNSAFE_PATH", `The selected path is too long: ${relativePath}.`);
        }
        selectedByPath.set(foldedPath, requireFileLike(file, relativePath));
        if (selectedByPath.size > MAX_IMPORTED_FILES) {
            throw importError("TOO_MANY_FILES", "The selected installation contains too many files.");
        }
    }

    const canonical = new Map(selectedByPath);
    for (const requirement of REQUIRED_ASSETS) {
        const file = selectedByPath.get(requirement.path);
        if (!file) {
            const message = requirement.path === "localization.txt"
                ? "Select the Call of Duty 4 installation folder containing localization.txt."
                : `The selected installation is missing ${requirement.path}.`;
            throw importError("MISSING_MANIFEST", message);
        }
        canonical.set(requirement.path, requireFileLike(file, requirement.path));
    }
    return retainSupportedEntries(canonical);
}

async function getRequiredHandle(directory, kind, name, displayPath = name)
{
    try {
        return kind === "directory"
            ? await directory.getDirectoryHandle(name)
            : await directory.getFileHandle(name);
    } catch (error) {
        if (error?.name === "NotFoundError") {
            throw importError(
                "MISSING_MANIFEST",
                `The selected installation is missing ${displayPath}.`,
                error,
            );
        }
        throw error;
    }
}

async function getRequiredFileFromDirectory(directory, relativePath, directoryCache = null)
{
    const segments = relativePath.split("/");
    let current = directory;
    let directoryPath = "";
    for (const segment of segments.slice(0, -1)) {
        directoryPath = directoryPath ? `${directoryPath}/${segment}` : segment;
        let next = directoryCache?.get(directoryPath);
        if (!next) {
            next = await getRequiredHandle(current, "directory", segment, relativePath);
            directoryCache?.set(directoryPath, next);
        }
        current = next;
    }
    return getRequiredHandle(
        current,
        "file",
        segments.at(-1),
        relativePath,
    );
}

export async function entriesFromDirectoryHandle(directory)
{
    if (!directory || typeof directory.getFileHandle !== "function" ||
        typeof directory.getDirectoryHandle !== "function") {
        throw importError("INVALID_PICKER", "The browser returned an invalid directory handle.");
    }
    const entries = new Map();
    const directoryCache = new Map();
    for (const requirement of REQUIRED_ASSETS) {
        const handle = await getRequiredFileFromDirectory(
            directory,
            requirement.path,
            directoryCache,
        );
        if (!handle || handle.kind !== "file" || typeof handle.getFile !== "function") {
            throw importError(
                "INVALID_FILE",
                `${requirement.path} is not a readable browser file handle.`,
            );
        }
        entries.set(
            requirement.path,
            requireFileLike(await handle.getFile(), requirement.path),
        );
    }

    const languageDirectory = directoryCache.get(`zone/${M12_INSTALL_PROFILE.language}`);
    if (!languageDirectory || typeof languageDirectory.entries !== "function") {
        throw importError(
            "INVALID_PICKER",
            `The selected installation does not expose zone/${M12_INSTALL_PROFILE.language} files.`,
        );
    }
    for await (const [name, handle] of languageDirectory.entries()) {
        const lowerName = typeof name === "string" ? name.toLocaleLowerCase("en-US") : "";
        if (!lowerName.endsWith(".ff")) {
            continue;
        }
        const relativePath = normalizeSelectionPath(
            `zone/${M12_INSTALL_PROFILE.language}/${name}`,
        ).toLocaleLowerCase("en-US");
        if (REQUIRED_ASSET_PATHS.has(relativePath) ||
            !isAdditionalSinglePlayerFastfile(relativePath)) {
            continue;
        }
        if (!handle || handle.kind !== "file" || typeof handle.getFile !== "function") {
            throw importError(
                "INVALID_FILE",
                `${relativePath} is not a readable browser file handle.`,
            );
        }
        if (entries.has(relativePath)) {
            throw importError(
                "DUPLICATE_PATH",
                `The selected folder contains conflicting paths for ${relativePath}.`,
            );
        }
        entries.set(relativePath, requireFileLike(await handle.getFile(), relativePath));
        if (entries.size > MAX_IMPORTED_FILES) {
            throw importError("TOO_MANY_FILES", "The selected installation contains too many files.");
        }
    }
    validateSelectedEntries(entries);
    return entries;
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
        manifest.files.length < REQUIRED_ASSETS.length ||
        manifest.files.length > MAX_IMPORTED_FILES) {
        throw importError("INVALID_METADATA", "Stored asset metadata has an invalid shape.");
    }
    const importId = validateImportId(manifest.importId);
    const sizes = new Map();
    for (const entry of manifest.files) {
        if (!entry || typeof entry !== "object" || Array.isArray(entry) ||
            typeof entry.path !== "string" || !Number.isSafeInteger(entry.size)) {
            throw importError("INVALID_METADATA", "Stored asset file metadata is malformed.");
        }
        const normalizedPath = normalizeSelectionPath(entry.path).toLocaleLowerCase("en-US");
        if (normalizedPath !== entry.path ||
            new TextEncoder().encode(normalizedPath).byteLength > MAX_IMPORTED_PATH_BYTES ||
            !isSupportedImportedPath(normalizedPath) ||
            sizes.has(entry.path) || entry.size < 0 ||
            entry.size > MAX_IMPORTED_FILE_SIZE) {
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
    if (entries.size < REQUIRED_ASSETS.length || entries.size > MAX_IMPORTED_FILES) {
        throw importError("INVALID_SELECTION", "The selected installation has an invalid file count.");
    }
    let totalSize = 0;
    for (const [path, selectedFile] of entries) {
        if (normalizeSelectionPath(path).toLocaleLowerCase("en-US") !== path ||
            new TextEncoder().encode(path).byteLength > MAX_IMPORTED_PATH_BYTES) {
            throw importError("UNSAFE_PATH", `The selected path is unsafe: ${path}.`);
        }
        if (!isSupportedImportedPath(path)) {
            throw importError("UNSUPPORTED_FILE", `The selected file is outside the supported asset set: ${path}.`);
        }
        const file = requireFileLike(selectedFile, path);
        if (file.size > MAX_IMPORTED_FILE_SIZE) {
            throw importError("INVALID_SIZE", `${path} is too large for the browser filesystem boundary.`);
        }
        totalSize += file.size;
        if (!Number.isSafeInteger(totalSize)) {
            throw importError("INVALID_SIZE", "The selected installation is too large.");
        }
    }
    for (const requirement of REQUIRED_ASSETS) {
        const file = requireFileLike(entries.get(requirement.path), requirement.path);
        if (file.size < requirement.minimumSize || file.size > requirement.maximumSize) {
            throw importError(
                "INVALID_SIZE",
                `${requirement.path} has an unexpected size (${file.size.toLocaleString()} bytes).`,
            );
        }
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

async function callWasmProbe(module, functionName, buffers, buildArguments)
{
    if (typeof module?.callProbe === "function") {
        const symbolicPointers = buffers.map((_, index) =>
            Object.freeze({ pointerIndex: index }));
        const argumentLayout = buildArguments(symbolicPointers).map((argument) =>
            argument && typeof argument === "object" &&
                Number.isInteger(argument.pointerIndex)
                ? { kind: "pointer", index: argument.pointerIndex }
                : { kind: "value", value: argument });
        return module.callProbe(functionName, buffers, argumentLayout);
    }
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
    const result = await callWasmProbe(
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
    const result = await callWasmProbe(
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

async function probeFastfile(module, file)
{
    const head = await readWindow(file, 0, Math.min(file.size, 14));
    const result = await callWasmProbe(
        module,
        "_KisakWeb_ProbeFastfileHeader",
        [head],
        ([data]) => [data, head.byteLength, file.size],
    );
    assertProbeSucceeded(result);
    return { version: 5, compression: "zlib" };
}

async function probeInstallEntries(module, entries)
{
    const language = await probeLocalization(module, entries.get("localization.txt"));
    if (language !== M12_INSTALL_PROFILE.language) {
        throw importError(
            "PROFILE_LANGUAGE",
            `The ${M12_INSTALL_PROFILE.id} profile requires English game data.`,
        );
    }

    let entriesDeclared = 0;
    let archiveCount = 0;
    let zoneCount = 0;
    const requiredFastfiles = new Set();
    for (const requirement of REQUIRED_ASSETS) {
        try {
            if (requirement.kind === "iwd") {
                const result = await probeIwd(module, entries.get(requirement.path));
                entriesDeclared += result.entriesDeclared;
                archiveCount += 1;
            } else if (requirement.kind === "fastfile") {
                await probeFastfile(module, entries.get(requirement.path));
                zoneCount += 1;
                requiredFastfiles.add(requirement.path);
            }
        } catch (error) {
            if (error instanceof AssetImportError && error.code.startsWith("PROBE_")) {
                throw importError(error.code, `${requirement.path}: ${error.message}`, error);
            }
            throw error;
        }
    }
    for (const [path, file] of entries) {
        if (!isAdditionalSinglePlayerFastfile(path)) {
            continue;
        }
        try {
            if (!requiredFastfiles.has(path)) {
                await probeFastfile(module, file);
                zoneCount += 1;
            }
        } catch (error) {
            if (error instanceof AssetImportError && error.code.startsWith("PROBE_")) {
                throw importError(error.code, `${path}: ${error.message}`, error);
            }
            throw error;
        }
    }
    return {
        language,
        profile: {
            id: M12_INSTALL_PROFILE.id,
            map: M12_INSTALL_PROFILE.map,
            archiveCount,
            zoneCount,
        },
        archiveProbe: { entriesDeclared, archivesProbed: archiveCount },
        zoneProbe: { filesProbed: zoneCount, version: 5, compression: "zlib" },
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
    const engineFilesystemLock = "kisakcod-web-engine-filesystem-v1";

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

    async function releaseEngineFilesForMutation()
    {
        storageChannel?.postMessage({ type: "release-engine-files" });
        await module?.unmount?.();
        // Every mounted engine tab holds this lock in shared mode.  Waiting for
        // an exclusive turn proves their Worker SyncAccessHandles have closed
        // before OPFS directories are replaced or removed.
        await navigator.locks.request(engineFilesystemLock, { mode: "exclusive" }, () => {});
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
        const normalizedPath = normalizeSelectionPath(path).toLocaleLowerCase("en-US");
        if (normalizedPath !== path ||
            new TextEncoder().encode(normalizedPath).byteLength > MAX_IMPORTED_PATH_BYTES) {
            throw importError("UNSAFE_PATH", `The asset adapter does not allow ${path}.`);
        }
        const directory = await importDirectory(importId, create);
        const segments = normalizedPath.split("/");
        let current = directory;
        for (const segment of segments.slice(0, -1)) {
            current = await current.getDirectoryHandle(segment, { create });
        }
        return current.getFileHandle(segments.at(-1), { create });
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
        const entries = new Map();
        const recordedFiles = recordedManifest?.files ?? REQUIRED_ASSETS;
        for (const entry of recordedFiles) {
            const handle = await fileHandleFor(importId, entry.path);
            entries.set(entry.path, await handle.getFile());
        }
        validateSelectedEntries(entries);

        if (recordedManifest) {
            const { importId: recordedId, sizes: recordedSizes } =
                validateStoredManifest(recordedManifest);
            if (recordedId !== importId) {
                throw importError("INVALID_METADATA", "Stored asset metadata names the wrong import.");
            }
            for (const [path, file] of entries) {
                if (recordedSizes.get(path) !== file.size) {
                    throw importError(
                        "PERSISTED_SIZE",
                        "Persisted asset sizes no longer match the manifest.",
                    );
                }
            }
        }

        const probe = await probeInstallEntries(module, entries);
        return {
            ...probe,
            files: [...entries].map(([path, file]) => ({ path, size: file.size })),
        };
    }

    async function probeSelectedEntries(entries)
    {
        return probeInstallEntries(module, entries);
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
                        profile: probe.profile,
                        archiveProbe: probe.archiveProbe,
                        zoneProbe: probe.zoneProbe,
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
            await releaseEngineFilesForMutation();

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

                for (const [logicalPath, file] of entries) {
                    const destination = await fileHandleFor(stagedImportId, logicalPath, true);
                    await copyFileToHandle(file, destination, reportBytes);
                }

                emit({
                    state: "importing",
                    message: "Reopening and validating persisted files",
                    copiedBytes,
                    totalBytes: totalSize,
                    progress: 1,
                    manifest: previousManifest,
                });
                const candidateManifest = {
                    schema: MANIFEST_SCHEMA,
                    importId: stagedImportId,
                    importedAt: new Date().toISOString(),
                    files: [...entries].map(([path, file]) => ({ path, size: file.size })),
                };
                const probe = await probeStoredImport(stagedImportId, candidateManifest);
                const manifest = {
                    ...candidateManifest,
                    language: probe.language,
                    files: probe.files,
                    profile: probe.profile,
                    archiveProbe: probe.archiveProbe,
                    zoneProbe: probe.zoneProbe,
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
            await releaseEngineFilesForMutation();
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
        if (event.data?.type === "release-engine-files") {
            void module?.unmount?.().catch(() => {});
            return;
        }
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
