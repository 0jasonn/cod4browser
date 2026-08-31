import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { join } from 'node:path';
import { cpus, totalmem } from 'node:os';
import { chromium } from '@playwright/test';
import { aggregateGameplayProfile, summarizeProfileSamples } from '../tests/browser/retail_profile_aggregate.mjs';
import { summarizeForegroundSamples } from '../tests/browser/retail_foreground_window.mjs';
import { validateWorkload } from './renderer_workload.mjs';

// Local, owned installation only. No asset contents or logs are written to evidence.
const runLabel = process.argv[2] ?? 'sample';
assert(/^[a-z0-9-]+$/.test(runLabel));
const production = process.argv[3] === 'production';
const controlled = process.argv[6] === 'fixedtime';
assert(!process.argv[6] || controlled, 'optional workload must be fixedtime');
assert(!controlled || production, 'controlled windows currently require production');
const sourceRevision = production ? process.argv[4] : 'HEAD';
assert(sourceRevision, 'production measurement requires the built source revision');
const source = {
    commitSha: execFileSync('git', ['rev-parse', sourceRevision], { encoding: 'utf8' }).trim(),
    dirty: execFileSync('git', ['status', '--porcelain'], { encoding: 'utf8' }).trim().length > 0,
};
// Match the directory being served, including retained before/after artifacts.
const wasmPath = production ? join(process.argv[5] ?? 'build/web/site', 'kisakcod.wasm')
    : 'build/web-diagnostics/site-diagnostics/kisakcod.wasm';
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
        await engineWorker.evaluate(() => {
            globalThis.__cleanFrames = [];
            globalThis.__workloadViews = [];
            let generation = 0;
            const view = event => {
                const detail = event.detail;
                if (!detail.worldName?.toLowerCase().includes('cargoship')) return;
                generation = detail.submissionGeneration;
                if (generation >= 60 && generation <= 360) __workloadViews.push(structuredClone(detail));
            };
            const sample = event => {
                if (generation < 60) return;
                __cleanFrames.push({ at: performance.now(), generation: event.detail.framePumpTicks });
                if (generation >= 360) {
                    removeEventListener('kisakcod:system', sample);
                    removeEventListener('kisakcod:renderer-scene-view', view);
                }
            };
            addEventListener('kisakcod:renderer-scene-view', view);
            addEventListener('kisakcod:system', sample);
        });
    }
    // fixedtime is a canonical cheat dvar; devmap enables it through the normal
    // engine command path. No diagnostic exports or memory writes are used.
    await page.locator('#engine-command-input').fill(controlled ? 'devmap cargoship; fixedtime 16' : 'map cargoship');
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
    while (await engineWorker.evaluate(() => __cleanFrames.length) < 301) {
        assert(Date.now() < cleanDeadline, 'profiling-disabled window completed within 60 seconds');
        await page.waitForTimeout(1000);
    }
    const cleanFrames = await engineWorker.evaluate(() => __cleanFrames);
    assert.equal(cleanFrames.length, 301, 'exactly 300 consecutive completed callback intervals');
    const workload = controlled ? validateWorkload(await engineWorker.evaluate(() => __workloadViews)) : undefined;
    const cleanForeground = summarizeForegroundSamples(await page.evaluate(() => {
        __dobj.collecting = false;
        __dobj.foreground.push({ observedMs: performance.now(), visibilityState: document.visibilityState,
            pageFocused: document.hasFocus() });
        return __dobj.foreground;
    }));
    assert(cleanForeground.performanceWindowValid);
    assert.equal(await page.evaluate(() => __dobj.profiles.length), 0, 'profiler stayed inactive');
    const cleanIntervals = cleanFrames.slice(1).map((frame, index) => {
        assert.equal(frame.generation, cleanFrames[index].generation + 1, 'no skipped canonical views');
        return frame.at - cleanFrames[index].at;
    });
    const cleanTiming = { intervals: summarizeProfileSamples(cleanIntervals), foreground: cleanForeground,
        clock: production ? 'Worker performance.now at completed main-loop callbacks'
            : 'Worker performance.now at completed canonical render events',
        profilerActive: false, diagnosticBuild: !production, displayedFps: false };
    console.log('CLEAN_TIMING', JSON.stringify(cleanTiming));
    if (production) {
        assert.equal(pageErrors.length, 0);
        const result = { source, artifactSha256, cleanTiming, workload, pageErrorCount: 0,
            recordedAtUtc: new Date().toISOString(),
            environment: { browser: 'Chrome', version: browser.version(), headless: true,
                processor: cpus()[0].model, viewport: { width: 1440, height: 1000 }, build: 'Release production' },
            methodology: { map: 'cargoship', warmupWorldFrames: controlled ? 60 : 30, profilingDisabledIntervals: 300,
                command: controlled ? 'devmap cargoship; fixedtime 16' : 'map cargoship',
                input: 'No gameplay input; authored scene continues running', displayedFps: false } };
        await writeFile(`build/renderer-efficiency-${source.commitSha.slice(0, 8)}-${runLabel}.json`,
            `${JSON.stringify(result, null, 2)}\n`);
    } else {
    stage = 'profile';
    await page.bringToFront();
    const started = await page.evaluate(async () => {
        __dobj.profiles = [];
        __dobj.foreground = [{ observedMs: performance.now(), visibilityState: document.visibilityState,
            pageFocused: document.hasFocus() }];
        __dobj.collecting = true;
        const startedMs = performance.now();
        const accepted = await __KISAKCOD_WEB__.module.call('_KisakWeb_TestBeginFrameProfileWithTimeout', 120, 30000);
        return { startedMs, accepted };
    });
    assert.equal(started.accepted, 1);
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
    const sceneFields = ['sceneSetupMs', 'dobjBuildMs', 'sceneAssemblyMs',
        'sceneImageResolveMs', 'sceneDynamicSubmitMs', 'sceneCameraVisibilityMs', 'sceneViewSubmitMs'];
    const assemblyFields = ['sceneEffectsPrepareMs', 'sceneModelBuildMs', 'sceneCommandAppendMs'];
    const brushFields = ['sceneBrushRemapMs', 'sceneBrushGeometryMs', 'sceneBrushMaterialMs', 'sceneBrushAppendMs'];
    const dynamicFields = ['dynamicCopyMs', 'dynamicGeometryUploadMs', 'dynamicTextureUploadMs', 'dynamicPublishMs'];
    const commandFields = ['commandGeometryCheckMs', 'commandGeometryCopyMs', 'commandBatchCopyMs'];
    for (const field of [...sceneFields, ...assemblyFields, ...brushFields, ...dynamicFields, ...commandFields, 'sceneCloudAppendMs',
        'dobjPoseMs', 'dobjLightingMs', 'dobjSkinningMs', 'dobjGeometryMs',
        'dobjVertexEmitMs', 'dobjIndexEmitMs', 'sceneBrushBuildMs'])
        assert.equal(profile.cpu[field]?.sampleCount, 120, field);
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
    const result = { schemaVersion: 1, cleanTiming, artifactSha256, recordedAtUtc: new Date().toISOString(), source,
        environment: { browser: 'Chrome', version: browser.version(), headless: true,
            processor: cpus()[0].model, totalSystemMemoryBytes: totalmem(), viewport: { width: 1440, height: 1000 },
            build: 'Release diagnostics' },
        methodology: { map: 'cargoship', warmupWorldFrames: 30, completedGameplayFrames: 120, profilingDisabledIntervals: 300,
            input: 'No gameplay input; authored scene continues running', cleanBenchmark: true,
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
    console.error(JSON.stringify(await page.evaluate(() => ({ state: globalThis.__KISAKCOD_WEB__?.state,
        assets: globalThis.__KISAKCOD_WEB__?.assets?.state,
        worldFrames: globalThis.__dobj?.frames.length })).catch(() => null)));
    process.exitCode = 1;
} finally {
    clearInterval(progressTimer);
    await context.close();
    await browser.close();
}
