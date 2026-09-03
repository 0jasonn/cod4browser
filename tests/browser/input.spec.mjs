import { expect, test } from "@playwright/test";

test("browser reload, melee, and wheel input pulses cross the host boundary", async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ = { observeInput: true };
    });
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state))
        .toBe("running");
    await page.evaluate(() => {
        globalThis.__browserInputEvents = [];
        globalThis.addEventListener("kisakcod:input", (event) => {
            globalThis.__browserInputEvents.push(event.detail);
        });
        document.querySelector("#game-canvas").focus();
    });

    await page.keyboard.press("r");
    await page.keyboard.press("v");
    const canvas = await page.locator("#game-canvas").boundingBox();
    await page.mouse.move(canvas.x + canvas.width / 2, canvas.y + canvas.height / 2);
    await page.mouse.wheel(0, -100);
    await page.mouse.wheel(0, 100);
    await expect.poll(() => page.evaluate(() => globalThis.__browserInputEvents))
        .toEqual([
        { type: "key", key: 0x72, down: true },
        { type: "key", key: 0x72, down: false },
        { type: "key", key: 0x76, down: true },
        { type: "key", key: 0x76, down: false },
        { type: "key", key: 0xCE, down: true },
        { type: "key", key: 0xCE, down: false },
        { type: "key", key: 0xCD, down: true },
        { type: "key", key: 0xCD, down: false },
        ]);
});

test("Escape leaves pointer lock and reaches the engine as one key press", async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ = { observeInput: true };
    });
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state))
        .toBe("running");
    await page.bringToFront();
    await expect.poll(() => page.evaluate(() => document.hasFocus())).toBe(true);
    const canvas = page.locator("#game-canvas");
    await canvas.click({ position: { x: 12, y: 12 } });
    await expect.poll(() => page.evaluate(() => document.pointerLockElement?.id))
        .toBe("game-canvas");
    // The DOM property can change before the queued pointerlockchange event.
    // Wait for the real input owner to observe acquisition before releasing it.
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.input.pointerLocked))
        .toBe(true);
    await page.evaluate(() => {
        globalThis.__browserInputEvents = [];
        globalThis.addEventListener("kisakcod:input", (event) => {
            globalThis.__browserInputEvents.push(event.detail);
        });
    });

    // Automation-generated Escape does not invoke Chromium's trusted
    // pointer-lock exit gesture. A spontaneous lock loss exercises the same
    // pointerlockchange path used by a real browser Escape gesture.
    await page.evaluate(() => document.exitPointerLock());
    await expect.poll(() => page.evaluate(() => document.pointerLockElement))
        .toBeNull();
    await expect.poll(() => page.evaluate(() =>
        globalThis.__browserInputEvents.filter((event) => event.key === 0x1B)))
        .toEqual([
            { type: "key", key: 0x1B, down: true },
            { type: "key", key: 0x1B, down: false },
        ]);
});

test("absolute menu mode releases pointer lock and forwards canvas coordinates", async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ = { observeInput: true };
    });
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state))
        .toBe("running");
    const canvas = page.locator("#game-canvas");
    await canvas.click({ position: { x: 12, y: 12 } });
    await expect.poll(() => page.evaluate(() => document.pointerLockElement?.id))
        .toBe("game-canvas");

    await page.evaluate(() => {
        globalThis.__browserInputEvents = [];
        globalThis.addEventListener("kisakcod:input", (event) => {
            globalThis.__browserInputEvents.push(event.detail);
        });
        globalThis.dispatchEvent(new CustomEvent("kisakcod:mouse-mode", {
            detail: { absolute: true },
        }));
    });
    await expect.poll(() => page.evaluate(() => document.pointerLockElement))
        .toBeNull();

    const bounds = await canvas.boundingBox();
    const expectedPosition = await canvas.evaluate((element) => ({
        x: Math.round(element.width * 0.25),
        y: Math.round(element.height * 0.75),
    }));
    await page.mouse.move(bounds.x + bounds.width * 0.25, bounds.y + bounds.height * 0.75);
    await expect.poll(() => page.evaluate(() =>
        globalThis.__browserInputEvents.some((event) => event.type === "mouse-move")))
        .toBe(true);
    const mouseMove = await page.evaluate(() =>
        globalThis.__browserInputEvents.find((event) => event.type === "mouse-move"));
    expect(mouseMove).toMatchObject({ type: "mouse-move", dx: 0, dy: 0 });
    expect(Math.abs(mouseMove.x - expectedPosition.x)).toBeLessThanOrEqual(1);
    expect(Math.abs(mouseMove.y - expectedPosition.y)).toBeLessThanOrEqual(1);

    await page.evaluate(() => {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:cursor", {
            detail: { visible: false },
        }));
    });
    await page.mouse.down();
    await expect.poll(() => page.evaluate(() => document.pointerLockElement))
        .toBeNull();
});
