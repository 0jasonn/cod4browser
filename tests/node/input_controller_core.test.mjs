import assert from "node:assert/strict";
import test from "node:test";

import { browserTextToEngineCharacters, createInputControllerCore } from "../../web/input_controller_core.mjs";

class EventHub
{
    listeners = new Map();

    addEventListener(type, listener)
    {
        const listeners = this.listeners.get(type) ?? new Set();
        listeners.add(listener);
        this.listeners.set(type, listeners);
    }

    removeEventListener(type, listener)
    {
        this.listeners.get(type)?.delete(listener);
    }

    fire(type, values = {})
    {
        const event = {
            target: this,
            defaultPrevented: false,
            preventDefault() { this.defaultPrevented = true; },
            ...values,
        };
        for (const listener of this.listeners.get(type) ?? []) listener(event);
        return event;
    }
}

function createHarness({ pointerLocked = true, sendInput } = {})
{
    const windowEvents = new EventHub();
    const documentEvents = new EventHub();
    const canvas = new EventHub();
    const textInput = new EventHub();
    canvas.width = 100;
    canvas.height = 50;
    canvas.bounds = { left: 10, top: 20, width: 200, height: 100 };
    canvas.getBoundingClientRect = () => canvas.bounds;
    canvas.focus = () => { documentEvents.activeElement = canvas; };
    textInput.value = "";
    textInput.focus = () => { documentEvents.activeElement = textInput; };
    canvas.requestPointerLock = () => Promise.resolve();
    documentEvents.pointerLockElement = pointerLocked ? canvas : null;
    documentEvents.activeElement = canvas;
    documentEvents.visibilityState = "visible";
    documentEvents.hasFocus = () => true;
    documentEvents.exitPointerLock = () => {
        documentEvents.pointerLockElement = null;
        documentEvents.fire("pointerlockchange");
    };

    let nextFrame = 1;
    const frames = new Map();
    const cancelledFrames = [];
    const requestFrame = (callback) => {
        const handle = nextFrame++;
        frames.set(handle, callback);
        return handle;
    };
    const cancelFrame = (handle) => {
        cancelledFrames.push(handle);
        frames.delete(handle);
    };
    const runFrames = () => {
        const callbacks = [...frames.values()];
        frames.clear();
        for (const callback of callbacks) callback(performance.now());
    };

    const inputs = [];
    const failures = [];
    const originalDocument = globalThis.document;
    const originalAddEventListener = globalThis.addEventListener;
    const originalRemoveEventListener = globalThis.removeEventListener;
    globalThis.document = documentEvents;
    globalThis.addEventListener = windowEvents.addEventListener.bind(windowEvents);
    globalThis.removeEventListener = windowEvents.removeEventListener.bind(windowEvents);
    const controller = createInputControllerCore({
        canvas,
        textInput,
        sendInput: sendInput ?? ((event) => { inputs.push(event); }),
        onFailure: (error) => failures.push(error),
        requestFrame,
        cancelFrame,
    });

    return {
        canvas,
        controller,
        document: documentEvents,
        failures,
        inputs,
        textInput,
        window: windowEvents,
        cancelledFrames,
        runFrames,
        restore() {
            controller.dispose();
            if (originalDocument === undefined) delete globalThis.document;
            else globalThis.document = originalDocument;
            if (originalAddEventListener === undefined) delete globalThis.addEventListener;
            else globalThis.addEventListener = originalAddEventListener;
            if (originalRemoveEventListener === undefined) delete globalThis.removeEventListener;
            else globalThis.removeEventListener = originalRemoveEventListener;
        },
    };
}

function holdInputAndQueueMotion(harness)
{
    harness.window.fire("keydown", { code: "KeyW" });
    harness.canvas.fire("mousedown", { button: 0 });
    harness.window.fire("mousemove", {
        target: harness.canvas,
        movementX: 7,
        movementY: -4,
    });
}

test("text retains keyboard layout, shift and native repeat independently of physical keys", () => {
    const harness = createHarness();
    try {
        harness.window.fire("keydown", { code: "KeyQ", key: "A", shiftKey: true });
        harness.window.fire("keydown", { code: "KeyQ", key: "A", shiftKey: true, repeat: true });
        harness.window.fire("keyup", { code: "KeyQ" });
        harness.window.fire("keydown", { code: "Backspace", key: "Backspace" });
        harness.window.fire("keydown", { code: "Backspace", key: "Backspace", repeat: true });
        harness.window.fire("keyup", { code: "Backspace" });
        assert.deepEqual(harness.inputs, [
            { type: "key", key: 113, down: true }, { type: "char", character: 65 },
            { type: "key", key: 113, down: true }, { type: "char", character: 65 },
            { type: "key", key: 113, down: false },
            { type: "key", key: 127, down: true }, { type: "char", character: 8 },
            { type: "key", key: 127, down: true }, { type: "char", character: 8 },
            { type: "key", key: 127, down: false },
        ]);
    } finally {
        harness.restore();
    }
});

test("composition commits native bytes once, with no intermediate commands or Unicode truncation", () => {
    const harness = createHarness();
    try {
        harness.window.fire("keydown", { code: "Quote", key: "Dead" });
        harness.window.fire("keydown", { code: "KeyE", key: "e", isComposing: true });
        harness.window.fire("compositionend", {
            target: harness.textInput, data: "e\u0301\u20ac\u{1f600}",
        });
        assert.deepEqual(harness.inputs, [
            { type: "char", character: 233 }, { type: "char", character: 128 },
        ]);
        assert.deepEqual(browserTextToEngineCharacters("Äß\u0000\n"), [196, 223]);
        harness.document.activeElement = null;
        harness.document.pointerLockElement = null;
        harness.window.fire("compositionend", {
            target: harness.textInput, data: "ignored",
        });
        assert.equal(harness.inputs.length, 2);
    } finally {
        harness.restore();
    }
});

test("a trusted canvas press activates the editable IME sink without consuming physical input", () => {
    const harness = createHarness({ pointerLocked: false });
    try {
        harness.canvas.fire("mousedown", { button: 0 });
        assert.equal(harness.document.activeElement, harness.textInput);
        harness.window.fire("keydown", {
            target: harness.textInput, code: "KeyW", key: "Process", isComposing: true,
        });
        harness.textInput.value = "pending";
        harness.window.fire("compositionend", {
            target: harness.textInput, data: "e\u0301\u20ac\u{1f600}",
        });
        harness.textInput.fire("input");
        assert.equal(harness.textInput.value, "");
        assert.deepEqual(harness.inputs, [
            { type: "key", key: 0xC8, down: true },
            { type: "char", character: 233 },
            { type: "char", character: 128 },
        ]);
    } finally {
        harness.restore();
    }
});

test("native control shortcuts do not insert letters, while AltGraph permits layout text", () => {
    const harness = createHarness();
    try {
        harness.window.fire("keydown", { code: "KeyC", key: "c", ctrlKey: true });
        harness.window.fire("keyup", { code: "KeyC" });
        harness.window.fire("keydown", { code: "KeyQ", key: "@", ctrlKey: true,
            altKey: true, getModifierState: (name) => name === "AltGraph" });
        harness.window.fire("keyup", { code: "KeyQ" });
        harness.window.fire("keydown", { code: "KeyV", key: "v", metaKey: true });
        assert.deepEqual(harness.inputs.filter((event) => event.type === "char"), [
            { type: "char", character: 3 }, { type: "char", character: 64 },
        ]);
    } finally {
        harness.restore();
    }
});

test("trusted paste snapshots one native line before invoking canonical Field_Paste", () => {
    const harness = createHarness();
    try {
        const keyDown = harness.window.fire("keydown", {
            code: "KeyV", key: "v", ctrlKey: true,
        });
        const paste = harness.window.fire("paste", {
            clipboardData: {
                getData: (type) => type === "text/plain" ? "Äß\nignored" : "",
            },
        });
        harness.window.fire("keyup", { code: "KeyV" });
        assert.equal(keyDown.defaultPrevented, false);
        assert.equal(paste.defaultPrevented, true);
        assert.deepEqual(harness.inputs, [
            { type: "key", key: 0x76, down: true },
            { type: "clipboard", characters: [196, 223] },
            { type: "char", character: 22 },
            { type: "key", key: 0x76, down: false },
        ]);

        harness.inputs.length = 0;
        const insertDown = harness.window.fire("keydown", {
            code: "Insert", key: "Insert", shiftKey: true,
        });
        harness.window.fire("paste", {
            clipboardData: { getData: () => "Again" },
        });
        harness.window.fire("keyup", { code: "Insert", key: "Insert", shiftKey: true });
        assert.equal(insertDown.defaultPrevented, false);
        assert.deepEqual(harness.inputs, [
            { type: "clipboard", characters: [65, 103, 97, 105, 110] },
            { type: "char", character: 22 },
        ]);
    } finally {
        harness.restore();
    }
});

function assertReleasedWithoutMotion(harness)
{
    harness.runFrames();
    assert.deepEqual(harness.inputs, [
        { type: "key", key: 0x77, down: true },
        { type: "key", key: 0xC8, down: true },
        { type: "key", key: 0x77, down: false },
        { type: "key", key: 0xC8, down: false },
    ]);
    assert.equal(harness.cancelledFrames.length, 1);
}

test("relative movement is cancelled on window blur while held input is released", () => {
    const harness = createHarness();
    try {
        holdInputAndQueueMotion(harness);
        harness.window.fire("blur");
        assertReleasedWithoutMotion(harness);
    } finally {
        harness.restore();
    }
});

test("relative movement is cancelled when the document becomes hidden", () => {
    const harness = createHarness();
    try {
        holdInputAndQueueMotion(harness);
        harness.document.visibilityState = "hidden";
        harness.document.fire("visibilitychange");
        assertReleasedWithoutMotion(harness);
    } finally {
        harness.restore();
    }
});

test("relative movement is cancelled on disposal while held input is released", () => {
    const harness = createHarness();
    try {
        holdInputAndQueueMotion(harness);
        harness.controller.dispose();
        assertReleasedWithoutMotion(harness);
    } finally {
        harness.restore();
    }
});

test("relative movement is cancelled after a fatal transport failure", async () => {
    const inputs = [];
    const failure = new Error("transport failed");
    const harness = createHarness({
        sendInput(event) {
            inputs.push(event);
            if (event.type === "key") return Promise.reject(failure);
        },
    });
    try {
        harness.window.fire("mousemove", {
            target: harness.canvas,
            movementX: 3,
            movementY: 2,
        });
        harness.window.fire("keydown", { code: "KeyW" });
        await Promise.resolve();
        await Promise.resolve();
        harness.runFrames();
        assert.deepEqual(inputs, [{ type: "key", key: 0x77, down: true }]);
        assert.deepEqual(harness.failures, [failure]);
        assert.equal(harness.cancelledFrames.length, 1);
    } finally {
        harness.restore();
    }
});

test("absolute coordinates use canonical pixel-index bounds", () => {
    const harness = createHarness({ pointerLocked: false });
    try {
        harness.window.fire("kisakcod:mouse-mode", { detail: { absolute: true } });
        const move = (clientX, clientY) => harness.window.fire("mousemove", {
            target: harness.canvas,
            clientX,
            clientY,
            movementX: 0,
            movementY: 0,
        });
        move(10, 20);
        move(210, 120);
        move(-50, 70);
        move(270, 70);
        move(110, -30);
        move(110, 170);
        assert.deepEqual(harness.inputs.map(({ x, y }) => [x, y]), [
            [0, 0],
            [99, 49],
            [0, 25],
            [99, 25],
            [50, 0],
            [50, 49],
        ]);
    } finally {
        harness.restore();
    }
});

test("absolute movement is suppressed during zero-sized canvas transients", () => {
    const harness = createHarness({ pointerLocked: false });
    try {
        harness.window.fire("kisakcod:mouse-mode", { detail: { absolute: true } });
        harness.canvas.width = 0;
        harness.window.fire("mousemove", {
            target: harness.canvas, clientX: 10, clientY: 20,
        });
        harness.canvas.width = 100;
        harness.canvas.bounds = { left: 10, top: 20, width: 0, height: 100 };
        harness.window.fire("mousemove", {
            target: harness.canvas, clientX: 10, clientY: 20,
        });
        assert.deepEqual(harness.inputs, []);
    } finally {
        harness.restore();
    }
});
