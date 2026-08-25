import { expect, test } from "@playwright/test";

const retailRoot = process.env.KISAK_COD4_RETAIL_ROOT;

async function waitForAssets(page, state)
{
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.assets?.state,
    ), { timeout: 300_000 }).toBe(state);
}

async function submitCommand(page, command)
{
    await page.locator("#engine-command-input").fill(command);
    await page.locator("#engine-command-submit").click();
    await expect(page.locator("#engine-command-status"))
        .toHaveText(`Accepted: ${command}`, { timeout: 120_000 });
}

async function waitForWorldFrames(page, mapName, minimumGeneration = 1)
{
    await expect.poll(() => page.evaluate(({ name, generation }) => {
        const frame = globalThis.__retailValidationFrames?.findLast(
            (entry) => entry.state === "drawn" && entry.geometrySubmitted === true &&
                entry.worldName?.toLowerCase().includes(name));
        return frame?.viewSubmissionGeneration >= generation;
    }, { name: mapName, generation: minimumGeneration }), {
        timeout: 300_000,
        message: `${mapName} should publish sustained canonical world frames`,
    }).toBe(true);
}

test("local retail validation matrix", { tag: "@retail" }, async ({ page }) => {
    test.skip(!retailRoot,
        "Set KISAK_COD4_RETAIL_ROOT to a legally owned COD4 installation");
    test.setTimeout(900_000);

    await page.addInitScript(() => {
        Object.defineProperty(globalThis, "showDirectoryPicker", {
            configurable: true,
            value: undefined,
        });
        globalThis.__retailValidationFrames = [];
        globalThis.__retailValidationViews = [];
        globalThis.__retailValidationMemory = [];
        globalThis.addEventListener("kisakcod:renderer-scene-frame", (event) => {
            globalThis.__retailValidationFrames.push(structuredClone(event.detail));
            if (globalThis.__retailValidationFrames.length > 512)
                globalThis.__retailValidationFrames.shift();
        });
        globalThis.addEventListener("kisakcod:renderer-scene-view", (event) => {
            globalThis.__retailValidationViews.push(structuredClone(event.detail));
            if (globalThis.__retailValidationViews.length > 512)
                globalThis.__retailValidationViews.shift();
        });
        globalThis.addEventListener("kisakcod:renderer-memory", (event) => {
            globalThis.__retailValidationMemory.push(structuredClone(event.detail));
        });
    });

    await page.goto("/");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state,
    )).toBe("running");
    await waitForAssets(page, "empty");

    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(retailRoot);
    await waitForAssets(page, "ready");

    await submitCommand(page, "map killhouse");
    await waitForWorldFrames(page, "killhouse", 120);

    const beforeMove = await page.evaluate(() => structuredClone(
        globalThis.__retailValidationViews.at(-1)));
    const canvas = page.locator("#game-canvas");
    await canvas.focus();
    await page.keyboard.down("w");
    await page.waitForTimeout(600);
    await page.keyboard.up("w");
    await expect.poll(() => page.evaluate((origin) => {
        const current = globalThis.__retailValidationViews.at(-1)?.viewOrigin;
        return current ? Math.hypot(
            current[0] - origin[0], current[1] - origin[1], current[2] - origin[2]) : 0;
    }, beforeMove.viewOrigin)).toBeGreaterThan(1);

    const box = await canvas.boundingBox();
    await canvas.click({ position: { x: box.width / 2, y: box.height / 2 } });
    await page.mouse.down();
    await page.waitForTimeout(100);
    await page.mouse.up();
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.audioPlayback.length,
    ), { timeout: 30_000 }).toBeGreaterThan(0);

    await submitCommand(page, "writeconfig cleanup-validation.cfg");
    await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.module.dispose());

    await page.reload();
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state,
    )).toBe("running");
    await waitForAssets(page, "ready");
    await submitCommand(page, "exec cleanup-validation.cfg");
    expect(await page.locator("#boot-log").textContent())
        .not.toContain("couldn't exec cleanup-validation.cfg");

    await submitCommand(page, "map cargoship");
    await waitForWorldFrames(page, "cargoship", 120);

    const recoveryBefore = await page.evaluate(() => ({
        count: globalThis.__KISAKCOD_WEB__.rendererShader.recoveryCount ?? 0,
        frame: globalThis.__retailValidationFrames.findLast(
            (entry) => entry.worldName?.toLowerCase().includes("cargoship"))
            ?.viewSubmissionGeneration ?? 0,
    }));
    expect(await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestLoseWebGLContext")))
        .toBeTruthy();
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.rendererShader.state,
    )).toBe("lost");
    await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestRestoreWebGLContext"));
    await expect.poll(() => page.evaluate((before) => ({
        state: globalThis.__KISAKCOD_WEB__.rendererShader.state,
        recovered: globalThis.__KISAKCOD_WEB__.rendererShader.recoveryCount > before,
    }), recoveryBefore.count), { timeout: 30_000 })
        .toEqual({ state: "ready", recovered: true });
    await waitForWorldFrames(page, "cargoship", recoveryBefore.frame + 1);

    expect(await page.evaluate(
        () => globalThis.__retailValidationMemory.length,
    )).toBeGreaterThan(0);
});
