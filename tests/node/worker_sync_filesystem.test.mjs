import assert from "node:assert/strict";
import { after, test } from "node:test";

import { createWorkerSyncFilesystem } from "../../web/worker_sync_filesystem.mjs";

const IMPORT_ID = "filesystem-ordering-tests";
const originalNavigator = Object.getOwnPropertyDescriptor(globalThis, "navigator");

class MemoryFileHandle
{
    constructor(name)
    {
        this.kind = "file";
        this.name = name;
        this.bytes = new Uint8Array();
    }

    async getFile()
    {
        const snapshot = this.bytes.slice();
        return {
            size: snapshot.byteLength,
            async arrayBuffer() { return snapshot.buffer; },
        };
    }

    async createWritable()
    {
        let pending = this.bytes.slice();
        const handle = this;
        return {
            async write(bytes) { pending = new Uint8Array(bytes).slice(); },
            async truncate(size) { pending = pending.slice(0, size); },
            async close() { handle.bytes = pending; },
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
        if (!this.children.delete(name)) throw domError("NotFoundError");
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
