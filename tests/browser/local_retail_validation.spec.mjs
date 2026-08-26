import { execFileSync } from "node:child_process";
import { mkdtemp, rm } from "node:fs/promises";
import { platform, release, tmpdir, version } from "node:os";
import { join } from "node:path";

import { chromium, expect, test as base } from "@playwright/test";

const retailRoot = process.env.KISAK_COD4_RETAIL_ROOT;
const browserChannel = process.env.KISAK_BROWSER_CHANNEL;
const phase3TargetMap = process.env.KISAK_RETAIL_PHASE3_MAP?.trim().toLowerCase();
if (phase3TargetMap && phase3TargetMap !== "blackout") {
    throw new Error("KISAK_RETAIL_PHASE3_MAP currently supports only blackout");
}
const sourceCommit = execFileSync(
    "git", ["rev-parse", "HEAD"], { encoding: "utf8" }).trim();
const sourceDirty = execFileSync(
    "git", ["status", "--porcelain"], { encoding: "utf8" }).trim().length > 0;
const allowDirty = process.env.KISAK_RETAIL_ALLOW_DIRTY === "1";
const requestedExploratoryStabilityMs = Number(
    process.env.KISAK_RETAIL_EXPLORATORY_STABILITY_MS ?? 60_000);
const stabilityDurationMs = allowDirty &&
    Number.isInteger(requestedExploratoryStabilityMs) &&
    requestedExploratoryStabilityMs >= 1_000 &&
    requestedExploratoryStabilityMs <= 60_000
    ? requestedExploratoryStabilityMs : 60_000;
const operatingSystem = {
    platform: platform(),
    release: release(),
    version: version(),
    architecture: process.arch,
};
let failureStage = "test setup";
let failureClass = "unknown";
let retailBrowserMetadata = {
    name: "chromium",
    version: null,
    channel: browserChannel ?? null,
};

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
    "RETAIL_ROOT_MISSING: set KISAK_COD4_RETAIL_ROOT to a legally owned COD4 installation");

test.afterEach(async ({}, testInfo) => {
    if (testInfo.status === testInfo.expectedStatus) return;
    const phase3 = testInfo.tags.includes("@retail-phase3");
    const prefix = phase3
        ? "KISAK_RETAIL_PHASE3_RESULT" : "KISAK_RETAIL_RESULT";
    console.log(`${prefix} ${JSON.stringify({
        schemaVersion: 1,
        source: { commitSha: sourceCommit, dirty: sourceDirty },
        recordedAtUtc: new Date().toISOString(),
        environment: {
            browser: retailBrowserMetadata,
            operatingSystem,
            build: "Release diagnostics",
        },
        validationResult: "fail",
        failureStage,
        failureClass,
        ...(phase3 ? { targetMap: phase3TargetMap } : {}),
    })}`);
});

async function installRetailObservers(page)
{
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
        globalThis.__retailLogs = [];
        globalThis.addEventListener("kisakcod:log", (event) => {
            globalThis.__retailLogs.push(structuredClone(event.detail));
        });
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
}

async function waitForAssets(page, state)
{
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.assets?.state,
    ), { timeout: 300_000 }).toBe(state);
    if (state === "ready") {
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__?.module?.filesystemState,
        ), {
            timeout: 300_000,
            message: "canonical filesystem should finish mounting",
        }).toBe("mounted");
    }
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

async function waitForDatabaseCompletion(page, mapName, databaseCursor)
{
    const logicalPath = `zone/english/${mapName}.ff`;
    await expect.poll(() => page.evaluate(({ cursor, path }) => {
        const terminal = globalThis.__retailDatabase.slice(cursor).findLast(
            (event) => event.logicalPath.toLowerCase() === path &&
                (event.stage === "XAssetList end" || event.generatedLoadFailed));
        return terminal ? "terminal" : "pending";
    }, { cursor: databaseCursor, path: logicalPath }), {
        timeout: 300_000,
        message: `${mapName} should reach a canonical database terminal event`,
    }).toBe("terminal");
    const events = await page.evaluate(({ cursor, path }) => structuredClone(
        globalThis.__retailDatabase.slice(cursor).filter(
            (event) => event.logicalPath.toLowerCase() === path)), {
        cursor: databaseCursor,
        path: logicalPath,
    });
    expect(events.some((event) => event.stage === "XAssetList begin")).toBe(true);
    expect(events.find((event) => event.generatedLoadFailed)).toBeUndefined();
    expect(events.some((event) => event.stage === "XAssetList end")).toBe(true);
}

async function waitForWorldFrames(page, mapName, minimumGeneration = 1)
{
    const outcome = await page.waitForFunction(({ name, generation }) => {
        const frame = globalThis.__retailValidationFrames?.findLast(
            (entry) => entry.state === "drawn" && entry.geometrySubmitted === true &&
                entry.worldName?.toLowerCase().includes(name));
        if (frame?.viewSubmissionGeneration >= generation) return "drawn";
        return globalThis.__retailLogs?.findLast((entry) =>
            entry.text?.includes("Canonical world submission failed:"))?.text ?? null;
    }, { name: mapName, generation: minimumGeneration }, { timeout: 300_000 });
    const result = await outcome.jsonValue();
    await outcome.dispose();
    expect(result, `${mapName} should publish sustained canonical world frames`)
        .toBe("drawn");
}

async function sustainWorldFrames(page, mapName, durationMs = 60_000)
{
    const started = await page.evaluate((name) => ({
        observedMs: performance.now(),
        generation: globalThis.__retailValidationFrames.findLast((entry) =>
            entry.state === "drawn" && entry.geometrySubmitted === true &&
            entry.worldName?.toLowerCase().includes(name))?.viewSubmissionGeneration ?? 0,
    }), mapName);
    await page.waitForTimeout(durationMs);
    const ended = await page.evaluate((name) => ({
        observedMs: performance.now(),
        generation: globalThis.__retailValidationFrames.findLast((entry) =>
            entry.state === "drawn" && entry.geometrySubmitted === true &&
            entry.worldName?.toLowerCase().includes(name))?.viewSubmissionGeneration ?? 0,
    }), mapName);
    expect(ended.observedMs - started.observedMs).toBeGreaterThanOrEqual(durationMs);
    expect(ended.generation - started.generation)
        .toBeGreaterThanOrEqual(Math.max(1, Math.floor(durationMs / 1000)));
    return {
        requestedDurationMs: durationMs,
        observedDurationMs: ended.observedMs - started.observedMs,
        startedMs: started.observedMs,
        endedMs: ended.observedMs,
        firstGeneration: started.generation,
        finalGeneration: ended.generation,
    };
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

async function writeConfigAndCheckpoint(page)
{
    const logStart = await page.evaluate(() =>
        globalThis.__retailLogs.length);
    await submitCommand(page, "writeconfig cleanup-validation.cfg");
    await expect.poll(() => page.evaluate((start) => {
        const messages = globalThis.__retailLogs.slice(start)
            .map(({ text }) => text)
            .filter((text) => text.toLowerCase().includes("cleanup-validation"));
        return messages.some((text) =>
            text.includes("Writing cleanup-validation.cfg."))
            ? "written" : messages.join(" | ") || "pending";
    }, logStart), {
        timeout: 30_000,
    }).toBe("written");
    return checkpoint(page);
}

async function rendererMemorySnapshot(page)
{
    const before = await page.evaluate(() =>
        globalThis.__retailValidationMemory.length);
    const recoveryCopyBytes = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call(
            "_KisakWeb_TestEmitRendererMemory"));
    await expect.poll(() => page.evaluate((start) =>
        globalThis.__retailValidationMemory.slice(start).some(
            (entry) => entry.state === "diagnostic-snapshot"), before)).toBe(true);
    const snapshot = await page.evaluate((start) => structuredClone(
        globalThis.__retailValidationMemory.slice(start).findLast(
            (entry) => entry.state === "diagnostic-snapshot")), before);
    expect(snapshot.state).toBe("diagnostic-snapshot");
    expect(snapshot.recoveryCopyBytes).toBe(recoveryCopyBytes);
    return snapshot;
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
    heapAfterMapLoad, heapAfterWorldPublication, heapAtStabilityEnd, stability,
    memorySnapshot, audioSnapshot, input, checkpointResult)
{
    return page.evaluate(({ map, cursor, commandTimeMs, heapBeforeMapLoad,
        heapAfterMapLoad, heapAfterWorldPublication, heapAtStabilityEnd,
        stability, memorySnapshot, audioSnapshot, input, checkpointResult }) => {
        const frames = globalThis.__retailValidationFrames.slice(cursor.frames)
            .filter((entry) => entry.state === "drawn" &&
                entry.geometrySubmitted === true &&
                entry.worldName?.toLowerCase().includes(map));
        const stabilityFrames = frames.filter((entry) =>
            entry.observedMs >= stability.startedMs &&
            entry.observedMs <= stability.endedMs);
        const intervals = stabilityFrames.slice(1).map((entry, index) =>
            entry.observedMs - stabilityFrames[index].observedMs)
            .filter((value) => value >= 0);
        const sorted = [...intervals].sort((left, right) => left - right);
        const percentile = (fraction) => sorted.length
            ? sorted[Math.min(sorted.length - 1, Math.ceil(sorted.length * fraction) - 1)]
            : null;
        const lifecycle = globalThis.__retailLifecycle.slice(cursor.lifecycle);
        const database = globalThis.__retailDatabase.slice(cursor.database);
        const firstFrame = frames[0];
        const cgame = lifecycle.find((event) => event.stage === "CG_Init complete");
        const logicalPath = `zone/english/${map}.ff`;
        const mapDatabase = database.filter((event) =>
            event.logicalPath.toLowerCase() === logicalPath);
        const dbStart = mapDatabase.find(
            (event) => event.stage === "XAssetList begin");
        const dbComplete = mapDatabase.findLast(
            (event) => event.stage === "XAssetList end" && !event.generatedLoadFailed);
        return {
            map,
            validationResult: "pass",
            failureStage: null,
            failureClass: null,
            checks: {
                commandAccepted: true,
                databaseCompleted: dbComplete !== undefined,
                cgameInitialized: cgame !== undefined,
                worldFrameProduced: firstFrame !== undefined,
                stability60s: stabilityFrames.length >= 60,
                input: Boolean(input) && Object.values(input).every(Boolean),
                audio: (audioSnapshot?.decodedPcmBytes ?? 0) > 0,
                checkpoint: checkpointResult.bytesPersisted > 0,
                noFatalError: globalThis.__KISAKCOD_WEB__.state === "running",
            },
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
            framesRenderedDuringStabilityWindow: stabilityFrames.length,
            stability,
            wasmLinearMemoryCapacityBytes: {
                beforeMapLoad: heapBeforeMapLoad,
                afterCGameInit: heapAfterMapLoad,
                afterWorldPublication: heapAfterWorldPublication,
                atStabilityEnd: heapAtStabilityEnd,
            },
            decodedTextureRecoveryBytes: memorySnapshot.decodedTextureSourceBytes,
            aggregateCpuRecoveryBytes: memorySnapshot.recoveryCopyBytes,
            estimatedGpuTextureBytes: memorySnapshot.gpuTextureEstimateBytes,
            geometryBytes: memorySnapshot.geometryBytes,
            temporaryUploadBytes: memorySnapshot.temporaryUploadBytes,
            shaderProgramBytes: memorySnapshot.shaderProgramCacheEstimateBytes,
            imagePoolRecoveryBytes: {
                world: memorySnapshot.worldImageRecoveryBytes,
                staticModels: memorySnapshot.staticModelImageRecoveryBytes,
                dynamicModels: memorySnapshot.dynamicModelImageRecoveryBytes,
                ui: memorySnapshot.uiImageRecoveryBytes,
                perPoolAdmissionLimit: memorySnapshot.recoveryBudgetBytes,
            },
            supplementalTextureRecoveryBytes:
                memorySnapshot.supplementalTextureRecoveryBytes,
            audioDecodedBytes: audioSnapshot?.decodedPcmBytes ?? null,
            audioQueuedBuffers: audioSnapshot?.queuedBufferCount ?? null,
            input,
            checkpoint: checkpointResult,
        };
    }, { map, cursor, commandTimeMs, heapBeforeMapLoad, heapAfterMapLoad,
        heapAfterWorldPublication, heapAtStabilityEnd, stability,
        memorySnapshot, audioSnapshot, input, checkpointResult });
}

function assertMemoryTelemetry(sample)
{
    const imagePoolBytes = [
        sample.worldImageRecoveryBytes,
        sample.staticModelImageRecoveryBytes,
        sample.dynamicModelImageRecoveryBytes,
        sample.uiImageRecoveryBytes,
    ];
    expect(imagePoolBytes.reduce((sum, bytes) => sum + bytes, 0) +
        sample.supplementalTextureRecoveryBytes)
        .toBe(sample.decodedTextureSourceBytes);
    expect(sample.decodedTextureSourceBytes + sample.geometryBytes +
        sample.shaderProgramCacheEstimateBytes)
        .toBe(sample.recoveryCopyBytes);
    expect(sample.gpuTextureEstimateBytes)
        .toBeGreaterThanOrEqual(sample.decodedTextureSourceBytes);
    for (const bytes of imagePoolBytes) {
        expect(bytes).toBeLessThanOrEqual(sample.recoveryBudgetBytes);
    }
}

function assertMapEvidence(evidence, memorySnapshot)
{
    assertMemoryTelemetry(memorySnapshot);
    expect(Object.values(evidence.checks).every(Boolean)).toBe(true);
    expect(evidence.databaseStartTimeMs).not.toBeNull();
    expect(evidence.databaseCompletionTimeMs).not.toBeNull();
    expect(evidence.databaseCompletionTimeMs)
        .toBeGreaterThanOrEqual(evidence.databaseStartTimeMs);
    expect(evidence.cgameInitTimeMs).not.toBeNull();
    expect(evidence.firstRealWorldFrameTimeMs).not.toBeNull();
    expect(evidence.framesRenderedDuringStabilityWindow).toBeGreaterThanOrEqual(60);
    expect(evidence.averageFrameTimeMs).not.toBeNull();
    expect(evidence.p95FrameTimeMs).not.toBeNull();
    expect(evidence.p99FrameTimeMs).not.toBeNull();
    expect(evidence.audioDecodedBytes).toBeGreaterThan(0);
    expect(evidence.audioQueuedBuffers).toBeGreaterThanOrEqual(0);
    expect(evidence.checkpoint.filesPersisted).toBeGreaterThan(0);
    expect(evidence.checkpoint.bytesPersisted).toBeGreaterThan(0);
    expect(evidence.checkpoint.durationMs).toBeGreaterThanOrEqual(0);
}

const canonicalMapLifecycleStages = [
    "CM_LoadMap complete",
    "Com_LoadWorld complete",
    "G_InitGame complete",
    "G_LoadLevel complete",
    "SV_InitGameVM complete",
    "SV_InitGameProgs complete",
    "CG_Init complete",
    "CL_InitCGame complete",
];

async function waitForLifecycleStages(page, lifecycleCursor, requiredStages)
{
    await expect.poll(() => page.evaluate(({ cursor, required }) => {
        const observed = new Set(globalThis.__retailLifecycle.slice(cursor)
            .map((event) => event.stage));
        return required.filter((stage) => !observed.has(stage));
    }, { cursor: lifecycleCursor, required: requiredStages }), {
        timeout: 300_000,
    }).toEqual([]);
}

async function lifecycleEvidence(page, lifecycleCursor)
{
    return page.evaluate(({ cursor, required }) => structuredClone(
        globalThis.__retailLifecycle.slice(cursor)
            .filter((event) => required.includes(event.stage))
            .map((event) => ({
                stage: event.stage,
                observedMs: event.observedMs,
            }))), {
        cursor: lifecycleCursor,
        required: canonicalMapLifecycleStages,
    });
}

async function firstWorldFrameEvidence(page, mapName, frameCursor)
{
    await waitForWorldFrames(page, mapName);
    const frame = await page.evaluate(({ cursor, name }) => structuredClone(
        globalThis.__retailValidationFrames.slice(cursor).find(
            (entry) => entry.state === "drawn" && entry.geometrySubmitted === true &&
                entry.worldName?.toLowerCase().includes(name)) ?? null), {
        cursor: frameCursor,
        name: mapName,
    });
    expect(frame).not.toBeNull();
    return frame;
}

async function rendererTransitionEvidence(page, lifecycleCursor, startedMs, endedMs)
{
    const lifecycle = await page.evaluate((cursor) => structuredClone(
        globalThis.__retailRendererLifecycle.slice(cursor)), lifecycleCursor);
    const unloadBeginIndex = lifecycle.findIndex(
        (event) => event.state === "worldUnloadBegin");
    const unloadEndIndex = lifecycle.findIndex(
        (event, index) => index > unloadBeginIndex && event.state === "worldUnloadEnd");
    const publishedIndex = lifecycle.findIndex(
        (event, index) => index > unloadEndIndex && event.state === "newWorldPublished");
    expect(unloadBeginIndex).toBeGreaterThanOrEqual(0);
    expect(unloadEndIndex).toBeGreaterThan(unloadBeginIndex);
    expect(publishedIndex).toBeGreaterThan(unloadEndIndex);
    expect(lifecycle[unloadEndIndex].oldMapBytesReleased).toBeGreaterThan(0);
    expect(lifecycle[unloadEndIndex].contextGenerationUnchanged).toBe(true);
    expect(lifecycle[unloadEndIndex].contextGenerationAfter)
        .toBe(lifecycle[publishedIndex].contextGenerationAfter);

    const memory = await page.evaluate(({ start, end }) => structuredClone(
        globalThis.__retailValidationMemory.filter((entry) =>
            entry.observedMs >= start && entry.observedMs <= end)), {
        start: startedMs,
        end: endedMs,
    });
    expect(memory.length).toBeGreaterThanOrEqual(3);
    for (const sample of memory) assertMemoryTelemetry(sample);
    const unloadBegin = memory.find((entry) => entry.state === "world-unload-begin");
    const unloadEnd = memory.find((entry) => entry.state === "world-unloaded");
    const newWorld = memory.find((entry) => entry.state === "world-submitted" &&
        entry.observedMs > unloadEnd?.observedMs);
    expect(unloadBegin).toBeTruthy();
    expect(unloadEnd).toBeTruthy();
    expect(newWorld).toBeTruthy();
    expect(unloadEnd.worldImageRecoveryBytes).toBe(0);
    expect(unloadEnd.staticModelImageRecoveryBytes).toBe(0);
    expect(unloadEnd.dynamicModelImageRecoveryBytes).toBe(0);
    expect(unloadEnd.uiImageRecoveryBytes).toBe(0);
    expect(unloadEnd.supplementalTextureRecoveryBytes).toBe(0);
    expect(unloadEnd.decodedTextureSourceBytes).toBe(0);
    expect(unloadEnd.geometryBytes).toBe(0);
    expect(lifecycle[unloadEndIndex].oldMapBytesReleased)
        .toBe(unloadBegin.recoveryCopyBytes - unloadEnd.recoveryCopyBytes);
    const maximum = (key) => Math.max(...memory.map((entry) => entry[key]));
    return {
        validationResult: "pass",
        durationToFirstWorldFrameMs: endedMs - startedMs,
        contextGenerationBefore: lifecycle[unloadEndIndex].contextGenerationBefore,
        contextGenerationAfter: lifecycle[unloadEndIndex].contextGenerationAfter,
        oldMap: {
            aggregateCpuRecoveryBytesBeforeUnload: unloadBegin.recoveryCopyBytes,
            aggregateCpuRecoveryBytesAfterUnload: unloadEnd.recoveryCopyBytes,
            aggregateCpuRecoveryBytesReleased:
                lifecycle[unloadEndIndex].oldMapBytesReleased,
        },
        newMapAggregateCpuRecoveryBytesAtWorldPublication:
            newWorld.recoveryCopyBytes,
        peakObserved: {
            decodedTextureRecoveryBytes: maximum("decodedTextureSourceBytes"),
            aggregateCpuRecoveryBytes: maximum("recoveryCopyBytes"),
            estimatedGpuTextureBytes: maximum("gpuTextureEstimateBytes"),
            geometryBytes: maximum("geometryBytes"),
            boundaryRetainedTemporaryUploadBytes: maximum("temporaryUploadBytes"),
            shaderProgramBytes: maximum("shaderProgramCacheEstimateBytes"),
        },
        lifecycleOrder: ["worldUnloadBegin", "worldUnloadEnd", "newWorldPublished"],
    };
}

async function recoverMapContext(page, mapName)
{
    const before = await page.evaluate((name) => ({
        startedMs: performance.now(),
        contextLosses: globalThis.__KISAKCOD_WEB__.contextLosses,
        surfaceRecoveryCount:
            globalThis.__KISAKCOD_WEB__.rendererSurface.recoveryCount ?? 0,
        frame: structuredClone(globalThis.__retailValidationFrames.findLast(
            (entry) => entry.worldName?.toLowerCase().includes(name))),
    }), mapName);
    expect(before.frame).toBeTruthy();
    expect(await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestLoseWebGLContext")))
        .toBeTruthy();
    await expect.poll(() => page.evaluate(() => ({
        runtime: globalThis.__KISAKCOD_WEB__.state,
        surface: globalThis.__KISAKCOD_WEB__.rendererSurface.state,
    }))).toEqual({ runtime: "renderer-lost", surface: "lost" });
    expect(await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestRestoreWebGLContext")))
        .toBeTruthy();
    await expect.poll(() => page.evaluate(({ previous, name }) => ({
        runtime: globalThis.__KISAKCOD_WEB__.state,
        contextLossRecorded:
            globalThis.__KISAKCOD_WEB__.contextLosses > previous.contextLosses,
        surface: globalThis.__KISAKCOD_WEB__.rendererSurface.state,
        surfaceRecovered:
            globalThis.__KISAKCOD_WEB__.rendererSurface.recoveryCount >
                previous.surfaceRecoveryCount,
        canonicalResourcesRebuilt:
            (globalThis.__retailValidationFrames.findLast((entry) =>
                entry.worldName?.toLowerCase().includes(name))
                ?.resourceGeneration ?? 0) >
                    (previous.frame?.resourceGeneration ?? 0),
    }), { previous: before, name: mapName }), { timeout: 30_000 }).toEqual({
        runtime: "running",
        contextLossRecorded: true,
        surface: "ready",
        surfaceRecovered: true,
        canonicalResourcesRebuilt: true,
    });
    await waitForWorldFrames(page, mapName,
        before.frame.viewSubmissionGeneration + 1);
    const completedMs = await page.evaluate(() => performance.now());
    const input = await exerciseRetailInput(page);
    const after = await page.evaluate((name) => ({
        contextLosses: globalThis.__KISAKCOD_WEB__.contextLosses,
        surfaceRecoveryCount:
            globalThis.__KISAKCOD_WEB__.rendererSurface.recoveryCount ?? 0,
        frame: structuredClone(globalThis.__retailValidationFrames.findLast(
            (entry) => entry.worldName?.toLowerCase().includes(name))),
    }), mapName);
    return {
        validationResult: "pass",
        durationToFirstRecoveredWorldFrameMs: completedMs - before.startedMs,
        contextLossesBefore: before.contextLosses,
        contextLossesAfter: after.contextLosses,
        surfaceRecoveryCountBefore: before.surfaceRecoveryCount,
        surfaceRecoveryCountAfter: after.surfaceRecoveryCount,
        resourceGenerationBefore: before.frame.resourceGeneration,
        resourceGenerationAfter: after.frame.resourceGeneration,
        framesResumed: true,
        inputResumed: input,
    };
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
    const holdUntilMoved = async (key, origin, minimumDistance) => {
        await page.keyboard.down(key);
        try {
            await expect.poll(() => page.evaluate((previous) => {
                const current = globalThis.__retailValidationViews.at(-1)?.viewOrigin;
                return current ? Math.hypot(...current.map(
                    (value, index) => value - previous[index])) : 0;
            }, origin), { timeout: 15_000 }).toBeGreaterThan(minimumDistance);
        } finally {
            await page.keyboard.up(key);
        }
    };

    await holdUntilMoved("w", beforeMove, 1);
    for (const key of ["s", "a", "d"]) {
        const origin = await page.evaluate(() => structuredClone(
            globalThis.__retailValidationViews.at(-1)?.viewOrigin));
        await holdUntilMoved(key, origin, 0.25);
    }
    const beforeJump = await page.evaluate(() =>
        globalThis.__retailValidationViews.at(-1)?.viewOrigin[2]);
    await page.keyboard.down("Space");
    try {
        await expect.poll(() => page.evaluate((previous) => Math.abs(
            (globalThis.__retailValidationViews.at(-1)?.viewOrigin[2] ?? previous) - previous),
        beforeJump), { timeout: 15_000 }).toBeGreaterThan(0.25);
    } finally {
        await page.keyboard.up("Space");
    }

    const audioBefore = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.audioPlayback.reduce(
            (total, entry) => total + entry.count, 0));
    const box = await canvas.boundingBox();
    expect(box).not.toBeNull();
    await canvas.click({ position: { x: box.width / 2, y: box.height / 2 } });
    await expect.poll(() => page.evaluate(() => document.pointerLockElement?.id))
        .toBe("game-canvas");
    const adsBefore = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestUsingAds"));
    await page.mouse.down({ button: "right" });
    try {
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestUsingAds")),
        { timeout: 15_000 }).not.toBe(adsBefore);
    } finally {
        await page.mouse.up({ button: "right" });
    }
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
        primaryFireAudio: true, secondaryAction: true };
}

test("local retail validation matrix", { tag: "@retail" }, async ({ retailPage: page }) => {
    test.setTimeout(900_000);
    if (!allowDirty) expect(sourceDirty,
        "authoritative retail validation requires a clean source commit").toBe(false);
    const browser = page.context().browser();
    retailBrowserMetadata = {
        name: browser?.browserType().name() ?? "unknown",
        version: browser?.version() ?? "unknown",
        channel: browserChannel ?? null,
    };
    const pageErrors = [];
    page.on("pageerror", (error) => pageErrors.push(error.message));
    await installRetailObservers(page);

    failureStage = "runtime bootstrap";
    failureClass = "lifecycle";
    await page.goto("/");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state,
    )).toBe("running");
    await waitForAssets(page, "empty");

    failureStage = "installation/profile validation";
    failureClass = "filesystem";
    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(retailRoot);
    await waitForAssets(page, "ready");

    failureStage = "Killhouse database load";
    failureClass = "database";
    const killhouseCursor = await captureMapCursor(page);
    const killhouseHeapBefore = await heapBytes(page);
    const killhouseCommandMs = await submitCommand(page, "map killhouse");
    await waitForDatabaseCompletion(page, "killhouse", killhouseCursor.database);
    failureStage = "Killhouse CGame initialization";
    failureClass = "cgame";
    await expect.poll(() => page.evaluate((start) =>
        globalThis.__retailLifecycle.slice(start).some(
            (event) => event.stage === "CG_Init complete"), killhouseCursor.lifecycle), {
        timeout: 300_000,
    }).toBe(true);
    const killhouseHeapAfterLoad = await heapBytes(page);
    await waitForWorldFrames(page, "killhouse", 120);
    const killhouseHeapAfterWorld = await heapBytes(page);
    const killhouseStability = await sustainWorldFrames(
        page, "killhouse", stabilityDurationMs);
    const killhouseHeapAtStabilityEnd = await heapBytes(page);
    const killhouseMemory = await rendererMemorySnapshot(page);
    const killhouseAudio = await page.evaluate(() => structuredClone(
        globalThis.__retailAudioTelemetry.at(-1) ?? null));

    failureStage = "Killhouse gameplay validation";
    const killhouseInput = await exerciseRetailInput(page);
    failureStage = "Killhouse checkpoint";
    failureClass = "lifecycle";
    const killhouseCheckpoint = await writeConfigAndCheckpoint(page);
    const killhouseEvidence = await mapEvidence(page, "killhouse", killhouseCursor,
        killhouseCommandMs, killhouseHeapBefore, killhouseHeapAfterLoad,
        killhouseHeapAfterWorld, killhouseHeapAtStabilityEnd, killhouseStability,
        killhouseMemory, killhouseAudio, killhouseInput, killhouseCheckpoint);
    assertMapEvidence(killhouseEvidence, killhouseMemory);
    failureStage = "Killhouse to CargoShip transition";
    failureClass = "lifecycle";
    const transitionStart = await page.evaluate(() =>
        globalThis.__retailRendererLifecycle.length);
    const cargoshipCursor = await captureMapCursor(page);
    const cargoshipHeapBefore = await heapBytes(page);
    const cargoshipCommandMs = await submitCommand(page, "map cargoship");
    failureStage = "CargoShip database load";
    failureClass = "database";
    await waitForDatabaseCompletion(page, "cargoship", cargoshipCursor.database);
    failureStage = "CargoShip CGame initialization";
    failureClass = "cgame";
    await expect.poll(() => page.evaluate((start) =>
        globalThis.__retailLifecycle.slice(start).some(
            (event) => event.stage === "CG_Init complete"), cargoshipCursor.lifecycle), {
        timeout: 300_000,
    }).toBe(true);
    const cargoshipHeapAfterLoad = await heapBytes(page);
    await waitForWorldFrames(page, "cargoship", 120);
    const cargoshipHeapAfterWorld = await heapBytes(page);
    const cargoshipStability = await sustainWorldFrames(
        page, "cargoship", stabilityDurationMs);
    const cargoshipHeapAtStabilityEnd = await heapBytes(page);
    const cargoshipMemory = await rendererMemorySnapshot(page);
    const cargoshipAudio = await page.evaluate(() => structuredClone(
        globalThis.__retailAudioTelemetry.at(-1) ?? null));
    expect(await page.evaluate(() => globalThis.__retailLogs
        .filter(({ text }) => text.includes("duration must be greater than 0"))
        .map(({ text }) => text))).toEqual([]);

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
    failureStage = "CargoShip gameplay validation";
    const cargoshipInput = await exerciseRetailInput(page);
    failureStage = "CargoShip checkpoint";
    failureClass = "lifecycle";
    const cargoshipCheckpoint = await writeConfigAndCheckpoint(page);

    failureStage = "CargoShip WebGL context recovery";
    failureClass = "renderer";
    const recoveryBefore = await page.evaluate(() => ({
        startedMs: performance.now(),
        contextLosses: globalThis.__KISAKCOD_WEB__.contextLosses,
        surfaceRecoveryCount:
            globalThis.__KISAKCOD_WEB__.rendererSurface.recoveryCount ?? 0,
        frame: structuredClone(globalThis.__retailValidationFrames.findLast(
            (entry) => entry.worldName?.toLowerCase().includes("cargoship"))),
    }));
    expect(await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestLoseWebGLContext")))
        .toBeTruthy();
    await expect.poll(() => page.evaluate(() => ({
        runtime: globalThis.__KISAKCOD_WEB__.state,
        surface: globalThis.__KISAKCOD_WEB__.rendererSurface.state,
    }))).toEqual({ runtime: "renderer-lost", surface: "lost" });
    expect(await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestRestoreWebGLContext")))
        .toBeTruthy();
    await expect.poll(() => page.evaluate((before) => ({
        runtime: globalThis.__KISAKCOD_WEB__.state,
        contextLossRecorded:
            globalThis.__KISAKCOD_WEB__.contextLosses > before.contextLosses,
        surface: globalThis.__KISAKCOD_WEB__.rendererSurface.state,
        surfaceRecovered: globalThis.__KISAKCOD_WEB__.rendererSurface.recoveryCount >
            before.surfaceRecoveryCount,
        canonicalResourcesRebuilt:
            (globalThis.__retailValidationFrames.findLast((entry) =>
                entry.worldName?.toLowerCase().includes("cargoship"))
                ?.resourceGeneration ?? 0) > (before.frame?.resourceGeneration ?? 0),
    }), recoveryBefore), { timeout: 30_000 }).toEqual({
        runtime: "running",
        contextLossRecorded: true,
        surface: "ready",
        surfaceRecovered: true,
        canonicalResourcesRebuilt: true,
    });
    await waitForWorldFrames(page, "cargoship",
        recoveryBefore.frame.viewSubmissionGeneration + 1);
    const recoveryCompletedMs = await page.evaluate(() => performance.now());
    const recoveryInput = await exerciseRetailInput(page);

    const cargoshipEvidence = await mapEvidence(page, "cargoship", cargoshipCursor,
        cargoshipCommandMs, cargoshipHeapBefore, cargoshipHeapAfterLoad,
        cargoshipHeapAfterWorld, cargoshipHeapAtStabilityEnd, cargoshipStability,
        cargoshipMemory, cargoshipAudio, cargoshipInput, cargoshipCheckpoint);
    assertMapEvidence(cargoshipEvidence, cargoshipMemory);

    const transitionMemory = await page.evaluate(({ startedMs, endedMs }) =>
        structuredClone(globalThis.__retailValidationMemory.filter((entry) =>
            entry.observedMs >= startedMs && entry.observedMs <= endedMs)), {
        startedMs: cargoshipCommandMs,
        endedMs: cargoshipEvidence.firstRealWorldFrameTimeMs,
    });
    expect(transitionMemory.length).toBeGreaterThanOrEqual(3);
    for (const sample of transitionMemory) assertMemoryTelemetry(sample);
    const unloadBeginMemory = transitionMemory.find(
        (entry) => entry.state === "world-unload-begin");
    const unloadEndMemory = transitionMemory.find(
        (entry) => entry.state === "world-unloaded");
    const newWorldMemory = transitionMemory.find(
        (entry) => entry.state === "world-submitted" &&
            entry.observedMs > unloadEndMemory?.observedMs);
    expect(unloadBeginMemory).toBeTruthy();
    expect(unloadEndMemory).toBeTruthy();
    expect(newWorldMemory).toBeTruthy();
    expect(unloadEndMemory.worldImageRecoveryBytes).toBe(0);
    expect(unloadEndMemory.staticModelImageRecoveryBytes).toBe(0);
    expect(unloadEndMemory.dynamicModelImageRecoveryBytes).toBe(0);
    expect(unloadEndMemory.uiImageRecoveryBytes).toBe(0);
    expect(unloadEndMemory.geometryBytes).toBe(0);
    expect(transition[unloadEndIndex].oldMapBytesReleased)
        .toBe(unloadBeginMemory.recoveryCopyBytes - unloadEndMemory.recoveryCopyBytes);
    const maximum = (key) => Math.max(...transitionMemory.map(
        (entry) => entry[key]));
    const transitionEvidence = {
        validationResult: "pass",
        durationToFirstWorldFrameMs:
            cargoshipEvidence.firstRealWorldFrameTimeMs - cargoshipCommandMs,
        contextGenerationBefore: transition[unloadEndIndex].contextGenerationBefore,
        contextGenerationAfter: transition[unloadEndIndex].contextGenerationAfter,
        oldMap: {
            decodedTextureRecoveryBytesBeforeUnload:
                unloadBeginMemory.decodedTextureSourceBytes,
            decodedTextureRecoveryBytesAfterUnload:
                unloadEndMemory.decodedTextureSourceBytes,
            aggregateCpuRecoveryBytesBeforeUnload:
                unloadBeginMemory.recoveryCopyBytes,
            aggregateCpuRecoveryBytesAfterUnload:
                unloadEndMemory.recoveryCopyBytes,
            aggregateCpuRecoveryBytesReleased:
                transition[unloadEndIndex].oldMapBytesReleased,
        },
        newMapAggregateCpuRecoveryBytesAtWorldPublication:
            newWorldMemory.recoveryCopyBytes,
        peakObserved: {
            decodedTextureRecoveryBytes: maximum("decodedTextureSourceBytes"),
            aggregateCpuRecoveryBytes: maximum("recoveryCopyBytes"),
            estimatedGpuTextureBytes: maximum("gpuTextureEstimateBytes"),
            geometryBytes: maximum("geometryBytes"),
            boundaryRetainedTemporaryUploadBytes: maximum("temporaryUploadBytes"),
            shaderProgramBytes: maximum("shaderProgramCacheEstimateBytes"),
            imagePoolRecoveryBytes: {
                world: maximum("worldImageRecoveryBytes"),
                staticModels: maximum("staticModelImageRecoveryBytes"),
                dynamicModels: maximum("dynamicModelImageRecoveryBytes"),
                ui: maximum("uiImageRecoveryBytes"),
                perPoolAdmissionLimit: unloadBeginMemory.recoveryBudgetBytes,
            },
            wasmLinearMemoryCapacityBytes: Math.max(
                cargoshipEvidence.wasmLinearMemoryCapacityBytes.beforeMapLoad,
                cargoshipEvidence.wasmLinearMemoryCapacityBytes.afterCGameInit,
                cargoshipEvidence.wasmLinearMemoryCapacityBytes.afterWorldPublication),
        },
        lifecycleOrder: ["worldUnloadBegin", "worldUnloadEnd", "newWorldPublished"],
    };

    const recoveryAfter = await page.evaluate(() => ({
        contextLosses: globalThis.__KISAKCOD_WEB__.contextLosses,
        surfaceRecoveryCount:
            globalThis.__KISAKCOD_WEB__.rendererSurface.recoveryCount ?? 0,
        frame: structuredClone(globalThis.__retailValidationFrames.findLast(
            (entry) => entry.worldName?.toLowerCase().includes("cargoship"))),
    }));
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.state))
        .toBe("running");
    expect(pageErrors).toEqual([]);
    failureStage = "shutdown flush";
    failureClass = "lifecycle";
    const shutdownFlushDurationMs = await page.evaluate(async () => {
        const startedMs = performance.now();
        await globalThis.__KISAKCOD_WEB__.module.dispose();
        return performance.now() - startedMs;
    });
    await page.reload();
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state))
        .toBe("running");
    await waitForAssets(page, "ready");
    failureStage = "reload persistence";
    const reloadLogStart = await page.evaluate(() =>
        globalThis.__retailLogs.length);
    await submitCommand(page, "exec cleanup-validation.cfg");
    await expect.poll(() => page.evaluate((start) => {
        const messages = globalThis.__retailLogs.slice(start)
            .map(({ text }) => text);
        if (messages.some((text) =>
            text.includes("couldn't exec cleanup-validation.cfg"))) return "failed";
        if (messages.some((text) =>
            text.includes("execing cleanup-validation.cfg from disk"))) return "loaded";
        return "pending";
    }, reloadLogStart), { timeout: 30_000 }).toBe("loaded");
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.state))
        .toBe("running");
    expect(pageErrors).toEqual([]);
    const validationRecord = {
        schemaVersion: 1,
        source: { commitSha: sourceCommit, dirty: sourceDirty },
        recordedAtUtc: new Date().toISOString(),
        environment: {
            browser: retailBrowserMetadata,
            operatingSystem,
            build: "Release diagnostics",
        },
        validationResult: "pass",
        failureStage: null,
        failureClass: null,
        maps: { killhouse: killhouseEvidence, cargoship: cargoshipEvidence },
        transition: transitionEvidence,
        contextRecovery: {
            validationResult: "pass",
            durationToFirstRecoveredWorldFrameMs:
                recoveryCompletedMs - recoveryBefore.startedMs,
            contextLossesBefore: recoveryBefore.contextLosses,
            contextLossesAfter: recoveryAfter.contextLosses,
            surfaceRecoveryCountBefore: recoveryBefore.surfaceRecoveryCount,
            surfaceRecoveryCountAfter: recoveryAfter.surfaceRecoveryCount,
            resourceGenerationBefore: recoveryBefore.frame.resourceGeneration,
            resourceGenerationAfter: recoveryAfter.frame.resourceGeneration,
            framesResumed: true,
            inputResumed: recoveryInput,
        },
        shutdown: {
            flushDurationMs: shutdownFlushDurationMs,
            reloadResult: "pass",
            persistedProfileReady: true,
            persistedConfigLoaded: true,
        },
    };
    console.log(`KISAK_RETAIL_RESULT ${JSON.stringify(validationRecord)}`);
});

if (phase3TargetMap) {
    test("local retail Phase 3 campaign map", { tag: "@retail-phase3" },
        async ({ retailPage: page }) => {
            test.setTimeout(900_000);
            failureStage = "Phase 3 test setup";
            failureClass = "unknown";
            expect(sourceDirty,
                "authoritative retail validation requires a clean source commit")
                .toBe(false);
            const browser = page.context().browser();
            retailBrowserMetadata = {
                name: browser?.browserType().name() ?? "unknown",
                version: browser?.version() ?? "unknown",
                channel: browserChannel ?? null,
            };
            const pageErrors = [];
            page.on("pageerror", (error) => pageErrors.push(error.message));
            await installRetailObservers(page);

            failureStage = "Phase 3 runtime bootstrap";
            failureClass = "lifecycle";
            await page.goto("/");
            await expect.poll(() => page.evaluate(
                () => globalThis.__KISAKCOD_WEB__?.state,
            )).toBe("running");
            await waitForAssets(page, "empty");

            failureStage = "Phase 3 installation/profile validation";
            failureClass = "filesystem";
            const chooserPromise = page.waitForEvent("filechooser");
            await page.locator("#portable-install-button").click();
            const chooser = await chooserPromise;
            await chooser.setFiles(retailRoot);
            await waitForAssets(page, "ready");

            failureStage = "CargoShip baseline database load";
            failureClass = "database";
            const cargoshipCursor = await captureMapCursor(page);
            const cargoshipCommandMs = await submitCommand(page, "map cargoship");
            await waitForDatabaseCompletion(
                page, "cargoship", cargoshipCursor.database);
            failureStage = "CargoShip baseline canonical lifecycle";
            failureClass = "cgame";
            await waitForLifecycleStages(
                page, cargoshipCursor.lifecycle, canonicalMapLifecycleStages);
            failureStage = "CargoShip baseline first world frame";
            failureClass = "renderer";
            const cargoshipFrame = await firstWorldFrameEvidence(
                page, "cargoship", cargoshipCursor.frames);
            const cargoshipLifecycle = await lifecycleEvidence(
                page, cargoshipCursor.lifecycle);

            failureStage = `CargoShip to ${phase3TargetMap} transition`;
            failureClass = "lifecycle";
            const transitionInCursor = await page.evaluate(() =>
                globalThis.__retailRendererLifecycle.length);
            const targetCursor = await captureMapCursor(page);
            const targetHeapBefore = await heapBytes(page);
            const targetCommandMs = await submitCommand(
                page, `map ${phase3TargetMap}`);

            failureStage = `${phase3TargetMap} database load`;
            failureClass = "database";
            await waitForDatabaseCompletion(
                page, phase3TargetMap, targetCursor.database);
            failureStage = `${phase3TargetMap} ClipMap/world initialization`;
            await waitForLifecycleStages(page, targetCursor.lifecycle, [
                "CM_LoadMap complete",
                "Com_LoadWorld complete",
            ]);
            failureStage = `${phase3TargetMap} server/game initialization`;
            failureClass = "cgame";
            await waitForLifecycleStages(page, targetCursor.lifecycle, [
                "G_InitGame complete",
                "G_LoadLevel complete",
                "SV_InitGameVM complete",
                "SV_InitGameProgs complete",
            ]);
            failureStage = `${phase3TargetMap} client/cgame initialization`;
            await waitForLifecycleStages(page, targetCursor.lifecycle, [
                "CG_Init complete",
                "CL_InitCGame complete",
            ]);
            const targetHeapAfterLoad = await heapBytes(page);
            failureStage = `${phase3TargetMap} first world frame`;
            failureClass = "renderer";
            const targetFrame = await firstWorldFrameEvidence(
                page, phase3TargetMap, targetCursor.frames);
            const targetHeapAfterWorld = await heapBytes(page);
            const transitionIn = await rendererTransitionEvidence(
                page, transitionInCursor, targetCommandMs, targetFrame.observedMs);

            failureStage = `${phase3TargetMap} 60-second stability`;
            const targetStability = await sustainWorldFrames(
                page, phase3TargetMap, 60_000);
            const targetHeapAtStabilityEnd = await heapBytes(page);
            failureStage = `${phase3TargetMap} renderer memory`;
            failureClass = "memory";
            const targetMemory = await rendererMemorySnapshot(page);
            failureStage = `${phase3TargetMap} gameplay input/audio`;
            failureClass = "unknown";
            const targetInput = await exerciseRetailInput(page);
            const targetAudio = await page.evaluate(() => structuredClone(
                globalThis.__retailAudioTelemetry.at(-1) ?? null));
            expect(targetAudio?.decodedPcmBytes ?? 0).toBeGreaterThan(0);
            failureStage = `${phase3TargetMap} config checkpoint`;
            failureClass = "lifecycle";
            const targetCheckpoint = await writeConfigAndCheckpoint(page);
            const targetEvidence = await mapEvidence(
                page, phase3TargetMap, targetCursor, targetCommandMs,
                targetHeapBefore, targetHeapAfterLoad, targetHeapAfterWorld,
                targetHeapAtStabilityEnd, targetStability, targetMemory,
                targetAudio, targetInput, targetCheckpoint);
            targetEvidence.canonicalLifecycle = await lifecycleEvidence(
                page, targetCursor.lifecycle);
            assertMapEvidence(targetEvidence, targetMemory);

            failureStage = `${phase3TargetMap} WebGL context recovery`;
            failureClass = "renderer";
            const contextRecovery = await recoverMapContext(page, phase3TargetMap);

            failureStage = `${phase3TargetMap} to Killhouse transition`;
            failureClass = "lifecycle";
            const transitionOutCursor = await page.evaluate(() =>
                globalThis.__retailRendererLifecycle.length);
            const killhouseCursor = await captureMapCursor(page);
            const killhouseCommandMs = await submitCommand(page, "map killhouse");
            failureStage = "Killhouse transition-out database load";
            failureClass = "database";
            await waitForDatabaseCompletion(
                page, "killhouse", killhouseCursor.database);
            failureStage = "Killhouse transition-out canonical lifecycle";
            failureClass = "cgame";
            await waitForLifecycleStages(
                page, killhouseCursor.lifecycle, canonicalMapLifecycleStages);
            failureStage = "Killhouse transition-out first world frame";
            failureClass = "renderer";
            const killhouseFrame = await firstWorldFrameEvidence(
                page, "killhouse", killhouseCursor.frames);
            const transitionOut = await rendererTransitionEvidence(
                page, transitionOutCursor, killhouseCommandMs,
                killhouseFrame.observedMs);
            const killhouseLifecycle = await lifecycleEvidence(
                page, killhouseCursor.lifecycle);
            expect(pageErrors).toEqual([]);

            failureStage = "Phase 3 shutdown flush";
            failureClass = "lifecycle";
            const shutdownFlushDurationMs = await page.evaluate(async () => {
                const startedMs = performance.now();
                await globalThis.__KISAKCOD_WEB__.module.dispose();
                return performance.now() - startedMs;
            });
            await page.reload();
            await expect.poll(() => page.evaluate(() =>
                globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
            await waitForAssets(page, "ready");
            failureStage = "Phase 3 reload persistence";
            const reloadLogStart = await page.evaluate(() =>
                globalThis.__retailLogs.length);
            await submitCommand(page, "exec cleanup-validation.cfg");
            await expect.poll(() => page.evaluate((start) => {
                const messages = globalThis.__retailLogs.slice(start)
                    .map(({ text }) => text);
                if (messages.some((text) =>
                    text.includes("couldn't exec cleanup-validation.cfg"))) return "failed";
                if (messages.some((text) =>
                    text.includes("execing cleanup-validation.cfg from disk"))) return "loaded";
                return "pending";
            }, reloadLogStart), { timeout: 30_000 }).toBe("loaded");
            expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.state))
                .toBe("running");
            expect(pageErrors).toEqual([]);

            const validationRecord = {
                schemaVersion: 1,
                source: { commitSha: sourceCommit, dirty: sourceDirty },
                recordedAtUtc: new Date().toISOString(),
                environment: {
                    browser: retailBrowserMetadata,
                    operatingSystem,
                    build: "Release diagnostics",
                },
                validationResult: "pass",
                failureStage: null,
                failureClass: null,
                targetMap: phase3TargetMap,
                baseline: {
                    map: "cargoship",
                    commandAccepted: true,
                    mapCommandTimeMs: cargoshipCommandMs,
                    firstRealWorldFrameTimeMs: cargoshipFrame.observedMs,
                    canonicalLifecycle: cargoshipLifecycle,
                },
                map: targetEvidence,
                transitionIn: {
                    from: "cargoship",
                    to: phase3TargetMap,
                    ...transitionIn,
                },
                contextRecovery,
                transitionOut: {
                    from: phase3TargetMap,
                    to: "killhouse",
                    ...transitionOut,
                    destinationCanonicalLifecycle: killhouseLifecycle,
                },
                shutdown: {
                    flushDurationMs: shutdownFlushDurationMs,
                    reloadResult: "pass",
                    persistedProfileReady: true,
                    persistedConfigLoaded: true,
                },
            };
            console.log(`KISAK_RETAIL_PHASE3_RESULT ${JSON.stringify(
                validationRecord)}`);
        });
}
