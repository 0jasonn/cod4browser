import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { dirname, join, resolve } from 'node:path';
import { cpus, totalmem } from 'node:os';
import { chromium } from '@playwright/test';
import { aggregateGameplayProfile, summarizeProfileSamples } from '../tests/browser/retail_profile_aggregate.mjs';
import { summarizeForegroundSamples } from '../tests/browser/retail_foreground_window.mjs';
import { installFrameTimingCapture, validateFrameTimingCapture, validateGpuDrain,
    validateBenchmarkEnvironment, validateWorkload, canonicalWorkCounts, ACTIVE_WORKLOAD_TIMES,
    validateActiveWorkload, validateActiveProfileViews, summarizeClockAdvancement,
    KILLHOUSE_REALTIME_CAMERA, normalizeRealtimeKillhouseViews, validatePreImportCadence } from './renderer_workload.mjs';

// Local, owned installation only. Evidence contains identities and timings, never assets.
const [runLabel = 'sample', buildKind = 'diagnostics', revision = 'HEAD', requestedSite,
    mode = 'active', option] = process.argv.slice(2);
assert(/^[a-z0-9-]+$/.test(runLabel));
assert(['production', 'diagnostics'].includes(buildKind));
assert(['fixedtime', 'active', 'realtime'].includes(mode));
assert(!option || ['recovery', 'uncapped', 'moving-camera'].includes(option));
const production = buildKind === 'production';
const paused = mode === 'fixedtime';
const realtime = mode === 'realtime';
const timeWindow = !paused && !realtime ? ACTIVE_WORKLOAD_TIMES : null;
const map = process.env.KISAK_PROFILE_MAP ?? 'cargoship';
assert(['cargoship', 'killhouse'].includes(map));
const realtimeCamera = realtime && map === 'killhouse' ? KILLHOUSE_REALTIME_CAMERA : null;
assert(!paused || map === 'cargoship', 'paused camera is defined for Cargoship');
assert(option !== 'recovery' || (!production && paused));
assert(option !== 'moving-camera' || (!production && paused));
const water = process.env.KISAK_PROFILE_WATER;
const cadenceTarget = process.env.KISAK_PROFILE_CADENCE_MS === undefined
    ? undefined : Number(process.env.KISAK_PROFILE_CADENCE_MS);
assert(water === undefined || water === '1', 'KISAK_PROFILE_WATER must be omitted or 1');
const maxFps = Number(option === 'uncapped' ? 0 : process.env.KISAK_PROFILE_MAXFPS ?? 125);
assert(Number.isInteger(maxFps) && maxFps >= 0 && maxFps <= 1000);
const site = resolve(requestedSite ?? (production ? 'build/web/site' : 'build/web-diagnostics/site-diagnostics'));
const receiptPath = process.env.KISAK_PROFILE_RECEIPT ?? join(dirname(site), 'build-receipt.json');
const receipt = JSON.parse(await readFile(receiptPath, 'utf8'));
const commitSha = execFileSync('git', ['rev-parse', revision], { encoding: 'utf8' }).trim();
assert.equal(receipt.revision, commitSha, 'requested source differs from build receipt');
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const benchmarkDriver = { sha256: sha256(await readFile(new URL(import.meta.url))) };
const artifactSha256 = sha256(await readFile(join(site, 'kisakcod.wasm')));
assert.equal(receipt.site?.['kisakcod.wasm']?.sha256, artifactSha256);
execFileSync('python', ['tools/qualify_web_release.py', 'verify-build', receiptPath, site], { stdio: 'inherit' });
const provenance = { verified: true, receiptSha256: sha256(await readFile(receiptPath)), receipt };
const baseUrl = process.env.KISAK_PROFILE_URL ?? 'http://127.0.0.1:8051/';
assert(process.env.KISAK_COD4_RETAIL_ROOT, 'owned installation root is required');
const context = await chromium.launchPersistentContext('', {
    channel: 'chrome', headless: true, args: ['--mute-audio'],
    viewport: { width: 1920, height: 1080 },
    deviceScaleFactor: 1,
});
const browser = context.browser();
const page = context.pages()[0] ?? await context.newPage();
const pageErrors = [];
page.on('pageerror', error => pageErrors.push(error.message));
let stage = 'bootstrap';
let engineWorker;
const progressTimer = setInterval(() => console.log('PROGRESS', stage), 20000);
function readEngineGpu() {
    const gl = __KISAKCOD_OFFSCREEN_CANVAS__.getContext('webgl2');
    const debug = gl.getExtension('WEBGL_debug_renderer_info');
    return { renderer: debug && gl.getParameter(debug.UNMASKED_RENDERER_WEBGL),
        vendor: debug && gl.getParameter(debug.UNMASKED_VENDOR_WEBGL),
        version: gl.getParameter(gl.VERSION), shadingLanguage: gl.getParameter(gl.SHADING_LANGUAGE_VERSION),
        attributes: gl.getContextAttributes() };
}
const command = async text => {
    // Production hides its diagnostic console after mount. Submit through its
    // existing named command handler without changing launcher visibility.
    await page.locator('#engine-command-input').evaluate((input, value) => { input.value = value; }, text);
    await page.locator('#engine-command-form').evaluate(form => form.requestSubmit());
};
async function waitWorker(predicate, argument, timeout = 300000) {
    const deadline = Date.now() + timeout;
    while (!await engineWorker.evaluate(predicate, argument)) {
        assert(Date.now() < deadline, `timed out during ${stage}`);
        assert.equal(pageErrors.length, 0, pageErrors.join('\n'));
        await page.waitForTimeout(500);
    }
}
try {
    await page.addInitScript(() => {
        Object.defineProperty(globalThis, 'showDirectoryPicker', { configurable: true, value: undefined });
        globalThis.__dobj = { frames: [], profiles: [], logs: [], memory: [], foreground: [],
            lifecycle: [], movies: [],
            collecting: false, presentation: [], presentationOverflow: false, graphicsSnapshots: {}, graphicsPhase: null };
        for (const [name, key] of [['engine-lifecycle', 'lifecycle'], ['cinematic', 'movies'],
            ['renderer-scene-frame', 'frames'], ['frame-profile', 'profiles'],
            ['renderer-memory', 'memory']]) addEventListener(`kisakcod:${name}`, event => {
            const list = __dobj[key];
            list.push({ ...structuredClone(event.detail), hostObservedMs: performance.now() });
            if (list.length > 4096) list.shift();
        });
        const NativeWorker = globalThis.Worker;
        globalThis.Worker = class extends NativeWorker {
            constructor(...args) {
                super(...args);
                this.addEventListener('message', event => {
                    if (event.data?.type === 'benchmark-settings-start') {
                        __dobj.graphicsPhase = event.data.phase;
                        __dobj.graphicsSnapshots[event.data.phase] = { settings: {}, dumps: 0 };
                    }
                    if (event.data?.type !== 'log') return;
                    const snapshot = __dobj.graphicsSnapshots[__dobj.graphicsPhase];
                    if (snapshot && snapshot.dumps < 6) {
                        const text = String(event.data.message);
                        for (const match of text.matchAll(/\b((?:r|fx|sm|snd)_[a-zA-Z0-9_]+|cg_drawFPS|com_statmon) "([^"\n]*)"/g))
                            snapshot.settings[match[1]] = match[2];
                        snapshot.dumps += (text.match(/total dvars/g) ?? []).length;
                    }
                    __dobj.logs.push({ message: String(event.data.message), level: event.data.level });
                    if (__dobj.logs.length > 4096) __dobj.logs.shift();
                });
            }
        };
        const sampleFocus = () => {
            if (__dobj.collecting) __dobj.foreground.push({ observedMs: performance.now(),
                visibilityState: document.visibilityState, pageFocused: document.hasFocus() });
        };
        setInterval(sampleFocus, 250);
        for (const name of ['visibilitychange', 'focus', 'blur']) addEventListener(name, sampleFocus);
        const presentation = at => {
            if (__dobj.collecting) {
                if (__dobj.presentation.length < 65536) __dobj.presentation.push(at);
                else __dobj.presentationOverflow = true;
            }
            requestAnimationFrame(presentation);
        };
        requestAnimationFrame(presentation);
    });
    await page.goto(baseUrl);
    // A matching Wasm alone cannot identify host/shader/codec work. Verify every
    // byte in the served receipt inventory, including host and public modules.
    for (const [name, identity] of Object.entries(receipt.site)) {
        const bytes = await readFile(join(site, name));
        assert.equal(sha256(bytes), identity.sha256, `local receipt mismatch: ${name}`);
        const response = await page.request.get(new URL(name, baseUrl).href);
        assert(response.ok(), `missing served receipt file: ${name}`);
        const body = await response.body();
        assert.equal(body.length, bytes.length, `served length differs: ${name}`);
        assert.equal(sha256(body), identity.sha256, `served receipt mismatch: ${name}`);
        await response.dispose();
    }
    await page.waitForFunction(production => production
        ? document.documentElement.dataset.runtimeState === 'running'
        : globalThis.__KISAKCOD_WEB__?.state === 'running', production, { timeout: 60000 });
    engineWorker = page.workers().find(worker => worker.url().includes('engine_worker.mjs'));
    assert(engineWorker, 'engine Worker exists');
    const gpu = await engineWorker.evaluate(readEngineGpu);
    const cdp = await browser.newBrowserCDPSession();
    const { gpu: systemGpu, modelName, modelVersion } = await cdp.send('SystemInfo.getInfo');
    await cdp.detach();
    const display = await page.evaluate(async () => {
        const times = [];
        await new Promise(resolve => {
            const sample = at => { times.push(at); if (times.length === 241) resolve(); else requestAnimationFrame(sample); };
            requestAnimationFrame(sample);
        });
        return { intervals: times.slice(1).map((at, index) => at - times[index]),
            screen: { width: screen.width, height: screen.height, availWidth: screen.availWidth,
                availHeight: screen.availHeight, colorDepth: screen.colorDepth, devicePixelRatio } };
    });
    console.log('BOOTED', JSON.stringify({ gpu, displayCadence: summarizeProfileSamples(display.intervals) }));
    validatePreImportCadence(summarizeProfileSamples(display.intervals), cadenceTarget);
    stage = 'import';
    const pendingChooser = page.waitForEvent('filechooser');
    await page.locator('#portable-install-button').click();
    await (await pendingChooser).setFiles(process.env.KISAK_COD4_RETAIL_ROOT);
    await page.waitForFunction(production => production
        ? document.querySelector('.asset-control')?.dataset.assetState === 'ready' &&
            document.querySelector('#boot-log')?.textContent.includes('Local installation mounted; canonical runtime started.')
        : __KISAKCOD_WEB__?.assets?.state === 'ready' && __KISAKCOD_WEB__?.module?.filesystemState === 'mounted',
        production, { timeout: 300000 });
    stage = 'fixed render resolution';
    await command('r_mode 1920x1080; vid_restart');
    await waitWorker(() => __KISAKCOD_OFFSCREEN_CANVAS__.width === 1920 && __KISAKCOD_OFFSCREEN_CANVAS__.height === 1080);
    // Opening cinematics advance on the real audio device clock. Unlock it
    // through the same trusted canvas gesture used by the product launcher.
    await page.locator('#game-canvas').click({ position: { x: 5, y: 5 } });
    assert.deepEqual(await engineWorker.evaluate(readEngineGpu), gpu, 'engine GPU changed after vid_restart');
    // Warmup captures settings after the map has applied authored state.
    await engineWorker.evaluate(installFrameTimingCapture, { map, realtime, timeWindow });
    await engineWorker.evaluate(async ({ production, paused, realtime, map, movingCamera, timeWindow, realtimeCamera }) => {
        const { ENGINE_PROTOCOL_VERSION } = await import(production ? './product_protocol.mjs' : './engine_protocol.mjs');
        globalThis.__workloadViews = [];
        globalThis.__workloadWarmup = [];
        globalThis.__profileViews = [];
        globalThis.__workloadPauseReady = false;
        let settingsRequested = false;
        let cameraRequested = false;
        const enqueue = (generation, command) => dispatchEvent(new MessageEvent('message', { data: {
            protocolVersion: ENGINE_PROTOCOL_VERSION, id: 1000000 + generation,
            ...(production ? { type: 'submitCanonicalCommand', command }
                : { type: 'probe', functionName: '_KisakWeb_SubmitCanonicalCommand',
                    buffers: [new TextEncoder().encode(`${command}\0`).buffer],
                    argumentLayout: [{ kind: 'pointer', index: 0 }] }),
        } }));
        const commands = paused ? new Map([[30, 'cg_ufo'], [60, 'cg_cinematicFullscreen 0; cl_paused_simple 1; pause'],
            [120, 'cg_setviewpos -9732 -9384 2041 73 16'], [180, 'cg_setviewpos -9732 -9384 2041 73 16']]) : new Map();
        if (movingCamera) for (let frame = 610; frame <= 710; frame += 10)
            commands.set(frame, `cg_setviewpos -9732 -9384 2041 ${frame % 20 === 10 ? 253 : 73} 16`);
        addEventListener('kisakcod:renderer-scene-view', event => {
            const detail = event.detail;
            if (!detail.worldName?.toLowerCase().includes(map)) return;
            const frame = detail.submissionGeneration;
            if (frame === 30 || frame === 60) __workloadWarmup.push(structuredClone(detail));
            if (paused && frame === 61) __workloadPauseReady = true;
            if (realtimeCamera && !cameraRequested && detail.time >= realtimeCamera.atCanonicalTime) {
                cameraRequested = true;
                commands.set(frame, realtimeCamera.command);
            }
            if (!settingsRequested && (realtime ? detail.time >= 44000 : frame === 180)) {
                settingsRequested = true;
                postMessage({ type: 'benchmark-settings-start', phase: 'warmup' });
                const graphics = 'dvarlist r_*; dvarlist fx_*; dvarlist sm_*; dvarlist snd_*; dvarlist cg_drawFPS; dvarlist com_statmon';
                commands.set(frame, commands.has(frame) ? `${commands.get(frame)}; ${graphics}` : graphics);
            }
            if (commands.has(frame)) { enqueue(frame, commands.get(frame)); commands.delete(frame); }
            if (timeWindow ? detail.time >= timeWindow.first && detail.time <= timeWindow.last &&
                (detail.time - timeWindow.first) % (60 * timeWindow.step) === 0
                : !realtime && frame >= 240 && frame <= 540 && frame % 60 === 0)
                __workloadViews.push(structuredClone(detail));
            if (realtimeCamera && __workloadViews.length < 12 &&
                detail.time >= 45000 + __workloadViews.length * 5000)
                __workloadViews.push(structuredClone(detail));
            if (!production && !realtime && (timeWindow ? detail.time === timeWindow.profileFirst - timeWindow.step
                : frame === 600)) dispatchEvent(new MessageEvent('message', { data: {
                protocolVersion: ENGINE_PROTOCOL_VERSION, type: 'call', id: 1000600,
                functionName: '_KisakWeb_TestBeginFrameProfileWithTimeout', arguments: [120, 30000],
            } }));
            if (!production && !realtime && (timeWindow ? detail.time >= timeWindow.profileFirst &&
                detail.time <= timeWindow.profileLast : frame > 600 && frame <= 720))
                __profileViews.push(structuredClone(detail));
        });
    }, { production, paused, realtime, map, movingCamera: option === 'moving-camera', timeWindow, realtimeCamera });
    await page.evaluate(() => {
        __dobj.collecting = true;
        __dobj.foreground = [{ observedMs: performance.now(), visibilityState: document.visibilityState, pageFocused: document.hasFocus() }];
        __dobj.profiles = [];
        __dobj.presentation = [];
    });
    const mapCommand = `${water === '1' ? 'r_drawWater 1; ' : ''}com_maxfps ${maxFps}; set sv_mapSeed 1; set ui_autoContinue 1; devmap ${map}; fixedtime ${realtime ? 0 : 16}${realtime ? '' : '; cg_drawFPS 0'}`;
    stage = `${map} ${mode} window`;
    await command(mapCommand);
    // Canonical UI_AutoContinue releases pregame only when loading finishes.
    // An asynchronous Escape can arrive after that transition and open pause.
    await page.waitForFunction(() =>
        ['CL_InitCGame complete', 'SV_InitGameProgs complete'].every(stage =>
            __dobj.lifecycle.some(event => event.stage === stage)), null, { timeout: 300000 });
    stage = `${map} ${mode} window`;
    let observedPauseState;
    if (paused && !production) {
        stage = 'canonical pause admission';
        await waitWorker(() => __workloadPauseReady);
        observedPauseState = await page.evaluate(() =>
            __KISAKCOD_WEB__.module.call('_KisakWeb_TestUiState', 5));
        assert([1, 2].includes(observedPauseState), 'canonical pause command was rejected');
        stage = `${map} ${mode} window`;
    }
    await waitWorker(() => __frameTimingCapture.complete);
    const frameTiming = await engineWorker.evaluate(() => ({ ...__frameTimingCapture, pendingView: undefined, lastView: undefined }));
    const intervals = summarizeProfileSamples(validateFrameTimingCapture(frameTiming, realtime));
    const [views, warmup] = await engineWorker.evaluate(() => [__workloadViews, __workloadWarmup]);
    let workload;
    if (paused) workload = validateWorkload(views, warmup);
    else if (!realtime) {
        assert.equal(warmup.length, 2);
        assert.equal(warmup[1].time - warmup[0].time, 480, 'fixed simulation time not honored');
        workload = validateActiveWorkload(views, frameTiming);
    } else workload = { mode: 'realtime', requestedMapSeed: 1, fixedtime: 0,
        minimumCanonicalStartTime: 45000, durationMilliseconds: 60000 };
    workload = { ...workload, map, requestedMaxFps: maxFps, requestedIntroSkip: true, introSkipMethod: 'ui_autoContinue',
        movingCamera: option === 'moving-camera',
        ...(!realtime ? { requestedFpsOverlay: 0 } : {}),
        ...(paused ? { requestedCinematicFullscreen: 0, observedPauseState } : {}),
        ...(realtimeCamera ? { requestedCamera: realtimeCamera, trace: views } : {}),
        ...(water === '1' ? { requestedWaterAnimation: true } : {}) };
    if (realtimeCamera) normalizeRealtimeKillhouseViews(workload, frameTiming);
    const cleanTiming = { intervals, averageSubmissionFps: 1000 / intervals.average,
        ...(realtime ? { clockAdvancement: summarizeClockAdvancement(frameTiming) } : {}),
        canonicalFrameCpu: summarizeProfileSamples(frameTiming.samples.map(frame => frame.submissionStarted - frame.simulationStarted)),
        rendererSubmissionCpu: summarizeProfileSamples(frameTiming.samples.map(frame => frame.completed - frame.submissionStarted)),
        totalCpuElapsed: summarizeProfileSamples(frameTiming.samples.map(frame => frame.completed - frame.started)),
        clock: 'Worker performance.now at every completed engine submission; includes gameplay stalls',
        profilerActive: false, diagnosticBuild: !production,
        qualified: realtime && 1000 / intervals.average >= 60 && intervals.p95 <= 20 && intervals.p99 <= 33.3 };
    console.log('CLEAN_TIMING', JSON.stringify(cleanTiming));
    let profile, frameSamples, gpuResults, gpuDrain, workCounts, profileViews;
    if (!production && !realtime) {
        stage = 'diagnostic capture and GPU query drain';
        await page.waitForFunction(() => __dobj.profiles.some(entry => entry.kind === 'capture'), null, { timeout: 60000 });
        // The renderer's bounded query slots keep polling after capture completion.
        // Wait for one terminal result per issued logical stage, including late tails.
        await page.waitForFunction(() => {
            const frames = __dobj.profiles.filter(entry => entry.kind === 'frame');
            return frames.length === 120 && frames.filter(frame => frame.gpu?.queryIssued).every(frame =>
                __dobj.profiles.some(result => result.kind === 'gpu-result' && result.pumpTick === frame.pumpTick &&
                    result.gpu?.stage === frame.gpu.stage));
        }, null, { timeout: 30000 });
        const entries = await page.evaluate(() => __dobj.profiles);
        const capture = entries.find(entry => entry.kind === 'capture');
        assert.equal(capture.profileComplete, true);
        assert.equal(capture.profileSamplesCollected, 120);
        frameSamples = entries.filter(entry => entry.kind === 'frame');
        profileViews = await engineWorker.evaluate(() => __profileViews);
        assert.equal(profileViews.length, 120);
        if (timeWindow) validateActiveProfileViews(profileViews, frameSamples, frameTiming);
        else for (const [index, frame] of frameSamples.entries()) {
            assert.equal(frame.viewSubmissionGeneration, 601 + index);
            assert.equal(profileViews[index].submissionGeneration, 601 + index);
            assert.equal(profileViews[index].time, views.at(-1).time + (paused ? 0 : (61 + index) * 16));
        }
        gpuResults = entries.filter(entry => entry.kind === 'gpu-result');
        gpuDrain = validateGpuDrain(frameSamples, gpuResults);
        workCounts = canonicalWorkCounts(frameSamples);
        profile = aggregateGameplayProfile({ frames: frameSamples, gpuResults, capture,
            cleanAverageFrameIntervalMs: intervals.average, cleanWorkloadMatched: false });
    }
    const observed = await page.evaluate(() => {
        __dobj.collecting = false;
        __dobj.foreground.push({ observedMs: performance.now(), visibilityState: document.visibilityState, pageFocused: document.hasFocus() });
        return { foreground: __dobj.foreground, presentation: __dobj.presentation, timeOrigin: performance.timeOrigin, overflow: __dobj.presentationOverflow };
    });
    assert.equal(observed.overflow, false);
    const graphicsBefore = await page.evaluate(() => __dobj.graphicsSnapshots.warmup);
    assert.equal(graphicsBefore?.dumps, 6, 'warmup graphics snapshot incomplete');
    await engineWorker.evaluate(() => postMessage({ type: 'benchmark-settings-start', phase: 'end' }));
    await command('dvarlist r_*; dvarlist fx_*; dvarlist sm_*; dvarlist snd_*; dvarlist cg_drawFPS; dvarlist com_statmon');
    await page.waitForFunction(() => __dobj.graphicsSnapshots.end?.dumps === 6, null, { timeout: 30000 });
    const graphicsSettings = await page.evaluate(() => __dobj.graphicsSnapshots.end.settings);
    assert.deepEqual(graphicsSettings, graphicsBefore.settings, 'canonical graphics settings changed during capture');
    assert.notEqual(graphicsSettings.cg_drawFPS, undefined, 'developer FPS overlay setting missing');
    if (!realtime) {
        assert.equal(graphicsSettings.cg_drawFPS, 'Off', 'developer FPS overlay changed fixed-work geometry');
    }
    if (water === '1') assert.equal(graphicsSettings.r_drawWater, '1', 'requested water animation was not active');
    const foreground = summarizeForegroundSamples(observed.foreground);
    const windowStart = frameTiming.timeOrigin + frameTiming.samples[0].completed;
    const windowEnd = frameTiming.timeOrigin + frameTiming.samples.at(-1).completed;
    const presentationTimes = observed.presentation.filter(at => observed.timeOrigin + at >= windowStart &&
        observed.timeOrigin + at <= windowEnd);
    assert(presentationTimes.length >= 2, 'main-thread presentation window did not overlap the Worker window');
    assert.deepEqual(await engineWorker.evaluate(readEngineGpu), gpu, 'engine GPU changed during capture');
    if (!production) await page.evaluate(() => __KISAKCOD_WEB__.module.call('_KisakWeb_TestEmitRendererMemory'));
    const memorySamples = await page.evaluate(() => __dobj.memory.map(sample => Object.fromEntries(
        Object.entries(sample).filter(([key, value]) => key === 'state' ||
            typeof value === 'number' && Number.isFinite(value)))));
    assert(memorySamples.length > 0, 'renderer memory evidence missing');
    const heapCapacities = frameTiming.samples.map(frame => frame.wasmHeapCapacityBytes);
    assert(heapCapacities.every(bytes => Number.isSafeInteger(bytes) && bytes > 0), 'Wasm heap evidence missing');
    const memory = { rendererSamples: memorySamples,
        wasmLinearMemoryCapacityHighWaterBytes: Math.max(...heapCapacities),
        wasmLinearMemoryCapacityFirstBytes: heapCapacities[0],
        wasmLinearMemoryCapacityLastBytes: heapCapacities.at(-1),
        scope: 'Renderer counters are separate overlapping populations; Wasm capacity is sampled on every completed clean frame. No summed process-memory total.' };

    const renderSize = await engineWorker.evaluate(() => ({ width: __KISAKCOD_OFFSCREEN_CANVAS__.width, height: __KISAKCOD_OFFSCREEN_CANVAS__.height }));
    const environment = { buildKind, browser: 'Chrome', version: browser.version(), headless: true,
        executionMode: 'headless-muted', audioMuted: true,
        processor: cpus()[0].model, totalSystemMemoryBytes: totalmem(), gpu, systemGpu, modelName, modelVersion,
        viewport: { width: 1920, height: 1080 }, renderSize, screen: display.screen,
        displayCadence: summarizeProfileSamples(display.intervals), displayCadenceTargetMs: cadenceTarget ?? null,
        graphicsSettings, foreground };
    validateBenchmarkEnvironment(environment);
    assert.equal(pageErrors.length, 0, pageErrors.join('\n'));
    let recovery;
    if (option === 'recovery') {
        stage = 'actual context loss and restoration';
        const readShadowSelection = () => page.evaluate(() => Promise.all(Array.from({ length: 9 }, (_, field) =>
            __KISAKCOD_WEB__.module.call('_KisakWeb_TestTransientSpotShadowState', field))));
        const shadowSelection = await readShadowSelection();
        const before = await page.evaluate(() => ({ frame: __dobj.frames.at(-1),
            losses: __KISAKCOD_WEB__.contextLosses ?? 0,
            recoveries: __KISAKCOD_WEB__.rendererSurface.recoveryCount ?? 0 }));
        assert(await page.evaluate(() => __KISAKCOD_WEB__.module.call('_KisakWeb_TestLoseWebGLContext')));
        await page.waitForFunction(() => __KISAKCOD_WEB__.rendererSurface.state === 'lost');
        assert.deepEqual(await readShadowSelection(), shadowSelection, 'context loss changed canonical shadow selection');
        assert(await page.evaluate(() => __KISAKCOD_WEB__.module.call('_KisakWeb_TestRestoreWebGLContext')));
        await page.waitForFunction(before => __dobj.frames.at(-1)?.resourceGeneration > before.frame.resourceGeneration &&
            __dobj.frames.at(-1)?.viewSubmissionGeneration > before.frame.viewSubmissionGeneration + 10,
            before, { timeout: 30000 });
        await page.evaluate(async () => {
            __dobj.profiles = [];
            await __KISAKCOD_WEB__.module.call('_KisakWeb_TestBeginFrameProfileWithTimeout', 12, 30000);
        });
        await page.waitForFunction(() => __dobj.profiles.some(entry => entry.kind === 'capture'), null, { timeout: 30000 });
        const resumed = await page.evaluate(() => ({
            frames: __dobj.profiles.filter(entry => entry.kind === 'frame'),
            capture: __dobj.profiles.find(entry => entry.kind === 'capture'),
            losses: __KISAKCOD_WEB__.contextLosses,
            recoveries: __KISAKCOD_WEB__.rendererSurface.recoveryCount }));
        assert(resumed.capture.profileComplete);
        assert.equal(resumed.frames.length, 12);
        assert(resumed.losses > before.losses && resumed.recoveries > before.recoveries);
        assert.deepEqual(await readShadowSelection(), shadowSelection, 'context restoration changed canonical shadow selection');
        assert.deepEqual(await engineWorker.evaluate(readEngineGpu), gpu, 'recovery changed the engine GPU');
        await writeFile(`build/renderer-recovery-${runLabel}.json`, JSON.stringify({
            before: frameSamples, beforeViews: profileViews, after: resumed.frames,
            losses: resumed.losses, recoveries: resumed.recoveries }, null, 2));
        for (const counts of canonicalWorkCounts(resumed.frames)) assert.deepEqual(counts, workCounts[0]);
        recovery = { passed: true, matchingWorkSamples: 12, actualContextLoss: true, shadowSelection };
        assert.equal(pageErrors.length, 0);
    }
    const result = { schemaVersion: 2, benchmarkDriver, recordedAtUtc: new Date().toISOString(), source: { commitSha, dirty: receipt.dirty },
        artifactSha256, provenance, environment, workload, frameTiming, cleanTiming,
        recovery, memory, profile, gpuDrain, gpuResults, frameSamples, profileViews, workCounts, pageErrorCount: pageErrors.length,
        presentation: { evidence: 'main-thread requestAnimationFrame opportunities only; not physical scanout or proof each game frame was presented',
            windowStartEpochMs: windowStart, windowEndEpochMs: windowEnd,
            intervals: summarizeProfileSamples(presentationTimes.slice(1).map((at, index) => at - presentationTimes[index])) },
        methodology: { command: mapCommand, warmupWorldFrames: frameTiming.samples[0].scene.generation,
            introSkipMethod: 'canonical UI_AutoContinue',
            canonicalWorkWindow: timeWindow, engineWorker: true,
            simulation: 'canonical fixedtime 16 for matched work; fixedtime 0 for realtime qualification',
            canonicalFrameCpuScope: 'canonical server/client frame including scene/frontend, uploads and sound; rendererSubmissionCpu measures the later WebGL draw submission separately',
            input: paused ? 'Canonical paused free camera' : realtimeCamera
                ? 'Defined idle realtime Killhouse view; one canonical warmup camera command, live physics and scripts'
                : 'Seeded authored gameplay, no external input' } };
    await writeFile(`build/renderer-efficiency-${commitSha.slice(0, 8)}-${runLabel}.json`, `${JSON.stringify(result, null, 2)}\n`);
    console.log('COMPLETE', JSON.stringify({ cleanTiming, gpuDrain, artifactSha256 }));
} catch (error) {
    console.error('PROFILE_FAILED', stage, error.stack);
    if (engineWorker) console.error('FRAME_CAPTURE_STATE', await engineWorker.evaluate(() => {
        const capture = globalThis.__frameTimingCapture;
        return capture && { startupPhase: capture.startupPhase, invalidReason: capture.invalidReason,
            samples: capture.samples.length, complete: capture.complete };
    }).catch(() => null));
    console.error('ENGINE_ERRORS', await page.evaluate(() => globalThis.__dobj?.logs
        .filter(log => /error|failed|invalid|assert/i.test(log.text ?? log.message ?? '')).slice(-8)).catch(() => null));
    process.exitCode = 1;
} finally {
    clearInterval(progressTimer);
    await context.close();
    await browser.close();
}
