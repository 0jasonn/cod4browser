import assert from "node:assert/strict";
import test from "node:test";

import {
    createEngineWorkerHost,
    DIAGNOSTIC_FILESYSTEM_STATES,
} from "../../web/engine_worker_host.mjs";
import { ENGINE_PROTOCOL_VERSION } from "../../web/engine_protocol.mjs";

const HOME_LOCK = "kisakcod-web-home-writer-v1";
const manifest = { importId: "diagnostic-test", files: [] };

class LockManager
{
    holders = new Map();
    waiters = [];
    log = [];

    available(name, mode)
    {
        const holders = this.holders.get(name) ?? [];
        return holders.length === 0 ||
            (mode === "shared" && holders.every((holder) => holder.mode === "shared"));
    }

    held(name) { return (this.holders.get(name)?.length ?? 0) > 0; }

    request(name, options, callback)
    {
        const mode = options?.mode ?? "exclusive";
        if (options?.ifAvailable && !this.available(name, mode)) {
            return Promise.resolve(callback(null));
        }
        return new Promise((resolve, reject) => {
            const start = () => {
                if (!this.available(name, mode)) return false;
                const holder = { mode };
                const holders = this.holders.get(name) ?? [];
                holders.push(holder);
                this.holders.set(name, holders);
                this.log.push(`lock:${name}:acquire`);
                Promise.resolve(callback({ name, mode })).then(resolve, reject).finally(() => {
                    const current = this.holders.get(name) ?? [];
                    current.splice(current.indexOf(holder), 1);
                    if (current.length === 0) this.holders.delete(name);
                    this.log.push(`lock:${name}:release`);
                    this.drain();
                });
                return true;
            };
            if (!start()) this.waiters.push(start);
        });
    }

    drain()
    {
        const waiters = this.waiters;
        this.waiters = [];
        for (const start of waiters) {
            if (!start()) this.waiters.push(start);
        }
    }
}

class WorkerDouble extends EventTarget
{
    terminated = false;

    constructor(behavior, log)
    {
        super();
        this.behavior = behavior;
        this.log = log;
    }

    reply(request, result = true, error = null, delay = 0)
    {
        setTimeout(() => this.dispatchEvent(new MessageEvent("message", { data: {
            protocolVersion: ENGINE_PROTOCOL_VERSION,
            type: "reply",
            id: request.id,
            operation: request.type,
            result,
            error,
        } })), delay);
    }

    postMessage(message)
    {
        this.log.push(`worker:post:${message.type}`);
        if (message.type === "init") {
            queueMicrotask(() => this.dispatchEvent(new MessageEvent("message", { data: {
                protocolVersion: ENGINE_PROTOCOL_VERSION,
                type: "ready",
            } })));
        } else {
            this.behavior(message, this);
        }
    }

    crash()
    {
        this.dispatchEvent(Object.assign(new Event("error"), {
            error: new Error("diagnostic Worker crashed"),
            message: "diagnostic Worker crashed",
        }));
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

function createHarness({ behavior, locks = new LockManager(), timeout = 20 } = {})
{
    const defaultBehavior = (message, worker) => worker.reply(
        message, message.type === "mount" ? { fileCount: 0 } : { mounted: false });
    const worker = new WorkerDouble(behavior ?? defaultBehavior, locks.log);
    const states = [];
    const lifecycle = [];
    const host = createEngineWorkerHost({
        style: {},
        transferControlToOffscreen() { return {}; },
    }, {
        requestTimeoutMs: timeout,
        managePageLifecycle: false,
        workerFactory: () => worker,
        audioDriverFactory: () => ({ attachGestureResume() {}, dispose() {} }),
        lockManager: locks,
        onFilesystemState(state) {
            states.push(state);
            locks.log.push(`state:${state}`);
        },
        onFilesystemLifecycleEvent(event) { lifecycle.push(event); },
    });
    return { host, worker, locks, states, lifecycle, log: locks.log };
}

test("diagnostic lifecycle mounts and unmounts cleanly", async () => {
    const { host, locks } = createHarness();
    await host.ready;
    await host.mount(manifest);
    assert.equal(host.filesystemState, DIAGNOSTIC_FILESYSTEM_STATES.MOUNTED);
    assert.equal(locks.held(HOME_LOCK), true);
    await host.unmount();
    assert.equal(host.filesystemState, DIAGNOSTIC_FILESYSTEM_STATES.UNMOUNTED);
    assert.equal(locks.held(HOME_LOCK), false);
    await host.dispose();
});

test("diagnostic unmount failure terminates before releasing its lease", async () => {
    const { host, worker, log } = createHarness({
        behavior(message, target) {
            target.reply(message, null, message.type === "unmount"
                ? failure(message.type) : null);
        },
    });
    await host.ready;
    await host.mount(manifest);
    await assert.rejects(host.unmount());
    assert.equal(worker.terminated, true);
    assert.ok(log.indexOf("worker:terminate") < log.indexOf(`lock:${HOME_LOCK}:release`));
    await host.dispose();
});

test("diagnostic unmount timeout terminates and releases only afterward", async () => {
    const { host, worker, log } = createHarness({
        timeout: 5,
        behavior(message, target) {
            if (message.type !== "unmount") target.reply(message, { fileCount: 0 });
        },
    });
    await host.ready;
    await host.mount(manifest);
    await assert.rejects(host.unmount(), (error) => error.code === "REQUEST_TIMEOUT");
    assert.equal(worker.terminated, true);
    assert.ok(log.indexOf("worker:terminate") < log.indexOf(`lock:${HOME_LOCK}:release`));
    await host.dispose();
});

test("diagnostic Worker crash while mounted ends ownership", async () => {
    const { host, worker, locks } = createHarness();
    await host.ready;
    await host.mount(manifest);
    worker.crash();
    await new Promise((resolve) => setTimeout(resolve, 0));
    assert.equal(host.filesystemState, DIAGNOSTIC_FILESYSTEM_STATES.TERMINATED);
    assert.equal(locks.held(HOME_LOCK), false);
    await host.dispose();
});

test("diagnostic lifecycle reports termination before writer release", async () => {
    const { host, lifecycle } = createHarness({
        behavior(message, target) {
            target.reply(message, null, message.type === "unmount"
                ? failure(message.type) : null);
        },
    });
    await host.ready;
    await host.mount(manifest);
    await assert.rejects(host.unmount());
    assert.deepEqual(lifecycle, [
        "writerLeaseAcquired",
        "workerTerminationStarted",
        "workerTerminated",
        "writerLeaseReleased",
    ]);
    await host.dispose();
});

test("a second diagnostic tab remains excluded while ownership is uncertain", async () => {
    const locks = new LockManager();
    const first = createHarness({
        locks,
        timeout: 15,
        behavior(message, target) {
            if (message.type !== "unmount") target.reply(message, { fileCount: 0 });
        },
    });
    const second = createHarness({ locks });
    await Promise.all([first.host.ready, second.host.ready]);
    await first.host.mount(manifest);
    const unmount = first.host.unmount();
    await assert.rejects(second.host.mount(manifest),
        (error) => error.code === "HOME_WRITER_CONFLICT");
    await assert.rejects(unmount, (error) => error.code === "REQUEST_TIMEOUT");
    await Promise.all([first.host.dispose(), second.host.dispose()]);
});

test("diagnostic writer lease hands off after forced termination", async () => {
    const locks = new LockManager();
    const first = createHarness({
        locks,
        behavior(message, target) {
            target.reply(message, null, message.type === "unmount"
                ? failure(message.type) : null);
        },
    });
    const second = createHarness({ locks });
    await Promise.all([first.host.ready, second.host.ready]);
    await first.host.mount(manifest);
    await assert.rejects(first.host.unmount());
    await second.host.mount(manifest);
    assert.equal(second.host.filesystemState, DIAGNOSTIC_FILESYSTEM_STATES.MOUNTED);
    await Promise.all([first.host.dispose(), second.host.dispose()]);
});
