import { expect, test } from "@playwright/test";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { createInstallDirectory } from "./install_fixture.mjs";

const retailRoot = process.env.KISAK_COD4_RETAIL_ROOT;

test("canonical Gate 3 traverses the locally owned retail startup prerequisite chain", {
    tag: "@retail",
}, async ({ page }, testInfo) => {
    test.skip(!retailRoot,
        "Set KISAK_COD4_RETAIL_ROOT to a legally owned COD4 installation");
    test.setTimeout(180_000);

    const startupZones = await Promise.all(["code_post_gfx", "ui", "common", "killhouse"]
        .map(async (name) => [
            `zone/english/${name}.ff`,
            await readFile(path.join(retailRoot, "zone", "english", `${name}.ff`)),
        ]));
    const directory = await createInstallDirectory(testInfo, "gate3-retail-db", {
        overrides: new Map(startupZones),
    });
    await page.addInitScript(() => {
        Object.defineProperty(globalThis, "showDirectoryPicker", {
            configurable: true,
            value: undefined,
        });
        globalThis.__gate3RetailDbEvents = [];
        globalThis.__gate3LifecycleEvents = [];
        globalThis.__gate3CanonicalWorldEvents = [];
        globalThis.__gate3RendererSurfaceEvents = [];
        globalThis.addEventListener("kisakcod:database", (event) => {
            globalThis.__gate3RetailDbEvents.push(structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:engine-lifecycle", (event) => {
            globalThis.__gate3LifecycleEvents.push(structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:canonical-gfxworld", (event) => {
            globalThis.__gate3CanonicalWorldEvents.push(
                structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:renderer-surface-draw", (event) => {
            globalThis.__gate3RendererSurfaceEvents.push(
                structuredClone(event.detail));
        });
    });
    await page.goto("/");
    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#select-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(directory);

    await expect.poll(() => page.evaluate(() => {
        const database = globalThis.__KISAKCOD_WEB__?.database;
        return database?.logicalPath === "zone/english/common.ff" &&
            database?.stopStage !== "" && database?.assetIndex === 6501;
    }), { timeout: 150_000 }).toBe(true);
    const result = await page.evaluate(() => ({
        final: structuredClone(globalThis.__KISAKCOD_WEB__.database),
        stages: globalThis.__gate3RetailDbEvents.map((event) => event.stage),
        completions: globalThis.__gate3RetailDbEvents
            .filter((event) => event.stage === "XAssetList end")
            .map((event) => ({
                logicalPath: event.logicalPath,
                xassetCount: event.xassetCount,
                assetIndex: event.assetIndex,
                streamOffsets: event.streamOffsets,
            })),
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
        completions: result.completions,
        firstLocalize: localizePublications[0],
        lastLocalize: localizePublications.at(-1),
        lastPublication: result.publications.at(-1),
        emptyNames: result.publications.filter(
            (entry) => entry.assetName.length === 0),
    })}`);
    expect(result.final).toMatchObject({
        stopStage: "Load_XAssetHeader/next-family-closure",
        logicalPath: "zone/english/common.ff",
        assetIndex: 6501,
        assetType: 31,
        assetName: "common",
        publicationEnd: true,
        freeEntryCountAfter: 23115,
        generatedLoadFailed: false,
        xassetListEnd: true,
        streamOffsets: [0, 0, 0, 0, 28021740, 0, 0, 438944, 76704],
    });
    expect(result.completions).toEqual([
        {
            logicalPath: "zone/english/code_post_gfx.ff",
            xassetCount: 1639,
            assetIndex: 1638,
            streamOffsets: [0, 0, 0, 0, 407412, 0, 0, 4224, 480],
        },
        {
            logicalPath: "zone/english/ui.ff",
            xassetCount: 35,
            assetIndex: 34,
            streamOffsets: [0, 0, 0, 0, 1267176, 0, 0, 0, 0],
        },
        {
            logicalPath: "zone/english/common.ff",
            xassetCount: 6502,
            assetIndex: 6501,
            streamOffsets: [0, 0, 0, 0, 28021740, 0, 0, 438944, 76704],
        },
    ]);
    expect(result.publications).toHaveLength(9637);
    expect(publicationsByType).toEqual({
        1: 8,
        2: 1017,
        3: 37,
        4: 395,
        5: 192,
        6: 321,
        7: 1734,
        8: 9,
        9: 772,
        17: 2,
        19: 9,
        20: 5,
        21: 141,
        22: 4379,
        23: 17,
        25: 154,
        26: 2,
        31: 441,
        32: 2,
    });
    expect(localizePublications).toHaveLength(4379);
    expect(localizePublications[0]).toMatchObject({
        assetIndex: 5,
        assetEntryIndex: 22,
        assetPoolIndex: 0,
        freeEntryCountBefore: 32746,
        freeEntryCountAfter: 32745,
    });
    expect(localizePublications.at(-1)).toMatchObject({
        assetIndex: 3990,
        assetName: "STARTS_DRIVE",
        assetEntryIndex: 5848,
        assetPoolIndex: 4378,
        freeEntryCountBefore: 26920,
        freeEntryCountAfter: 26919,
    });
    expect(result.publications.at(-1)).toMatchObject({
        assetIndex: 6501,
        assetType: 31,
        assetName: "common",
        assetEntryIndex: 9652,
        assetPoolIndex: 440,
        freeEntryCountBefore: 23116,
        freeEntryCountAfter: 23115,
    });
    const emptyNames = result.publications.filter(
        (entry) => entry.assetName.length === 0);
    expect(emptyNames).toEqual([
        expect.objectContaining({
            assetIndex: 1202,
            assetType: 8,
            assetEntryIndex: 1219,
            assetPoolIndex: 1,
        }),
        expect.objectContaining({
            assetIndex: 4661,
            assetType: 26,
            assetEntryIndex: 6859,
            assetPoolIndex: 1,
        }),
        expect.objectContaining({
            assetIndex: 4778,
            assetType: 8,
            assetEntryIndex: 1219,
            assetPoolIndex: 1,
        }),
    ]);
    expect(result.publications.filter(
        (entry) => entry.assetName.length > 0)).toHaveLength(9634);

    const mapDbEventStart = await page.evaluate(
        () => globalThis.__gate3RetailDbEvents.length);
    const commandAccepted = await page.evaluate(async () => {
        const bytes = new TextEncoder().encode("map KiLlHoUsE\0");
        return globalThis.__KISAKCOD_WEB__.module.callProbe(
            "_KisakWeb_SubmitCanonicalCommand",
            [bytes],
            [{ kind: "pointer", index: 0 }],
        );
    });
    expect(commandAccepted).toBe(1);
    await expect.poll(() => page.evaluate(() =>
        globalThis.__gate3LifecycleEvents.some((event) =>
            event.stage === "logical fastfile requested" &&
            event.name === "killhouse")), { timeout: 150_000 }).toBe(true);
    const mapLifecycle = await page.evaluate(() => structuredClone(
        globalThis.__gate3LifecycleEvents.filter((event) =>
            event.name === "killhouse" || event.stage === "map command accepted")));
    expect(mapLifecycle.map((event) => event.stage)).toEqual([
        "map command accepted",
        "canonical map name selected",
        "SV_SpawnServer",
        "map loading begins",
        "map zone request constructed",
        "DB_LoadXAssets",
        "DB_LoadXZone",
        "logical fastfile requested",
    ]);
    expect(mapLifecycle.at(-1)).toMatchObject({
        name: "killhouse",
        allocFlags: 8,
        asyncify: false,
        pthreads: false,
    });
    await expect.poll(() => page.evaluate(() =>
        globalThis.__gate3CanonicalWorldEvents.some((event) =>
            event.name === "maps/killhouse.d3dbsp")),
    { timeout: 150_000 }).toBe(true);
    await expect.poll(() => page.evaluate(() =>
        globalThis.__gate3RendererSurfaceEvents.some((event) =>
            event.state === "drawn" && event.vertexCount === 2009 &&
            event.indexCount === 384)),
    { timeout: 30_000 }).toBe(true);
    const mapDatabase = await page.evaluate((eventStart) => ({
        final: structuredClone(globalThis.__KISAKCOD_WEB__.database),
        publicationCount: globalThis.__gate3RetailDbEvents
            .filter((event) => event.stage === "publication end" &&
                event.logicalPath === "zone/english/killhouse.ff").length,
        firstFailure: structuredClone(globalThis.__gate3RetailDbEvents
            .slice(eventStart).find((event) => event.generatedLoadFailed)),
        stops: globalThis.__gate3RetailDbEvents.slice(eventStart)
            .filter((event) => event.stage === "DB stop")
            .map((event) => ({
                stopStage: event.stopStage,
                logicalPath: event.logicalPath,
                assetIndex: event.assetIndex,
                assetType: event.assetType,
                streamOffsets: event.streamOffsets,
            })),
        canonicalWorld: structuredClone(
            globalThis.__gate3CanonicalWorldEvents.at(-1)),
        renderedSurface: structuredClone(
            globalThis.__gate3RendererSurfaceEvents.findLast((event) =>
                event.state === "drawn" && event.vertexCount === 2009 &&
                event.indexCount === 384)),
    }), mapDbEventStart);
    console.log(`KISAK_RETAIL_MAP_DB ${JSON.stringify(mapDatabase)}`);
    expect(mapDatabase.final).toMatchObject({
        logicalPath: "zone/english/killhouse.ff",
        openSucceeded: true,
        xassetListBegin: true,
        scriptStringCount: 892,
        scriptStringObservedCount: 892,
        xassetCount: 1684,
        assetIndex: 773,
        assetType: 13,
        assetName: "maps/killhouse.d3dbsp",
        generatedLoadFailed: true,
        stopStage: "Load_XAssetHeader/unsupported family closure",
        streamOffsets: [0, 509664, 0, 0, 37146694, 0, 0, 21693664, 3128676],
    });
    expect(mapDatabase.publicationCount).toBe(2371);
    expect(mapDatabase.firstFailure).toMatchObject({
        stage: "Load_XAssetHeader/unsupported family closure",
        logicalPath: "zone/english/killhouse.ff",
        assetIndex: 773,
        assetType: 13,
    });
    expect(mapDatabase.stops).toEqual([
        expect.objectContaining({
            stopStage: "Load_XAssetHeader/unsupported family closure",
            logicalPath: "zone/english/killhouse.ff",
            assetIndex: 773,
            assetType: 13,
        }),
    ]);
    expect(mapDatabase.canonicalWorld).toMatchObject({
        state: "submitted",
        sourceRepresentation: "real-kisak-db-gfxworld",
        databaseOwned: true,
        browserWorldRepresentation: false,
        name: "maps/killhouse.d3dbsp",
        baseName: "killhouse",
        planeCount: 5712,
        nodeCount: 5074,
        cellCount: 3,
        vertexCount: 448962,
        indexCount: 829539,
        surfaceCount: 8694,
        staticModelCount: 12255,
        lightmapCount: 3,
        materialMemoryCount: 170,
        inflatedOffset: 86162172,
        assetIndex: 772,
        assetCount: 1684,
        gate2OracleApplicable: true,
        gate2WorldMatch: true,
        gate2GeometryMatch: true,
        gate2MaterialMatch: false,
        gate2SurfaceMatch: false,
        selectedSurface: 6077,
        selectedVertexCount: 2009,
        selectedTriangleCount: 128,
        materialName: "wc/me_ground_mud1",
        adapterResult: "success",
        submissionResult: "success",
    });
    expect(mapDatabase.renderedSurface).toMatchObject({
        state: "drawn",
        vertexCount: 2009,
        indexCount: 384,
        drawFirstIndex: 0,
        drawIndexCount: 384,
        topology: "triangle-list",
        resident: true,
    });
    const prematureCollisionLoad = await page.evaluate(() =>
        globalThis.__gate3LifecycleEvents.filter((event) =>
            event.stage === "CM_LoadMap begin" ||
            event.stage === "CM_LoadMap complete" ||
            event.stage === "Com_LoadWorld begin" ||
            event.stage === "Com_LoadWorld complete" ||
            event.stage === "SaveMemory initialization begin" ||
            event.stage === "SaveMemory initialization complete"));
    expect(prematureCollisionLoad).toEqual([]);
});
