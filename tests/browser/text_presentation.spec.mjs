import { expect, test } from "@playwright/test";

test("native text commands retain styles, subtitle glow, console rings and timed effects", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    // These assertions measure native glyph colors with a neutral display ramp.
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.submitCanonicalCommand("r_gamma 1; echo text-gamma-ready"));
    await expect(page.locator("#boot-log")).toContainText("text-gamma-ready");
    const sample = (scenario, time = 501, field = 0, index = 0) => page.evaluate(
        args => globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestTextDraw", ...args),
        [scenario, time, field, index]);
    expect(await sample(0)).toBe(2);
    expect(await sample(0, 501, 1)).toBeCloseTo(19.5, 3);
    expect(await sample(0, 501, 2)).toBeCloseTo(19.5, 3);
    expect(await sample(1)).toBe(4);
    expect(await sample(1, 501, 1)).toBeCloseTo(20.5, 3);
    expect(await sample(2, 501, 1)).toBeCloseTo(21.5, 3);
    expect(await sample(3, 501, 1, 1)).toBeCloseTo(31.5, 3);
    expect(await sample(9, 501, 1)).toBeCloseTo(29.5, 3);
    expect(await sample(4)).toBe(7);
    expect(await sample(4, 501, 6, 1)).toBe(220);
    expect(await sample(6)).toBe(3); // ^2ABC crosses the byte-ring boundary
    expect(await sample(6, 501, 4)).toBe(255);
    expect(await sample(6, 501, 3)).toBe(0);
    expect(await sample(7)).toBe(15); // three base glyphs and four glow quads each
    for (const scenario of [5, 8]) {
        expect(await sample(scenario, 100)).toBe(1);
        expect(await sample(scenario, 250)).toBe(2);
        expect(await sample(scenario, 501)).toBe(scenario === 5 ? 4 : 3);
        expect(await sample(scenario, 2101)).toBe(0);
    }
    const pixel = await sample(4, 501, 100);
    expect(pixel >>> 24, "WebGL error drawing canonical text").toBe(0);
    const rgb = [pixel & 255, pixel >>> 8 & 255, pixel >>> 16 & 255];
    rgb.forEach((value, i) => expect(Math.abs(value - [255, 92, 92][i])).toBeLessThanOrEqual(2));
});
