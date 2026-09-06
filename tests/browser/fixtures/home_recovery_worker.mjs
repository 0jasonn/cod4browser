// Synthetic OPFS fault harness served only by the Playwright route. It is not
// packaged in either product. Every test uses its own persistent profile.
import { createWorkerSyncFilesystem, restoreHomeFiles } from "/worker_sync_filesystem.mjs";
import { createFilesystemLeases } from "/worker_transport.mjs";

async function restoreProbe(root, home, data)
{
    if (data.operation !== "recover") {
        const old = await (await home.getFileHandle("save.svg", { create: true })).createWritable();
        await old.write("old complete save");
        await old.close();
        const original = FileSystemFileHandle.prototype.createWritable;
        FileSystemFileHandle.prototype.createWritable = async function (...args) {
            const stream = await original.apply(this, args);
            const name = this.name;
            return {
                async write(bytes) {
                    if (name === "restored.svg" && data.operation !== "restore-journal") {
                        await stream.write(bytes.subarray(0, 1));
                        if (data.operation === "restore-fail-write") throw new DOMException("Synthetic quota", "QuotaExceededError");
                        postMessage({ phase: "staged-partial-write" });
                        await new Promise(() => {});
                    }
                    await stream.write(bytes);
                },
                async close() {
                    await stream.close();
                    if (name === "home-restore.json" && data.operation === "restore-journal") {
                        postMessage({ phase: "journal-published" });
                        await new Promise(() => {});
                    }
                },
                truncate: stream.truncate.bind(stream), abort: stream.abort.bind(stream),
            };
        };
        const file = new Blob(["new complete save"]);
        const sha256 = [...new Uint8Array(await crypto.subtle.digest("SHA-256", await file.arrayBuffer()))]
            .map(byte => byte.toString(16).padStart(2, "0")).join("");
        try { await restoreHomeFiles(root, [{ path: "restored.svg", size: file.size, sha256, file }]); }
        catch (error) {
            if (data.operation !== "restore-fail-write") throw error;
            postMessage({ failure: error.name, durable: await (await (await home.getFileHandle("save.svg")).getFile()).text() });
        }
        return;
    }
    const filesystem = createWorkerSyncFilesystem();
    filesystem.installForModule({ HEAPU8: new Uint8Array(4096) });
    await filesystem.mount({ importId: "storage-recovery-test", files: [] });
    if (await (await (await home.getFileHandle("save.svg")).getFile()).text() !== "old complete save")
        throw new Error("Restore changed an existing save");
    postMessage({ recovered: await (await (await home.getFileHandle("restored.svg")).getFile()).text(), temporary: null });
    await filesystem.flushAndUnmount();
}

async function directory(root, path)
{
    for (const name of path.split("/")) root = await root.getDirectoryHandle(name, { create: true });
    return root;
}

self.onmessage = async ({ data }) => {
    const leases = createFilesystemLeases(navigator.locks, () => {});
    try {
        await leases.acquire();
        const root = await navigator.storage.getDirectory();
        await directory(root, "kisakcod-web/imports/storage-recovery-test");
        const home = await directory(root, "kisakcod-web/home");
        if (data.operation.startsWith("restore-") || data.restore) {
            await restoreProbe(root, home, data);
            return;
        }
        if (data.operation !== "recover") {
            for (const [name, text] of [["save.svg", "old complete save"], ["temp.svg", "new complete save"]]) {
                const handle = await home.getFileHandle(name, { create: true });
                const writable = await handle.createWritable();
                await writable.write(new TextEncoder().encode(text));
                await writable.close();
            }
        }
        if (data.operation === "interrupt-write" || data.operation === "fail-write") {
            const original = FileSystemFileHandle.prototype.createWritable;
            FileSystemFileHandle.prototype.createWritable = async function (...args) {
                const stream = await original.apply(this, args);
                if (this.name !== "save.svg") return stream;
                return {
                    async write(bytes) {
                        await stream.write(bytes.subarray(0, 1));
                        if (data.operation === "fail-write") throw new DOMException("Synthetic partial write", "QuotaExceededError");
                        postMessage({ phase: "staged-partial-write" });
                        await new Promise(() => {});
                    },
                    truncate: stream.truncate.bind(stream),
                    close: stream.close.bind(stream),
                    abort: stream.abort.bind(stream),
                };
            };
        }
        const filesystem = createWorkerSyncFilesystem({
            async beforePersist(path) {
                if (path === "save.svg" && data.operation === "interrupt-journal") {
                    postMessage({ phase: "journal-published" });
                    await new Promise(() => {});
                }
            },
        });
        const module = { HEAPU8: new Uint8Array(4096) };
        filesystem.installForModule(module);
        await filesystem.mount({ importId: "storage-recovery-test", files: [] });
        const io = globalThis.__KISAKCOD_SYNC_FS__;
        if (data.operation !== "recover") {
            if (!io.rename("temp.svg", "save.svg")) throw new Error("Synthetic rename rejected");
            try {
                await (data.shutdown ? filesystem.flushAndUnmount() : filesystem.checkpoint());
            } catch (error) {
                if (data.operation !== "fail-write") throw error;
                const file = await (await home.getFileHandle("save.svg")).getFile();
                postMessage({ failure: error.name, durable: await file.text() });
                return;
            }
        }
        const descriptor = io.open("save.svg");
        const size = io.size(descriptor);
        if (descriptor < 0 || io.read(descriptor, 1, size) !== size) throw new Error("Recovered save missing");
        io.close(descriptor);
        postMessage({ recovered: new TextDecoder().decode(module.HEAPU8.subarray(1, 1 + size)),
            temporary: io.stat("temp.svg") });
        await filesystem.flushAndUnmount();
    } catch (error) { postMessage({ error: `${error.name}: ${error.message}` }); }
    finally { await leases.release(); }
};
