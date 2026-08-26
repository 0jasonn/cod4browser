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
