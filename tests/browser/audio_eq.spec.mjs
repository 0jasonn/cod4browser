import { expect, test } from "@playwright/test";

test("six canonical EQ bands filter loaded and queued PCM without restarting playback", async ({ page }) => {
    await page.goto("/");
    const result = await page.evaluate(async () => {
        const { WebAudioDriver } = await import("/web_audio_driver.mjs");
        const band = (type, gain, freq = 1000, q = Math.SQRT1_2) => [1, type, gain, freq, q];
        const snapshot = (bands) => [...bands.flat(), ...new Array(30 - bands.length * 5).fill(0)];
        async function render(bands, frequency, { queued = false, rate = 48000, update = false } = {}) {
            const context = new OfflineAudioContext(2, rate, rate);
            const driver = new WebAudioDriver({ contextFactory: () => context, startMuted: false });
            const command = (op, values = {}) => driver.handleCommand({ version: 1, op, ...values });
            command("source-create", { id: 1 });
            const frames = queued ? rate / 2 : rate;
            const samples = new Int16Array(frames * 2);
            for (let i = 0; i < frames; ++i) {
                samples[i * 2] = Math.round(2048 * Math.sin(2 * Math.PI * frequency * i / rate));
                samples[i * 2 + 1] = -samples[i * 2];
            }
            command("buffer-upload", { bufferId: 1, bytes: samples.byteLength, rate,
                format: 0x1103, pcm: samples.buffer });
            if (queued) command("source-queue", { sourceId: 1, generation: 0, bufferIds: [1, 1] });
            command("source-property", { sourceId: 1, generation: 0, bufferId: queued ? 0 : 1 });
            if (!update && !command("source-eq", { sourceId: 1, generation: 0, bands: snapshot(bands) }))
                throw new Error("EQ rejected");
            command("source-play", { sourceId: 1, generation: 1 });
            const source = driver.sources.get(1);
            const playingNode = queued ? source.streamNodes[0] : source.node;
            if (update && !command("source-eq", { sourceId: 1, generation: 1, bands: snapshot(bands) }))
                throw new Error("Live EQ rejected");
            const nodePreserved = playingNode === (queued ? source.streamNodes[0] : source.node);
            const filterCount = source.eqNodes.length;
            const filters = source.eqNodes.slice();
            // A bad later band must preserve the entire previous chain.
            const malformed = snapshot([band(4, 6), band(0, 0, 1000, 0)]);
            const rejected = !command("source-eq", { sourceId: 1, generation: 1, bands: malformed });
            const extreme = snapshot([band(4, 6), band(4, 1e30)]);
            const numericalRejected = !command("source-eq", { sourceId: 1, generation: 1, bands: extreme });
            const atomic = filters.every((filter, index) => filter === source.eqNodes[index]);
            command("source-eq", { sourceId: 1, generation: 0, bands: snapshot([]) });
            const staleIgnored = source.eqNodes.length === filterCount;
            let restoredAfterPause = true;
            if (queued) {
                command("source-pause", { sourceId: 1, generation: 1, offset: 0 });
                const released = source.eqNodes.length === 0;
                command("source-play", { sourceId: 1, generation: 2 });
                restoredAfterPause = released && source.eqNodes.length === filterCount;
            }
            const rendered = await context.startRendering();
            const left = rendered.getChannelData(0), right = rendered.getChannelData(1);
            let energy = 0, stereoError = 0, tailEnergy = 0;
            for (let i = rate / 4; i < rate / 2; ++i) {
                energy += left[i] ** 2;
                stereoError = Math.max(stereoError, Math.abs(left[i] + right[i]));
                tailEnergy += left[i + rate / 2] ** 2;
            }
            driver.dispose();
            return { rms: Math.sqrt(energy / (rate / 4)), tailRms: Math.sqrt(tailEnergy / (rate / 4)),
                stereoError, filterCount, nodePreserved, rejected, numericalRejected, atomic,
                staleIgnored, restoredAfterPause };
        }
        const baseline = await render([], 1000);
        const checks = [];
        for (const [type, gain, frequency, expected] of [
            [0, 0, 1000, Math.SQRT1_2], [1, 0, 1000, Math.SQRT1_2],
            [2, -12, 1000, 10 ** (-12 / 40)], [3, 6, 1000, 10 ** (6 / 40)],
            [4, 6, 1000, 10 ** (6 / 20)],
        ]) checks.push({ ...(await render([band(type, gain)], frequency)), expected });
        const lowCut = await render([band(0, 0)], 8000);
        const highCut = await render([band(1, 0)], 100);
        const resonant = await render([band(0, 0, 1000, 2)], 1000);
        const cascade = await render(Array.from({ length: 6 }, () => band(4, 2)), 1000,
            { queued: true, update: true });
        const otherRate = await render([band(4, 6)], 1000, { rate: 44100, update: true });
        const shelfWide = await render([band(2, 12, 1000, 0.5)], 500);
        const shelfResonant = await render([band(2, 12, 1000, 2)], 500);
        const zeroCut = await render([band(0, 0, 0)], 1000);
        const nyquistCut = await render([band(1, 0, 20000)], 1000, { rate: 32000 });
        return { baseline, checks, lowCut, highCut, resonant, cascade, otherRate,
            shelfWide, shelfResonant, zeroCut, nyquistCut };
    });
    for (const check of result.checks) {
        expect(check.rms / result.baseline.rms).toBeCloseTo(check.expected, 2);
        expect(check.stereoError).toBeLessThan(1e-7);
    }
    expect(result.lowCut.rms / result.baseline.rms).toBeLessThan(0.02);
    expect(result.highCut.rms / result.baseline.rms).toBeLessThan(0.02);
    expect(result.resonant.rms / result.baseline.rms).toBeCloseTo(2, 2);
    expect(result.cascade.rms / result.baseline.rms).toBeCloseTo(10 ** (12 / 20), 2);
    expect(result.cascade.tailRms / result.cascade.rms).toBeCloseTo(1, 4);
    expect(result.cascade.filterCount).toBe(6);
    expect(result.cascade.restoredAfterPause).toBe(true);
    expect(result.otherRate.rms / result.baseline.rms).toBeCloseTo(10 ** (6 / 20), 2);
    expect(Math.abs(result.shelfWide.rms - result.shelfResonant.rms)).toBeGreaterThan(0.02);
    expect(result.zeroCut.rms).toBe(0);
    expect(result.nyquistCut.rms).toBe(0);
    for (const check of [...result.checks, result.cascade, result.otherRate]) {
        expect(check.nodePreserved).toBe(true);
        expect(check.rejected).toBe(true);
        expect(check.numericalRejected).toBe(true);
        expect(check.atomic).toBe(true);
        expect(check.staleIgnored).toBe(true);
    }
});

test("canonical EQ owner forwards both stages and bypass through the Worker audio proxy", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const result = await page.evaluate(async () => {
        const driver = globalThis.__KISAKCOD_WEB__.module.audioDriver;
        const original = driver.handleCommand.bind(driver);
        const messages = [];
        driver.handleCommand = (command) => {
            if (command.op === "source-eq") messages.push(structuredClone(command));
            return original(command);
        };
        try {
            const accepted = await globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestAudioEq");
            return { accepted, messages };
        } finally { driver.handleCommand = original; }
    });
    expect(result.accepted).toBe(1);
    expect(result.messages).toHaveLength(2);
    expect(result.messages[0].bands).toEqual(Array.from({ length: 6 }, (_, i) =>
        [1, i % 5, -6 + i, 500 + 100 * i, 0.75]).flat());
    expect(result.messages[1].bands).toEqual(new Array(30).fill(0));
});
