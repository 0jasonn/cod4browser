import { expect, test } from "@playwright/test";

test.skip(process.env.KISAK_WEB_PRODUCT_TEST !== "1", "Runs only against the production site.");

test("production artifact boots without diagnostic browser APIs @product", async ({ page }) => {
    const pageErrors = [];
    const consoleErrors = [];
    await page.addInitScript(() => {
        globalThis.__productStates = [];
        globalThis.addEventListener("kisakcod:state", (event) => {
            globalThis.__productStates.push(structuredClone(event.detail));
        });
    });
    page.on("pageerror", (error) => pageErrors.push(error.message));
    page.on("console", (message) => {
        if (message.type() === "error") consoleErrors.push(message.text());
    });

    await page.goto("/");
    await expect.poll(() => page.locator("html").getAttribute("data-runtime-state"))
        .toBe("running");
    await expect.poll(async () => {
        const text = await page.locator("#frame-counter").textContent();
        return Number(text?.match(/\d+/u)?.[0] ?? 0);
    }).toBeGreaterThan(1);
    await expect(page.locator("#asset-state-label")).toHaveText("Installation required");
    expect(await page.evaluate(() => globalThis.__productStates.some(
        (state) => state.state === "running"))).toBe(true);
    expect(await page.evaluate(() => "__KISAKCOD_WEB__" in globalThis)).toBe(false);
    expect(pageErrors).toEqual([]);
    expect(consoleErrors).toEqual([]);
});

test("production JavaScript exposes only named product operations @product", async ({ request }) => {
    const files = [
        "asset_store.mjs",
        "engine_protocol.mjs",
        "engine_worker.mjs",
        "engine_worker_host.mjs",
        "launcher.mjs",
        "worker_sync_filesystem.mjs",
    ];
    const source = (await Promise.all(files.map(async (file) => {
        const response = await request.get(`/${file}`);
        expect(response.ok()).toBe(true);
        return response.text();
    }))).join("\n");

    for (const forbidden of [
        "callProbe", "testControl", "test-control", "_KisakWeb_Test",
        "filesystem_bridge", "renderer-comparison", "retail-census",
    ]) {
        expect(source).not.toContain(forbidden);
    }
    for (const operation of [
        "mountAssets", "flushAndUnmount", "probeAsset",
        "submitCanonicalCommand", "runtimeStatus", "shutdown",
    ]) {
        expect(source).toContain(operation);
    }
});
