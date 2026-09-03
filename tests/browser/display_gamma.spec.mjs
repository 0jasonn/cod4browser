import { expect, test } from "@playwright/test";

test("final menu pixels follow the native gamma ramp and bypass", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    for (const gamma of [0.5, 0.8, 1, 2, 3]) {
        for (const level of [0, 32, 64, 128, 192, 255]) {
            const result = await page.evaluate(({ level, gamma }) =>
                globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestDisplayGamma", level, gamma, 0),
            { level, gamma });
            const expected = Math.floor(result / 4294967296), pixel = result >>> 0;
            expect(pixel >>> 24, `WebGL error: gamma=${gamma}, input=${level}`).toBe(0);
            for (const shift of [0, 8, 16])
                // Native 16-bit LUT -> byte truncation and framebuffer UNORM
                // rounding can differ by one unit. The reference is shared C++.
                expect(Math.abs(((pixel >>> shift) & 255) - expected), `gamma=${gamma}, input=${level}`)
                    .toBeLessThanOrEqual(1);
        }
    }
    const bypass = await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestDisplayGamma", 128, 2, 1));
    expect(bypass >>> 0).toBe(0x808080);
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestLoseWebGLContext"));
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("renderer-lost");
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestRestoreWebGLContext"));
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("running");
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.resize(800, 600));
    const restored = await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestDisplayGamma", 128, 2, 0));
    expect(restored >>> 24 & 255).toBe(0);
    expect(Math.abs((restored & 255) - Math.floor(restored / 4294967296))).toBeLessThanOrEqual(1);
});
