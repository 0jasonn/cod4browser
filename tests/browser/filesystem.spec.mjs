import { expect, test } from "@playwright/test";
import {
    createInstallDirectory as createM12InstallDirectory,
    createSyntheticWorldInventoryFastfile,
} from "./install_fixture.mjs";
import {
    createSyntheticIwd,
    ZIP_METHOD_DEFLATE,
    ZIP_METHOD_STORE,
} from "./synthetic_iwd.mjs";

const ARCHIVE_PATH = "main/iw_00.iwd";
const MAP_FASTFILE_PATH = "zone/english/killhouse.ff";
const MAP_FASTFILE_SIZE = createSyntheticWorldInventoryFastfile().length;
const STARTUP_FASTFILE_PATH = "zone/english/common.ff";
function filesystemFixture()
{
    return createSyntheticIwd([
        {
            path: "synthetic/stored.txt",
            contents: "Stored filesystem seam fixture.\n",
            method: ZIP_METHOD_STORE,
        },
        {
            path: "synthetic/deflated.txt",
            contents: "Deflated filesystem seam fixture.\n",
            method: ZIP_METHOD_DEFLATE,
        },
    ]);
}

async function createInstallDirectory(testInfo, name)
{
    const archive = filesystemFixture();
    const directory = await createM12InstallDirectory(
        testInfo,
        name,
        { primaryIwd: archive },
    );
    return { directory, archive };
}

async function usePortableFolderPicker(page)
{
    await page.addInitScript(() => {
        Object.defineProperty(globalThis, "showDirectoryPicker", {
            configurable: true,
            value: undefined,
        });
    });
}

async function waitForRuntimeState(page, property, state)
{
    await expect.poll(() => page.evaluate(
        ({ key }) => globalThis.__KISAKCOD_WEB__?.[key]?.state,
        { key: property },
    )).toBe(state);
}

async function importFixture(page, testInfo, name)
{
    await usePortableFolderPicker(page);
    const fixture = await createInstallDirectory(testInfo, name);
    await page.goto("/");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state,
    )).toBe("running");
    await waitForRuntimeState(page, "assets", "empty");

    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#select-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(fixture.directory);
    await waitForRuntimeState(page, "assets", "ready");
    await waitForRuntimeState(page, "archive", "ready");
    return fixture;
}

test("immutable asset sources provide bounded reads while the frame pump advances", { tag: "@smoke" }, async ({ page }, testInfo) => {
    const fixture = await importFixture(page, testInfo, "filesystem-source");

    const result = await page.evaluate(async ({ archivePath, mapPath }) => {
        const runtime = globalThis.__KISAKCOD_WEB__;
        const store = runtime.assetStore;
        const source = await store.openSource(archivePath);
        const mutationAccepted = Reflect.set(source, "size", source.size + 1);
        const signature = Array.from(await store.readSource(source, { offset: 0, length: 4 }));
        const mapSource = await store.openSource(mapPath);
        const mapHeader = Array.from(await store.readSource(
            mapSource,
            { offset: 0, length: 14 },
        ));

        let invalidRange = null;
        try {
            await store.readSource(source, { offset: 0, length: 1024 * 1024 + 1 });
        } catch (error) {
            invalidRange = error.code;
        }

        const arrayBufferDescriptor = Object.getOwnPropertyDescriptor(
            Blob.prototype,
            "arrayBuffer",
        );
        let delayedReads = 0;
        Object.defineProperty(Blob.prototype, "arrayBuffer", {
            ...arrayBufferDescriptor,
            async value() {
                delayedReads += 1;
                await new Promise((resolve) => globalThis.setTimeout(resolve, 800));
                return arrayBufferDescriptor.value.call(this);
            },
        });

        const beforeFrame = runtime.system?.framePumpTicks ?? 0;
        let delayedSignature;
        try {
            delayedSignature = Array.from(
                await store.readSource(source, { offset: 0, length: 4 }),
            );
        } finally {
            Object.defineProperty(Blob.prototype, "arrayBuffer", arrayBufferDescriptor);
        }
        const afterFrame = runtime.system?.framePumpTicks ?? 0;
        return {
            source: {
                path: source.path,
                size: source.size,
                frozen: Object.isFrozen(source),
                mutationAccepted,
            },
            signature,
            mapSource: {
                path: mapSource.path,
                size: mapSource.size,
                frozen: Object.isFrozen(mapSource),
            },
            mapHeader,
            delayedSignature,
            invalidRange,
            delayedReads,
            beforeFrame,
            afterFrame,
        };
    }, { archivePath: ARCHIVE_PATH, mapPath: MAP_FASTFILE_PATH });

    expect(result.source).toMatchObject({
        path: ARCHIVE_PATH,
        size: fixture.archive.length,
        frozen: true,
        mutationAccepted: false,
    });
    expect(result.signature).toEqual([0x50, 0x4b, 0x03, 0x04]);
    expect(result.mapSource).toEqual({
        path: MAP_FASTFILE_PATH,
        size: MAP_FASTFILE_SIZE,
        frozen: true,
    });
    expect(result.mapHeader).toEqual([
        0x49, 0x57, 0x66, 0x66, 0x75, 0x31, 0x30, 0x30,
        0x05, 0x00, 0x00, 0x00, 0x78, 0xda,
    ]);
    expect(result.delayedSignature).toEqual(result.signature);
    expect(result.invalidRange).toBe("INVALID_RANGE");
    expect(result.delayedReads).toBe(1);
    expect(result.afterFrame).toBeGreaterThan(result.beforeFrame);
});

test("bridge cancellation prevents a delayed read from touching Wasm memory or completing", { tag: "@smoke" }, async ({ page }, testInfo) => {
    await importFixture(page, testInfo, "filesystem-cancellation");

    const result = await page.evaluate(async ({ fastfilePath, fastfileSize }) => {
        const runtime = globalThis.__KISAKCOD_WEB__;
        const module = runtime.module;
        const bridge = runtime.filesystemBridge;
        const statRequestId = 0xf000_0010;
        const requestId = 0xf000_0011;
        const destination = module._malloc(4);
        if (!destination) {
            throw new Error("Synthetic cancellation test could not allocate Wasm memory.");
        }

        const arrayBufferDescriptor = Object.getOwnPropertyDescriptor(
            Blob.prototype,
            "arrayBuffer",
        );
        const originalStatCompletion = module._KisakWeb_CompleteFsStat;
        const originalCompletion = module._KisakWeb_CompleteFsRead;
        const completions = [];
        let releaseRead;
        let reportReadStarted;
        const readStarted = new Promise((resolve) => { reportReadStarted = resolve; });
        const readGate = new Promise((resolve) => { releaseRead = resolve; });

        const completionWrapped = Reflect.set(
            module,
            "_KisakWeb_CompleteFsRead",
            (...arguments_) => {
                completions.push(Array.from(arguments_));
                return originalCompletion(...arguments_);
            },
        );
        let reportStat;
        const statCompleted = new Promise((resolve) => { reportStat = resolve; });
        Reflect.set(module, "_KisakWeb_CompleteFsStat", (...arguments_) => {
            reportStat(Array.from(arguments_));
            return originalStatCompletion(...arguments_);
        });
        Object.defineProperty(Blob.prototype, "arrayBuffer", {
            ...arrayBufferDescriptor,
            async value() {
                reportReadStarted();
                await readGate;
                return arrayBufferDescriptor.value.call(this);
            },
        });

        module.HEAPU8.fill(0xa5, destination, destination + 4);
        let accepted = false;
        let cancelled = false;
        try {
            if (!bridge.stat(statRequestId, fastfilePath)) {
                throw new Error("Synthetic fastfile stat was not accepted.");
            }
            const statCompletion = await statCompleted;
            if (statCompletion[1] !== 0 || statCompletion[2] !== fastfileSize) {
                throw new Error("Synthetic fastfile stat did not complete successfully.");
            }
            accepted = bridge.read(requestId, fastfilePath, 0, 4, destination, 4);
            await Promise.race([
                readStarted,
                new Promise((_, reject) => globalThis.setTimeout(
                    () => reject(new Error("The delayed OPFS read did not begin.")),
                    2000,
                )),
            ]);
            cancelled = bridge.cancel(requestId);
            releaseRead();
            await new Promise((resolve) => globalThis.setTimeout(resolve, 50));
            return {
                accepted,
                cancelled,
                completionWrapped,
                completions,
                destinationBytes: Array.from(module.HEAPU8.slice(destination, destination + 4)),
            };
        } finally {
            releaseRead();
            Object.defineProperty(Blob.prototype, "arrayBuffer", arrayBufferDescriptor);
            Reflect.set(module, "_KisakWeb_CompleteFsStat", originalStatCompletion);
            Reflect.set(module, "_KisakWeb_CompleteFsRead", originalCompletion);
            module._free(destination);
        }
    }, { fastfilePath: MAP_FASTFILE_PATH, fastfileSize: MAP_FASTFILE_SIZE });

    expect(result).toMatchObject({
        accepted: true,
        cancelled: true,
        completionWrapped: true,
        completions: [],
        destinationBytes: [0xa5, 0xa5, 0xa5, 0xa5],
    });
});

test("an immutable source reports STALE_SOURCE after its import is cleared", async ({ page }, testInfo) => {
    await importFixture(page, testInfo, "filesystem-stale-source");

    const result = await page.evaluate(async ({ fastfilePath }) => {
        const store = globalThis.__KISAKCOD_WEB__.assetStore;
        const source = await store.openSource(fastfilePath);
        const beforeClear = Array.from(
            await store.readSource(source, { offset: 0, length: 4 }),
        );
        await store.clear();

        let staleCode = null;
        try {
            await store.readSource(source, { offset: 0, length: 4 });
        } catch (error) {
            staleCode = error.code;
        }
        return { beforeClear, staleCode };
    }, { fastfilePath: STARTUP_FASTFILE_PATH });

    expect(result.beforeClear).toEqual([0x49, 0x57, 0x66, 0x66]);
    expect(result.staleCode).toBe("STALE_SOURCE");
    await waitForRuntimeState(page, "assets", "empty");
});
