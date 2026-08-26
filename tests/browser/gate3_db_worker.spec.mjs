import { expect, test } from "@playwright/test";
import { deflateSync } from "node:zlib";
import {
    createInstallDirectory,
    createSyntheticCanonicalXFile,
    createSyntheticCanonicalRefillXFile,
    createSyntheticGeneratedPrefixFastfile,
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
}

test("Worker-hosted canonical DB streams an XFile into PMem and stops at generated loading", { tag: "@smoke" }, async ({ page }, testInfo) => {
    const fastfile = createSyntheticGeneratedPrefixFastfile({
        scriptStrings: ["gate3_script_identity"],
        rawFiles: [{ name: "tests/gate3_first.txt", contents: "first" }],
    });
    await importWithCodePost(page, testInfo, "gate3-db-worker", fastfile);

    await expect.poll(() => page.evaluate(() => globalThis.__databaseEvents.some(
        (event) => event.stage === "DB stop" &&
            event.logicalPath === "zone/english/code_post_gfx.ff" &&
            event.stopStage === "Load_XAssetHeader/next-family-closure",
    )), { timeout: 30_000 }).toBe(true);

    const result = await page.evaluate(() => ({
        current: structuredClone(globalThis.__databaseEvents.findLast(
            (event) => event.stage === "DB stop" &&
                event.logicalPath === "zone/english/code_post_gfx.ff")),
        runtime: structuredClone(globalThis.__KISAKCOD_WEB__.database),
        events: structuredClone(globalThis.__databaseEvents),
        domEvidence: JSON.parse(document.querySelector(
            "#database-evidence").textContent),
    }));
    expect(result.current).toMatchObject({
        stage: "DB stop",
        logicalPath: "zone/english/code_post_gfx.ff",
        compressedBytesConsumed: expect.any(Number),
        decompressedBytesProduced: 134,
        inputRefillCount: 1,
        readOffset: 0,
        requestedBytes: 12,
        openSucceeded: true,
        initializedPoolCount: 26,
        freeAssetEntryCount: 32752,
        threadInitialized: true,
        headerValid: true,
        inflateInitialized: true,
        xfileSize: 8192,
        xfileExternalSize: 0,
        blockSizes: [4096, 0, 0, 0, 4096, 0, 0, 0, 0],
        blockAllocationCount: 2,
        blockAllocationBytes: 8192,
        streamBlock: 0,
        streamOffset: 0,
        streamInitialized: true,
        cleanupComplete: true,
        xassetListBegin: true,
        xassetListEnd: true,
        scriptStringCount: 1,
        scriptStringObservedCount: 1,
        scriptStringIdentity: "gate3_script_identity",
        xassetCount: 1,
        assetIndex: 0,
        assetType: 31,
        assetName: "tests/gate3_first.txt",
        pointerClassification: "inline-insert/-2",
        publicationBegin: true,
        publicationEnd: true,
        assetEntryIndex: 16,
        assetPoolIndex: 0,
        freeEntryCountBefore: 32752,
        freeEntryCountAfter: 32751,
        assetZoneIndex: 1,
        generatedLoadFailed: false,
        streamOffsets: [0, 0, 0, 0, 68, 0, 0, 0, 0],
        stopStage: "Load_XAssetHeader/next-family-closure",
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
        "XAssetList begin",
        "script string",
        "XAsset begin",
        "publication begin",
        "publication end",
        "XAsset loaded",
        "XAssetList end",
        "XFile cleanup",
        "DB stop",
    ]));
    expect(result.domEvidence).toEqual(result.runtime);
    expect(result.events.some((event) =>
        /(?:[a-z]:\\|users[/\\]|opfs|indexeddb|[0-9a-f]{8}-[0-9a-f]{4}-4)/iu
            .test(event.logicalPath))).toBe(false);
});

test("canonical generated prefix rejects a truncated XAssetList", async ({ page }, testInfo) => {
    const complete = createSyntheticGeneratedPrefixFastfile({ compressionLevel: 0 });
    const fastfile = complete.slice(0, 12 + 2 + 5 + 52);
    await importWithCodePost(page, testInfo, "gate3-db-list-truncated", fastfile);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    )).toBe("inflate/premature EOF");
    const trace = await page.evaluate(() => structuredClone(
        globalThis.__KISAKCOD_WEB__.database));
    expect(trace).toMatchObject({
        xassetListBegin: false,
        xassetListEnd: false,
        publicationEnd: false,
        cleanupComplete: true,
    });
});

test("canonical generated prefix rejects excessive script strings", async ({ page }, testInfo) => {
    const fastfile = createSyntheticGeneratedPrefixFastfile({
        scriptStringCount: 2000,
    });
    await importWithCodePost(page, testInfo, "gate3-db-strings-excessive", fastfile);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    )).toBe("ScriptStringList/excessive count");
    await expect(page.evaluate(() => ({
        publicationEnd: globalThis.__KISAKCOD_WEB__.database.publicationEnd,
        generatedLoadFailed: globalThis.__KISAKCOD_WEB__.database.generatedLoadFailed,
    }))).resolves.toEqual({ publicationEnd: false, generatedLoadFailed: true });
});

test("canonical generated prefix rejects a truncated script string", async ({ page }, testInfo) => {
    const complete = createSyntheticGeneratedPrefixFastfile({
        scriptStrings: ["gate3_truncated_identity"],
        compressionLevel: 0,
    });
    const fastfile = complete.slice(0, 12 + 2 + 5 + 44 + 16 + 4 + 7);
    await importWithCodePost(page, testInfo, "gate3-db-string-truncated", fastfile);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    )).toBe("inflate/premature EOF");
    const trace = await page.evaluate(() => structuredClone(
        globalThis.__KISAKCOD_WEB__.database));
    expect(trace.scriptStringObservedCount).toBe(0);
    expect(trace.publicationEnd).toBe(false);
    expect(trace.cleanupComplete).toBe(true);
});

test("canonical generated prefix rejects excessive assets", async ({ page }, testInfo) => {
    const fastfile = createSyntheticGeneratedPrefixFastfile({
        assetCount: 1000,
        rawFiles: [{ name: "tests/not_loaded.txt", contents: "no" }],
    });
    await importWithCodePost(page, testInfo, "gate3-db-assets-excessive", fastfile);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    )).toBe("XAssetList/excessive asset count");
    expect(await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.database.publicationEnd)).toBe(false);
});

test("canonical generated prefix rejects an invalid asset type", async ({ page }, testInfo) => {
    const fastfile = createSyntheticGeneratedPrefixFastfile({
        assetType: 99,
        rawFiles: [{ name: "tests/not_loaded.txt", contents: "no" }],
    });
    await importWithCodePost(page, testInfo, "gate3-db-invalid-type", fastfile);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    )).toBe("Load_XAsset/invalid asset type");
    expect(await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.database.publicationEnd)).toBe(false);
});

test("canonical generated prefix rejects a malformed asset alias", async ({ page }, testInfo) => {
    const fastfile = createSyntheticGeneratedPrefixFastfile({
        assetPointer: 0xffff_fffd,
        rawFiles: [{ name: "tests/not_loaded.txt", contents: "no" }],
    });
    await importWithCodePost(page, testInfo, "gate3-db-invalid-alias", fastfile);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    )).toBe("stream/invalid alias offset");
    expect(await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.database.publicationEnd)).toBe(false);
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
    await expect.poll(() => page.evaluate(() => globalThis.__databaseEvents.some(
        (event) => event.stage === "DB stop" &&
            event.logicalPath === "zone/english/code_post_gfx.ff" &&
            event.stopStage === "Load_XAssetHeader/next-family-closure",
    ))).toBe(true);
    const trace = await page.evaluate(() => structuredClone(
        globalThis.__databaseEvents.findLast((event) =>
            event.stage === "DB stop" &&
            event.logicalPath === "zone/english/code_post_gfx.ff")));
    expect(trace).toMatchObject({
        bytesRead: fastfile.byteLength,
        inputRefillCount: 2,
        decompressedBytesProduced: 60,
        blockSizes: [1024, 0, 0, 0, 1024, 0, 0, 0, 0],
        blockAllocationCount: 2,
        streamInitialized: true,
        cleanupComplete: true,
        xassetListBegin: true,
        xassetListEnd: true,
        xassetCount: 0,
    });
    expect(trace.compressedBytesConsumed).toBeGreaterThan(0x40000 - 12);
});
