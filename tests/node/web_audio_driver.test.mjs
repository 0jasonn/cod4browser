import assert from "node:assert/strict";
import test from "node:test";

import { acceptAudioPlayback } from "../../web/worker_transport.mjs";

import { WebAudioDriver } from "../../web/web_audio_driver.mjs";

const command = (op, values = {}) => ({ version: 1, op, ...values });

test("signed PCM conversion preserves opposite samples and OpenAL full scale", () => {
    const output = new Float32Array(5);
    const driver = new WebAudioDriver({ contextFactory: () => ({
        createBuffer: () => ({ getChannelData: () => output }),
    }) });
    const samples = new Int16Array([-32768, -16384, 0, 16384, 32767]);
    assert.equal(driver.handleCommand(command("buffer-upload", {
        bufferId: 1, bytes: samples.byteLength, rate: 44100,
        format: 0x1101, pcm: samples.buffer,
    })), true);
    assert.deepEqual([...output], [-1, -0.5, 0, 0.5, 32767 / 32768]);
    driver.dispose();
});

test("stream unqueue validates the complete prefix before mutation", () => {
    const diagnostics = [];
    const driver = new WebAudioDriver({
        contextFactory: () => null,
        onDiagnostic: (message) => diagnostics.push(message),
    });
    assert.equal(driver.handleCommand(command("source-create", { id: 1 })), true);
    for (const id of [1, 2, 3]) driver.buffers.set(id, { duration: 0.25 });
    assert.equal(driver.handleCommand(command("source-queue", {
        sourceId: 1, generation: 4, bufferIds: [1, 2, 3],
    })), true);

    const source = driver.sources.get(1);
    const stopped = [];
    const disconnected = [];
    source.queue.forEach((entry) => {
        entry.node = {
            stop: () => stopped.push(entry.bufferId),
            disconnect: () => disconnected.push(entry.bufferId),
        };
        source.streamNodes.push(entry.node);
    });
    source.queueProcessed = 2;
    source.activeBufferId = 3;

    assert.equal(driver.handleCommand(command("source-unqueue", {
        sourceId: 1, generation: 5, bufferIds: [1, 9],
    })), false);
    assert.equal(driver.handleCommand(command("source-unqueue", {
        sourceId: 1, generation: 5, bufferIds: [1, 2, 3, 4],
    })), false);
    assert.deepEqual(source.queue.map(({ bufferId }) => bufferId), [1, 2, 3]);
    assert.equal(source.queueProcessed, 2);
    assert.equal(source.activeBufferId, 3);
    assert.equal(source.generation, 4);
    assert.deepEqual(stopped, []);
    assert.deepEqual(disconnected, []);

    assert.equal(driver.handleCommand(command("source-unqueue", {
        sourceId: 1, generation: 5, bufferIds: [1, 2],
    })), true);
    assert.deepEqual(source.queue.map(({ bufferId }) => bufferId), [3]);
    assert.equal(source.queueProcessed, 0);
    assert.equal(source.activeBufferId, 3);
    assert.equal(source.generation, 5);
    assert.deepEqual(stopped, [1, 2]);
    assert.deepEqual(disconnected, [1, 2]);
    assert.equal(source.streamNodes.length, 1);
    assert.equal(diagnostics.length, 2);
    driver.dispose();
});

test("source position changes only when x, y, and z are all finite", () => {
    const driver = new WebAudioDriver({ contextFactory: () => null });
    assert.equal(driver.handleCommand(command("source-create", { id: 1 })), true);
    assert.equal(driver.handleCommand(command("source-property", {
        sourceId: 1, generation: 1, x: 1, y: 2, z: 3,
    })), true);
    const source = driver.sources.get(1);

    for (const [x, y, z] of [
        [Number.NaN, 20, 30],
        [10, Number.POSITIVE_INFINITY, 30],
        [10, 20, Number.NEGATIVE_INFINITY],
        [10, 20, undefined],
    ]) {
        assert.equal(driver.handleCommand(command("source-property", {
            sourceId: 1, generation: 2, x, y, z,
        })), false);
        assert.deepEqual([source.x, source.y, source.z], [1, 2, 3]);
        assert.equal(source.generation, 1);
    }

    assert.equal(driver.handleCommand(command("source-property", {
        sourceId: 1, generation: 2, x: -4, y: 5.5, z: 6,
    })), true);
    assert.deepEqual([source.x, source.y, source.z], [-4, 5.5, 6]);
    assert.equal(source.generation, 2);
    driver.dispose();
});

function clockDriver() {
    const context = { currentTime: 10, state: "running", sampleRate: 48000,
        destination: {}, createGain: () => ({ gain: { value: 1 }, connect() {}, disconnect() {} }),
        createBufferSource: () => ({ playbackRate: { value: 1 },
            connect() {}, disconnect() {}, start() {}, stop() {} }) };
    const driver = new WebAudioDriver({ contextFactory: () => context, startMuted: false });
    for (const id of [1, 2, 3]) driver.buffers.set(id, { duration: 1 });
    const send = (op, values = {}) => driver.handleCommand(command(op,
        { sourceId: 1, generation: 1, ...values }));
    send("source-create", { id: 1 });
    return { context, driver, send, snapshot: () => driver.playbackState(driver.sources.get(1)) };
}

test("device clock owns position across delayed start, pitch, suspension, pause and seek", async (t) => {
    const { context, driver, send, snapshot } = clockDriver();
    t.after(() => driver.dispose());
    // Delivery after a long host stall starts at the current device time.
    context.currentTime = 100;
    assert(send("source-play", { bufferId: 1, offset: 0, pitch: 1 }));
    assert.equal(snapshot().offset, 0);
    context.currentTime = 100.25;
    assert.equal(snapshot().offset, 0.25);
    assert(send("source-property", { pitch: 2, offset: 0 })); // stale proxy offset is not a seek
    context.currentTime = 100.5;
    assert.equal(snapshot().offset, 0.75);
    context.state = "suspended";
    await new Promise((resolve) => setTimeout(resolve, 10));
    assert.equal(snapshot().offset, 0.75);
    assert.equal(snapshot().state, 0x1012);
    assert(send("source-pause", { generation: 2, offset: 0.25 }));
    context.currentTime = 103;
    assert.equal(snapshot().offset, 0.75);
    assert(send("source-resume", { generation: 3, offset: 0.25 }));
    assert.equal(snapshot().offset, 0.75);
    assert(send("source-seek", { generation: 4, offset: 0.1 }));
    assert.equal(snapshot().offset, 0.1);
    context.currentTime = 104;
    assert.equal(snapshot().state, 0x1014);
    assert.equal(snapshot().offset, 1);
});

test("queued device progress survives unqueue, pause, pitch and delayed end callbacks", (t) => {
    const { context, driver, send, snapshot } = clockDriver();
    t.after(() => driver.dispose());
    assert(send("source-queue", { bufferIds: [1, 2, 3] }));
    assert(send("source-play", { queueProcessed: 0, pitch: 1, offset: 0 }));
    context.currentTime = 11.25; // no onended callback has run
    assert.equal(snapshot().processed, 1);
    assert.equal(snapshot().offset, 0.25);
    assert(send("source-unqueue", { bufferIds: [1] }));
    assert.equal(snapshot().processed, 1);
    assert(send("source-pause", { generation: 2, queueProcessed: 0, offset: 0 }));
    context.currentTime = 20;
    assert.equal(snapshot().offset, 0.25);
    assert(send("source-resume", { generation: 3, offset: 0 }));
    context.currentTime = 20.25;
    assert.equal(snapshot().offset, 0.5);
    assert(send("source-property", { generation: 3, pitch: 2, offset: 0 }));
    context.currentTime = 20.5;
    assert.equal(snapshot().processed, 2);
    assert.equal(snapshot().offset, 0);
    context.currentTime = 21;
    assert.equal(snapshot().processed, 3);
    assert.equal(snapshot().state, 0x1014);
});

test("unavailable audio reports device failure rather than invented elapsed playback", (t) => {
    const { driver, send, snapshot } = clockDriver();
    t.after(() => driver.dispose());
    assert.equal(send("source-play", { bufferId: 9 }), false);
    assert.equal(snapshot().state, 0);
});


test("Worker device snapshots reject malformed batches atomically", () => {
    const source = { sourceId: 1, generation: 7, processed: 2, offset: 0.25, state: 0x1012 };
    const message = { type: "audio-playback", version: 1, sources: [source] };
    assert(acceptAudioPlayback(message));
    const accepted = globalThis.__KISAKCOD_AUDIO_PLAYBACK__;
    for (const sources of [[source, source], new Array(55).fill(source),
        [{ ...source, sourceId: 0 }], [{ ...source, sourceId: 55 }],
        [{ ...source, processed: -1 }], [{ ...source, generation: 1.5 }],
        [{ ...source, offset: Infinity }], [{ ...source, state: 9 }]]) {
        assert.equal(acceptAudioPlayback({ ...message, sources }), false);
        assert.equal(globalThis.__KISAKCOD_AUDIO_PLAYBACK__, accepted);
    }
    assert(acceptAudioPlayback({ ...message, sources: [] }));
    assert.deepEqual(Object.keys(globalThis.__KISAKCOD_AUDIO_PLAYBACK__), []);
});


test("failed PCM admission preserves queue ordinals through retirement and reuse", (t) => {
    const { driver, send, snapshot } = clockDriver();
    t.after(() => driver.dispose());
    assert.equal(send("source-queue", { bufferIds: [9] }), false);
    assert.equal(send("source-play"), false);
    assert.equal(snapshot().state, 0);
    assert(send("source-unqueue", { bufferIds: [9] }));
    assert(send("source-queue", { generation: 2, bufferIds: [1] }));
    assert(send("source-play", { generation: 2 }));
    assert.equal(snapshot().processed, 1);
    assert.equal(snapshot().state, 0x1012);
});
