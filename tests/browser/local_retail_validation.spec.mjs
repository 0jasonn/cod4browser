import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { chromium, expect, test as base } from "@playwright/test";

const retailRoot = process.env.KISAK_COD4_RETAIL_ROOT;
const browserChannel = process.env.KISAK_BROWSER_CHANNEL;

const test = base.extend({
    retailPage: async ({}, use, testInfo) => {
        const profile = await mkdtemp(join(tmpdir(), "kisakcod-retail-"));
        const context = await chromium.launchPersistentContext(profile, {
            baseURL: testInfo.project.use.baseURL,
            headless: testInfo.project.use.headless ?? true,
            viewport: testInfo.project.use.viewport,
            ...(browserChannel ? { channel: browserChannel } : {}),
        });
        const page = context.pages()[0] ?? await context.newPage();
        try {
            await use(page);
        } finally {
            try {
                await context.close();
            } finally {
                await rm(profile, { recursive: true, force: true, maxRetries: 5 });
            }
        }
    },
});

test.skip(!retailRoot,
    "Set KISAK_COD4_RETAIL_ROOT to a legally owned COD4 installation");

async function waitForAssets(page, state)
{
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.assets?.state,
    ), { timeout: 300_000 }).toBe(state);
}

async function submitCommand(page, command)
{
    await page.locator("#engine-command-input").fill(command);
    const commandTimeMs = await page.evaluate(() => performance.now());
    await page.locator("#engine-command-submit").click();
    await expect(page.locator("#engine-command-status"))
        .toHaveText(`Accepted: ${command}`, { timeout: 120_000 });
    return commandTimeMs;
}

async function waitForWorldFrames(page, mapName, minimumGeneration = 1)
{
    await expect.poll(() => page.evaluate(({ name, generation }) => {
        const frame = globalThis.__retailValidationFrames?.findLast(
            (entry) => entry.state === "drawn" && entry.geometrySubmitted === true &&
                entry.worldName?.toLowerCase().includes(name));
        return frame?.viewSubmissionGeneration >= generation;
    }, { name: mapName, generation: minimumGeneration }), {
        timeout: 300_000,
        message: `${mapName} should publish sustained canonical world frames`,
    }).toBe(true);
}

async function sustainWorldFrames(page, mapName, durationMs = 60_000)
{
    const firstGeneration = await page.evaluate((name) =>
        globalThis.__retailValidationFrames.findLast((entry) =>
            entry.state === "drawn" && entry.geometrySubmitted === true &&
            entry.worldName?.toLowerCase().includes(name))?.viewSubmissionGeneration ?? 0,
    mapName);
    await page.waitForTimeout(durationMs);
    const finalGeneration = await page.evaluate((name) =>
        globalThis.__retailValidationFrames.findLast((entry) =>
            entry.state === "drawn" && entry.geometrySubmitted === true &&
            entry.worldName?.toLowerCase().includes(name))?.viewSubmissionGeneration ?? 0,
    mapName);
    expect(finalGeneration - firstGeneration).toBeGreaterThanOrEqual(60);
    return { durationMs, firstGeneration, finalGeneration };
}

async function heapBytes(page)
{
    return page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestHeapBytes"));
}

async function checkpoint(page)
{
    return page.evaluate(async () => {
        const startedMs = performance.now();
        const summary = await globalThis.__KISAKCOD_WEB__.module.checkpoint();
        return { ...summary, durationMs: performance.now() - startedMs };
    });
}

async function captureMapCursor(page)
{
    return page.evaluate(() => ({
        lifecycle: globalThis.__retailLifecycle.length,
        database: globalThis.__retailDatabase.length,
        memory: globalThis.__retailValidationMemory.length,
        frames: globalThis.__retailValidationFrames.length,
    }));
}

async function mapEvidence(page, map, cursor, commandTimeMs, heapBeforeMapLoad,
    heapAfterMapLoad, heapAfterWorldPublication, stability, input, checkpointResult)
{
    return page.evaluate(({ map, cursor, commandTimeMs, heapBeforeMapLoad,
        heapAfterMapLoad, heapAfterWorldPublication, stability, input,
        checkpointResult }) => {
        const frames = globalThis.__retailValidationFrames.slice(cursor.frames)
            .filter((entry) => entry.state === "drawn" &&
                entry.geometrySubmitted === true &&
                entry.worldName?.toLowerCase().includes(map));
        const intervals = frames.slice(1).map((entry, index) =>
            entry.observedMs - frames[index].observedMs).filter((value) => value >= 0);
        const sorted = [...intervals].sort((left, right) => left - right);
        const percentile = (fraction) => sorted.length
            ? sorted[Math.min(sorted.length - 1, Math.ceil(sorted.length * fraction) - 1)]
            : null;
        const lifecycle = globalThis.__retailLifecycle.slice(cursor.lifecycle);
        const database = globalThis.__retailDatabase.slice(cursor.database);
        const memory = globalThis.__retailValidationMemory.slice(cursor.memory);
        const firstFrame = frames[0];
        const cgame = lifecycle.find((event) => event.stage === "CG_Init complete");
        const dbStart = database.find((event) => event.stage === "XAssetList begin");
        const dbComplete = database.findLast((event) => event.stage === "XAssetList end" &&
            (!firstFrame || event.observedMs <= firstFrame.observedMs));
        const latestMemory = memory.findLast((entry) =>
            entry.state === "world-submitted") ?? memory.at(-1);
        const memoryTotal = (entry) => entry ?
            entry.recoveryCopyBytes + entry.gpuTextureEstimateBytes +
                entry.geometryBytes + entry.temporaryUploadBytes +
                entry.shaderProgramCacheEstimateBytes : 0;
        const audio = globalThis.__retailAudioTelemetry.at(-1) ?? null;
        return {
            map,
            mapCommandTimeMs: commandTimeMs,
            databaseStartTimeMs: dbStart?.observedMs ?? null,
            databaseCompletionTimeMs: dbComplete?.observedMs ?? null,
            cgameInitTimeMs: cgame?.observedMs ?? null,
            firstRealWorldFrameTimeMs: firstFrame?.observedMs ?? null,
            mapToFirstFrameMs: firstFrame ? firstFrame.observedMs - commandTimeMs : null,
            averageFrameTimeMs: intervals.length
                ? intervals.reduce((sum, value) => sum + value, 0) / intervals.length : null,
            p95FrameTimeMs: percentile(0.95),
            p99FrameTimeMs: percentile(0.99),
            framesRenderedDuringStabilityWindow: frames.length,
            stability,
            wasmHeapBeforeMapLoad: heapBeforeMapLoad,
            wasmHeapAfterMapLoad: heapAfterMapLoad,
            wasmHeapAfterWorldPublication: heapAfterWorldPublication,
            decodedTextureRecoveryBytes: latestMemory?.recoveryCopyBytes ?? null,
            estimatedGpuTextureBytes: latestMemory?.gpuTextureEstimateBytes ?? null,
            geometryBytes: latestMemory?.geometryBytes ?? null,
            temporaryUploadBytes: latestMemory?.temporaryUploadBytes ?? null,
            shaderProgramBytes: latestMemory?.shaderProgramCacheEstimateBytes ?? null,
            peakMapMemoryBytes: Math.max(0, ...memory.map(memoryTotal)),
            audioDecodedBytes: audio?.decodedPcmBytes ?? null,
            audioQueuedBuffers: audio?.queuedBufferCount ?? null,
            input,
            checkpoint: checkpointResult,
        };
    }, { map, cursor, commandTimeMs, heapBeforeMapLoad, heapAfterMapLoad,
        heapAfterWorldPublication, stability, input, checkpointResult });
}

async function exerciseRetailInput(page)
{
    const canvas = page.locator("#game-canvas");
    const beforeMove = await page.evaluate(() => structuredClone(
        globalThis.__retailValidationViews.at(-1)?.viewOrigin));
    expect(beforeMove).toHaveLength(3);
    if (await page.evaluate(() => globalThis.__KISAKCOD_WEB__.input.absoluteMouse ||
        globalThis.__KISAKCOD_WEB__.input.cursorVisible)) {
        await page.keyboard.press("Escape");
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__.input.absoluteMouse)).toBe(false);
    }
    await canvas.click({ position: { x: 8, y: 8 } });
    await expect.poll(() => page.evaluate(() => document.pointerLockElement?.id))
        .toBe("game-canvas");
    await page.evaluate(() => { globalThis.__retailValidationInput = []; });
    await page.keyboard.down("w");
    await page.waitForTimeout(500);
    await page.keyboard.up("w");
    await expect.poll(() => page.evaluate((origin) => {
        const current = globalThis.__retailValidationViews.at(-1)?.viewOrigin;
        return current ? Math.hypot(
            current[0] - origin[0], current[1] - origin[1], current[2] - origin[2]) : 0;
    }, beforeMove)).toBeGreaterThan(1);
    for (const key of ["s", "a", "d"]) {
        const origin = await page.evaluate(() => structuredClone(
            globalThis.__retailValidationViews.at(-1)?.viewOrigin));
        await page.keyboard.down(key);
        await page.waitForTimeout(350);
        await page.keyboard.up(key);
        await expect.poll(() => page.evaluate((previous) => {
            const current = globalThis.__retailValidationViews.at(-1)?.viewOrigin;
            return current ? Math.hypot(...current.map(
                (value, index) => value - previous[index])) : 0;
        }, origin)).toBeGreaterThan(0.25);
    }
    const beforeJump = await page.evaluate(() =>
        globalThis.__retailValidationViews.at(-1)?.viewOrigin[2]);
    await page.keyboard.press("Space");
    await expect.poll(() => page.evaluate((previous) => Math.abs(
        (globalThis.__retailValidationViews.at(-1)?.viewOrigin[2] ?? previous) - previous),
    beforeJump)).toBeGreaterThan(0.25);

    const audioBefore = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.audioPlayback.reduce(
            (total, entry) => total + entry.count, 0));
    const box = await canvas.boundingBox();
    expect(box).not.toBeNull();
    await canvas.click({ position: { x: box.width / 2, y: box.height / 2 } });
    await expect.poll(() => page.evaluate(() => document.pointerLockElement?.id))
        .toBe("game-canvas");
    const fovBefore = await page.evaluate(() =>
        globalThis.__retailValidationViews.at(-1)?.tanHalfFovX);
    await page.mouse.down({ button: "right" });
    await page.mouse.up({ button: "right" });
    await expect.poll(() => page.evaluate((previous) => Math.abs(
        (globalThis.__retailValidationViews.at(-1)?.tanHalfFovX ?? previous) - previous),
    fovBefore)).toBeGreaterThan(0.0001);
    await page.mouse.wheel(0, -120);
    await page.mouse.wheel(0, 120);
    const lookBefore = await page.evaluate(() => structuredClone(
        globalThis.__retailValidationViews.at(-1)?.viewForward));
    await page.evaluate(() => {
        const movement = new MouseEvent("mousemove");
        Object.defineProperties(movement, {
            movementX: { value: 11 },
            movementY: { value: -5 },
        });
        globalThis.dispatchEvent(movement);
    });
    await expect.poll(() => page.evaluate(() =>
        globalThis.__retailValidationInput.some(
            (event) => event.type === "mouse-move" && event.dx === 11 && event.dy === -5)))
        .toBe(true);
    await expect.poll(() => page.evaluate((previous) => {
        const current = globalThis.__retailValidationViews.at(-1)?.viewForward;
        return current ? Math.hypot(...current.map(
            (value, index) => value - previous[index])) : 0;
    }, lookBefore)).toBeGreaterThan(0.0001);
    await page.evaluate(() => document.exitPointerLock());
    await expect.poll(() => page.evaluate(() => document.pointerLockElement)).toBeNull();
    await expect.poll(() => page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.input.absoluteMouse ||
        globalThis.__KISAKCOD_WEB__.input.cursorVisible)).toBe(true);
    await expect.poll(() => page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.audioPlayback.reduce(
            (total, entry) => total + entry.count, 0)), { timeout: 30_000 })
        .toBeGreaterThan(audioBefore);

    const inputs = await page.evaluate(() => structuredClone(
        globalThis.__retailValidationInput));
    for (const key of [0x77, 0x73, 0x61, 0x64, 0x20, 0x1B, 0xC8, 0xC9, 0xCD, 0xCE]) {
        expect(inputs).toContainEqual({ type: "key", key, down: true });
        expect(inputs).toContainEqual({ type: "key", key, down: false });
    }
    await page.keyboard.press("Escape");
    await expect.poll(() => page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.input.absoluteMouse)).toBe(false);
    return { eventCount: inputs.length, movement: true, mouseLook: true,
        primaryFireAudio: true, secondaryAim: true };
}

test("local retail validation matrix", { tag: "@retail" }, async ({ retailPage: page }) => {
    test.setTimeout(900_000);

    await page.addInitScript(() => {
        globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ = { observeInput: true };
        Object.defineProperty(globalThis, "showDirectoryPicker", {
            configurable: true,
            value: undefined,
        });
        globalThis.__retailValidationFrames = [];
        globalThis.__retailValidationViews = [];
        globalThis.__retailValidationMemory = [];
        globalThis.__retailRendererLifecycle = [];
        globalThis.__retailValidationInput = [];
        globalThis.__retailLifecycle = [];
        globalThis.__retailDatabase = [];
        globalThis.__retailAudioTelemetry = [];
        globalThis.addEventListener("kisakcod:renderer-scene-frame", (event) => {
            globalThis.__retailValidationFrames.push({
                ...structuredClone(event.detail), observedMs: performance.now(),
            });
            if (globalThis.__retailValidationFrames.length > 20_000)
                globalThis.__retailValidationFrames.shift();
        });
        globalThis.addEventListener("kisakcod:renderer-scene-view", (event) => {
            globalThis.__retailValidationViews.push({
                ...structuredClone(event.detail), observedMs: performance.now(),
            });
            if (globalThis.__retailValidationViews.length > 20_000)
                globalThis.__retailValidationViews.shift();
        });
        globalThis.addEventListener("kisakcod:renderer-memory", (event) => {
            globalThis.__retailValidationMemory.push({
                ...structuredClone(event.detail), observedMs: performance.now(),
            });
        });
        globalThis.addEventListener("kisakcod:renderer-lifecycle", (event) => {
            globalThis.__retailRendererLifecycle.push({
                ...structuredClone(event.detail), observedMs: performance.now(),
            });
        });
        globalThis.addEventListener("kisakcod:input", (event) => {
            globalThis.__retailValidationInput.push(structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:engine-lifecycle", (event) => {
            globalThis.__retailLifecycle.push({
                ...structuredClone(event.detail), observedMs: performance.now(),
            });
        });
        globalThis.addEventListener("kisakcod:database", (event) => {
            if (event.detail.stage === "XAssetList begin" ||
                event.detail.stage === "XAssetList end" || event.detail.generatedLoadFailed) {
                globalThis.__retailDatabase.push({
                    stage: event.detail.stage,
                    logicalPath: event.detail.logicalPath,
                    generatedLoadFailed: event.detail.generatedLoadFailed,
                    observedMs: performance.now(),
                });
            }
        });
        globalThis.addEventListener("kisakcod:audio-telemetry", (event) => {
            globalThis.__retailAudioTelemetry.push({
                ...structuredClone(event.detail), observedMs: performance.now(),
            });
        });
    });

    await page.goto("/");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state,
    )).toBe("running");
    await waitForAssets(page, "empty");

    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(retailRoot);
    await waitForAssets(page, "ready");

    const killhouseCursor = await captureMapCursor(page);
    const killhouseHeapBefore = await heapBytes(page);
    const killhouseCommandMs = await submitCommand(page, "map killhouse");
    await expect.poll(() => page.evaluate((start) =>
        globalThis.__retailLifecycle.slice(start).some(
            (event) => event.stage === "CG_Init complete"), killhouseCursor.lifecycle), {
        timeout: 300_000,
    }).toBe(true);
    const killhouseHeapAfterLoad = await heapBytes(page);
    await waitForWorldFrames(page, "killhouse", 120);
    const killhouseHeapAfterWorld = await heapBytes(page);
    const killhouseStability = await sustainWorldFrames(page, "killhouse");

    const killhouseInput = await exerciseRetailInput(page);

    await submitCommand(page, "writeconfig cleanup-validation.cfg");
    const killhouseCheckpoint = await checkpoint(page);
    const killhouseEvidence = await mapEvidence(page, "killhouse", killhouseCursor,
        killhouseCommandMs, killhouseHeapBefore, killhouseHeapAfterLoad,
        killhouseHeapAfterWorld, killhouseStability, killhouseInput,
        killhouseCheckpoint);
    const transitionStart = await page.evaluate(() =>
        globalThis.__retailRendererLifecycle.length);
    const cargoshipCursor = await captureMapCursor(page);
    const cargoshipHeapBefore = await heapBytes(page);
    const cargoshipCommandMs = await submitCommand(page, "map cargoship");
    await expect.poll(() => page.evaluate((start) =>
        globalThis.__retailLifecycle.slice(start).some(
            (event) => event.stage === "CG_Init complete"), cargoshipCursor.lifecycle), {
        timeout: 300_000,
    }).toBe(true);
    const cargoshipHeapAfterLoad = await heapBytes(page);
    await waitForWorldFrames(page, "cargoship", 120);
    const cargoshipHeapAfterWorld = await heapBytes(page);
    const cargoshipStability = await sustainWorldFrames(page, "cargoship");

    const transition = await page.evaluate((start) => structuredClone(
        globalThis.__retailRendererLifecycle.slice(start)), transitionStart);
    const unloadBeginIndex = transition.findIndex(
        (event) => event.state === "worldUnloadBegin");
    const unloadEndIndex = transition.findIndex(
        (event, index) => index > unloadBeginIndex && event.state === "worldUnloadEnd");
    const publishedIndex = transition.findIndex(
        (event, index) => index > unloadEndIndex && event.state === "newWorldPublished");
    expect(unloadBeginIndex).toBeGreaterThanOrEqual(0);
    expect(unloadEndIndex).toBeGreaterThan(unloadBeginIndex);
    expect(publishedIndex).toBeGreaterThan(unloadEndIndex);
    expect(transition[unloadEndIndex].oldMapBytesReleased).toBeGreaterThan(0);
    expect(transition[unloadEndIndex].contextGenerationUnchanged).toBe(true);
    expect(transition[unloadEndIndex].contextGenerationAfter)
        .toBe(transition[publishedIndex].contextGenerationAfter);
    const cargoshipInput = await exerciseRetailInput(page);
    await submitCommand(page, "writeconfig cleanup-validation.cfg");
    const cargoshipCheckpoint = await checkpoint(page);

    const recoveryBefore = await page.evaluate(() => ({
        count: globalThis.__KISAKCOD_WEB__.rendererShader.recoveryCount ?? 0,
        frame: globalThis.__retailValidationFrames.findLast(
            (entry) => entry.worldName?.toLowerCase().includes("cargoship"))
            ?.viewSubmissionGeneration ?? 0,
    }));
    expect(await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestLoseWebGLContext")))
        .toBeTruthy();
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.rendererShader.state,
    )).toBe("lost");
    await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestRestoreWebGLContext"));
    await expect.poll(() => page.evaluate((before) => ({
        state: globalThis.__KISAKCOD_WEB__.rendererShader.state,
        recovered: globalThis.__KISAKCOD_WEB__.rendererShader.recoveryCount > before,
    }), recoveryBefore.count), { timeout: 30_000 })
        .toEqual({ state: "ready", recovered: true });
    await waitForWorldFrames(page, "cargoship", recoveryBefore.frame + 1);
    const recoveryInput = await exerciseRetailInput(page);

    const cargoshipEvidence = await mapEvidence(page, "cargoship", cargoshipCursor,
        cargoshipCommandMs, cargoshipHeapBefore, cargoshipHeapAfterLoad,
        cargoshipHeapAfterWorld, cargoshipStability, cargoshipInput,
        cargoshipCheckpoint);

    const transitionEvidence = await page.evaluate(({ transitionStart,
        unloadBeginIndex, unloadEndIndex, publishedIndex }) => {
        const memory = globalThis.__retailValidationMemory;
        const lifecycle = globalThis.__retailRendererLifecycle.slice(transitionStart);
        const unloadBegin = lifecycle[unloadBeginIndex];
        const unload = lifecycle[unloadEndIndex];
        const published = lifecycle[publishedIndex];
        const firstMemoryIndex = memory.findLastIndex((entry) =>
            entry.state === "world-unload-begin" &&
            entry.observedMs <= unloadBegin.observedMs);
        const transitionMemory = memory.slice(Math.max(0, firstMemoryIndex))
            .filter((entry) => entry.observedMs <= published.observedMs);
        const total = (entry) => entry.recoveryCopyBytes +
            entry.gpuTextureEstimateBytes + entry.geometryBytes +
            entry.temporaryUploadBytes + entry.shaderProgramCacheEstimateBytes;
        return {
            contextGenerationBefore: unload.contextGenerationBefore,
            contextGenerationAfter: unload.contextGenerationAfter,
            oldMapDecodedBytesBeforeUnload: unloadBegin.recoveryBytes,
            oldMapDecodedBytesAfterUnload: unload.recoveryBytes,
            oldMapDecodedBytesBeforeNewWorldPublication: unload.recoveryBytes,
            decodedBytesAfterNewWorldPublication: published.recoveryBytes,
            oldMapBytesReleased: unload.oldMapBytesReleased,
            peakTransitionBytes: Math.max(0, ...transitionMemory.map(total)),
            peakDecodedRecoveryBytes: Math.max(0, ...transitionMemory.map(
                (entry) => entry.recoveryCopyBytes)),
            recoveryBudgetBytes: memory.at(-1)?.recoveryBudgetBytes ?? 0,
            unloadEndIndex,
            publishedIndex,
        };
    }, { transitionStart, unloadBeginIndex, unloadEndIndex, publishedIndex });
    expect(Math.max(killhouseEvidence.decodedTextureRecoveryBytes,
        cargoshipEvidence.decodedTextureRecoveryBytes,
        transitionEvidence.peakDecodedRecoveryBytes))
        .toBeLessThanOrEqual(transitionEvidence.recoveryBudgetBytes);

    const recoveryAfter = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.rendererShader.recoveryCount ?? 0);
    const shutdownFlushDurationMs = await page.evaluate(async () => {
        const startedMs = performance.now();
        await globalThis.__KISAKCOD_WEB__.module.dispose();
        return performance.now() - startedMs;
    });
    await page.reload();
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state))
        .toBe("running");
    await waitForAssets(page, "ready");
    await submitCommand(page, "exec cleanup-validation.cfg");
    expect(await page.locator("#boot-log").textContent())
        .not.toContain("couldn't exec cleanup-validation.cfg");
    console.log(`KISAK_RETAIL_RESULT ${JSON.stringify({
        maps: { killhouse: killhouseEvidence, cargoship: cargoshipEvidence },
        transition: transitionEvidence,
        contextRecovery: {
            countBefore: recoveryBefore.count,
            countAfter: recoveryAfter,
            framesResumed: true,
            inputResumed: recoveryInput,
        },
        shutdownFlushDurationMs,
        saveReloadVerified: true,
    })}`);
});
