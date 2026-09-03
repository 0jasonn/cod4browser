import { expect, test } from "@playwright/test";

test("canonical wet sends reach the OpenAL worklet with room, stereo and position intact", async ({ page }) => {
    await page.goto("/");
    const result = await page.evaluate(async () => {
        const { WebAudioDriver } = await import("/web_audio_driver.mjs");
        async function render({ wet = 1, room = 0, stereo = false, x = 0, queued = false, eq = false } = {}) {
            const rate = 48000, context = new OfflineAudioContext(2, rate, rate);
            const errors = [];
            const driver = new WebAudioDriver({ contextFactory: () => context,
                startMuted: false, onDiagnostic: (message) => errors.push(message) });
            const send = (op, data = {}) => driver.handleCommand({ version: 1, op, ...data });
            if (!send("room-type", { id: room }) || !await driver.ensureReverb())
                throw new Error(errors.join("; ") || "Reverb did not initialize");
            const memory = driver.publishTelemetry().reverbMemoryBytes;
            const pcm = new Int16Array(rate * (stereo ? 2 : 1));
            pcm[4800 * (stereo ? 2 : 1)] = 16384;
            if (stereo) pcm[4800 * 2 + 1] = -16384;
            send("source-create", { id: 1 });
            send("buffer-upload", { bufferId: 1, pcm: pcm.buffer, bytes: pcm.byteLength,
                format: stereo ? 0x1103 : 0x1101, rate });
            if (queued) send("source-queue", { sourceId: 1, generation: 0, bufferIds: [1] });
            send("source-property", { sourceId: 1, generation: 0, bufferId: queued ? 0 : 1,
                wet, gain: 1, x, y: 0, z: 0, spatialized: x !== 0 });
            if (eq) send("source-eq", { sourceId: 1, generation: 0,
                bands: [1, 0, 0, 1000, 0.7071, ...new Array(25).fill(0)] });
            send("source-play", { sourceId: 1, generation: 1 });
            const playingNode = queued ? driver.sources.get(1).streamNodes[0] : driver.sources.get(1).node;
            const rejected = !send("source-property", { sourceId: 1, generation: 1, wet: NaN });
            const preserved = driver.sources.get(1).wet === wet;
            send("source-property", { sourceId: 1, generation: 0, wet: 0 });
            const staleIgnored = driver.sources.get(1).wet === wet;
            const sameNode = playingNode === (queued ? driver.sources.get(1).streamNodes[0] : driver.sources.get(1).node);
            const rendered = await context.startRendering();
            const left = rendered.getChannelData(0), right = rendered.getChannelData(1);
            let energy = 0, late = 0, weighted = 0;
            for (let i = 6000; i < rate; ++i) {
                if (!Number.isFinite(left[i]) || !Number.isFinite(right[i])) throw new Error("Non-finite audio");
                energy += left[i] ** 2 + right[i] ** 2;
                if (i > 24000) late += left[i] ** 2 + right[i] ** 2;
                weighted += (left[i] - right[i]) * Math.sin(i * 0.137);
            }
            const direct = [left[4800], right[4800]];
            driver.dispose();
            return { energy, late, weighted, direct, memory, errors, rejected, preserved, staleIgnored, sameNode };
        }
        return { dry: await render({ wet: 0 }), wet: await render(), quarter: await render({ wet: 0.25 }),
            padded: await render({ room: 1 }), cave: await render({ room: 8 }),
            stereo: await render({ stereo: true }), left: await render({ x: -1 }),
            right: await render({ x: 1 }), queued: await render({ queued: true }), eq: await render({ eq: true }) };
    });
    expect(result.dry.energy).toBe(0);
    expect(result.wet.energy).toBeGreaterThan(1e-5);
    expect(result.quarter.energy / result.wet.energy).toBeCloseTo(1 / 16, 5);
    expect(result.wet.direct).toEqual(result.dry.direct);
    expect(result.cave.late).toBeGreaterThan(result.padded.late * 100 + 1e-8);
    expect(result.stereo.energy).toBeGreaterThan(1e-5);
    expect(Math.abs(result.left.weighted - result.right.weighted)).toBeGreaterThan(1e-5);
    expect(result.queued.energy).toBeCloseTo(result.wet.energy, 7);
    expect(result.eq.energy).toBeLessThan(result.wet.energy * 0.75);
    for (const output of Object.values(result)) {
        expect(output.errors).toEqual([]);
        expect(output.memory).toBe(32 * 1024 * 1024);
        expect(output.rejected && output.preserved && output.staleIgnored && output.sameNode).toBe(true);
    }
});

test("reverb startup failure preserves dry playback and reports the missing device", async ({ page }) => {
    await page.goto("/");
    const result = await page.evaluate(async () => {
        const { WebAudioDriver } = await import("/web_audio_driver.mjs");
        const context = new OfflineAudioContext(1, 4800, 48000), errors = [];
        // Worklet module fetches do not pass through Playwright's page routes.
        // Exercise the browser's real module-load failure with a missing URL.
        const addModule = context.audioWorklet.addModule.bind(context.audioWorklet);
        context.audioWorklet.addModule = () => addModule("/missing-reverb-worklet.mjs");
        const driver = new WebAudioDriver({ contextFactory: () => context, startMuted: false,
            onDiagnostic: (message) => errors.push(message) });
        const command = (op, data = {}) => driver.handleCommand({ version: 1, op, ...data });
        command("room-type", { id: 0 });
        const unavailable = await driver.ensureReverb() === null;
        command("source-create", { id: 1 });
        const pcm = new Int16Array(4800).fill(4096);
        command("buffer-upload", { bufferId: 1, format: 0x1101, rate: 48000,
            bytes: pcm.byteLength, pcm: pcm.buffer });
        command("source-play", { sourceId: 1, generation: 1, bufferId: 1, wet: 1 });
        const rendered = await context.startRendering();
        const sample = rendered.getChannelData(0)[2400];
        driver.dispose();
        return { unavailable, errors, sample };
    });
    expect(result.unavailable).toBe(true);
    expect(result.sample).toBeCloseTo(0.125, 6);
    expect(result.errors.some((message) => message.startsWith("Room reverb unavailable:"))).toBe(true);
});

test("wet changes preserve playback and a reset discards the previous reverb device", async ({ page }) => {
    await page.goto("/");
    const result = await page.evaluate(async () => {
        const { WebAudioDriver } = await import("/web_audio_driver.mjs");
        const context = new OfflineAudioContext(2, 48000, 48000), errors = [];
        const driver = new WebAudioDriver({ contextFactory: () => context, startMuted: false,
            onDiagnostic: (message) => errors.push(message) });
        const send = (op, data = {}) => driver.handleCommand({ version: 1, op, ...data });
        send("room-type", { id: 0 });
        const pending = driver.ensureReverb();
        driver.resetResources();
        const staleDiscarded = await pending === null && driver.reverbNode === null;
        send("room-type", { id: 1 });
        if (!await driver.ensureReverb()) throw new Error(errors.join("; "));
        const oldDevice = driver.reverbNode;
        send("source-create", { id: 1 });
        const pcm = new Int16Array(48000);
        pcm[4800] = pcm[24000] = 16384;
        send("buffer-upload", { bufferId: 1, pcm: pcm.buffer, bytes: pcm.byteLength, format: 0x1101, rate: 48000 });
        send("source-play", { sourceId: 1, generation: 1, bufferId: 1, wet: 1 });
        const source = driver.sources.get(1), initialNode = source.node;
        let preserved = false;
        const change = context.suspend(0.25).then(async () => {
            send("source-property", { sourceId: 1, generation: 1, wet: 0 });
            preserved = initialNode === source.node;
            await context.resume();
        });
        const rendered = await context.startRendering();
        await change;
        let firstTail = 0, secondTail = 0;
        const left = rendered.getChannelData(0);
        for (let i = 5200; i < 10000; ++i) firstTail += left[i] ** 2;
        for (let i = 24400; i < 30000; ++i) secondTail += left[i] ** 2;
        const direct = left[24000];
        driver.resetResources();
        const reset = driver.reverbNode === null && driver.publishTelemetry().reverbMemoryBytes === 0;
        send("room-type", { id: 8 });
        const restored = await driver.ensureReverb();
        const recreated = Boolean(restored && restored !== oldDevice);
        driver.dispose();
        return { errors, staleDiscarded, preserved, firstTail, secondTail, direct, reset, recreated };
    });
    expect(result.errors).toEqual([]);
    expect(result.staleDiscarded && result.preserved && result.reset && result.recreated).toBe(true);
    expect(result.firstTail).toBeGreaterThan(1e-6);
    expect(result.secondTail).toBeLessThan(result.firstTail * 1e-6);
    expect(result.direct).toBe(0.5);
});
