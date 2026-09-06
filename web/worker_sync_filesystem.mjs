const APP_DIRECTORY = "kisakcod-web";
const IMPORTS_DIRECTORY = "imports";
const HOME_DIRECTORY = "home";
const SYNC_GLOBAL = "__KISAKCOD_SYNC_FS__";
const MAX_HOME_FILE_BYTES = 64 * 1024 * 1024;
const MAX_HOME_TOTAL_BYTES = 128 * 1024 * 1024;
const MAX_HOME_FILES = 8191;
const MAX_HOME_DIRECTORIES = 8191;
// Canonical MAX_OSPATH is 260 bytes including the terminator.
function validHomePath(path) { return new TextEncoder().encode(path).byteLength < 260; }
const RENAME_JOURNAL = "home-rename.json";
const RESTORE_JOURNAL = "home-restore.json";
const RESTORE_STAGE = "home-restore";
const MAX_RECOVERY_HEADER_BYTES = 4 * 1024 * 1024;
const BACKUP_MAGIC = new TextEncoder().encode("KISAKHOME1\n");
// Live content and queued immutable copies are separate budgets. Reserve the
// next close snapshot when accepting an open/write, so close cannot drop data.
const MAX_PENDING_OPERATIONS = 16384;
const MAX_PENDING_BYTES = 256 * 1024 * 1024;

// Read-only escape hatch for oversized/conflicting homes. Deliberately does
// not mount, normalize names, replay journals, or load file contents into RAM.
// The caller holds the existing home-writer lease while using these handles.
export async function* readHomeRecoveryFiles(root)
{
    let app;
    try { app = await root.getDirectoryHandle(APP_DIRECTORY); }
    catch (error) { if (error?.name === "NotFoundError") return; throw error; }
    for (const name of [RENAME_JOURNAL, RESTORE_JOURNAL]) {
        const handle = await optionalRecoveryFile(app, name);
        if (handle) yield { path: name, handle };
    }
    let home;
    try { home = await app.getDirectoryHandle(HOME_DIRECTORY); }
    catch (error) { if (error?.name !== "NotFoundError") throw error; }
    const stack = home ? [{ prefix: "home", iterator: home.entries() }] : [];
    try {
        const stage = await app.getDirectoryHandle(RESTORE_STAGE);
        stack.push({ prefix: RESTORE_STAGE, iterator: stage.entries() });
    } catch (error) { if (error?.name !== "NotFoundError") throw error; }
    while (stack.length) {
        const current = stack.at(-1);
        const next = await current.iterator.next();
        if (next.done) { stack.pop(); continue; }
        const [name, handle] = next.value;
        const path = `${current.prefix}/${name}`;
        if (handle.kind === "directory") stack.push({ prefix: path, iterator: handle.entries() });
        else if (handle.kind === "file") yield { path, handle };
    }
}

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

function recoveryEntries(value)
{
    if (!value || value.version !== 1 || !Array.isArray(value.files) || value.files.length > MAX_HOME_FILES)
        throw new Error("Unsupported or oversized home backup version.");
    const names = new Set();
    let bytes = 0;
    for (const entry of value.files) {
        if (!entry || typeof entry.path !== "string" || normalizeLogicalPath(entry.path) !== entry.path ||
            !validHomePath(entry.path) || names.has(entry.path) ||
            !Number.isSafeInteger(entry.size) || entry.size < 0 || entry.size > MAX_HOME_FILE_BYTES ||
            typeof entry.sha256 !== "string" || !/^[a-f0-9]{64}$/u.test(entry.sha256))
            throw new Error("Home backup contains an invalid path, size, hash or duplicate.");
        names.add(entry.path);
        bytes += entry.size;
    }
    if (bytes > MAX_HOME_TOTAL_BYTES) throw new Error("Home backup exceeds the storage limit.");
    const header = JSON.stringify({ version: 1, files: value.files.map(({ path, size, sha256 }) => ({ path, size, sha256 })) });
    if (new TextEncoder().encode(header).byteLength > MAX_RECOVERY_HEADER_BYTES)
        throw new Error("Home recovery metadata exceeds its limit.");
    return value.files.map(({ path, size, sha256 }) => Object.freeze({ path, size, sha256 }));
}

async function recoveryHash(bytes)
{
    return [...new Uint8Array(await crypto.subtle.digest("SHA-256", bytes))]
        .map(byte => byte.toString(16).padStart(2, "0")).join("");
}

// Metadata-only inventory. Never normalize a physical name into another file.
async function inspectRecoveryHome(home)
{
    const files = new Map();
    const directories = new Set([""]);
    let bytes = 0;
    const stack = [{ prefix: "", directory: home }];
    while (stack.length) {
        const current = stack.pop();
        for await (const [name, handle] of current.directory.entries()) {
            const path = current.prefix ? `${current.prefix}/${name}` : name;
            if (normalizeLogicalPath(path) !== path || !validHomePath(path))
                throw new Error("Home has invalid or non-normalized paths; use raw export first.");
            if (handle.kind === "directory") {
                directories.add(path);
                if (directories.size - 1 > MAX_HOME_DIRECTORIES) throw new Error("Home exceeds its directory limit; use raw export.");
                stack.push({ prefix: path, directory: handle });
            } else if (handle.kind === "file") {
                const file = await handle.getFile();
                if (file.size > MAX_HOME_FILE_BYTES || files.size >= MAX_HOME_FILES ||
                    bytes + file.size > MAX_HOME_TOTAL_BYTES) throw new Error("Home exceeds its storage limit; use raw export.");
                files.set(path, { file, handle });
                bytes += file.size;
            } else throw new Error("Unknown home entry type.");
        }
    }
    return { files, directories, bytes };
}

function preflightRestore(inventory, entries, replay = false)
{
    const files = new Set(inventory.files.keys());
    const directories = new Set(inventory.directories);
    let bytes = inventory.bytes;
    for (const entry of entries) {
        const existing = inventory.files.get(entry.path);
        if (existing && !replay) throw new Error(`Restore refuses to overwrite ${entry.path}.`);
        if (directories.has(entry.path)) throw new Error(`Restore path conflicts with a directory: ${entry.path}.`);
        files.add(entry.path);
        bytes += entry.size - (existing?.file.size ?? 0);
        const segments = entry.path.split("/");
        segments.pop();
        let path = "";
        for (const name of segments) {
            path = path ? `${path}/${name}` : name;
            if (files.has(path)) throw new Error(`Restore path conflicts with a file: ${path}.`);
            directories.add(path);
        }
    }
    for (const path of files) if (directories.has(path)) throw new Error(`Restore has a file/directory conflict: ${path}.`);
    if (files.size > MAX_HOME_FILES || directories.size - 1 > MAX_HOME_DIRECTORIES || bytes > MAX_HOME_TOTAL_BYTES)
        throw new Error("Restored home would exceed its file, directory or byte limit.");
}

async function optionalRecoveryFile(directory, name)
{
    try { return await directory.getFileHandle(name); }
    catch (error) { if (error?.name === "NotFoundError") return null; throw error; }
}

async function checkRecoveryIdle(app)
{
    for (const name of [RENAME_JOURNAL, RESTORE_JOURNAL]) {
        if (await optionalRecoveryFile(app, name)) throw new Error("Home has a pending recovery. Resume recovery before restoring another backup.");
    }
}

// Holds the same home lease as the engine. Backup format is platform transport;
// files remain canonical opaque save/config bytes, without a second save parser.
export async function createHomeBackup(root, signal = null)
{
    const app = await childDirectory(root, [APP_DIRECTORY], true);
    await checkRecoveryIdle(app);
    const home = await childDirectory(app, [HOME_DIRECTORY], true);
    const inventory = await inspectRecoveryHome(home);
    const entries = [];
    const blobs = [];
    for (const [path, { file }] of inventory.files) {
        signal?.throwIfAborted();
        const bytes = await file.arrayBuffer();
        entries.push({ path, size: file.size, sha256: await recoveryHash(bytes) });
        // OPFS File snapshots can become unreadable after their source changes.
        // Own these bounded bytes so the downloaded backup survives lease release.
        blobs.push(new Blob([bytes]));
    }
    const header = new TextEncoder().encode(JSON.stringify({ version: 1, files: entries }));
    recoveryEntries({ version: 1, files: entries });
    const length = new Uint8Array(4);
    new DataView(length.buffer).setUint32(0, header.byteLength, true);
    return new Blob([BACKUP_MAGIC, length, header, ...blobs], { type: "application/octet-stream" });
}

export async function readHomeBackup(backup)
{
    const prefixSize = BACKUP_MAGIC.byteLength + 4;
    if (!Number.isSafeInteger(backup?.size) || backup.size < prefixSize ||
        backup.size > MAX_HOME_TOTAL_BYTES + MAX_RECOVERY_HEADER_BYTES + prefixSize)
        throw new Error("Home backup is truncated or exceeds the storage limit.");
    const prefix = new Uint8Array(await backup.slice(0, prefixSize).arrayBuffer());
    if (!BACKUP_MAGIC.every((byte, index) => prefix[index] === byte)) throw new Error("Unsupported home backup format/version.");
    const size = new DataView(prefix.buffer).getUint32(BACKUP_MAGIC.byteLength, true);
    if (size > MAX_RECOVERY_HEADER_BYTES || size > backup.size - prefixSize) throw new Error("Invalid home backup header length.");
    const entries = recoveryEntries(JSON.parse(new TextDecoder("utf-8", { fatal: true })
        .decode(await backup.slice(prefixSize, prefixSize + size).arrayBuffer())));
    let offset = prefixSize + size;
    const files = entries.map(entry => {
        const file = backup.slice(offset, offset + entry.size);
        offset += entry.size;
        return { ...entry, file };
    });
    if (offset !== backup.size) throw new Error("Home backup payload is truncated or has trailing bytes.");
    return files;
}

async function cleanRestoreStage(app)
{
    try { await app.removeEntry(RESTORE_STAGE, { recursive: true }); }
    catch (error) { if (error?.name !== "NotFoundError") throw error; }
}

async function replayHomeRestore(app, home)
{
    const handle = await optionalRecoveryFile(app, RESTORE_JOURNAL);
    if (!handle) return;
    if (await optionalRecoveryFile(app, RENAME_JOURNAL))
        throw new Error("Conflicting home recovery records; use raw export before recovery.");
    const journal = await handle.getFile();
    if (journal.size === 0) { await removeDurable(app, RESTORE_JOURNAL); return; }
    if (journal.size > MAX_RECOVERY_HEADER_BYTES) throw new Error("Home restore journal is oversized; use raw export.");
    const entries = recoveryEntries(JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(await journal.arrayBuffer())));
    const inventory = await inspectRecoveryHome(home);
    preflightRestore(inventory, entries, true);
    const stage = await app.getDirectoryHandle(RESTORE_STAGE);
    // Verify every staged source and any already-published destination first.
    // A newly created zero-byte destination can precede writable-stream close.
    for (const [index, entry] of entries.entries()) {
        const source = await (await stage.getFileHandle(String(index))).getFile();
        if (source.size !== entry.size || await recoveryHash(await source.arrayBuffer()) !== entry.sha256)
            throw new Error(`Staged restore is damaged: ${entry.path}. Original home files remain unchanged.`);
        const existing = inventory.files.get(entry.path)?.file;
        if (existing?.size && (existing.size !== entry.size || await recoveryHash(await existing.arrayBuffer()) !== entry.sha256))
            throw new Error(`Restore refuses to overwrite changed file ${entry.path}.`);
    }
    for (const [index, entry] of entries.entries()) {
        const existing = inventory.files.get(entry.path)?.file;
        if (existing?.size === entry.size) continue;
        const segments = entry.path.split("/");
        const name = segments.pop();
        const directory = await childDirectory(home, segments, true);
        const bytes = new Uint8Array(await (await (await stage.getFileHandle(String(index))).getFile()).arrayBuffer());
        await publishBytes(directory, name, bytes);
    }
    // Once this record is gone, all destinations are complete. Orphaned staging
    // is uncommitted scratch and may be reclaimed by a future explicit restore.
    await removeDurable(app, RESTORE_JOURNAL);
    try { await cleanRestoreStage(app); }
    catch (error) {
        throw Object.assign(new Error("Restored files are complete, but temporary cleanup failed. Use Resume interrupted recovery to retry cleanup."),
            { code: "RESTORE_COMMITTED_CLEANUP_FAILED", cause: error });
    }
}

export async function resumeHomeRestore(root)
{
    const app = await childDirectory(root, [APP_DIRECTORY], true);
    const home = await childDirectory(app, [HOME_DIRECTORY], true);
    const pending = Boolean(await optionalRecoveryFile(app, RESTORE_JOURNAL) || await optionalRecoveryFile(app, RENAME_JOURNAL));
    await replayHomeRestore(app, home);
    await recoverRename(app, home);
    await cleanRestoreStage(app);
    return { recovered: pending };
}

export async function restoreHomeFiles(root, selected, signal = null)
{
    const entries = recoveryEntries({ version: 1, files: selected });
    const sources = selected.map(entry => entry.file);
    const app = await childDirectory(root, [APP_DIRECTORY], true);
    await checkRecoveryIdle(app);
    const home = await childDirectory(app, [HOME_DIRECTORY], true);
    preflightRestore(await inspectRecoveryHome(home), entries);
    signal?.throwIfAborted();
    await cleanRestoreStage(app);
    const stage = await app.getDirectoryHandle(RESTORE_STAGE, { create: true });
    for (const [index, entry] of entries.entries()) {
        signal?.throwIfAborted();
        if (sources[index]?.size !== entry.size) throw new Error("Restore source size changed.");
        const bytes = new Uint8Array(await sources[index].arrayBuffer());
        if (await recoveryHash(bytes) !== entry.sha256) throw new Error(`Backup checksum failed: ${entry.path}.`);
        await publishBytes(stage, String(index), bytes);
    }
    signal?.throwIfAborted();
    const record = new TextEncoder().encode(JSON.stringify({ version: 1,
        files: entries.map(({ path, size, sha256 }) => ({ path, size, sha256 })) }));
    await publishBytes(app, RESTORE_JOURNAL, record);
    // Cancellation is allowed before intent commits. Afterwards finish/replay;
    // never abandon a partly published restore or release the writer lease early.
    await replayHomeRestore(app, home);
    return { filesRestored: entries.length, bytesRestored: entries.reduce((sum, entry) => sum + entry.size, 0) };
}

export async function restoreRawHomeFile(root, path, file, signal = null)
{
    const [entry] = recoveryEntries({ version: 1, files: [{ path, size: file?.size, sha256: "0".repeat(64) }] });
    signal?.throwIfAborted();
    const sha256 = await recoveryHash(await file.arrayBuffer());
    return restoreHomeFiles(root, [{ ...entry, sha256, file }], signal);
}

async function childDirectory(root, segments, create = false)
{
    let current = root;
    for (const segment of segments) {
        current = await current.getDirectoryHandle(segment, { create });
    }
    return current;
}

async function removeDurable(directory, name)
{
    try {
        await directory.removeEntry(name);
    } catch (error) {
        if (error?.name !== "NotFoundError") throw error;
    }
}

// OPFS writable streams stage changes until close. Preserve the original
// failure if abort or removal of a newly created empty entry also fails.
async function publishBytes(directory, name, bytes)
{
    let handle;
    let created = false;
    try {
        handle = await directory.getFileHandle(name);
    } catch (error) {
        if (error?.name !== "NotFoundError") throw error;
        handle = await directory.getFileHandle(name, { create: true });
        created = true;
    }
    let writable;
    try {
        writable = await handle.createWritable();
        await writable.write(bytes);
        await writable.truncate(bytes.byteLength);
        await writable.close();
    } catch (error) {
        const cleanupErrors = [];
        try { await writable?.abort(); } catch (cleanup) { cleanupErrors.push(cleanup); }
        if (created) {
            try { await removeDurable(directory, name); } catch (cleanup) { cleanupErrors.push(cleanup); }
        }
        if (cleanupErrors.length) error.cleanupErrors = cleanupErrors;
        throw error;
    }
}


async function homeHandle(homeDirectory, path)
{
    const segments = path.split("/");
    const name = segments.pop();
    const directory = await childDirectory(homeDirectory, segments);
    return { directory, name };
}

async function recoverRename(appDirectory, homeDirectory)
{
    let journal;
    try { journal = await appDirectory.getFileHandle(RENAME_JOURNAL); }
    catch (error) {
        if (error?.name === "NotFoundError") return;
        throw error;
    }
    const blob = await journal.getFile();
    // A newly created journal interrupted before close cannot have begun
    // publication. An existing journal is never truncated in place.
    if (blob.size === 0) return removeDurable(appDirectory, RENAME_JOURNAL);
    if (blob.size > 65536) throw new Error("Browser home rename journal is oversized; export home before recovery.");
    const record = JSON.parse(new TextDecoder().decode(await blob.arrayBuffer()));
    if (!record || record.version !== 1 || typeof record.source !== "string" ||
        typeof record.destination !== "string" || normalizeLogicalPath(record.source) !== record.source ||
        normalizeLogicalPath(record.destination) !== record.destination ||
        !validHomePath(record.source) || !validHomePath(record.destination) ||
        record.source === record.destination) {
        throw new Error("Browser home rename journal is invalid; export home before recovery.");
    }
    const source = await homeHandle(homeDirectory, record.source);
    const destination = await homeHandle(homeDirectory, record.destination);
    let sourceHandle;
    try { sourceHandle = await source.directory.getFileHandle(source.name); }
    catch (error) { if (error?.name !== "NotFoundError") throw error; }
    if (sourceHandle) {
        const sourceBlob = await sourceHandle.getFile();
        if (sourceBlob.size > MAX_HOME_FILE_BYTES) throw new Error("Browser home rename source exceeds its recovery limit.");
        await publishBytes(destination.directory, destination.name,
            new Uint8Array(await sourceBlob.arrayBuffer()));
        await removeDurable(source.directory, source.name);
    } else {
        // Source retirement follows destination close, so a missing source
        // is committed only if the published destination still exists.
        await destination.directory.getFileHandle(destination.name);
    }
    await removeDurable(appDirectory, RENAME_JOURNAL);
}


function mountFailure(code, error, cleanupError = null)
{
    const cleanup = cleanupError
        ? ` Cleanup also failed: ${cleanupError?.message ?? cleanupError}.`
        : "";
    return Object.assign(new Error(
        `Engine filesystem mount failed: ${error?.message ?? error}.${cleanup}`), {
        code,
        cause: error,
    });
}

export async function mountWorkerFilesystem(
    filesystem, manifest, mountRuntime, report = null)
{
    let runtimeMountStarted = false;
    try {
        const mounted = await filesystem.mount(manifest, report);
        runtimeMountStarted = true;
        const stopReadProgress = filesystem.observeReadProgress(report);
        try {
            mountRuntime();
        } finally {
            stopReadProgress();
        }
        await filesystem.checkpoint(report);
        return mounted;
    } catch (error) {
        let cleanupError = null;
        try {
            await filesystem.flushAndUnmount(report);
        } catch (failure) {
            cleanupError = failure;
        }
        if (cleanupError) {
            throw mountFailure("MOUNT_CLEANUP_FAILED", error, cleanupError);
        }
        if (runtimeMountStarted) {
            throw mountFailure("FILESYSTEM_OWNERSHIP_UNKNOWN", error);
        }
        throw mountFailure("MOUNT_FAILED_CLEAN", error);
    }
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
    let homeDirectory = null;
    let appDirectory = null;
    let homeLoaded = false;
    let persistChain = null;
    let persistenceFailure = null;
    const pendingPersistence = [];
    let pendingPersistenceBytes = 0;
    const writableReservations = new Map();
    let reservedSnapshotBytes = 0;
    const persistenceObservers = new Set();
    let acceptingWrites = true;
    let flushPromise = null;
    let homeBytes = 0;
    let module = null;
    let readProgress = null;

    // Native bootstrap is synchronous and cannot run a timer heartbeat.
    // Successful reads report actual work to the host during this scope.
    function observeReadProgress(report)
    {
        const previous = readProgress;
        let bytesProcessed = 0;
        readProgress = report ? (bytes) => {
            bytesProcessed += bytes;
            report({ phase: "runtime-loading", filesProcessed: 0, bytesProcessed });
        } : null;
        report?.({ phase: "runtime-loading", filesProcessed: 0, bytesProcessed });
        return () => { readProgress = previous; };
    }

    function closeMountedFilesystem()
    {
        descriptors.clear();
        writableReservations.clear();
        reservedSnapshotBytes = 0;
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
    }

    function resetHomeCache()
    {
        homeFiles.clear();
        homeDirectories.clear();
        homeDirectories.add("");
        homeDirectoryEntries.clear();
        homeDirectoryEntries.set("", new Map());
        homeDirectory = null;
        appDirectory = null;
        homeLoaded = false;
        homeBytes = 0;
    }

    function addHomeDirectory(logicalPath)
    {
        if (homeDirectories.has(logicalPath)) return;
        if (!validHomePath(logicalPath)) throw new Error("The browser home path exceeds canonical MAX_OSPATH.");
        const missing = [];
        let parent = "";
        for (const name of logicalPath.split("/")) {
            const path = parent ? `${parent}/${name}` : name;
            if (homeFiles.has(path)) throw new Error(`The browser home path contains a file/directory conflict: ${path}.`);
            if (!homeDirectories.has(path)) missing.push({ path, parent, name });
            parent = path;
        }
        if (homeDirectories.size - 1 + missing.length > MAX_HOME_DIRECTORIES) {
            throw new Error("The browser home path exceeds its directory-count limit; export stored saves before recovery.");
        }
        for (const { path, parent, name } of missing) {
            homeDirectories.add(path);
            homeDirectoryEntries.set(path, new Map());
            homeDirectoryEntries.get(parent).set(name, { name, type: "directory", size: 0 });
        }
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
            const storedPath = relative ? `${relative}/${name}` : name;
            const logicalPath = normalizeLogicalPath(storedPath);
            if (!logicalPath || !validHomePath(logicalPath)) throw new Error("The browser home contains an invalid path; export stored saves before recovery.");
            // All live mutations publish normalized names. Accepting a legacy
            // spelling here would create a second physical file on its next write.
            if (logicalPath !== storedPath) throw new Error("The browser home contains a normalized path conflict; export stored saves before recovery.");
            if (homeFiles.has(logicalPath) || homeDirectories.has(logicalPath)) {
                throw new Error(`The browser home contains a normalized path conflict: ${logicalPath}. Export stored saves before recovery.`);
            }
            if (handle.kind === "directory") {
                addHomeDirectory(logicalPath);
                await loadHomeDirectory(handle, logicalPath, budget, report);
                continue;
            }
            if (handle.kind !== "file" || ++budget.files > MAX_HOME_FILES) {
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
        resetHomeCache();
        try {
            appDirectory = await childDirectory(root, [APP_DIRECTORY], true);
            homeDirectory = await childDirectory(appDirectory, [HOME_DIRECTORY], true);
            await replayHomeRestore(appDirectory, homeDirectory);
            await recoverRename(appDirectory, homeDirectory);
            await loadHomeDirectory(homeDirectory, "", { bytes: 0, files: 0 }, report);
            homeLoaded = true;
        } catch (error) {
            resetHomeCache();
            throw error;
        }
    }

    async function persistHomeFile(file, logicalPath, bytes, version)
    {
        if (!homeDirectory) return;
        await faults?.beforePersist?.(logicalPath);
        const segments = logicalPath.split("/");
        const name = segments.pop();
        const directory = await childDirectory(homeDirectory, segments, true);
        await publishBytes(directory, name, bytes);
        const current = homeFiles.get(logicalPath);
        if (current === file && file.logicalPath === logicalPath &&
            current.version === version) current.persistedVersion = version;
    }

    function canQueue(bytes = 0, operations = 1)
    {
        return pendingPersistence.length + writableReservations.size + operations <= MAX_PENDING_OPERATIONS &&
            pendingPersistenceBytes + reservedSnapshotBytes + bytes <= MAX_PENDING_BYTES;
    }

    function reserveWritable(file, size)
    {
        const previous = writableReservations.get(file);
        if (!canQueue(size - (previous ?? 0), previous === undefined ? 1 : 0)) return false;
        reservedSnapshotBytes += size - (previous ?? 0);
        writableReservations.set(file, size);
        return true;
    }

    function releaseWritable(file)
    {
        reservedSnapshotBytes -= writableReservations.get(file) ?? 0;
        writableReservations.delete(file);
    }

    function drainPersistence(retry = false)
    {
        if (persistChain) return persistChain;
        if (persistenceFailure && !retry) return Promise.reject(persistenceFailure);
        persistenceFailure = null;
        const running = Promise.resolve().then(async () => {
            while (pendingPersistence.length > 0) {
                const operation = pendingPersistence[0];
                await operation.run();
                pendingPersistence.shift();
                pendingPersistenceBytes -= operation.retainedBytes;
                for (const observer of persistenceObservers) observer(operation.progress);
            }
        });
        persistChain = running.catch((error) => {
            persistenceFailure = error;
            throw error;
        }).finally(() => { persistChain = null; });
        return persistChain;
    }

    function schedulePersistence(run, progress = {
        phase: "persisting", files: 0, bytes: 0,
    }, retainedBytes = progress.bytes)
    {
        const operation = { run, progress, retainedBytes };
        pendingPersistence.push(operation);
        pendingPersistenceBytes += retainedBytes;
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
            await drainPersistence(true);
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

        if (flushPromise) await flushPromise;
        if (!acceptingWrites && homeLoaded) {
            throw Object.assign(new Error(
                "Browser home persistence must be retried before remounting."), {
                code: "HOME_FLUSH_RETRY_REQUIRED",
            });
        }

        closeMountedFilesystem();
        acceptingWrites = true;
        if (module) module.webImageClosing = false;
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
            report?.({
                phase: "complete",
                filesProcessed,
                bytesProcessed,
                totalFiles: manifest.files.length,
                totalBytes,
            });
            return { fileCount: files.size };
        } catch (error) {
            closeMountedFilesystem();
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
        if (!logicalPath || !validHomePath(logicalPath)) return -1;
        const segments = logicalPath.split("/");
        const parent = segments.slice(0, -1).join("/");
        if (!homeDirectories.has(parent) || directories.has(logicalPath) ||
            homeDirectories.has(logicalPath)) return -1;
        let file = homeFiles.get(logicalPath);
        if (file && [...descriptors.values()].some((open) => open.file === file && open.writable)) return -1;
        if (!canQueue(append ? file?.size ?? 0 : 0)) return -1;
        if (!file) {
            if (homeFiles.size >= MAX_HOME_FILES) return -1;
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
        reserveWritable(file, file.size);
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
        if (!acceptingWrites || !canQueue()) return false;
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

    function removeHomeTree(path)
    {
        if (!acceptingWrites || !canQueue()) return false;
        const logicalPath = normalizeDirectoryPath(path);
        if (!logicalPath || !homeDirectories.has(logicalPath)) return false;
        const prefix = `${logicalPath}/`;
        const treeFiles = [...homeFiles.entries()].filter(([name]) =>
            name.startsWith(prefix));
        if ([...descriptors.values()].some(({ file }) =>
            file.logicalPath.startsWith(prefix))) return false;

        for (const [name, file] of treeFiles) {
            homeFiles.delete(name);
            homeBytes -= file.size;
            const segments = name.split("/");
            const entryName = segments.pop();
            homeDirectoryEntries.get(segments.join("/"))?.delete(entryName);
        }
        const treeDirectories = [...homeDirectories].filter((name) =>
            name === logicalPath || name.startsWith(prefix))
            .sort((left, right) => right.length - left.length);
        for (const name of treeDirectories) {
            homeDirectories.delete(name);
            homeDirectoryEntries.delete(name);
        }
        const segments = logicalPath.split("/");
        const name = segments.pop();
        homeDirectoryEntries.get(segments.join("/"))?.delete(name);
        void schedulePersistence(async () => {
            if (!homeDirectory) return;
            try {
                const directory = await childDirectory(homeDirectory, segments);
                await directory.removeEntry(name, { recursive: true });
            } catch (error) {
                if (error?.name !== "NotFoundError") throw error;
            }
        }, { phase: "removing", files: treeFiles.length, bytes: 0 });
        faults?.onDirty?.();
        return true;
    }

    function renameHomePath(from, to)
    {
        if (!acceptingWrites) return false;
        const sourcePath = normalizeLogicalPath(from);
        const destinationPath = normalizeLogicalPath(to);
        const source = sourcePath ? homeFiles.get(sourcePath) : null;
        if (!source || !destinationPath || !validHomePath(destinationPath) || sourcePath === destinationPath ||
            [...descriptors.values()].some((open) => open.file === source)) return false;
        const destinationSegments = destinationPath.split("/");
        destinationSegments.pop();
        const destinationParent = destinationSegments.join("/");
        if (!homeDirectories.has(destinationParent) ||
            homeDirectories.has(destinationPath)) return false;
        const destination = homeFiles.get(destinationPath);
        if (destination && [...descriptors.values()].some((open) => open.file === destination)) return false;
        const journal = new TextEncoder().encode(JSON.stringify({
            version: 1, source: sourcePath, destination: destinationPath,
        }));
        if (journal.byteLength > 65536 || !canQueue(source.size + journal.byteLength)) return false;
        if (destination) homeBytes -= destination.size;
        const sourceSegments = sourcePath.split("/");
        const sourceName = sourceSegments.pop();
        homeFiles.delete(sourcePath);
        homeDirectoryEntries.get(sourceSegments.join("/"))?.delete(sourceName);
        source.logicalPath = destinationPath;
        ++source.version;
        publishHomeFile(source);
        const snapshot = source.bytes.slice(0, source.size);
        const version = source.version;
        // One serialized rename owns the journal. Replay runs before recovery
        // budgets, including the temporary source/destination duplication.
        // Keep the old destination until close, then retire source and journal.
        let step = 0;
        void schedulePersistence(async () => {
            if (!homeDirectory) return;
            if (step === 0) {
                await publishBytes(appDirectory, RENAME_JOURNAL, journal);
                step = 1;
            }
            if (step === 1) {
                await persistHomeFile(source, destinationPath, snapshot, version);
                step = 2;
            }
            if (step === 2) {
                const directory = await childDirectory(homeDirectory, sourceSegments);
                await removeDurable(directory, sourceName);
                step = 3;
            }
            await removeDurable(appDirectory, RENAME_JOURNAL);
        }, { phase: "persisting", files: 1, bytes: snapshot.byteLength }, snapshot.byteLength + journal.byteLength);
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
        const bytes = [...dirty].reduce((sum, file) => sum + file.size, 0);
        if (!canQueue(bytes, dirty.size)) {
            throw new Error("Browser home persistence is full; checkpoint must be retried after pending writes finish.");
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
        await Promise.all(module?.webImageTasks ?? []);
        // Drain first: retries do not enqueue another copy of every open file.
        await drainPersistenceWithProgress(report);
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
        if (module) module.webImageClosing = true;
        const operation = (async () => {
            await Promise.all(module?.webImageTasks ?? []);
            acceptingWrites = false;
            await drainPersistenceWithProgress(report);
            report?.({ phase: "snapshotting", filesProcessed: 0, bytesProcessed: 0 });
            scheduleDirtyOpenFiles();
            await drainPersistenceWithProgress(report);
            const summary = persistenceSummary();
            report?.({
                phase: "closing-handles",
                filesProcessed: summary.filesPersisted,
                bytesProcessed: summary.bytesPersisted,
            });
            closeMountedFilesystem();
            report?.({
                phase: "unmounting",
                filesProcessed: summary.filesPersisted,
                bytesProcessed: summary.bytesPersisted,
            });
            resetHomeCache();
            report?.({
                phase: "complete",
                filesProcessed: summary.filesPersisted,
                bytesProcessed: summary.bytesPersisted,
            });
            return summary;
        })();
        const tracked = operation.finally(() => {
            if (flushPromise === tracked) flushPromise = null;
        });
        flushPromise = tracked;
        return tracked;
    }

    function installForModule(wasmModule)
    {
        module = wasmModule;
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
                if (bytesRead > 0) readProgress?.(bytesRead);
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
                    !canQueue(Math.max(open.file.size, required) - writableReservations.get(open.file), 0) ||
                    !ensureHomeCapacity(open.file, required)) return -1;
                reserveWritable(open.file, Math.max(open.file.size, required));
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
                if (open.writable) {
                    releaseWritable(open.file);
                    scheduleHomeFilePersistence(open.file);
                }
                return true;
            },
            mkdir(path) {
                if (!acceptingWrites || !canQueue()) return false;
                const logicalPath = normalizeDirectoryPath(path);
                if (logicalPath === null || files.has(logicalPath) ||
                    homeFiles.has(logicalPath)) return false;
                try { addHomeDirectory(logicalPath); } catch { return false; }
                void schedulePersistence(() => childDirectory(
                    homeDirectory,
                    logicalPath ? logicalPath.split("/") : [],
                    true,
                ), { phase: "persisting", files: 1, bytes: 0 });
                faults?.onDirty?.();
                return true;
            },
            remove(path) { return removeHomePath(path); },
            removeTree(path) { return removeHomeTree(path); },
            rename(from, to) { return renameHomePath(from, to); },
        });
    }

    return Object.freeze({
        mount,
        unmount: closeMountedFilesystem,
        checkpoint,
        flushAndUnmount,
        installForModule,
        observeReadProgress,
        persistenceUsage() {
            return {
                pendingOperations: pendingPersistence.length,
                pendingSnapshotBytes: pendingPersistenceBytes,
                reservedOperations: writableReservations.size,
                reservedSnapshotBytes,
                liveFiles: homeFiles.size,
                liveBytes: homeBytes,
                failed: persistenceFailure !== null,
            };
        },
    });
}
