import { expect, test } from "@playwright/test";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { createInstallDirectory } from "./install_fixture.mjs";

const retailRoot = process.env.KISAK_COD4_RETAIL_ROOT;

test("canonical Gate 3 traverses a locally owned retail startup zone", {
    tag: "@retail",
}, async ({ page }, testInfo) => {
    test.skip(!retailRoot,
        "Set KISAK_COD4_RETAIL_ROOT to a legally owned COD4 installation");
    test.setTimeout(120_000);

    const zone = await readFile(path.join(
        retailRoot, "zone", "english", "code_post_gfx.ff"));
    const directory = await createInstallDirectory(testInfo, "gate3-retail-db", {
        overrides: new Map([["zone/english/code_post_gfx.ff", zone]]),
    });
    await page.addInitScript(() => {
        Object.defineProperty(globalThis, "showDirectoryPicker", {
            configurable: true,
            value: undefined,
        });
        globalThis.__gate3RetailDbEvents = [];
        globalThis.addEventListener("kisakcod:database", (event) => {
            globalThis.__gate3RetailDbEvents.push(structuredClone(event.detail));
        });
    });
    await page.goto("/");
    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#select-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(directory);

    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.database?.stopStage,
    ), { timeout: 90_000 }).not.toBe("");
    const result = await page.evaluate(() => ({
        final: structuredClone(globalThis.__KISAKCOD_WEB__.database),
        stages: globalThis.__gate3RetailDbEvents.map((event) => event.stage),
        publications: globalThis.__gate3RetailDbEvents
            .filter((event) => event.stage === "publication end")
            .map((event) => ({
                assetIndex: event.assetIndex,
                assetType: event.assetType,
                assetName: event.assetName,
                assetEntryIndex: event.assetEntryIndex,
                assetPoolIndex: event.assetPoolIndex,
                freeEntryCountBefore: event.freeEntryCountBefore,
                freeEntryCountAfter: event.freeEntryCountAfter,
            })),
    }));
    expect(result.final.openSucceeded).toBe(true);
    expect(result.final.xassetListBegin).toBe(true);
    expect(result.stages).toContain("first generated-loader entry");
    expect(result.final).toMatchObject({
        stopStage: "Load_XAssetHeader/unsupported family closure",
        assetIndex: 2,
        assetType: 4,
        publicationEnd: true,
        freeEntryCountAfter: 32750,
    });
    expect(result.publications).toHaveLength(2);
    expect(result.publications.map((entry) => entry.assetIndex)).toEqual([0, 1]);
    expect(result.publications.map((entry) => entry.assetType)).toEqual([5, 5]);
    expect(result.publications.map((entry) => entry.assetEntryIndex)).toEqual([16, 17]);
    expect(result.publications.map((entry) => entry.assetPoolIndex)).toEqual([0, 1]);
    expect(result.publications.every((entry) => entry.assetName.length > 0)).toBe(true);
    console.log(`KISAK_RETAIL_GATE3 ${JSON.stringify(result)}`);
});
