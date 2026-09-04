import assert from "node:assert/strict";
import test from "node:test";
import { createFilesystemProgressReporter } from "../../web/worker_transport.mjs";

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
