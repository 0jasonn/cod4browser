import assert from "node:assert/strict";
import test from "node:test";

import {
    createBrowserAssetStore,
    M12_INSTALL_PROFILE,
} from "../../web/asset_store.mjs";
import {
    isAdditionalSinglePlayerFastfile,
} from "../../web/asset_profile.mjs";

test("the default asset profile is versioned and excludes multiplayer zones", () => {
    assert.equal(M12_INSTALL_PROFILE.version, 1);
    assert.equal(M12_INSTALL_PROFILE.product, "offline-single-player");
    assert.equal(isAdditionalSinglePlayerFastfile("zone/english/cargoship.ff"), true);
    assert.equal(isAdditionalSinglePlayerFastfile("zone/english/mp_crash.ff"), false);
    assert.equal(isAdditionalSinglePlayerFastfile("zone/english/crash_mp.ff"), false);
});

test("asset-store disposal is idempotent and rejects later operations", async () => {
    let flushes = 0;
    const store = createBrowserAssetStore({
        async flushAndUnmount() { ++flushes; },
    });
    const first = store.dispose();
    assert.equal(store.dispose(), first);
    await first;
    assert.equal(flushes, 1);
    await assert.rejects(store.initialize(), (error) => error.code === "STORE_DISPOSED");
});
