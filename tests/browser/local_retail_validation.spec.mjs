import { execFileSync } from "node:child_process";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { cpus, platform, release, tmpdir, totalmem, version } from "node:os";
import { join } from "node:path";

import { chromium, expect, test as base } from "@playwright/test";

import { summarizeForegroundSamples } from "./retail_foreground_window.mjs";
import { aggregateGameplayProfile } from "./retail_profile_aggregate.mjs";
import {
    createMissionRouteController,
    createMissionRouteRecorder,
    MISSION_ROUTE_FAILURE,
    parseMissionRoute,
} from "../../web/diagnostic_mission_route.mjs";

const retailRoot = process.env.KISAK_COD4_RETAIL_ROOT;
const browserChannel = process.env.KISAK_BROWSER_CHANNEL;
const phase3TargetMap = process.env.KISAK_RETAIL_PHASE3_MAP?.trim().toLowerCase();
const missionTargetMap = process.env.KISAK_RETAIL_MISSION_MAP?.trim().toLowerCase();
const missionRoutePath = process.env.KISAK_RETAIL_ROUTE_PATH?.trim();
const missionRouteOutputPath = process.env.KISAK_RETAIL_ROUTE_OUTPUT?.trim();
const missionRouteAssist = process.env.KISAK_RETAIL_ROUTE_ASSIST === "1";
const missionValidationStage = process.env.KISAK_RETAIL_MISSION_STAGE?.trim()
    .toLowerCase() ?? "full";
const missionRouteMode = process.env.KISAK_RETAIL_ROUTE_MODE?.trim().toLowerCase() ??
    (missionRoutePath ? "replay" : null);
const runDecodeChain = process.env.KISAK_RETAIL_DECODE_CHAIN === "1";
if (phase3TargetMap && (!/^[a-z0-9_]+$/.test(phase3TargetMap) ||
    phase3TargetMap.startsWith("mp_") || phase3TargetMap.endsWith("_mp"))) {
    throw new Error("KISAK_RETAIL_PHASE3_MAP must name one single-player zone");
}
if (missionTargetMap && (!/^[a-z0-9_]+$/.test(missionTargetMap) ||
    missionTargetMap.startsWith("mp_") || missionTargetMap.endsWith("_mp"))) {
    throw new Error("KISAK_RETAIL_MISSION_MAP must name one single-player zone");
}
if (missionRouteMode && !["author", "replay"].includes(missionRouteMode))
    throw new Error("KISAK_RETAIL_ROUTE_MODE must be author or replay");
if (missionRouteMode === "author" && !missionRouteOutputPath)
    throw new Error("KISAK_RETAIL_ROUTE_OUTPUT is required in author mode");
if (missionRouteMode === "replay" && !missionRoutePath)
    throw new Error("KISAK_RETAIL_ROUTE_PATH is required in replay mode");
if (!['progression', 'full'].includes(missionValidationStage))
    throw new Error("KISAK_RETAIL_MISSION_STAGE must be progression or full");
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
const frameProfileTimeoutMs = 120_000;
const retailEvidenceSchemaVersion = 4;
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
    const mission = testInfo.tags.includes("@retail-mission");
    const routeAuthor = testInfo.tags.includes("@retail-route-author");
    const decode = testInfo.tags.includes("@retail-decode");
    const prefix = routeAuthor ? "KISAK_RETAIL_ROUTE_RESULT" : mission
        ? "KISAK_RETAIL_MISSION_RESULT" : phase3
        ? "KISAK_RETAIL_PHASE3_RESULT" : decode
            ? "KISAK_RETAIL_DECODE_RESULT" : "KISAK_RETAIL_RESULT";
    console.log(`${prefix} ${JSON.stringify({
        schemaVersion: retailEvidenceSchemaVersion,
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
        ...(mission || routeAuthor ? { targetMap: missionTargetMap } : {}),
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
        globalThis.__retailCinematics = [];
        globalThis.__retailLogs = [];
        globalThis.__retailRouteAuthor = { markers: 0, finish: false };
        globalThis.addEventListener("keydown", (event) => {
            if (event.code !== "F8" && event.code !== "F9") return;
            event.preventDefault();
            event.stopImmediatePropagation();
            if (event.code === "F8") ++globalThis.__retailRouteAuthor.markers;
            else globalThis.__retailRouteAuthor.finish = true;
        }, true);
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
            globalThis.__retailValidationInput.push({
                ...structuredClone(event.detail), observedMs: performance.now(),
            });
        });
        globalThis.addEventListener("kisakcod:engine-lifecycle", (event) => {
            globalThis.__retailLifecycle.push({
                ...structuredClone(event.detail), observedMs: performance.now(),
            });
        });
        globalThis.addEventListener("kisakcod:cinematic", (event) => {
            globalThis.__retailCinematics.push({
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

async function measureCleanPerformanceWindow(page, mapName, durationMs = 60_000)
{
    await page.bringToFront();
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
    stability, profileCapture, audioSnapshot, input, checkpointResult)
{
    const rawEvidence = await page.evaluate(({ map, cursor, commandTimeMs,
        memoryLifecycle, stability, profileCapture, audioSnapshot, input,
        checkpointResult }) => {
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
        const rawProfiles = globalThis.__retailFrameProfiles.slice(
            profileCapture.profileCursor);
        const frameProfiles = rawProfiles.filter((entry) =>
            entry.kind === "frame" && entry.gameplayFrame === true &&
            entry.rendererSubmitted === true &&
            entry.observedMs >= profileCapture.startedMs &&
            entry.observedMs <= profileCapture.endedMs &&
            entry.viewSubmissionGeneration >= profileCapture.firstGeneration &&
            entry.viewSubmissionGeneration <= profileCapture.finalGeneration);
        const gpuProfiles = rawProfiles.filter((entry) =>
            entry.kind === "gpu-result");
        const gameTimeAdvancementMs = stability.performanceWindowValid &&
            stabilityViews.length > 1
            ? stabilityViews.at(-1).time - stabilityViews[0].time : null;
        const wallTimeAdvancementMs = stability.performanceWindowValid
            ? stability.endedMs - stability.startedMs : null;
        const averageFrameIntervalMs = intervals.length
            ? intervals.reduce((sum, value) => sum + value, 0) / intervals.length
            : null;
        const averageFpsEquivalent = averageFrameIntervalMs
            ? 1_000 / averageFrameIntervalMs : null;
        const gameTimeWallTimeRatio = wallTimeAdvancementMs
            ? gameTimeAdvancementMs / wallTimeAdvancementMs : null;
        const classification = stability.performanceWindowValid
            ? averageFpsEquivalent >= 30 && percentile(0.95) <= 50 &&
                gameTimeWallTimeRatio >= 0.90 ? "PLAYABLE" : "FUNCTIONAL"
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
            averageFpsEquivalent,
            gameTimeAdvancementMs,
            wallTimeAdvancementMs,
            gameTimeWallTimeRatio,
            classification,
            classificationSource: "clean-performance-window",
            framesRenderedDuringStabilityWindow: stabilityFrames.length,
            cleanPerformanceWindow: stability,
            rawFrameProfiles: frameProfiles,
            rawGpuProfiles: gpuProfiles,
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
    }, { map, cursor, commandTimeMs, memoryLifecycle, stability, profileCapture,
        audioSnapshot, input, checkpointResult });
    rawEvidence.frameProfile = aggregateGameplayProfile({
        frames: rawEvidence.rawFrameProfiles,
        gpuResults: rawEvidence.rawGpuProfiles,
        capture: profileCapture,
        cleanAverageFrameIntervalMs: rawEvidence.averageFrameIntervalMs,
    });
    delete rawEvidence.rawFrameProfiles;
    delete rawEvidence.rawGpuProfiles;
    return rawEvidence;
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
    const decode = sample.imageDecode;
    for (const field of [
        "encodedImageInspectionCount",
        "metadataParseCount",
        "pixelDecodeCount",
        "initialUploadDecodeCount",
        "contextRecoveryDecodeCount",
        "decodedBytes",
        "duplicateDecodeCount",
    ]) {
        expect(Number.isSafeInteger(decode[field])).toBe(true);
        expect(decode[field]).toBeGreaterThanOrEqual(0);
    }
    expect(decode.initialUploadDecodeCount + decode.contextRecoveryDecodeCount)
        .toBe(decode.pixelDecodeCount);
    expect(decode.duplicateDecodeCount)
        .toBeLessThanOrEqual(decode.initialUploadDecodeCount);
    expect(decode.cpuMilliseconds).toBeGreaterThanOrEqual(0);
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
    expect(evidence.frameProfile.profileComplete).toBe(true);
    expect(evidence.frameProfile.profileSamplesRequested)
        .toBe(frameProfileSampleLimit);
    expect(evidence.frameProfile.profileSamplesCollected)
        .toBe(frameProfileSampleLimit);
    expect(evidence.frameProfile.sampleCount).toBe(frameProfileSampleLimit);
    expect(evidence.frameProfile.profileIncompleteReason).toBeNull();
    expect(evidence.frameProfile.cpu.totalMs).not.toBeNull();
    expect(evidence.frameProfile.renderer.worldMs).not.toBeNull();
    expect(evidence.frameProfile.counters.worldDrawCalls).not.toBeNull();
    expect(evidence.frameProfile.counters.worldDrawCalls.maximum)
        .toBeGreaterThan(0);
    expect(evidence.frameProfile.counters.worldSurfacesDrawn.maximum)
        .toBeGreaterThan(0);
    if (evidence.performanceWindowValid) {
        expect(evidence.averageFrameTimeMs).not.toBeNull();
        expect(evidence.p50FrameTimeMs).not.toBeNull();
        expect(evidence.p95FrameTimeMs).not.toBeNull();
        expect(evidence.p99FrameTimeMs).not.toBeNull();
        expect(evidence.gameTimeWallTimeRatio).not.toBeNull();
        expect(evidence.frameProfile.overhead.cleanAverageFrameIntervalMs)
            .toBe(evidence.averageFrameIntervalMs);
        expect(evidence.frameProfile.overhead.profiledAverageFrameIntervalMs)
            .not.toBeNull();
        expect(evidence.frameProfile.overhead.profilerOverheadPercent)
            .not.toBeNull();
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

async function recoverMapContext(page, mapName, validateInput = true)
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
    const input = validateInput ? await exerciseRetailInput(page) : null;
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

async function gameplayFloat(page, field, component)
{
    return page.evaluate(
        ({ stateField, vectorComponent }) => globalThis.__KISAKCOD_WEB__.module.call(
            "_KisakWeb_TestGameplayFloat", stateField, vectorComponent),
        { stateField: field, vectorComponent: component });
}

async function missionObjectiveMarkers(page)
{
    return page.evaluate(async () => {
        const module = globalThis.__KISAKCOD_WEB__.module;
        const markers = [];
        for (let slot = 0; slot < 16; ++slot) {
            const state = await module.call("_KisakWeb_TestObjectiveState", slot);
            for (let marker = 0; marker < 8; ++marker) {
                const origin = await Promise.all([0, 1, 2].map(
                    (component) => module.call(
                        "_KisakWeb_TestObjectiveOrigin", slot, marker,
                        component)));
                if (origin.every(Number.isFinite) &&
                    origin.some((value) => Math.abs(value) > 0.001)) {
                    markers.push({ source: "cgame", slot, marker, state, origin });
                }
            }
            const serverState = await module.call(
                "_KisakWeb_TestServerObjectiveState", slot);
            for (let marker = 0; marker < 8; ++marker) {
                const serverOrigin = await Promise.all([0, 1, 2].map(
                    (component) => module.call(
                        "_KisakWeb_TestServerObjectiveOrigin", slot, marker,
                        component)));
                if (serverOrigin.every(Number.isFinite) &&
                    serverOrigin.some((value) => Math.abs(value) > 0.001)) {
                    markers.push({ source: "server", slot, marker,
                        state: serverState, origin: serverOrigin });
                }
            }
        }
        for (let slot = 0; slot < 32; ++slot) {
            const entity = await module.call(
                "_KisakWeb_TestTargetEntity", slot);
            if (entity < 0) continue;
            const origin = await Promise.all([0, 1, 2].map(
                (component) => module.call(
                    "_KisakWeb_TestTargetOrigin", slot, component)));
            if (origin.every(Number.isFinite)) {
                markers.push({ source: "target", slot, marker: entity,
                    state: 4, origin });
            }
        }
        return markers;
    });
}

async function missionActors(page)
{
    return page.evaluate(async () => {
        const module = globalThis.__KISAKCOD_WEB__.module;
        const playerTeam = await module.call("_KisakWeb_TestPlayerTeam");
        const actors = [];
        for (let slot = 0; slot < 32; ++slot) {
            const team = await module.call("_KisakWeb_TestActorState", slot, 0);
            if (team < 0) continue;
            const [health, compass, hasPath, state, moveMode, lastShotTime,
                lineOfSight] = await Promise.all([1, 2, 3, 4, 5, 6, 7].map(
                    (field) => module.call(
                        "_KisakWeb_TestActorState", slot, field)));
            const origin = await Promise.all([0, 1, 2].map(
                (component) => module.call(
                    "_KisakWeb_TestActorVector", slot, 0, component)));
            const pathGoal = hasPath ? await Promise.all([0, 1, 2].map(
                (component) => module.call(
                    "_KisakWeb_TestActorVector", slot, 1, component))) : null;
            const aimOrigin = await Promise.all([0, 1, 2].map(
                (component) => module.call(
                    "_KisakWeb_TestActorVector", slot, 2, component)));
            actors.push({ slot, team, health, compass, hasPath, state,
                moveMode, lastShotTime, lineOfSight, origin, pathGoal,
                aimOrigin });
        }
        return { playerTeam, actors };
    });
}

const missionStateField = Object.freeze({
    serverHealth: 17,
    serverPmType: 18,
    objectiveHash: 19,
    activeActors: 20,
    aliveActors: 21,
    actorFingerprint: 22,
    scriptThreads: 23,
    levelTime: 24,
    levelFrame: 25,
    missionFlags: 26,
    committedSave: 27,
    saveWrittenToDevice: 28,
    saveHealth: 29,
    saveBodySize: 30,
    saveMapMatches: 31,
    saveId: 32,
    activeObjectives: 33,
    doneObjectives: 34,
    saveChecksum: 35,
});

async function canonicalMissionState(page)
{
    const read = (field, argument = 0) => gameplayState(page, field, argument);
    const values = await Promise.all([
        read(missionStateField.serverHealth),
        read(missionStateField.serverPmType),
        read(missionStateField.objectiveHash),
        read(missionStateField.activeActors),
        read(missionStateField.aliveActors),
        read(missionStateField.actorFingerprint),
        read(missionStateField.scriptThreads),
        read(missionStateField.levelTime),
        read(missionStateField.levelFrame),
        read(missionStateField.missionFlags),
        read(missionStateField.committedSave),
        read(missionStateField.saveWrittenToDevice),
        read(missionStateField.saveHealth),
        read(missionStateField.saveBodySize),
        read(missionStateField.saveMapMatches),
        read(missionStateField.saveId),
        read(missionStateField.activeObjectives),
        read(missionStateField.doneObjectives),
        read(missionStateField.saveChecksum),
    ]);
    const [serverHealth, serverPmType, objectiveHash, activeActors, aliveActors,
        actorFingerprint, scriptThreads, levelTime, levelFrame, missionFlags,
        committedSave, saveWrittenToDevice, saveHealth, saveBodySize,
        saveMapMatches, saveId, activeObjectives, doneObjectives,
        saveChecksum] = values;
    return {
        serverHealth,
        serverPmType,
        objectiveHash,
        activeActors,
        aliveActors,
        actorFingerprint,
        scriptThreads,
        levelTime,
        levelFrame,
        missionFlags,
        committedSave: Boolean(committedSave),
        saveWrittenToDevice: Boolean(saveWrittenToDevice),
        saveHealth,
        saveBodySize,
        saveMapMatches: Boolean(saveMapMatches),
        saveId,
        activeObjectives,
        doneObjectives,
        saveChecksum,
    };
}

async function missionRouteObservation(page)
{
    const [view, serverHealth, objectiveHash, activeActors, aliveActors,
        scriptThreads, missionFlags, committedSave, saveId, activeObjectives,
        doneObjectives, saveChecksum, ...playerVector] = await Promise.all([
        page.evaluate(() => structuredClone(
            globalThis.__retailValidationViews.at(-1))),
        gameplayState(page, missionStateField.serverHealth),
        gameplayState(page, missionStateField.objectiveHash),
        gameplayState(page, missionStateField.activeActors),
        gameplayState(page, missionStateField.aliveActors),
        gameplayState(page, missionStateField.scriptThreads),
        gameplayState(page, missionStateField.missionFlags),
        gameplayState(page, missionStateField.committedSave),
        gameplayState(page, missionStateField.saveId),
        gameplayState(page, missionStateField.activeObjectives),
        gameplayState(page, missionStateField.doneObjectives),
        gameplayState(page, missionStateField.saveChecksum),
        ...[0, 1].flatMap((field) => [0, 1, 2].map(
            (component) => gameplayFloat(page, field, component))),
    ]);
    if (!view?.viewOrigin) throw new Error("canonical renderer view is unavailable");
    return {
        timestampMs: view.observedMs,
        origin: playerVector.slice(0, 3),
        aimOrigin: view.viewOrigin.slice(0, 3),
        viewAngles: playerVector.slice(3, 6),
        health: serverHealth,
        progression: {
            objectiveHash,
            activeObjectives,
            doneObjectives,
            missionFlags,
        },
        mission: {
            activeActors,
            aliveActors,
            scriptThreads,
        },
        checkpoint: {
            committed: Boolean(committedSave),
            saveId,
            checksum: saveChecksum,
        },
    };
}

function missionRouteAdapter(page)
{
    const keys = {
        forward: "w", left: "a", right: "d", jump: "Space", use: "f",
    };
    return Object.freeze({
        observe: () => missionRouteObservation(page),
        async key(key, down) {
            if (key === "fire" || key === "ads") {
                const button = key === "fire" ? "left" : "right";
                if (down) await page.mouse.down({ button });
                else await page.mouse.up({ button });
                return;
            }
            if (!keys[key]) throw new Error(`unsupported route key ${key}`);
            if (down) await page.keyboard.down(keys[key]);
            else await page.keyboard.up(keys[key]);
        },
        mouse(dx, dy) {
            return page.evaluate(({ x, y }) => {
                const movement = new MouseEvent("mousemove");
                Object.defineProperties(movement, {
                    movementX: { value: x },
                    movementY: { value: y },
                });
                globalThis.dispatchEvent(movement);
            }, { x: dx, y: dy });
        },
        wait: (milliseconds) => page.waitForTimeout(milliseconds),
    });
}

async function prepareMissionRouteInput(page)
{
    await page.bringToFront();
    const canvas = page.locator("#game-canvas");
    for (let attempt = 0; attempt < 3 &&
        (await gameplayState(page, 12) & 0x10) !== 0; ++attempt) {
        await canvas.focus();
        await page.keyboard.press("Escape");
        await page.waitForTimeout(100);
    }
    await expect.poll(async () => (await gameplayState(page, 12) & 0x10))
        .toBe(0);
    await waitForRelativeMouseMode(page);
    await canvas.click({ position: { x: 8, y: 8 } });
    await expect.poll(() => page.evaluate(() => document.pointerLockElement?.id))
        .toBe("game-canvas");
}

async function replayMissionRoute(page, before)
{
    const route = parseMissionRoute(JSON.parse(
        await readFile(missionRoutePath, "utf8")));
    expect(route.map).toBe(missionTargetMap);
    await prepareMissionRouteInput(page);
    let replay;
    try {
        replay = await createMissionRouteController(missionRouteAdapter(page), {
            tickMs: 200,
            obstacleRecoveryAttempts: 4,
            obstacleRecoveryMs: 1_000,
        }).run(route);
    } catch (error) {
        console.log(`KISAK_ROUTE_REPLAY_FAILURE ${JSON.stringify({
            code: error?.code, segmentIndex: error?.segmentIndex,
            message: error?.message,
        })}`);
        throw error;
    }
    const after = await canonicalMissionState(page);
    const changes = missionProgressDelta(before, after);
    expect(changes,
        "route replay must change a canonical objective/progression marker")
        .not.toEqual([]);
    return { ...replay, changes, before, after };
}

async function ensureMissionEnemyDamage(page, logStart)
{
    const damageEvents = () => page.evaluate((startIndex) =>
        globalThis.__retailLogs.slice(startIndex).filter(({ text }) =>
            text.includes("canonical damage target=") &&
            text.includes("class=actor_enemy")).map(({ text }) => text),
    logStart);
    let events = await damageEvents();
    if (events.length > 0) return events;
    const adapter = missionRouteAdapter(page);
    for (let attempt = 0; attempt < 24 && events.length === 0; ++attempt) {
        const [observation, actorState] = await Promise.all([
            adapter.observe(), missionActors(page),
        ]);
        const targets = actorState.actors
            .filter(({ team, health, origin, aimOrigin }) =>
                team !== actorState.playerTeam && health > 0 &&
                origin.every(Number.isFinite) &&
                aimOrigin.every(Number.isFinite))
            .map((actor) => ({
                ...actor,
                distance: Math.hypot(...actor.origin.map((value, index) =>
                    value - observation.origin[index])),
            }))
            .filter(({ distance }) => distance >= 64 && distance <= 2_048)
            .sort((left, right) =>
                Number(right.lineOfSight > 0) -
                    Number(left.lineOfSight > 0) ||
                Number(right.lastShotTime > 0) -
                    Number(left.lastShotTime > 0) ||
                left.distance - right.distance);
        const visibleTarget = targets.find(({ lineOfSight }) =>
            lineOfSight > 0);
        let segment;
        if (visibleTarget) {
            segment = {
                targetRegion: {
                    x: visibleTarget.aimOrigin[0],
                    y: visibleTarget.aimOrigin[1],
                    z: visibleTarget.aimOrigin[2],
                    radius: visibleTarget.distance + 32,
                },
                maxDurationMs: 10_000,
                minimumDurationMs: 2_000,
                stuckTimeoutMs: 10_000,
                restartPolicy: "fail",
                actions: { ads: true, fire: true },
            };
        } else {
            const allies = actorState.actors
                .filter(({ team, health, origin }) =>
                    team === actorState.playerTeam && health > 0 &&
                    origin.every(Number.isFinite))
                .map((actor) => ({
                    ...actor,
                    routeOrigin: actor.hasPath &&
                        actor.pathGoal?.every(Number.isFinite)
                        ? actor.pathGoal : actor.origin,
                }))
                .map((actor) => ({
                    ...actor,
                    distance: Math.hypot(...actor.routeOrigin.map(
                        (value, index) => value - observation.origin[index])),
                }))
                .filter(({ distance }) =>
                    distance >= 192 && distance <= 4_096)
                .sort((left, right) =>
                    Number(right.hasPath) - Number(left.hasPath) ||
                    left.distance - right.distance);
            if (allies.length === 0) break;
            const ally = allies[0];
            segment = {
                targetRegion: {
                    x: ally.routeOrigin[0], y: ally.routeOrigin[1],
                    z: ally.routeOrigin[2], radius: 192,
                },
                maxDurationMs: 45_000,
                stuckTimeoutMs: 10_000,
                restartPolicy: "fail",
                actions: { jump: true, use: true },
            };
        }
        try {
            await createMissionRouteController(adapter, {
                tickMs: 100,
                mouseCountsPerDegree: 8,
                obstacleRecoveryAttempts: 4,
            }).run({ schemaVersion: 1, map: missionTargetMap,
                segments: [segment] });
        } catch (error) {
            if (![MISSION_ROUTE_FAILURE.STUCK, MISSION_ROUTE_FAILURE.TIMEOUT]
                .includes(error?.code)) throw error;
        }
        events = await damageEvents();
    }
    return events;
}

async function authorMissionRoute(page)
{
    const recorder = createMissionRouteRecorder({ map: missionTargetMap });
    let inputCursor = 0;
    console.log("KISAK_ROUTE_AUTHOR_READY F8=mark waypoint F9=finish");
    for (;;) {
        recorder.recordObservation(await missionRouteObservation(page));
        const captured = await page.evaluate((cursor) => {
            const inputs = structuredClone(
                globalThis.__retailValidationInput.slice(cursor));
            const control = { ...globalThis.__retailRouteAuthor };
            globalThis.__retailRouteAuthor.markers = 0;
            return {
                inputs,
                inputCursor: globalThis.__retailValidationInput.length,
                control,
            };
        }, inputCursor);
        inputCursor = captured.inputCursor;
        for (const input of captured.inputs)
            recorder.recordInput(input, input.observedMs);
        for (let marker = 0; marker < captured.control.markers; ++marker)
            recorder.markWaypoint();
        if (captured.control.finish) break;
        await page.waitForTimeout(250);
    }
    const authored = recorder.finish();
    await writeFile(missionRouteOutputPath,
        `${JSON.stringify(authored.route, null, 2)}\n`, "utf8");
    await writeFile(`${missionRouteOutputPath}.evidence.json`,
        `${JSON.stringify(authored.evidence, null, 2)}\n`, "utf8");
    return authored;
}

async function authorAssistedMissionRoute(page)
{
    const recorder = createMissionRouteRecorder({
        map: missionTargetMap,
        radius: 160,
    });
    const baseAdapter = missionRouteAdapter(page);
    const routeKey = {
        ads: 0xC9, fire: 0xC8, forward: 0x77, left: 0x61, right: 0x64,
        jump: 0x20, use: 0x66,
    };
    let lastObservationMs = 0;
    let initialProgression = null;
    let initialCheckpoint = null;
    let progressed = false;
    let combatObserved = false;
    let routeController = null;
    let routeHeading = null;
    let progressionWaypointDeferred = false;
    let lastRecordedWaypointOrigin = null;
    const meaningfulProgression = (observation) => initialProgression && (
        (observation.progression.objectiveHash !==
            initialProgression.objectiveHash &&
            observation.progression.activeObjectives > 0) ||
        observation.progression.doneObjectives >
            initialProgression.doneObjectives ||
        observation.progression.missionFlags !==
            initialProgression.missionFlags ||
        observation.checkpoint.saveId !== initialCheckpoint.saveId ||
        observation.checkpoint.checksum !== initialCheckpoint.checksum);
    const adapter = Object.freeze({
        ...baseAdapter,
        async observe() {
            const observation = await baseAdapter.observe();
            lastObservationMs = observation.timestampMs;
            recorder.recordObservation(observation);
            const newlyObservedProgression = !progressed &&
                meaningfulProgression(observation);
            if (newlyObservedProgression) {
                progressed = true;
                routeController?.cancel();
            }
            if (!newlyObservedProgression && lastRecordedWaypointOrigin &&
                Math.hypot(...observation.origin.map((value, index) =>
                    value - lastRecordedWaypointOrigin[index])) >= 512) {
                recorder.markWaypoint();
                lastRecordedWaypointOrigin = [...observation.origin];
            }
            return observation;
        },
        async key(key, down) {
            await baseAdapter.key(key, down);
            recorder.recordInput(
                { type: "key", key: routeKey[key], down }, lastObservationMs);
        },
        async mouse(dx, dy) {
            await baseAdapter.mouse(dx, dy);
            recorder.recordInput(
                { type: "mouse-move", dx, dy }, lastObservationMs);
        },
    });
    const start = await adapter.observe();
    lastRecordedWaypointOrigin = [...start.origin];
    const combatLogStart = await page.evaluate(() =>
        globalThis.__retailLogs.length);
    initialProgression = { ...start.progression };
    initialCheckpoint = { ...start.checkpoint };
    const attempted = new Set();
    const retryable = new Set([
        MISSION_ROUTE_FAILURE.DIVERGED,
        MISSION_ROUTE_FAILURE.STUCK,
        MISSION_ROUTE_FAILURE.TIMEOUT,
    ]);
    let squadUpdateWaits = 0;
    for (let attempt = 0; attempt < 96 &&
        (!progressed || !combatObserved); ++attempt) {
        const progressedBeforeSegment = progressed;
        const observation = await adapter.observe();
        const [objectiveMarkers, actorState] = await Promise.all([
            missionObjectiveMarkers(page), missionActors(page),
        ]);
        if (attempt === 0) {
            console.log(`KISAK_ROUTE_OBJECTIVE_MARKERS ${JSON.stringify(
                objectiveMarkers)}`);
            console.log(`KISAK_ROUTE_ACTORS ${JSON.stringify(actorState)}`);
        }
        const canonicalOrigins = new Set();
        const canonicalMarkers = objectiveMarkers.filter(
            ({ source, slot, marker, state, origin }) => {
                const originKey = origin.map(Math.round).join(":");
                if (canonicalOrigins.has(originKey)) return false;
                canonicalOrigins.add(originKey);
                return (state === 1 || state === 4) &&
                    !attempted.has(`${source}:${slot}:${marker}`);
            })
            .sort((left, right) =>
                Math.hypot(...left.origin.map((value, index) =>
                    value - observation.origin[index])) -
                Math.hypot(...right.origin.map((value, index) =>
                    value - observation.origin[index])));
        const actorPositions = actorState.actors
            .filter(({ team, health, origin }) =>
                team === actorState.playerTeam && health > 0 &&
                origin.every(Number.isFinite))
            .map((actor) => ({
                ...actor,
                source: "actor",
                marker: actor.origin.map(
                    (value) => Math.round(value / 96)).join(":"),
                distance: Math.hypot(...actor.origin.map(
                    (value, index) => value - observation.origin[index])),
            }))
            .filter(({ source, slot, marker, distance, origin }) =>
                distance >= 192 && distance <= 4_096 &&
                (!routeHeading || origin.reduce((sum, value, index) =>
                    sum + (value - observation.origin[index]) *
                        routeHeading[index], 0) >= 0) &&
                !attempted.has(`${source}:${slot}:${marker}`));
        const actorPathGoals = actorState.actors
            .filter(({ team, health, hasPath, pathGoal }) =>
                team === actorState.playerTeam && health > 0 && hasPath &&
                pathGoal?.every(Number.isFinite))
            .map((actor) => ({
                ...actor,
                source: "actor-path",
                origin: actor.pathGoal,
                marker: actor.pathGoal.map(
                    (value) => Math.round(value / 96)).join(":"),
                distance: Math.hypot(...actor.pathGoal.map(
                    (value, index) => value - observation.origin[index])),
            }))
            .filter(({ source, slot, marker, distance, origin }) =>
                distance >= 192 && distance <= 8_192 &&
                (!routeHeading || origin.reduce((sum, value, index) =>
                    sum + (value - observation.origin[index]) *
                        routeHeading[index], 0) >= 0) &&
                !attempted.has(`${source}:${slot}:${marker}`));
        const actorMarkers = [...actorPositions, ...actorPathGoals]
            .sort((left, right) =>
                Number(right.source === "actor") -
                    Number(left.source === "actor") ||
                Number(right.hasPath) - Number(left.hasPath) ||
                Number(right.moveMode > 0) - Number(left.moveMode > 0) ||
                left.distance - right.distance);
        const enemyMarkers = actorState.actors
            .filter(({ team, health, aimOrigin }) =>
                team !== actorState.playerTeam && health > 0 &&
                aimOrigin.every(Number.isFinite))
            .map((actor) => ({
                ...actor,
                source: "enemy",
                origin: actor.aimOrigin,
                marker: actor.aimOrigin.map(
                    (value) => Math.round(value / 96)).join(":"),
                distance: Math.hypot(...actor.aimOrigin.map(
                    (value, index) => value - observation.origin[index])),
            }))
            .filter(({ source, slot, marker, distance }) =>
                distance >= 64 && distance <= 1_800 &&
                !attempted.has(`${source}:${slot}:${marker}`))
            .sort((left, right) =>
                Number(right.lineOfSight > 0) -
                    Number(left.lineOfSight > 0) ||
                Number(right.lastShotTime > 0) -
                    Number(left.lastShotTime > 0) ||
                left.distance - right.distance);
        if (routeHeading && enemyMarkers.length === 0 &&
            actorMarkers.length === 0 && canonicalMarkers.length > 0) {
            if (squadUpdateWaits < 60) {
                ++squadUpdateWaits;
                await adapter.wait(1_000);
                continue;
            }
            break;
        }
        if (actorMarkers.length > 0) squadUpdateWaits = 0;
        const markers = enemyMarkers.length > 0 ? enemyMarkers :
            actorMarkers.length > 0 ? actorMarkers : canonicalMarkers;
        if (markers.length === 0) break;
        const target = markers[0];
        attempted.add(`${target.source}:${target.slot}:${target.marker}`);
        const segment = {
            targetRegion: {
                x: target.origin[0], y: target.origin[1], z: target.origin[2],
                radius: target.source.startsWith("actor") ? 192 :
                    target.source === "enemy" ? target.distance + 32 : 128,
            },
            maxDurationMs: target.source.startsWith("actor") ? 45_000 :
                target.source === "enemy" ? 20_000 : 90_000,
            ...(target.source === "enemy"
                ? { minimumDurationMs: 2_000 } : {}),
            stuckTimeoutMs: target.source.startsWith("actor") ||
                target.source === "enemy" ? 8_000 : 12_000,
            restartPolicy: "resume",
            actions: target.source === "enemy"
                ? { ads: true, fire: true } : {
                ...(target.source.startsWith("actor") ? {} : { fire: true }),
                jump: true, use: true,
            },
        };
        console.log(`KISAK_ROUTE_ASSIST_TARGET ${JSON.stringify(target)}`);
        try {
            routeController = createMissionRouteController(adapter, {
                tickMs: 100,
                mouseCountsPerDegree: 8,
                minimumProgress: 4,
                obstacleRecoveryAttempts: 2,
                obstacleRecoveryMs: 1_000,
            });
            await routeController.run({ schemaVersion: 1, map: missionTargetMap,
                segments: [segment] });
        } catch (error) {
            if (progressed && error?.code === MISSION_ROUTE_FAILURE.CANCELED) {
                console.log("KISAK_ROUTE_ASSIST_PROGRESSION_CANCELED_SEGMENT");
            } else if (!retryable.has(error?.code)) {
                throw error;
            } else {
                console.log(`KISAK_ROUTE_ASSIST_RETRY ${JSON.stringify({
                    code: error.code, slot: target.slot, marker: target.marker,
                })}`);
            }
        } finally {
            routeController = null;
        }
        if (!progressed) await adapter.wait(1_000);
        const after = await adapter.observe();
        progressed ||= meaningfulProgression(after);
        const newlyProgressed = !progressedBeforeSegment && progressed;
        combatObserved ||= await page.evaluate((startIndex) =>
            globalThis.__retailLogs.slice(startIndex).some(({ text }) =>
                text.includes("canonical damage target=") &&
                text.includes("class=actor_enemy")), combatLogStart);
        const movement = after.origin.map((value, index) =>
            value - observation.origin[index]);
        const movementLength = Math.hypot(...movement);
        if (movementLength >= 32) {
            routeHeading = movement.map((value) => value / movementLength);
        }
        if (newlyProgressed && !combatObserved) {
            progressionWaypointDeferred = true;
        } else if (target.source === "enemy") {
            recorder.markWaypoint({
                targetRegion: segment.targetRegion,
                minimumDurationMs: segment.minimumDurationMs,
            });
            lastRecordedWaypointOrigin = [...after.origin];
        } else if (progressed || movementLength >= 32) {
            recorder.markWaypoint();
            lastRecordedWaypointOrigin = [...after.origin];
            progressionWaypointDeferred = false;
        }
    }
    if (!progressed || !combatObserved) {
        throw new Error(
            `assisted route authoring exhausted canonical markers without ${
                !progressed ? "progression" : "enemy damage"}`);
    }
    if (progressionWaypointDeferred)
        recorder.markWaypoint();
    const authored = recorder.finish();
    await writeFile(missionRouteOutputPath,
        `${JSON.stringify(authored.route, null, 2)}\n`, "utf8");
    await writeFile(`${missionRouteOutputPath}.evidence.json`,
        `${JSON.stringify(authored.evidence, null, 2)}\n`, "utf8");
    return authored;
}

async function loadMissionStart(page)
{
    await installRetailObservers(page);
    failureStage = "mission runtime bootstrap";
    failureClass = "lifecycle";
    await page.goto("/");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    await waitForAssets(page, "empty");
    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(retailRoot);
    await waitForAssets(page, "ready");

    failureStage = `${missionTargetMap} mission load`;
    failureClass = "database";
    const mapCursor = await captureMapCursor(page);
    await submitCommand(page, `devmap ${missionTargetMap}`);
    await waitForDatabaseCompletion(page, missionTargetMap, mapCursor.database);
    failureClass = "cgame";
    await waitForLifecycleStages(
        page, mapCursor.lifecycle, canonicalMapLifecycleStages);
    failureClass = "renderer";
    await firstWorldFrameEvidence(page, missionTargetMap, mapCursor.frames);

    failureStage = `${missionTargetMap} canonical mission systems`;
    failureClass = "game";
    for (const field of [missionStateField.activeActors,
        missionStateField.aliveActors, missionStateField.scriptThreads,
        missionStateField.activeObjectives]) {
        await expect.poll(() => gameplayState(page, field),
            { timeout: 120_000 }).toBeGreaterThan(0);
    }
    const initialState = await canonicalMissionState(page);
    await expect.poll(() => gameplayState(
        page, missionStateField.actorFingerprint), { timeout: 30_000 })
        .not.toBe(initialState.actorFingerprint);
    return initialState;
}

function imageDecodeDelta(after, before)
{
    return Object.fromEntries(Object.keys(after).map((field) =>
        [field, after[field] - before[field]]));
}

async function loadDecodeChainMap(page, map, index)
{
    failureStage = `${map} decode-chain load`;
    failureClass = "database";
    const before = await rendererMemorySnapshot(page);
    const cursors = await page.evaluate(() => ({
        memory: globalThis.__retailValidationMemory.length,
    }));
    const mapCursor = await captureMapCursor(page);
    const commandMs = await submitCommand(page, `map ${map}`);
    await waitForDatabaseCompletion(page, map, mapCursor.database);
    failureStage = `${map} decode-chain canonical lifecycle`;
    failureClass = "cgame";
    await waitForLifecycleStages(
        page, mapCursor.lifecycle, canonicalMapLifecycleStages);
    failureStage = `${map} decode-chain first world frame`;
    failureClass = "renderer";
    const firstFrame = await firstWorldFrameEvidence(page, map, mapCursor.frames);
    await waitForWorldFrames(page, map, firstFrame.viewSubmissionGeneration + 30);
    const afterInitialUpload = await rendererMemorySnapshot(page);
    assertMemoryTelemetry(afterInitialUpload);
    expect(afterInitialUpload.encodedImageRecoveryBytes).toBeGreaterThan(0);
    expect(afterInitialUpload.imageLoadDefCacheEncodedPayloadBytes)
        .toBeLessThanOrEqual(afterInitialUpload.imageLoadDefCacheBudgetBytes);

    const transitionMemory = await page.evaluate((cursor) => structuredClone(
        globalThis.__retailValidationMemory.slice(cursor.memory)), cursors);
    const unloadBegin = transitionMemory.find(
        (entry) => entry.state === "world-unload-begin");
    const unloadEnd = transitionMemory.find(
        (entry) => entry.state === "world-unloaded");
    if (index > 0) {
        expect(unloadBegin).toBeTruthy();
        expect(unloadEnd).toBeTruthy();
        expect(unloadEnd.encodedImageRecoveryBytes).toBe(0);
        expect(unloadEnd.worldImageRecoveryBytes).toBe(0);
        expect(unloadEnd.staticModelImageRecoveryBytes).toBe(0);
        expect(unloadEnd.dynamicModelImageRecoveryBytes).toBe(0);
        expect(unloadEnd.uiImageRecoveryBytes).toBe(0);
        expect(unloadEnd.imageLoadDefCacheEntryCount)
            .toBe(unloadBegin.imageLoadDefCacheEntryCount);
        expect(unloadEnd.imageLoadDefCacheEncodedPayloadBytes)
            .toBe(unloadBegin.imageLoadDefCacheEncodedPayloadBytes);
    }

    failureStage = `${map} decode-chain context recovery`;
    const recovery = await recoverMapContext(page, map, false);
    const recovered = recovery.memory.contextRestored;
    const recoveryDecode = imageDecodeDelta(
        recovered.imageDecode, recovery.memory.contextLost.imageDecode);
    expect(recoveryDecode.contextRecoveryDecodeCount).toBeGreaterThan(0);
    expect(recoveryDecode.duplicateDecodeCount).toBe(0);
    return {
        map,
        firstWorldFrameLatencyMs: firstFrame.observedMs - commandMs,
        initialUploadDecode: imageDecodeDelta(
            afterInitialUpload.imageDecode, before.imageDecode),
        recovery: {
            durationToFirstRecoveredWorldFrameMs:
                recovery.durationToFirstRecoveredWorldFrameMs,
            decode: recoveryDecode,
            wasmAllocatorInUseDeltaBytes:
                recovery.memory.allocatorInUseDeltaBytes,
            wasmLinearMemoryCapacityDeltaBytes:
                recovery.memory.linearMemoryCapacityDeltaBytes,
        },
        rendererRecovery: {
            encodedImageRecoveryBytes:
                afterInitialUpload.encodedImageRecoveryBytes,
            decodedTextureSourceBytes:
                afterInitialUpload.decodedTextureSourceBytes,
            recoveryCopyBytes: afterInitialUpload.recoveryCopyBytes,
        },
        sourceCache: {
            entryCount: afterInitialUpload.imageLoadDefCacheEntryCount,
            encodedPayloadBytes:
                afterInitialUpload.imageLoadDefCacheEncodedPayloadBytes,
            budgetBytes: afterInitialUpload.imageLoadDefCacheBudgetBytes,
            evictionCount: afterInitialUpload.imageLoadDefCacheEvictionCount,
        },
        worldUnload: index === 0 ? null : {
            rendererEncodedBytesAfterUnload:
                unloadEnd.encodedImageRecoveryBytes,
            cacheEntryCountBefore: unloadBegin.imageLoadDefCacheEntryCount,
            cacheEntryCountAfter: unloadEnd.imageLoadDefCacheEntryCount,
            cacheBytesBefore:
                unloadBegin.imageLoadDefCacheEncodedPayloadBytes,
            cacheBytesAfter: unloadEnd.imageLoadDefCacheEncodedPayloadBytes,
        },
    };
}

function missionProgressDelta(before, after)
{
    const changes = [];
    if (after.objectiveHash !== before.objectiveHash)
        changes.push("objectiveHash");
    if (after.activeObjectives !== before.activeObjectives)
        changes.push("activeObjectives");
    if (after.doneObjectives > before.doneObjectives)
        changes.push("doneObjectives");
    if (after.missionFlags !== before.missionFlags)
        changes.push("missionFlags");
    if (after.saveId !== before.saveId ||
        after.saveChecksum !== before.saveChecksum)
        changes.push("checkpoint");
    return changes;
}

async function advanceMissionProgression(page, before, timeoutMs = 120_000)
{
    const started = Date.now();
    let after = before;
    let actionBursts = 0;
    while (Date.now() - started < timeoutMs) {
        await missionActionBurst(page);
        ++actionBursts;
        after = await canonicalMissionState(page);
        const changes = missionProgressDelta(before, after);
        if (changes.length > 0) {
            return {
                validationResult: "pass",
                changes,
                actionBursts,
                observedDurationMs: Date.now() - started,
                before,
                after,
            };
        }
    }
    expect(missionProgressDelta(before, after),
        "real mission actions must change a canonical objective/progression marker")
        .not.toEqual([]);
    return {
        validationResult: "pass",
        changes: missionProgressDelta(before, after),
        actionBursts,
        observedDurationMs: Date.now() - started,
        before,
        after,
    };
}

async function captureGameplayProfile(page, mapName)
{
    await page.bringToFront();
    const started = await page.evaluate((name) => ({
        observedMs: performance.now(),
        profileCursor: globalThis.__retailFrameProfiles.length,
        generation: globalThis.__retailValidationFrames.findLast((entry) =>
            entry.state === "drawn" && entry.geometrySubmitted === true &&
            entry.worldName?.toLowerCase().includes(name))
            ?.viewSubmissionGeneration ?? 0,
    }), mapName);
    expect(await page.evaluate(({ sampleLimit, timeoutMs }) =>
        globalThis.__KISAKCOD_WEB__.module.call(
            "_KisakWeb_TestBeginFrameProfileWithTimeout", sampleLimit, timeoutMs), {
        sampleLimit: frameProfileSampleLimit,
        timeoutMs: frameProfileTimeoutMs,
    })).toBe(1);
    const terminalHandle = await page.waitForFunction((cursor) =>
        globalThis.__retailFrameProfiles.slice(cursor).find(
            (entry) => entry.kind === "capture") ?? null,
    started.profileCursor, { timeout: frameProfileTimeoutMs + 30_000 });
    const terminal = await terminalHandle.jsonValue();
    await terminalHandle.dispose();
    const gpuResultsExpected = await page.evaluate((cursor) =>
        globalThis.__retailFrameProfiles.slice(cursor).filter((entry) =>
            entry.kind === "frame" && entry.gpu?.queryIssued === true).length,
    started.profileCursor);
    if (gpuResultsExpected > 0) {
        await expect.poll(() => page.evaluate((cursor) =>
            globalThis.__retailFrameProfiles.slice(cursor).filter((entry) =>
                entry.kind === "gpu-result").length,
        started.profileCursor), { timeout: 30_000 }).toBe(gpuResultsExpected);
    }
    const ended = await page.evaluate((name) => ({
        observedMs: performance.now(),
        generation: globalThis.__retailValidationFrames.findLast((entry) =>
            entry.state === "drawn" && entry.geometrySubmitted === true &&
            entry.worldName?.toLowerCase().includes(name))
            ?.viewSubmissionGeneration ?? 0,
    }), mapName);
    const capture = {
        requestedSamples: frameProfileSampleLimit,
        timeoutMs: frameProfileTimeoutMs,
        profileCursor: started.profileCursor,
        startedMs: started.observedMs,
        endedMs: ended.observedMs,
        observedDurationMs: ended.observedMs - started.observedMs,
        firstGeneration: started.generation,
        finalGeneration: ended.generation,
        profileComplete: terminal.profileComplete,
        profileSamplesRequested: terminal.profileSamplesRequested,
        profileSamplesCollected: terminal.profileSamplesCollected,
        profileIncompleteReason: terminal.profileIncompleteReason,
        gpuResultsExpected,
        gpuResultsCollected: gpuResultsExpected,
    };
    expect(capture.profileComplete, JSON.stringify(capture)).toBe(true);
    expect(capture.profileSamplesRequested).toBe(frameProfileSampleLimit);
    expect(capture.profileSamplesCollected).toBe(frameProfileSampleLimit);
    return capture;
}

async function missionActionBurst(page, durationMs = 8_000)
{
    const canvas = page.locator("#game-canvas");
    await page.bringToFront();
    if ((await gameplayState(page, 12) & 0x10) !== 0) {
        await canvas.focus();
        await page.keyboard.press("Escape");
    }
    await waitForRelativeMouseMode(page);
    await canvas.click({ position: { x: 8, y: 8 } });
    await expect.poll(() => page.evaluate(() => document.pointerLockElement?.id))
        .toBe("game-canvas");
    await page.keyboard.down("w");
    await page.mouse.down({ button: "left" });
    const started = Date.now();
    let step = 0;
    try {
        while (Date.now() - started < durationMs) {
            await page.evaluate(({ x, y }) => {
                const movement = new MouseEvent("mousemove");
                Object.defineProperties(movement, {
                    movementX: { value: x },
                    movementY: { value: y },
                });
                globalThis.dispatchEvent(movement);
            }, {
                x: step % 8 < 4 ? 2 : -2,
                y: step % 10 === 0 ? -1 : 0,
            });
            if (step % 4 === 0) await page.keyboard.press("f");
            if (step % 12 === 0) await page.keyboard.press("Space");
            await page.waitForTimeout(500);
            ++step;
        }
    } finally {
        await page.mouse.up({ button: "left" });
        await page.keyboard.up("w");
    }
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
    await page.bringToFront();
    await expect.poll(() => page.evaluate(() => ({
        visibility: document.visibilityState,
        focused: document.hasFocus(),
    }))).toEqual({ visibility: "visible", focused: true });
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
    }
    await waitForRelativeMouseMode(page);
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
    const killhouseStability = await measureCleanPerformanceWindow(
        page, "killhouse", stabilityDurationMs);
    const killhouseProfile = await captureGameplayProfile(page, "killhouse");
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
        killhouseProfile, killhouseAudio, killhouseInput, killhouseCheckpoint);
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
    const cargoshipStability = await measureCleanPerformanceWindow(
        page, "cargoship", stabilityDurationMs);
    const cargoshipProfile = await captureGameplayProfile(page, "cargoship");
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
        cargoshipProfile, cargoshipAudio, cargoshipInput, cargoshipCheckpoint);
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
    const blackoutStability = await measureCleanPerformanceWindow(
        page, "blackout", stabilityDurationMs);
    const blackoutProfile = await captureGameplayProfile(page, "blackout");
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
        blackoutMemoryLifecycle, blackoutStability, blackoutProfile, blackoutAudio,
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
        schemaVersion: retailEvidenceSchemaVersion,
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

if (runDecodeChain) {
    test("local retail encoded-source decode chain", { tag: "@retail-decode" },
        async ({ retailPage: page }) => {
            test.setTimeout(1_800_000);
            if (!allowDirty) expect(sourceDirty,
                "authoritative decode validation requires a clean source commit")
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

            failureStage = "decode-chain runtime bootstrap";
            failureClass = "lifecycle";
            await page.goto("/");
            await expect.poll(() => page.evaluate(
                () => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
            await waitForAssets(page, "empty");

            failureStage = "decode-chain installation/profile validation";
            failureClass = "filesystem";
            const chooserPromise = page.waitForEvent("filechooser");
            await page.locator("#portable-install-button").click();
            const chooser = await chooserPromise;
            await chooser.setFiles(retailRoot);
            await waitForAssets(page, "ready");
            const chain = [
                "killhouse",
                "cargoship",
                "blackout",
                "hunted",
                "bog_a",
                "airplane",
                "killhouse",
            ];
            const available = await page.evaluate(() =>
                globalThis.__KISAKCOD_WEB__.assets.manifest.profile
                    .availableSinglePlayerZones.map((path) => path.toLowerCase()));
            for (const map of new Set(chain))
                expect(available).toContain(`zone/english/${map}.ff`);

            const decodeBefore = await rendererMemorySnapshot(page);
            const maps = [];
            for (let index = 0; index < chain.length; ++index)
                maps.push(await loadDecodeChainMap(page, chain[index], index));
            const decodeAfter = await rendererMemorySnapshot(page);
            const totals = imageDecodeDelta(
                decodeAfter.imageDecode, decodeBefore.imageDecode);
            expect(pageErrors).toEqual([]);
            expect(maps.slice(1).every((entry) =>
                entry.worldUnload.rendererEncodedBytesAfterUnload === 0)).toBe(true);
            expect(maps.every((entry) =>
                entry.sourceCache.encodedPayloadBytes <=
                    entry.sourceCache.budgetBytes)).toBe(true);
            expect(maps.every((entry) =>
                entry.initialUploadDecode.duplicateDecodeCount === 0)).toBe(true);
            expect(totals.duplicateDecodeCount).toBe(0);
            for (let index = 1; index < maps.length; ++index) {
                expect(maps[index].sourceCache.evictionCount)
                    .toBeGreaterThanOrEqual(maps[index - 1].sourceCache.evictionCount);
            }

            failureStage = "decode-chain shutdown";
            failureClass = "lifecycle";
            await page.evaluate(() =>
                globalThis.__KISAKCOD_WEB__.module.dispose());
            console.log(`KISAK_RETAIL_DECODE_RESULT ${JSON.stringify({
                schemaVersion: 1,
                source: { commitSha: sourceCommit, dirty: sourceDirty },
                recordedAtUtc: new Date().toISOString(),
                environment: {
                    browser: retailBrowserMetadata,
                    operatingSystem,
                    referenceHardware,
                    build: "Release diagnostics",
                },
                validationResult: "pass",
                chain,
                maps,
                totals,
                sourceCacheBounded: true,
                mapOwnedRendererSourcesRetired: true,
                contextRecoveryValidatedForEveryMap: true,
            })}`);
        });
}

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
            const targetStability = await measureCleanPerformanceWindow(
                page, phase3TargetMap, 60_000);
            const targetProfile = await captureGameplayProfile(
                page, phase3TargetMap);
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
                targetMemoryLifecycle, targetStability, targetProfile, targetAudio,
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
                schemaVersion: retailEvidenceSchemaVersion,
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

if (missionTargetMap && missionRouteMode === "author") {
    test("local retail mission route authoring",
        { tag: "@retail-route-author" }, async ({ retailPage: page }) => {
            test.setTimeout(7_200_000);
            if (!missionRouteAssist) {
                expect(test.info().project.use.headless,
                    "manual route authoring requires a headed browser")
                    .toBe(false);
            }
            const pageErrors = [];
            page.on("pageerror", (error) => pageErrors.push(error.message));
            await loadMissionStart(page);
            failureStage = `${missionTargetMap} route authoring`;
            failureClass = "input";
            await prepareMissionRouteInput(page);
            const authored = missionRouteAssist
                ? await authorAssistedMissionRoute(page)
                : await authorMissionRoute(page);
            expect(authored.route.segments.length).toBeGreaterThan(0);
            expect(pageErrors).toEqual([]);
            console.log(`KISAK_RETAIL_ROUTE_RESULT ${JSON.stringify({
                schemaVersion: authored.route.schemaVersion,
                source: { commitSha: sourceCommit, dirty: sourceDirty },
                recordedAtUtc: new Date().toISOString(),
                validationResult: "authored",
                targetMap: missionTargetMap,
                routeSegments: authored.route.segments.length,
                observationCount: authored.evidence.observations.length,
                inputTransitionCount: authored.evidence.inputTransitions.length,
            })}`);
        });
}

if (missionTargetMap && missionRouteMode !== "author") {
    test("local retail mission progression", { tag: "@retail-mission" },
        async ({ retailPage: initialPage }) => {
            test.setTimeout(1_200_000);
            if (!allowDirty) expect(sourceDirty,
                "authoritative mission validation requires a clean source commit")
                .toBe(false);
            let page = initialPage;
            const context = page.context();
            const browser = context.browser();
            retailBrowserMetadata = {
                name: browserChannel ?? browser?.browserType().name() ?? "unknown",
                version: browser?.version() ?? "unknown",
                channel: browserChannel ?? null,
                headless: Boolean(test.info().project.use.headless ?? true),
            };
            const pageErrors = [];
            page.on("pageerror", (error) => pageErrors.push(error.message));
            const initialState = await loadMissionStart(page);

            failureStage = `${missionTargetMap} objective progression and combat`;
            failureClass = "game";
            const combatLogStart = await page.evaluate(() =>
                globalThis.__retailLogs.length);
            const progressionStart = initialState;
            const inputEvidence = missionRoutePath
                ? { validationResult: "canonical-input-route" }
                : await exerciseTransitionInput(page);
            const progression = missionRoutePath
                ? await replayMissionRoute(page, progressionStart)
                : await advanceMissionProgression(page, progressionStart);
            let enemyDamageEvents = await page.evaluate((start) =>
                globalThis.__retailLogs.slice(start).filter(({ text }) =>
                    text.includes("canonical damage target=") &&
                    text.includes("class=actor_enemy")).map(({ text }) => text),
            combatLogStart);
            if (missionRoutePath && enemyDamageEvents.length === 0) {
                enemyDamageEvents = await ensureMissionEnemyDamage(
                    page, combatLogStart);
            }
            expect(enemyDamageEvents.length,
                "mission flow must include canonical enemy damage")
                .toBeGreaterThan(0);
            if (missionValidationStage === "progression") {
                const progressedState = await canonicalMissionState(page);
                console.log(`KISAK_RETAIL_MISSION_RESULT ${JSON.stringify({
                    schemaVersion: 2,
                    source: { commitSha: sourceCommit, dirty: sourceDirty },
                    recordedAtUtc: new Date().toISOString(),
                    environment: {
                        browser: retailBrowserMetadata,
                        operatingSystem,
                        referenceHardware,
                        build: "Release diagnostics",
                    },
                    validationResult: "pass",
                    validationStage: "progression",
                    targetMap: missionTargetMap,
                    canonicalSystems: { initialState, progressedState },
                    progression: {
                        ...progression,
                        inputEvidence,
                        enemyDamageEventCount: enemyDamageEvents.length,
                    },
                })}`);
                return;
            }

            failureStage = `${missionTargetMap} progressed natural checkpoint`;
            failureClass = "savegame";
            await expect.poll(async () => {
                const state = await canonicalMissionState(page);
                return state.committedSave && state.saveId > 0 &&
                    (!progressionStart.committedSave ||
                        state.saveChecksum !== progressionStart.saveChecksum);
            }, { timeout: 120_000 }).toBe(true);
            const naturalCheckpoint = await canonicalMissionState(page);
            expect(missionProgressDelta(initialState, naturalCheckpoint))
                .not.toEqual([]);
            await submitCommand(page, "savegame_lastcommit");
            await expect.poll(() => gameplayState(
                page, missionStateField.saveWrittenToDevice),
            { timeout: 60_000 }).toBe(1);
            const naturalCheckpointFlush = await checkpoint(page);
            expect(naturalCheckpointFlush.bytesPersisted).toBeGreaterThan(0);
            const cinematicEvidence = await page.evaluate(() => structuredClone(
                globalThis.__retailCinematics));
            expect(cinematicEvidence.every(({ state }) => state === "skipped"))
                .toBe(true);

            failureStage = `${missionTargetMap} named game save`;
            failureClass = "savegame";
            const saveLogStart = await page.evaluate(() =>
                globalThis.__retailLogs.length);
            await submitCommand(page, "devsave mission-validation");
            await expect.poll(() => gameplayState(
                page, missionStateField.committedSave), { timeout: 60_000 })
                .toBe(1);
            await expect.poll(() => gameplayState(
                page, missionStateField.saveId), { timeout: 60_000 }).toBe(0);
            await expect.poll(() => gameplayState(
                page, missionStateField.saveWrittenToDevice),
            { timeout: 60_000 }).toBe(1);
            await expect.poll(() => page.evaluate((start) =>
                globalThis.__retailLogs.slice(start).some(({ text }) =>
                    text.includes("G_WriteGame 'mission-validation'")),
            saveLogStart), { timeout: 60_000 }).toBe(true);
            const savedState = await canonicalMissionState(page);
            expect(savedState.saveBodySize).toBeGreaterThan(0);
            expect(savedState.saveMapMatches).toBe(true);
            const savedWeapon = await gameplayState(page, 0);
            const savedClip = await gameplayState(page, 1, savedWeapon);
            const savedViewOrigin = await page.evaluate(() => structuredClone(
                globalThis.__retailValidationViews.at(-1)?.viewOrigin));
            const namedSaveFlush = await checkpoint(page);
            expect(namedSaveFlush.bytesPersisted).toBeGreaterThan(0);

            failureStage = `${missionTargetMap} death and checkpoint restart`;
            failureClass = "game";
            const deathLoadLogStart = await page.evaluate(() =>
                globalThis.__retailLogs.length);
            const generationBeforeDeath = await page.evaluate((mapName) =>
                globalThis.__retailValidationFrames.findLast((entry) =>
                    entry.state === "drawn" && entry.geometrySubmitted === true &&
                    entry.worldName?.toLowerCase().includes(mapName))
                    ?.viewSubmissionGeneration ?? 0,
            missionTargetMap);
            await submitCommand(page, "kill");
            await expect.poll(() => gameplayState(
                page, missionStateField.serverHealth), { timeout: 30_000 })
                .toBeLessThanOrEqual(0);
            await expect.poll(() => page.evaluate((start) =>
                globalThis.__retailLogs.slice(start).some(({ text }) =>
                    text.includes("=== G_LoadGame ===")),
            deathLoadLogStart), { timeout: 120_000 }).toBe(true);
            await expect.poll(() => gameplayState(
                page, missionStateField.serverHealth), { timeout: 120_000 })
                .toBeGreaterThan(0);
            await waitForWorldFrames(
                page, missionTargetMap, generationBeforeDeath + 1);
            await expect.poll(() => gameplayState(
                page, missionStateField.objectiveHash), { timeout: 30_000 })
                .toBe(savedState.objectiveHash);
            await expect.poll(() => gameplayState(
                page, missionStateField.activeObjectives), { timeout: 30_000 })
                .toBe(savedState.activeObjectives);
            await expect.poll(() => gameplayState(
                page, missionStateField.doneObjectives), { timeout: 30_000 })
                .toBe(savedState.doneObjectives);
            const restartedState = await canonicalMissionState(page);
            expect(restartedState.serverHealth).toBe(savedState.saveHealth);
            await expect.poll(() => gameplayState(page, 0)).toBe(savedWeapon);
            await expect.poll(() => gameplayState(page, 1, savedWeapon))
                .toBe(savedClip);
            const continuedInput = await exerciseTransitionInput(page);
            await expect.poll(() => gameplayState(
                page, missionStateField.levelFrame), { timeout: 30_000 })
                .toBeGreaterThan(restartedState.levelFrame);

            failureStage = "browser shutdown and durable save flush";
            failureClass = "filesystem";
            const shutdownFlushDurationMs = await page.evaluate(async () => {
                const startedMs = performance.now();
                await globalThis.__KISAKCOD_WEB__.module.dispose();
                return performance.now() - startedMs;
            });
            await page.close();

            failureStage = "fresh browser runtime save load";
            page = await context.newPage();
            page.on("pageerror", (error) => pageErrors.push(error.message));
            await installRetailObservers(page);
            await page.goto("/");
            await expect.poll(() => page.evaluate(() =>
                globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
            await waitForAssets(page, "ready");
            const reloadCursor = await captureMapCursor(page);
            const freshLoadLogStart = await page.evaluate(() =>
                globalThis.__retailLogs.length);
            await submitCommand(page, "loadgame mission-validation");
            await waitForDatabaseCompletion(
                page, missionTargetMap, reloadCursor.database);
            await waitForLifecycleStages(
                page, reloadCursor.lifecycle, canonicalMapLifecycleStages);
            await firstWorldFrameEvidence(
                page, missionTargetMap, reloadCursor.frames);
            await expect.poll(() => page.evaluate((start) =>
                globalThis.__retailLogs.slice(start).some(({ text }) =>
                    text.includes("=== G_LoadGame ===")),
            freshLoadLogStart), { timeout: 120_000 }).toBe(true);
            await expect.poll(() => gameplayState(
                page, missionStateField.objectiveHash), { timeout: 30_000 })
                .toBe(savedState.objectiveHash);
            await expect.poll(() => gameplayState(
                page, missionStateField.activeObjectives), { timeout: 30_000 })
                .toBe(savedState.activeObjectives);
            await expect.poll(() => gameplayState(
                page, missionStateField.doneObjectives), { timeout: 30_000 })
                .toBe(savedState.doneObjectives);
            const freshState = await canonicalMissionState(page);
            expect(freshState.serverHealth).toBe(savedState.saveHealth);
            expect(freshState.scriptThreads).toBeGreaterThan(0);
            expect(freshState.activeActors).toBeGreaterThan(0);
            await expect.poll(() => gameplayState(page, 0)).toBe(savedWeapon);
            await expect.poll(() => gameplayState(page, 1, savedWeapon))
                .toBe(savedClip);
            const freshViewOrigin = await page.evaluate(() => structuredClone(
                globalThis.__retailValidationViews.at(-1)?.viewOrigin));
            expect(Math.hypot(...freshViewOrigin.map((value, index) =>
                value - savedViewOrigin[index]))).toBeLessThan(8);
            const freshContinuationLogStart = await page.evaluate(() =>
                globalThis.__retailLogs.length);
            const freshInput = await exerciseTransitionInput(page);
            let continuedProgression;
            if (missionRoutePath) {
                const damageEvents = await ensureMissionEnemyDamage(
                    page, freshContinuationLogStart);
                expect(damageEvents,
                    "loaded mission must continue through canonical combat")
                    .not.toEqual([]);
                continuedProgression = {
                    validationResult: "pass",
                    changes: ["enemyDamage"],
                    damageEventCount: damageEvents.length,
                    before: freshState,
                    after: await canonicalMissionState(page),
                };
            } else {
                continuedProgression = await advanceMissionProgression(
                    page, freshState);
            }
            expect(pageErrors).toEqual([]);

            console.log(`KISAK_RETAIL_MISSION_RESULT ${JSON.stringify({
                schemaVersion: 2,
                source: { commitSha: sourceCommit, dirty: sourceDirty },
                recordedAtUtc: new Date().toISOString(),
                environment: {
                    browser: retailBrowserMetadata,
                    operatingSystem,
                    referenceHardware,
                    build: "Release diagnostics",
                },
                validationResult: "pass",
                targetMap: missionTargetMap,
                canonicalSystems: {
                    initialState,
                    naturalCheckpoint,
                    naturalCheckpointFlush,
                    cinematicEvidence,
                },
                progression: {
                    ...progression,
                    inputEvidence,
                    enemyDamageEventCount: enemyDamageEvents.length,
                },
                saveAndDeath: {
                    savedState,
                    namedSaveFlush,
                    restartedState,
                    continuedInput,
                },
                shutdown: {
                    flushDurationMs: shutdownFlushDurationMs,
                    freshRuntimeState: freshState,
                    freshRuntimeInput: freshInput,
                    continuedProgression,
                },
            })}`);
        });
}
