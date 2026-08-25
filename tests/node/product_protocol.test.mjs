import assert from "node:assert/strict";
import test from "node:test";

import {
    ENGINE_PROTOCOL_VERSION,
    PRODUCT_OPERATIONS,
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
    assert.equal(validateProductRequest(request("input", {
        event: { type: "key", key: 32, down: true },
    })).type, "input");
    assert.equal(validateProductRequest(request("runtimeStatus")).type, "runtimeStatus");
    assert.equal(validateProductRequest(request("shutdown")).type, "shutdown");
    assert.deepEqual(new Set(PRODUCT_OPERATIONS), new Set([
        "init", "mountAssets", "flushAndUnmount", "checkpoint", "probeAsset",
        "submitCanonicalCommand", "resize", "input", "runtimeStatus", "shutdown",
    ]));
});

test("the production protocol rejects malformed and diagnostic requests", () => {
    const rejects = [
        [null, "INVALID_PAYLOAD"],
        [{ type: "runtimeStatus", id: 1 }, "PROTOCOL_VERSION"],
        [request("call", { functionName: "_KisakWeb_TestAnything" }), "UNKNOWN_OPERATION"],
        [request("test-control"), "UNKNOWN_OPERATION"],
        [request("runtimeStatus", { id: 0 }), "INVALID_PAYLOAD"],
        [request("resize", { width: 0, height: 720 }), "INVALID_PAYLOAD"],
        [request("input", { event: { type: "key", key: -1, down: true } }),
            "INVALID_PAYLOAD"],
        [request("input", { event: { type: "mouse-move", x: 0, y: 0, dx: Infinity, dy: 0 } }),
            "INVALID_PAYLOAD"],
        [request("probeAsset", {
            kind: "iwd", buffers: [new ArrayBuffer(1)], metadata: {},
        }), "INVALID_PAYLOAD"],
    ];
    for (const [message, code] of rejects) {
        assert.throws(() => validateProductRequest(message), (error) => error.code === code);
    }
});
