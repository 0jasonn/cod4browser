import { expect, test } from "@playwright/test";

test("canonical saved-screen commands preserve capture order, flash arithmetic and temporal blur", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    // Isolate saved-screen arithmetic from the independently tested display ramp.
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.submitCanonicalCommand("r_gamma 1; echo saved-screen-gamma-ready"));
    await expect(page.locator("#boot-log")).toContainText("saved-screen-gamma-ready");
    const sample = async (action, time, fade = 200, white = 0, grab = 0.5, bottom = 0) => {
        const packed = await page.evaluate(args => globalThis.__KISAKCOD_WEB__.module.call(
            "_KisakWeb_TestSavedScreen", ...args), [action, time, fade, white, grab, bottom]);
        expect(packed >>> 24, "WebGL error while capturing/drawing saved screen").toBe(0);
        return [packed & 255, (packed >>> 8) & 255, (packed >>> 16) & 255];
    };
    const close = (actual, expected) => actual.forEach((value, i) =>
        expect(Math.abs(value - expected[i]), `pixel ${actual}, expected ${expected}`).toBeLessThanOrEqual(2));

    // Independent expected pixels from native RB packing and owned sm3 shader
    // arithmetic. Synthetic solid colors contain no game data; tolerance is two
    // RGBA8 units for blend/UNORM rounding across WebGL implementations.
    expect(await sample(0, 1000)).toEqual([0, 0, 255]);
    close(await sample(1, 1010), [128, 0, 0]);
    close(await sample(6, 1010), [128, 0, 255]);
    close(await sample(1, 1010, 200, 0, 0.5, 1), [0, 128, 0]);
    close(await sample(1, 1020, 200, 0.25, 0.5), [192, 64, 64]);
    expect(await sample(1, 1020, 200, 1, 1)).toEqual([255, 255, 255]);
    close(await sample(2, 1000), [208, 19, 22]);
    close(await sample(2, 1100), [21, 2, 231]);
    expect(await sample(2, 1200)).toEqual([0, 0, 255]);
    expect(await sample(2, 999)).toEqual([0, 0, 255]);
    expect(await sample(2, 1100, 0)).toEqual([0, 0, 255]);
    close(await sample(5, 1100), [21, 2, 231]);
    close(await sample(1, 1110, 200, 0, 1), [21, 2, 231]);

    await sample(0, 2000);
    expect(await sample(3, 2010)).toEqual([0, 0, 255]);
    close(await sample(1, 2020), [128, 128, 0]);
    close(await sample(1, 2020, 200, 0, 0.5, 1), [0, 128, 0]);
    await sample(4, 2100);

    await sample(0, 3000);
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestLoseWebGLContext"))).toBe(1);
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("renderer-lost");
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestRestoreWebGLContext"))).toBe(1);
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("running");
    expect(await sample(1, 3010, 200, 0, 1)).toEqual([0, 0, 0]);
    // A section saved with timer 1 is still the shared feedback image used by
    // flashed commands, which intentionally carry no timer ID.
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestUnloadWorldResources"));
    await sample(3, 3100);
    close(await sample(1, 3110), [128, 128, 0]);
    await sample(0, 3200);
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.resize(800, 600));
    // Host resize requests are applied by the platform at the next engine
    // frame, together with canonical screen placement and cgame dimensions.
    await expect.poll(() => page.locator("#game-canvas").evaluate(
        (canvas) => [canvas.width, canvas.height])).toEqual([800, 600]);
    expect(await sample(1, 3210, 200, 0, 1)).toEqual([0, 0, 0]);
});
