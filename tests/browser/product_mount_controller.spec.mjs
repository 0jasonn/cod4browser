import { expect, test } from "@playwright/test";

test.skip(process.env.KISAK_WEB_PRODUCT_TEST !== "1", "Runs only against the production site.");

async function installHarness(page)
{
    await page.goto("/");
    await page.evaluate(async () => {
        const { createLatestMountController } = await import("/product_mount_controller.mjs");
        const pending = new Map();
        const events = { calls: [], mounted: [], failed: [], cleared: 0 };
        const operation = (key) => new Promise((resolve, reject) => {
            pending.set(key, { resolve, reject });
        });
        const controller = createLatestMountController({
            mount(manifest) {
                events.calls.push(`mount:${manifest.importId}`);
                return operation(`mount:${manifest.importId}`);
            },
            unmount() {
                events.calls.push("unmount");
                return operation("unmount");
            },
            onMounted(manifest) { events.mounted.push(manifest.importId); },
            onFailed(error, manifest) {
                events.failed.push({ code: error.code, importId: manifest?.importId ?? null });
            },
            onCleared() { ++events.cleared; },
        });
        globalThis.mountRace = {
            controller,
            events,
            select(id) { return controller.select({ importId: id, files: [] }); },
            resolve(key, result = { runtime: true }) {
                const item = pending.get(key);
                pending.delete(key);
                item.resolve(result);
            },
            reject(key, code = "MOUNT_FAILED_CLEAN") {
                const item = pending.get(key);
                pending.delete(key);
                item.reject(Object.assign(new Error(`${key} failed`), { code }));
            },
        };
    });
}

test("delayed mount A drains the newer selection B @product", async ({ page }) => {
    await installHarness(page);
    await page.evaluate(() => {
        globalThis.mountRace.a = globalThis.mountRace.select("A");
        globalThis.mountRace.b = globalThis.mountRace.select("B");
        globalThis.mountRace.resolve("mount:A");
    });
    await expect.poll(() => page.evaluate(() => globalThis.mountRace.events.calls))
        .toEqual(["mount:A", "mount:B"]);
    await page.evaluate(() => globalThis.mountRace.resolve("mount:B"));
});

test("the newest ready installation becomes active @product", async ({ page }) => {
    await installHarness(page);
    await page.evaluate(async () => {
        const a = globalThis.mountRace.select("A");
        const b = globalThis.mountRace.select("B");
        globalThis.mountRace.resolve("mount:A");
        await a;
        globalThis.mountRace.resolve("mount:B");
        await b;
    });
    expect(await page.evaluate(() => globalThis.mountRace.controller.activeImportId)).toBe("B");
    expect(await page.evaluate(() => globalThis.mountRace.events.mounted)).toEqual(["B"]);
});

test("late A completion cannot publish over B @product", async ({ page }) => {
    await installHarness(page);
    await page.evaluate(() => {
        globalThis.mountRace.a = globalThis.mountRace.select("A");
        globalThis.mountRace.b = globalThis.mountRace.select("B");
        globalThis.mountRace.resolve("mount:A");
    });
    await expect.poll(() => page.evaluate(() => globalThis.mountRace.events.calls.length)).toBe(2);
    expect(await page.evaluate(() => globalThis.mountRace.events.mounted)).toEqual([]);
    await page.evaluate(async () => {
        globalThis.mountRace.resolve("mount:B");
        await globalThis.mountRace.b;
    });
    expect(await page.evaluate(() => globalThis.mountRace.events.mounted)).toEqual(["B"]);
});

test("clear during a mount deterministically unmounts its late completion @product", async ({ page }) => {
    await installHarness(page);
    await page.evaluate(() => {
        globalThis.mountRace.mount = globalThis.mountRace.select("A");
        globalThis.mountRace.clear = globalThis.mountRace.controller.clear();
        globalThis.mountRace.resolve("mount:A");
    });
    await expect.poll(() => page.evaluate(() => globalThis.mountRace.events.calls))
        .toEqual(["mount:A", "unmount"]);
    await page.evaluate(async () => {
        globalThis.mountRace.resolve("unmount", { mounted: false });
        await globalThis.mountRace.clear;
    });
    expect(await page.evaluate(() => ({
        active: globalThis.mountRace.controller.activeImportId,
        mounted: globalThis.mountRace.events.mounted,
        cleared: globalThis.mountRace.events.cleared,
    }))).toEqual({ active: null, mounted: [], cleared: 1 });
});

test("failed A does not prevent a ready B from mounting @product", async ({ page }) => {
    await installHarness(page);
    await page.evaluate(() => {
        globalThis.mountRace.a = globalThis.mountRace.select("A");
        globalThis.mountRace.b = globalThis.mountRace.select("B");
        globalThis.mountRace.reject("mount:A");
    });
    await expect.poll(() => page.evaluate(() => globalThis.mountRace.events.calls.length)).toBe(2);
    await page.evaluate(async () => {
        globalThis.mountRace.resolve("mount:B");
        await globalThis.mountRace.b;
    });
    expect(await page.evaluate(() => ({
        active: globalThis.mountRace.controller.activeImportId,
        mounted: globalThis.mountRace.events.mounted,
        failed: globalThis.mountRace.events.failed,
    }))).toEqual({ active: "B", mounted: ["B"], failed: [] });
});

test("disposal cancels a queued remount @product", async ({ page }) => {
    await installHarness(page);
    await page.evaluate(() => {
        globalThis.mountRace.a = globalThis.mountRace.select("A");
        globalThis.mountRace.b = globalThis.mountRace.select("B");
        globalThis.mountRace.disposal = globalThis.mountRace.controller.dispose();
        globalThis.mountRace.resolve("mount:A");
    });
    await page.evaluate(() => globalThis.mountRace.disposal);
    expect(await page.evaluate(() => ({
        calls: globalThis.mountRace.events.calls,
        mounted: globalThis.mountRace.events.mounted,
        active: globalThis.mountRace.controller.activeImportId,
    }))).toEqual({ calls: ["mount:A"], mounted: [], active: null });
});

test("sequential awaited selections cannot strand the second mount @product", async ({ page }) => {
    await installHarness(page);
    const beforeSecondCompletion = await page.evaluate(async () => {
        globalThis.mountRace.sequence = (async () => {
            await globalThis.mountRace.select("A");
            return globalThis.mountRace.select("B");
        })();
        globalThis.mountRace.resolve("mount:A");
        for (let turn = 0; turn < 10; ++turn) await Promise.resolve();
        return {
            calls: globalThis.mountRace.events.calls,
            busy: globalThis.mountRace.controller.busy,
        };
    });
    expect(beforeSecondCompletion).toEqual({ calls: ["mount:A", "mount:B"], busy: true });
    await page.evaluate(async () => {
        globalThis.mountRace.resolve("mount:B");
        await globalThis.mountRace.sequence;
    });
    expect(await page.evaluate(() => ({
        active: globalThis.mountRace.controller.activeImportId,
        busy: globalThis.mountRace.controller.busy,
    }))).toEqual({ active: "B", busy: false });
});

test("an awaited clear after select cannot be stranded @product", async ({ page }) => {
    await installHarness(page);
    const beforeClearCompletion = await page.evaluate(async () => {
        globalThis.mountRace.sequence = (async () => {
            await globalThis.mountRace.select("A");
            return globalThis.mountRace.controller.clear();
        })();
        globalThis.mountRace.resolve("mount:A");
        for (let turn = 0; turn < 10; ++turn) await Promise.resolve();
        return {
            calls: globalThis.mountRace.events.calls,
            busy: globalThis.mountRace.controller.busy,
        };
    });
    expect(beforeClearCompletion).toEqual({ calls: ["mount:A", "unmount"], busy: true });
    await page.evaluate(async () => {
        globalThis.mountRace.resolve("unmount", { mounted: false });
        await globalThis.mountRace.sequence;
    });
    expect(await page.evaluate(() => ({
        active: globalThis.mountRace.controller.activeImportId,
        cleared: globalThis.mountRace.events.cleared,
        busy: globalThis.mountRace.controller.busy,
    }))).toEqual({ active: null, cleared: 1, busy: false });
});

test("a selection submitted directly from a settled Promise continues draining @product", async ({ page }) => {
    await installHarness(page);
    const state = await page.evaluate(async () => {
        globalThis.mountRace.continuation = globalThis.mountRace.select("A")
            .then(() => globalThis.mountRace.select("B"));
        globalThis.mountRace.resolve("mount:A");
        for (let turn = 0; turn < 10; ++turn) await Promise.resolve();
        return {
            calls: globalThis.mountRace.events.calls,
            busy: globalThis.mountRace.controller.busy,
        };
    });
    expect(state).toEqual({ calls: ["mount:A", "mount:B"], busy: true });
    await page.evaluate(async () => {
        globalThis.mountRace.resolve("mount:B");
        await globalThis.mountRace.continuation;
    });
});

test("disposal from a request continuation leaves no pending work @product", async ({ page }) => {
    await installHarness(page);
    const state = await page.evaluate(async () => {
        const disposing = globalThis.mountRace.select("A")
            .then(() => globalThis.mountRace.controller.dispose());
        globalThis.mountRace.resolve("mount:A");
        await disposing;
        let disposedCode = null;
        try {
            await globalThis.mountRace.select("B");
        } catch (error) {
            disposedCode = error.code;
        }
        return {
            calls: globalThis.mountRace.events.calls,
            busy: globalThis.mountRace.controller.busy,
            disposedCode,
        };
    });
    expect(state).toEqual({
        calls: ["mount:A"],
        busy: false,
        disposedCode: "MOUNT_CONTROLLER_DISPOSED",
    });
});
