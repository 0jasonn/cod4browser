import { expect, test } from "@playwright/test";

async function observeAntiAliasing(page)
{
    await page.addInitScript(() => {
        globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ = {
            ...(globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ ?? {}),
            observeAa: true,
        };
        globalThis.__kisakcodAaLifecycle = [];
        globalThis.__kisakcodAaGlOperations = [];
        globalThis.addEventListener("kisakcod:renderer-aa", (event) => {
            globalThis.__kisakcodAaLifecycle.push(structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:test-webgl-aa", (event) => {
            globalThis.__kisakcodAaGlOperations.push(structuredClone(event.detail));
        });
    });
}

test("matches COD4 scene anti-aliasing through 4x", { tag: "@smoke" }, async ({ page }) => {
    await observeAntiAliasing(page);
    await page.goto("/");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("running");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.rendererAa?.state,
    )).toBe("disabled");

    const disabled = await page.evaluate(() => ({
        lifecycle: structuredClone(globalThis.__KISAKCOD_WEB__.rendererAa),
        operations: structuredClone(globalThis.__kisakcodAaGlOperations),
    }));
    expect(disabled.lifecycle).toMatchObject({
        state: "disabled",
        configuredSamples: 1,
        requestedSamples: 1,
        activeSamples: 1,
        resident: false,
        backend: "webgl2-multisample-renderbuffer",
        resolveBoundary: "scene-before-postfx-and-ui",
    });
    expect(disabled.operations.filter(
        (operation) => operation.operation === "renderbuffer-storage-multisample",
    )).toEqual([]);
    expect(disabled.operations).toContainEqual({
        operation: "create-context",
        antialias: false,
    });

    const configured4x = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestSetAaSamples", 4));
    expect(configured4x).toBe(4);
    await expect.poll(() => page.evaluate(() => ({
        state: globalThis.__KISAKCOD_WEB__?.rendererAa?.state,
        configured: globalThis.__KISAKCOD_WEB__?.rendererAa?.configuredSamples,
        requested: globalThis.__KISAKCOD_WEB__?.rendererAa?.requestedSamples,
        active: globalThis.__KISAKCOD_WEB__?.rendererAa?.activeSamples,
    }))).toEqual({ state: "ready", configured: 4, requested: 4, active: 4 });
    await expect.poll(() => page.evaluate(() =>
        globalThis.__kisakcodAaGlOperations.filter(
            (operation) => operation.operation === "blit-framebuffer",
        ).length,
    )).toBeGreaterThan(0);

    const active4x = await page.evaluate(() => ({
        lifecycle: structuredClone(globalThis.__KISAKCOD_WEB__.rendererAa),
        operations: structuredClone(globalThis.__kisakcodAaGlOperations),
    }));
    expect(active4x.lifecycle).toMatchObject({
        state: "ready",
        configuredSamples: 4,
        requestedSamples: 4,
        activeSamples: 4,
        resident: true,
    });
    const storage4x = active4x.operations.filter(
        (operation) => operation.operation === "renderbuffer-storage-multisample" &&
            operation.samples === 4,
    );
    expect(storage4x).toHaveLength(2);
    expect(storage4x.map((operation) => operation.internalFormat).sort((a, b) => a - b))
        .toEqual([0x8058, 0x81A6]); // RGBA8 and DEPTH_COMPONENT24
    expect(storage4x.every((operation) =>
        operation.width === active4x.lifecycle.width &&
        operation.height === active4x.lifecycle.height,
    )).toBe(true);
    expect(active4x.operations.some((operation) =>
        operation.operation === "blit-framebuffer" &&
        operation.mask === 0x4000 && // COLOR_BUFFER_BIT; bootstrap has no DOF target
        operation.filter === 0x2600 && // NEAREST is required for multisample resolve
        operation.source[2] === active4x.lifecycle.width &&
        operation.source[3] === active4x.lifecycle.height,
    )).toBe(true);

    const configuredAboveWebMaximum = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestSetAaSamples", 8));
    expect(configuredAboveWebMaximum).toBe(8);
    await expect.poll(() => page.evaluate(() => ({
        configured: globalThis.__KISAKCOD_WEB__?.rendererAa?.configuredSamples,
        requested: globalThis.__KISAKCOD_WEB__?.rendererAa?.requestedSamples,
        active: globalThis.__KISAKCOD_WEB__?.rendererAa?.activeSamples,
    }))).toEqual({ configured: 8, requested: 4, active: 4 });

    await page.evaluate(() => {
        const canvas = document.querySelector("#game-canvas");
        canvas.style.width = "777px";
        canvas.style.height = "433px";
    });
    await expect.poll(() => page.evaluate(() => ({
        width: globalThis.__KISAKCOD_WEB__?.rendererAa?.width,
        height: globalThis.__KISAKCOD_WEB__?.rendererAa?.height,
    }))).toEqual({ width: 777, height: 433 });
    const resizedGeneration = await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.rendererAa.resourceGeneration,
    );

    const lost = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestLoseWebGLContext"));
    expect(Boolean(lost)).toBe(true);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.rendererAa?.state,
    )).toBe("lost");
    await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestRestoreWebGLContext"));
    await expect.poll(() => page.evaluate(() => ({
        state: globalThis.__KISAKCOD_WEB__?.rendererAa?.state,
        active: globalThis.__KISAKCOD_WEB__?.rendererAa?.activeSamples,
        generation: globalThis.__KISAKCOD_WEB__?.rendererAa?.resourceGeneration,
    }))).toEqual({ state: "ready", active: 4, generation: resizedGeneration + 1 });

    const blitsBeforeDisable = await page.evaluate(() =>
        globalThis.__kisakcodAaGlOperations.filter(
            (operation) => operation.operation === "blit-framebuffer").length);
    await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestSetAaSamples", 1));
    await expect.poll(() => page.evaluate(() => ({
        state: globalThis.__KISAKCOD_WEB__?.rendererAa?.state,
        active: globalThis.__KISAKCOD_WEB__?.rendererAa?.activeSamples,
        resident: globalThis.__KISAKCOD_WEB__?.rendererAa?.resident,
    }))).toEqual({ state: "disabled", active: 1, resident: false });
    await page.evaluate(() => new Promise((resolve) =>
        requestAnimationFrame(() => requestAnimationFrame(resolve))));
    expect(await page.evaluate(() =>
        globalThis.__kisakcodAaGlOperations.filter(
            (operation) => operation.operation === "blit-framebuffer").length))
        .toBe(blitsBeforeDisable);
});

test("falls down to the highest WebGL2-supported sample count", async ({ page }) => {
    await observeAntiAliasing(page);
    await page.addInitScript(() => {
        globalThis.__KISAKCOD_WORKER_TEST_CONFIG__.maxAaSamples = 2;
    });
    await page.goto("/");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("running");

    await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestSetAaSamples", 4));
    await expect.poll(() => page.evaluate(() => ({
        configured: globalThis.__KISAKCOD_WEB__?.rendererAa?.configuredSamples,
        requested: globalThis.__KISAKCOD_WEB__?.rendererAa?.requestedSamples,
        active: globalThis.__KISAKCOD_WEB__?.rendererAa?.activeSamples,
        max: globalThis.__KISAKCOD_WEB__?.rendererAa?.maxSamples,
    }))).toEqual({ configured: 4, requested: 4, active: 2, max: 2 });
    const samples = await page.evaluate(() =>
        globalThis.__kisakcodAaGlOperations
            .filter((operation) =>
                operation.operation === "renderbuffer-storage-multisample")
            .map((operation) => operation.samples));
    expect(samples).toEqual([2, 2]);
});
