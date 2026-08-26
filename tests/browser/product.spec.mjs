import { expect, test } from "@playwright/test";

test.skip(process.env.KISAK_WEB_PRODUCT_TEST !== "1", "Runs only against the production site.");

test("production artifact boots without diagnostic browser APIs @product", async ({ page }) => {
    const pageErrors = [];
    const consoleErrors = [];
    await page.addInitScript(() => {
        globalThis.__productStates = [];
        globalThis.addEventListener("kisakcod:state", (event) => {
            globalThis.__productStates.push(structuredClone(event.detail));
        });
    });
    page.on("pageerror", (error) => pageErrors.push(error.message));
    page.on("console", (message) => {
        if (message.type() === "error") consoleErrors.push(message.text());
    });

    await page.goto("/");
    await expect.poll(() => page.locator("html").getAttribute("data-runtime-state"))
        .toBe("running");
    await expect.poll(async () => {
        const text = await page.locator("#frame-counter").textContent();
        return Number(text?.match(/\d+/u)?.[0] ?? 0);
    }).toBeGreaterThan(1);
    await expect(page.locator("#asset-state-label")).toHaveText("Installation required");
    expect(await page.evaluate(() => globalThis.__productStates.some(
        (state) => state.state === "running"))).toBe(true);
    expect(await page.evaluate(() => "__KISAKCOD_WEB__" in globalThis)).toBe(false);
    expect(pageErrors).toEqual([]);
    expect(consoleErrors).toEqual([]);
});

test("production JavaScript exposes only named product operations @product", async ({ request }) => {
    const files = [
        "asset_store.mjs",
        "browser_capabilities.mjs",
        "capability_probe_worker.mjs",
        "product_protocol.mjs",
        "engine_worker.mjs",
        "product_engine_worker_host.mjs",
        "product_mount_controller.mjs",
        "launcher.mjs",
        "product_checkpoint_controller.mjs",
        "input_controller_core.mjs",
        "worker_sync_filesystem.mjs",
    ];
    const source = (await Promise.all(files.map(async (file) => {
        const response = await request.get(`/${file}`);
        expect(response.ok()).toBe(true);
        return response.text();
    }))).join("\n");

    for (const forbidden of [
        "callProbe", "testControl", "test-control", "_KisakWeb_Test",
        "filesystem_bridge", "renderer-comparison", "retail-census",
    ]) {
        expect(source).not.toContain(forbidden);
    }
    for (const operation of [
        "mountAssets", "flushAndUnmount", "probeAsset",
        "submitCanonicalCommand", "runtimeStatus", "shutdown",
    ]) {
        expect(source).toContain(operation);
    }
});

test("missing required capability blocks engine and asset startup @product", async ({ page }) => {
    await page.addInitScript(() => {
        const getContext = HTMLCanvasElement.prototype.getContext;
        HTMLCanvasElement.prototype.getContext = function(type, options) {
            return type === "webgl2" ? null : getContext.call(this, type, options);
        };
        const NativeWorker = globalThis.Worker;
        globalThis.__capabilityWorkerConstructions = 0;
        globalThis.Worker = class extends NativeWorker {
            constructor(...args)
            {
                ++globalThis.__capabilityWorkerConstructions;
                super(...args);
            }
        };
        const storage = navigator.storage;
        const getDirectory = storage.getDirectory.bind(storage);
        globalThis.__capabilityStorageAccesses = 0;
        storage.getDirectory = (...args) => {
            ++globalThis.__capabilityStorageAccesses;
            return getDirectory(...args);
        };
    });
    await page.goto("/");
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "unsupported");
    await expect(page.locator("#asset-state-label")).toHaveText("Browser storage unsupported");
    await expect(page.locator("#asset-message")).toContainText("WebGL 2");
    await expect(page.locator("#select-install-button")).toBeDisabled();
    expect(await page.evaluate(() => globalThis.__capabilityWorkerConstructions)).toBe(0);
    expect(await page.evaluate(() => globalThis.__capabilityStorageAccesses)).toBe(0);
});

test("production storage status is honest and persistence can be retried @product", async ({ page }) => {
    await page.addInitScript(() => {
        navigator.storage.persisted = async () => false;
        navigator.storage.persist = async () => true;
        navigator.storage.estimate = async () => ({
            usage: 256 * 1024 * 1024,
            quota: 1024 * 1024 * 1024,
        });
    });
    await page.goto("/");
    await expect(page.locator("#storage-retention"))
        .toContainText("Persistent storage not granted");
    await expect(page.locator("#storage-capacity"))
        .toHaveText("256.0 MiB used of 1.0 GiB (25%).");
    await expect(page.locator("#storage-warning")).toBeVisible();
    await page.locator("#retry-persistence-button").click();
    await expect(page.locator("#storage-retention"))
        .toContainText("Persistent storage granted");
    await expect(page.locator("#retry-persistence-button")).toBeHidden();
});

test("production input transport failure marks the runtime unavailable @product", async ({ page }) => {
    await page.addInitScript(() => {
        const NativeWorker = globalThis.Worker;
        globalThis.__inputAttempts = 0;
        globalThis.Worker = class extends NativeWorker {
            postMessage(message, transfer)
            {
                if (message?.type === "input-event") {
                    ++globalThis.__inputAttempts;
                    throw new Error("input transport unavailable");
                }
                return super.postMessage(message, transfer);
            }
        };
    });
    await page.goto("/");
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "running");
    const canvas = page.locator("#game-canvas");
    await canvas.focus();
    await page.keyboard.press("KeyW");
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "failed");
    await expect(page.locator("#boot-log")).toContainText("Input transport failed");
    await page.keyboard.press("Space");
    expect(await page.evaluate(() => globalThis.__inputAttempts)).toBe(1);
});

test("production input covers canonical keyboard, mouse, focus, and pointer-lock lifecycles @product", async ({ page }) => {
    await page.addInitScript(() => {
        const NativeWorker = globalThis.Worker;
        globalThis.__productInputMessages = [];
        globalThis.Worker = class extends NativeWorker {
            postMessage(message, transfer)
            {
                if (message?.type === "input-event") {
                    globalThis.__productInputMessages.push(structuredClone(message.event));
                }
                return super.postMessage(message, transfer);
            }
        };
    });
    await page.goto("/");
    await expect.poll(() => page.locator("html").getAttribute("data-runtime-state"))
        .toBe("running");
    const canvas = page.locator("#game-canvas");
    await canvas.focus();

    for (const key of ["KeyW", "Space", "Semicolon", "F12", "Numpad5", "Escape"]) {
        await page.keyboard.down(key);
        await page.keyboard.up(key);
    }
    await expect.poll(() => page.evaluate(() => globalThis.__productInputMessages))
        .toEqual([
            { type: "key", key: 0x77, down: true },
            { type: "key", key: 0x77, down: false },
            { type: "key", key: 0x20, down: true },
            { type: "key", key: 0x20, down: false },
            { type: "key", key: 0x3B, down: true },
            { type: "key", key: 0x3B, down: false },
            { type: "key", key: 0xB2, down: true },
            { type: "key", key: 0xB2, down: false },
            { type: "key", key: 0xBA, down: true },
            { type: "key", key: 0xBA, down: false },
            { type: "key", key: 0x1B, down: true },
            { type: "key", key: 0x1B, down: false },
        ]);

    await page.evaluate(() => {
        globalThis.__productInputMessages = [];
        globalThis.dispatchEvent(new CustomEvent("kisakcod:mouse-mode", {
            detail: { absolute: true },
        }));
        const canvasElement = document.querySelector("#game-canvas");
        for (const button of [0, 2, 1, 3, 4]) {
            canvasElement.dispatchEvent(new MouseEvent("mousedown", { button, bubbles: true }));
            globalThis.dispatchEvent(new MouseEvent("mouseup", { button }));
        }
        globalThis.__auxClickPrevented = !canvasElement.dispatchEvent(
            new MouseEvent("auxclick", { button: 3, bubbles: true, cancelable: true }));
        for (const [deltaY, deltaMode] of [[-1, 0], [3, 1], [-1, 2]]) {
            canvasElement.dispatchEvent(new WheelEvent("wheel", {
                deltaY, deltaMode, bubbles: true, cancelable: true,
            }));
        }
        const bounds = canvasElement.getBoundingClientRect();
        canvasElement.dispatchEvent(new MouseEvent("mousemove", {
            clientX: bounds.left + bounds.width * 0.25,
            clientY: bounds.top + bounds.height * 0.75,
            bubbles: true,
        }));
    });
    await expect.poll(() => page.evaluate(() => globalThis.__productInputMessages))
        .toEqual([
            { type: "key", key: 0xC8, down: true },
            { type: "key", key: 0xC8, down: false },
            { type: "key", key: 0xC9, down: true },
            { type: "key", key: 0xC9, down: false },
            { type: "key", key: 0xCA, down: true },
            { type: "key", key: 0xCA, down: false },
            { type: "key", key: 0xCB, down: true },
            { type: "key", key: 0xCB, down: false },
            { type: "key", key: 0xCC, down: true },
            { type: "key", key: 0xCC, down: false },
            { type: "key", key: 0xCE, down: true },
            { type: "key", key: 0xCE, down: false },
            { type: "key", key: 0xCD, down: true },
            { type: "key", key: 0xCD, down: false },
            { type: "key", key: 0xCE, down: true },
            { type: "key", key: 0xCE, down: false },
            expect.objectContaining({ type: "mouse-move", dx: 0, dy: 0 }),
        ]);
    expect(await page.evaluate(() => globalThis.__auxClickPrevented)).toBe(true);

    await page.evaluate(() => {
        globalThis.__productInputMessages = [];
        globalThis.dispatchEvent(new CustomEvent("kisakcod:mouse-mode", {
            detail: { absolute: false },
        }));
    });
    await canvas.click({ position: { x: 10, y: 10 } });
    await expect.poll(() => page.evaluate(() => document.pointerLockElement?.id))
        .toBe("game-canvas");
    await page.evaluate(() => {
        globalThis.__productInputMessages = [];
        const firstMovement = new MouseEvent("mousemove");
        Object.defineProperties(firstMovement, {
            movementX: { value: 3 },
            movementY: { value: -1 },
        });
        globalThis.dispatchEvent(firstMovement);
        const secondMovement = new MouseEvent("mousemove");
        Object.defineProperties(secondMovement, {
            movementX: { value: 4 },
            movementY: { value: -3 },
        });
        globalThis.dispatchEvent(secondMovement);
        document.querySelector("#game-canvas").dispatchEvent(
            new MouseEvent("mousedown", { button: 2, bubbles: true }));
        document.exitPointerLock();
    });
    await expect.poll(() => page.evaluate(() => document.pointerLockElement)).toBeNull();
    await expect.poll(() => page.evaluate(() => globalThis.__productInputMessages))
        .toContainEqual(expect.objectContaining({
            type: "mouse-move", dx: 7, dy: -4,
        }));
    await expect.poll(() => page.evaluate(() => globalThis.__productInputMessages))
        .toContainEqual({ type: "key", key: 0xC9, down: false });

    await page.evaluate(() => {
        globalThis.__productInputMessages = [];
        globalThis.dispatchEvent(new CustomEvent("kisakcod:mouse-mode", {
            detail: { absolute: true },
        }));
        const canvasElement = document.querySelector("#game-canvas");
        canvasElement.focus();
        globalThis.dispatchEvent(new KeyboardEvent("keydown", { code: "KeyA" }));
        canvasElement.dispatchEvent(new MouseEvent("mousedown", { button: 0, bubbles: true }));
        globalThis.dispatchEvent(new Event("blur"));
    });
    await expect.poll(() => page.evaluate(() => globalThis.__productInputMessages))
        .toEqual([
            { type: "key", key: 0x61, down: true },
            { type: "key", key: 0xC8, down: true },
            { type: "key", key: 0x61, down: false },
            { type: "key", key: 0xC8, down: false },
        ]);

    const countAfterDispose = await page.evaluate(async () => {
        globalThis.__productInputMessages = [];
        const canvasElement = document.querySelector("#game-canvas");
        canvasElement.focus();
        globalThis.dispatchEvent(new KeyboardEvent("keydown", { code: "KeyQ" }));
        canvasElement.dispatchEvent(new MouseEvent("mousedown", { button: 2, bubbles: true }));
        globalThis.dispatchEvent(new PageTransitionEvent("pagehide"));
        await new Promise((resolve) => setTimeout(resolve, 0));
        const count = globalThis.__productInputMessages.length;
        globalThis.dispatchEvent(new KeyboardEvent("keydown", { code: "KeyW" }));
        canvasElement.dispatchEvent(new MouseEvent("mousedown", { button: 0, bubbles: true }));
        return count;
    });
    await page.waitForTimeout(50);
    expect(await page.evaluate(() => globalThis.__productInputMessages)).toEqual([
        { type: "key", key: 0x71, down: true },
        { type: "key", key: 0xC9, down: true },
        { type: "key", key: 0x71, down: false },
        { type: "key", key: 0xC9, down: false },
    ]);
    expect(countAfterDispose).toBe(4);
});
