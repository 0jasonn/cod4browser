import {
    isAdditionalSinglePlayerFastfile,
    isCinematicPath,
    isSupportedImportedPath,
    getInstallProfile,
    getRequiredAssets,
    REQUIRED_ASSETS,
} from "./asset_profile.mjs";

export { M12_INSTALL_PROFILE, REQUIRED_ASSETS } from "./asset_profile.mjs";

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

class AssetImportError extends Error
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

function retainSupportedEntries(entries, language)
{
    const supported = new Map();
    for (const [path, file] of entries) {
        if (isSupportedImportedPath(path, language)) {
            supported.set(path, file);
        }
    }
    return supported;
}

async function entriesFromFileList(fileList, module)
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

    if (!selectedByPath.has("localization.txt"))
        throw importError("MISSING_MANIFEST",
            "Select the Call of Duty 4 installation folder containing localization.txt.");
    const language = await probeLocalization(module, selectedByPath.get("localization.txt"));
    for (const requirement of getRequiredAssets(language)) {
        const file = selectedByPath.get(requirement.path);
        if (!file) {
            const message = requirement.path === "localization.txt"
                ? "Select the Call of Duty 4 installation folder containing localization.txt."
                : `The selected installation is missing ${requirement.path}.`;
            throw importError("MISSING_MANIFEST", message);
        }
        requireFileLike(file, requirement.path);
    }
    return retainSupportedEntries(selectedByPath, language);
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

async function entriesFromDirectoryHandle(directory, module)
{
    if (!directory || typeof directory.getFileHandle !== "function" ||
        typeof directory.getDirectoryHandle !== "function") {
        throw importError("INVALID_PICKER", "The browser returned an invalid directory handle.");
    }
    const entries = new Map();
    const directoryCache = new Map();
    const localizationHandle = await getRequiredFileFromDirectory(directory, "localization.txt");
    if (localizationHandle?.kind !== "file" || typeof localizationHandle.getFile !== "function")
        throw importError("INVALID_FILE", "localization.txt is not a readable browser file handle.");
    entries.set("localization.txt", requireFileLike(await localizationHandle.getFile(), "localization.txt"));
    const language = await probeLocalization(module, entries.get("localization.txt"));
    for (const requirement of getRequiredAssets(language)) {
        if (requirement.path === "localization.txt") continue;
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

    const languageDirectory = directoryCache.get(`zone/${language}`);
    if (!languageDirectory || typeof languageDirectory.entries !== "function") {
        throw importError(
            "INVALID_PICKER",
            `The selected installation does not expose zone/${language} files.`,
        );
    }
    for await (const [name, handle] of languageDirectory.entries()) {
        const lowerName = typeof name === "string" ? name.toLocaleLowerCase("en-US") : "";
        if (!lowerName.endsWith(".ff")) {
            continue;
        }
        const relativePath = normalizeSelectionPath(
            `zone/${language}/${name}`,
        ).toLocaleLowerCase("en-US");
        if (entries.has(relativePath) ||
            !isAdditionalSinglePlayerFastfile(relativePath, language)) {
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
    // Movies are optional for existing installations, but a new selection
    // admits the owned originals through the same durable file boundary.
    let videoDirectory;
    try { videoDirectory = await directoryCache.get("main").getDirectoryHandle("video"); }
    catch (error) { if (error?.name !== "NotFoundError") throw error; }
    if (videoDirectory) {
        for await (const [name, handle] of videoDirectory.entries()) {
            const path = normalizeSelectionPath(`main/video/${name}`).toLocaleLowerCase("en-US");
            if (!isCinematicPath(path)) continue;
            if (entries.has(path)) throw importError("DUPLICATE_PATH", `Conflicting movie path: ${path}.`);
            if (handle?.kind !== "file" || typeof handle.getFile !== "function")
                throw importError("INVALID_FILE", `${path} is not a readable movie file.`);
            entries.set(path, requireFileLike(await handle.getFile(), path));
            if (entries.size > MAX_IMPORTED_FILES)
                throw importError("TOO_MANY_FILES", "The selected installation contains too many files.");
        }
    }
    validateSelectedEntries(entries, language);
    return entries;
}

function selectFromInput(input, module)
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
                resolve(entriesFromFileList(input.files, module));
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

export async function selectInstallEntries(fallbackInput, { portable = false, module = null } = {})
{
    if (portable) {
        return selectFromInput(fallbackInput, module);
    }
    if (typeof globalThis.showDirectoryPicker === "function") {
        try {
            const directory = await globalThis.showDirectoryPicker({
                id: "kisakcod-install",
                mode: "read",
            });
            return await entriesFromDirectoryHandle(directory, module);
        } catch (error) {
            if (error?.name === "AbortError") {
                return null;
            }
            throw error;
        }
    }
    return selectFromInput(fallbackInput, module);
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
        manifest.schema !== MANIFEST_SCHEMA || typeof manifest.language !== "string" ||
        !Array.isArray(manifest.files) ||
        manifest.files.length < REQUIRED_ASSETS.length ||
        manifest.files.length > MAX_IMPORTED_FILES) {
        throw importError("INVALID_METADATA", "Stored asset metadata has an invalid shape.");
    }
    const importId = validateImportId(manifest.importId);
    let requirements;
    try { requirements = getRequiredAssets(manifest.language); }
    catch { throw importError("INVALID_METADATA", "Stored installation language is invalid."); }
    const sizes = new Map();
    for (const entry of manifest.files) {
        if (!entry || typeof entry !== "object" || Array.isArray(entry) ||
            typeof entry.path !== "string" || !Number.isSafeInteger(entry.size)) {
            throw importError("INVALID_METADATA", "Stored asset file metadata is malformed.");
        }
        const normalizedPath = normalizeSelectionPath(entry.path).toLocaleLowerCase("en-US");
        if (normalizedPath !== entry.path ||
            new TextEncoder().encode(normalizedPath).byteLength > MAX_IMPORTED_PATH_BYTES ||
            !isSupportedImportedPath(normalizedPath, manifest.language) ||
            sizes.has(entry.path) || entry.size < 0 ||
            entry.size > MAX_IMPORTED_FILE_SIZE) {
            throw importError("INVALID_METADATA", "Stored asset file metadata is inconsistent.");
        }
        sizes.set(entry.path, entry.size);
    }
    if (requirements.some(({ path }) => !sizes.has(path))) {
        throw importError("INVALID_METADATA", "Stored asset metadata is incomplete.");
    }
    return { importId, sizes };
}

function validateSelectedEntries(entries, language)
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
        if (!isSupportedImportedPath(path, language)) {
            throw importError("UNSUPPORTED_FILE", `The selected file is outside the supported asset set: ${path}.`);
        }
        const file = requireFileLike(selectedFile, path);
        if (file.size > MAX_IMPORTED_FILE_SIZE) {
            throw importError("INVALID_SIZE", `${path} is too large for the browser filesystem boundary.`);
        }
        if (isCinematicPath(path) && (file.size < 48 || file.size > 512 * 1024 * 1024))
            throw importError("INVALID_SIZE", `${path} is outside the movie size limit.`);
        totalSize += file.size;
        if (!Number.isSafeInteger(totalSize)) {
            throw importError("INVALID_SIZE", "The selected installation is too large.");
        }
    }
    for (const requirement of getRequiredAssets(language)) {
        if (!entries.has(requirement.path))
            throw importError("MISSING_MANIFEST", `The selected installation is missing ${requirement.path}.`);
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

async function runAssetProbe(module, kind, buffers, metadata)
{
    if (typeof module?.probeAsset !== "function") {
        throw importError("WASM_API", "The WebAssembly asset-probe API is unavailable.");
    }
    return module.probeAsset(kind, buffers, metadata);
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
    requireFileLike(file, "localization.txt");
    if (file.size < 1 || file.size > 4095)
        throw importError("PROBE_10", PROBE_ERRORS.get(10));
    const bytes = await readWindow(file, 0, file.size);
    const result = await runAssetProbe(
        module, "localization", [bytes], { fileSize: file.size });
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
    const result = await runAssetProbe(module, "iwd", [head, tail, central], {
        tailOffset,
        centralOffset,
        fileSize: file.size,
    });
    assertProbeSucceeded(result);
    return {
        entriesDeclared: eocd?.entriesDeclared ?? 0,
    };
}

async function probeFastfile(module, file)
{
    const head = await readWindow(file, 0, Math.min(file.size, 14));
    const result = await runAssetProbe(
        module, "fastfile", [head], { fileSize: file.size });
    assertProbeSucceeded(result);
    return { version: 5, compression: "zlib" };
}

async function probeCinematic(file, path)
{
    const header = await readWindow(file, 0, 44);
    const view = new DataView(header.buffer, header.byteOffset, header.byteLength);
    const integer = (offset) => view.getUint32(offset, true);
    // Admission bounds only; FFmpeg remains the container/codec implementation.
    if (header.length !== 44 || integer(0) !== 0x694b4942 ||
        integer(4) + 8 !== file.size || integer(8) < 1 || integer(8) > 108000 ||
        integer(12) > 16 * 1024 * 1024 || integer(12) > file.size ||
        integer(20) < 1 || integer(20) > 1920 || integer(24) < 1 || integer(24) > 1080 ||
        integer(28) < 1 || integer(32) < 1 || integer(28) / integer(32) > 120 ||
        integer(28) / integer(32) < 1 || integer(40) > 1 ||
        integer(8) * integer(32) / integer(28) > 3600 ||
        44 + integer(40) * 12 + integer(8) * 4 > file.size)
        throw importError("INVALID_CINEMATIC", `${path} has an invalid or unsupported Bink 1 header.`);
}

async function probeInstallEntries(module, entries, expectedLanguage = null)
{
    if (!(entries instanceof Map))
        throw importError("INVALID_SELECTION", "The asset selection is not a canonical file map.");
    const language = await probeLocalization(module, entries.get("localization.txt"));
    if (expectedLanguage !== null && language !== expectedLanguage)
        throw importError("INVALID_METADATA", "Stored language does not match localization.txt.");
    const profile = getInstallProfile(language);
    const totalSize = validateSelectedEntries(entries, language);

    let entriesDeclared = 0;
    let archiveCount = 0;
    let zoneCount = 0;
    const availableSinglePlayerZones = [];
    const requiredFastfiles = new Set();
    for (const requirement of getRequiredAssets(language)) {
        try {
            if (requirement.kind === "iwd") {
                const result = await probeIwd(module, entries.get(requirement.path));
                entriesDeclared += result.entriesDeclared;
                archiveCount += 1;
            } else if (requirement.kind === "fastfile") {
                await probeFastfile(module, entries.get(requirement.path));
                zoneCount += 1;
                requiredFastfiles.add(requirement.path);
                if (requirement.path === profile.mapZone) {
                    availableSinglePlayerZones.push(requirement.path);
                }
            }
        } catch (error) {
            if (error instanceof AssetImportError && error.code.startsWith("PROBE_")) {
                throw importError(error.code, `${requirement.path}: ${error.message}`, error);
            }
            throw error;
        }
    }
    for (const [path, file] of entries) {
        if (isCinematicPath(path)) {
            await probeCinematic(file, path);
            continue;
        }
        if (!isAdditionalSinglePlayerFastfile(path, language)) {
            continue;
        }
        try {
            if (!requiredFastfiles.has(path)) {
                await probeFastfile(module, file);
                zoneCount += 1;
                availableSinglePlayerZones.push(path);
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
        totalSize,
        profile: {
            version: profile.version,
            id: profile.id,
            product: profile.product,
            map: profile.map,
            archiveCount,
            zoneCount,
            availableSinglePlayerZones: availableSinglePlayerZones.sort(),
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

/**
 * @param {any} module
 * @param {{onState?: (detail: any) => void}} [options]
 */
export function createBrowserAssetStore(module, { onState = () => {} } = {})
{
    let database = null;
    let importsRoot = null;
    let activeManifest = null;
    let persistenceGranted = null;
    let storageEstimate = { usage: null, quota: null };
    let operationActive = false;
    let pendingExternalSync = false;
    let disposed = false;
    let disposePromise = null;
    const pendingOperations = new Set();
    const storageChannel = typeof BroadcastChannel === "function"
        ? new BroadcastChannel(STORAGE_CHANNEL_NAME)
        : null;
    const engineFilesystemLock = "kisakcod-web-engine-filesystem-v1";

    const flushEngine = () => module?.flushAndUnmount?.() ?? module?.unmount?.();

    const emit = (detail) => {
        onState({ persistenceGranted, storageEstimate, ...detail });
    };

    async function refreshStorageEstimate()
    {
        if (typeof navigator.storage?.estimate !== "function") {
            storageEstimate = { usage: null, quota: null };
            return storageEstimate;
        }
        try {
            const estimate = await navigator.storage.estimate();
            storageEstimate = {
                usage: Number.isFinite(estimate.usage) ? estimate.usage : null,
                quota: Number.isFinite(estimate.quota) ? estimate.quota : null,
            };
        } catch {
            storageEstimate = { usage: null, quota: null };
        }
        return storageEstimate;
    }

    /** @param {LockGrantedCallback} callback @param {LockMode} [mode] */
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
        await flushEngine();
        // Every mounted engine tab holds this lock in shared mode.  Waiting for
        // an exclusive turn proves their Worker SyncAccessHandles have closed
        // before OPFS directories are replaced or removed.
        await navigator.locks.request(engineFilesystemLock, { mode: "exclusive" }, () => {});
    }

    function scheduleExternalSync()
    {
        if (disposed || !pendingExternalSync || operationActive) {
            return;
        }
        pendingExternalSync = false;
        queueMicrotask(() => track(initialize).catch(() => {}));
    }

    function track(callback)
    {
        if (disposed) {
            return Promise.reject(importError(
                "STORE_DISPOSED", "The browser asset store has been disposed."));
        }
        const operation = Promise.resolve().then(callback);
        pendingOperations.add(operation);
        operation.finally(() => pendingOperations.delete(operation)).catch(() => {});
        return operation;
    }

    async function ensureBackend()
    {
        if (!globalThis.indexedDB || typeof navigator.storage?.getDirectory !== "function" ||
            typeof navigator.locks?.request !== "function" ||
            typeof BroadcastChannel !== "function") {
            throw importError(
                "STORAGE_UNSUPPORTED",
                "This browser does not provide OPFS, IndexedDB, Web Locks, and BroadcastChannel for safe local imports.",
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

    async function probeStoredImport(importId, recordedManifest)
    {
        const { importId: recordedId, sizes: recordedSizes } = validateStoredManifest(recordedManifest);
        if (recordedId !== importId)
            throw importError("INVALID_METADATA", "Stored asset metadata names the wrong import.");
        const entries = new Map();
        for (const entry of recordedManifest.files) {
            const handle = await fileHandleFor(importId, entry.path);
            entries.set(entry.path, await handle.getFile());
        }
        for (const [path, file] of entries) {
            if (recordedSizes.get(path) !== file.size) {
                throw importError(
                    "PERSISTED_SIZE",
                    "Persisted asset sizes no longer match the manifest.",
                );
            }
        }

        const probe = await probeInstallEntries(module, entries, recordedManifest.language);
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
            await refreshStorageEstimate();
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
        await refreshStorageEstimate();
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
            emit({
                state: "validating",
                message: "Checking selected file structure before copying",
                copiedBytes: 0,
                totalBytes: 0,
                progress: 0,
                manifest: previousManifest,
            });
            const selectedProbe = await probeSelectedEntries(entries);
            const totalSize = selectedProbe.totalSize;
            await releaseEngineFilesForMutation();

            return await withStorageLock(async () => {
                const storedBeforeImport = await databaseGet(database, ACTIVE_IMPORT_KEY);
                previousManifest = activeManifest?.importId === storedBeforeImport?.importId
                    ? activeManifest
                    : storedBeforeImport;
                const estimate = await refreshStorageEstimate();
                if (estimate.quota !== null && estimate.usage !== null &&
                    estimate.quota - estimate.usage < totalSize) {
                    throw importError(
                        "QUOTA",
                        "Browser storage does not have enough free space for the selected files.",
                    );
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
                    language: selectedProbe.language,
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
                await refreshStorageEstimate();
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
                await refreshStorageEstimate();
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

    const handleStorageMessage = (event) => {
        if (disposed) return;
        if (event.data?.type === "release-engine-files") {
            void flushEngine()?.catch(() => {});
            return;
        }
        if (event.data?.type !== "changed") {
            return;
        }
        pendingExternalSync = true;
        scheduleExternalSync();
    };
    storageChannel?.addEventListener("message", handleStorageMessage);

    function dispose()
    {
        if (disposePromise) return disposePromise;
        disposed = true;
        disposePromise = (async () => {
            await Promise.allSettled([...pendingOperations]);
            try {
                await flushEngine();
            } finally {
                storageChannel?.removeEventListener("message", handleStorageMessage);
                storageChannel?.close();
                database?.close();
                database = null;
                importsRoot = null;
                activeManifest = null;
            }
        })();
        return disposePromise;
    }

    return Object.freeze({
        initialize: () => track(initialize),
        requestPersistence: () => track(requestPersistence),
        importEntries: (entries) => track(() => importEntries(entries)),
        clear: () => track(clear),
        stat: (path) => track(() => stat(path)),
        read: (path, options) => track(() => read(path, options)),
        dispose,
        get manifest() { return activeManifest; },
        get storageStatus() { return { persistenceGranted, storageEstimate }; },
    });
}
