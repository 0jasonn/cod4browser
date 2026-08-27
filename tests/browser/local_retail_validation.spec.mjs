import { execFileSync } from "node:child_process";
import { mkdtemp, rm } from "node:fs/promises";
import { cpus, platform, release, tmpdir, totalmem, version } from "node:os";
import { join } from "node:path";

import { chromium, expect, test as base } from "@playwright/test";

import { summarizeForegroundSamples } from "./retail_foreground_window.mjs";

const retailRoot = process.env.KISAK_COD4_RETAIL_ROOT;
const browserChannel = process.env.KISAK_BROWSER_CHANNEL;
const phase3TargetMap = process.env.KISAK_RETAIL_PHASE3_MAP?.trim().toLowerCase();
if (phase3TargetMap && (!/^[a-z0-9_]+$/.test(phase3TargetMap) ||
    phase3TargetMap.startsWith("mp_") || phase3TargetMap.endsWith("_mp"))) {
    throw new Error("KISAK_RETAIL_PHASE3_MAP must name one single-player zone");
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
const frameProfileSampleLimit = 300;
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
    headless: true,
};
const referenceHardware = {
    processor: cpus()[0]?.model ?? "unknown",
    logicalProcessorCount: cpus().length,
    totalSystemMemoryBytes: totalmem(),
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
        schemaVersion: 3,
        source: { commitSha: sourceCommit, dirty: sourceDirty },
        recordedAtUtc: new Date().toISOString(),
        environment: {
            browser: retailBrowserMetadata,
            operatingSystem,
            referenceHardware,
            build: "Release diagnostics",
        },
        browserHeadless: retailBrowserMetadata.headless,
        browserName: retailBrowserMetadata.name,
        browserVersion: retailBrowserMetadata.version,
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
        globalThis.__retailFrameProfiles = [];
        globalThis.__retailRendererLifecycle = [];
        globalThis.__retailValidationInput = [];
        globalThis.__retailLifecycle = [];
        globalThis.__retailDatabase = [];
        globalThis.__retailAudioTelemetry = [];
        globalThis.__retailAudioPlaybackCount = 0;
        globalThis.__retailLogs = [];
        globalThis.addEventListener("kisakcod:log", (event) => {
            globalThis.__retailLogs.push(structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:renderer-scene-frame", (event) => {
            globalThis.__retailValidationFrames.push({
                ...structuredClone(event.detail), observedMs: performance.now(),
            });
            if (globalThis.__retailValidationFrames.length > 100_000)
                globalThis.__retailValidationFrames.shift();
        });
        globalThis.addEventListener("kisakcod:renderer-scene-view", (event) => {
            globalThis.__retailValidationViews.push({
                ...structuredClone(event.detail), observedMs: performance.now(),
            });
            if (globalThis.__retailValidationViews.length > 100_000)
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
        globalThis.addEventListener("kisakcod:frame-profile", (event) => {
            globalThis.__retailFrameProfiles.push({
                ...structuredClone(event.detail), observedMs: performance.now(),
            });
            if (globalThis.__retailFrameProfiles.length > 4_096)
                globalThis.__retailFrameProfiles.shift();
        });
        globalThis.addEventListener("kisakcod:audio-playback", () => {
            ++globalThis.__retailAudioPlaybackCount;
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
    const input = page.locator("#engine-command-input");
    await expect(input).toBeEditable({ timeout: 30_000 });
    await input.fill(command, { timeout: 30_000 });
    const commandTimeMs = await page.evaluate(() => performance.now());
    await page.locator("#engine-command-form").evaluate((form) => form.requestSubmit());
    await expect(page.locator("#engine-command-status"))
        .toHaveText(`Accepted: ${command}`, { timeout: 30_000 });
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
    await page.bringToFront();
    expect(await page.evaluate((sampleLimit) =>
        globalThis.__KISAKCOD_WEB__.module.call(
            "_KisakWeb_TestBeginFrameProfile", sampleLimit),
    frameProfileSampleLimit)).toBe(1);
    const sampleForeground = () => page.evaluate(() => ({
        observedMs: performance.now(),
        visibilityState: document.visibilityState,
        pageFocused: document.hasFocus(),
    }));
    const foregroundSamples = [await sampleForeground()];
    const started = await page.evaluate((name) => ({
        observedMs: performance.now(),
        generation: globalThis.__retailValidationFrames.findLast((entry) =>
            entry.state === "drawn" && entry.geometrySubmitted === true &&
            entry.worldName?.toLowerCase().includes(name))?.viewSubmissionGeneration ?? 0,
    }), mapName);
    const deadline = Date.now() + durationMs;
    while (Date.now() < deadline) {
        await page.waitForTimeout(Math.min(1_000, deadline - Date.now()));
        foregroundSamples.push(await sampleForeground());
    }
    const ended = await page.evaluate((name) => ({
        observedMs: performance.now(),
        generation: globalThis.__retailValidationFrames.findLast((entry) =>
            entry.state === "drawn" && entry.geometrySubmitted === true &&
            entry.worldName?.toLowerCase().includes(name))?.viewSubmissionGeneration ?? 0,
    }), mapName);
    expect(ended.observedMs - started.observedMs).toBeGreaterThanOrEqual(durationMs);
    const foreground = summarizeForegroundSamples(foregroundSamples);
    expect(ended.generation - started.generation).toBeGreaterThanOrEqual(
        foreground.performanceWindowValid
            ? Math.max(1, Math.floor(durationMs / 1000)) : 1);
    return {
        requestedDurationMs: durationMs,
        observedDurationMs: ended.observedMs - started.observedMs,
        startedMs: started.observedMs,
        endedMs: ended.observedMs,
        firstGeneration: started.generation,
        finalGeneration: ended.generation,
        ...foreground,
    };
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
        profiles: globalThis.__retailFrameProfiles.length,
    }));
}

async function mapEvidence(page, map, cursor, commandTimeMs, memoryLifecycle,
    stability, audioSnapshot, input, checkpointResult)
{
    return page.evaluate(({ map, cursor, commandTimeMs, memoryLifecycle,
        stability, audioSnapshot, input, checkpointResult,
        frameProfileSampleLimit }) => {
        const memorySnapshot = memoryLifecycle.steadyState;
        const frames = globalThis.__retailValidationFrames.slice(cursor.frames)
            .filter((entry) => entry.state === "drawn" &&
                entry.geometrySubmitted === true &&
                entry.worldName?.toLowerCase().includes(map));
        const stabilityFrames = frames.filter((entry) =>
            entry.observedMs >= stability.startedMs &&
            entry.observedMs <= stability.endedMs);
        const measuredIntervals = stabilityFrames.slice(1).map((entry, index) =>
            entry.observedMs - stabilityFrames[index].observedMs)
            .filter((value) => value >= 0);
        const intervals = stability.performanceWindowValid ? measuredIntervals : [];
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
        const stabilityViews = globalThis.__retailValidationViews.filter((entry) =>
            entry.observedMs >= stability.startedMs &&
            entry.observedMs <= stability.endedMs &&
            entry.worldName?.toLowerCase().includes(map));
        const rawProfiles = globalThis.__retailFrameProfiles.slice(cursor.profiles)
            .filter((entry) => entry.observedMs >= stability.startedMs &&
                entry.observedMs <= stability.endedMs);
        const frameProfiles = stability.performanceWindowValid
            ? rawProfiles.filter((entry) => entry.kind === "frame" &&
                entry.gameplayFrame === true &&
                entry.viewSubmissionGeneration >= stability.firstGeneration &&
                entry.viewSubmissionGeneration <= stability.finalGeneration)
            : [];
        const framePumpTicks = new Set(frameProfiles.map(({ pumpTick }) => pumpTick));
        const gpuProfiles = rawProfiles.filter((entry) =>
            entry.kind === "gpu-result" && framePumpTicks.has(entry.pumpTick));
        const summarize = (values) => {
            const finite = values.filter(Number.isFinite)
                .sort((left, right) => left - right);
            if (!finite.length) return null;
            const at = (fraction) => finite[Math.min(finite.length - 1,
                Math.ceil(finite.length * fraction) - 1)];
            return {
                samples: finite.length,
                average: finite.reduce((sum, value) => sum + value, 0) /
                    finite.length,
                p50: at(0.50),
                p95: at(0.95),
                p99: at(0.99),
                maximum: finite.at(-1),
            };
        };
        const summarizeFields = (container, fields) => Object.fromEntries(
            fields.map((field) => [field,
                summarize(frameProfiles.map((entry) => entry[container]?.[field]))]));
        const gpuStatusCounts = Object.fromEntries(gpuProfiles.reduce((counts, entry) => {
            const status = entry.gpu?.status ?? "unknown";
            counts.set(status, (counts.get(status) ?? 0) + 1);
            return counts;
        }, new Map()));
        const frameProfile = {
            requestedSamples: frameProfileSampleLimit,
            capturedSamples: frameProfiles.length,
            cpu: summarizeFields("cpu", [
                "filesystemMs", "commandMs", "serverMs", "clientOnceMs",
                "commandBufferMs", "clientFrameMs", "cgameFrameMs",
                "sceneBuildMs", "rendererFrontendMs", "soundMs",
                "rendererBackendMs", "totalMs",
            ]),
            renderer: summarizeFields("renderer", [
                "setupMs", "lodMs", "sunShadowPrepareMs", "sunShadowDrawMs",
                "spotShadowPrepareMs", "spotShadowDrawMs", "skyMs", "worldMs",
                "staticModelsMs", "dynamicModelsMs", "fxModelsMs",
                "particlesMs", "marksMs", "uiMs", "postProcessMs",
                "bufferUploadMs", "textureUploadMs",
            ]),
            counters: summarizeFields("counters", [
                "worldSurfacesSubmitted", "worldSurfacesDrawn",
                "staticModelInstancesRetained", "staticModelInstanceDraws",
                "dynamicBatchesDrawn", "fxModelBatchesDrawn",
                "particleBatchesDrawn", "markBatchesDrawn", "worldDrawCalls",
                "staticModelDrawCalls", "dynamicDrawCalls", "fxDrawCalls",
                "shadowDrawCalls", "uiDrawCalls", "postProcessDrawCalls",
                "queryDrawCalls", "resolveBlits", "submittedIndices",
                "submittedTriangles", "textureBindCalls", "programSwitches",
                "bufferUploadBytes", "textureUploadBytes",
                "unmeasuredTextureUploads", "lodChanges", "shadowCasterDraws",
            ]),
            gpu: {
                timingsAvailable: frameProfiles.some(
                    (entry) => entry.gpu?.timingsAvailable === true),
                queriesIssued: frameProfiles.filter(
                    (entry) => entry.gpu?.queryIssued === true).length,
                queriesDropped: frameProfiles.filter(
                    (entry) => entry.gpu?.queryDropped === true).length,
                results: gpuProfiles.length,
                statusCounts: gpuStatusCounts,
                backendDrawMs: summarize(gpuProfiles
                    .filter((entry) => entry.gpu?.status === "valid")
                    .map((entry) => entry.gpu.backendDrawMs)),
                queryLagFrames: summarize(gpuProfiles
                    .map((entry) => entry.gpu?.queryLagFrames)),
            },
        };
        const gameTimeAdvancementMs = stability.performanceWindowValid &&
            stabilityViews.length > 1
            ? stabilityViews.at(-1).time - stabilityViews[0].time : null;
        const wallTimeAdvancementMs = stability.performanceWindowValid
            ? stability.endedMs - stability.startedMs : null;
        const averageFrameIntervalMs = intervals.length
            ? intervals.reduce((sum, value) => sum + value, 0) / intervals.length
            : null;
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
                stability60s: stability.requestedDurationMs === 60_000 &&
                    stability.performanceWindowValid
                    ? stabilityFrames.length >= 60
                    : false,
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
            pageVisibilityState: stability.pageVisibilityState,
            pageFocused: stability.pageFocused,
            backgroundTransitions: stability.backgroundTransitions,
            foregroundStateTransitions: stability.foregroundStateTransitions,
            performanceWindowValid: stability.performanceWindowValid,
            performanceInvalidReason: stability.performanceInvalidReason,
            actualWorldFrames: stability.performanceWindowValid
                ? stabilityFrames.length : null,
            averageFrameIntervalMs,
            averageFrameTimeMs: averageFrameIntervalMs,
            p50FrameTimeMs: percentile(0.50),
            p95FrameTimeMs: percentile(0.95),
            p99FrameTimeMs: percentile(0.99),
            minimumFpsEquivalent: sorted.length
                ? 1_000 / sorted.at(-1) : null,
            averageFpsEquivalent: averageFrameIntervalMs
                ? 1_000 / averageFrameIntervalMs : null,
            gameTimeAdvancementMs,
            wallTimeAdvancementMs,
            gameTimeWallTimeRatio: wallTimeAdvancementMs
                ? gameTimeAdvancementMs / wallTimeAdvancementMs : null,
            framesRenderedDuringStabilityWindow: stabilityFrames.length,
            stability,
            frameProfile,
            wasmLinearMemoryCapacityBytes: {
                beforeMapLoad:
                    memoryLifecycle.beforeMapLoad.wasmLinearMemoryCapacityBytes,
                afterDatabaseCompletion:
                    memoryLifecycle.afterDatabaseCompletion
                        .wasmLinearMemoryCapacityBytes,
                afterCGameInit:
                    memoryLifecycle.afterCGameInit.wasmLinearMemoryCapacityBytes,
                afterWorldPublication:
                    memoryLifecycle.afterFirstWorldFrame
                        .wasmLinearMemoryCapacityBytes,
                atStabilityEnd:
                    memoryLifecycle.steadyState.wasmLinearMemoryCapacityBytes,
            },
            memoryLifecycle,
            decodedTextureRecoveryBytes: memorySnapshot.decodedTextureSourceBytes,
            textureRecoverySourceBytes:
                memorySnapshot.textureRecoverySourceBytes,
            encodedImageRecoveryBytes: memorySnapshot.encodedImageRecoveryBytes,
            aggregateCpuRecoveryBytes: memorySnapshot.recoveryCopyBytes,
            estimatedGpuTextureBytes: memorySnapshot.gpuTextureEstimateBytes,
            geometryBytes: memorySnapshot.geometryBytes,
            temporaryUploadBytes: memorySnapshot.temporaryUploadBytes,
            shaderProgramBytes: memorySnapshot.shaderProgramCacheEstimateBytes,
            webglRendererIdentity: memorySnapshot.webglRendererIdentity,
            imagePoolRecoveryBytes: {
                world: memorySnapshot.worldImageRecoveryBytes,
                staticModels: memorySnapshot.staticModelImageRecoveryBytes,
                dynamicModels: memorySnapshot.dynamicModelImageRecoveryBytes,
                ui: memorySnapshot.uiImageRecoveryBytes,
                perPoolAdmissionLimit:
                    memorySnapshot.decodedTextureAdmissionBudgetBytes,
            },
            imagePoolDecodedBytes: {
                world: memorySnapshot.worldImageDecodedBytes,
                staticModels: memorySnapshot.staticModelImageDecodedBytes,
                dynamicModels: memorySnapshot.dynamicModelImageDecodedBytes,
                ui: memorySnapshot.uiImageDecodedBytes,
            },
            imageRecoverySources: memorySnapshot.imageRecoverySources,
            supplementalTextureRecoveryBytes:
                memorySnapshot.supplementalTextureRecoveryBytes,
            audioDecodedBytes: audioSnapshot?.decodedPcmBytes ?? null,
            audioQueuedBuffers: audioSnapshot?.queuedBufferCount ?? null,
            input,
            checkpoint: checkpointResult,
        };
    }, { map, cursor, commandTimeMs, memoryLifecycle, stability,
        audioSnapshot, input, checkpointResult, frameProfileSampleLimit });
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
        .toBe(sample.textureRecoverySourceBytes);
    const imagePoolDecodedBytes = [
        sample.worldImageDecodedBytes,
        sample.staticModelImageDecodedBytes,
        sample.dynamicModelImageDecodedBytes,
        sample.uiImageDecodedBytes,
    ];
    expect(imagePoolDecodedBytes.reduce((sum, bytes) => sum + bytes, 0) +
        sample.supplementalTextureRecoveryBytes)
        .toBe(sample.decodedTextureSourceBytes);
    expect(sample.textureRecoverySourceBytes + sample.geometryBytes +
        sample.shaderProgramCacheEstimateBytes)
        .toBe(sample.recoveryCopyBytes);
    const recoverySources = Object.values(sample.imageRecoverySources);
    expect(recoverySources.reduce((sum, source) =>
        sum + source.recoveryBytes, 0)).toBe(
        imagePoolBytes.reduce((sum, bytes) => sum + bytes, 0));
    expect(recoverySources.reduce((sum, source) =>
        sum + source.decodedBytes, 0)).toBe(
        imagePoolDecodedBytes.reduce((sum, bytes) => sum + bytes, 0));
    expect(sample.imageRecoverySources.loadDef.recoveryBytes +
        sample.imageRecoverySources.iwiMember.recoveryBytes)
        .toBe(sample.encodedImageRecoveryBytes);
    expect(sample.recoveryBudgetBytes)
        .toBe(sample.decodedTextureAdmissionBudgetBytes);
    expect(sample.gpuTextureEstimateBytes)
        .toBeGreaterThanOrEqual(sample.decodedTextureSourceBytes);
    for (const bytes of imagePoolDecodedBytes) {
        expect(bytes).toBeLessThanOrEqual(
            sample.decodedTextureAdmissionBudgetBytes);
    }
    expect(sample.wasmProgramBreakOffsetBytes).toBeGreaterThan(0);
    expect(sample.wasmProgramBreakOffsetBytes)
        .toBeLessThanOrEqual(sample.wasmLinearMemoryCapacityBytes);
    expect(sample.wasmLinearMemoryCapacityBytes)
        .toBeLessThanOrEqual(sample.wasmLinearMemoryMaximumBytes);
    if (sample.wasmAllocatorStatsSampled) {
        expect(sample.wasmAllocatorInUseBytes + sample.wasmAllocatorFreeBytes)
            .toBe(sample.wasmAllocatorFootprintBytes);
        expect(sample.wasmAllocatorTopFreeBytes)
            .toBeLessThanOrEqual(sample.wasmAllocatorFreeBytes);
    } else {
        expect(sample.state).toBe("ui-submitted");
    }
    expect(sample.imageLoadDefCacheEncodedPayloadBytes)
        .toBeLessThanOrEqual(sample.imageLoadDefCacheBudgetBytes);
}

function assertMapEvidence(evidence, memorySnapshot)
{
    assertMemoryTelemetry(memorySnapshot);
    for (const sample of Object.values(evidence.memoryLifecycle)) {
        assertMemoryTelemetry(sample);
        expect(sample.wasmAllocatorStatsSampled).toBe(true);
    }
    for (const [check, passed] of Object.entries(evidence.checks)) {
        if (check === "stability60s" && stabilityDurationMs < 60_000) {
            expect(passed).toBe(false);
        } else {
            expect(passed).toBe(true);
        }
    }
    expect(evidence.databaseStartTimeMs).not.toBeNull();
    expect(evidence.databaseCompletionTimeMs).not.toBeNull();
    expect(evidence.databaseCompletionTimeMs)
        .toBeGreaterThanOrEqual(evidence.databaseStartTimeMs);
    expect(evidence.cgameInitTimeMs).not.toBeNull();
    expect(evidence.firstRealWorldFrameTimeMs).not.toBeNull();
    expect(evidence.framesRenderedDuringStabilityWindow).toBeGreaterThanOrEqual(
        evidence.performanceWindowValid ? 60 : 1);
    expect(evidence.webglRendererIdentity).toEqual(expect.objectContaining({
        vendor: expect.any(String),
        renderer: expect.any(String),
        version: expect.any(String),
    }));
    if (evidence.performanceWindowValid) {
        expect(evidence.averageFrameTimeMs).not.toBeNull();
        expect(evidence.p50FrameTimeMs).not.toBeNull();
        expect(evidence.p95FrameTimeMs).not.toBeNull();
        expect(evidence.p99FrameTimeMs).not.toBeNull();
        expect(evidence.gameTimeWallTimeRatio).not.toBeNull();
        expect(evidence.frameProfile.capturedSamples).toBeGreaterThan(0);
        expect(evidence.frameProfile.cpu.totalMs).not.toBeNull();
        expect(evidence.frameProfile.renderer.worldMs).not.toBeNull();
        expect(evidence.frameProfile.counters.worldDrawCalls).not.toBeNull();
        expect(evidence.frameProfile.counters.worldDrawCalls.maximum)
            .toBeGreaterThan(0);
        expect(evidence.frameProfile.counters.worldSurfacesDrawn.maximum)
            .toBeGreaterThan(0);
    } else {
        expect(evidence.performanceInvalidReason)
            .toBe("INVALID_BACKGROUND_THROTTLED");
        expect(evidence.averageFrameTimeMs).toBeNull();
        expect(evidence.p50FrameTimeMs).toBeNull();
        expect(evidence.p95FrameTimeMs).toBeNull();
        expect(evidence.p99FrameTimeMs).toBeNull();
        expect(evidence.gameTimeWallTimeRatio).toBeNull();
    }
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
    await expect.poll(() => page.evaluate(({ cursor, name }) =>
        globalThis.__retailValidationFrames.slice(cursor).some(
            (entry) => entry.state === "drawn" && entry.geometrySubmitted === true &&
                entry.worldName?.toLowerCase().includes(name)), {
        cursor: frameCursor,
        name: mapName,
    }), { timeout: 300_000 }).toBe(true);
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
    expect(unloadEnd.imageLoadDefCacheEntryCount)
        .toBe(unloadBegin.imageLoadDefCacheEntryCount);
    expect(unloadEnd.imageLoadDefCacheEncodedPayloadBytes)
        .toBe(unloadBegin.imageLoadDefCacheEncodedPayloadBytes);
    expect(unloadEnd.imageLoadDefCacheEvictionCount)
        .toBe(unloadBegin.imageLoadDefCacheEvictionCount);
    expect(lifecycle[unloadEndIndex].oldMapBytesReleased)
        .toBe(unloadBegin.recoveryCopyBytes - unloadEnd.recoveryCopyBytes);
    const maximum = (key) => Math.max(...memory.map((entry) => entry[key]));
    const sampledAllocator = memory.filter(
        (entry) => entry.wasmAllocatorStatsSampled);
    const maximumSampled = (key) => Math.max(...sampledAllocator.map(
        (entry) => entry[key]));
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
            textureRecoverySourceBytes: maximum("textureRecoverySourceBytes"),
            encodedImageRecoveryBytes: maximum("encodedImageRecoveryBytes"),
            aggregateCpuRecoveryBytes: maximum("recoveryCopyBytes"),
            estimatedGpuTextureBytes: maximum("gpuTextureEstimateBytes"),
            geometryBytes: maximum("geometryBytes"),
            boundaryRetainedTemporaryUploadBytes: maximum("temporaryUploadBytes"),
            shaderProgramBytes: maximum("shaderProgramCacheEstimateBytes"),
            wasmProgramBreakOffsetBytes: maximum("wasmProgramBreakOffsetBytes"),
            wasmLinearMemoryCapacityBytes:
                maximum("wasmLinearMemoryCapacityBytes"),
            wasmAllocatorInUseBytes:
                maximumSampled("wasmAllocatorInUseBytes"),
            wasmAllocatorFootprintBytes:
                maximumSampled("wasmAllocatorFootprintBytes"),
            imageLoadDefCacheEntryCount:
                maximum("imageLoadDefCacheEntryCount"),
            imageLoadDefCacheEncodedPayloadBytes:
                maximum("imageLoadDefCacheEncodedPayloadBytes"),
            imageLoadDefCacheEvictionCount:
                maximum("imageLoadDefCacheEvictionCount"),
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
        memoryCursor: globalThis.__retailValidationMemory.length,
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
    const contextMemory = await page.evaluate((cursor) => structuredClone(
        globalThis.__retailValidationMemory.slice(cursor).filter((entry) =>
            entry.state === "context-lost" || entry.state === "context-restored")),
    before.memoryCursor);
    const contextLostMemory = contextMemory.find(
        (entry) => entry.state === "context-lost");
    const contextRestoredMemory = contextMemory.find(
        (entry) => entry.state === "context-restored");
    expect(contextLostMemory).toBeTruthy();
    expect(contextRestoredMemory).toBeTruthy();
    assertMemoryTelemetry(contextLostMemory);
    assertMemoryTelemetry(contextRestoredMemory);
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
        memory: {
            contextLost: contextLostMemory,
            contextRestored: contextRestoredMemory,
            allocatorInUseDeltaBytes:
                contextRestoredMemory.wasmAllocatorInUseBytes -
                    contextLostMemory.wasmAllocatorInUseBytes,
            linearMemoryCapacityDeltaBytes:
                contextRestoredMemory.wasmLinearMemoryCapacityBytes -
                    contextLostMemory.wasmLinearMemoryCapacityBytes,
        },
    };
}

async function gameplayState(page, field, weaponIndex = 0)
{
    return page.evaluate(
        ({ stateField, weapon }) => globalThis.__KISAKCOD_WEB__.module.call(
            "_KisakWeb_TestGameplayState", stateField, weapon),
        { stateField: field, weapon: weaponIndex });
}

async function waitForRelativeMouseMode(page)
{
    await expect.poll(() => page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.input.absoluteMouse)).toBe(false);
}

async function provisionGameplayInventory(page)
{
    if (await gameplayState(page, 7) !== 0) return null;
    await submitCommand(page, "give all");
    await expect.poll(() => gameplayState(page, 7), { timeout: 15_000 })
        .toBeGreaterThan(0);
    return "canonical give all";
}

async function waitForWeaponReady(page)
{
    await expect.poll(async () => ({
        keyCatchers: await gameplayState(page, 12),
        respawnedOrFrozen: (await gameplayState(page, 10) & 0xC00) !== 0,
        turret: (await gameplayState(page, 9) & 0x300) !== 0,
        scriptDisabled: (await gameplayState(page, 11) & 0x80) !== 0,
    }), { timeout: 90_000 }).toEqual({
        keyCatchers: 0,
        respawnedOrFrozen: false,
        turret: false,
        scriptDisabled: false,
    });
    for (const movementX of [320, 320, 320, 320]) {
        if ((await gameplayState(page, 11) & 8) === 0) break;
        await page.evaluate((dx) => {
            const movement = new MouseEvent("mousemove");
            Object.defineProperties(movement, {
                movementX: { value: dx },
                movementY: { value: -25 },
            });
            globalThis.dispatchEvent(movement);
        }, movementX);
        await page.waitForTimeout(100);
    }
    await expect.poll(async () => ({
        weaponTime: await gameplayState(page, 13),
        weaponDelay: await gameplayState(page, 14),
        weaponState: await gameplayState(page, 15),
        weaponDisabled: (await gameplayState(page, 11) & 0x88) !== 0,
    }), { timeout: 30_000 }).toEqual({
        weaponTime: 0,
        weaponDelay: 0,
        weaponState: 0,
        weaponDisabled: false,
    });
}

async function exerciseTransitionInput(page)
{
    await page.bringToFront();
    await expect.poll(() => page.evaluate(() => ({
        visibility: document.visibilityState,
        focused: document.hasFocus(),
    }))).toEqual({ visibility: "visible", focused: true });
    let inventoryProvisioning = await provisionGameplayInventory(page);
    const canvas = page.locator("#game-canvas");
    if ((await gameplayState(page, 12) & 0x10) !== 0) {
        await canvas.focus();
        await page.keyboard.press("Escape");
        await expect.poll(async () => await gameplayState(page, 12) & 0x10)
            .toBe(0);
        await waitForRelativeMouseMode(page);
    }
    await canvas.click({ position: { x: 8, y: 8 } });
    await expect.poll(() => page.evaluate(() => document.pointerLockElement?.id))
        .toBe("game-canvas");

    const origin = await page.evaluate(() => structuredClone(
        globalThis.__retailValidationViews.at(-1)?.viewOrigin));
    await page.keyboard.down("w");
    try {
        await expect.poll(() => page.evaluate((previous) => {
            const current = globalThis.__retailValidationViews.at(-1)?.viewOrigin;
            return current ? Math.hypot(...current.map(
                (value, index) => value - previous[index])) : 0;
        }, origin), { timeout: 30_000 }).toBeGreaterThan(1);
    } finally {
        await page.keyboard.up("w");
    }

    if (await gameplayState(page, 4) <= 0) {
        await expect.poll(() => gameplayState(page, 8), { timeout: 60_000 })
            .toBe(1);
        await page.mouse.wheel(0, -120);
    }
    await expect.poll(() => gameplayState(page, 4), { timeout: 30_000 })
        .toBeGreaterThan(0);
    await waitForWeaponReady(page);
    let weapon = await gameplayState(page, 4);
    if (await gameplayState(page, 6, weapon) <= 0) {
        await submitCommand(page, "give all");
        inventoryProvisioning = "canonical give all";
        await expect.poll(async () => {
            weapon = await gameplayState(page, 4);
            return gameplayState(page, 6, weapon);
        }, { timeout: 15_000 }).toBeGreaterThan(0);
        await waitForWeaponReady(page);
        weapon = await gameplayState(page, 4);
    }
    const clipBefore = await gameplayState(page, 6, weapon);
    expect(clipBefore).toBeGreaterThan(0);
    const shotCountBefore = await gameplayState(page, 16);
    await page.mouse.down({ button: "left" });
    try {
        await expect.poll(() => gameplayState(page, 6, weapon), { timeout: 15_000 })
            .toBeLessThan(clipBefore);
    } finally {
        await page.mouse.up({ button: "left" });
    }

    const viewForward = await page.evaluate(() => structuredClone(
        globalThis.__retailValidationViews.at(-1)?.viewForward));
    await page.evaluate(() => {
        const movement = new MouseEvent("mousemove");
        Object.defineProperties(movement, {
            movementX: { value: 7 },
            movementY: { value: -3 },
        });
        globalThis.dispatchEvent(movement);
    });
    await expect.poll(() => page.evaluate((previous) => {
        const current = globalThis.__retailValidationViews.at(-1)?.viewForward;
        return current ? Math.hypot(...current.map(
            (value, index) => value - previous[index])) : 0;
    }, viewForward)).toBeGreaterThan(0.0001);
    return {
        validationResult: "pass",
        inventoryProvisioning,
        movement: true,
        primaryFireCanonicalResponse: {
            weapon,
            clipBefore,
            clipAfter: await gameplayState(page, 6, weapon),
            shotCountBefore,
            shotCountAfter: await gameplayState(page, 16),
        },
        mouseLook: true,
        pointerLock: true,
    };
}

async function exerciseRetailInput(page)
{
    const canvas = page.locator("#game-canvas");
    const beforeMove = await page.evaluate(() => structuredClone(
        globalThis.__retailValidationViews.at(-1)?.viewOrigin));
    expect(beforeMove).toHaveLength(3);
    const inventoryProvisioning = await provisionGameplayInventory(page);
    if ((await gameplayState(page, 12) & 0x10) !== 0) {
        await canvas.focus();
        await page.keyboard.press("Escape");
        await expect.poll(async () => await gameplayState(page, 12) & 0x10)
            .toBe(0);
        await waitForRelativeMouseMode(page);
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
        globalThis.__retailAudioPlaybackCount);
    await expect.poll(() => page.evaluate(() => document.pointerLockElement?.id))
        .toBe("game-canvas");
    const primaryWeaponCount = await gameplayState(page, 7);
    const selectedWeaponBefore = await gameplayState(page, 2);
    let wheelCanonicalResponse;
    if (primaryWeaponCount <= 1) {
        await page.mouse.wheel(0, -120);
        wheelCanonicalResponse = {
            validationResult: "NOT_APPLICABLE_SINGLE_WEAPON",
            primaryWeaponCount,
            selectedWeaponBefore,
            selectedWeaponAfter: selectedWeaponBefore,
        };
    } else {
        await expect.poll(() => gameplayState(page, 8), { timeout: 60_000 })
            .toBe(1);
        await page.mouse.wheel(0, -120);
        await expect.poll(() => gameplayState(page, 2), { timeout: 15_000 })
            .not.toBe(selectedWeaponBefore);
        const selectedWeaponAfter = await gameplayState(page, 2);
        await expect.poll(() => gameplayState(page, 4), { timeout: 15_000 })
            .toBe(selectedWeaponAfter);
        wheelCanonicalResponse = {
            validationResult: "pass",
            primaryWeaponCount,
            selectionAllowed: true,
            selectedWeaponBefore,
            selectedWeaponAfter,
        };
    }
    const snapshotWeaponBefore = await gameplayState(page, 0);
    const selectedWeaponBeforeFire = await gameplayState(page, 2);
    const predictedWeaponBefore = await gameplayState(page, 4);
    const viewmodelWeaponBefore = await gameplayState(page, 5);
    const fireWeapon = [predictedWeaponBefore, viewmodelWeaponBefore,
        snapshotWeaponBefore, selectedWeaponBeforeFire]
        .find((weapon) => weapon > 0) ?? 0;
    const snapshotClipBefore = await gameplayState(page, 1, fireWeapon);
    const clipBefore = await gameplayState(page, 6, fireWeapon);
    console.log(`KISAK_RETAIL_GAMEPLAY_STATE ${JSON.stringify({
        snapshotWeaponBefore,
        selectedWeaponBeforeFire,
        predictedWeaponBefore,
        viewmodelWeaponBefore,
        fireWeapon,
        snapshotClipBefore,
        predictedClipBefore: clipBefore,
        snapshotPrimaryWeaponCount: await gameplayState(page, 3),
        predictedPrimaryWeaponCount: primaryWeaponCount,
    })}`);
    expect(fireWeapon).toBeGreaterThan(0);
    expect(clipBefore).toBeGreaterThan(0);
    await waitForWeaponReady(page);
    const shotCountBefore = await gameplayState(page, 16);
    await page.mouse.down({ button: "left" });
    try {
        await expect.poll(() => gameplayState(page, 6, fireWeapon), {
            timeout: 15_000,
        })
            .toBeLessThan(clipBefore);
    } finally {
        await page.mouse.up({ button: "left" });
    }
    const clipAfter = await gameplayState(page, 6, fireWeapon);
    const snapshotClipAfter = await gameplayState(page, 1, fireWeapon);
    const primaryFireCanonicalResponse = {
        validationResult: "pass",
        inventoryProvisioning,
        weapon: fireWeapon,
        snapshotWeaponBefore,
        selectedWeaponBefore: selectedWeaponBeforeFire,
        predictedWeaponBefore,
        viewmodelWeaponBefore,
        snapshotClipBefore,
        snapshotClipAfter,
        clipBefore,
        clipAfter,
        shotCountBefore,
        shotCountAfter: await gameplayState(page, 16),
    };
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
        globalThis.__retailAudioPlaybackCount), { timeout: 30_000 })
        .toBeGreaterThan(audioBefore);

    await expect.poll(async () => await gameplayState(page, 12) & 0x10)
        .toBe(0x10);
    const menuKeyCatchers = await gameplayState(page, 12);
    await page.waitForTimeout(150);
    await canvas.focus();
    await page.keyboard.press("Escape");
    await expect.poll(async () => await gameplayState(page, 12) & 0x10)
        .toBe(0);
    await waitForRelativeMouseMode(page);
    const inputs = await page.evaluate(() => structuredClone(
        globalThis.__retailValidationInput));
    for (const key of [0x77, 0x73, 0x61, 0x64, 0x20, 0x1B, 0xC8, 0xC9, 0xCD, 0xCE]) {
        expect(inputs).toContainEqual({ type: "key", key, down: true });
        expect(inputs).toContainEqual({ type: "key", key, down: false });
    }
    await canvas.click({ position: { x: 8, y: 8 } });
    await expect.poll(() => page.evaluate(() => document.pointerLockElement?.id))
        .toBe("game-canvas");
    return {
        eventCount: inputs.length,
        movement: true,
        mouseLook: true,
        primaryFireCanonicalResponse,
        primaryFireAudioSecondaryEvidence: true,
        secondaryAction: true,
        wheelCanonicalResponse,
        escapeMenu: {
            validationResult: "pass",
            openedKeyCatchers: menuKeyCatchers,
            closedKeyCatchers: await gameplayState(page, 12),
        },
        pointerLockLoss: true,
        pointerLockReacquisition: true,
    };
}

test("local retail validation matrix", { tag: "@retail" }, async ({ retailPage: page }) => {
    test.setTimeout(1_800_000);
    if (!allowDirty) expect(sourceDirty,
        "authoritative retail validation requires a clean source commit").toBe(false);
    const browser = page.context().browser();
    retailBrowserMetadata = {
        name: browserChannel ?? browser?.browserType().name() ?? "unknown",
        version: browser?.version() ?? "unknown",
        channel: browserChannel ?? null,
        headless: Boolean(test.info().project.use.headless ?? true),
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
    const killhouseMemoryBefore = await rendererMemorySnapshot(page);
    const killhouseCommandMs = await submitCommand(page, "map killhouse");
    await waitForDatabaseCompletion(page, "killhouse", killhouseCursor.database);
    const killhouseMemoryAfterDatabase = await rendererMemorySnapshot(page);
    failureStage = "Killhouse CGame initialization";
    failureClass = "cgame";
    await expect.poll(() => page.evaluate((start) =>
        globalThis.__retailLifecycle.slice(start).some(
            (event) => event.stage === "CG_Init complete"), killhouseCursor.lifecycle), {
        timeout: 300_000,
    }).toBe(true);
    const killhouseMemoryAfterCGame = await rendererMemorySnapshot(page);
    await waitForWorldFrames(page, "killhouse", 120);
    const killhouseMemoryAfterWorld = await rendererMemorySnapshot(page);
    const killhouseStability = await sustainWorldFrames(
        page, "killhouse", stabilityDurationMs);
    const killhouseMemory = await rendererMemorySnapshot(page);
    const killhouseMemoryLifecycle = {
        beforeMapLoad: killhouseMemoryBefore,
        afterDatabaseCompletion: killhouseMemoryAfterDatabase,
        afterCGameInit: killhouseMemoryAfterCGame,
        afterFirstWorldFrame: killhouseMemoryAfterWorld,
        steadyState: killhouseMemory,
    };
    const killhouseAudio = await page.evaluate(() => structuredClone(
        globalThis.__retailAudioTelemetry.at(-1) ?? null));

    failureStage = "Killhouse gameplay validation";
    const killhouseInput = await exerciseRetailInput(page);
    failureStage = "Killhouse checkpoint";
    failureClass = "lifecycle";
    const killhouseCheckpoint = await writeConfigAndCheckpoint(page);
    const killhouseEvidence = await mapEvidence(page, "killhouse", killhouseCursor,
        killhouseCommandMs, killhouseMemoryLifecycle, killhouseStability,
        killhouseAudio, killhouseInput, killhouseCheckpoint);
    assertMapEvidence(killhouseEvidence, killhouseMemory);
    failureStage = "Killhouse to CargoShip transition";
    failureClass = "lifecycle";
    const transitionStart = await page.evaluate(() =>
        globalThis.__retailRendererLifecycle.length);
    const cargoshipCursor = await captureMapCursor(page);
    const cargoshipMemoryBefore = await rendererMemorySnapshot(page);
    const cargoshipCommandMs = await submitCommand(page, "map cargoship");
    failureStage = "CargoShip database load";
    failureClass = "database";
    await waitForDatabaseCompletion(page, "cargoship", cargoshipCursor.database);
    const cargoshipMemoryAfterDatabase = await rendererMemorySnapshot(page);
    failureStage = "CargoShip CGame initialization";
    failureClass = "cgame";
    await expect.poll(() => page.evaluate((start) =>
        globalThis.__retailLifecycle.slice(start).some(
            (event) => event.stage === "CG_Init complete"), cargoshipCursor.lifecycle), {
        timeout: 300_000,
    }).toBe(true);
    const cargoshipMemoryAfterCGame = await rendererMemorySnapshot(page);
    await waitForWorldFrames(page, "cargoship", 120);
    const cargoshipMemoryAfterWorld = await rendererMemorySnapshot(page);
    const cargoshipStability = await sustainWorldFrames(
        page, "cargoship", stabilityDurationMs);
    failureStage = "Killhouse to CargoShip critical input";
    failureClass = "cgame";
    const cargoshipTransitionInput = await exerciseTransitionInput(page);
    const cargoshipMemory = await rendererMemorySnapshot(page);
    const cargoshipMemoryLifecycle = {
        beforeMapLoad: cargoshipMemoryBefore,
        afterDatabaseCompletion: cargoshipMemoryAfterDatabase,
        afterCGameInit: cargoshipMemoryAfterCGame,
        afterFirstWorldFrame: cargoshipMemoryAfterWorld,
        steadyState: cargoshipMemory,
    };
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
    const cargoshipContextRecovery = await recoverMapContext(page, "cargoship");

    const cargoshipEvidence = await mapEvidence(page, "cargoship", cargoshipCursor,
        cargoshipCommandMs, cargoshipMemoryLifecycle, cargoshipStability,
        cargoshipAudio, cargoshipInput, cargoshipCheckpoint);
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
    expect(unloadEndMemory.imageLoadDefCacheEntryCount)
        .toBe(unloadBeginMemory.imageLoadDefCacheEntryCount);
    expect(unloadEndMemory.imageLoadDefCacheEncodedPayloadBytes)
        .toBe(unloadBeginMemory.imageLoadDefCacheEncodedPayloadBytes);
    expect(unloadEndMemory.imageLoadDefCacheEvictionCount)
        .toBe(unloadBeginMemory.imageLoadDefCacheEvictionCount);
    expect(transition[unloadEndIndex].oldMapBytesReleased)
        .toBe(unloadBeginMemory.recoveryCopyBytes - unloadEndMemory.recoveryCopyBytes);
    const maximum = (key) => Math.max(...transitionMemory.map(
        (entry) => entry[key]));
    const sampledAllocator = transitionMemory.filter(
        (entry) => entry.wasmAllocatorStatsSampled);
    const maximumSampled = (key) => Math.max(...sampledAllocator.map(
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
            textureRecoverySourceBytes: maximum("textureRecoverySourceBytes"),
            encodedImageRecoveryBytes: maximum("encodedImageRecoveryBytes"),
            aggregateCpuRecoveryBytes: maximum("recoveryCopyBytes"),
            estimatedGpuTextureBytes: maximum("gpuTextureEstimateBytes"),
            geometryBytes: maximum("geometryBytes"),
            boundaryRetainedTemporaryUploadBytes: maximum("temporaryUploadBytes"),
            shaderProgramBytes: maximum("shaderProgramCacheEstimateBytes"),
            wasmProgramBreakOffsetBytes: maximum("wasmProgramBreakOffsetBytes"),
            wasmAllocatorInUseBytes:
                maximumSampled("wasmAllocatorInUseBytes"),
            wasmAllocatorFootprintBytes:
                maximumSampled("wasmAllocatorFootprintBytes"),
            imageLoadDefCacheEntryCount:
                maximum("imageLoadDefCacheEntryCount"),
            imageLoadDefCacheEncodedPayloadBytes:
                maximum("imageLoadDefCacheEncodedPayloadBytes"),
            imageLoadDefCacheEvictionCount:
                maximum("imageLoadDefCacheEvictionCount"),
            imagePoolRecoveryBytes: {
                world: maximum("worldImageRecoveryBytes"),
                staticModels: maximum("staticModelImageRecoveryBytes"),
                dynamicModels: maximum("dynamicModelImageRecoveryBytes"),
                ui: maximum("uiImageRecoveryBytes"),
                perPoolAdmissionLimit:
                    unloadBeginMemory.decodedTextureAdmissionBudgetBytes,
            },
            wasmLinearMemoryCapacityBytes: Math.max(
                cargoshipEvidence.wasmLinearMemoryCapacityBytes.beforeMapLoad,
                cargoshipEvidence.wasmLinearMemoryCapacityBytes
                    .afterDatabaseCompletion,
                cargoshipEvidence.wasmLinearMemoryCapacityBytes.afterCGameInit,
                cargoshipEvidence.wasmLinearMemoryCapacityBytes.afterWorldPublication),
        },
        lifecycleOrder: ["worldUnloadBegin", "worldUnloadEnd", "newWorldPublished"],
    };

    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.state))
        .toBe("running");

    failureStage = "CargoShip to Blackout transition";
    failureClass = "lifecycle";
    const cargoshipToBlackoutCursor = await page.evaluate(() =>
        globalThis.__retailRendererLifecycle.length);
    const blackoutCursor = await captureMapCursor(page);
    const blackoutMemoryBefore = await rendererMemorySnapshot(page);
    const blackoutCommandMs = await submitCommand(page, "map blackout");
    failureStage = "Blackout database load";
    failureClass = "database";
    await waitForDatabaseCompletion(page, "blackout", blackoutCursor.database);
    const blackoutMemoryAfterDatabase = await rendererMemorySnapshot(page);
    failureStage = "Blackout canonical lifecycle";
    failureClass = "cgame";
    await waitForLifecycleStages(
        page, blackoutCursor.lifecycle, canonicalMapLifecycleStages);
    const blackoutMemoryAfterCGame = await rendererMemorySnapshot(page);
    failureStage = "Blackout first world frame";
    failureClass = "renderer";
    const blackoutFrame = await firstWorldFrameEvidence(
        page, "blackout", blackoutCursor.frames);
    const blackoutMemoryAfterWorld = await rendererMemorySnapshot(page);
    const cargoshipToBlackout = await rendererTransitionEvidence(
        page, cargoshipToBlackoutCursor, blackoutCommandMs,
        blackoutFrame.observedMs);
    failureStage = "Blackout foreground stability";
    failureClass = "renderer";
    const blackoutStability = await sustainWorldFrames(
        page, "blackout", stabilityDurationMs);
    failureStage = "CargoShip to Blackout critical input";
    failureClass = "cgame";
    const blackoutTransitionInput = await exerciseTransitionInput(page);
    const blackoutMemory = await rendererMemorySnapshot(page);
    const blackoutMemoryLifecycle = {
        beforeMapLoad: blackoutMemoryBefore,
        afterDatabaseCompletion: blackoutMemoryAfterDatabase,
        afterCGameInit: blackoutMemoryAfterCGame,
        afterFirstWorldFrame: blackoutMemoryAfterWorld,
        steadyState: blackoutMemory,
    };
    const blackoutAudio = await page.evaluate(() => structuredClone(
        globalThis.__retailAudioTelemetry.at(-1) ?? null));
    failureStage = "Blackout gameplay validation";
    failureClass = "cgame";
    const blackoutInput = await exerciseRetailInput(page);
    failureStage = "Blackout checkpoint";
    failureClass = "lifecycle";
    const blackoutCheckpoint = await writeConfigAndCheckpoint(page);
    const blackoutEvidence = await mapEvidence(
        page, "blackout", blackoutCursor, blackoutCommandMs,
        blackoutMemoryLifecycle, blackoutStability, blackoutAudio,
        blackoutInput, blackoutCheckpoint);
    blackoutEvidence.canonicalLifecycle = await lifecycleEvidence(
        page, blackoutCursor.lifecycle);
    assertMapEvidence(blackoutEvidence, blackoutMemory);
    failureStage = "Blackout WebGL context recovery";
    failureClass = "renderer";
    const blackoutContextRecovery = await recoverMapContext(page, "blackout");

    failureStage = "Blackout to Killhouse transition";
    failureClass = "lifecycle";
    const blackoutToKillhouseCursor = await page.evaluate(() =>
        globalThis.__retailRendererLifecycle.length);
    const returnedKillhouseCursor = await captureMapCursor(page);
    const returnedKillhouseCommandMs = await submitCommand(page, "map killhouse");
    failureStage = "Returned Killhouse database load";
    failureClass = "database";
    await waitForDatabaseCompletion(
        page, "killhouse", returnedKillhouseCursor.database);
    failureStage = "Returned Killhouse canonical lifecycle";
    failureClass = "cgame";
    await waitForLifecycleStages(
        page, returnedKillhouseCursor.lifecycle, canonicalMapLifecycleStages);
    failureStage = "Returned Killhouse first world frame";
    failureClass = "renderer";
    const returnedKillhouseFrame = await firstWorldFrameEvidence(
        page, "killhouse", returnedKillhouseCursor.frames);
    const blackoutToKillhouse = await rendererTransitionEvidence(
        page, blackoutToKillhouseCursor, returnedKillhouseCommandMs,
        returnedKillhouseFrame.observedMs);
    failureStage = "Blackout to Killhouse critical input";
    failureClass = "cgame";
    const returnedKillhouseInput = await exerciseTransitionInput(page);
    failureStage = "Returned Killhouse WebGL context recovery";
    failureClass = "renderer";
    const returnedKillhouseContextRecovery = await recoverMapContext(
        page, "killhouse");

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
        schemaVersion: 3,
        source: { commitSha: sourceCommit, dirty: sourceDirty },
        recordedAtUtc: new Date().toISOString(),
        environment: {
            browser: retailBrowserMetadata,
            operatingSystem,
            referenceHardware,
            build: "Release diagnostics",
        },
        validationResult: "pass",
        browserHeadless: retailBrowserMetadata.headless,
        browserName: retailBrowserMetadata.name,
        browserVersion: retailBrowserMetadata.version,
        failureStage: null,
        failureClass: null,
        maps: {
            killhouse: killhouseEvidence,
            cargoship: cargoshipEvidence,
            blackout: blackoutEvidence,
        },
        transitions: {
            killhouseToCargoship: {
                from: "killhouse",
                to: "cargoship",
                ...transitionEvidence,
                destinationCriticalInput: cargoshipTransitionInput,
            },
            cargoshipToBlackout: {
                from: "cargoship",
                to: "blackout",
                ...cargoshipToBlackout,
                destinationCriticalInput: blackoutTransitionInput,
            },
            blackoutToKillhouse: {
                from: "blackout",
                to: "killhouse",
                ...blackoutToKillhouse,
                destinationCriticalInput: returnedKillhouseInput,
            },
        },
        contextRecovery: {
            cargoship: cargoshipContextRecovery,
            blackout: blackoutContextRecovery,
            returnedKillhouse: returnedKillhouseContextRecovery,
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
                name: browserChannel ?? browser?.browserType().name() ?? "unknown",
                version: browser?.version() ?? "unknown",
                channel: browserChannel ?? null,
                headless: Boolean(test.info().project.use.headless ?? true),
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
            expect(await page.evaluate((mapName) =>
                globalThis.__KISAKCOD_WEB__.assets.manifest.profile
                    .availableSinglePlayerZones.includes(`zone/english/${mapName}.ff`),
            phase3TargetMap)).toBe(true);

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
            const targetMemoryBefore = await rendererMemorySnapshot(page);
            const targetCommandMs = await submitCommand(
                page, `map ${phase3TargetMap}`);

            failureStage = `${phase3TargetMap} database load`;
            failureClass = "database";
            await waitForDatabaseCompletion(
                page, phase3TargetMap, targetCursor.database);
            const targetMemoryAfterDatabase = await rendererMemorySnapshot(page);
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
            const targetMemoryAfterCGame = await rendererMemorySnapshot(page);
            failureStage = `${phase3TargetMap} first world frame`;
            failureClass = "renderer";
            const targetFrame = await firstWorldFrameEvidence(
                page, phase3TargetMap, targetCursor.frames);
            const targetMemoryAfterWorld = await rendererMemorySnapshot(page);
            const transitionIn = await rendererTransitionEvidence(
                page, transitionInCursor, targetCommandMs, targetFrame.observedMs);

            failureStage = `${phase3TargetMap} 60-second stability`;
            const targetStability = await sustainWorldFrames(
                page, phase3TargetMap, 60_000);
            failureStage = `${phase3TargetMap} renderer memory`;
            failureClass = "memory";
            const targetMemory = await rendererMemorySnapshot(page);
            const targetMemoryLifecycle = {
                beforeMapLoad: targetMemoryBefore,
                afterDatabaseCompletion: targetMemoryAfterDatabase,
                afterCGameInit: targetMemoryAfterCGame,
                afterFirstWorldFrame: targetMemoryAfterWorld,
                steadyState: targetMemory,
            };
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
                targetMemoryLifecycle, targetStability, targetAudio,
                targetInput, targetCheckpoint);
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
                schemaVersion: 3,
                source: { commitSha: sourceCommit, dirty: sourceDirty },
                recordedAtUtc: new Date().toISOString(),
                environment: {
                    browser: retailBrowserMetadata,
                    operatingSystem,
                    referenceHardware,
                    build: "Release diagnostics",
                },
                validationResult: "pass",
                browserHeadless: retailBrowserMetadata.headless,
                browserName: retailBrowserMetadata.name,
                browserVersion: retailBrowserMetadata.version,
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
