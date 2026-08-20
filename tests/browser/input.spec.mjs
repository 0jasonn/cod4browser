import { expect, test } from "@playwright/test";

test("browser reload and wheel input pulses cross the host boundary", async ({ page }) => {
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
    const canvas = await page.locator("#game-canvas").boundingBox();
    await page.mouse.move(canvas.x + canvas.width / 2, canvas.y + canvas.height / 2);
    await page.mouse.wheel(0, -100);
    await page.mouse.wheel(0, 100);
    await page.waitForTimeout(20);

    const events = await page.evaluate(() => globalThis.__browserInputEvents);
    expect(events).toEqual([
        { type: "key", key: 0x72, down: true },
        { type: "key", key: 0x72, down: false },
        { type: "key", key: 0xCE, down: true },
        { type: "key", key: 0xCE, down: false },
        { type: "key", key: 0xCD, down: true },
        { type: "key", key: 0xCD, down: false },
    ]);
});
