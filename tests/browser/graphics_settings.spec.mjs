import { expect, test } from "@playwright/test";

test("multiply-fog material preserves native pass pixels and recovers its WebGL context", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const call = (name, ...args) => page.evaluate(({ name, args }) =>
        globalThis.__KISAKCOD_WEB__.module.call(name, ...args), { name, args });
    // D3D9 executing the encountered native mul / mul_fog vertex and pixel
    // programs with synthetic inputs. No proprietary program or image fixture
    // is shipped. Allow one UNORM quantization step between the two backends.
    const native = [[95,60,30,255], [100,76,61,255], [95,60,30,255],
        [95,60,30,255], [153,102,51,255], [48,50,72,255],
        [69,68,80,255], [95,60,30,255], [100,76,61,255]];
    const check = async () => {
        for (const [scenario, expected] of native.entries()) {
            const pixel = (await call("_KisakWeb_TestMultiplyFogPixel", scenario)) >>> 0;
            const actual = [pixel & 255, (pixel >>> 8) & 255, (pixel >>> 16) & 255, pixel >>> 24];
            actual.forEach((value, i) => expect(Math.abs(value - expected[i]),
                `scenario ${scenario}, channel ${i}: ${actual}`).toBeLessThanOrEqual(1));
        }
    };
    await check();
    expect(await call("_KisakWeb_TestLoseWebGLContext")).toBe(1);
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("renderer-lost");
    expect(await call("_KisakWeb_TestRestoreWebGLContext")).toBe(1);
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("running");
    await check();
});

test("authored distortion projects its basis, rejects foreground offsets and resolves MSAA", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const call = (name, ...args) => page.evaluate(({ name, args }) =>
        globalThis.__KISAKCOD_WEB__.module.call(name, ...args), { name, args });
    const sample = (scenario, field = 0) => call("_KisakWeb_TestSoftParticlePixel", scenario, field);
    const check = async (scenario, expected) => {
        const rgb = await sample(scenario);
        expect(rgb >>> 24).toBe(0);
        const actual = [rgb & 255, (rgb >>> 8) & 255, (rgb >>> 16) & 255, await sample(scenario, 1)];
        actual.forEach((value, index) => expect(Math.abs(value - expected[index]),
            `scenario ${scenario} channel ${index}: ${value}`).toBeLessThanOrEqual(2));
    };
    // Linear source texels are (32+64*x, 32+64*y, 192). At pixel (1,1),
    // each projected unit basis shifts half a texel; blue tints the result.
    for (const scenario of [20, 22, 29]) await check(scenario, [64, 32, 96, 64]);
    expect(await sample(29, 3)).toBe(0xc06060); // Snapshot survives source clear/draw.
    await check(21, [48, 48, 96, 64]);
    await check(23, [32, 64, 96, 64]);
    await check(24, [48, 32, 96, 64]);
    await check(25, [64, 48, 96, 64]);
    await check(26, [32, 16, 48, 64]);
    await check(27, [64, 32, 96, 128]);
    await check(30, [51, 51, 96, 64]); // Projected tangent changes lookup W too.
    await check(31, [80, 16, 96, 64]); // Resize: same projected offset is one texel.
    await check(20, [64, 32, 96, 64]); // Both targets return to the smaller size.
    const command = text => page.evaluate(text =>
        globalThis.__KISAKCOD_WEB__.submitCanonicalCommand(text), text);
    await command("r_zFeather 0");
    await check(20, [64, 32, 96, 64]); // Distortion's depth check is independent.
    await command("r_distortion 0");
    await expect.poll(() => sample(20, 1)).toBe(0);
    await check(20, [0, 0, 0, 0]);
    expect(await call("_KisakWeb_TestLoseWebGLContext")).toBe(1);
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("renderer-lost");
    expect(await call("_KisakWeb_TestRestoreWebGLContext")).toBe(1);
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("running");
    await check(20, [0, 0, 0, 0]);
    await command("r_distortion 1");
    await expect.poll(() => sample(20, 1)).toBe(64);
    await check(29, [64, 32, 96, 64]);
});

test("authored outdoor particle clouds use the world lookup and inclusive height mask", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const call = (scenario, field = 0) => page.evaluate(({ scenario, field }) =>
        globalThis.__KISAKCOD_WEB__.module.call(
            "_KisakWeb_TestOutdoorParticleCloudPixel", scenario, field),
    { scenario, field });
    const rgba = async scenario => {
        const packed = (await call(scenario)) >>> 0;
        return [packed & 255, (packed >>> 8) & 255,
            (packed >>> 16) & 255, packed >>> 24];
    };
    expect(await call(0, 1)).toBe(1);
    expect(await call(0, 2)).toBe(18);
    expect(await rgba(0)).toEqual([64, 64, 48, 64]);
    expect(await rgba(1)).toEqual([64, 64, 48, 0]);
    expect(await rgba(2)).toEqual([64, 64, 48, 64]);
    expect(await rgba(3)).toEqual([64, 64, 48, 0]);
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestLoseWebGLContext"))).toBe(1);
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("renderer-lost");
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestRestoreWebGLContext"))).toBe(1);
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("running");
    expect(await rgba(0)).toEqual([64, 64, 48, 64]);
});

test("authored soft-particle depth, variants and recovery preserve pixels", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const call = (name, ...args) => page.evaluate(({ name, args }) =>
        globalThis.__KISAKCOD_WEB__.module.call(name, ...args), { name, args });
    const sample = (scenario, field = 0) => call("_KisakWeb_TestSoftParticlePixel", scenario, field);
    const check = async (scenario, expected) => {
        const rgb = await sample(scenario);
        expect(rgb >>> 24).toBe(0);
        const actual = [rgb & 255, (rgb >>> 8) & 255, (rgb >>> 16) & 255, await sample(scenario, 1)];
        actual.forEach((value, index) => expect(Math.abs(value - expected[index])).toBeLessThanOrEqual(2));
    };
    for (const scenario of [0, 1, 2, 10, 11]) await check(scenario, [64, 128, 192, 128]);
    await check(3, [64, 128, 192, 64]);
    await check(4, [32, 64, 96, 128]);
    await check(5, [45, 90, 134, 128]);
    await check(6, [7, 14, 21, 128]);
    await check(7, [64, 128, 192, 143]);
    for (const scenario of [8, 9]) await check(scenario, [64, 128, 192, 255]);
    await check(13, [64, 128, 192, 64]);
    await check(14, [7, 14, 21, 143]);
    await check(15, [64, 128, 192, 16]);
    for (const [scenario, expected] of [[0, 3], [1, 1], [2, -3], [10, 3]]) {
        const bits = await sample(scenario, 2);
        const bytes = new ArrayBuffer(4), view = new DataView(bytes);
        view.setUint32(0, bits, true);
        expect(view.getFloat32(0, true)).toBeCloseTo(expected, 6);
    }
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.submitCanonicalCommand("r_zFeather 0"));
    await expect.poll(() => sample(0, 1)).toBe(255);
    await check(3, [64, 128, 192, 255]);
    await call("_KisakWeb_TestLoseWebGLContext");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("renderer-lost");
    await call("_KisakWeb_TestRestoreWebGLContext");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("running");
    await check(0, [64, 128, 192, 255]);
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.submitCanonicalCommand("r_zFeather 1"));
    await expect.poll(() => sample(0, 1)).toBe(128);
    await check(11, [64, 128, 192, 128]);
});

test("cinematic code images preserve plane colour, alpha, interpolation and recovery", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const call = (name, ...args) => page.evaluate(({ name, args }) =>
        globalThis.__KISAKCOD_WEB__.module.call(name, ...args), { name, args });
    const movie = (op) => call("_KisakWeb_TestCinematicState", op);
    const check = async (expected) => {
        for (const mode of [20, 22]) { // World state and UI state use the same planes.
            const rgb = await movie(mode);
            expect(rgb >>> 24).toBe(0);
            const actual = [rgb & 255, (rgb >>> 8) & 255, (rgb >>> 16) & 255, await movie(mode + 1)];
            actual.forEach((value, i) => expect(Math.abs(value - expected[i])).toBeLessThanOrEqual(2));
        }
    };
    expect(await movie(10)).toBe(1);
    await check([127, 0, 0, 128]); // Limited-range red, opaque movie, vertex tint.
    expect(await movie(11)).toBe(1);
    await check([0, 255, 0, 64]); // New frame at the same image identities, explicit alpha.
    expect(await movie(15)).toBe(0); // Invalid last-plane stride must leave all planes intact.
    expect(await movie(16)).toBe(0); // Oversized decoded dimensions never reach upload.
    await check([0, 255, 0, 64]);
    expect(await movie(12)).toBe(1);
    await check([64, 129, 32, 32]); // Four chroma texels interpolate at odd-size centre.
    await call("_KisakWeb_TestLoseWebGLContext");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("renderer-lost");
    // Upload while lost: restoration must use retained R8 planes, not stale GL handles.
    expect(await movie(10)).toBe(1);
    await call("_KisakWeb_TestRestoreWebGLContext");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("running");
    await check([127, 0, 0, 128]);
    expect(await movie(13)).toBe(1);
    await check([64, 128, 32, 32]);
    expect(await movie(14)).toBe(1);
    await check([0, 0, 0, 0]); // Native inactive Y/Cr/Cb/A defaults.
    const idleTexture = await movie(24);
    expect(await movie(14)).toBe(1);
    expect(await movie(24)).toBe(idleTexture); // Canonical idle stop runs every game frame.
});

test("native texture quality selects authored mip pixels and reduces GL upload residency", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const texture = (semantic = 2, field = 0, noPicmip = 0, recovery = 0) =>
        page.evaluate((args) => globalThis.__KISAKCOD_WEB__.module.call(
            "_KisakWeb_TestPicmipTexture", ...args), [semantic, noPicmip, field, recovery]);
    const command = async (text) => {
        await page.locator("#engine-command-input").fill(text);
        await page.locator("#engine-command-form").evaluate((form) => form.requestSubmit());
        await expect(page.locator("#engine-command-status")).toHaveText(`Accepted: ${text}`);
    };
    expect(await texture()).toBe(32);
    expect(await texture(2, 2)).toBe(5460);
    expect(await texture(2, 3)).toBe(255);
    await command("r_picmip_manual 1; r_picmip 1; r_picmip_bump 2; r_picmip_spec 3");
    await expect.poll(() => texture()).toBe(16);
    for (const [semantic, edge, bytes, pixel] of [[2, 16, 1364, 0x2800d7],
        [5, 8, 340, 0x5000af], [8, 4, 84, 0x780087]]) {
        expect(await texture(semantic)).toBe(edge);
        expect(await texture(semantic, 1)).toBe(bytes);
        expect(await texture(semantic, 2)).toBe(bytes);
        expect(await texture(semantic, 3)).toBe(pixel);
        expect(await texture(semantic, 3, 0, 1)).toBe(pixel);
        expect(await texture(semantic, 4)).toBe(5460); // Recovery retains the original encoded source.
    }
    expect(await texture(2, 0, 1)).toBe(32);
    expect(await texture(0)).toBe(32); // Native non-color semantics bypass picmip.
    await command("r_picmip_manual 0");
    await expect.poll(() => texture()).toBe(32);
    expect(await texture(2, 3)).toBe(255);
});

test("canonical filtering and normal settings affect WebGL state and light pixels", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const call = (name, ...args) => page.evaluate(({ name, args }) =>
        globalThis.__KISAKCOD_WEB__.module.call(name, ...args), { name, args });
    const sampler = (bits, field = 0, mips = 1) =>
        call("_KisakWeb_TestSamplerState", bits, mips, field);
    const command = async (text) => {
        await page.locator("#engine-command-input").fill(text);
        await page.locator("#engine-command-form").evaluate((form) => form.requestSubmit());
        await expect(page.locator("#engine-command-status")).toHaveText(`Accepted: ${text}`);
    };
    const maxAniso = await sampler(0x12, 3);
    await command('r_texFilterMipMode "Force Trilinear"');
    await expect.poll(() => sampler(0x0a)).toBe(0x2703);
    await command('r_texFilterMipMode "Force Bilinear"');
    await expect.poll(() => sampler(0x12)).toBe(0x2701);
    for (const target of [0x100, 0x200]) { // Cubemaps and model-lighting volumes.
        expect(await sampler(0x12, target)).toBe(0x2701);
        expect(await sampler(0xe2, target, 0)).toBe(0x2601);
        expect(await sampler(0xe2, target + 4, 0)).toBe(0x812f);
        expect(await sampler(0x02, target + 4, 0)).toBe(0x2901);
    }
    await command("r_texFilterAnisoMax 4; r_texFilterAnisoMin 4");
    await expect.poll(() => sampler(0x12, 2)).toBe(Math.min(4, maxAniso));
    expect(await sampler(0x12, 0x102)).toBe(Math.min(4, maxAniso));
    expect(await sampler(0x12, 0x202)).toBe(Math.min(4, maxAniso));
    expect(await sampler(0x02, 2, 0)).toBe(1);
    await command("r_texFilterAnisoMax 1");
    await expect.poll(() => sampler(0x14, 2)).toBe(1);
    const pixel = (style = 0, gradient = 0) =>
        call("_KisakWeb_TestDynamicLightPixel", style, 255, 1, gradient);
    const normalOn = await pixel(6);
    expect(await pixel(1)).not.toBe(0);
    expect(await pixel(12)).toBe(0);
    expect(await pixel(13)).toBe(await pixel(0));
    await command("r_normal 0");
    await expect.poll(() => pixel(6)).toBe(0xbf8040);
    await command("r_normal 1");
    await expect.poll(() => pixel(6)).toBe(normalOn);
    const detailOn = await pixel(10);
    await command("r_detail 0");
    await expect.poll(() => pixel(10)).not.toBe(detailOn);
    await command("r_detail 1");
    await expect.poll(() => pixel(10)).toBe(detailOn);
    const linear = await pixel(0, 1);
    await command("r_texFilterDisable 1");
    await expect.poll(() => sampler(0x14)).toBe(0x2600);
    expect(await sampler(0x12, 0x100)).toBe(0x2600);
    expect(await sampler(0xe2, 0x200, 0)).toBe(0x2600);
    expect(await sampler(0x14, 1)).toBe(0x2600);
    expect(await pixel(0, 1)).not.toBe(linear);
    await command("r_texFilterDisable 0");
    await expect.poll(() => pixel(0, 1)).toBe(linear);
    await call("_KisakWeb_TestLoseWebGLContext");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("renderer-lost");
    await call("_KisakWeb_TestRestoreWebGLContext");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("running");
    expect(await sampler(0x12)).toBe(0x2701);
    expect(await sampler(0x14, 2)).toBe(1);
    expect(await sampler(0x12, 0x100)).toBe(0x2701);
    expect(await sampler(0xe2, 0x200, 0)).toBe(0x2601);
    expect(await pixel(6)).toBe(normalOn);
});

test("canonical mip bias changes sampled levels and survives context recovery", async ({ page }) => {
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const call = (name, ...args) => page.evaluate(({ name, args }) =>
        globalThis.__KISAKCOD_WEB__.module.call(name, ...args), { name, args });
    const pixel = (style = 11) => call("_KisakWeb_TestDynamicLightPixel", style, 255, 1, 0);
    const command = async (text) => {
        await page.locator("#engine-command-input").fill(text);
        await page.locator("#engine-command-form").evaluate((form) => form.requestSubmit());
        await expect(page.locator("#engine-command-status")).toHaveText(`Accepted: ${text}`);
    };
    await command('r_texFilterAnisoMin 1; r_texFilterAnisoMax 1; r_texFilterMipMode "Force Bilinear"');
    await expect.poll(() => pixel()).toBe(0x008000); // Two texels per pixel: level 1.
    await command("r_texFilterMipBias 1");
    await expect.poll(() => pixel()).toBe(0xbf0000); // Level 2.
    expect(await pixel(0)).toBe(0xbf8040); // A single-level texture is unchanged.
    await command("r_texFilterMipBias -1");
    await expect.poll(() => pixel()).toBe(0x000040); // Level 0.
    await call("_KisakWeb_TestLoseWebGLContext");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("renderer-lost");
    await call("_KisakWeb_TestRestoreWebGLContext");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("running");
    expect(await pixel()).toBe(0x000040);
    await command('r_texFilterMipMode "Force Trilinear"; r_texFilterMipBias 0.5');
    await expect.poll(async () => {
        const rgb = await pixel();
        return Math.abs(((rgb >>> 8) & 255) - 64) <= 2 &&
            Math.abs(((rgb >>> 16) & 255) - 96) <= 2 && (rgb & 255) === 0;
    }).toBe(true);
});
