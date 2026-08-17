import { expect, test } from "@playwright/test";
import { createInstallDirectory } from "./install_fixture.mjs";

test("Worker-hosted canonical DB prefix synchronously validates a zone header", { tag: "@smoke" }, async ({ page }, testInfo) => {
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
    ), { timeout: 30_000 }).toBe("DB_LoadXFile/streaming-inflate-closure");

    const result = await page.evaluate(() => ({
        current: structuredClone(globalThis.__KISAKCOD_WEB__.database),
        events: structuredClone(globalThis.__databaseEvents),
    }));
    expect(result.current).toMatchObject({
        stage: "DB stop",
        logicalPath: "zone/english/code_post_gfx.ff",
        bytesRead: 14,
        readOffset: 0,
        requestedBytes: 14,
        openSucceeded: true,
        initializedPoolCount: 26,
        freeAssetEntryCount: 32752,
        threadInitialized: true,
        headerValid: true,
        stopStage: "DB_LoadXFile/streaming-inflate-closure",
    });
    expect(result.current.fileSize).toBeGreaterThan(14);
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
        "zone header read",
        "zone header/framing validation",
        "DB stop",
    ]));
    expect(result.events.some((event) =>
        /(?:[a-z]:\\|users[/\\]|opfs|indexeddb|[0-9a-f]{8}-[0-9a-f]{4}-4)/iu
            .test(event.logicalPath))).toBe(false);
});
