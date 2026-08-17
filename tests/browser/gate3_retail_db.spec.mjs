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
    const publicationsByType = Object.fromEntries(
        [...new Set(result.publications.map((entry) => entry.assetType))]
            .map((type) => [type, result.publications.filter(
                (entry) => entry.assetType === type).length]),
    );
    const localizePublications = result.publications.filter(
        (entry) => entry.assetType === 22);
    console.log(`KISAK_RETAIL_GATE3 ${JSON.stringify({
        final: result.final,
        publicationCount: result.publications.length,
        publicationsByType,
        firstLocalize: localizePublications[0],
        lastLocalize: localizePublications.at(-1),
        lastPublication: result.publications.at(-1),
        emptyNames: result.publications.filter(
            (entry) => entry.assetName.length === 0),
    })}`);
    expect(result.final).toMatchObject({
        stopStage: "Load_XAssetHeader/unsupported family closure",
        assetIndex: 1506,
        assetType: 23,
        streamBlock: 4,
        streamOffset: 373196,
        publicationEnd: true,
        freeEntryCountAfter: 31197,
        pointerClassification: "inline-shared/-1",
    });
    expect(result.publications).toHaveLength(1555);
    expect(publicationsByType).toEqual({
        4: 72,
        5: 86,
        6: 24,
        7: 11,
        8: 2,
        9: 9,
        17: 2,
        19: 9,
        20: 2,
        21: 14,
        22: 1248,
        25: 1,
        26: 1,
        31: 74,
    });
    expect(localizePublications).toHaveLength(1248);
    expect(localizePublications[0]).toMatchObject({
        assetIndex: 5,
        assetEntryIndex: 22,
        assetPoolIndex: 0,
        freeEntryCountBefore: 32746,
        freeEntryCountAfter: 32745,
    });
    expect(localizePublications.at(-1)).toMatchObject({
        assetIndex: 1359,
        assetEntryIndex: 1394,
        assetPoolIndex: 1247,
        freeEntryCountBefore: 31374,
        freeEntryCountAfter: 31373,
    });
    expect(result.publications.at(-1)).toMatchObject({
        assetIndex: 1505,
        assetType: 20,
        assetName: "ui/default.menu",
        assetEntryIndex: 1570,
        assetPoolIndex: 1,
        freeEntryCountBefore: 31198,
        freeEntryCountAfter: 31197,
    });
    const emptyNames = result.publications.filter(
        (entry) => entry.assetName.length === 0);
    expect(emptyNames).toEqual([expect.objectContaining({
        assetIndex: 1202,
        assetType: 8,
        assetEntryIndex: 1219,
        assetPoolIndex: 1,
    })]);
    expect(result.publications.filter(
        (entry) => entry.assetName.length > 0)).toHaveLength(1554);
});
