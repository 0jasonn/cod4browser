import { expect, test } from "@playwright/test";

test.skip(process.env.KISAK_WEB_PRODUCT_TEST !== "1", "Runs only against the production site.");

async function installInputCapture(page)
{
    await page.addInitScript(() => {
        const NativeWorker = globalThis.Worker;
        globalThis.__scaledInput = [];
        globalThis.Worker = class extends NativeWorker {
            postMessage(message, transfer)
            {
                if (message?.type === "input-event") {
                    globalThis.__scaledInput.push(structuredClone(message.event));
                }
                return super.postMessage(message, transfer);
            }
        };
    });
    await page.goto("/");
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "running");
}

for (const deviceScaleFactor of [1, 1.25, 1.5, 2]) {
    test(`absolute input tracks backing pixels at DPR ${deviceScaleFactor} @product`,
        async ({ browser }) => {
            const context = await browser.newContext({
                viewport: { width: 1280, height: 900 },
                deviceScaleFactor,
            });
            const page = await context.newPage();
            await installInputCapture(page);
            const canvas = page.locator("#game-canvas");
            await page.evaluate(() => {
                globalThis.dispatchEvent(new CustomEvent("kisakcod:mouse-mode", {
                    detail: { absolute: true },
                }));
                const element = document.querySelector("#game-canvas");
                element.style.width = "640px";
                element.style.height = "360px";
            });
            await expect.poll(() => canvas.evaluate((element) => ({
                width: element.width,
                height: element.height,
            }))).toEqual({
                width: Math.round(640 * deviceScaleFactor),
                height: Math.round(360 * deviceScaleFactor),
            });

            for (const layout of [
                { width: 640, height: 360, scale: 1, x: 0.25, y: 0.75 },
                { width: 480, height: 270, scale: 1, x: 0.6, y: 0.2 },
                { width: 480, height: 270, scale: 0.8, x: 0.4, y: 0.65 },
            ]) {
                const result = await page.evaluate(async (settings) => {
                    const element = document.querySelector("#game-canvas");
                    element.style.width = `${settings.width}px`;
                    element.style.height = `${settings.height}px`;
                    element.style.transformOrigin = "top left";
                    element.style.transform = settings.scale === 1
                        ? "none"
                        : `scale(${settings.scale})`;
                    await new Promise((resolve) => requestAnimationFrame(() =>
                        requestAnimationFrame(resolve)));
                    globalThis.__scaledInput = [];
                    const bounds = element.getBoundingClientRect();
                    element.dispatchEvent(new MouseEvent("mousemove", {
                        clientX: bounds.left + bounds.width * settings.x,
                        clientY: bounds.top + bounds.height * settings.y,
                        bubbles: true,
                    }));
                    return {
                        backingWidth: element.width,
                        backingHeight: element.height,
                        expectedX: Math.round(element.width * settings.x),
                        expectedY: Math.round(element.height * settings.y),
                        dpr: globalThis.devicePixelRatio,
                    };
                }, layout);
                await expect.poll(() => page.evaluate(() => globalThis.__scaledInput.length))
                    .toBeGreaterThan(0);
                const movement = await page.evaluate(() => globalThis.__scaledInput.at(-1));
                expect(movement).toMatchObject({
                    type: "mouse-move", dx: 0, dy: 0,
                });
                // MouseEvent client coordinates are integral CSS pixels, so a
                // transformed element can legitimately round one backing
                // pixel either side of the exact fraction.
                expect(Math.abs(movement.x - result.expectedX)).toBeLessThanOrEqual(1);
                expect(Math.abs(movement.y - result.expectedY)).toBeLessThanOrEqual(1);
                expect(result.dpr).toBe(deviceScaleFactor);
                expect(result.backingWidth).toBeGreaterThan(0);
                expect(result.backingHeight).toBeGreaterThan(0);
            }
            await context.close();
        });
}

test("relative input remains locked and coalesced across a viewport resize @product",
    async ({ browser }) => {
        const context = await browser.newContext({
            viewport: { width: 1280, height: 900 },
            deviceScaleFactor: 1.5,
        });
        const page = await context.newPage();
        await installInputCapture(page);
        const canvas = page.locator("#game-canvas");
        await canvas.click({ position: { x: 10, y: 10 } });
        await expect.poll(() => page.evaluate(() => document.pointerLockElement?.id))
            .toBe("game-canvas");
        await page.setViewportSize({ width: 1100, height: 760 });
        await expect.poll(() => page.evaluate(() => document.pointerLockElement?.id))
            .toBe("game-canvas");
        await page.evaluate(() => {
            globalThis.__scaledInput = [];
            for (const [movementX, movementY] of [[3, -2], [5, 1]]) {
                const event = new MouseEvent("mousemove");
                Object.defineProperties(event, {
                    movementX: { value: movementX },
                    movementY: { value: movementY },
                });
                globalThis.dispatchEvent(event);
            }
        });
        await expect.poll(() => page.evaluate(() => globalThis.__scaledInput))
            .toContainEqual(expect.objectContaining({
                type: "mouse-move", dx: 8, dy: -1,
            }));
        await context.close();
    });
