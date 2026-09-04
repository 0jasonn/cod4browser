import assert from "node:assert/strict";
import test from "node:test";

import {
    ENGINE_PROTOCOL_VERSION,
    PRODUCT_OPERATIONS,
    PRODUCT_ONE_WAY_OPERATIONS,
    validateProductRequest,
} from "../../web/product_protocol.mjs";

const request = (type, payload = {}) => ({
    protocolVersion: ENGINE_PROTOCOL_VERSION,
    type,
    id: 1,
    ...payload,
});

test("the production protocol accepts only its explicit operations", () => {
    const canvas = {};
    assert.equal(validateProductRequest({
        protocolVersion: ENGINE_PROTOCOL_VERSION,
        type: "init",
        canvas,
    }, { isCanvas: (value) => value === canvas }).canvas, canvas);
    assert.equal(validateProductRequest(request("mountAssets", { manifest: {} })).type,
        "mountAssets");
    assert.equal(validateProductRequest(request("flushAndUnmount")).type, "flushAndUnmount");
    assert.equal(validateProductRequest(request("checkpoint")).type, "checkpoint");
    assert.equal(validateProductRequest(request("probeAsset", {
        kind: "fastfile", buffers: [new ArrayBuffer(8)], metadata: {},
    })).type, "probeAsset");
    assert.equal(validateProductRequest(request("submitCanonicalCommand", {
        command: "map killhouse",
    })).type, "submitCanonicalCommand");
    assert.equal(validateProductRequest(request("resize", { width: 1280, height: 720 })).type,
        "resize");
    assert.equal(validateProductRequest(request("runtimeStatus")).type, "runtimeStatus");
    assert.equal(validateProductRequest(request("shutdown")).type, "shutdown");
    assert.deepEqual(new Set(PRODUCT_OPERATIONS), new Set([
        "init", "mountAssets", "flushAndUnmount", "checkpoint", "probeAsset",
        "submitCanonicalCommand", "resize", "runtimeStatus", "shutdown",
    ]));
    assert.deepEqual(new Set(PRODUCT_ONE_WAY_OPERATIONS), new Set(["input-event"]));
    assert.equal(validateProductRequest({
        protocolVersion: ENGINE_PROTOCOL_VERSION,
        type: "input-event",
        event: { type: "key", key: 0xC8, down: true },
    }).type, "input-event");
});

test("the production protocol rejects malformed and diagnostic requests", () => {
    const rejects = [
        [null, "INVALID_PAYLOAD"],
        [{ type: "runtimeStatus", id: 1 }, "PROTOCOL_VERSION"],
        [request("call", { functionName: "_KisakWeb_TestAnything" }), "UNKNOWN_OPERATION"],
        [request("test-control"), "UNKNOWN_OPERATION"],
        [request("runtimeStatus", { id: 0 }), "INVALID_PAYLOAD"],
        [request("resize", { width: 0, height: 720 }), "INVALID_PAYLOAD"],
        [{
            protocolVersion: ENGINE_PROTOCOL_VERSION,
            type: "input-event",
            event: { type: "key", key: -1, down: true },
        },
            "INVALID_PAYLOAD"],
        [{
            protocolVersion: ENGINE_PROTOCOL_VERSION,
            type: "input-event",
            event: { type: "mouse-move", x: 0, y: 0, dx: Infinity, dy: 0 },
        },
            "INVALID_PAYLOAD"],
        [request("probeAsset", {
            kind: "iwd", buffers: [new ArrayBuffer(1)], metadata: {},
        }), "INVALID_PAYLOAD"],
    ];
    for (const [message, code] of rejects) {
        assert.throws(() => validateProductRequest(message), (error) => error.code === code);
    }
});

test("character transport is bounded to native nonzero bytes", () => {
    const input = (character) => ({ protocolVersion: ENGINE_PROTOCOL_VERSION,
        type: "input-event", event: { type: "char", character } });
    for (const character of [1, 8, 32, 65, 233, 255]) {
        assert.equal(validateProductRequest(input(character)).event.character, character);
    }
    for (const character of [0, -1, 256, 0x400, NaN, 1.5, "65", undefined]) {
        assert.throws(() => validateProductRequest(input(character)),
            (error) => error.code === "INVALID_PAYLOAD");
    }
});

test("clipboard transport accepts one bounded native byte line", () => {
    const input = (characters) => ({ protocolVersion: ENGINE_PROTOCOL_VERSION,
        type: "input-event", event: { type: "clipboard", characters } });
    assert.deepEqual(validateProductRequest(input([32, 65, 128, 255])).event.characters,
        [32, 65, 128, 255]);
    assert.equal(validateProductRequest(input(Array(4095).fill(65))).event.characters.length,
        4095);
    for (const characters of [[], [31], [127], [256], [65.5], ["65"],
        Array(4096).fill(65), "text", undefined]) {
        assert.throws(() => validateProductRequest(input(characters)),
            (error) => error.code === "INVALID_PAYLOAD");
    }
});
