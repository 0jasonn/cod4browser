import assert from "node:assert/strict";
import test from "node:test";

import { WebAudioDriver } from "../../web/web_audio_driver.mjs";

const command = (op, values = {}) => ({ version: 1, op, ...values });

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
