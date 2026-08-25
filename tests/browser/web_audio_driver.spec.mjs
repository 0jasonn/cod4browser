import { expect, test } from "@playwright/test";

test("Web Audio proxy accepts bounded PCM and ignores stale source generations", async ({ page }) => {
    await page.goto("/");
    const result = await page.evaluate(async () => {
        const { WebAudioDriver } = await import("/web_audio_driver.mjs");
        class Node {
            constructor() {
                this.gain = { value: 1 }; this.playbackRate = { value: 1 };
                this.connections = [];
            }
            connect(target) { this.connections.push(target); }
            disconnect() { this.disconnected = true; }
            start(when = 0, offset = 0) {
                this.started = true; this.startWhen = when; this.startOffset = offset;
            }
            stop() { this.onended?.(); }
        }
        class Context {
            constructor({ gain = true, panner = true } = {}) {
                this.currentTime = 0; this.state = "suspended"; this.destination = {};
                this.hasGain = gain; this.hasPanner = panner;
            }
            createBuffer(channels, frames, rate) {
                return { duration: frames / rate, getChannelData: () => new Float32Array(frames) };
            }
            createBufferSource() {
                const node = new Node(); this.lastSource = node;
                (this.sources ??= []).push(node); return node;
            }
            createGain() { if (!this.hasGain) return null; const node = new Node(); this.lastGain = node; return node; }
            createPanner() {
                if (!this.hasPanner) return null;
                const node = new Node();
                node.positionX = { value: 0 };
                node.positionY = { value: 0 };
                node.positionZ = { value: 0 };
                this.lastPanner = node;
                return node;
            }
            resume() { this.state = "running"; return Promise.resolve(); }
            close() { return Promise.resolve(); }
        }
        const startedEvents = [];
        const driver = new WebAudioDriver({
            contextFactory: () => new Context(),
            onPlaybackStarted: (detail) => startedEvents.push(detail),
        });
        const pcm = new Uint8Array([0, 0, 0, 0]).buffer;
        const accepted = [
            driver.handleCommand({ version: 1, op: "source-create", id: 1 }),
            driver.handleCommand({ version: 1, op: "buffer-upload", bufferId: 1,
                format: 0x1101, bytes: 4, rate: 44100, pcm }),
            driver.handleCommand({ version: 1, op: "source-property", sourceId: 1,
                generation: 0, bufferId: 1, gain: 0.5, pitch: 1, looping: false,
                offset: 0, x: 12, y: -4, z: 3, spatialized: true }),
            driver.handleCommand({ version: 1, op: "source-play", sourceId: 1,
                generation: 1, bufferId: 1, gain: 0.5, pitch: 1, looping: false,
                offset: 0, x: 12, y: -4, z: 3, aliasName: "weapon_test_fire" }),
        ];
        const stateAfterPlay = driver.sources.get(1).state;
        const source = driver.sources.get(1);
        const graph = {
            sourceToPanner: driver.context.lastSource.connections.includes(driver.context.lastPanner),
            pannerToGain: driver.context.lastPanner.connections.includes(driver.context.lastGain),
            gainToDestination: driver.context.lastGain.connections.includes(driver.context.destination),
            position: [driver.context.lastPanner.positionX.value,
                driver.context.lastPanner.positionY.value,
                driver.context.lastPanner.positionZ.value],
        };
        const firstNodes = {
            source: driver.context.lastSource,
            gain: driver.context.lastGain,
            panner: driver.context.lastPanner,
        };
        firstNodes.source.onended();
        const naturalEnd = {
            state: source.state,
            activeBufferId: source.activeBufferId,
            disconnected: [firstNodes.source, firstNodes.gain, firstNodes.panner]
                .every((node) => node.disconnected),
        };
        const staleProperty = driver.handleCommand({ version: 1, op: "source-property", sourceId: 1,
            generation: 0, bufferId: 0, gain: 0.1, pitch: 2, looping: false,
            offset: 0, x: 0, y: 0, z: 0 });
        const stalePropertyState = { gain: source.gain, bufferId: source.bufferId };
        const stale = driver.handleCommand({ version: 1, op: "source-stop", sourceId: 1,
            generation: 0 });
        const invalid = driver.handleCommand({ version: 1, op: "buffer-upload", bufferId: 2,
            format: 0x1101, bytes: 4, rate: 44100, pcm: new ArrayBuffer(3) });
        driver.attachGestureResume(globalThis);
        globalThis.dispatchEvent(new Event("pointerdown"));
        await new Promise((resolve) => setTimeout(resolve, 0));
        const gestureResumed = driver.context.state === "running";
        await driver.resumeFromGesture();
        driver.handleCommand({ version: 1, op: "source-pause", sourceId: 1, generation: 1 });
        const paused = source.state;
        driver.handleCommand({ version: 1, op: "source-play", sourceId: 1, generation: 2,
            bufferId: 1, gain: 0.5, pitch: 1, looping: false, offset: 0, x: 0, y: 0, z: 0 });
        const resumed = source.state;
        driver.handleCommand({ version: 1, op: "source-stop", sourceId: 1, generation: 3 });
        const stopped = source.state;
        driver.handleCommand({ version: 1, op: "buffer-delete", id: 1 });
        const deleted = { state: source.state, bufferId: source.bufferId,
            activeBufferId: source.activeBufferId };
        const contextBeforeReset = driver.context;
        driver.handleCommand({ version: 1, op: "source-create", id: 1 });
        driver.handleCommand({ version: 1, op: "buffer-upload", bufferId: 1,
            format: 0x1101, bytes: 4, rate: 44100, pcm });
        driver.handleCommand({ version: 1, op: "source-property", sourceId: 1,
            generation: 0, bufferId: 1, gain: 0.5, pitch: 1, looping: false,
            offset: 0, x: 0, y: 0, z: 0, spatialized: true });
        driver.handleCommand({ version: 1, op: "source-play", sourceId: 1,
            generation: 1, bufferId: 1, gain: 0.5, pitch: 1, looping: false,
            offset: 0, x: 0, y: 0, z: 0 });
        driver.handleCommand({ version: 1, op: "device-reset" });
        const reset = { sources: driver.sources.size, buffers: driver.buffers.size,
            contextReused: driver.context === contextBeforeReset };
        driver.handleCommand({ version: 1, op: "source-create", id: 1 });
        driver.handleCommand({ version: 1, op: "buffer-upload", bufferId: 1,
            format: 0x1101, bytes: 4, rate: 44100, pcm });
        driver.handleCommand({ version: 1, op: "source-property", sourceId: 1,
            generation: 0, bufferId: 1, gain: 0.5, pitch: 1, looping: false,
            offset: 0, x: 0, y: 0, z: 0 });
        const reinit = driver.handleCommand({ version: 1, op: "source-play", sourceId: 1,
            generation: 1, bufferId: 1, gain: 0.5, pitch: 1, looping: false,
            offset: 0, x: 0, y: 0, z: 0 });
        const graphVariants = [];
        for (const options of [{ gain: true, panner: false },
            { gain: false, panner: true }, { gain: false, panner: false }]) {
            const variant = new WebAudioDriver({ contextFactory: () => new Context(options) });
            variant.handleCommand({ version: 1, op: "source-create", id: 1 });
            variant.handleCommand({ version: 1, op: "buffer-upload", bufferId: 1,
                format: 0x1101, bytes: 4, rate: 44100, pcm });
            variant.handleCommand({ version: 1, op: "source-property", sourceId: 1,
                generation: 0, bufferId: 1, gain: 1, pitch: 1, looping: false,
                offset: 0, x: 0, y: 0, z: 0, spatialized: true });
            variant.handleCommand({ version: 1, op: "source-play", sourceId: 1,
                generation: 1, bufferId: 1, gain: 1, pitch: 1, looping: false,
                offset: 0, x: 0, y: 0, z: 0 });
            const variantContext = variant.context;
            const variantSource = variantContext.lastSource;
            const variantGain = variantContext.lastGain;
            const variantPanner = variantContext.lastPanner;
            graphVariants.push({
                sourceToDestination: variantSource.connections.includes(variantContext.destination),
                sourceToGain: variantGain ? variantSource.connections.includes(variantGain) : false,
                sourceToPanner: variantPanner ? variantSource.connections.includes(variantPanner) : false,
                gainToDestination: variantGain ? variantGain.connections.includes(variantContext.destination) : false,
                pannerToDestination: variantPanner ? variantPanner.connections.includes(variantContext.destination) : false,
            });
            variant.dispose();
        }
        const direct2D = new WebAudioDriver({ contextFactory: () => new Context() });
        direct2D.handleCommand({ version: 1, op: "source-create", id: 1 });
        direct2D.handleCommand({ version: 1, op: "buffer-upload", bufferId: 1,
            format: 0x1103, bytes: 4, rate: 44100, pcm });
        direct2D.handleCommand({ version: 1, op: "source-property", sourceId: 1,
            generation: 0, bufferId: 1, gain: 1, pitch: 1, looping: false,
            offset: 0, x: 0, y: 0, z: 0, spatialized: false });
        direct2D.handleCommand({ version: 1, op: "source-play", sourceId: 1,
            generation: 1, bufferId: 1, gain: 1, pitch: 1, looping: false,
            offset: 0, x: 0, y: 0, z: 0 });
        const direct2DGraph = {
            pannerCreated: Boolean(direct2D.context.lastPanner),
            sourceToGain: direct2D.context.lastSource.connections.includes(
                direct2D.context.lastGain),
            gainToDestination: direct2D.context.lastGain.connections.includes(
                direct2D.context.destination),
        };
        direct2D.dispose();
        const streamStarted = [];
        const stream = new WebAudioDriver({
            contextFactory: () => new Context(),
            onPlaybackStarted: (detail) => streamStarted.push(detail),
        });
        stream.handleCommand({ version: 1, op: "source-create", id: 1 });
        for (const bufferId of [1, 2, 3]) {
            stream.handleCommand({ version: 1, op: "buffer-upload", bufferId,
                format: 0x1101, bytes: 4, rate: 4, pcm });
        }
        const queued = stream.handleCommand({ version: 1, op: "source-queue",
            sourceId: 1, generation: 0, bufferIds: [1, 2] });
        stream.handleCommand({ version: 1, op: "source-property", sourceId: 1,
            generation: 0, bufferId: 0, gain: 0.25, pitch: 1, looping: false,
            offset: 0, x: 0, y: 0, z: 0, spatialized: false,
            queueProcessed: 0 });
        const streamPlayed = stream.handleCommand({ version: 1, op: "source-play",
            sourceId: 1, generation: 1, bufferId: 0, gain: 0.25, pitch: 1,
            looping: false, offset: 0, x: 0, y: 0, z: 0,
            aliasName: "voice_test", spatialized: false, queueProcessed: 0 });
        const refilled = stream.handleCommand({ version: 1, op: "source-queue",
            sourceId: 1, generation: 1, bufferIds: [3] });
        const streamSource = stream.sources.get(1);
        const streamGraph = {
            queued: streamSource.queue.map((entry) => entry.bufferId),
            nodeCount: stream.context.sources.length,
            starts: stream.context.sources.map((node) => node.startWhen),
            allToGain: stream.context.sources.every((node) =>
                node.connections.includes(stream.context.lastGain)),
        };
        stream.context.currentTime = 0.25;
        stream.handleCommand({ version: 1, op: "source-pause", sourceId: 1,
            generation: 1, offset: 0.25, queueProcessed: 0 });
        const streamPaused = streamSource.state;
        const streamResumed = stream.handleCommand({ version: 1, op: "source-play",
            sourceId: 1, generation: 2, bufferId: 0, gain: 0.25, pitch: 1,
            looping: false, offset: 0.25, x: 0, y: 0, z: 0,
            aliasName: "voice_test", spatialized: false, queueProcessed: 0 });
        const resumedNodes = stream.context.sources.slice(3);
        const streamPauseResume = {
            paused: streamPaused,
            resumed: streamSource.state,
            accepted: streamResumed,
            starts: resumedNodes.map((node) => node.startWhen),
            offsets: resumedNodes.map((node) => node.startOffset),
        };
        const unqueued = stream.handleCommand({ version: 1,
            op: "source-unqueue", sourceId: 1, generation: 2, bufferIds: [1] });
        const queueAfterUnqueue = streamSource.queue.map((entry) => entry.bufferId);
        stream.dispose();
        driver.dispose();
        const failedStarts = [];
        const failedDriver = new WebAudioDriver({
            contextFactory: () => new Context(),
            onPlaybackStarted: (detail) => failedStarts.push(detail),
        });
        failedDriver.handleCommand({ version: 1, op: "source-create", id: 1 });
        failedDriver.handleCommand({ version: 1, op: "source-property", sourceId: 1,
            generation: 0, bufferId: 1, aliasName: "failed" });
        const failedStart = failedDriver.handleCommand({ version: 1, op: "source-play",
            sourceId: 1, generation: 1, bufferId: 1, aliasName: "failed" });
        return { accepted, stateAfterPlay, graph, staleProperty, stalePropertyState, stale,
            naturalEnd, gestureResumed, invalid, paused, resumed, stopped, deleted, reset, reinit, graphVariants,
            direct2DGraph, queued, streamPlayed, refilled, streamGraph,
            streamStarted, streamPauseResume, unqueued, queueAfterUnqueue,
            startedEvents, failedStart, failedStarts,
            disposed: driver.sources.size === 0 };
    });
    expect(result.accepted).toEqual([true, true, true, true]);
    expect(result.stateAfterPlay).toBe("playing");
    expect(result.startedEvents).toEqual([
        { sourceId: 1, generation: 1, bufferId: 1,
            aliasName: "weapon_test_fire", spatialized: true,
            position: { x: 12, y: -4, z: 3 }, gain: 0.5, pitch: 1,
            contextState: "suspended" },
        { sourceId: 1, generation: 2, bufferId: 1,
            aliasName: "weapon_test_fire", spatialized: true,
            position: { x: 0, y: 0, z: 0 }, gain: 0.5, pitch: 1,
            contextState: "running" },
        { sourceId: 1, generation: 1, bufferId: 1,
            aliasName: "", spatialized: false,
            position: { x: 0, y: 0, z: 0 }, gain: 0.5, pitch: 1,
            contextState: "running" },
    ]);
    expect(result.graph).toEqual({ sourceToPanner: true, pannerToGain: true,
        gainToDestination: true, position: [12, -4, 3] });
    expect(result.naturalEnd).toEqual({ state: "stopped", activeBufferId: 0, disconnected: true });
    expect(result.gestureResumed).toBe(true);
    expect(result.staleProperty).toBe(true);
    expect(result.stalePropertyState).toEqual({ gain: 0.5, bufferId: 1 });
    expect(result.stale).toBe(true);
    expect(result.invalid).toBe(false);
    expect(result.paused).toBe("paused");
    expect(result.resumed).toBe("playing");
    expect(result.stopped).toBe("stopped");
    expect(result.deleted).toEqual({ state: "stopped", bufferId: 0, activeBufferId: 0 });
    expect(result.reset).toEqual({ sources: 0, buffers: 0, contextReused: true });
    expect(result.reinit).toBe(true);
    expect(result.graphVariants).toEqual([
        { sourceToDestination: false, sourceToGain: true, sourceToPanner: false,
            gainToDestination: true, pannerToDestination: false },
        { sourceToDestination: false, sourceToGain: false, sourceToPanner: true,
            gainToDestination: false, pannerToDestination: true },
        { sourceToDestination: true, sourceToGain: false, sourceToPanner: false,
            gainToDestination: false, pannerToDestination: false },
    ]);
    expect(result.direct2DGraph).toEqual({
        pannerCreated: false,
        sourceToGain: true,
        gainToDestination: true,
    });
    expect(result.queued).toBe(true);
    expect(result.streamPlayed).toBe(true);
    expect(result.refilled).toBe(true);
    expect(result.streamGraph).toEqual({
        queued: [1, 2, 3],
        nodeCount: 3,
        starts: [0, 0.5, 1],
        allToGain: true,
    });
    expect(result.streamStarted).toEqual([{
        sourceId: 1, generation: 1, bufferId: 1,
        aliasName: "voice_test", spatialized: false,
        position: { x: 0, y: 0, z: 0 }, gain: 0.25, pitch: 1,
        streaming: true, contextState: "suspended",
    }, {
        sourceId: 1, generation: 2, bufferId: 1,
        aliasName: "voice_test", spatialized: false,
        position: { x: 0, y: 0, z: 0 }, gain: 0.25, pitch: 1,
        streaming: true, contextState: "suspended",
    }]);
    expect(result.streamPauseResume).toEqual({
        paused: "paused", resumed: "playing", accepted: true,
        starts: [0.25, 0.5, 1], offsets: [0.25, 0, 0],
    });
    expect(result.unqueued).toBe(true);
    expect(result.queueAfterUnqueue).toEqual([2, 3]);
    expect(result.failedStart).toBe(false);
    expect(result.failedStarts).toEqual([]);
    expect(result.disposed).toBe(true);
});

test("Web Audio proxy bounds decoded PCM and per-source stream queues", async ({ page }) => {
    await page.goto("/");
    const result = await page.evaluate(async () => {
        const { WebAudioDriver } = await import("/web_audio_driver.mjs");
        class Context {
            constructor() {
                this.currentTime = 0;
                this.state = "running";
                this.destination = {};
            }
            createBuffer(channels, frames, rate) {
                return { duration: frames / rate,
                    getChannelData: () => new Float32Array(frames) };
            }
            close() { return Promise.resolve(); }
        }
        const telemetry = [];
        const driver = new WebAudioDriver({
            contextFactory: () => new Context(),
            decodedPcmBudgetBytes: 16,
            maxQueuedBuffersPerSource: 1,
            onTelemetry: (detail) => telemetry.push(detail),
        });
        const pcm = new Uint8Array([0, 0, 0, 0]).buffer;
        const upload = (bufferId) => driver.handleCommand({
            version: 1, op: "buffer-upload", bufferId,
            format: 0x1101, bytes: 4, rate: 44100, pcm,
        });
        const uploaded = [upload(1), upload(2), upload(3)];
        const afterEviction = [...driver.buffers.keys()];
        driver.handleCommand({ version: 1, op: "source-create", id: 1 });
        driver.handleCommand({ version: 1, op: "source-property", sourceId: 1,
            generation: 0, bufferId: 2, gain: 1, pitch: 1, looping: false,
            offset: 0, x: 0, y: 0, z: 0 });
        const firstQueue = driver.handleCommand({ version: 1, op: "source-queue",
            sourceId: 1, generation: 0, bufferIds: [3] });
        const queueOverrun = driver.handleCommand({ version: 1, op: "source-queue",
            sourceId: 1, generation: 0, bufferIds: [3] });
        const oversizedPcm = new Uint8Array(10).buffer;
        const memoryOverrun = driver.handleCommand({
            version: 1, op: "buffer-upload", bufferId: 4,
            format: 0x1101, bytes: 10, rate: 44100, pcm: oversizedPcm,
        });
        const finalTelemetry = telemetry.at(-1);
        driver.dispose();
        return { uploaded, afterEviction, firstQueue, queueOverrun,
            memoryOverrun, finalTelemetry };
    });
    expect(result.uploaded).toEqual([true, true, true]);
    expect(result.afterEviction).toEqual([2, 3]);
    expect(result.firstQueue).toBe(true);
    expect(result.queueOverrun).toBe(false);
    expect(result.memoryOverrun).toBe(false);
    expect(result.finalTelemetry).toMatchObject({
        decodedPcmBytes: 16,
        decodedPcmBudgetBytes: 16,
        bufferCount: 2,
        sourceCount: 1,
        queuedBufferCount: 1,
        evictions: 1,
        overruns: 2,
    });
});

test("Worker audio proxy crosses one platform PCM command without gameplay state", async ({ page }) => {
    await page.goto("/");
    await page.waitForFunction(() => globalThis.__KISAKCOD_WEB__?.module?.ready);
    const result = await page.evaluate(async () => {
        const host = globalThis.__KISAKCOD_WEB__.module;
        let crossed = false;
        const original = host.audioDriver.handleCommand.bind(host.audioDriver);
        host.audioDriver.handleCommand = (command) => {
            if (command.op === "buffer-upload") crossed = true;
            return original(command);
        };
        const accepted = await host.call("_KisakWeb_TestAudioProxyPcm");
        const deadline = performance.now() + 1000;
        while (!crossed && performance.now() < deadline)
            await new Promise((resolve) => setTimeout(resolve, 10));
        return { accepted, crossed };
    });
    expect(result).toEqual({ accepted: 1, crossed: true });
});

test("Unavailable native cinematics complete and publish an explicit omission", async ({ page }) => {
    await page.goto("/");
    await page.waitForFunction(() => globalThis.__KISAKCOD_WEB__?.module?.ready);
    const result = await page.evaluate(async () => {
        const event = new Promise((resolve) => globalThis.addEventListener(
            "kisakcod:cinematic", ({ detail }) => resolve(detail), { once: true }));
        const finished = await globalThis.__KISAKCOD_WEB__.module.call(
            "_KisakWeb_DiagnosticCinematicOmission");
        return { finished, detail: await event };
    });
    expect(result).toEqual({
        finished: 1,
        detail: {
            state: "skipped",
            name: "diagnostic_intro",
            reason: "native-bink-unavailable",
            message: "Cinematic playback is unavailable in the browser build; continuing gameplay",
        },
    });
});
