import { expect, test } from "@playwright/test";

async function boot(page)
{
    await page.goto("/");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
}

async function submitTestSurface(page)
{
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestSubmitSurface"))).toBe(1);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.rendererSurface?.state)).toBe("ready");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.rendererSurface?.drawCount ?? 0))
        .toBeGreaterThan(0);
}

test("boots the canonical runtime and renders through WebGL2", { tag: "@smoke" }, async ({ page }) => {
    const pageErrors = [];
    const consoleErrors = [];
    let wasmResponse = null;
    page.on("pageerror", (error) => pageErrors.push(error.message));
    page.on("console", (message) => {
        if (message.type() === "error") consoleErrors.push(message.text());
    });
    page.on("response", (response) => {
        if (response.url().endsWith("/kisakcod.wasm")) wasmResponse = response;
    });

    await boot(page);
    await submitTestSurface(page);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.lastFrame?.frame ?? 0))
        .toBeGreaterThan(2);

    const surface = await page.evaluate(() => structuredClone(
        globalThis.__KISAKCOD_WEB__.rendererSurface));
    expect(surface).toMatchObject({
        state: "ready",
        vertexCount: 3,
        indexCount: 3,
        drawIndexCount: 3,
        topology: "triangle-list",
        resident: true,
    });
    expect(wasmResponse).not.toBeNull();
    expect(wasmResponse.ok()).toBe(true);
    expect(wasmResponse.headers()["content-type"]).toContain("application/wasm");
    expect(pageErrors).toEqual([]);
    expect(consoleErrors).toEqual([]);
});

test("recovers the retained surface after WebGL2 context loss", { tag: "@smoke" }, async ({ page }) => {
    await boot(page);
    await submitTestSurface(page);
    const before = await page.evaluate(() => structuredClone(
        globalThis.__KISAKCOD_WEB__.rendererSurface));

    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestLoseWebGLContext"))).toBe(1);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state)).toBe("renderer-lost");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.rendererSurface?.state)).toBe("lost");

    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestRestoreWebGLContext"))).toBe(1);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.rendererSurface?.state)).toBe("ready");

    const after = await page.evaluate(() => structuredClone(
        globalThis.__KISAKCOD_WEB__.rendererSurface));
    expect(after.submissionGeneration).toBe(before.submissionGeneration);
    expect(after.resourceGeneration).toBeGreaterThan(before.resourceGeneration);
    expect(after.recoveryCount).toBe(before.recoveryCount + 1);
    expect(after.resident).toBe(true);
});

test("surface restoration failure remains terminal and non-resident", async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__rendererStates = [];
        globalThis.__rendererFrames = [];
        globalThis.addEventListener("kisakcod:state", (event) => {
            globalThis.__rendererStates.push(structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:frame", (event) => {
            globalThis.__rendererFrames.push(structuredClone(event.detail));
        });
    });
    await boot(page);
    await submitTestSurface(page);
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.testControl({
        failSurfaceRestore: true,
    }));

    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestLoseWebGLContext"))).toBe(1);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state)).toBe("renderer-lost");
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestRestoreWebGLContext"))).toBe(1);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state)).toBe("failed");

    const evidence = await page.evaluate(async () => {
        const failedIndex = globalThis.__rendererStates.findLastIndex(
            ({ state }) => state === "failed");
        const framesAtFailure = globalThis.__rendererFrames.length;
        await new Promise((resolve) => requestAnimationFrame(() =>
            requestAnimationFrame(() => requestAnimationFrame(resolve))));
        return {
            surface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
            statesAfterFailure: globalThis.__rendererStates.slice(failedIndex + 1),
            framesAtFailure,
            framesAfterWait: globalThis.__rendererFrames.length,
            state: globalThis.__KISAKCOD_WEB__.state,
        };
    });
    expect(evidence.state).toBe("failed");
    expect(evidence.surface).toMatchObject({ resident: false });
    expect(evidence.statesAfterFailure.some(({ state }) => state === "running")).toBe(false);
    expect(evidence.framesAfterWait).toBe(evidence.framesAtFailure);
});

test("world unload releases renderer recovery data without replacing the context", async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__rendererLifecycle = [];
        globalThis.__rendererMemory = [];
        globalThis.addEventListener("kisakcod:renderer-lifecycle", (event) => {
            globalThis.__rendererLifecycle.push(structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:renderer-memory", (event) => {
            globalThis.__rendererMemory.push(structuredClone(event.detail));
        });
    });
    await boot(page);
    await submitTestSurface(page);
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestUnloadWorldResources"))).toBe(1);

    await expect.poll(() => page.evaluate(() =>
        globalThis.__rendererLifecycle.findLast(
            (event) => event.state === "worldUnloadEnd"))).toBeTruthy();
    const lifecycle = await page.evaluate(() => structuredClone(
        globalThis.__rendererLifecycle.filter((event) =>
            event.state === "worldUnloadBegin" || event.state === "worldUnloadEnd")));
    expect(lifecycle).toHaveLength(2);
    expect(lifecycle[1].contextGenerationBefore)
        .toBe(lifecycle[1].contextGenerationAfter);
    expect(lifecycle[1].recoveryBytes).toBeLessThan(lifecycle[0].recoveryBytes);
    const memory = await page.evaluate(() => structuredClone(
        globalThis.__rendererMemory.filter((event) =>
            event.state === "world-unload-begin" || event.state === "world-unloaded")));
    expect(memory).toHaveLength(2);
    expect(memory[0].wasmAllocatorStatsSampled).toBe(true);
    expect(memory[1].wasmAllocatorStatsSampled).toBe(true);
    expect(memory[1].wasmLinearMemoryCapacityBytes)
        .toBe(memory[0].wasmLinearMemoryCapacityBytes);
    expect(memory[1].wasmAllocatorInUseBytes)
        .toBeLessThanOrEqual(memory[0].wasmAllocatorInUseBytes);
    for (const field of [
        "imageLoadDefCacheEntryCount",
        "imageLoadDefCacheEncodedPayloadBytes",
        "imageLoadDefCacheBudgetBytes",
        "imageLoadDefCacheEvictionCount",
    ]) {
        expect(memory[1][field]).toBe(memory[0][field]);
    }
});

test("reports disjoint renderer recovery memory categories", async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__rendererMemory = [];
        globalThis.addEventListener("kisakcod:renderer-memory", (event) => {
            globalThis.__rendererMemory.push(structuredClone(event.detail));
        });
    });
    await boot(page);
    await submitTestSurface(page);
    const before = await page.evaluate(() => globalThis.__rendererMemory.length);
    const recoveryCopyBytes = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call(
            "_KisakWeb_TestEmitRendererMemory"));
    await expect.poll(() => page.evaluate((start) =>
        globalThis.__rendererMemory.slice(start).some(
            (entry) => entry.state === "diagnostic-snapshot"), before)).toBe(true);
    const sample = await page.evaluate((start) => structuredClone(
        globalThis.__rendererMemory.slice(start).findLast(
            (entry) => entry.state === "diagnostic-snapshot")), before);
    expect(sample.state).toBe("diagnostic-snapshot");
    expect(sample.recoveryCopyBytes).toBe(recoveryCopyBytes);
    expect(sample.worldImageRecoveryBytes +
        sample.staticModelImageRecoveryBytes +
        sample.dynamicModelImageRecoveryBytes +
        sample.uiImageRecoveryBytes +
        sample.supplementalTextureRecoveryBytes)
        .toBe(sample.decodedTextureSourceBytes);
    expect(sample.decodedTextureSourceBytes + sample.geometryBytes +
        sample.shaderProgramCacheEstimateBytes)
        .toBe(sample.recoveryCopyBytes);
    expect(sample.gpuTextureEstimateBytes)
        .toBeGreaterThanOrEqual(sample.decodedTextureSourceBytes);
    const heapCapacity = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestHeapBytes"));
    expect(sample.wasmLinearMemoryCapacityBytes).toBe(heapCapacity);
    expect(sample.wasmProgramBreakOffsetBytes).toBeGreaterThan(0);
    expect(sample.wasmProgramBreakOffsetBytes)
        .toBeLessThanOrEqual(sample.wasmLinearMemoryCapacityBytes);
    expect(sample.wasmLinearMemoryCapacityBytes)
        .toBeLessThanOrEqual(sample.wasmLinearMemoryMaximumBytes);
    expect(sample.wasmAllocatorStatsSampled).toBe(true);
    expect(sample.wasmAllocatorInUseBytes + sample.wasmAllocatorFreeBytes)
        .toBe(sample.wasmAllocatorFootprintBytes);
    expect(sample.wasmAllocatorTopFreeBytes)
        .toBeLessThanOrEqual(sample.wasmAllocatorFreeBytes);
    for (const field of [
        "imageLoadDefCacheEntryCount",
        "imageLoadDefCacheEncodedPayloadBytes",
        "imageLoadDefCacheBudgetBytes",
        "imageLoadDefCacheEvictionCount",
    ]) {
        expect(Number.isSafeInteger(sample[field])).toBe(true);
        expect(sample[field]).toBeGreaterThanOrEqual(0);
    }
    expect(sample.imageLoadDefCacheBudgetBytes).toBeGreaterThan(0);
    expect(sample.imageLoadDefCacheEncodedPayloadBytes)
        .toBeLessThanOrEqual(sample.imageLoadDefCacheBudgetBytes);
});

test("keeps an initial WebGL pipeline failure terminal", async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ = { failInitialShader: true };
        globalThis.__rendererStates = [];
        globalThis.__rendererFrames = [];
        globalThis.addEventListener("kisakcod:state", (event) => {
            globalThis.__rendererStates.push(structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:frame", (event) => {
            globalThis.__rendererFrames.push(structuredClone(event.detail));
        });
    });
    await page.goto("/");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state)).toBe("failed");
    await expect(page.locator("#boot-log")).toContainText("Shader compilation failed");
    await page.evaluate(async () => {
        await globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestLoseWebGLContext");
        await globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestRestoreWebGLContext");
        await new Promise((resolve) => requestAnimationFrame(() =>
            requestAnimationFrame(() => requestAnimationFrame(resolve))));
    });
    const evidence = await page.evaluate(() => ({
        state: globalThis.__KISAKCOD_WEB__.state,
        states: structuredClone(globalThis.__rendererStates),
        frames: globalThis.__rendererFrames.length,
    }));
    expect(evidence.state).toBe("failed");
    expect(evidence.frames).toBe(0);
    expect(evidence.states.some(({ state }) => state === "running")).toBe(false);
    expect(evidence.states.some(({ state }) => state === "renderer-lost")).toBe(false);
    expect(evidence.states.at(-1)?.state).toBe("failed");
});

test("shows a useful failure when the generated module is missing", { tag: "@smoke" }, async ({ page }) => {
    await page.route("**/kisakcod.mjs", (route) => route.abort("failed"));
    await page.goto("/");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state)).toBe("failed");
    await expect(page.locator("#runtime-message")).toHaveText(
        "The WebAssembly module could not start");
    await expect(page.locator("#boot-log")).toContainText("kisakcod.mjs");
});
