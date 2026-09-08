import assert from "node:assert/strict";
import { getEventListeners } from "node:events";
import test from "node:test";
import {
    createFilesystemProgressReporter,
    createWorkerRpc,
    EngineWorkerError,
    MAX_REQUEST_TIMEOUT_MS,
    rejectWorkerRequests,
    settleWorkerReply,
} from "../../web/worker_transport.mjs";

test("Worker RPC preserves request identity and releases replies, aborts and failures", async t => {
    t.mock.timers.enable({ apis: ["setTimeout"] });
    const pending = new Map();
    const sent = [];
    const worker = { postMessage: (...args) => sent.push(args) };
    let generation = 3;
    const rpc = createWorkerRpc(worker, pending, 100, () => generation);
    const firstAbort = new AbortController();
    const secondAbort = new AbortController();
    const transfer = [new ArrayBuffer(4)];
    const first = rpc("probeAsset", { buffers: transfer }, transfer, { signal: firstAbort.signal });
    const second = rpc("runtimeStatus", {}, [], { signal: secondAbort.signal });
    const firstId = sent[0][0].id;
    const secondId = sent[1][0].id;
    assert.notEqual(firstId, secondId);
    assert.deepEqual(sent[0], [{
        protocolVersion: 1, type: "probeAsset", id: firstId, buffers: transfer,
    }, transfer]);
    assert.equal(pending.get(firstId).generation, generation);
    settleWorkerReply(pending, { id: firstId, result: "stale" }, generation - 1);
    assert.equal(pending.size, 2);
    secondAbort.abort();
    await assert.rejects(second, { name: "AbortError" });
    settleWorkerReply(pending, { id: secondId, result: "late" }, generation);
    settleWorkerReply(pending, { id: firstId, result: "current" }, generation);
    assert.equal(await first, "current");
    assert.equal(pending.size, 0);
    assert.equal(getEventListeners(firstAbort.signal, "abort").length, 0);
    assert.equal(getEventListeners(secondAbort.signal, "abort").length, 0);

    await assert.rejects(rpc("runtimeStatus", {}, [], { signal: secondAbort.signal }),
        { name: "AbortError" });
    for (const options of [
        { timeoutMs: 0 }, { timeoutMs: NaN }, { timeoutMs: MAX_REQUEST_TIMEOUT_MS + 1 },
        { stallTimeoutMs: 0 }, { stallTimeoutMs: 10 },
        { stallTimeoutMs: 10, absoluteTimeoutMs: 5 },
    ]) await assert.rejects(rpc("runtimeStatus", {}, [], options), RangeError);
    assert.equal(sent.length, 2);
    assert.equal(pending.size, 0);

    ++generation;
    const timed = rpc("runtimeStatus", {}, [], { signal: firstAbort.signal });
    assert.equal(pending.get(sent.at(-1)[0].id).generation, generation);
    t.mock.timers.tick(99);
    assert.equal(pending.size, 1);
    const timedOut = assert.rejects(timed, {
        name: "EngineWorkerError", code: "REQUEST_TIMEOUT", operation: "runtimeStatus",
        message: "The engine Worker did not answer within 100 ms.", recoverable: true,
    });
    t.mock.timers.tick(1);
    await timedOut;
    assert.equal(pending.size, 0);
    assert.equal(getEventListeners(firstAbort.signal, "abort").length, 0);

    const retired = rpc("runtimeStatus", {}, [], { signal: firstAbort.signal });
    const failure = new EngineWorkerError({ code: "WORKER_TERMINATED", message: "retired" });
    rejectWorkerRequests(pending, failure);
    await assert.rejects(retired, error => error === failure);
    const postFailure = new DOMException("Could not clone payload", "DataCloneError");
    worker.postMessage = () => { throw postFailure; };
    await assert.rejects(rpc("probeAsset", {}, [], { signal: firstAbort.signal }),
        error => error === postFailure);
    assert.equal(pending.size, 0);
    assert.equal(getEventListeners(firstAbort.signal, "abort").length, 0);
    t.mock.timers.runAll();
});

test("filesystem progress reports only advancing work and preserves request identity", t => {
    const messages = [];
    const originalPost = globalThis.postMessage;
    globalThis.postMessage = message => messages.push(message);
    t.after(() => {
        if (originalPost) globalThis.postMessage = originalPost;
        else delete globalThis.postMessage;
    });
    let now = 0;
    t.mock.method(Date, "now", () => now);
    const report = createFilesystemProgressReporter({ id: 77, type: "mount" });
    report({ phase: "runtime-loading", filesProcessed: 0, bytesProcessed: 0 });
    report({ phase: "runtime-loading", filesProcessed: 0, bytesProcessed: 10 });
    assert.equal(messages.length, 1); // Throttled, but not lost from the cumulative count.
    now = 300;
    report({ phase: "runtime-loading", filesProcessed: 0, bytesProcessed: 20 });
    now = 600;
    report({ phase: "runtime-loading", filesProcessed: 0, bytesProcessed: 20 });
    report({ phase: "runtime-loading", filesProcessed: 0, bytesProcessed: 10 });
    assert.equal(messages.length, 2); // Repeated/regressing counters are not a heartbeat.
    report({ phase: "snapshotting", filesProcessed: 0, bytesProcessed: 0 });
    assert.equal(messages.length, 3);
    for (const message of messages) {
        assert.equal(message.id, 77);
        assert.equal(message.operation, "mount");
        assert.equal(message.protocolVersion, 1);
        assert.equal(message.type, "filesystem-progress");
    }
});
