import { expect, test } from "@playwright/test";

test("save JPEG codec preserves orientation and its canonical image survives context recovery", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const call = operation => page.evaluate(operation =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestSaveImage", operation), operation);
    expect(await call(0)).toBe(1);
    await expect.poll(() => call(1)).toBe(15);
    const verifyPixels = async () => {
        const top = await call(2), bottom = await call(3);
        expect(top >>> 24).toBe(0);
        expect(bottom >>> 24).toBe(0);
        expect(top & 255).toBeGreaterThan(250);
        expect((top >>> 8) & 255).toBeLessThan(3);
        expect(bottom & 255).toBeLessThan(3);
        expect((bottom >>> 8) & 255).toBeGreaterThan(250);
    };
    await verifyPixels();
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestLoseWebGLContext"))).toBe(1);
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("renderer-lost");
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestRestoreWebGLContext"))).toBe(1);
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("running");
    await verifyPixels();
    expect(await call(4)).toBe(1);
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestUnloadWorldResources"))).toBe(1);
    expect(await call(5)).toBe(0);
});
