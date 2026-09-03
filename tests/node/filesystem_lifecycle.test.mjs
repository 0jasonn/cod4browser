import assert from "node:assert/strict";
import test from "node:test";
import { LockManager } from "./lock_manager.mjs";

import {
    createEngineWorkerHost,
    FILESYSTEM_STATES,
} from "../../web/product_engine_worker_host.mjs";
import { ENGINE_PROTOCOL_VERSION } from "../../web/product_protocol.mjs";

const HOME_LOCK = "kisakcod-web-home-writer-v1";

class WorkerDouble
{
    listeners = new Map();
    terminated = false;

    constructor(behavior, log)
    {
        this.behavior = behavior;
        this.log = log;
    }

    addEventListener(type, callback)
    {
        const listeners = this.listeners.get(type) ?? new Set();
        listeners.add(callback);
        this.listeners.set(type, listeners);
    }

    removeEventListener(type, callback) { this.listeners.get(type)?.delete(callback); }

    emit(type, event, evenIfTerminated = false)
    {
        if (this.terminated && !evenIfTerminated) return;
        for (const callback of this.listeners.get(type) ?? []) callback(event);
    }

    reply(request, result = true, error = null, delay = 0, evenIfTerminated = false)
    {
        setTimeout(() => this.emit("message", { data: {
            protocolVersion: ENGINE_PROTOCOL_VERSION,
            type: "reply",
            id: request.id,
            result,
            error,
        } }, evenIfTerminated), delay);
    }

    progress(request, progress, delay = 0, evenIfTerminated = false)
    {
        setTimeout(() => this.emit("message", { data: {
            protocolVersion: ENGINE_PROTOCOL_VERSION,
            type: "filesystem-progress",
            id: request.id,
            operation: request.type,
            progress,
        } }, evenIfTerminated), delay);
    }

    crash(message = "worker crashed")
    {
        this.emit("error", { error: new Error(message), message });
    }

    postMessage(message)
    {
        this.log.push(`worker:post:${message.type}`);
        if (message.type === "init") {
            queueMicrotask(() => this.emit("message", { data: {
                protocolVersion: ENGINE_PROTOCOL_VERSION,
                type: "ready",
            } }));
            return;
        }
        this.behavior(message, this);
    }

    terminate()
    {
        this.log.push("worker:terminate");
        this.terminated = true;
    }
}

const failure = (operation, code = "OPERATION_FAILED") => ({
    code, operation, message: `${operation} failed`, recoverable: true,
});

function createHarness({
    behavior,
    locks = new LockManager(),
    timeout = 25,
    absoluteTimeout = Math.max(100, timeout),
    onState,
    onProgress,
    onLifecycle,
} = {})
{
    const log = locks.log;
    const defaultBehavior = (message, worker) => {
        if (["mountAssets", "flushAndUnmount", "checkpoint", "shutdown"].includes(message.type)) {
            worker.reply(message, message.type === "mountAssets"
                ? { mounted: true, runtime: true }
                : { mounted: false });
        } else {
            worker.reply(message, true);
        }
    };
    const worker = new WorkerDouble(behavior ?? defaultBehavior, log);
    const audio = { attachGestureResume() {}, dispose() {} };
    const canvas = {
        style: {},
        transferControlToOffscreen() { return {}; },
    };
    const states = [];
    const host = createEngineWorkerHost(canvas, {
        workerFactory: () => worker,
        audioDriverFactory: () => audio,
        lockManager: locks,
        mountTimeoutMs: timeout,
        flushTimeoutMs: timeout,
        filesystemStallTimeoutMs: timeout,
        filesystemAbsoluteTimeoutMs: absoluteTimeout,
        managePageLifecycle: false,
        onFilesystemProgress: onProgress,
        onFilesystemLifecycleEvent: onLifecycle,
        onFilesystemState(state) {
            states.push(state);
            log.push(`state:${state}`);
            onState?.(state, { host, worker, locks, log, states });
        },
    });
    return { host, worker, locks, log, states };
}

const manifest = { importId: "test", files: [] };

test("filesystem lifecycle: successful mount", async () => {
    const { host, locks } = createHarness();
    await host.ready;
    await host.mountAssets(manifest);
    assert.equal(host.filesystemState, FILESYSTEM_STATES.MOUNTED);
    assert.equal(locks.held(HOME_LOCK), true);
    await host.dispose();
});

test("filesystem lifecycle: explicit mount failure releases leases", async () => {
    const { host, locks } = createHarness({
        behavior(message, worker) {
            worker.reply(message, null, failure(message.type, "MOUNT_FAILED_CLEAN"));
        },
    });
    await host.ready;
    await assert.rejects(host.mountAssets(manifest));
    assert.equal(host.filesystemState, FILESYSTEM_STATES.UNMOUNTED);
    assert.equal(locks.held(HOME_LOCK), false);
    await host.dispose();
});

test("filesystem lifecycle: cleanup failure terminates before lease release", async () => {
    const { host, worker, locks, log } = createHarness({
        behavior(message, target) {
            target.reply(message, null, failure(message.type, "MOUNT_CLEANUP_FAILED"));
        },
    });
    await host.ready;
    await assert.rejects(host.mountAssets(manifest),
        (error) => error.code === "MOUNT_CLEANUP_FAILED");
    assert.equal(worker.terminated, true);
    assert.equal(host.filesystemState, FILESYSTEM_STATES.TERMINATED);
    assert.equal(locks.held(HOME_LOCK), false);
    assert.ok(log.indexOf("worker:terminate") < log.indexOf(`lock:${HOME_LOCK}:release`));
    await host.dispose();
});

test("filesystem lifecycle: unknown canonical ownership terminates the Worker", async () => {
    const { host, worker, states } = createHarness({
        behavior(message, target) {
            target.reply(message, null,
                failure(message.type, "FILESYSTEM_OWNERSHIP_UNKNOWN"));
        },
    });
    await host.ready;
    await assert.rejects(host.mountAssets(manifest),
        (error) => error.code === "FILESYSTEM_OWNERSHIP_UNKNOWN");
    assert.equal(worker.terminated, true);
    assert.equal(states.at(-1), FILESYSTEM_STATES.TERMINATED);
    await host.dispose();
});

test("filesystem lifecycle: second writer stays excluded during cleanup recovery", async () => {
    const locks = new LockManager();
    let secondAttempt;
    const { host } = createHarness({
        locks,
        behavior(message, target) {
            target.reply(message, null, failure(message.type, "MOUNT_CLEANUP_FAILED"));
        },
        onLifecycle(event) {
            if (event === "workerTerminationStarted") {
                secondAttempt = locks.request(HOME_LOCK, {
                    mode: "exclusive", ifAvailable: true,
                }, (lock) => Boolean(lock));
            }
        },
    });
    await host.ready;
    await assert.rejects(host.mountAssets(manifest));
    assert.equal(await secondAttempt, false);
    await host.dispose();
});

test("filesystem lifecycle: late mount reply cannot survive timeout recovery", async () => {
    const { host, worker, states } = createHarness({
        timeout: 5,
        behavior(message, target) { target.reply(message, { mounted: true }, null, 30, true); },
    });
    await host.ready;
    await assert.rejects(host.mountAssets(manifest), (error) => error.code === "REQUEST_TIMEOUT");
    await new Promise((resolve) => setTimeout(resolve, 40));
    assert.equal(worker.terminated, true);
    assert.equal(host.filesystemState, FILESYSTEM_STATES.TERMINATED);
    assert.equal(states.at(-1), FILESYSTEM_STATES.TERMINATED);
    await host.dispose();
});

test("filesystem lifecycle: progress extends the stall watchdog", async () => {
    const progress = [];
    const { host } = createHarness({
        timeout: 12,
        absoluteTimeout: 80,
        onProgress: (item) => progress.push(item),
        behavior(message, worker) {
            if (message.type === "mountAssets") {
                for (let index = 1; index <= 3; ++index) {
                    worker.progress(message, {
                        phase: "mounting",
                        filesProcessed: index,
                        bytesProcessed: index * 1024,
                    }, index * 8);
                }
                worker.reply(message, { mounted: true }, null, 32);
            } else worker.reply(message, { mounted: false });
        },
    });
    await host.ready;
    await host.mountAssets(manifest);
    assert.equal(host.filesystemState, FILESYSTEM_STATES.MOUNTED);
    assert.equal(progress.length, 3);
    await host.dispose();
});

test("filesystem lifecycle: late progress cannot revive a timed-out request", async () => {
    const progress = [];
    const { host } = createHarness({
        timeout: 5,
        onProgress: (item) => progress.push(item),
        behavior(message, worker) {
            worker.progress(message, {
                phase: "mounting", filesProcessed: 1, bytesProcessed: 1024,
            }, 20, true);
        },
    });
    await host.ready;
    await assert.rejects(host.mountAssets(manifest),
        (error) => error.code === "REQUEST_TIMEOUT");
    await new Promise((resolve) => setTimeout(resolve, 30));
    assert.equal(progress.length, 0);
    assert.equal(host.filesystemState, FILESYSTEM_STATES.TERMINATED);
    await host.dispose();
});

test("filesystem lifecycle: absolute watchdog wins despite continued progress", async () => {
    let timer;
    const { host } = createHarness({
        timeout: 30,
        absoluteTimeout: 80,
        behavior(message, worker) {
            if (message.type !== "mountAssets") {
                worker.reply(message, { mounted: false });
                return;
            }
            let filesProcessed = 0;
            timer = setInterval(() => worker.progress(message, {
                phase: "mounting",
                filesProcessed: ++filesProcessed,
                bytesProcessed: filesProcessed * 1024,
            }, 0, true), 6);
        },
    });
    await host.ready;
    let timeoutError;
    try {
        await host.mountAssets(manifest);
    } catch (error) {
        timeoutError = error;
    } finally {
        clearInterval(timer);
    }
    assert.equal(timeoutError?.code, "REQUEST_TIMEOUT");
    assert.match(timeoutError?.message ?? "", /absolute limit/u);
    assert.equal(host.filesystemState, FILESYSTEM_STATES.TERMINATED);
    await host.dispose();
});

test("filesystem lifecycle: successful flush unmounts and releases", async () => {
    const { host, locks } = createHarness();
    await host.ready;
    await host.mountAssets(manifest);
    await host.flushAndUnmount();
    assert.equal(host.filesystemState, FILESYSTEM_STATES.UNMOUNTED);
    assert.equal(locks.held(HOME_LOCK), false);
    await host.dispose();
});

test("filesystem lifecycle: concurrent checkpoints queue one non-overlapping follow-up", async () => {
    let checkpoints = 0;
    const { host, locks } = createHarness({
        behavior(message, worker) {
            if (message.type === "checkpoint") {
                ++checkpoints;
                worker.reply(message, { mounted: true }, null, 10);
            } else worker.reply(message, { mounted: true });
        },
    });
    await host.ready;
    await host.mountAssets(manifest);
    const first = host.checkpoint();
    while (checkpoints === 0) await new Promise((resolve) => setTimeout(resolve, 0));
    const second = host.checkpoint();
    assert.equal(first, second);
    await Promise.all([first, second]);
    assert.equal(checkpoints, 2);
    assert.equal(locks.held(HOME_LOCK), true);
    await host.dispose();
});

test("filesystem lifecycle: failed checkpoint retains ownership and can retry", async () => {
    let checkpoints = 0;
    const { host, locks } = createHarness({
        behavior(message, worker) {
            if (message.type === "checkpoint" && ++checkpoints === 1) {
                worker.reply(message, null, failure(message.type));
            } else worker.reply(message, { mounted: true });
        },
    });
    await host.ready;
    await host.mountAssets(manifest);
    await assert.rejects(host.checkpoint());
    assert.equal(host.filesystemState, FILESYSTEM_STATES.MOUNTED);
    assert.equal(locks.held(HOME_LOCK), true);
    await host.checkpoint();
    assert.equal(checkpoints, 2);
    assert.equal(locks.held(HOME_LOCK), true);
    await host.dispose();
});

test("filesystem lifecycle: recoverable flush failure retains ownership", async () => {
    const { host, locks } = createHarness({
        behavior(message, worker) {
            if (message.type === "flushAndUnmount") {
                worker.reply(message, null, failure(message.type));
            } else worker.reply(message, { mounted: true });
        },
    });
    await host.ready;
    await host.mountAssets(manifest);
    await assert.rejects(host.flushAndUnmount());
    assert.equal(host.filesystemState, FILESYSTEM_STATES.FLUSH_FAILED_RETRYABLE);
    assert.equal(locks.held(HOME_LOCK), true);
    await host.dispose();
});

test("filesystem lifecycle: retry succeeds after recoverable flush failure", async () => {
    let flushes = 0;
    const { host } = createHarness({
        behavior(message, worker) {
            if (message.type === "flushAndUnmount" && ++flushes === 1) {
                worker.reply(message, null, failure(message.type));
            } else worker.reply(message, { mounted: message.type === "mountAssets" });
        },
    });
    await host.ready;
    await host.mountAssets(manifest);
    await assert.rejects(host.flushAndUnmount());
    await host.flushAndUnmount();
    assert.equal(host.filesystemState, FILESYSTEM_STATES.UNMOUNTED);
    await host.dispose();
});

test("filesystem lifecycle: late flush reply cannot restore timed-out state", async () => {
    const { host, states } = createHarness({
        timeout: 5,
        behavior(message, worker) {
            if (message.type === "flushAndUnmount") {
                worker.reply(message, { mounted: false }, null, 30, true);
            } else worker.reply(message, { mounted: true });
        },
    });
    await host.ready;
    await host.mountAssets(manifest);
    await assert.rejects(host.flushAndUnmount(), (error) => error.code === "REQUEST_TIMEOUT");
    await new Promise((resolve) => setTimeout(resolve, 40));
    assert.equal(host.filesystemState, FILESYSTEM_STATES.TERMINATED);
    assert.equal(states.at(-1), FILESYSTEM_STATES.TERMINATED);
    await host.dispose();
});

test("filesystem lifecycle: second writer is blocked during retryable failure", async () => {
    const locks = new LockManager();
    const first = createHarness({
        locks,
        behavior(message, worker) {
            if (message.type === "flushAndUnmount") {
                worker.reply(message, null, failure(message.type));
            } else worker.reply(message, { mounted: true });
        },
    });
    const second = createHarness({ locks });
    await Promise.all([first.host.ready, second.host.ready]);
    await first.host.mountAssets(manifest);
    await assert.rejects(first.host.flushAndUnmount());
    await assert.rejects(second.host.mountAssets(manifest),
        (error) => error.code === "HOME_WRITER_CONFLICT");
    await Promise.all([first.host.dispose(), second.host.dispose()]);
});

test("filesystem lifecycle: writer remains held in unknown timeout state", async () => {
    let heldDuringUnknown = false;
    const { host } = createHarness({
        timeout: 5,
        behavior() {},
        onState(state, { locks }) {
            if (state === FILESYSTEM_STATES.UNKNOWN_AFTER_TIMEOUT) {
                heldDuringUnknown = locks.held(HOME_LOCK);
            }
        },
    });
    await host.ready;
    await assert.rejects(host.mountAssets(manifest));
    assert.equal(heldDuringUnknown, true);
    await host.dispose();
});

test("filesystem lifecycle: termination precedes timeout lock release", async () => {
    const { host, log } = createHarness({ timeout: 5, behavior() {} });
    await host.ready;
    await assert.rejects(host.mountAssets(manifest));
    assert.ok(log.indexOf("worker:terminate") < log.indexOf(`lock:${HOME_LOCK}:release`));
    await host.dispose();
});

test("filesystem lifecycle: second tab cannot overlap a timed-out writer", async () => {
    const locks = new LockManager();
    let secondAttempt;
    const first = createHarness({
        locks, timeout: 5, behavior() {},
        onState(state) {
            if (state === FILESYSTEM_STATES.UNKNOWN_AFTER_TIMEOUT) {
                secondAttempt = locks.request(HOME_LOCK, {
                    mode: "exclusive", ifAvailable: true,
                }, (lock) => Boolean(lock));
            }
        },
    });
    await first.host.ready;
    await assert.rejects(first.host.mountAssets(manifest));
    assert.equal(await secondAttempt, false);
    await first.host.dispose();
});

test("filesystem lifecycle: second tab acquires after clean shutdown", async () => {
    const locks = new LockManager();
    const first = createHarness({ locks });
    const second = createHarness({ locks });
    await Promise.all([first.host.ready, second.host.ready]);
    await first.host.mountAssets(manifest);
    await first.host.flushAndUnmount();
    await second.host.mountAssets(manifest);
    assert.equal(second.host.filesystemState, FILESYSTEM_STATES.MOUNTED);
    await Promise.all([first.host.dispose(), second.host.dispose()]);
});

test("filesystem lifecycle: stale old-Worker reply cannot mutate a new session", async () => {
    const locks = new LockManager();
    const old = createHarness({
        locks, timeout: 5,
        behavior(message, worker) { worker.reply(message, { mounted: true }, null, 30, true); },
    });
    await old.host.ready;
    await assert.rejects(old.host.mountAssets(manifest));
    const current = createHarness({ locks });
    await current.host.ready;
    await current.host.mountAssets(manifest);
    await new Promise((resolve) => setTimeout(resolve, 40));
    assert.equal(old.host.filesystemState, FILESYSTEM_STATES.TERMINATED);
    assert.equal(current.host.filesystemState, FILESYSTEM_STATES.MOUNTED);
    await Promise.all([old.host.dispose(), current.host.dispose()]);
});

test("filesystem lifecycle: Worker crash while mounted terminates before release", async () => {
    const { host, worker, log } = createHarness();
    await host.ready;
    await host.mountAssets(manifest);
    worker.crash();
    await new Promise((resolve) => setTimeout(resolve, 0));
    assert.equal(host.filesystemState, FILESYSTEM_STATES.TERMINATED);
    assert.ok(log.indexOf("worker:terminate") < log.indexOf(`lock:${HOME_LOCK}:release`));
    await host.dispose();
});

test("filesystem lifecycle: Worker crash while flushing terminates before release", async () => {
    const { host, log } = createHarness({
        behavior(message, worker) {
            if (message.type === "flushAndUnmount") worker.crash();
            else worker.reply(message, { mounted: true });
        },
    });
    await host.ready;
    await host.mountAssets(manifest);
    await assert.rejects(host.flushAndUnmount(), (error) => error.code === "WORKER_ERROR");
    assert.equal(host.filesystemState, FILESYSTEM_STATES.TERMINATED);
    assert.ok(log.indexOf("worker:terminate") < log.indexOf(`lock:${HOME_LOCK}:release`));
    await host.dispose();
});
