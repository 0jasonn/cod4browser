import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { join } from 'node:path';
import { cpus, totalmem } from 'node:os';
import { chromium } from '@playwright/test';
import { aggregateGameplayProfile, summarizeProfileSamples } from '../tests/browser/retail_profile_aggregate.mjs';
import { summarizeForegroundSamples } from '../tests/browser/retail_foreground_window.mjs';
import { validateWorkload, validateProfileWindow } from './renderer_workload.mjs';

// Local, owned installation only. No asset contents or logs are written to evidence.
const runLabel = process.argv[2] ?? 'sample';
assert(/^[a-z0-9-]+$/.test(runLabel));
const production = process.argv[3] === 'production';
const controlled = process.argv[6] === 'fixedtime';
const checkRecovery = process.argv[7] === 'recovery';
const uncapped = process.argv[7] === 'uncapped';
const movingCamera = process.argv[7] === 'moving-camera';
assert(!process.argv[7] || (checkRecovery && !production && controlled) ||
    (uncapped && production && controlled) || (movingCamera && !production && controlled));
const mapCommand = controlled ? 'set sv_mapSeed 1; devmap cargoship; fixedtime 16' : 'map cargoship';
assert(!process.argv[6] || controlled, 'optional workload must be fixedtime');
const sourceRevision = process.argv[4] ?? (production ? undefined : 'HEAD');
assert(sourceRevision, 'production measurement requires the built source revision');
const source = {
    commitSha: execFileSync('git', ['rev-parse', sourceRevision], { encoding: 'utf8' }).trim(),
    dirty: execFileSync('git', ['status', '--porcelain'], { encoding: 'utf8' }).trim().length > 0,
};
// Match the directory being served, including retained before/after artifacts.
const wasmPath = join(process.argv[5] ?? (production ? 'build/web/site'
    : 'build/web-diagnostics/site-diagnostics'), 'kisakcod.wasm');
const artifactSha256 = createHash('sha256').update(await readFile(wasmPath)).digest('hex');
assert(process.env.KISAK_COD4_RETAIL_ROOT);
// Playwright owns this new temporary persistent profile and its cleanup.
const context = await chromium.launchPersistentContext('', {
    channel: 'chrome', headless: true, viewport: { width: 1440, height: 1000 },
});
const browser = context.browser();
const page = context.pages()[0] ?? await context.newPage();
const pageErrors = [];
page.on('pageerror', error => pageErrors.push(error.message));
let stage = 'bootstrap';
const progressTimer = setInterval(async () => {
    console.log('PROGRESS', stage, JSON.stringify(await page.evaluate(() => ({
        state: globalThis.__KISAKCOD_WEB__?.state,
        assets: globalThis.__KISAKCOD_WEB__?.assets?.state,
        filesystem: globalThis.__KISAKCOD_WEB__?.module?.filesystemState,
        frames: globalThis.__dobj?.frames.length,
    })).catch(() => null)));
}, 20000);
try {
    await page.addInitScript(() => {
        Object.defineProperty(globalThis, 'showDirectoryPicker', { configurable: true, value: undefined });
        globalThis.__dobj = { frames: [], profiles: [], logs: [], memory: [], system: [], foreground: [], collecting: false };
        for (const [name, key] of [['renderer-scene-frame', 'frames'], ['frame-profile', 'profiles'], ['log', 'logs'], ['renderer-memory', 'memory'], ['system', 'system']]) {
            addEventListener(`kisakcod:${name}`, event => {
                const list = __dobj[key];
                list.push({ ...structuredClone(event.detail), observedMs: performance.now() });
                if (list.length > 2048) list.shift();
            });
        }
        const sampleFocus = () => {
            if (__dobj.collecting) __dobj.foreground.push({ observedMs: performance.now(),
                visibilityState: document.visibilityState, pageFocused: document.hasFocus() });
        };
        setInterval(sampleFocus, 250);
        for (const name of ['visibilitychange', 'focus', 'blur']) addEventListener(name, sampleFocus);
    });
    await page.goto('http://127.0.0.1:8051/');
    const servedWasm = await page.request.get('http://127.0.0.1:8051/kisakcod.wasm');
    assert(servedWasm.ok());
    assert.equal(createHash('sha256').update(await servedWasm.body()).digest('hex'), artifactSha256,
        'served Wasm does not match the selected retained artifact');
    await servedWasm.dispose();
    await page.waitForFunction(production => production
        ? document.documentElement.dataset.runtimeState === 'running'
        : globalThis.__KISAKCOD_WEB__?.state === 'running', production, { timeout: 60000 });
    const engineWorker = page.workers().find(worker => worker.url().includes('engine_worker.mjs'));
    assert(engineWorker, 'engine Worker exists');
    console.log('BOOTED');
    stage = 'import';
    const pendingChooser = page.waitForEvent('filechooser');
    await page.locator('#portable-install-button').click();
    const chooser = await pendingChooser;
    await chooser.setFiles(process.env.KISAK_COD4_RETAIL_ROOT);
    await page.waitForFunction(production => {
        if (!production) return __KISAKCOD_WEB__?.assets?.state === 'failed' ||
            (__KISAKCOD_WEB__?.assets?.state === 'ready' &&
            __KISAKCOD_WEB__?.module?.filesystemState === 'mounted');
        const state = document.querySelector('.asset-control')?.dataset.assetState;
        return state === 'failed' || (state === 'ready' &&
            document.querySelector('#boot-log')?.textContent.includes('Local installation mounted; canonical runtime started.'));
    }, production, { timeout: 300000 });
    assert.equal(await page.evaluate(production => production
        ? document.querySelector('.asset-control').dataset.assetState : __KISAKCOD_WEB__.assets.state, production), 'ready');
    if (production) await engineWorker.evaluate(() => {
        globalThis.__warmWorldFrames = 0;
        let worldReady = false;
        addEventListener('kisakcod:renderer-scene-frame', event => {
            if (event.detail.worldName?.toLowerCase().includes('cargoship')) worldReady = true;
        });
        addEventListener('kisakcod:system', () => { if (worldReady) ++__warmWorldFrames; });
    });
    console.log('IMPORTED_AND_MOUNTED');
    stage = 'CargoShip load';
    if (controlled) {
        await page.bringToFront();
        await page.evaluate(() => { __dobj.profiles = []; __dobj.collecting = true; });
        await engineWorker.evaluate(async ({ production, uncapped, movingCamera }) => {
            const { ENGINE_PROTOCOL_VERSION } = await import(production ? './product_protocol.mjs' : './engine_protocol.mjs');
            globalThis.__cleanFrames = [];
            globalThis.__workloadViews = [];
            globalThis.__workloadWarmup = [];
            globalThis.__profileViews = [];
            let generation = 0;
            const commands = new Map([
                [30, 'cg_ufo'], [60, 'cl_paused_simple 1; pause'],
                [120, 'cg_setviewpos -9732 -9384 2041 73 16'],
                [180, `cg_setviewpos -9732 -9384 2041 73 16${uncapped ? '; com_maxfps 0' : ''}`],
            ]);
            if (movingCamera) {
                for (let generation = 610; generation <= 710; generation += 10) {
                    const yaw = generation % 20 === 10 ? 253 : 73;
                    commands.set(generation,
                        `cg_setviewpos -9732 -9384 2041 ${yaw} 16`);
                }
            }
            const view = event => {
                const detail = event.detail;
                if (!detail.worldName?.toLowerCase().includes('cargoship')) return;
                generation = detail.submissionGeneration;
                if (generation === 30 || generation === 60) __workloadWarmup.push(structuredClone(detail));
                if (commands.has(generation)) {
                    // Use the validated production command request, synchronously
                    // queued for the next pump. DOM/Worker transit must not select
                    // which simulation frame is paused. No diagnostic exports.
                    const command = commands.get(generation);
                    dispatchEvent(new MessageEvent('message', { data: {
                        protocolVersion: ENGINE_PROTOCOL_VERSION, id: 1000000 + generation,
                        ...(production ? { type: 'submitCanonicalCommand', command }
                            : { type: 'probe', functionName: '_KisakWeb_SubmitCanonicalCommand',
                                buffers: [new TextEncoder().encode(`${command}\0`).buffer],
                                argumentLayout: [{ kind: 'pointer', index: 0 }] }),
                    } }));
                    commands.delete(generation);
                }
                if (generation >= 240 && generation <= 540 && generation % 60 === 0) {
                    __workloadViews.push(structuredClone(detail));
                    // Production system telemetry omits most callbacks below
                    // 16 ms. Existing canonical view checkpoints remain exact
                    // at every 60 views, with no added runtime instrumentation.
                    if (production) __cleanFrames.push({ at: performance.now(), generation });
                }
                if (!production && generation === 600) dispatchEvent(new MessageEvent('message', { data: {
                    protocolVersion: ENGINE_PROTOCOL_VERSION, type: 'call', id: 1000600,
                    functionName: '_KisakWeb_TestBeginFrameProfileWithTimeout', arguments: [120, 30000],
                } }));
                if (!production && generation > 600 && generation <= 720) __profileViews.push(structuredClone(detail));
                if (!production && generation === 720) removeEventListener('kisakcod:renderer-scene-view', view);
            };
            const sample = event => {
                if (generation < 240) return;
                __cleanFrames.push({ at: performance.now(), generation: production
                    ? event.detail.framePumpTicks : event.detail.viewSubmissionGeneration });
                if (generation >= 540) {
                    removeEventListener(production ? 'kisakcod:system' : 'kisakcod:renderer-scene-frame', sample);
                    if (production) removeEventListener('kisakcod:renderer-scene-view', view);
                }
            };
            addEventListener('kisakcod:renderer-scene-view', view);
            if (!production) addEventListener('kisakcod:renderer-scene-frame', sample);
        }, { production, uncapped, movingCamera });
    }
    // fixedtime is a canonical cheat dvar; devmap enables it through the normal
    // engine command path. No diagnostic exports or memory writes are used.
    await page.locator('#engine-command-input').fill(mapCommand);
    await page.locator('#engine-command-form').evaluate(form => form.requestSubmit());
    if (production) {
        const warmupDeadline = Date.now() + 300000;
        while (await engineWorker.evaluate(() => __warmWorldFrames) < 30) {
            assert(Date.now() < warmupDeadline, 'production world warmup completed');
            await page.waitForTimeout(1000);
        }
    } else await page.waitForFunction(() => __dobj.frames.filter(frame => frame.state === 'drawn' &&
        frame.geometrySubmitted && frame.worldName?.toLowerCase().includes('cargoship')).length >= 30,
        null, { timeout: 300000 });
    console.log('CARGOSHIP_WARM_30_FRAMES');
    stage = 'profiling disabled';
    await page.bringToFront();
    if (!controlled) await page.evaluate(() => {
        __dobj.profiles = [];
        __dobj.foreground = [{ observedMs: performance.now(), visibilityState: document.visibilityState,
            pageFocused: document.hasFocus() }];
        __dobj.collecting = true;
    });
    if (!controlled) await engineWorker.evaluate(production => {
        const eventName = production ? 'kisakcod:system' : 'kisakcod:renderer-scene-frame';
        globalThis.__cleanFrames = [];
        const sample = event => {
            __cleanFrames.push({ at: performance.now(), generation: production
                ? event.detail.framePumpTicks : event.detail.viewSubmissionGeneration });
            if (__cleanFrames.length === 301) removeEventListener(eventName, sample);
        };
        addEventListener(eventName, sample);
    }, production);
    // Poll only completion, not individual frame timestamps across the Worker bridge.
    const cleanDeadline = Date.now() + 60000;
    const checkpointTiming = production && controlled;
    const timestampCount = checkpointTiming ? 6 : 301;
    while (await engineWorker.evaluate(() => __cleanFrames.length) < timestampCount) {
        assert(Date.now() < cleanDeadline, 'profiling-disabled window completed within 60 seconds');
        await page.waitForTimeout(1000);
    }
    const cleanFrames = await engineWorker.evaluate(() => __cleanFrames);
    assert.equal(cleanFrames.length, timestampCount, 'timing window must cover exactly 300 frames');
    const workload = controlled ? validateWorkload(...await engineWorker.evaluate(() =>
        [__workloadViews, __workloadWarmup])) : undefined;
    if (uncapped) workload.requestedMaxFpsAfterView180 = 0;
    const cleanForeground = summarizeForegroundSamples(await page.evaluate(() => {
        __dobj.collecting = false;
        __dobj.foreground.push({ observedMs: performance.now(), visibilityState: document.visibilityState,
            pageFocused: document.hasFocus() });
        return __dobj.foreground;
    }));
    assert(cleanForeground.performanceWindowValid);
    assert.equal(await page.evaluate(() => __dobj.profiles.length), 0, 'profiler stayed inactive');
    const cleanIntervals = cleanFrames.slice(1).map((frame, index) => {
        const span = checkpointTiming ? 60 : 1;
        assert.equal(frame.generation, cleanFrames[index].generation + span, 'no skipped timing checkpoints');
        return (frame.at - cleanFrames[index].at) / span;
    });
    const cleanTiming = { intervals: summarizeProfileSamples(cleanIntervals), foreground: cleanForeground,
        checkpointSpanFrames: checkpointTiming ? 60 : undefined,
        frameIntervalsCovered: 300, checkpointTimes: checkpointTiming ? cleanFrames : undefined,
        clock: checkpointTiming ? 'Worker performance.now at canonical view checkpoints; samples are 60-frame span means'
            : production ? 'Worker performance.now at completed main-loop callbacks'
            : 'Worker performance.now at completed canonical render events',
        profilerActive: false, diagnosticBuild: !production, displayedFps: false };
    console.log('CLEAN_TIMING', JSON.stringify(cleanTiming));
    if (production) {
        assert.equal(pageErrors.length, 0);
        const result = { source, artifactSha256, cleanTiming, workload, pageErrorCount: 0,
            recordedAtUtc: new Date().toISOString(),
            environment: { browser: 'Chrome', version: browser.version(), headless: true,
                processor: cpus()[0].model, viewport: { width: 1440, height: 1000 }, build: 'Release production' },
            methodology: { map: 'cargoship', warmupWorldFrames: controlled ? 240 : 30, profilingDisabledIntervals: 300,
                command: mapCommand,
                cameraSetup: controlled ? { mode: 'cg_ufo after view 30', pauseAfterView: 60, afterViews: [120, 180],
                    command: 'cg_setviewpos -9732 -9384 2041 73 16' } : undefined,
                input: controlled ? 'Paused renderer-only workload; no gameplay input'
                    : 'No gameplay input; authored scene continues running', displayedFps: false } };
        await writeFile(`build/renderer-efficiency-${source.commitSha.slice(0, 8)}-${runLabel}.json`,
            `${JSON.stringify(result, null, 2)}\n`);
    } else {
    stage = 'profile';
    await page.bringToFront();
    const started = await page.evaluate(async controlled => {
        if (!controlled) __dobj.profiles = [];
        __dobj.foreground = [{ observedMs: performance.now(), visibilityState: document.visibilityState,
            pageFocused: document.hasFocus() }];
        __dobj.collecting = true;
        const startedMs = performance.now();
        const accepted = controlled ? null : await __KISAKCOD_WEB__.module.call('_KisakWeb_TestBeginFrameProfileWithTimeout', 120, 30000);
        return { startedMs, accepted };
    }, controlled);
    if (!controlled) assert.equal(started.accepted, 1);
    await page.waitForFunction(() => __dobj.profiles.some(entry => entry.kind === 'capture'), null, { timeout: 45000 });
    const captured = await page.evaluate(() => {
        __dobj.foreground.push({ observedMs: performance.now(), visibilityState: document.visibilityState,
            pageFocused: document.hasFocus() });
        __dobj.collecting = false;
        return { entries: __dobj.profiles, foreground: __dobj.foreground, endedMs: performance.now() };
    });
    const terminal = captured.entries.find(entry => entry.kind === 'capture');
    assert(terminal.profileComplete, JSON.stringify(terminal));
    assert.equal(terminal.profileSamplesCollected, 120);
    const frames = captured.entries.filter(entry => entry.kind === 'frame');
    assert.equal(frames.length, 120);
    const workCounts = controlled && !movingCamera ? validateProfileWindow(frames,
        await engineWorker.evaluate(() => __profileViews), workload) : undefined;
    const foreground = summarizeForegroundSamples(captured.foreground);
    assert(foreground.performanceWindowValid, JSON.stringify(foreground));
    const profile = aggregateGameplayProfile({ frames,
        gpuResults: captured.entries.filter(entry => entry.kind === 'gpu-result'),
        capture: { ...terminal, observedDurationMs: captured.endedMs - started.startedMs },
        cleanAverageFrameIntervalMs: cleanTiming.intervals.average });
    const drawFields = ['dynamicModelProjectionMs', 'dynamicModelParametersMs', 'dynamicModelMaterialMs', 'dynamicModelTexturesMs', 'dynamicModelDrawMs'];
    for (const field of drawFields) assert.equal(profile.renderer[field]?.sampleCount, 120, field);
    for (const { renderer } of frames) {
        for (const field of drawFields) assert(renderer[field] >= 0, field);
        assert(drawFields.reduce((sum, field) => sum + renderer[field], 0) <= renderer.dynamicModelsMs + 0.001, 'Draw intervals exceed parent');
    }
    const sunShadowFields = ['sunShadowWorldMs', 'sunShadowStaticModelsMs',
        'sunShadowDynamicModelsMs'];
    for (const field of sunShadowFields)
        assert.equal(profile.renderer[field]?.sampleCount, 120, field);
    for (const { renderer } of frames) {
        for (const field of sunShadowFields) assert(renderer[field] >= 0, field);
        assert(sunShadowFields.reduce((sum, field) => sum + renderer[field], 0) <=
            renderer.sunShadowDrawMs + 0.001, 'Sun-shadow intervals exceed parent');
    }
    const spotShadowFields = ['spotShadowWorldMs', 'spotShadowStaticModelsMs',
        'spotShadowDynamicModelsMs'];
    for (const field of spotShadowFields)
        assert.equal(profile.renderer[field]?.sampleCount, 120, field);
    for (const { renderer } of frames) {
        for (const field of spotShadowFields) assert(renderer[field] >= 0, field);
        assert(spotShadowFields.reduce((sum, field) => sum + renderer[field], 0) <=
            renderer.spotShadowDrawMs + 0.001, 'Spot-shadow intervals exceed parent');
    }
    const sceneFields = ['sceneSetupMs', 'dobjBuildMs', 'sceneAssemblyMs',
        'sceneImageResolveMs', 'sceneDynamicSubmitMs', 'sceneCameraVisibilityMs', 'sceneViewSubmitMs'];
    const assemblyFields = ['sceneEffectsPrepareMs', 'sceneModelBuildMs', 'sceneCommandAppendMs'];
    const brushFields = ['sceneBrushRemapMs', 'sceneBrushGeometryMs', 'sceneBrushMaterialMs', 'sceneBrushAppendMs'];
    const dynamicFields = ['dynamicCopyMs', 'dynamicGeometryUploadMs', 'dynamicTextureUploadMs', 'dynamicPublishMs'];
    const commandFields = ['commandGeometryCheckMs', 'commandGeometryCopyMs', 'commandBatchCopyMs'];
    for (const field of [...sceneFields, ...assemblyFields, ...brushFields, ...dynamicFields, ...commandFields, 'sceneCloudAppendMs',
        'dobjPoseMs', 'dobjLightingMs', 'dobjSkinningMs',
        'dobjMatrixMs', 'dobjWeightedSkinningMs', 'dobjRigidSkinningMs', 'dobjGeometryMs',
        'dobjVertexEmitMs', 'dobjIndexEmitMs', 'sceneBrushBuildMs'])
        assert.equal(profile.cpu[field]?.sampleCount, 120, field);
    for (const { cpu } of frames) {
        const splitSkinning = cpu.dobjMatrixMs + cpu.dobjWeightedSkinningMs +
            cpu.dobjRigidSkinningMs;
        assert(splitSkinning <= cpu.dobjSkinningMs + 0.001,
            `DObj skinning intervals overlap: ${splitSkinning - cpu.dobjSkinningMs}`);
    }
    const sceneResiduals = frames.map(({ cpu }) => {
        for (const field of sceneFields) assert(cpu[field] >= 0, field);
        const residual = cpu.sceneBuildMs - sceneFields.reduce((sum, field) => sum + cpu[field], 0);
        assert(residual >= -0.001, `Scene intervals overlap: ${residual}`);
        return residual;
    });
    for (const { cpu } of frames) {
        for (const field of brushFields) assert(cpu[field] >= 0, field);
        assert(brushFields.reduce((sum, field) => sum + cpu[field], 0) <= cpu.sceneBrushBuildMs + 0.001,
            'Brush intervals exceed construction/append parent');
        for (const field of ['dobjVertexEmitMs', 'dobjIndexEmitMs', 'sceneBrushBuildMs'])
            assert(cpu[field] >= 0, field);
        assert(cpu.dobjVertexEmitMs + cpu.dobjIndexEmitMs <= cpu.dobjGeometryMs + 0.001,
            'DObj emission intervals exceed geometry parent');
        assert(cpu.sceneBrushBuildMs <= cpu.sceneModelBuildMs + 0.001,
            'Brush build interval exceeds model parent');
        for (const field of [...assemblyFields, 'sceneCloudAppendMs']) assert(cpu[field] >= 0, field);
        const assemblySum = assemblyFields.reduce((sum, field) => sum + cpu[field], 0);
        assert(Math.abs(cpu.sceneAssemblyMs - assemblySum) <= 0.001, 'Assembly partition mismatch');
        assert(cpu.sceneCloudAppendMs <= cpu.sceneCommandAppendMs + 0.001, 'Cloud interval exceeds parent');
        for (const field of dynamicFields) assert(cpu[field] >= 0, field);
        assert(dynamicFields.reduce((sum, field) => sum + cpu[field], 0) <= cpu.sceneDynamicSubmitMs + 0.001,
            'Dynamic submission intervals exceed parent');
        for (const field of commandFields) assert(cpu[field] >= 0, field);
        assert(commandFields.reduce((sum, field) => sum + cpu[field], 0) <= cpu.dynamicCopyMs + 0.001,
            'Command copy intervals exceed dynamic parent in warmed scene');
    }
    assert.equal(pageErrors.length, 0);
    const geometryMemory = await page.evaluate(async () => {
        await __KISAKCOD_WEB__.module.call('_KisakWeb_TestEmitRendererMemory');
        const sample = __dobj.memory.at(-1);
        return { capacityBytes: sample.dynamicGeometryCapacityBytes,
            stagingCapacityBytes: sample.dynamicGeometryStagingCapacityBytes };
    });
    assert(geometryMemory.stagingCapacityBytes > 0);
    assert(geometryMemory.capacityBytes > geometryMemory.stagingCapacityBytes);
    let recovery;
    if (checkRecovery) {
        // Separate from both timing windows: rebuild GPU resources, then check
        // actual resumed draw work against the completed pre-loss profile.
        stage = 'context recovery';
        const before = await page.evaluate(() => ({
            frame: __dobj.frames.at(-1), losses: __KISAKCOD_WEB__.contextLosses ?? 0,
            recoveries: __KISAKCOD_WEB__.rendererSurface.recoveryCount ?? 0,
        }));
        assert(await page.evaluate(() => __KISAKCOD_WEB__.module.call('_KisakWeb_TestLoseWebGLContext')));
        await page.waitForFunction(() => __KISAKCOD_WEB__.rendererSurface.state === 'lost');
        assert(await page.evaluate(() => __KISAKCOD_WEB__.module.call('_KisakWeb_TestRestoreWebGLContext')));
        await page.waitForFunction(before =>
            __dobj.frames.at(-1)?.resourceGeneration > before.frame.resourceGeneration &&
            __dobj.frames.at(-1)?.viewSubmissionGeneration > before.frame.viewSubmissionGeneration + 10,
            before, { timeout: 30000 });
        const restored = await page.evaluate(() => ({
            runtime: __KISAKCOD_WEB__.state, surface: __KISAKCOD_WEB__.rendererSurface.state,
            losses: __KISAKCOD_WEB__.contextLosses, recoveries: __KISAKCOD_WEB__.rendererSurface.recoveryCount,
        }));
        assert.equal(restored.runtime, 'running');
        assert.equal(restored.surface, 'ready');
        assert(restored.losses > before.losses, JSON.stringify({ before, restored }));
        assert(restored.recoveries > before.recoveries, JSON.stringify({ before, restored }));
        await page.evaluate(async () => {
            __dobj.profiles = [];
            await __KISAKCOD_WEB__.module.call('_KisakWeb_TestBeginFrameProfileWithTimeout', 12, 30000);
        });
        await page.waitForFunction(() => __dobj.profiles.some(entry => entry.kind === 'capture'), null, { timeout: 30000 });
        const resumed = await page.evaluate(() => ({
            frames: __dobj.profiles.filter(entry => entry.kind === 'frame'),
            capture: __dobj.profiles.find(entry => entry.kind === 'capture'),
            generation: __dobj.frames.at(-1).resourceGeneration,
        }));
        assert(resumed.capture.profileComplete);
        assert.equal(resumed.frames.length, 12);
        for (const frame of resumed.frames)
            for (const [key, value] of Object.entries(workCounts[0]))
                assert.equal(frame.counters[key], value, `recovered ${key} differs`);
        recovery = { passed: true, matchingWorkSamples: 12,
            resourceGenerationBefore: before.frame.resourceGeneration,
            resourceGenerationAfter: resumed.generation };
        assert.equal(pageErrors.length, 0);
    }
    const result = { schemaVersion: 1, recovery, cleanTiming, artifactSha256, recordedAtUtc: new Date().toISOString(), source,
        workload, workCounts, workCountSha256: workCounts
            ? createHash('sha256').update(JSON.stringify(workCounts)).digest('hex') : undefined,
        bufferUploadBytesSamples: movingCamera
            ? frames.map(({ counters }) => counters.bufferUploadBytes) : undefined,
        environment: { browser: 'Chrome', version: browser.version(), headless: true,
            processor: cpus()[0].model, totalSystemMemoryBytes: totalmem(), viewport: { width: 1440, height: 1000 },
            build: 'Release diagnostics' },
        methodology: { map: 'cargoship', warmupWorldFrames: controlled ? 240 : 30, completedGameplayFrames: 120, profilingDisabledIntervals: 300,
            profileViews: controlled ? [601, 720] : undefined,
            input: movingCamera ? 'Paused renderer-only workload; canonical camera yaw alternates every 10 views'
                : controlled ? 'Paused renderer-only workload; no gameplay input' : 'No gameplay input; authored scene continues running', cleanBenchmark: true,
            compatibilityValidation: false, foreground }, profile,
        dobjUnassignedMs: summarizeProfileSamples(frames.map(({ cpu }) => cpu.dobjBuildMs -
            cpu.dobjPoseMs - cpu.dobjLightingMs - cpu.dobjSkinningMs - cpu.dobjGeometryMs)),
        sceneUnassignedMs: summarizeProfileSamples(sceneResiduals), geometryMemory, pageErrors };
    await writeFile(`build/renderer-efficiency-${source.commitSha.slice(0, 8)}-${runLabel}.json`, `${JSON.stringify(result, null, 2)}\n`);
    console.log(JSON.stringify({ stage: 'complete', renderer: profile.renderer, cpu: profile.cpu,
        dobjUnassignedMs: result.dobjUnassignedMs, sceneUnassignedMs: result.sceneUnassignedMs,
        foreground, pageErrors }));
    }
} catch (error) {
    console.error('PROFILE_FAILED', stage, error.message);
    const failedWorker = page.workers().find(worker => worker.url().includes('engine_worker.mjs'));
    console.error('WORKLOAD_PROGRESS', await failedWorker?.evaluate(() => ({
        cleanCount: globalThis.__cleanFrames?.length,
        cleanFirst: globalThis.__cleanFrames?.[0], cleanLast: globalThis.__cleanFrames?.at(-1),
        views: globalThis.__workloadViews, warmup: globalThis.__workloadWarmup,
    })).catch(() => null));
    console.error('ENGINE_ERRORS', await page.evaluate(() => globalThis.__dobj?.logs
        .filter(log => /error|failed|invalid|assert/i.test(log.text ?? log.message ?? ''))
        .slice(-8)).catch(() => null));
    console.error(JSON.stringify(await page.evaluate(pageErrors => ({ state: globalThis.__KISAKCOD_WEB__?.state,
        assets: globalThis.__KISAKCOD_WEB__?.assets?.state,
        worldFrames: globalThis.__dobj?.frames.length,
        lastScene: globalThis.__dobj?.frames.at(-1),
        lastSystem: globalThis.__dobj?.system.at(-1),
        pageErrors }), pageErrors).catch(() => null)));
    process.exitCode = 1;
} finally {
    clearInterval(progressTimer);
    await context.close();
    await browser.close();
}
