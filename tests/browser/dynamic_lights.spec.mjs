import { expect, test } from "@playwright/test";

test("transient light material passes preserve additive, cone, alpha and attenuation arithmetic", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const pixel = async (style = 0, alpha = 255, repeats = 1, gradient = 0) => {
        const packed = await page.evaluate(args => globalThis.__KISAKCOD_WEB__.module.call(
            "_KisakWeb_TestDynamicLightPixel", ...args), [style, alpha, repeats, gradient]);
        expect(packed >>> 24, "WebGL error in transient material pass").toBe(0);
        return [packed & 255, (packed >>> 8) & 255, (packed >>> 16) & 255];
    };
    const close = (actual, expected) => actual.forEach((value, i) =>
        expect(Math.abs(value - expected[i]), `pixel ${actual}, expected ${expected}`).toBeLessThanOrEqual(2));
    // Expected arithmetic from owned l_omni/l_spot bytecode; fixtures contain
    // only solid colors and an authored synthetic 2x2 gradient. Two byte units
    // allow normalized texture/blend rounding across WebGL implementations.
    // The first draw also forces ANGLE's backend executable compilation. Run
    // this on D3D11 after sampler edits: GLSL link success alone missed HLSL
    // signature collisions between 3D, cube and shadow sampler overloads.
    close(await pixel(), [64, 128, 191]);
    close(await pixel(1), [64, 128, 191]);
    expect(await pixel(2)).toEqual([0, 0, 0]);
    close(await pixel(0, 255, 2), [128, 255, 255]);
    close(await pixel(3, 128), [16, 32, 48]);
    expect(await pixel(4, 127)).toEqual([0, 0, 0]);
    close(await pixel(4, 128), [64, 128, 191]);
    close(await pixel(0, 255, 1, 1), [32, 64, 96]);
    close(await pixel(5), [32, 64, 96]); // Vertex fog is 0.5; the center is at the eye.
    close(await pixel(6), [21, 43, 64]); // AG=1 encodes slopes (2, 2), so N.L=1/3.
    close(await pixel(8, 255, 2), [64, 128, 191]); // Native destination-alpha coverage.
    close(await pixel(9, 255, 2), [128, 255, 255]); // A new light resets coverage only.
    expect(await pixel(14)).toEqual([0, 0, 0]); // Sphere beyond omni radius sum.
    close(await pixel(15), [64, 128, 191]); // Tangent spheres still receive.
    expect(await pixel(16)).toEqual([0, 0, 0]); // Sphere outside spot near plane.
    close(await pixel(17), [64, 128, 191]); // Tangent to near plane still receives.
    expect(await pixel(18)).toEqual([0, 0, 0]); // Brush outside omni radius.
    close(await pixel(19), [64, 128, 191]); // Box/omni tangent included.
    expect(await pixel(20)).toEqual([0, 0, 0]); // Box/spot outside contact rejected.
    close(await pixel(21), [64, 128, 191]); // Brush crosses the spot near plane.
    expect(await pixel(22)).toEqual([0, 0, 0]); // Posed DObj box outside omni radius.
    close(await pixel(23), [64, 128, 191]); // DObj box/omni tangent included.
    expect(await pixel(24)).toEqual([0, 0, 0]); // DObj box/spot contact rejected.
    close(await pixel(25), [64, 128, 191]); // DObj crosses the spot near plane.
});
