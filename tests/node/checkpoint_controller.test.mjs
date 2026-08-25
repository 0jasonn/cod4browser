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

test("visibility checkpoint coalesces hidden notifications", async () => {
    const documentTarget = new DocumentDouble();
    let checkpoints = 0;
    let finish;
    const pending = new Promise((resolve) => { finish = resolve; });
    const controller = createVisibilityCheckpoint({
        documentTarget,
        isMounted: () => true,
        checkpoint: () => { ++checkpoints; return pending; },
    });
    documentTarget.hide();
    documentTarget.hide();
    await new Promise((resolve) => setTimeout(resolve, 0));
    assert.equal(checkpoints, 1);
    finish();
    assert.equal(await controller.request(), true);
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
