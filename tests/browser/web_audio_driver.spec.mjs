import { expect, test } from "@playwright/test";

test("Web Audio proxy accepts bounded PCM and ignores stale source generations", async ({ page }) => {
    await page.goto("/");
    const result = await page.evaluate(async () => {
        const { WebAudioDriver } = await import("/web_audio_driver.mjs");
        class Node {
            constructor() { this.gain = { value: 1 }; this.playbackRate = { value: 1 }; }
            connect() {}
            disconnect() {}
            start() { this.started = true; }
            stop() { this.onended?.(); }
        }
        class Context {
            constructor() { this.currentTime = 0; this.state = "suspended"; this.destination = {}; }
            createBuffer(channels, frames, rate) {
                return { duration: frames / rate, getChannelData: () => new Float32Array(frames) };
            }
            createBufferSource() { return new Node(); }
            createGain() { return new Node(); }
            createPanner() { return new Node(); }
            resume() { this.state = "running"; return Promise.resolve(); }
            close() { return Promise.resolve(); }
        }
        const driver = new WebAudioDriver({ contextFactory: () => new Context() });
        const pcm = new Uint8Array([0, 0, 0, 0]).buffer;
        const accepted = [
            driver.handleCommand({ version: 1, op: "source-create", id: 1 }),
            driver.handleCommand({ version: 1, op: "buffer-upload", bufferId: 1,
                format: 0x1101, bytes: 4, rate: 44100, pcm }),
            driver.handleCommand({ version: 1, op: "source-property", sourceId: 1,
                generation: 0, bufferId: 1, gain: 0.5, pitch: 1, looping: false,
                offset: 0, x: 0, y: 0, z: 0 }),
            driver.handleCommand({ version: 1, op: "source-play", sourceId: 1,
                generation: 1, bufferId: 1, gain: 0.5, pitch: 1, looping: false,
                offset: 0, x: 0, y: 0, z: 0 }),
        ];
        const stateAfterPlay = driver.sources.get(1).state;
        const stale = driver.handleCommand({ version: 1, op: "source-stop", sourceId: 1,
            generation: 0 });
        const invalid = driver.handleCommand({ version: 1, op: "buffer-upload", bufferId: 2,
            format: 0x1101, bytes: 4, rate: 44100, pcm: new ArrayBuffer(3) });
        await driver.resumeFromGesture();
        driver.dispose();
        return { accepted, stateAfterPlay, stale, invalid, disposed: driver.sources.size === 0 };
    });
    expect(result.accepted).toEqual([true, true, true, true]);
    expect(result.stateAfterPlay).toBe("playing");
    expect(result.stale).toBe(true);
    expect(result.invalid).toBe(false);
    expect(result.disposed).toBe(true);
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
