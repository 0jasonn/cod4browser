import { expect, test } from "@playwright/test";

test.skip(process.env.KISAK_WEB_PRODUCT_TEST !== "1", "Runs only against the production site.");

async function runOpfsProbe(page, mode)
{
    return page.evaluate(async (probeMode) => {
        const { probeOpfsSyncAccess } = await import("/capability_probe_worker.mjs");
        const state = { removed: 0 };
        const access = {
            getSize() {
                if (probeMode === "operation-fails") throw new Error("size failed");
                return 0;
            },
            truncate() {},
            write(bytes) { return bytes.byteLength; },
            flush() {},
            read(bytes) { bytes[0] = 0x4B; return 1; },
            close() {
                if (probeMode === "close-fails") throw new Error("close failed");
            },
        };
        const file = {
            async createSyncAccessHandle() {
                if (probeMode === "open-fails") throw new Error("open failed");
                return access;
            },
        };
        const root = {
            async getFileHandle() { return file; },
            async removeEntry() { ++state.removed; },
        };
        const result = await probeOpfsSyncAccess({
            storage: { async getDirectory() { return root; } },
            createProbeName: () => ".kisak-capability-probe-test",
        });
        return { result, removed: state.removed };
    }, mode);
}

test("real Worker probe performs OPFS sync access and removes its entry @product", async ({ page }) => {
    await page.goto("/");
    const evidence = await page.evaluate(async () => {
        const { probeWorkerCapabilities } = await import("/browser_capabilities.mjs");
        const root = await navigator.storage.getDirectory();
        const probeEntries = async () => {
            const names = [];
            for await (const [name] of root.entries()) {
                if (name.startsWith(".kisak-capability-probe-")) names.push(name);
            }
            return names.sort();
        };
        const before = await probeEntries();
        const result = await probeWorkerCapabilities(document.createElement("canvas"));
        const after = await probeEntries();
        return { before, result, after };
    });
    expect(evidence.result).toMatchObject({
        offscreenCanvas: true,
        syncAccessHandle: true,
        failure: null,
    });
    expect(evidence.after).toEqual(evidence.before);
});

test("API shape cannot hide a rejected sync-access open @product", async ({ page }) => {
    await page.goto("/");
    const { result, removed } = await runOpfsProbe(page, "open-fails");
    expect(result).toMatchObject({
        supported: false,
        capability: "opfsSyncAccessHandle",
        code: "SYNC_ACCESS_HANDLE_OPEN_FAILED",
    });
    expect(removed).toBe(1);
});

test("an opened handle must complete a real operation @product", async ({ page }) => {
    await page.goto("/");
    const { result } = await runOpfsProbe(page, "operation-fails");
    expect(result).toMatchObject({
        supported: false,
        capability: "opfsSyncAccessHandle",
        code: "SYNC_ACCESS_HANDLE_OPERATION_FAILED",
    });
});

test("sync-access close failure is a capability failure @product", async ({ page }) => {
    await page.goto("/");
    const { result, removed } = await runOpfsProbe(page, "close-fails");
    expect(result).toMatchObject({
        supported: false,
        capability: "opfsSyncAccessHandle",
        code: "SYNC_ACCESS_HANDLE_CLOSE_FAILED",
    });
    expect(removed).toBe(1);
});

test("temporary OPFS entry cleanup still runs after operation failure @product", async ({ page }) => {
    await page.goto("/");
    const { removed } = await runOpfsProbe(page, "operation-fails");
    expect(removed).toBe(1);
});

test("synchronous Worker construction errors are structured @product", async ({ page }) => {
    await page.goto("/");
    const result = await page.evaluate(async () => {
        const { probeWorkerCapabilities } = await import("/browser_capabilities.mjs");
        class ThrowingWorker {
            constructor() { throw new DOMException("blocked", "SecurityError"); }
        }
        return probeWorkerCapabilities(document.createElement("canvas"), {
            WorkerConstructor: ThrowingWorker,
        });
    });
    expect(result.failure).toMatchObject({
        supported: false,
        capability: "worker",
        code: "WORKER_CONSTRUCTION_FAILED",
    });
});

test("malformed temporary Worker replies are rejected @product", async ({ page }) => {
    await page.goto("/");
    const result = await page.evaluate(async () => {
        const { probeWorkerCapabilities } = await import("/browser_capabilities.mjs");
        class MalformedWorker {
            postMessage() { queueMicrotask(() => this.onmessage?.({ data: {} })); }
            terminate() {}
        }
        return probeWorkerCapabilities(document.createElement("canvas"), {
            WorkerConstructor: MalformedWorker,
        });
    });
    expect(result.failure).toMatchObject({
        supported: false,
        capability: "worker",
        code: "MALFORMED_PROBE_REPLY",
    });
});

test("temporary Worker probe timeout is structured without real-time delay @product", async ({ page }) => {
    await page.goto("/");
    const result = await page.evaluate(async () => {
        const { probeWorkerCapabilities } = await import("/browser_capabilities.mjs");
        class SilentWorker { postMessage() {} terminate() {} }
        return probeWorkerCapabilities(document.createElement("canvas"), {
            WorkerConstructor: SilentWorker,
            setTimeoutFn(callback) { queueMicrotask(callback); return 1; },
            clearTimeoutFn() {},
        });
    });
    expect(result.failure).toMatchObject({
        supported: false,
        capability: "worker",
        code: "CAPABILITY_PROBE_TIMEOUT",
    });
});

test("OffscreenCanvas transfer failure is structured before Worker creation @product", async ({ page }) => {
    await page.goto("/");
    const result = await page.evaluate(async () => {
        const { probeWorkerCapabilities } = await import("/browser_capabilities.mjs");
        let constructions = 0;
        class CountingWorker { constructor() { ++constructions; } }
        const probe = await probeWorkerCapabilities({
            transferControlToOffscreen() {
                throw new DOMException("transfer failed", "NotSupportedError");
            },
        }, { WorkerConstructor: CountingWorker });
        return { probe, constructions };
    });
    expect(result.constructions).toBe(0);
    expect(result.probe.failure).toMatchObject({
        supported: false,
        capability: "offscreenCanvas",
        code: "OFFSCREEN_CANVAS_TRANSFER_FAILED",
    });
});

test("capability failure stops product startup before asset import @product", async ({ page }) => {
    const pageErrors = [];
    page.on("pageerror", (error) => pageErrors.push(error.message));
    await page.addInitScript(() => {
        const NativeWorker = globalThis.Worker;
        globalThis.__engineWorkerConstructions = 0;
        globalThis.__assetStorageAccesses = 0;
        globalThis.Worker = class extends NativeWorker {
            constructor(url, options) {
                if (String(url).includes("capability_probe_worker")) {
                    throw new DOMException("workers blocked", "SecurityError");
                }
                ++globalThis.__engineWorkerConstructions;
                super(url, options);
            }
        };
        const getDirectory = navigator.storage.getDirectory.bind(navigator.storage);
        navigator.storage.getDirectory = (...arguments_) => {
            ++globalThis.__assetStorageAccesses;
            return getDirectory(...arguments_);
        };
    });
    await page.goto("/");
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "unsupported");
    await expect(page.locator("#select-install-button")).toBeDisabled();
    expect(await page.evaluate(() => globalThis.__engineWorkerConstructions)).toBe(0);
    expect(await page.evaluate(() => globalThis.__assetStorageAccesses)).toBe(0);
    expect(pageErrors).toEqual([]);
});
