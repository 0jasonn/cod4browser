import assert from "node:assert/strict";
import test from "node:test";

import { createVisibilityCheckpoint } from "../../web/product_checkpoint_controller.mjs";

class DocumentDouble extends EventTarget
{
    visibilityState = "visible";

    hide()
    {
        this.visibilityState = "hidden";
        this.dispatchEvent(new Event("visibilitychange"));
    }
}

test("visibility checkpoint coalesces hidden notifications into one follow-up", async () => {
    const documentTarget = new DocumentDouble();
    let checkpoints = 0;
    let concurrent = 0;
    let maximumConcurrent = 0;
    let finish;
    const pending = new Promise((resolve) => { finish = resolve; });
    const controller = createVisibilityCheckpoint({
        documentTarget,
        isMounted: () => true,
        checkpoint: async () => {
            ++checkpoints;
            maximumConcurrent = Math.max(maximumConcurrent, ++concurrent);
            await pending;
            --concurrent;
        },
    });
    documentTarget.hide();
    documentTarget.hide();
    await new Promise((resolve) => setTimeout(resolve, 0));
    assert.equal(checkpoints, 1);
    finish();
    assert.equal(await controller.request(), true);
    assert.equal(checkpoints, 2);
    assert.equal(maximumConcurrent, 1);
    controller.dispose();
});

test("visibility checkpoint surfaces failure and allows a later retry", async () => {
    const documentTarget = new DocumentDouble();
    const statuses = [];
    let attempts = 0;
    const controller = createVisibilityCheckpoint({
        documentTarget,
        isMounted: () => true,
        checkpoint: () => ++attempts === 1
            ? Promise.reject(new Error("sync failed"))
            : Promise.resolve(),
        onStatus: (status) => statuses.push(status),
    });
    await assert.rejects(controller.request(), /sync failed/u);
    assert.equal(statuses.at(-1).state, "failed");
    assert.equal(await controller.request(), true);
    assert.equal(attempts, 2);
    assert.equal(statuses.at(-1).state, "saved");
    controller.dispose();
});

test("dirty profile checkpoints after the quiet period", async () => {
    const documentTarget = new DocumentDouble();
    let checkpoints = 0;
    const controller = createVisibilityCheckpoint({
        documentTarget,
        isMounted: () => true,
        checkpoint: async () => { ++checkpoints; },
        quietDelayMs: 10,
        maxDirtyAgeMs: 30,
    });
    controller.markDirty();
    await new Promise((resolve) => setTimeout(resolve, 20));
    assert.equal(checkpoints, 1);
    controller.dispose();
});

test("continued writes checkpoint at the maximum dirty age", async () => {
    const documentTarget = new DocumentDouble();
    let checkpoints = 0;
    const controller = createVisibilityCheckpoint({
        documentTarget,
        isMounted: () => true,
        checkpoint: async () => { ++checkpoints; },
        quietDelayMs: 20,
        maxDirtyAgeMs: 45,
    });
    controller.markDirty();
    const writes = setInterval(() => controller.markDirty(), 8);
    await new Promise((resolve) => setTimeout(resolve, 55));
    clearInterval(writes);
    assert.equal(checkpoints, 1);
    controller.dispose();
});

test("dirty writes during a checkpoint cause exactly one follow-up", async () => {
    const documentTarget = new DocumentDouble();
    let checkpoints = 0;
    let finish;
    const first = new Promise((resolve) => { finish = resolve; });
    const controller = createVisibilityCheckpoint({
        documentTarget,
        isMounted: () => true,
        checkpoint: async () => {
            if (++checkpoints === 1) await first;
        },
        quietDelayMs: 10,
        maxDirtyAgeMs: 30,
    });
    controller.markDirty();
    await new Promise((resolve) => setTimeout(resolve, 15));
    controller.markDirty();
    controller.markDirty();
    finish();
    await new Promise((resolve) => setTimeout(resolve, 10));
    assert.equal(checkpoints, 2);
    controller.dispose();
});

test("failed dirty checkpoint remains owned and can be retried", async () => {
    const documentTarget = new DocumentDouble();
    let attempts = 0;
    const controller = createVisibilityCheckpoint({
        documentTarget,
        isMounted: () => true,
        checkpoint: async () => {
            if (++attempts === 1) throw new Error("sync failed");
        },
        quietDelayMs: 10,
        maxDirtyAgeMs: 30,
    });
    controller.markDirty();
    await new Promise((resolve) => setTimeout(resolve, 15));
    assert.equal(attempts, 1);
    assert.equal(await controller.request(), true);
    assert.equal(attempts, 2);
    controller.dispose();
});

test("dispose cancels a pending dirty checkpoint", async () => {
    const documentTarget = new DocumentDouble();
    let checkpoints = 0;
    const controller = createVisibilityCheckpoint({
        documentTarget,
        isMounted: () => true,
        checkpoint: async () => { ++checkpoints; },
        quietDelayMs: 10,
        maxDirtyAgeMs: 30,
    });
    controller.markDirty();
    controller.dispose();
    await new Promise((resolve) => setTimeout(resolve, 20));
    assert.equal(checkpoints, 0);
});
