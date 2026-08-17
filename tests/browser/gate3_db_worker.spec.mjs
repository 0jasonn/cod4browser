import { expect, test } from "@playwright/test";
import { deflateSync } from "node:zlib";
import {
    createInstallDirectory,
    createSyntheticCanonicalXFile,
    createSyntheticCanonicalRefillXFile,
} from "./install_fixture.mjs";

async function importWithCodePost(page, testInfo, name, contents)
{
    const directory = await createInstallDirectory(testInfo, name, {
        overrides: new Map([["zone/english/code_post_gfx.ff", contents]]),
    });
    await page.addInitScript(() => {
        Object.defineProperty(globalThis, "showDirectoryPicker", {
            configurable: true,
            value: undefined,
        });
    });
    await page.goto("/");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.assets?.state,
    )).toBe("empty");
    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#select-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(directory);
}

test("Worker-hosted canonical DB streams an XFile into PMem and stops at generated loading", { tag: "@smoke" }, async ({ page }, testInfo) => {
    const directory = await createInstallDirectory(testInfo, "gate3-db-worker");
    await page.addInitScript(() => {
        Object.defineProperty(globalThis, "showDirectoryPicker", {
            configurable: true,
            value: undefined,
        });
        globalThis.__databaseEvents = [];
        globalThis.addEventListener("kisakcod:database", (event) => {
            globalThis.__databaseEvents.push(structuredClone(event.detail));
        });
    });
    await page.goto("/");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.assets?.state,
    )).toBe("empty");

    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#select-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(directory);

    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    ), { timeout: 30_000 }).toBe("Load_XAssetListCustom/generated-loader-closure");

    const result = await page.evaluate(() => ({
        current: structuredClone(globalThis.__KISAKCOD_WEB__.database),
        events: structuredClone(globalThis.__databaseEvents),
    }));
    expect(result.current).toMatchObject({
        stage: "DB stop",
        logicalPath: "zone/english/code_post_gfx.ff",
        compressedBytesConsumed: expect.any(Number),
        decompressedBytesProduced: 44,
        inputRefillCount: 1,
        readOffset: 0,
        requestedBytes: 12,
        openSucceeded: true,
        initializedPoolCount: 26,
        freeAssetEntryCount: 32752,
        threadInitialized: true,
        headerValid: true,
        inflateInitialized: true,
        xfileSize: 1_378_265,
        xfileExternalSize: 950_499,
        blockSizes: [498_816, 0, 0, 0, 407_412, 0, 0, 4_224, 480],
        blockAllocationCount: 4,
        blockAllocationBytes: 910_932,
        streamBlock: 0,
        streamOffset: 0,
        streamInitialized: true,
        cleanupComplete: true,
        stopStage: "Load_XAssetListCustom/generated-loader-closure",
    });
    expect(result.current.fileSize).toBeGreaterThan(12);
    expect(result.current.bytesRead).toBe(result.current.fileSize);
    expect(result.current.compressedBytesConsumed).toBeGreaterThan(0);
    expect(result.events.map((event) => event.stage)).toEqual(expect.arrayContaining([
        "DB_InitThread",
        "DB_Thread initialized",
        "DB_LoadXAssets",
        "DB_Init",
        "asset-pool initialization",
        "DB_LoadXZone",
        "DB_TryLoadXFile",
        "DB_TryLoadXFileInternal",
        "DB_BuildOSPath",
        "resolved logical path",
        "FS/platform open",
        "FS/platform open success",
        "DB_LoadXFile",
        "compressed input refill",
        "zone header read",
        "zone header/framing validation",
        "inflate init",
        "inflate progress",
        "XFile block sizes",
        "PMem block allocation",
        "stream block initialization",
        "first generated-loader entry",
        "XFile cleanup",
        "DB stop",
    ]));
    expect(result.events.some((event) =>
        /(?:[a-z]:\\|users[/\\]|opfs|indexeddb|[0-9a-f]{8}-[0-9a-f]{4}-4)/iu
            .test(event.logicalPath))).toBe(false);
});

test("canonical DB reports a premature zlib stream end and cleans up", async ({ page }, testInfo) => {
    const compressed = deflateSync(Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8]));
    const fastfile = Uint8Array.from([
        ...Buffer.from("IWffu100", "ascii"), 5, 0, 0, 0, ...compressed,
    ]);
    await importWithCodePost(page, testInfo, "gate3-db-truncated", fastfile);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    )).toBe("inflate/premature end of stream");
    const trace = await page.evaluate(() => structuredClone(
        globalThis.__KISAKCOD_WEB__.database));
    expect(trace).toMatchObject({
        inflateInitialized: true,
        streamInitialized: false,
        cleanupComplete: true,
        stopStage: "inflate/premature end of stream",
    });
    expect(trace.decompressedBytesProduced).toBe(8);
});

test("canonical DB reports truncated compressed input and cleans up", async ({ page }, testInfo) => {
    const complete = createSyntheticCanonicalXFile();
    // Keep a syntactically plausible zlib prefix but remove the compressed
    // bytes that would produce the complete 44-byte XFile envelope.
    const fastfile = complete.slice(0, 20);
    await importWithCodePost(page, testInfo, "gate3-db-truncated", fastfile);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    )).toBe("inflate/premature EOF");
    const trace = await page.evaluate(() => structuredClone(
        globalThis.__KISAKCOD_WEB__.database));
    expect(trace).toMatchObject({
        bytesRead: fastfile.byteLength,
        inflateInitialized: true,
        streamInitialized: false,
        cleanupComplete: true,
        stopStage: "inflate/premature EOF",
    });
    expect(trace.decompressedBytesProduced).toBeLessThan(44);
});

test("canonical DB preserves the 256 KiB alternating input refill contract", async ({ page }, testInfo) => {
    const fastfile = createSyntheticCanonicalRefillXFile();
    await importWithCodePost(page, testInfo, "gate3-db-refill", fastfile);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    )).toBe("Load_XAssetListCustom/generated-loader-closure");
    const trace = await page.evaluate(() => structuredClone(
        globalThis.__KISAKCOD_WEB__.database));
    expect(trace).toMatchObject({
        bytesRead: fastfile.byteLength,
        inputRefillCount: 2,
        decompressedBytesProduced: 44,
        blockSizes: [1024, 0, 0, 0, 1024, 0, 0, 0, 0],
        blockAllocationCount: 2,
        streamInitialized: true,
        cleanupComplete: true,
    });
    expect(trace.compressedBytesConsumed).toBeGreaterThan(0x40000 - 12);
});

test("canonical DB rejects corrupt zlib data and cleans up", async ({ page }, testInfo) => {
    const fastfile = Uint8Array.from([
        ...Buffer.from("IWffu100", "ascii"), 5, 0, 0, 0,
        0x78, 0xda, 0xff, 0xff, 0xff, 0xff,
    ]);
    await importWithCodePost(page, testInfo, "gate3-db-corrupt", fastfile);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    )).toBe("inflate/corrupt zlib data");
    await expect(page.evaluate(() => ({
        cleanupComplete: globalThis.__KISAKCOD_WEB__.database.cleanupComplete,
        streamInitialized: globalThis.__KISAKCOD_WEB__.database.streamInitialized,
    }))).resolves.toEqual({ cleanupComplete: true, streamInitialized: false });
});

test("canonical DB rejects XFile block allocation exhaustion atomically", async ({ page }, testInfo) => {
    const fastfile = createSyntheticCanonicalXFile({
        blockSizes: [0x0800_0001, 0, 0, 0, 0, 0, 0, 0, 0],
    });
    await importWithCodePost(page, testInfo, "gate3-db-oversized", fastfile);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    )).toBe("XFile/block allocation exhaustion");
    const trace = await page.evaluate(() => structuredClone(
        globalThis.__KISAKCOD_WEB__.database));
    expect(trace).toMatchObject({
        blockSizes: [0x0800_0001, 0, 0, 0, 0, 0, 0, 0, 0],
        blockAllocationCount: 0,
        blockAllocationBytes: 0,
        streamInitialized: false,
        cleanupComplete: true,
    });
});

test("canonical DB rejects XFile block arithmetic overflow atomically", async ({ page }, testInfo) => {
    const fastfile = createSyntheticCanonicalXFile({
        blockSizes: [0xffff_fff8, 0, 0, 0, 0, 0, 0, 0, 0],
    });
    await importWithCodePost(page, testInfo, "gate3-db-overflow", fastfile);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    )).toBe("XFile/block allocation exhaustion");
    const trace = await page.evaluate(() => structuredClone(
        globalThis.__KISAKCOD_WEB__.database));
    expect(trace).toMatchObject({
        blockSizes: [0xffff_fff8, 0, 0, 0, 0, 0, 0, 0, 0],
        blockAllocationCount: 0,
        blockAllocationBytes: 0,
        streamInitialized: false,
        cleanupComplete: true,
    });
});
