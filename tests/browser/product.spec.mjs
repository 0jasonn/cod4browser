import { expect, test } from "@playwright/test";
import { createInstallDirectory } from "./install_fixture.mjs";
import { createSyntheticIwd } from "./synthetic_iwd.mjs";

test.skip(process.env.KISAK_WEB_PRODUCT_TEST !== "1", "Runs only against the production site.");

test("canonical Quit stops the French production runtime and can start again @product", async ({ page }, testInfo) => {
    const directory = await createInstallDirectory(testInfo, "quit-install", { language: "french" });
    await page.goto("/");
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "running");
    const chooser = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    await (await chooser).setFiles(directory);
    await expect(page.locator("#boot-log")).toContainText("canonical runtime started");
    await page.locator("#engine-command-input").fill("path");
    await page.locator("#engine-command-submit").click();
    await expect(page.locator("#boot-log")).toContainText("Current language: french");
    await page.locator("#engine-command-input").fill("snd_volume 0.37; quit");
    await page.locator("#engine-command-submit").click();
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "stopped");
    await expect(page.locator("#quit-dialog")).toBeVisible();
    const frame = await page.locator("#frame-counter").textContent();
    await page.waitForTimeout(150);
    await expect(page.locator("#frame-counter")).toHaveText(frame);
    const savedConfigs = await page.evaluate(async () => {
        const root = await navigator.storage.getDirectory();
        const app = await root.getDirectoryHandle("kisakcod-web");
        const home = await app.getDirectoryHandle("home");
        const configs = [];
        async function visit(directory) {
            for await (const [name, handle] of directory.entries()) {
                if (handle.kind === "directory") await visit(handle);
                else if (name === "config.cfg") configs.push(await (await handle.getFile()).text());
            }
        }
        await visit(home);
        return configs;
    });
    expect(savedConfigs).toEqual(expect.arrayContaining([
        expect.stringContaining('seta snd_volume "0.37"'),
    ]));
    expect(await page.evaluate(async () => (await navigator.locks.query()).held
        .some(({ name }) => name.includes("engine-filesystem") || name.includes("home-writer"))))
        .toBe(false);
    await page.getByRole("button", { name: "Start game", exact: true }).click();
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "running");
    await expect(page.locator("#boot-log")).toContainText("canonical runtime started");
});

test("display resolution applies through native restart and survives durable restart @product", async ({ page }, testInfo) => {
    const directory = await createInstallDirectory(testInfo, "display-install");
    await page.goto("/");
    const chooser = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    await (await chooser).setFiles(directory);
    await expect(page.locator("#boot-log")).toContainText("canonical runtime started");
    const command = async (text) => {
        await page.locator("#engine-command-input").fill(text);
        await page.locator("#engine-command-submit").click();
        await expect(page.locator("#engine-command-status")).toHaveText(`Accepted: ${text}`);
    };
    const canvas = page.locator("#game-canvas");
    const size = () => canvas.evaluate((element) => [element.width, element.height]);
    await command("r_mode 640x480; vid_restart");
    await expect.poll(size).toEqual([640, 480]);
    const bounds = await canvas.boundingBox();
    expect(bounds.width / bounds.height).toBeCloseTo(4 / 3, 2);
    await page.setViewportSize({ width: 1000, height: 800 });
    await expect.poll(size).toEqual([640, 480]);
    await command("r_mode Automatic; vid_restart");
    await expect.poll(() => canvas.evaluate((element) => {
        const rect = element.getBoundingClientRect();
        return Math.abs(element.width - Math.round(rect.width)) +
            Math.abs(element.height - Math.round(rect.height));
    })).toBe(0);
    await command("r_displayRefresh");
    await expect(page.locator("#boot-log")).toContainText('"r_displayRefresh" is: "Browser controlled');
    await command("r_mode 1280x720; vid_restart");
    await expect.poll(size).toEqual([1280, 720]);
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "running");
    await command("quit");
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "stopped");
    await page.getByRole("button", { name: "Start game", exact: true }).click();
    await expect(page.locator("#boot-log")).toContainText("canonical runtime started");
    await expect.poll(size).toEqual([1280, 720]);
});

test("Quit retains the runtime when saving fails and retries before disposal @product", async ({ page }) => {
    await page.goto("/");
    await page.evaluate(async () => {
        const { createBrowserQuit } = await import("/browser_quit.mjs");
        globalThis.quitCalls = [];
        let fail = true;
        const controller = createBrowserQuit({
            engine: { async flushAndUnmount() {
                globalThis.quitCalls.push("flush");
                if (fail) { fail = false; throw new Error("storage full"); }
            } },
            onStop: () => globalThis.quitCalls.push("stop"),
            dispose: async () => { globalThis.quitCalls.push("dispose"); },
            dialog: document.querySelector("#quit-dialog"),
        });
        await controller.request();
    });
    await expect(page.locator("#quit-dialog")).toContainText("storage full");
    expect(await page.evaluate(() => globalThis.quitCalls)).toEqual(["stop", "flush"]);
    await page.keyboard.press("Escape");
    await expect(page.locator("#quit-dialog")).toBeVisible();
    await page.getByRole("button", { name: "Retry save and quit" }).click();
    await expect(page.locator("#quit-dialog")).toContainText("Game closed");
    expect(await page.evaluate(() => globalThis.quitCalls))
        .toEqual(["stop", "flush", "flush", "dispose"]);
});

test("production mount reports canonical filesystem errors @product", async ({ page }, testInfo) => {
    const directory = await createInstallDirectory(testInfo, "missing-filesystem-check", {
        overrides: new Map([["main/iw_13.iwd", createSyntheticIwd()]]),
    });
    await page.goto("/");
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "running");
    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    await (await chooserPromise).setFiles(directory);
    await expect(page.locator("#asset-message")).toContainText("fileSysCheck.cfg");
    await expect(page.locator("#asset-message")).not.toContainText("[object Object]");
});

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
        "browser_quit.mjs",
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

for (const [missingCapability, label, expectedWorkers] of [
    ["web-locks", "Web Locks", 0],
    ["broadcast-channel", "BroadcastChannel", 0],
    ["offscreen-transfer", "transferable OffscreenCanvas", 0],
    ["opfs", "OPFS", 0],
    ["sync-access", "Worker OPFS sync access", 1],
]) {
    test(`missing ${label} fails before asset import @product`, async ({ page }) => {
        await page.addInitScript((missing) => {
            const NativeWorker = globalThis.Worker;
            globalThis.__capabilityWorkerConstructions = 0;
            globalThis.__capabilityStorageAccesses = 0;
            const storage = navigator.storage;
            const getDirectory = storage.getDirectory?.bind(storage);
            if (getDirectory) {
                storage.getDirectory = (...arguments_) => {
                    ++globalThis.__capabilityStorageAccesses;
                    return getDirectory(...arguments_);
                };
            }
            if (missing === "web-locks") {
                Object.defineProperty(navigator, "locks", {
                    configurable: true,
                    value: undefined,
                });
            } else if (missing === "broadcast-channel") {
                Object.defineProperty(globalThis, "BroadcastChannel", {
                    configurable: true,
                    value: undefined,
                });
            } else if (missing === "offscreen-transfer") {
                HTMLCanvasElement.prototype.transferControlToOffscreen = () => {
                    throw new DOMException("transfer disabled", "NotSupportedError");
                };
            } else if (missing === "opfs") {
                Object.defineProperty(storage, "getDirectory", {
                    configurable: true,
                    value: undefined,
                });
            }
            if (missing === "sync-access") {
                globalThis.Worker = class {
                    constructor() { ++globalThis.__capabilityWorkerConstructions; }
                    postMessage() {
                        queueMicrotask(() => this.onmessage?.({ data: {
                            type: "kisak-capability-probe-result",
                            offscreenCanvas: {
                                supported: true,
                                capability: "offscreenCanvas",
                                code: "OK",
                                message: "Available",
                                detail: "",
                            },
                            opfsSyncAccess: {
                                supported: false,
                                capability: "opfsSyncAccessHandle",
                                code: "SYNC_ACCESS_HANDLE_OPEN_FAILED",
                                message: "Unavailable",
                                detail: "test",
                            },
                        } }));
                    }
                    terminate() {}
                };
            } else {
                globalThis.Worker = class extends NativeWorker {
                    constructor(...arguments_) {
                        ++globalThis.__capabilityWorkerConstructions;
                        super(...arguments_);
                    }
                };
            }
        }, missingCapability);
        await page.goto("/");
        await expect(page.locator("html"))
            .toHaveAttribute("data-runtime-state", "unsupported");
        await expect(page.locator("#asset-message")).toContainText(label);
        await expect(page.locator("#select-install-button")).toBeDisabled();
        expect(await page.evaluate(() => globalThis.__capabilityWorkerConstructions))
            .toBe(expectedWorkers);
        expect(await page.evaluate(() => globalThis.__capabilityStorageAccesses)).toBe(0);
    });
}

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
