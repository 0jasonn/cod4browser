import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { runInNewContext } from "node:vm";

// Exercise the platform's actual EM_JS scheduling boundary without retail data.
const source = readFileSync(new URL("../../src/web/web_system.cpp", import.meta.url), "utf8");
const pumpBody = source.match(/EM_JS\(void, StartFramePumpJs, \(std::uintptr_t callback\), \{([\s\S]*?)\n\}\);/u)?.[1];
assert.ok(pumpBody, "The platform frame pump must be present");

function startPump(runFrame)
{
    const frames = [];
    const timers = [];
    const errors = [];
    const context = {
        ABORT: false,
        callback: 1,
        WebAssembly: { promising: fn => fn },
        getWasmTableEntry: () => runFrame,
        requestAnimationFrame: fn => frames.push(fn),
        setTimeout: fn => timers.push(fn),
        handleException: error => errors.push(error),
    };
    runInNewContext(pumpBody, context);
    return { frames, timers, errors, context };
}

test("frame pump waits for suspended Wasm before scheduling another frame", async () => {
    let finish;
    let calls = 0;
    const pump = startPump(() => {
        ++calls;
        return new Promise(resolve => { finish = resolve; });
    });
    const pending = pump.frames.shift()();
    assert.equal(calls, 1);
    assert.equal(pump.frames.length, 0);
    finish();
    await pending;
    assert.equal(pump.frames.length, 1);
    pump.context.kisakStopFramePump();
    await pump.frames.shift()();
    assert.equal(calls, 1);
    assert.equal(pump.frames.length, 0);
});

test("frame pump preserves fatal aborts raised between frames", async () => {
    let calls = 0;
    const pump = startPump(() => { ++calls; });
    await pump.frames.shift()();
    pump.context.ABORT = true;
    await pump.frames.shift()();
    assert.equal(calls, 1);
    assert.equal(pump.frames.length, 0);
});

test("rejected Wasm frames stop and deliver the error through the Worker event loop", async () => {
    const error = new Error("synthetic frame failure");
    const pump = startPump(() => Promise.reject(error));
    await pump.frames.shift()();
    assert.equal(pump.frames.length, 0);
    assert.equal(pump.errors.length, 0);
    assert.equal(pump.timers.length, 1);
    pump.timers.shift()();
    assert.deepEqual(pump.errors, [error]);
});
