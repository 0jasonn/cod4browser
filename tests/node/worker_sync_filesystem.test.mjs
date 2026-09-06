import assert from "node:assert/strict";
import { after, test } from "node:test";

import { createWorkerSyncFilesystem, mountWorkerFilesystem, readHomeRecoveryFiles,
    createHomeBackup, readHomeBackup, restoreHomeFiles, restoreRawHomeFile, resumeHomeRestore } from "../../web/worker_sync_filesystem.mjs";

const IMPORT_ID = "filesystem-ordering-tests";
const originalNavigator = Object.getOwnPropertyDescriptor(globalThis, "navigator");
let durableObserver = null;

class MemoryFileHandle
{
    constructor(name)
    {
        this.kind = "file";
        this.name = name;
        this.bytes = new Uint8Array();
        this.failure = null;
        this.aborts = 0;
    }

    async getFile()
    {
        const snapshot = this.bytes.slice();
        return new Blob([snapshot]);
    }

    async createWritable()
    {
        if (this.failure === "create") throw domError("QuotaExceededError");
        let pending = this.bytes.slice();
        const handle = this;
        return {
            async write(bytes) {
                pending = new Uint8Array(bytes).slice();
                if (handle.failure === "write" || handle.failure === "abort") {
                    pending = pending.slice(0, 1);
                    throw domError("QuotaExceededError");
                }
            },
            async truncate(size) {
                pending = pending.slice(0, size);
                if (handle.failure === "truncate") throw domError("QuotaExceededError");
            },
            async close() {
                if (handle.failure === "close") throw domError("QuotaExceededError");
                handle.bytes = pending;
                durableObserver?.();
            },
            async abort() {
                ++handle.aborts;
                pending = null;
                if (handle.failure === "abort") throw domError("AbortError");
            },
        };
    }
}

class MemoryDirectoryHandle
{
    constructor(name = "")
    {
        this.kind = "directory";
        this.name = name;
        this.children = new Map();
        this.entriesRead = 0;
    }

    async getDirectoryHandle(name, { create = false } = {})
    {
        const existing = this.children.get(name);
        if (existing?.kind === "directory") return existing;
        if (existing || !create) throw domError("NotFoundError");
        const directory = new MemoryDirectoryHandle(name);
        this.children.set(name, directory);
        return directory;
    }

    async getFileHandle(name, { create = false } = {})
    {
        const existing = this.children.get(name);
        if (existing?.kind === "file") return existing;
        if (existing || !create) throw domError("NotFoundError");
        const file = new MemoryFileHandle(name);
        this.children.set(name, file);
        return file;
    }

    async removeEntry(name)
    {
        if (this.removeFailure === name) throw domError("NotAllowedError");
        if (!this.children.delete(name)) throw domError("NotFoundError");
        durableObserver?.();
    }

    async *entries()
    {
        ++this.entriesRead;
        yield* this.children.entries();
    }
}

function domError(name)
{
    return Object.assign(new Error(name), { name });
}

function deferred()
{
    let resolve;
    const promise = new Promise((done) => { resolve = done; });
    return { promise, resolve };
}

async function childDirectory(root, path, create = false)
{
    let directory = root;
    for (const segment of path.split("/").filter(Boolean)) {
        directory = await directory.getDirectoryHandle(segment, { create });
    }
    return directory;
}

async function createRoot()
{
    const root = new MemoryDirectoryHandle();
    const imports = await childDirectory(root, "kisakcod-web/imports", true);
    await imports.getDirectoryHandle(IMPORT_ID, { create: true });
    return root;
}

async function mount(root, faults = null)
{
    Object.defineProperty(globalThis, "navigator", {
        configurable: true,
        value: { storage: { async getDirectory() { return root; } } },
    });
    const filesystem = createWorkerSyncFilesystem(faults);
    const module = { HEAPU8: new Uint8Array(4096) };
    filesystem.installForModule(module);
    await filesystem.mount({ importId: IMPORT_ID, files: [] });
    return {
        filesystem,
        io: globalThis.__KISAKCOD_SYNC_FS__,
        heap: module.HEAPU8,
        module,
    };
}

async function remount(root, harness)
{
    Object.defineProperty(globalThis, "navigator", {
        configurable: true,
        value: { storage: { async getDirectory() { return root; } } },
    });
    await harness.filesystem.mount({ importId: IMPORT_ID, files: [] });
    return harness;
}

async function writeDurableText(root, path, text)
{
    const segments = path.split("/");
    const name = segments.pop();
    const home = await childDirectory(root, "kisakcod-web/home", true);
    const directory = await childDirectory(home, segments.join("/"), true);
    const handle = await directory.getFileHandle(name, { create: true });
    const writable = await handle.createWritable();
    const bytes = new TextEncoder().encode(text);
    await writable.write(bytes);
    await writable.truncate(bytes.byteLength);
    await writable.close();
}

function writeText(harness, path, text)
{
    const bytes = new TextEncoder().encode(text);
    harness.heap.set(bytes, 64);
    const descriptor = harness.io.openWrite(path);
    assert.notEqual(descriptor, -1);
    assert.equal(harness.io.write(descriptor, 64, bytes.byteLength), bytes.byteLength);
    assert.equal(harness.io.close(descriptor), true);
}

function readText(harness, path)
{
    const stat = harness.io.stat(path);
    if (!stat) return null;
    const descriptor = harness.io.open(path);
    assert.notEqual(descriptor, -1);
    const bytesRead = harness.io.read(descriptor, 512, stat.size);
    assert.equal(bytesRead, stat.size);
    assert.equal(harness.io.close(descriptor), true);
    return new TextDecoder().decode(harness.heap.subarray(512, 512 + bytesRead));
}

async function restart(root, harness)
{
    harness.filesystem.unmount();
    return mount(root);
}

async function expectRestartedFile(root, harness, path, expected)
{
    const remounted = await restart(root, harness);
    assert.equal(readText(remounted, path), expected);
    return remounted;
}

async function homeContents(root)
{
    const home = await childDirectory(root, "kisakcod-web/home");
    const files = new Map();
    async function visit(directory, prefix = "")
    {
        for await (const [name, handle] of directory.entries()) {
            const path = prefix ? `${prefix}/${name}` : name;
            if (handle.kind === "directory") await visit(handle, path);
            else files.set(path, new Uint8Array(await (await handle.getFile()).arrayBuffer()));
        }
    }
    await visit(home);
    return files;
}

after(() => {
    if (originalNavigator) Object.defineProperty(globalThis, "navigator", originalNavigator);
    else delete globalThis.navigator;
    delete globalThis.__KISAKCOD_WEB_FS_BRIDGE__;
    delete globalThis.__KISAKCOD_SYNC_FS__;
});

test("failed overwrite retains the complete durable destination and supports retry", async () => {
    for (const path of ["config.cfg", "save.svg"]) {
        for (const failure of ["create", "write", "truncate", "close", "abort"]) {
            const root = await createRoot();
            await writeDurableText(root, path, "complete old contents");
            await writeDurableText(root, "temp.svg", "complete replacement");
            const home = await childDirectory(root, "kisakcod-web/home");
            const destination = await home.getFileHandle(path);
            destination.failure = failure;
            const harness = await mount(root);
            assert.equal(harness.io.rename("temp.svg", path), true);
            await assert.rejects(harness.filesystem.checkpoint(), { name: "QuotaExceededError" });
            const durable = await homeContents(root);
            assert.equal(new TextDecoder().decode(durable.get(path)), "complete old contents", failure);
            assert.equal(new TextDecoder().decode(durable.get("temp.svg")), "complete replacement");
            if (failure !== "create") assert.ok(destination.aborts > 0);
            destination.failure = null;
            await harness.filesystem.checkpoint();
            await harness.filesystem.checkpoint();
            const reopened = await mount(root);
            assert.equal(readText(reopened, path), "complete replacement");
            assert.equal(reopened.io.stat("temp.svg"), null);
        }
    }
});

test("live file-count boundary agrees with cold recovery", async () => {
    const root = await createRoot();
    const harness = await mount(root);
    for (let index = 0; index < 8191; ++index) {
        const descriptor = harness.io.openWrite(`file-${index}`);
        assert.notEqual(descriptor, -1, `file ${index}`);
        assert.equal(harness.io.close(descriptor), true);
    }
    assert.equal(harness.io.openWrite("overflow"), -1);
    assert.equal(harness.io.stat("overflow"), null);
    await harness.filesystem.checkpoint();
    const reopened = await mount(root);
    assert.equal(reopened.io.list("").length, 8191);
    writeText(reopened, "file-0", "overwrite");
    assert.equal(reopened.io.remove("file-1"), true);
    writeText(reopened, "replacement", "");
    await reopened.filesystem.checkpoint();
    assert.equal((await mount(root)).io.list("").length, 8191);
});

function cloneDurable(directory)
{
    const copy = new MemoryDirectoryHandle(directory.name);
    for (const [name, entry] of directory.children) {
        if (entry.kind === "directory") copy.children.set(name, cloneDurable(entry));
        else {
            const file = new MemoryFileHandle(name);
            file.bytes = entry.bytes.slice();
            copy.children.set(name, file);
        }
    }
    return copy;
}

test("home backup restores opaque nested and empty files without overwriting existing paths", async () => {
    const source = await createRoot();
    await writeDurableText(source, "profiles/player/config.cfg", "synthetic config");
    await writeDurableText(source, "profiles/player/save/slot.svg", "synthetic save");
    await writeDurableText(source, "empty", "");
    const backup = await createHomeBackup(source);
    const files = await readHomeBackup(backup);
    const target = await createRoot();
    await writeDurableText(target, "preserved.cfg", "original");
    assert.deepEqual(await restoreHomeFiles(target, files), { filesRestored: 3, bytesRestored: 30 });
    const fresh = await mount(target);
    assert.equal(readText(fresh, "profiles/player/save/slot.svg"), "synthetic save");
    assert.equal(readText(fresh, "preserved.cfg"), "original");
    assert.equal(readText(fresh, "empty"), "");
    const before = await (await createHomeBackup(target)).arrayBuffer();
    await assert.rejects(restoreHomeFiles(target, files), /overwrite/u);
    assert.deepEqual(await (await createHomeBackup(target)).arrayBuffer(), before);
    await restoreRawHomeFile(target, "raw/config.cfg", new Blob(["raw exported file"]));
    assert.equal(readText(await mount(target), "raw/config.cfg"), "raw exported file");
    await assert.rejects(restoreRawHomeFile(target, "../raw.cfg", new Blob(["bad"])), /invalid/u);
    await assert.rejects(restoreRawHomeFile(target, "raw/config.cfg", new Blob(["replacement"])), /overwrite/u);
});

test("restore validates paths, counts, sizes, conflicts and checksums before home publication", async () => {
    const source = await createRoot();
    await writeDurableText(source, "slot.svg", "synthetic");
    const [entry] = await readHomeBackup(await createHomeBackup(source));
    for (const entries of [
        [{ ...entry, path: "../slot.svg" }], [{ ...entry, path: "Slot.svg" }],
        [{ ...entry, path: "é".repeat(130) }], [entry, entry],
        [{ ...entry, size: 64 * 1024 * 1024 + 1 }],
        [{ ...entry, size: -1 }],
        [entry, { ...entry, path: "slot.svg/child" }],
        Array.from({ length: 8192 }, (_, index) => ({ ...entry, path: `slot-${index}` })),
        [{ ...entry, sha256: "0".repeat(64) }],
    ]) {
        const target = await createRoot();
        await writeDurableText(target, "keep.cfg", "untouched");
        const before = await (await createHomeBackup(target)).arrayBuffer();
        await assert.rejects(restoreHomeFiles(target, entries));
        assert.deepEqual(await (await createHomeBackup(target)).arrayBuffer(), before);
    }
    const backup = await createHomeBackup(source);
    for (const invalid of [backup.slice(0, 8), backup.slice(0, backup.size - 1),
        new Blob([backup, "extra"]), new Blob(["KISAKHOME2\n", backup.slice(11)])])
        await assert.rejects(readHomeBackup(invalid));
});

test("each interrupted restore step replays completely or leaves the original home unchanged", async () => {
    const source = await createRoot();
    await writeDurableText(source, "saves/one.svg", "one complete save");
    await writeDurableText(source, "saves/two.svg", "two complete save");
    const files = await readHomeBackup(await createHomeBackup(source));
    const target = await createRoot();
    await writeDurableText(target, "keep.cfg", "old config");
    const checkpoints = [cloneDurable(target)];
    durableObserver = () => checkpoints.push(cloneDurable(target));
    try { await restoreHomeFiles(target, files); }
    finally { durableObserver = null; }
    for (const root of checkpoints) {
        const app = await childDirectory(root, "kisakcod-web");
        const committed = app.children.has("home-restore.json") ||
            app.children.get("home")?.children.get("saves")?.children.has("two.svg");
        for (let retry = 0; retry < 2; ++retry) {
            const fresh = await mount(root);
            assert.equal(readText(fresh, "keep.cfg"), "old config");
            assert.equal(readText(fresh, "saves/one.svg"), committed ? "one complete save" : null);
            assert.equal(readText(fresh, "saves/two.svg"), committed ? "two complete save" : null);
        }
    }
});

test("restore cancellation, quota failure and failed journal retirement preserve retry sources", async () => {
    const source = await createRoot();
    await writeDurableText(source, "slot.svg", "complete save");
    const entries = await readHomeBackup(await createHomeBackup(source));
    const root = await createRoot();
    await writeDurableText(root, "keep.cfg", "original");
    const aborted = new AbortController();
    aborted.abort();
    await assert.rejects(restoreHomeFiles(root, entries, aborted.signal), { name: "AbortError" });
    const app = await childDirectory(root, "kisakcod-web");
    const home = await childDirectory(app, "home");
    const getFile = home.getFileHandle.bind(home);
    home.getFileHandle = async (name, options) => {
        const handle = await getFile(name, options);
        if (name === "slot.svg") handle.failure = "write";
        return handle;
    };
    await assert.rejects(restoreHomeFiles(root, entries), { name: "QuotaExceededError" });
    assert.ok(app.children.has("home-restore.json"));
    assert.equal(home.children.has("slot.svg"), false);
    home.getFileHandle = getFile;
    app.removeFailure = "home-restore.json";
    await assert.rejects(resumeHomeRestore(root), { name: "NotAllowedError" });
    app.removeFailure = null;
    await resumeHomeRestore(root);
    const fresh = await mount(root);
    assert.equal(readText(fresh, "slot.svg"), "complete save");
    assert.equal(readText(fresh, "keep.cfg"), "original");
    const cleanupRoot = await createRoot();
    const cleanupApp = await childDirectory(cleanupRoot, "kisakcod-web");
    durableObserver = () => {
        if (cleanupApp.children.has("home-restore.json")) cleanupApp.removeFailure = "home-restore";
    };
    try {
        await assert.rejects(restoreHomeFiles(cleanupRoot, entries), error =>
            error.code === "RESTORE_COMMITTED_CLEANUP_FAILED" && error.cause.name === "NotAllowedError");
    } finally { durableObserver = null; }
    cleanupApp.removeFailure = null;
    assert.equal((await resumeHomeRestore(cleanupRoot)).recovered, false);
    assert.equal(cleanupApp.children.has("home-restore"), false);
    assert.equal(readText(await mount(cleanupRoot), "slot.svg"), "complete save");
});

test("restore budgets include existing files and reject before staging", async () => {
    const source = await createRoot();
    await writeDurableText(source, "new.svg", "new save");
    const entries = await readHomeBackup(await createHomeBackup(source));
    for (const boundary of ["files", "bytes", "directories"]) {
        const root = await createRoot();
        const home = await childDirectory(root, "kisakcod-web/home", true);
        if (boundary === "files") {
            for (let index = 0; index < 8191; ++index) await home.getFileHandle(`old-${index}`, { create: true });
        } else if (boundary === "directories") {
            for (let index = 0; index < 8191; ++index) await home.getDirectoryHandle(`d${index}`, { create: true });
        } else {
            // Metadata preflight must reject without reading these contents.
            for (const name of ["one", "two"]) {
                const file = await home.getFileHandle(name, { create: true });
                file.getFile = async () => ({ size: 64 * 1024 * 1024,
                    arrayBuffer() { throw new Error("Budget rejection must precede byte reads"); } });
            }
        }
        const selected = boundary === "directories" ? [{ ...entries[0], path: "overflow/new.svg" }] : entries;
        await assert.rejects(restoreHomeFiles(root, selected), /limit/u);
        const app = await childDirectory(root, "kisakcod-web");
        assert.equal(app.children.has("home-restore"), false);
        assert.equal(home.children.has("new.svg"), false);
        assert.equal(home.children.has("overflow"), false);
    }
});

test("restore snapshots validated metadata and damaged staging never publishes", async () => {
    const source = await createRoot();
    await writeDurableText(source, "new.svg", "new save");
    const entries = await readHomeBackup(await createHomeBackup(source));
    const root = await createRoot();
    await writeDurableText(root, "keep.cfg", "old config");
    let interrupted;
    durableObserver = () => {
        const app = root.children.get("kisakcod-web");
        if (app.children.has("home-restore.json") && !interrupted) interrupted = cloneDurable(root);
    };
    try {
        const restoring = restoreHomeFiles(root, entries);
        entries[0].path = "keep.cfg";
        await restoring;
    } finally { durableObserver = null; }
    assert.equal(readText(await mount(root), "keep.cfg"), "old config");
    assert.equal(readText(await mount(root), "new.svg"), "new save");
    assert.ok(interrupted);
    const app = await childDirectory(interrupted, "kisakcod-web");
    await app.getFileHandle("home-rename.json", { create: true });
    await assert.rejects(mount(interrupted), /Conflicting home recovery records/u);
    await app.removeEntry("home-rename.json");
    const stage = await childDirectory(interrupted, "kisakcod-web/home-restore");
    stage.children.get("0").bytes = new TextEncoder().encode("bad data");
    await assert.rejects(mount(interrupted), /damaged/u);
    const home = await childDirectory(interrupted, "kisakcod-web/home");
    assert.equal(home.children.has("new.svg"), false);
    assert.equal(await (await home.children.get("keep.cfg").getFile()).text(), "old config");
});

test("every durable rename step replays idempotently in a fresh filesystem", async () => {
    for (const overwrite of [false, true]) {
        const root = await createRoot();
        if (overwrite) await writeDurableText(root, "save.svg", "old save");
        await writeDurableText(root, "temp.svg", "new save");
        const harness = await mount(root);
        const checkpoints = [cloneDurable(root)];
        durableObserver = () => checkpoints.push(cloneDurable(root));
        try {
            assert.equal(harness.io.rename("temp.svg", "save.svg"), true);
            await harness.filesystem.flushAndUnmount();
        } finally { durableObserver = null; }
        assert.ok(checkpoints.length >= 5);
        for (const state of checkpoints) {
            const recovered = await mount(state);
            const value = readText(recovered, "save.svg");
            assert.ok(value === "new save" || (overwrite ? value === "old save" : value === null));
            if (value !== "new save") assert.equal(readText(recovered, "temp.svg"), "new save");
            const repeated = await mount(state);
            assert.equal(readText(repeated, "save.svg"), value);
        }
    }
});

test("failed source or journal retirement preserves publication and retries only cleanup", async () => {
    for (const failure of ["temp.svg", "home-rename.json"]) {
        const root = await createRoot();
        await writeDurableText(root, "save.svg", "old");
        await writeDurableText(root, "temp.svg", "new");
        const directory = await childDirectory(root,
            failure === "temp.svg" ? "kisakcod-web/home" : "kisakcod-web");
        directory.removeFailure = failure;
        let publications = 0;
        const harness = await mount(root, { beforePersist() { ++publications; } });
        assert.equal(harness.io.rename("temp.svg", "save.svg"), true);
        await assert.rejects(harness.filesystem.checkpoint(), { name: "NotAllowedError" });
        const interrupted = cloneDurable(root);
        const fresh = await mount(interrupted);
        assert.equal(readText(fresh, "save.svg"), "new");
        assert.equal(fresh.io.stat("temp.svg"), null);
        directory.removeFailure = null;
        await harness.filesystem.checkpoint();
        assert.equal(publications, 1, "cleanup retry must not republish destination");
    }
});

test("queue pressure rejects mutations before changing home and retries do not multiply work", async () => {
    const root = await createRoot();
    let attempts = 0;
    let fail = true;
    const harness = await mount(root, {
        beforePersist() { ++attempts; if (fail) throw domError("QuotaExceededError"); },
    });
    writeText(harness, "config.cfg", "first");
    await assert.rejects(harness.filesystem.checkpoint(), { name: "QuotaExceededError" });
    const failedAttempts = attempts;
    for (let index = 1; index < 16384; ++index) writeText(harness, "config.cfg", `${index}`);
    const usage = harness.filesystem.persistenceUsage();
    assert.equal(usage.pendingOperations, 16384);
    assert.equal(harness.io.openWrite("config.cfg"), -1);
    assert.equal(harness.io.openWrite("new.cfg"), -1);
    assert.equal(harness.io.remove("config.cfg"), false);
    assert.equal(harness.io.mkdir("new-dir"), false);
    assert.equal(harness.io.rename("config.cfg", "other.cfg"), false);
    assert.equal(readText(harness, "config.cfg"), "16383");
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(attempts, failedAttempts, "new operations cannot spin retrying failed storage");
    for (let index = 0; index < 3; ++index) {
        await assert.rejects(harness.filesystem.checkpoint(), { name: "QuotaExceededError" });
        assert.deepEqual(harness.filesystem.persistenceUsage(), usage);
    }
    fail = false;
    await harness.filesystem.checkpoint();
    assert.equal(harness.filesystem.persistenceUsage().pendingSnapshotBytes, 0);
    assert.equal(harness.filesystem.persistenceUsage().pendingOperations, 0);
    await expectRestartedFile(root, harness, "config.cfg", "16383");
});

test("open snapshot reservations bound byte pressure and survive rejected writes", async () => {
    const root = await createRoot();
    let fail = true;
    const harness = await mount(root, { beforePersist() { if (fail) throw domError("QuotaExceededError"); } });
    // Repeated 64 MiB snapshots of one file exhaust the separate 256 MiB queue.
    const size = 64 * 1024 * 1024;
    harness.module.HEAPU8 = new Uint8Array(size + 1);
    for (let index = 0; index < 4; ++index) {
        const descriptor = harness.io.openWrite("large.svg");
        assert.notEqual(descriptor, -1);
        assert.equal(harness.io.openWrite("large.svg"), -1, "one writable handle per file");
        assert.equal(harness.io.write(descriptor, 1, size), size);
        assert.equal(harness.io.write(descriptor, 1, 1), -1, "per-file byte limit");
        assert.equal(harness.io.close(descriptor), true);
    }
    assert.equal(harness.filesystem.persistenceUsage().pendingSnapshotBytes, 4 * size);
    const descriptor = harness.io.openWrite("empty.svg");
    assert.notEqual(descriptor, -1);
    assert.equal(harness.io.write(descriptor, 1, 1), -1);
    assert.equal(harness.io.size(descriptor), 0);
    assert.equal(harness.io.close(descriptor), true);
    await assert.rejects(harness.filesystem.checkpoint(), { name: "QuotaExceededError" });
    fail = false;
    await harness.filesystem.checkpoint();
    assert.equal(harness.filesystem.persistenceUsage().pendingSnapshotBytes, 0);
    assert.equal((await mount(root)).io.stat("large.svg").size, size);
});

test("directory count and canonical UTF-8 path budgets reject before mutation and reopen", async () => {
    const root = await createRoot();
    const harness = await mount(root);
    const limitPath = "a/".repeat(129) + "b"; // 259 UTF-8 bytes, 130 directories.
    assert.equal(harness.io.mkdir(limitPath), true);
    assert.equal(harness.io.mkdir(limitPath + "c"), false);
    assert.equal(harness.io.mkdir("é".repeat(130)), false);
    assert.equal(harness.io.stat("é".repeat(130)), null);
    for (let index = 130; index < 8190; ++index) assert.equal(harness.io.mkdir(`d${index}`), true);
    assert.equal(harness.io.mkdir("overflow/child"), false, "both missing prefixes are preflighted");
    assert.equal(harness.io.stat("overflow"), null);
    assert.equal(harness.io.mkdir("last"), true);
    assert.equal(harness.io.mkdir("excess"), false);
    const acceptedFile = "x".repeat(259);
    writeText(harness, acceptedFile, "ok");
    assert.equal(harness.io.openWrite(acceptedFile + "x"), -1);
    assert.equal(harness.io.rename(acceptedFile, "é".repeat(130)), false);
    assert.equal(readText(harness, acceptedFile), "ok");
    await harness.filesystem.flushAndUnmount();
    const fresh = await mount(root);
    assert.equal(fresh.io.stat(limitPath).type, "directory");
    assert.equal(readText(fresh, acceptedFile), "ok");
    assert.equal(fresh.io.mkdir("excess"), false);
    assert.equal(fresh.io.removeTree("last"), true);
    assert.equal(fresh.io.mkdir("replacement"), true);
    await fresh.filesystem.flushAndUnmount();
    assert.equal((await mount(root)).io.stat("replacement").type, "directory");
});

test("live byte limit includes open files and rejected growth preserves reservations", async () => {
    const root = await createRoot();
    const harness = await mount(root);
    const size = 64 * 1024 * 1024;
    harness.module.HEAPU8 = new Uint8Array(size + 1);
    const first = harness.io.openWrite("one.svg");
    const second = harness.io.openWrite("two.svg");
    assert.equal(harness.io.write(first, 1, size), size);
    assert.equal(harness.io.write(second, 1, size - 1), size - 1);
    assert.equal(harness.io.write(second, 1, 1), 1);
    const third = harness.io.openWrite("three.svg");
    const before = harness.filesystem.persistenceUsage();
    assert.equal(harness.io.write(third, 1, 1), -1);
    assert.deepEqual(harness.filesystem.persistenceUsage(), before);
    assert.equal(harness.io.size(third), 0);
    assert.equal(harness.io.close(first), true);
    assert.equal(harness.io.close(second), true);
    assert.equal(harness.io.close(third), true);
    await harness.filesystem.flushAndUnmount();
    const fresh = await mount(root);
    assert.equal(fresh.filesystem.persistenceUsage().liveBytes, 2 * size);
    assert.equal(fresh.io.remove("one.svg"), true);
    writeText(fresh, "three.svg", "reclaimed");
    await fresh.filesystem.flushAndUnmount();
    assert.equal(readText(await mount(root), "three.svg"), "reclaimed");
});

test("oversized and case-conflicting homes remain exportable without loading or modifying contents", async () => {
    for (const count of [8192, 2, 1]) {
        const root = await createRoot();
        const home = await childDirectory(root, "kisakcod-web/home", true);
        for (let index = 0; index < count; ++index) {
            await home.getFileHandle(count < 3 ? ["SAVE.svg", "save.svg"][index] : `save-${index}`, { create: true });
        }
        await assert.rejects(mount(root), /limit|conflict/u);
        for (const file of home.children.values()) file.getFile = () => { throw new Error("Export enumeration must not read contents"); };
        let exported = 0;
        for await (const file of readHomeRecoveryFiles(root)) {
            assert.ok(file.path.startsWith("home/"));
            ++exported;
        }
        assert.equal(exported, count);
        assert.equal(home.children.size, count);
    }
});

test("rename at the count boundary replays its temporary duplicate before recovery budgets", async () => {
    const root = await createRoot();
    const home = await childDirectory(root, "kisakcod-web/home", true);
    for (let index = 0; index < 8191; ++index) await home.getFileHandle(`save-${index}`, { create: true });
    home.removeFailure = "save-0";
    const harness = await mount(root);
    assert.equal(harness.io.rename("save-0", "renamed"), true);
    await assert.rejects(harness.filesystem.checkpoint(), { name: "NotAllowedError" });
    assert.equal(home.children.size, 8192);
    const fresh = await mount(cloneDurable(root));
    assert.equal(fresh.io.list("").length, 8191);
    assert.equal(fresh.io.stat("save-0"), null);
    assert.deepEqual(fresh.io.stat("renamed"), { type: "file", size: 0 });
    assert.equal(fresh.io.rename("renamed", "renamed"), false);
});

test("browser home preserves write, remove, recreate ordering across remount", async () => {
    const root = await createRoot();
    const harness = await mount(root);
    assert.equal(harness.io.mkdir("test"), true);
    writeText(harness, "test/a.cfg", "old");
    assert.equal(harness.io.remove("test/a.cfg"), true);
    writeText(harness, "test/a.cfg", "new");

    const summary = await harness.filesystem.checkpoint();
    assert.deepEqual(summary, { filesPersisted: 1, bytesPersisted: 3 });
    await expectRestartedFile(root, harness, "test/a.cfg", "new");
});

test("checkpoint and shutdown include pending image codec writes before closing the filesystem", async () => {
    for (const shutdown of [false, true]) {
        const root = await createRoot();
        const writer = await mount(root);
        const encoded = deferred();
        const image = encoded.promise.then(() => writeText(writer, "thumbnail.jpg", "encoded-image"));
        writer.module.webImageTasks = new Set([image]);
        let complete = false;
        const flushing = (shutdown ? writer.filesystem.flushAndUnmount() : writer.filesystem.checkpoint())
            .then(() => { complete = true; });
        for (let turn = 0; turn < 10; ++turn) await Promise.resolve();
        assert.equal(complete, false);
        assert.equal(writer.module.webImageClosing === true, shutdown);
        encoded.resolve();
        await flushing;
        const verifier = await mount(root);
        assert.equal(readText(verifier, "thumbnail.jpg"), "encoded-image");
    }
});

test("browser home removes a profile tree durably", async () => {
    const root = await createRoot();
    const harness = await mount(root);
    assert.equal(harness.io.mkdir("players/profiles/test"), true);
    assert.equal(harness.io.mkdir("players/profiles/test/save"), true);
    writeText(harness, "players/profiles/test/config.cfg", "setting");
    writeText(harness, "players/profiles/test/save/save.svg", "state");
    await harness.filesystem.checkpoint();

    assert.equal(harness.io.removeTree("players/profiles/test"), true);
    assert.equal(harness.io.stat("players/profiles/test"), null);
    await harness.filesystem.checkpoint();
    const remounted = await restart(root, harness);
    assert.equal(remounted.io.stat("players/profiles/test"), null);
});

test("rename over an existing destination is durable after remount", async () => {
    const root = await createRoot();
    const harness = await mount(root);
    writeText(harness, "config.cfg", "old");
    writeText(harness, "config.tmp", "new");
    assert.equal(harness.io.rename("config.tmp", "config.cfg"), true);

    await harness.filesystem.checkpoint();
    const remounted = await expectRestartedFile(root, harness, "config.cfg", "new");
    assert.equal(remounted.io.stat("config.tmp"), null);
});

test("two rapid atomic replacements preserve the last snapshot", async () => {
    const root = await createRoot();
    const harness = await mount(root);
    writeText(harness, "first.tmp", "one");
    assert.equal(harness.io.rename("first.tmp", "config.cfg"), true);
    writeText(harness, "second.tmp", "two");
    assert.equal(harness.io.rename("second.tmp", "config.cfg"), true);

    await harness.filesystem.checkpoint();
    const remounted = await expectRestartedFile(root, harness, "config.cfg", "two");
    assert.equal(remounted.io.stat("first.tmp"), null);
    assert.equal(remounted.io.stat("second.tmp"), null);
});

test("same-path replacement stays behind a blocked persistence operation", async () => {
    const root = await createRoot();
    const blocked = deferred();
    const started = deferred();
    const harness = await mount(root, {
        async beforePersist(path) {
            if (path === "blocker.cfg") {
                started.resolve();
                await blocked.promise;
            }
        },
    });
    writeText(harness, "blocker.cfg", "wait");
    await started.promise;
    writeText(harness, "target.cfg", "old");
    assert.equal(harness.io.remove("target.cfg"), true);
    writeText(harness, "target.cfg", "new");
    blocked.resolve();

    await harness.filesystem.checkpoint();
    await expectRestartedFile(root, harness, "target.cfg", "new");
});

test("remove and recreate is safe while the old write is active", async () => {
    const root = await createRoot();
    const blocked = deferred();
    const started = deferred();
    let first = true;
    const harness = await mount(root, {
        async beforePersist(path) {
            if (path === "active.cfg" && first) {
                first = false;
                started.resolve();
                await blocked.promise;
            }
        },
    });
    writeText(harness, "active.cfg", "old");
    await started.promise;
    assert.equal(harness.io.remove("active.cfg"), true);
    writeText(harness, "active.cfg", "new");
    blocked.resolve();

    await harness.filesystem.checkpoint();
    await expectRestartedFile(root, harness, "active.cfg", "new");
});

test("rename creates barriers for both source and destination paths", async () => {
    const root = await createRoot();
    const blocked = deferred();
    const started = deferred();
    const harness = await mount(root, {
        async beforePersist(path) {
            if (path === "blocker.cfg") {
                started.resolve();
                await blocked.promise;
            }
        },
    });
    writeText(harness, "blocker.cfg", "wait");
    await started.promise;
    writeText(harness, "destination.cfg", "old");
    writeText(harness, "source.tmp", "new");
    assert.equal(harness.io.rename("source.tmp", "destination.cfg"), true);
    blocked.resolve();

    await harness.filesystem.checkpoint();
    const remounted = await expectRestartedFile(
        root, harness, "destination.cfg", "new");
    assert.equal(remounted.io.stat("source.tmp"), null);
});

test("replacement persistence failure retries without losing ordering", async () => {
    const root = await createRoot();
    let fail = true;
    const harness = await mount(root, {
        async beforePersist(path) {
            if (fail && path === "config.cfg") throw domError("QuotaExceededError");
        },
    });
    writeText(harness, "config.cfg", "old");
    assert.equal(harness.io.remove("config.cfg"), true);
    writeText(harness, "config.cfg", "new");

    await assert.rejects(harness.filesystem.checkpoint(), { name: "QuotaExceededError" });
    fail = false;
    await harness.filesystem.checkpoint();
    await expectRestartedFile(root, harness, "config.cfg", "new");
});

test("checkpoint summary matches the final durable OPFS contents", async () => {
    const root = await createRoot();
    const harness = await mount(root);
    writeText(harness, "keep.cfg", "four");
    writeText(harness, "gone.cfg", "remove-me");
    assert.equal(harness.io.remove("gone.cfg"), true);

    const summary = await harness.filesystem.checkpoint();
    const contents = await homeContents(root);
    assert.equal(summary.filesPersisted, contents.size);
    assert.equal(summary.bytesPersisted,
        [...contents.values()].reduce((sum, bytes) => sum + bytes.byteLength, 0));
    await expectRestartedFile(root, harness, "keep.cfg", "four");
});

test("flushAndUnmount preserves mutation ordering", async () => {
    const root = await createRoot();
    const harness = await mount(root);
    writeText(harness, "profile.cfg", "old");
    assert.equal(harness.io.remove("profile.cfg"), true);
    writeText(harness, "profile.cfg", "new");

    const summary = await harness.filesystem.flushAndUnmount();
    assert.deepEqual(summary, { filesPersisted: 1, bytesPersisted: 3 });
    const remounted = await mount(root);
    assert.equal(readText(remounted, "profile.cfg"), "new");
});

test("every mutation sequence survives a fresh Worker filesystem instance", async () => {
    const root = await createRoot();
    let harness = await mount(root);
    writeText(harness, "profile.tmp", "one");
    assert.equal(harness.io.rename("profile.tmp", "profile.cfg"), true);
    await harness.filesystem.checkpoint();
    harness = await expectRestartedFile(root, harness, "profile.cfg", "one");

    assert.equal(harness.io.remove("profile.cfg"), true);
    writeText(harness, "profile.cfg", "two");
    await harness.filesystem.checkpoint();
    harness = await expectRestartedFile(root, harness, "profile.cfg", "two");

    writeText(harness, "profile.tmp", "three");
    assert.equal(harness.io.rename("profile.tmp", "profile.cfg"), true);
    await harness.filesystem.flushAndUnmount();
    harness = await mount(root);
    assert.equal(readText(harness, "profile.cfg"), "three");
    assert.equal(harness.io.stat("profile.tmp"), null);
});

test("a returning writer reloads changes from the intervening writer", async () => {
    const root = await createRoot();
    const writerA = await mount(root);
    writeText(writerA, "config.cfg", "A");
    await writerA.filesystem.flushAndUnmount();

    const writerB = await mount(root);
    assert.equal(readText(writerB, "config.cfg"), "A");
    writeText(writerB, "config.cfg", "B");
    await writerB.filesystem.flushAndUnmount();

    await remount(root, writerA);
    assert.equal(readText(writerA, "config.cfg"), "B");
});

test("a returning writer observes an intervening removal", async () => {
    const root = await createRoot();
    const writerA = await mount(root);
    writeText(writerA, "removed.cfg", "present");
    await writerA.filesystem.flushAndUnmount();

    const writerB = await mount(root);
    assert.equal(writerB.io.remove("removed.cfg"), true);
    await writerB.filesystem.flushAndUnmount();

    await remount(root, writerA);
    assert.equal(readText(writerA, "removed.cfg"), null);
});

test("a returning writer observes an intervening rename replacement", async () => {
    const root = await createRoot();
    const writerA = await mount(root);
    writeText(writerA, "config.cfg", "old");
    await writerA.filesystem.flushAndUnmount();

    const writerB = await mount(root);
    writeText(writerB, "config.tmp", "replacement");
    assert.equal(writerB.io.rename("config.tmp", "config.cfg"), true);
    await writerB.filesystem.flushAndUnmount();

    await remount(root, writerA);
    assert.equal(readText(writerA, "config.cfg"), "replacement");
    assert.equal(writerA.io.stat("config.tmp"), null);
});

test("a clean remount reloads the same durable home", async () => {
    const root = await createRoot();
    const writer = await mount(root);
    writeText(writer, "config.cfg", "durable");
    await writer.filesystem.flushAndUnmount();

    await remount(root, writer);
    assert.equal(readText(writer, "config.cfg"), "durable");
});

test("a failed flush retains dirty in-memory home state", async () => {
    const root = await createRoot();
    let fail = true;
    const writer = await mount(root, {
        async beforePersist(path) {
            if (fail && path === "dirty.cfg") throw domError("QuotaExceededError");
        },
    });
    writeText(writer, "dirty.cfg", "retryable");

    await assert.rejects(writer.filesystem.flushAndUnmount(), {
        name: "QuotaExceededError",
    });
    assert.equal(readText(writer, "dirty.cfg"), "retryable");
    fail = false;
    await writer.filesystem.flushAndUnmount();
});

test("a failed flush can be retried and then remounted durably", async () => {
    const root = await createRoot();
    let fail = true;
    const writer = await mount(root, {
        async beforePersist(path) {
            if (fail && path === "retry.cfg") throw domError("QuotaExceededError");
        },
    });
    writeText(writer, "retry.cfg", "saved-on-retry");

    await assert.rejects(writer.filesystem.flushAndUnmount(), {
        name: "QuotaExceededError",
    });
    fail = false;
    await writer.filesystem.flushAndUnmount();
    await remount(root, writer);
    assert.equal(readText(writer, "retry.cfg"), "saved-on-retry");
});

test("a failed flush cannot discard its cache by remounting", async () => {
    const root = await createRoot();
    let fail = true;
    const writer = await mount(root, {
        async beforePersist(path) {
            if (fail && path === "owned.cfg") throw domError("QuotaExceededError");
        },
    });
    const home = await childDirectory(root, "kisakcod-web/home");
    const loadsBeforeFlush = home.entriesRead;
    writeText(writer, "owned.cfg", "owned");

    await assert.rejects(writer.filesystem.flushAndUnmount(), {
        name: "QuotaExceededError",
    });
    await assert.rejects(remount(root, writer));
    assert.equal(home.entriesRead, loadsBeforeFlush);
    assert.equal(readText(writer, "owned.cfg"), "owned");
    fail = false;
    await writer.filesystem.flushAndUnmount();
});

test("durable home reload waits for the next writer tenure", async () => {
    const root = await createRoot();
    const writer = await mount(root);
    const home = await childDirectory(root, "kisakcod-web/home");
    writeText(writer, "config.cfg", "old");
    await writer.filesystem.flushAndUnmount();
    const loadsAfterFlush = home.entriesRead;

    await writeDurableText(root, "config.cfg", "new");
    assert.equal(home.entriesRead, loadsAfterFlush);
    assert.equal(writer.io.stat("config.cfg"), null);

    await remount(root, writer);
    assert.ok(home.entriesRead > loadsAfterFlush);
    assert.equal(readText(writer, "config.cfg"), "new");
});

test("a remount waits for old-tenure persistence to finish", async () => {
    const root = await createRoot();
    const blocked = deferred();
    const started = deferred();
    const writer = await mount(root, {
        async beforePersist(path) {
            if (path === "config.cfg") {
                started.resolve();
                await blocked.promise;
            }
        },
    });
    writeText(writer, "config.cfg", "old-tenure");
    await started.promise;

    const flushing = writer.filesystem.flushAndUnmount();
    let remounted = false;
    const remounting = remount(root, writer).then(() => { remounted = true; });
    for (let turn = 0; turn < 10; ++turn) await Promise.resolve();
    assert.equal(remounted, false);
    blocked.resolve();
    await flushing;
    await remounting;
    assert.equal(readText(writer, "config.cfg"), "old-tenure");

    writeText(writer, "config.cfg", "new-tenure");
    await writer.filesystem.flushAndUnmount();
    const verifier = await mount(root);
    assert.equal(readText(verifier, "config.cfg"), "new-tenure");
});

test("reload rebuilds byte and directory accounting from durable state", async () => {
    const root = await createRoot();
    const writerA = await mount(root);
    assert.equal(writerA.io.mkdir("profiles"), true);
    writeText(writerA, "profiles/config.cfg", "old");
    await writerA.filesystem.flushAndUnmount();

    const writerB = await mount(root);
    writeText(writerB, "profiles/config.cfg", "newer");
    assert.equal(writerB.io.mkdir("saves"), true);
    writeText(writerB, "saves/slot.dat", "four");
    await writerB.filesystem.flushAndUnmount();

    await remount(root, writerA);
    assert.deepEqual(writerA.io.list("profiles"), [{
        name: "config.cfg", type: "file", size: 5,
    }]);
    assert.deepEqual(writerA.io.list("saves"), [{
        name: "slot.dat", type: "file", size: 4,
    }]);
    assert.deepEqual(await writerA.filesystem.checkpoint(), {
        filesPersisted: 2,
        bytesPersisted: 9,
    });

    writeText(writerA, "profiles/config.cfg", "ok");
    assert.deepEqual(await writerA.filesystem.flushAndUnmount(), {
        filesPersisted: 2,
        bytesPersisted: 6,
    });
    const verifier = await mount(root);
    assert.equal(readText(verifier, "profiles/config.cfg"), "ok");
    assert.equal(readText(verifier, "saves/slot.dat"), "four");
});


test("native mount reports successful synchronous reads and releases its observer", async () => {
    for (const failRuntime of [false, true]) {
        const root = await createRoot();
        await writeDurableText(root, "startup.cfg", "abcdef");
        const harness = await mount(root);
        const progress = [];
        const operation = mountWorkerFilesystem(harness.filesystem,
            { importId: IMPORT_ID, files: [] }, () => {
                const descriptor = harness.io.open("startup.cfg");
                assert.equal(harness.io.read(descriptor, 512, 2), 2);
                assert.equal(harness.io.read(descriptor, 512, 8), 4);
                assert.equal(harness.io.read(descriptor, 512, 8), 0); // EOF is not progress.
                assert.equal(harness.io.read(-1, 512, 8), -1);
                harness.io.close(descriptor);
                if (failRuntime) throw new Error("native bootstrap failed");
            }, item => progress.push(item));
        if (failRuntime) await assert.rejects(operation, error => error.code === "FILESYSTEM_OWNERSHIP_UNKNOWN");
        else await operation;
        assert.deepEqual(progress.filter(item => item.phase === "runtime-loading").map(item => item.bytesProcessed),
            [0, 2, 6]);
        const completedCount = progress.length;
        if (failRuntime) await remount(root, harness);
        assert.equal(readText(harness, "startup.cfg"), "abcdef");
        assert.equal(progress.length, completedCount);
        await harness.filesystem.flushAndUnmount();
    }
});
