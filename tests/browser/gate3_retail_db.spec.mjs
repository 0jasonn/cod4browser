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
        globalThis.__gate3RendererSceneViewEvents = [];
        globalThis.__gate3RendererSceneFrameEvents = [];
        globalThis.addEventListener("kisakcod:database", (event) => {
            globalThis.__gate3RetailDbEvents.push(structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:engine-lifecycle", (event) => {
            globalThis.__gate3LifecycleEvents.push(structuredClone(event.detail));
            if (globalThis.__gate3LifecycleEvents.length > 512)
                globalThis.__gate3LifecycleEvents.shift();
        });
        globalThis.addEventListener("kisakcod:canonical-gfxworld", (event) => {
            globalThis.__gate3CanonicalWorldEvents.push(
                structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:renderer-surface-draw", (event) => {
            globalThis.__gate3RendererSurfaceEvents.push(
                structuredClone(event.detail));
            if (globalThis.__gate3RendererSurfaceEvents.length > 16)
                globalThis.__gate3RendererSurfaceEvents.shift();
        });
        globalThis.addEventListener("kisakcod:renderer-scene-view", (event) => {
            globalThis.__gate3RendererSceneViewEvents.push(
                structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:renderer-scene-frame", (event) => {
            globalThis.__gate3RendererSceneFrameEvents.push(
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
        freeEntryCountAfter: 23152,
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
    expect(result.publications).toHaveLength(9635);
    expect(publicationsByType).toEqual({
        1: 8,
        2: 1017,
        3: 37,
        4: 395,
        5: 192,
        6: 321,
        7: 1734,
        8: 7,
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
        assetEntryIndex: 5836,
        assetPoolIndex: 4378,
        freeEntryCountBefore: 26932,
        freeEntryCountAfter: 26931,
    });
    expect(result.publications.at(-1)).toMatchObject({
        assetIndex: 6501,
        assetType: 31,
        assetName: "common",
        assetEntryIndex: 9615,
        assetPoolIndex: 440,
        freeEntryCountBefore: 23153,
        freeEntryCountAfter: 23152,
    });
    const emptyNames = result.publications.filter(
        (entry) => entry.assetName.length === 0);
    expect(emptyNames).toEqual([
        expect.objectContaining({
            assetIndex: 4661,
            assetType: 26,
            assetEntryIndex: 6851,
            assetPoolIndex: 1,
        }),
    ]);
    expect(result.publications.filter(
        (entry) => entry.assetName.length > 0)).toHaveLength(9634);

    const mapDbEventStart = await page.evaluate(
        () => globalThis.__gate3RetailDbEvents.length);
    await page.evaluate(() => {
        globalThis.__gate3MapCommand = { state: "pending" };
        try {
            const bytes = new TextEncoder().encode("map KiLlHoUsE\0");
            Promise.resolve(globalThis.__KISAKCOD_WEB__.module.callProbe(
                "_KisakWeb_SubmitCanonicalCommand",
                [bytes],
                [{ kind: "pointer", index: 0 }],
            )).then((accepted) => {
                globalThis.__gate3MapCommand = {
                    state: "complete",
                    accepted,
                };
            }, (error) => {
                globalThis.__gate3MapCommand = {
                    state: "rejected",
                    message: error?.message ?? String(error),
                    stack: typeof error?.stack === "string" ? error.stack : "",
                };
            });
        } catch (error) {
            globalThis.__gate3MapCommand = {
                state: "threw",
                message: error?.message ?? String(error),
                stack: typeof error?.stack === "string" ? error.stack : "",
            };
        }
    });
    await expect.poll(() => page.evaluate(() =>
        globalThis.__gate3RendererSceneFrameEvents.some((event) =>
            event.state === "drawn" && event.geometrySubmitted === true)),
    { timeout: 150_000 }).toBe(true);
    await expect.poll(() => page.evaluate(() => {
        const view = globalThis.__gate3RendererSceneViewEvents.at(-1);
        return Boolean(view && view.submissionGeneration >= 120 &&
            view.viewOrigin[2] > 50 && view.viewOrigin[2] < 90 &&
            view.worldSurfaceCount === 8064 &&
            view.worldVertexCount === 431747 &&
            view.worldIndexCount === 793188);
    }), { timeout: 30_000 }).toBe(true);

    const beforeKeyboard = await page.evaluate(() => structuredClone(
        globalThis.__gate3RendererSceneViewEvents.at(-1)));
    const canvas = page.locator("#game-canvas");
    await canvas.focus();
    await page.keyboard.down("w");
    await page.waitForTimeout(600);
    await page.keyboard.up("w");
    await expect.poll(() => page.evaluate((origin) => {
        const current = globalThis.__gate3RendererSceneViewEvents.at(-1);
        return current ? Math.hypot(
            current.viewOrigin[0] - origin[0],
            current.viewOrigin[1] - origin[1]) : 0;
    }, beforeKeyboard.viewOrigin), { timeout: 15_000 }).toBeGreaterThan(20);

    const beforeMouse = await page.evaluate(() => structuredClone(
        globalThis.__gate3RendererSceneViewEvents.at(-1)));
    const box = await canvas.boundingBox();
    await canvas.click({ position: { x: box.width / 2, y: box.height / 2 } });
    await expect.poll(() => page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.input.pointerLocked),
    { timeout: 5_000 }).toBe(true);
    await page.mouse.move(
        box.x + box.width / 2 + 100,
        box.y + box.height / 2);
    await expect.poll(() => page.evaluate((forward) => {
        const current = globalThis.__gate3RendererSceneViewEvents.at(-1);
        return current ? Math.hypot(
            current.viewForward[0] - forward[0],
            current.viewForward[1] - forward[1],
            current.viewForward[2] - forward[2]) : 0;
    }, beforeMouse.viewForward), { timeout: 15_000 }).toBeGreaterThan(0.05);

    const checkpoint = await page.evaluate((eventStart) => {
        const mapEvents = globalThis.__gate3RetailDbEvents.slice(eventStart);
        return {
            command: structuredClone(globalThis.__gate3MapCommand),
            lifecycle: structuredClone(globalThis.__gate3LifecycleEvents),
            mapEnd: structuredClone(mapEvents.findLast((event) =>
                event.stage === "XAssetList end")),
            mapPublication: structuredClone(mapEvents.findLast((event) =>
                event.stage === "publication end")),
            generatedFailure: structuredClone(mapEvents.find((event) =>
                event.generatedLoadFailed)),
            canonicalWorld: structuredClone(
                globalThis.__gate3CanonicalWorldEvents.at(-1)),
            sceneView: structuredClone(
                globalThis.__gate3RendererSceneViewEvents.at(-1)),
            sceneFrame: structuredClone(
                globalThis.__gate3RendererSceneFrameEvents.at(-1)),
            logs: structuredClone(globalThis.__KISAKCOD_WEB__.logs.slice(-24)),
        };
    }, mapDbEventStart);
    console.log(`KISAK_RETAIL_LIFECYCLE_CHECKPOINT ${JSON.stringify({
        command: checkpoint.command,
        lifecycle: checkpoint.lifecycle,
        mapEnd: checkpoint.mapEnd,
        mapPublication: checkpoint.mapPublication,
        logs: checkpoint.logs,
        sceneView: checkpoint.sceneView,
        sceneFrame: checkpoint.sceneFrame,
    })}`);

    expect(checkpoint.command).toEqual({ state: "complete", accepted: 1 });

    expect(checkpoint.mapEnd).toMatchObject({
        logicalPath: "zone/english/killhouse.ff",
        scriptStringCount: 892,
        scriptStringObservedCount: 892,
        xassetCount: 1684,
        assetIndex: 1683,
        generatedLoadFailed: false,
        xassetListEnd: true,
    });
    expect(checkpoint.generatedFailure).toBeUndefined();
    expect(checkpoint.canonicalWorld).toMatchObject({
        state: "published",
        sourceRepresentation: "real-kisak-db-gfxworld",
        databaseOwned: true,
        browserWorldRepresentation: false,
        name: "maps/killhouse.d3dbsp",
    });
    expect(checkpoint.sceneView).toMatchObject({
        state: "submitted",
        source: "canonical-cgame-refdef",
        worldName: "maps/killhouse.d3dbsp",
        viewport: { x: 0, y: 0 },
        localClientNum: 0,
        geometrySubmitted: true,
    });
    expect(checkpoint.sceneView.viewport.width).toBeGreaterThan(0);
    expect(checkpoint.sceneView.viewport.height).toBeGreaterThan(0);
    expect(checkpoint.sceneView.tanHalfFovX).toBeGreaterThan(0);
    expect(checkpoint.sceneView.tanHalfFovY).toBeGreaterThan(0);
    expect(checkpoint.sceneView.zNear).toBeGreaterThan(0);
    expect(checkpoint.sceneView.worldSurfaceCount).toBe(8064);
    expect(checkpoint.sceneView.worldVertexCount).toBe(431747);
    expect(checkpoint.sceneView.worldIndexCount).toBe(793188);
    expect(checkpoint.sceneFrame).toMatchObject({
        state: "drawn",
        source: "canonical-cgame-refdef",
        worldName: "maps/killhouse.d3dbsp",
        geometrySubmitted: true,
        backend: "webgl2",
    });
    expect(checkpoint.sceneView.submissionGeneration).toBeGreaterThan(1);
    expect(checkpoint.sceneFrame.viewSubmissionGeneration).toBeGreaterThan(0);
    expect(checkpoint.sceneFrame.viewSubmissionGeneration)
        .toBeLessThanOrEqual(checkpoint.sceneView.submissionGeneration);
    expect(checkpoint.sceneFrame.worldSurfaceCount).toBe(8064);
    expect(checkpoint.sceneFrame.worldVertexCount).toBe(431747);
    expect(checkpoint.sceneFrame.worldIndexCount).toBe(793188);
    expect(checkpoint.sceneView.viewOrigin[2]).toBeGreaterThan(50);
    expect(checkpoint.sceneView.viewOrigin[2]).toBeLessThan(90);
    expect(checkpoint.sceneView.viewOrigin).not.toEqual(beforeKeyboard.viewOrigin);
    expect(checkpoint.sceneView.viewForward).not.toEqual(beforeMouse.viewForward);
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.input))
        .toMatchObject({ pointerLocked: true });
    expect(checkpoint.logs.some(({ text }) =>
        text.includes("First cgame-driven maps/killhouse.d3dbsp frame rendered through WebGL2"),
    )).toBe(true);
    expect(checkpoint.logs.some(({ text }) =>
        text.includes("514 batches: 429 lightmapped, 10 base-only") &&
        text.includes("143/147 images"),
    )).toBe(true);
    const stages = checkpoint.lifecycle.map((event) => event.stage);
    for (const stage of [
        "CM_LoadMap complete",
        "Com_LoadWorld complete",
        "G_InitGame complete",
        "G_LoadLevel complete",
        "SV_InitGameVM complete",
        "SV_InitGameProgs complete",
        "CG_Init complete",
        "CL_InitCGame complete",
    ]) expect(stages).toContain(stage);
    expect(stages).toContain("game-driven frame");
});
