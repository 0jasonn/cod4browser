import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";

import { chromium, expect, test as base } from "@playwright/test";

const retailRoot = process.env.KISAK_COD4_RETAIL_ROOT;
const browserChannel = process.env.KISAK_BROWSER_CHANNEL;

const test = base.extend({
    retailPage: async ({}, use, testInfo) => {
        const profile = await mkdtemp(join(tmpdir(), "kisakcod-ui-"));
        const context = await chromium.launchPersistentContext(profile, {
            baseURL: testInfo.project.use.baseURL,
            headless: testInfo.project.use.headless ?? true,
            viewport: testInfo.project.use.viewport,
            ...(browserChannel ? { channel: browserChannel } : {}),
        });
        const page = context.pages()[0] ?? await context.newPage();
        try {
            await use(page);
        } finally {
            try {
                if (testInfo.status !== testInfo.expectedStatus && !page.isClosed()) {
                    const reverb = await page.evaluate(() => {
                        const driver = globalThis.__observedReverbDriver;
                        return driver ? { telemetry: driver.publishTelemetry(),
                            contextState: driver.context?.state,
                            diagnostics: globalThis.__reverbDiagnostics,
                            messages: globalThis.__reverbMessages,
                            sources: [...driver.sources.values()].map((source) => ({
                                alias: source.aliasName, wet: source.wet, gain: source.gain,
                                state: source.state })) } : null;
                    });
                    if (reverb) await writeFile(testInfo.outputPath("reverb-failure.json"),
                        JSON.stringify(reverb, null, 2));
                }
            } finally {
                try {
                    await context.close();
                } finally {
                    await rm(profile, { recursive: true, force: true, maxRetries: 5 });
                }
            }
        }
    },
});

test("production canonical reverb controls affect playing Killhouse audio", {
    tag: ["@retail-reverb", "@product"],
}, async ({ retailPage: page }, testInfo) => {
    test.skip(process.env.KISAK_WEB_PRODUCT_TEST !== "1", "Runs against the production site.");
    test.setTimeout(480_000);
    const failures = [];
    page.on("pageerror", (error) => failures.push(String(error)));
    await page.addInitScript(() => {
        globalThis.__reverbScene = null;
        globalThis.addEventListener("kisakcod:renderer-scene-frame", ({ detail }) => {
            globalThis.__reverbScene = detail;
        });
    });
    await page.goto("/");
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "running");
    await page.evaluate(async () => {
        const { WebAudioDriver } = await import("/web_audio_driver.mjs");
        const original = WebAudioDriver.prototype.handleCommand;
        globalThis.__reverbMessages = [];
        globalThis.__reverbDiagnostics = [];
        const diagnostic = WebAudioDriver.prototype.diagnostic;
        WebAudioDriver.prototype.diagnostic = function (message) {
            globalThis.__reverbDiagnostics.push(String(message));
            return diagnostic.call(this, message);
        };
        WebAudioDriver.prototype.handleCommand = function (command) {
            const result = original.call(this, command);
            globalThis.__observedReverbDriver = this;
            if (command.op === "room-type" || command.wet > 0) {
                const messages = globalThis.__reverbMessages;
                messages.push({ op: command.op, room: command.id, wet: command.wet,
                    alias: command.aliasName, at: performance.now() });
                if (messages.length > 2000) messages.shift();
            }
            return result;
        };
    });
    const chooser = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    await (await chooser).setFiles(retailRoot);
    await expect(page.locator(".asset-control")).toHaveAttribute("data-asset-state", "ready", { timeout: 300_000 });
    await expect(page.locator("#boot-log")).toContainText("canonical runtime started", { timeout: 300_000 });
    const command = async (text) => {
        await page.locator("#engine-command-input").fill(text);
        await page.locator("#engine-command-submit").click();
        await expect(page.locator("#engine-command-status")).toHaveText(`Accepted: ${text}`);
    };
    await command("map killhouse");
    await expect.poll(() => page.evaluate(() => globalThis.__reverbScene?.worldName),
        { timeout: 300_000 }).toContain("killhouse");
    await page.locator("#game-canvas").click({ position: { x: 5, y: 5 } });
    await expect.poll(() => page.evaluate(() => {
        const driver = globalThis.__observedReverbDriver;
        return driver.roomType === 17 && [...driver.sources.values()].some((source) =>
            source.state === "playing" && Math.abs(source.wet - 0.3) < 1e-6);
    })).toBe(true);
    const baseline = await page.evaluate(() => {
        const driver = globalThis.__observedReverbDriver;
        return { room: driver.roomType, sources: [...driver.sources.values()]
            .filter((source) => source.state === "playing")
            .map((source) => ({ alias: source.aliasName, wet: source.wet })) };
    });
    // Exercise the existing sound console API, without replacing any alias,
    // PCM, mission script, player action, or objective. This is a device
    // qualification, not evidence of an authored campaign reverb transition.
    // The cgame clears the shellshock priority every frame when no shock is
    // active (EndShellShockSound). Use the level console API, then restore
    // the observed Killhouse exterior preset after the device check.
    await command("snd_setEnvironmentEffects level cave 1 0.75 1000");
    await expect.poll(() => page.evaluate(() => {
        const driver = globalThis.__observedReverbDriver;
        return { room: driver.roomType, ready: driver.reverbNode !== null };
    })).toEqual({ room: 8, ready: true });
    await expect.poll(() => page.evaluate(() => [...globalThis.__observedReverbDriver.sources.values()]
        .some((source) => source.state === "playing" && source.wet >= 0.74 && source.gain > 0)),
    { timeout: 20_000 }).toBe(true);
    await page.evaluate(() => {
        const driver = globalThis.__observedReverbDriver;
        const analyser = driver.context.createAnalyser();
        analyser.fftSize = 2048;
        driver.reverbNode.connect(analyser);
        globalThis.__wetAnalyser = analyser;
    });
    await expect.poll(() => page.evaluate(() => {
        const samples = new Float32Array(2048);
        globalThis.__wetAnalyser.getFloatTimeDomainData(samples);
        return samples.reduce((sum, value) => sum + value * value, 0);
    }), { timeout: 20_000 }).toBeGreaterThan(1e-12);
    const evidence = await page.evaluate(() => {
        const driver = globalThis.__observedReverbDriver;
        const samples = new Float32Array(2048);
        globalThis.__wetAnalyser.getFloatTimeDomainData(samples);
        return { telemetry: driver.publishTelemetry(), contextState: driver.context.state,
            wetEnergy: samples.reduce((sum, value) => sum + value * value, 0),
            messages: globalThis.__reverbMessages,
            sources: [...driver.sources.values()].filter((source) => source.state === "playing")
                .map((source) => ({ alias: source.aliasName, wet: source.wet, gain: source.gain })) };
    });
    expect(evidence.contextState).toBe("running");
    expect(evidence.messages.some(({ wet }) => wet > 0.300001 && wet < 0.74)).toBe(true);
    await command("snd_setEnvironmentEffects level mountains 1 0.3 500");
    await expect.poll(() => page.evaluate(() => {
        const driver = globalThis.__observedReverbDriver;
        return driver.roomType === 17 && [...driver.sources.values()].some((source) =>
            source.state === "playing" && Math.abs(source.wet - 0.3) < 1e-6);
    })).toBe(true);
    expect(await page.evaluate(() => globalThis.__reverbDiagnostics)).toEqual([]);
    expect(failures).toEqual([]);
    expect(await page.evaluate(() => "__KISAKCOD_WEB__" in globalThis)).toBe(false);
    await writeFile(testInfo.outputPath("reverb-evidence.json"), JSON.stringify({ baseline, ...evidence }, null, 2));
    await testInfo.attach("reverb-evidence", { contentType: "application/json",
        body: Buffer.from(JSON.stringify({ baseline, ...evidence })) });
});

test("production shipped brightness slider changes menu and world pixels", {
    tag: ["@retail-gamma", "@product"],
}, async ({ retailPage: page }, testInfo) => {
    test.skip(process.env.KISAK_WEB_PRODUCT_TEST !== "1", "Runs against the production site.");
    test.setTimeout(480_000);
    const errors = [];
    page.on("pageerror", (error) => errors.push(String(error)));
    await page.addInitScript(() => {
        globalThis.__gammaScene = null;
        addEventListener("kisakcod:renderer-scene-frame", ({ detail }) => {
            globalThis.__gammaScene = detail;
        });
    });
    await page.goto("/");
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "running");
    const chooser = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    await (await chooser).setFiles(retailRoot);
    await expect(page.locator(".asset-control")).toHaveAttribute("data-asset-state", "ready", { timeout: 300_000 });
    await expect(page.locator("#boot-log")).toContainText("canonical runtime started", { timeout: 300_000 });
    const command = async (text) => {
        await page.locator("#engine-command-input").fill(text);
        await page.locator("#engine-command-submit").click();
        await expect(page.locator("#engine-command-status")).toHaveText(`Accepted: ${text}`);
    };
    const meanPixel = () => page.evaluate(() => {
        const sample = new OffscreenCanvas(32, 18);
        const context = sample.getContext("2d");
        context.drawImage(document.querySelector("#game-canvas"), 0, 0, 32, 18);
        const pixels = context.getImageData(0, 0, 32, 18).data;
        let total = 0;
        for (let i = 0; i < pixels.length; i += 4) total += pixels[i] + pixels[i + 1] + pixels[i + 2];
        return total / (32 * 18 * 3);
    });
    await command("openmenu main_options");
    const canvas = page.locator("#game-canvas");
    // Canonical centered 640x480 menu positions work at either display aspect.
    const clickMenu = async (x, y) => {
        const bounds = await canvas.boundingBox();
        await canvas.click({ position: {
            x: (bounds.width - bounds.height * 4 / 3) / 2 + x * bounds.height / 480,
            y: y * bounds.height / 480,
        } });
    };
    await clickMenu(190, 45); // Graphics
    await clickMenu(454, 130); // Brightness slider, near minimum
    await command("r_gamma");
    await expect(page.locator("#boot-log")).toContainText('"r_gamma" is: "0.5');
    const lowMenu = await meanPixel();
    expect(lowMenu).toBeGreaterThan(0);
    await canvas.screenshot({ path: testInfo.outputPath("brightness-low.png") });
    await clickMenu(534, 130); // Same canonical slider, near maximum
    await command("r_gamma");
    await expect(page.locator("#boot-log")).toContainText('"r_gamma" is: "2.9');
    await expect.poll(meanPixel).toBeGreaterThan(lowMenu + 20);
    const highMenu = await meanPixel();
    await canvas.screenshot({ path: testInfo.outputPath("brightness-high.png") });

    // Loaded-world display qualification only; no mission progress is inferred.
    await command("map killhouse");
    await expect.poll(() => page.evaluate(() => globalThis.__gammaScene?.worldName),
        { timeout: 300_000 }).toContain("killhouse");
    await expect.poll(meanPixel).toBeGreaterThan(30);
    const highWorld = await meanPixel();
    await command("r_gamma 0.5");
    await expect.poll(meanPixel).toBeLessThan(highWorld - 20);
    const lowWorld = await meanPixel();
    await command("r_gamma 0.8");
    expect(errors).toEqual([]);
    expect(await page.evaluate(() => "__KISAKCOD_WEB__" in globalThis)).toBe(false);
    await writeFile(testInfo.outputPath("gamma-evidence.json"), JSON.stringify({
        browser: page.context().browser()?.version(), lowMenu, highMenu, lowWorld, highWorld, errors,
    }, null, 2));
});

test("production shipped display Apply and in-game restart preserve the loaded mission", {
    tag: ["@retail-display", "@product"],
}, async ({ retailPage: page }, testInfo) => {
    test.skip(process.env.KISAK_WEB_PRODUCT_TEST !== "1", "Runs against the production site.");
    test.setTimeout(480_000);
    const errors = [];
    page.on("pageerror", (error) => errors.push(String(error)));
    await page.addInitScript(() => {
        globalThis.__displayScene = null;
        globalThis.__displayView = null;
        globalThis.__displayLogs = [];
        const NativeWorker = globalThis.Worker;
        globalThis.Worker = class extends NativeWorker {
            constructor(...args) {
                super(...args);
                this.addEventListener("message", ({ data }) => {
                    if (data.type === "log") globalThis.__displayLogs.push(String(data.message));
                });
            }
        };
        addEventListener("kisakcod:renderer-scene-frame", ({ detail }) => {
            globalThis.__displayScene = detail;
        });
        addEventListener("kisakcod:renderer-scene-view", ({ detail }) => {
            globalThis.__displayView = detail;
        });
    });
    await page.goto("/");
    const canvas = page.locator("#game-canvas");
    const size = () => canvas.evaluate((element) => [element.width, element.height]);
    const command = async (text) => {
        await page.locator("#engine-command-input").fill(text);
        await page.locator("#engine-command-submit").click();
        await expect(page.locator("#engine-command-status")).toHaveText(`Accepted: ${text}`);
    };
    const clickMenu = async (x, y) => {
        const bounds = await canvas.boundingBox();
        await canvas.click({ position: {
            x: (bounds.width - bounds.height * 4 / 3) / 2 + x * bounds.height / 480,
            y: y * bounds.height / 480,
        } });
    };
    try {
        const chooser = page.waitForEvent("filechooser");
        await page.locator("#portable-install-button").click();
        await (await chooser).setFiles(retailRoot);
        await expect(page.locator(".asset-control")).toHaveAttribute("data-asset-state", "ready", { timeout: 300_000 });
        await expect(page.locator("#boot-log")).toContainText("canonical runtime started", { timeout: 300_000 });
        await command("r_mode Automatic; vid_restart");
        await expect.poll(() => canvas.evaluate((element) => element.width)).toBeGreaterThan(640);
        await command("openmenu main_options");
        await clickMenu(190, 45); // Graphics, in canonical centered 640x480 coordinates.
        await clickMenu(480, 42); // Video Mode: Automatic -> 640x480.
        await clickMenu(535, 464); // Apply.
        await clickMenu(360, 260); // Native Apply Settings confirmation: Yes.
        await expect.poll(size).toEqual([640, 480]);
        await canvas.screenshot({ path: testInfo.outputPath("display-applied.png") });
        const bounds = await canvas.boundingBox();
        expect(bounds.width / bounds.height).toBeCloseTo(4 / 3, 2);
        await command("quit");
        await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "stopped");
        await page.getByRole("button", { name: "Start game", exact: true }).click();
        await expect(page.locator("#boot-log")).toContainText("canonical runtime started");
        await expect.poll(size).toEqual([640, 480]);
        await command("r_mode Automatic; vid_restart");
        await command("map killhouse");
        await expect.poll(() => page.evaluate(() => globalThis.__displayScene?.worldName),
            { timeout: 300_000 }).toContain("killhouse");
        await page.setViewportSize({ width: 1100, height: 850 });
        await expect.poll(() => page.evaluate(() => {
            const canvas = document.querySelector("#game-canvas");
            const bounds = canvas.getBoundingClientRect();
            const viewport = globalThis.__displayView?.viewport;
            return canvas.width === Math.round(bounds.width * devicePixelRatio) &&
                canvas.height === Math.round(bounds.height * devicePixelRatio) &&
                viewport?.width === canvas.width && viewport?.height === canvas.height;
        })).toBe(true);
        // Let the authored opening camera/fall settle before comparing the
        // restored view. No player, script, objective or save state is injected.
        await expect.poll(() => page.evaluate(() => globalThis.__displayView?.time),
            { timeout: 30_000 }).toBeGreaterThanOrEqual(6000);
        const before = await page.evaluate(() => globalThis.__displayView);
        await page.evaluate(() => { globalThis.__displayView = null; globalThis.__displayScene = null; });
        await command("r_mode 1280x720; vid_restart");
        // Keep the actual Worker logs: native reload can roll the launcher's
        // bounded 160-line display past this message before the next poll.
        await expect.poll(() => page.evaluate(() => globalThis.__displayLogs.join("\n")),
            { timeout: 60_000 }).toContain("Game saved for vid_restart");
        await expect.poll(size, { timeout: 60_000 }).toEqual([1280, 720]);
        await expect.poll(() => page.evaluate(() => globalThis.__displayView?.viewport),
            { timeout: 300_000 }).toEqual({ x: 0, y: 0, width: 1280, height: 720 });
        await expect.poll(() => page.evaluate(() => globalThis.__displayScene?.worldName)).toContain("killhouse");
        await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "running");
        await canvas.screenshot({ path: testInfo.outputPath("display-mission-restored.png") });
        const after = await page.evaluate(() => globalThis.__displayView);
        expect(after.time).toBeGreaterThanOrEqual(before.time);
        for (let axis = 0; axis < 3; ++axis)
            expect(Math.abs(after.viewOrigin[axis] - before.viewOrigin[axis])).toBeLessThan(5);
        expect(errors).toEqual([]);
        expect(await page.evaluate(() => "__KISAKCOD_WEB__" in globalThis)).toBe(false);
        await writeFile(testInfo.outputPath("display-evidence.json"), JSON.stringify({
            browser: page.context().browser()?.version(), before,
            after, errors,
        }, null, 2));
    } finally {
        await writeFile(testInfo.outputPath("display-engine.log"),
            await page.evaluate(() => globalThis.__displayLogs.join("\n")));
    }
});

test("production texture quality survives renderer restart and a new runtime", {
    tag: ["@retail-quality", "@product"],
}, async ({ retailPage: page }, testInfo) => {
    test.skip(process.env.KISAK_WEB_PRODUCT_TEST !== "1", "Runs against the production site.");
    test.setTimeout(600_000);
    await page.addInitScript(() => {
        globalThis.__qualityMemory = null;
        globalThis.__qualityScene = null;
        globalThis.__qualityLogs = [];
        const NativeWorker = globalThis.Worker;
        globalThis.Worker = class extends NativeWorker {
            constructor(...arguments_) {
                super(...arguments_);
                this.addEventListener("message", ({ data }) => {
                    if (data.type === "log")
                        globalThis.__qualityLogs.push(String(data.message));
                });
            }
        };
        addEventListener("kisakcod:renderer-memory", ({ detail }) => {
            if (detail.state === "static-models-submitted") globalThis.__qualityMemory = detail;
        });
        addEventListener("kisakcod:renderer-scene-frame", ({ detail }) => {
            globalThis.__qualityScene = detail;
        });
    });
    const command = async (text) => {
        await page.locator("#engine-command-input").fill(text);
        await page.locator("#engine-command-submit").click();
        await expect(page.locator("#engine-command-status")).toHaveText(`Accepted: ${text}`);
    };
    const loaded = async () => {
        await expect.poll(() => page.evaluate(() => globalThis.__qualityScene?.worldName),
            { timeout: 300_000 }).toContain("killhouse");
        await expect.poll(() => page.evaluate(() => globalThis.__qualityMemory?.decodedTextureSourceBytes ?? 0),
            { timeout: 60_000 }).toBeGreaterThan(1_000_000);
        return page.evaluate(() => globalThis.__qualityMemory);
    };
    const clear = () => page.evaluate(() => {
        globalThis.__qualityMemory = null;
        globalThis.__qualityScene = null;
    });
    const canvas = page.locator("#game-canvas");
    const clickMenu = async (x, y) => {
        const bounds = await canvas.boundingBox();
        await canvas.click({ position: {
            x: (bounds.width - bounds.height * 4 / 3) / 2 + x * bounds.height / 480,
            y: y * bounds.height / 480,
        } });
    };
    await page.goto("/");
    const chooser = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    await (await chooser).setFiles(retailRoot);
    await expect(page.locator("#boot-log")).toContainText("canonical runtime started", { timeout: 300_000 });
    await command("map killhouse");
    const full = await loaded();
    await canvas.screenshot({ path: testInfo.outputPath("quality-full.png") });
    // Loading and renderer lifecycle only. No routes, objective changes, or
    // mission acceptance. Return through canonical disconnect after measuring
    // the full-quality scene because the authored opening UI owns in-map input.
    await clear();
    await command("disconnect");
    await expect.poll(() => page.evaluate(() => globalThis.__qualityLogs.join("\n")),
        { timeout: 60_000 }).toContain("----- Server Shutdown -----");
    await command("openmenu main_options");
    await clickMenu(190, 45); // Graphics.
    await clickMenu(190, 70); // Texture Settings.
    await clickMenu(480, 86); // Texture Quality: Automatic -> Manual.
    for (const y of [108, 130, 152]) {
        await clickMenu(480, y); // Extra -> High.
        await clickMenu(480, y); // High -> Normal (picmip 2).
    }
    await canvas.screenshot({ path: testInfo.outputPath("quality-menu-normal.png") });
    await clickMenu(535, 464); // Apply.
    await clickMenu(360, 260); // Native Apply Settings confirmation: Yes.
    await expect.poll(() => page.evaluate(() => globalThis.__qualityLogs.join("\n")),
        { timeout: 60_000 }).toContain("Picmip is set manually.");
    await command("map killhouse");
    const reduced = await loaded();
    expect(reduced.decodedTextureSourceBytes).toBeLessThan(full.decodedTextureSourceBytes * 0.6);
    expect(reduced.gpuTextureEstimateBytes).toBeLessThan(full.gpuTextureEstimateBytes);
    const appliedLogs = await page.evaluate(() => globalThis.__qualityLogs.join("\n"));
    expect(appliedLogs).toContain("Picmip is set manually.");
    expect(appliedLogs).toContain("Using picmip 2 on most textures, 2 on normal maps, and 2 on specular maps");
    expect(appliedLogs).not.toContain('Unknown command "r_applyPicmip"');
    await canvas.screenshot({ path: testInfo.outputPath("quality-reduced.png") });
    await command("quit");
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "stopped");
    await page.getByRole("button", { name: "Start game", exact: true }).click();
    await expect(page.locator("#boot-log")).toContainText("canonical runtime started", { timeout: 300_000 });
    await clear();
    await command("map killhouse");
    const persisted = await loaded();
    expect(persisted.decodedTextureSourceBytes).toBeLessThan(full.decodedTextureSourceBytes * 0.6);
    await writeFile(testInfo.outputPath("quality-evidence.json"), JSON.stringify({
        full, reduced, persisted,
        menu: { quality: "Manual", color: "Normal", normal: "Normal", specular: "Normal" },
    }, null, 2));
});

test.skip(!retailRoot,
    "RETAIL_ROOT_MISSING: set KISAK_COD4_RETAIL_ROOT to a legally owned COD4 installation");

function nameHash(text)
{
    let hash = 2166136261;
    for (const character of text.toLowerCase()) {
        hash ^= character.codePointAt(0);
        hash = Math.imul(hash, 16777619) >>> 0;
    }
    return hash;
}

async function call(page, name, ...arguments_)
{
    return page.evaluate(({ name, arguments_ }) =>
        globalThis.__KISAKCOD_WEB__.module.call(name, ...arguments_), {
        name, arguments_,
    });
}

test("canonical console and shipped profile field accept browser typing", { tag: "@retail-text" },
    async ({ retailPage: page }, testInfo) => {
        test.setTimeout(360_000);
        await page.addInitScript(() => {
            globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ = { observeInput: true };
            globalThis.__textLogs = [];
            globalThis.__textInputEvents = [];
            globalThis.addEventListener("kisakcod:log", ({ detail }) =>
                globalThis.__textLogs.push(detail.text));
            globalThis.addEventListener("kisakcod:input", ({ detail }) =>
                globalThis.__textInputEvents.push(detail));
        });
        await page.goto("/");
        const chooser = page.waitForEvent("filechooser");
        await page.locator("#portable-install-button").click();
        await (await chooser).setFiles(retailRoot);
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__?.module?.filesystemState),
        { timeout: 300_000 }).toBe("mounted");
        const canvas = page.locator("#game-canvas");
        await page.bringToFront();
        await canvas.focus();
        await page.keyboard.press("Backquote");
        await expect.poll(async () => (await call(page, "_KisakWeb_TestUiState", 3)) & 1)
            .toBe(1);
        await page.evaluate(() => {
            const paste = new Event("paste", { cancelable: true });
            Object.defineProperty(paste, "clipboardData", { value: {
                getData: (type) => type === "text/plain"
                    ? 'seta kisak_text "Browser Text!"x\nignored' : "",
            } });
            globalThis.dispatchEvent(paste);
        });
        await expect.poll(() => page.evaluate(() => globalThis.__textInputEvents.slice(-2)))
            .toEqual([
                { type: "clipboard", characters: Array.from(
                    new TextEncoder().encode('seta kisak_text "Browser Text!"x')) },
                { type: "char", character: 22 },
            ]);
        await page.keyboard.press("Backspace");
        await page.keyboard.press("Enter");
        await page.keyboard.type("kisak_text");
        await page.keyboard.press("Enter");
        await expect.poll(() => page.evaluate(() => globalThis.__textLogs.join("\n")))
            .toContain('"kisak_text" is: "Browser Text!');
        await page.keyboard.type("seta kisak_ui_archive 7");
        await page.keyboard.down("3");
        await page.keyboard.down("3");
        await page.keyboard.up("3");
        await page.keyboard.press("Backspace");
        await page.keyboard.press("Enter");
        await expect.poll(() => call(page, "_KisakWeb_TestConfigState", 0)).toBe(73);
        await page.keyboard.type("openmenu profile_create_popmenu");
        await page.keyboard.press("Enter");
        await page.keyboard.press("Backquote");
        await expect.poll(() => call(page, "_KisakWeb_TestMenuState",
            nameHash("profile_create_popmenu"))).toBe(7);
        const bounds = await canvas.boundingBox();
        await canvas.click({ position: { x: bounds.width * 0.50, y: bounds.height * 0.485 } });
        await page.keyboard.type("BrowserZ");
        await page.keyboard.press("Backspace");
        await page.keyboard.down("7");
        await page.keyboard.down("7");
        await page.keyboard.up("7");
        await page.keyboard.press("Backspace");
        await page.keyboard.press("Home");
        await page.keyboard.press("Delete");
        // Native UI starts in overstrike mode; explicitly choose insertion.
        await page.keyboard.press("Insert");
        await page.keyboard.type("B");
        await page.keyboard.press("End");
        await page.keyboard.type("3");
        await expect.poll(() => call(page, "_KisakWeb_TestUiTextSeen", nameHash("Browser73")))
            .toBe(1);
        await canvas.screenshot({ path: testInfo.outputPath("profile-field.png") });
        await page.keyboard.press("Backquote");
        await page.keyboard.type("ui_playerProfileNameNew");
        await page.keyboard.press("Enter");
        await expect.poll(() => page.evaluate(() => globalThis.__textLogs.join("\n")))
            .toContain('"ui_playerProfileNameNew" is: "Browser73');
    });

test("canonical recoverable frame errors return to UI and permit another map load", { tag: "@retail-recovery" },
    async ({ retailPage: page }, testInfo) => {
        test.setTimeout(420_000);
        const errors = [];
        page.on("pageerror", (error) => errors.push(String(error)));
        await page.addInitScript(() => {
            globalThis.__recoveryUnloads = 0;
            globalThis.__recoveryWorldFrames = 0;
            globalThis.__recoveryLog = [];
            addEventListener("kisakcod:log", ({ detail }) => {
                globalThis.__recoveryLog.push(detail.message);
            });
            addEventListener("kisakcod:renderer-memory", ({ detail }) => {
                if (detail.state === "world-unloaded") ++globalThis.__recoveryUnloads;
            });
            addEventListener("kisakcod:renderer-scene-frame", ({ detail }) => {
                if (detail.state === "drawn" && detail.geometrySubmitted)
                    ++globalThis.__recoveryWorldFrames;
            });
        });
        await page.goto("/");
        const chooser = page.waitForEvent("filechooser");
        await page.locator("#portable-install-button").click();
        await (await chooser).setFiles(retailRoot);
        await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.module?.filesystemState),
            { timeout: 300_000 }).toBe("mounted");
        const command = async (text) => {
            await page.locator("#engine-command-input").fill(text);
            await page.locator("#engine-command-form").evaluate((form) => form.requestSubmit());
            await expect(page.locator("#engine-command-status")).toHaveText(`Accepted: ${text}`);
        };
        const recover = async (fromMap = false) => {
            const before = await page.evaluate(() => globalThis.__recoveryUnloads);
            await call(page, "_KisakWeb_TestDeferredFrameError");
            if (fromMap)
                await expect.poll(() => page.evaluate(() => globalThis.__recoveryUnloads),
                    { timeout: 60_000 }).toBeGreaterThan(before);
            await expect.poll(() => call(page, "_KisakWeb_TestUiState", 0)).toBe(1);
            await expect.poll(() => call(page, "_KisakWeb_TestUiTextSeen",
                nameHash("Diagnostic deferred frame error"))).toBe(1);
            expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("running");
        };
        const loadMap = async () => {
            const before = await page.evaluate(() => globalThis.__recoveryWorldFrames);
            await command("map killhouse");
            await expect.poll(() => call(page, "_KisakWeb_TestGameplayState", 0),
                { timeout: 180_000 }).toBeGreaterThanOrEqual(0);
            await expect.poll(() => page.evaluate(() => globalThis.__recoveryWorldFrames),
                { timeout: 60_000 }).toBeGreaterThan(before);
        };
        await recover();
        await loadMap();
        await recover(true);
        await loadMap();
        expect(await page.evaluate(() => globalThis.__recoveryLog.join("\n")))
            .not.toContain("Info string length exceeded");
        expect(errors).toEqual([]);
        await page.locator("#game-canvas").screenshot({ path: testInfo.outputPath("recovered-map.png") });
    });

test("canonical retail main menu starts without a map", { tag: "@retail-ui" },
    async ({ retailPage: page }, testInfo) => {
        test.setTimeout(600_000);
        await page.addInitScript(() => {
            globalThis.__uiLogs = [];
            globalThis.__uiLifecycle = [];
            globalThis.__uiFrames = [];
            globalThis.addEventListener("kisakcod:log", (event) =>
                globalThis.__uiLogs.push(structuredClone(event.detail)));
            globalThis.addEventListener("kisakcod:engine-lifecycle", (event) =>
                globalThis.__uiLifecycle.push(structuredClone(event.detail)));
            globalThis.addEventListener("kisakcod:renderer-scene-frame", (event) =>
                globalThis.__uiFrames.push(structuredClone(event.detail)));
        });
        await page.goto("/");
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__?.state)).toBe("running");

        const chooserPromise = page.waitForEvent("filechooser");
        await page.locator("#portable-install-button").click();
        const chooser = await chooserPromise;
        await chooser.setFiles(retailRoot);
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__?.assets?.state), {
            timeout: 300_000,
        }).toBe("ready");
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__?.module?.filesystemState), {
            timeout: 300_000,
        }).toBe("mounted");

        await expect.poll(() => call(page, "_KisakWeb_TestUiState", 0))
            .toBe(1);
        const menuCount = await call(page, "_KisakWeb_TestUiState", 1);
        expect(menuCount).toBeGreaterThan(0);
        await expect.poll(() => call(page, "_KisakWeb_TestMenuState",
            nameHash("main"))).toBe(7);
        await expect.poll(() => page.evaluate(() =>
            globalThis.__uiLogs.some(({ text }) =>
                text.includes("first canonical 2D scene")))).toBe(true);
        await expect.poll(() => call(page, "_KisakWeb_TestUiDrawCount"))
            .toBeGreaterThan(0);
        await page.waitForTimeout(1_000);
        expect(await call(page, "_KisakWeb_TestUiDrawCount"))
            .toBeGreaterThan(0);
        await page.locator("#game-canvas").screenshot({
            path: testInfo.outputPath("main-menu.png"),
        });

        const submitCommand = async (command) => {
            await page.locator("#engine-command-input").fill(command);
            await page.locator("#engine-command-form").evaluate(
                (form) => form.requestSubmit());
            await expect(page.locator("#engine-command-status"))
                .toHaveText(`Accepted: ${command}`);
        };
        const menuLogCursor = await page.evaluate(() =>
            globalThis.__uiLogs.length);
        for (const menuName of [
            "main_options", "player_profile", "save_load_menu",
        ]) {
            await submitCommand(`openmenu ${menuName}`);
            try {
                await expect.poll(() => call(page, "_KisakWeb_TestMenuState",
                    nameHash(menuName))).toBe(7);
            } catch (error) {
                console.log(await page.evaluate(() => ({
                    lifecycle: globalThis.__uiLifecycle.slice(-10),
                    logs: globalThis.__uiLogs.slice(-20),
                })));
                throw error;
            }
            await submitCommand(`closemenu ${menuName}`);
            if (menuName === "player_profile") {
                await submitCommand("closemenu profile_create_popmenu");
            }
        }
        const missingMenuDvars = await page.evaluate((cursor) =>
            globalThis.__uiLogs.slice(cursor).filter(({ text }) =>
                text.includes("doesn't exist") ||
                text.includes("cannot find dvar")), menuLogCursor);
        expect(missingMenuDvars.every(({ text }) =>
            text.includes("cannot find dvar ui_sp_unlock"))).toBe(true);
        expect(await call(page, "_KisakWeb_TestConfigState", 3)).toBe(0xFF);
        await submitCommand("seta kisak_ui_archive 37");
        await expect.poll(() => call(page, "_KisakWeb_TestConfigState", 0))
            .toBe(37);
        await submitCommand("toggle kisak_ui_archive 37 41");
        await expect.poll(() => call(page, "_KisakWeb_TestConfigState", 0))
            .toBe(41);
        await submitCommand("reset kisak_ui_archive");
        await expect.poll(() => call(page, "_KisakWeb_TestConfigState", 0))
            .toBe(37);
        expect(await call(page, "_KisakWeb_TestConfigState", 4))
            .toBe((0xAF << 8) | 1);
        await submitCommand("bind F9 +scores");
        await expect.poll(() => call(page, "_KisakWeb_TestConfigState", 2))
            .toBe(1);
        await expect.poll(() => call(page, "_KisakWeb_TestConfigState", 1))
            .toBeGreaterThan(0);
        const lifecycleCursor = await page.evaluate(() =>
            globalThis.__uiLifecycle.length);
        await submitCommand("map killhouse");
        await expect.poll(() => page.evaluate((cursor) =>
            globalThis.__uiLifecycle.slice(cursor).some(
                ({ stage }) => stage === "CG_Init complete"), lifecycleCursor), {
            timeout: 300_000,
        }).toBe(true);
        await expect.poll(() => page.evaluate(() =>
            globalThis.__uiFrames.some(({ worldName, geometrySubmitted }) =>
                geometrySubmitted === true &&
                worldName?.toLowerCase().includes("killhouse"))), {
            timeout: 300_000,
        }).toBe(true);
        for (const menuName of ["pausedmenu", "objectiveinfo"]) {
            expect(await call(page, "_KisakWeb_TestMenuState",
                nameHash(menuName))).toBeGreaterThan(0);
        }
        expect(await call(page, "_KisakWeb_TestObjectiveNotification", 4))
            .toBe(1);
        await expect.poll(() => call(page, "_KisakWeb_TestUiState", 8))
            .toBe(4);
        await expect.poll(() => call(page, "_KisakWeb_TestUiTextSeen",
            nameHash("Kisak web objective test"))).toBe(1);
        expect(await call(page, "_KisakWeb_TestObjectiveNotification", 6))
            .toBe(1);
        expect(await call(page, "_KisakWeb_TestObjectiveNotification", 3))
            .toBe(1);
        await expect.poll(() => call(page, "_KisakWeb_TestUiState", 8))
            .toBe(3);
        const canvas = page.locator("#game-canvas");
        // A persistent Chrome tab can retain emulated DOM focus after the
        // directory picker while its browser widget is inactive. Pointer lock
        // requires the real tab to be active, as it is during a player's click.
        await page.bringToFront();
        await canvas.click();
        await expect.poll(() => page.evaluate(() =>
            document.pointerLockElement?.id)).toBe("game-canvas");
        await call(page, "_KisakWeb_QueueKeyEvent", 0x1B, 1);
        await call(page, "_KisakWeb_QueueKeyEvent", 0x1B, 0);
        await expect.poll(() => call(page, "_KisakWeb_TestMenuState",
            nameHash("pausedmenu"))).toBe(7);
        expect(await call(page, "_KisakWeb_TestUiState", 5)).toBe(1);
        expect((await call(page, "_KisakWeb_TestUiState", 3)) & 16).toBe(16);
        await expect.poll(() => page.evaluate(() =>
            document.pointerLockElement === null)).toBe(true);
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__.input.absoluteMouse ||
            globalThis.__KISAKCOD_WEB__.input.cursorVisible)).toBe(true);
        await page.waitForTimeout(150);
        await call(page, "_KisakWeb_TestResumeGame");
        await expect.poll(() => call(page, "_KisakWeb_TestUiState", 5))
            .toBe(0);
        expect((await call(page, "_KisakWeb_TestUiState", 3)) & 16).toBe(0);

        await test.step("slow browser frames retain real-time gameplay", async () => {
            await expect.poll(() => call(page, "_KisakWeb_TestGameplayState", 24, 0))
                .toBeGreaterThan(0);
            const timing = await page.evaluate(async () => {
                const module = globalThis.__KISAKCOD_WEB__.module;
                const gameBefore = await module.call("_KisakWeb_TestGameplayState", 24, 0);
                const wallBefore = performance.now();
                // Artificial Worker stalls, not input or a changed game timescale.
                // This reproduces the former 100 ms/frame cap at low frame rates.
                for (let frame = 0; frame < 12; ++frame) {
                    if (await module.call("_KisakWeb_TestSlowNextCommand", 250) !== 1)
                        throw new Error("Could not schedule timing probe");
                    const command = new TextEncoder().encode("echo frame-timing-probe\0");
                    if (await module.callProbe("_KisakWeb_SubmitCanonicalCommand",
                        [command], [{ kind: "pointer", index: 0 }]) !== 1)
                        throw new Error("Could not submit timing probe");
                    await new Promise((resolve) => setTimeout(resolve, 300));
                }
                const gameAfter = await module.call("_KisakWeb_TestGameplayState", 24, 0);
                return { gameMs: gameAfter - gameBefore, wallMs: performance.now() - wallBefore };
            });
            console.log("real-time frame probe", timing);
            expect(timing.wallMs).toBeGreaterThan(3_000);
            expect(timing.gameMs / timing.wallMs).toBeGreaterThan(0.85);
            expect(timing.gameMs / timing.wallMs).toBeLessThan(1.15);
        });

        await test.step("canonical EQ controls reach playing owned sounds and bypass", async () => {
            const logCursor = await page.evaluate(() => globalThis.__uiLogs.length);
            await submitCommand("snd_setEq");
            await expect.poll(() => page.evaluate((cursor) =>
                globalThis.__uiLogs.slice(cursor).some(({ text }) =>
                    text.includes("Current EQ Settings")), logCursor)).toBe(true);
            const channels = await page.evaluate((cursor) =>
                globalThis.__uiLogs.slice(cursor).flatMap(({ text }) =>
                    [...text.matchAll(/^\+ ([a-zA-Z0-9_]+)\s*$/gm)].map((match) => match[1])), logCursor);
            expect(channels.length).toBeGreaterThan(0);
            await submitCommand("snd_enableEq 1");
            for (const channel of channels)
                await submitCommand(`snd_setEq ${channel} 1 2 bell -6 1000 0.75`);
            await expect.poll(() => page.evaluate(() => {
                const driver = globalThis.__KISAKCOD_WEB__.module.audioDriver;
                return [...driver.sources.values()].some((source) =>
                    source.state === "playing" && source.eqBands[25] === 1 &&
                    source.eqBands[27] === -6 && source.eqNodes.length > 0);
            }), { timeout: 30_000 }).toBe(true);
            const filtered = await page.evaluate(() => {
                const driver = globalThis.__KISAKCOD_WEB__.module.audioDriver;
                const source = [...driver.sources.values()].find((value) =>
                    value.state === "playing" && value.eqNodes.length && value.eqBands[27] === -6);
                const magnitude = new Float32Array(1), phase = new Float32Array(1);
                source.eqNodes.at(-1).getFrequencyResponse(new Float32Array([1000]), magnitude, phase);
                return { alias: source.aliasName, contextState: driver.context.state,
                    magnitude: magnitude[0], filters: source.eqNodes.length };
            });
            console.log("owned EQ device probe", filtered);
            expect(filtered.alias.length).toBeGreaterThan(0);
            expect(filtered.contextState).toBe("running");
            expect(filtered.magnitude).toBeCloseTo(10 ** (-6 / 20), 5);
            await submitCommand("snd_enableEq 0");
            await expect.poll(() => page.evaluate(() =>
                [...globalThis.__KISAKCOD_WEB__.module.audioDriver.sources.values()]
                    .every((source) => source.eqNodes.length === 0))).toBe(true);
            const errors = await page.evaluate((cursor) =>
                globalThis.__uiLogs.slice(cursor).filter(({ text }) =>
                    text.includes("Rejected Web Audio EQ")), logCursor);
            expect(errors).toEqual([]);
        });

        await page.reload();
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__?.module?.filesystemState), {
            timeout: 300_000,
        }).toBe("mounted");
        await expect.poll(() => call(page, "_KisakWeb_TestConfigState", 0))
            .toBe(37);
        expect(await call(page, "_KisakWeb_TestConfigState", 2)).toBe(1);

        await submitCommand("openmenu player_profile");
        await expect.poll(() => call(page, "_KisakWeb_TestMenuState",
            nameHash("player_profile"))).toBe(7);
        await submitCommand("closemenu profile_create_popmenu");
        expect(await call(page, "_KisakWeb_TestProfileState", 1)).toBe(1);
        expect(await call(page, "_KisakWeb_TestProfileState", 2)).toBe(1);
        expect((await call(page, "_KisakWeb_TestProfileState", 0)) & 7)
            .toBe(7);

        expect(await call(page, "_KisakWeb_TestProfileState", 3)).toBe(1);
        await submitCommand("seta kisak_profile_value 101");
        await expect.poll(() => call(page, "_KisakWeb_TestProfileState", 7))
            .toBe(101);
        expect(await call(page, "_KisakWeb_TestProfileState", 4)).toBe(1);
        await submitCommand("seta kisak_profile_value 202");
        await expect.poll(() => call(page, "_KisakWeb_TestProfileState", 7))
            .toBe(202);
        expect(await call(page, "_KisakWeb_TestProfileState", 3)).toBe(1);
        expect(await call(page, "_KisakWeb_TestProfileState", 9)).toBe(101);
        expect(await call(page, "_KisakWeb_TestProfileState", 4)).toBe(1);
        expect(await call(page, "_KisakWeb_TestProfileState", 9)).toBe(202);
        expect(await call(page, "_KisakWeb_TestProfileState", 8)).toBe(2);
        await page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__.module.checkpoint());

        await page.reload();
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__?.module?.filesystemState), {
            timeout: 300_000,
        }).toBe("mounted");
        const restartedProfiles = await call(
            page, "_KisakWeb_TestProfileState", 0);
        expect(restartedProfiles & 7).toBe(7);
        expect(await call(page, "_KisakWeb_TestProfileState", 6)).toBe(2);
        expect(await call(page, "_KisakWeb_TestProfileState", 9)).toBe(202);
        expect(await call(page, "_KisakWeb_TestProfileState", 5)).toBe(1);
        expect((await call(page, "_KisakWeb_TestProfileState", 0)) & 7)
            .toBe(5);
        expect(await call(page, "_KisakWeb_TestProfileState", 6)).toBe(2);

        const gameplayState = (field, argument = 0) =>
            call(page, "_KisakWeb_TestGameplayState", field, argument);
        const gameplayFloat = (field, component) =>
            call(page, "_KisakWeb_TestGameplayFloat", field, component);
        const saveSnapshot = async () => {
            const weapon = await gameplayState(36);
            return {
                health: await gameplayState(17),
                weapon,
                ammo: await gameplayState(37, weapon),
                objectiveHash: await gameplayState(19),
                activeObjectives: await gameplayState(33),
                doneObjectives: await gameplayState(34),
                origin: await Promise.all([0, 1, 2].map((component) =>
                    gameplayFloat(0, component))),
            };
        };
        const expectRestoredSnapshot = async (saved) => {
            await expect.poll(() => gameplayState(17), { timeout: 120_000 })
                .toBe(saved.health);
            await expect.poll(() => gameplayState(36)).toBe(saved.weapon);
            await expect.poll(() => gameplayState(37, saved.weapon))
                .toBe(saved.ammo);
            await expect.poll(() => gameplayState(19))
                .toBe(saved.objectiveHash);
            expect(await gameplayState(33)).toBe(saved.activeObjectives);
            expect(await gameplayState(34)).toBe(saved.doneObjectives);
            const restoredOrigin = await Promise.all([0, 1, 2].map(
                (component) => gameplayFloat(0, component)));
            expect(Math.hypot(...restoredOrigin.map((value, index) =>
                value - saved.origin[index]))).toBeLessThan(8);
        };

        let saveLifecycleCursor = await page.evaluate(() =>
            globalThis.__uiLifecycle.length);
        await submitCommand("devmap airplane");
        await expect.poll(() => page.evaluate((cursor) =>
            globalThis.__uiLifecycle.slice(cursor).some(
                ({ stage }) => stage === "CG_Init complete"),
        saveLifecycleCursor), { timeout: 300_000 }).toBe(true);
        await expect.poll(() => gameplayState(17), { timeout: 120_000 })
            .toBeGreaterThan(0);
        // The native start-level save is committed before the first draw.
        // Its deferred capture must eventually publish without a devsave.
        await expect.poll(() => call(page, "_KisakWeb_TestSaveState", 14),
            { timeout: 30_000 }).toBeGreaterThan(1000);
        await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.checkpoint());
        const startImage = await page.evaluate(async () => {
            let directory = await navigator.storage.getDirectory();
            for (const name of ["kisakcod-web", "home", "players", "profiles", "kisak_web_test_b", "save"])
                directory = await directory.getDirectoryHandle(name);
            const file = await (await directory.getFileHandle("airplane.jpg")).getFile();
            return Array.from(new Uint8Array(await file.arrayBuffer()));
        });
        await writeFile(testInfo.outputPath("start-level-thumbnail.jpg"), Buffer.from(startImage));
        await submitCommand("give all");
        await expect.poll(() => gameplayState(7), { timeout: 30_000 })
            .toBeGreaterThan(0);
        await expect.poll(() => gameplayState(36), { timeout: 30_000 })
            .toBeGreaterThan(0);
        const saveLogCursor = await page.evaluate(() =>
            globalThis.__uiLogs.length);
        await submitCommand("devsave kisak_web_ui_test");
        await expect.poll(() => page.evaluate((cursor) =>
            globalThis.__uiLogs.slice(cursor).some(({ text }) =>
                text.includes("G_WriteGame 'kisak_web_ui_test'")),
        saveLogCursor), { timeout: 60_000 }).toBe(true);
        await expect.poll(() => gameplayState(32), { timeout: 60_000 })
            .toBe(0);
        await expect.poll(() => gameplayState(28), { timeout: 60_000 })
            .toBe(1);
        await expect.poll(() => call(page, "_KisakWeb_TestSaveState", 6))
            .toBeGreaterThan(0);
        const saved = await saveSnapshot();
        expect(saved.weapon).toBeGreaterThan(0);
        expect(saved.ammo).toBeGreaterThanOrEqual(0);

        expect(await call(page, "_KisakWeb_TestSaveState", 0))
            .toBeGreaterThanOrEqual(1);
        expect(await call(page, "_KisakWeb_TestSaveState", 1))
            .toBeGreaterThan(0);
        expect(await call(page, "_KisakWeb_TestSaveState", 2)).toBe(1);
        expect(await call(page, "_KisakWeb_TestSaveState", 7)).toBe(1);
        expect(await call(page, "_KisakWeb_TestSaveState", 8)).toBe(1);
        expect(await call(page, "_KisakWeb_TestSaveState", 9)).toBe(15);
        expect(await call(page, "_KisakWeb_TestSaveState", 10)).toBeGreaterThan(1000);
        await expect.poll(() => call(page, "_KisakWeb_TestSaveState", 11)).toBe(1);
        await page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__.module.checkpoint());

        await page.reload();
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__?.module?.filesystemState), {
            timeout: 300_000,
        }).toBe("mounted");
        expect(await call(page, "_KisakWeb_TestProfileState", 6)).toBe(2);
        expect(await call(page, "_KisakWeb_TestSaveState", 0))
            .toBeGreaterThanOrEqual(1);
        expect(await call(page, "_KisakWeb_TestSaveState", 8)).toBe(1);
        expect(await call(page, "_KisakWeb_TestSaveState", 9)).toBe(15);
        expect(await call(page, "_KisakWeb_TestSaveState", 10)).toBeGreaterThan(1000);
        await expect.poll(() => call(page, "_KisakWeb_TestSaveState", 11)).toBe(1);
        expect(await call(page, "_KisakWeb_TestProfileState", 1)).toBe(1);
        expect(await call(page, "_KisakWeb_TestProfileState", 3)).toBe(1);
        expect(await call(page, "_KisakWeb_TestSaveState", 0)).toBe(0);
        expect(await call(page, "_KisakWeb_TestSaveState", 5)).toBe(1);
        expect(await call(page, "_KisakWeb_TestProfileState", 10)).toBe(1);
        expect(await call(page, "_KisakWeb_TestProfileState", 6)).toBe(3);
        expect(await call(page, "_KisakWeb_TestSaveState", 1)).toBe(0);
        expect(await call(page, "_KisakWeb_TestSaveState", 6)).toBe(0);
        expect(await call(page, "_KisakWeb_TestProfileState", 4)).toBe(1);
        expect(await call(page, "_KisakWeb_TestSaveState", 1))
            .toBeGreaterThan(0);
        expect(await call(page, "_KisakWeb_TestSaveState", 6))
            .toBeGreaterThan(0);
        await submitCommand("closemenu player_profile");
        await submitCommand("openmenu save_load_menu");
        await expect.poll(() => call(page, "_KisakWeb_TestMenuState",
            nameHash("save_load_menu"))).toBe(7);
        expect(await call(page, "_KisakWeb_TestSaveState", 2)).toBe(1);
        await expect.poll(() => call(page, "_KisakWeb_TestSaveState", 12)).toBe(1);
        expect(await call(page, "_KisakWeb_TestSaveState", 13)).toBe(3);
        await page.locator("#game-canvas").screenshot({
            path: testInfo.outputPath("save-thumbnail-menu.png"),
        });
        saveLifecycleCursor = await page.evaluate(() =>
            globalThis.__uiLifecycle.length);
        expect(await call(page, "_KisakWeb_TestSaveState", 3)).toBe(1);
        await expect.poll(() => page.evaluate((cursor) =>
            globalThis.__uiLifecycle.slice(cursor).some(
                ({ stage }) => stage === "CG_Init complete"),
        saveLifecycleCursor), { timeout: 300_000 }).toBe(true);
        await expectRestoredSnapshot(saved);
        await page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__.module.checkpoint());

        await page.reload();
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__?.module?.filesystemState), {
            timeout: 300_000,
        }).toBe("mounted");
        expect(await call(page, "_KisakWeb_TestProfileState", 6)).toBe(2);
        expect(await call(page, "_KisakWeb_TestSaveState", 1))
            .toBeGreaterThan(0);
        expect(await call(page, "_KisakWeb_TestSaveState", 8)).toBe(1);
        saveLifecycleCursor = await page.evaluate(() =>
            globalThis.__uiLifecycle.length);
        await submitCommand("loadgame_continue");
        await expect.poll(() => page.evaluate((cursor) =>
            globalThis.__uiLifecycle.slice(cursor).some(
                ({ stage }) => stage === "CG_Init complete"),
        saveLifecycleCursor), { timeout: 300_000 }).toBe(true);
        await expectRestoredSnapshot(saved);

        // Use the shipped Save and Quit menus. Their successful-save callback
        // reaches ERR_DISCONNECT, which must unwind the live browser frame,
        // retire the world, and reload UI without terminating the Worker.
        await canvas.focus();
        await page.keyboard.press("Escape");
        await expect.poll(() => call(page, "_KisakWeb_TestMenuState",
            nameHash("pausedmenu"))).toBe(7);
        for (let index = 0; index < 4; ++index)
            await page.keyboard.press("ArrowDown");
        await page.keyboard.press("Enter");
        await expect.poll(() => call(page, "_KisakWeb_TestMenuState",
            nameHash("savegame_warning"))).toBe(7);
        await page.keyboard.press("ArrowDown");
        await page.keyboard.press("Enter");
        await expect.poll(() => call(page, "_KisakWeb_TestMenuState",
            nameHash("main")), { timeout: 60_000 }).toBe(7);
        expect(await call(page, "_KisakWeb_TestUiState", 5)).toBe(0);
        await expect(page.locator("#boot-log")).toContainText("Disconnecting: EXE_DISCONNECTED");
        await canvas.screenshot({ path: testInfo.outputPath("save-quit-main-menu.png") });
        saveLifecycleCursor = await page.evaluate(() => globalThis.__uiLifecycle.length);
        await submitCommand("loadgame_continue");
        await expect.poll(() => page.evaluate((cursor) =>
            globalThis.__uiLifecycle.slice(cursor).some(
                ({ stage }) => stage === "CG_Init complete"),
        saveLifecycleCursor), { timeout: 300_000 }).toBe(true);
        await expectRestoredSnapshot(saved);

        expect(await call(page, "_KisakWeb_TestSaveState", 4)).toBe(1);
        expect(await call(page, "_KisakWeb_TestSaveState", 1)).toBe(0);
        expect(await call(page, "_KisakWeb_TestSaveState", 6)).toBe(0);
        expect(await call(page, "_KisakWeb_TestProfileState", 5)).toBe(1);
        await submitCommand("quit");
        await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "stopped");
        await expect(page.locator("#quit-dialog")).toContainText("Game closed");
        await page.getByRole("button", { name: "Start game", exact: true }).click();
        await expect.poll(() => page.evaluate(() =>
            globalThis.__KISAKCOD_WEB__?.module?.filesystemState), {
            timeout: 300_000,
        }).toBe("mounted");
        await expect.poll(() => call(page, "_KisakWeb_TestMenuState",
            nameHash("main"))).toBe(7);
    });

test("owned paused scene responds to specular and normal controls", {
    tag: "@retail-graphics",
}, async ({ retailPage: page }, testInfo) => {
    test.skip(process.env.KISAK_WEB_PRODUCT_TEST === "1", "Uses diagnostic pause-state observation.");
    test.setTimeout(480_000);
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const chooser = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    await (await chooser).setFiles(retailRoot);
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.assets.state),
        { timeout: 240_000 }).toBe("ready");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.filesystemState),
        { timeout: 240_000 }).toBe("mounted");
    await page.locator("#game-canvas").click({ position: { x: 5, y: 5 } });
    await expect(page.locator("#boot-log")).toContainText("Browser mouse button reached canonical input");
    const command = text => page.evaluate(text =>
        globalThis.__KISAKCOD_WEB__.submitCanonicalCommand(text), text);
    await command("map killhouse");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.rendererSceneFrame?.worldName),
        { timeout: 90_000 }).toContain("killhouse");
    // CL_Pause_f deliberately refuses a fullscreen movie. Let the real movie
    // finish on the unlocked device clock before requesting canonical pause.
    await expect.poll(() => call(page, "_KisakWeb_TestCinematicState", 3),
        { timeout: 60_000 }).toBe(2);
    await page.waitForTimeout(6_000); // Authored opening fade, no movement or progression input.
    await command("set cg_cinematicFullscreen 0; set cl_paused_simple 1");
    await command("pause");
    await expect.poll(() => call(page, "_KisakWeb_TestUiState", 5)).toBe(1);
    const capture = async (name, baseline = false) => {
        const png = await page.locator("#game-canvas").screenshot({ path: testInfo.outputPath(`${name}.png`) });
        return page.evaluate(async ({ encoded, baseline }) => {
            const bitmap = await createImageBitmap(new Blob([
                Uint8Array.from(atob(encoded), c => c.charCodeAt(0)),
            ], { type: "image/png" }));
            const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
            const context = canvas.getContext("2d");
            context.drawImage(bitmap, 0, 0);
            const { data } = context.getImageData(0, 0, bitmap.width, bitmap.height);
            bitmap.close();
            if (baseline) globalThis.__graphicsBaseline = data;
            const original = globalThis.__graphicsBaseline;
            let squared = 0, changed = 0;
            for (let i = 0; i < data.length; i += 4) {
                let pixel = false;
                for (let channel = 0; channel < 3; ++channel) {
                    const delta = data[i + channel] - original[i + channel];
                    squared += delta * delta;
                    pixel ||= Math.abs(delta) > 1;
                }
                if (pixel) ++changed;
            }
            return { rms: Math.sqrt(squared / (data.length / 4 * 3)), changed };
        }, { encoded: png.toString("base64"), baseline });
    };
    await command("r_specular 1; r_normal 1");
    await page.waitForTimeout(500);
    await capture("graphics-baseline", true);
    await page.waitForTimeout(500);
    const noise = await capture("graphics-repeat");
    const evidence = { noise, controls: {} };
    for (const setting of ["r_specular", "r_normal"]) {
        await command(`${setting} 0`);
        await page.waitForTimeout(500);
        const disabled = await capture(`${setting}-off`);
        await command(`${setting} 1`);
        await page.waitForTimeout(500);
        const restored = await capture(`${setting}-restored`);
        evidence.controls[setting] = { disabled, restored };
        await writeFile(testInfo.outputPath("graphics-evidence.json"), JSON.stringify(evidence, null, 2));
        expect(disabled.rms, `${setting} must exceed measured unchanged-frame noise`)
            .toBeGreaterThan(Math.max(0.01, noise.rms * 4));
        expect(restored.rms).toBeLessThanOrEqual(noise.rms + 0.01);
    }
    await command("r_specular 0; r_normal 0");
    await page.waitForTimeout(500);
    const beforeRecovery = await capture("graphics-before-recovery");
    await call(page, "_KisakWeb_TestLoseWebGLContext");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("renderer-lost");
    await call(page, "_KisakWeb_TestRestoreWebGLContext");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state),
        { timeout: 60_000 }).toBe("running");
    await page.waitForTimeout(500);
    const recovered = await capture("graphics-recovered");
    expect(Math.abs(recovered.rms - beforeRecovery.rms)).toBeLessThanOrEqual(noise.rms + 0.01);
    await command("r_specular 1; r_normal 1");
    await page.waitForTimeout(500);
    const restored = await capture("graphics-restored-after-recovery");
    expect(restored.rms).toBeLessThanOrEqual(noise.rms + 0.01);
    evidence.recovery = { beforeRecovery, recovered, restored };
    await writeFile(testInfo.outputPath("graphics-evidence.json"), JSON.stringify(evidence, null, 2));
});

test("owned retail saved-screen materials blend from the scene composite", async ({ retailPage: page }) => {
    test.setTimeout(600_000);
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const chooser = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    await (await chooser).setFiles(retailRoot);
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.assets.state),
        { timeout: 240_000 }).toBe("ready");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.filesystemState),
        { timeout: 240_000 }).toBe("mounted");
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.submitCanonicalCommand("map killhouse"));
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.rendererSceneFrame?.worldName),
        { timeout: 60_000 }).toContain("killhouse");
    // Endpoint colors are invariant under final display gamma. The first
    // command captures before the blue overlay; the second uses the loaded
    // retail material/state and samples the retained scene-composite pixels.
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestSavedScreen", 0, 1000, 200, 0, 1, 0))).toBe(0xff0000);
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestSavedScreen", 1, 1010, 200, 0, 1, 0))).toBe(0x0000ff);
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.dispose());
});

test("owned retail transient lights reach native material passes and obey scene limits", async ({ retailPage: page }, testInfo) => {
    test.setTimeout(600_000);
    await page.goto("/");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
    const chooser = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    await (await chooser).setFiles(retailRoot);
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.assets.state),
        { timeout: 240_000 }).toBe("ready");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.filesystemState),
        { timeout: 240_000 }).toBe("mounted");
    const command = text => page.evaluate(text =>
        globalThis.__KISAKCOD_WEB__.submitCanonicalCommand(text), text);
    await command("map killhouse");
    await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.rendererSceneFrame?.worldName),
        { timeout: 60_000 }).toContain("killhouse");
    // Let the authored initial fade finish, then use canonical pause to hold
    // the pose/fog constant for private visual comparisons.
    await page.waitForTimeout(6_000);
    await command("set cg_cinematicFullscreen 0; set cl_paused_simple 1");
    await command("pause");
    await expect.poll(() => call(page, "_KisakWeb_TestUiState", 5)).toBe(1);
    const capture = async name => {
        const png = await page.locator("#game-canvas").screenshot({ path: testInfo.outputPath(`lights-${name}.png`) });
        return page.evaluate(async encoded => {
            const bitmap = await createImageBitmap(new Blob([Uint8Array.from(atob(encoded), c => c.charCodeAt(0))], { type: "image/png" }));
            const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
            const context = canvas.getContext("2d");
            context.drawImage(bitmap, 0, 0);
            const { data } = context.getImageData(0, 0, bitmap.width, bitmap.height);
            const means = [0, 0, 0];
            for (let i = 0; i < data.length; i += 4)
                for (let channel = 0; channel < 3; ++channel) means[channel] += data[i + channel];
            bitmap.close();
            return means.map(value => value / (data.length / 4));
        }, png.toString("base64"));
    };
    // The pause command and authored objective overlay can reach adjacent
    // renderer frames. Establish the comparison only after two paused captures
    // agree, so a UI transition cannot be mistaken for transient-light output.
    await page.waitForTimeout(500);
    let baseline = await capture("baseline-first");
    await page.waitForTimeout(500);
    const stableBaseline = await capture("baseline");
    for (let channel = 0; channel < 3; ++channel)
        expect(Math.abs(stableBaseline[channel] - baseline[channel]),
            "paused light baseline must be stable before injection").toBeLessThan(0.1);
    baseline = stableBaseline;
    const state = () => call(page, "_KisakWeb_TestTransientLightDraws");
    const shadowState = () => call(page, "_KisakWeb_TestTransientSpotShadowState");
    // Synthetic renderer input through the same R_Add* calls used by canonical
    // FX. This validates rendering, not authored FX timing or mission progress.
    for (const [mode, type, count, name] of [[1, 3, 1, "omni"], [2, 2, 1, "spot"], [3, 3, 4, "limit"]]) {
        await call(page, "_KisakWeb_TestTransientLights", mode);
        await expect.poll(async () => (await state()) >>> 16).toBe((type << 8) | count);
        await expect.poll(async () => (await state()) & 65535).toBeGreaterThan(0);
        if (mode === 3) expect(await call(page, "_KisakWeb_TestTransientLights", -1)).toBe(32);
        const lit = await capture(name);
        expect(lit[0] - baseline[0], `${name} must visibly illuminate the owned scene`).toBeGreaterThan(0.05);
        if (mode === 2) {
            await expect.poll(async () => ((await shadowState()) >>> 8) & 255).toBe(1);
            await command("set r_spotLightShadows 0");
            await expect.poll(async () => ((await shadowState()) >>> 8) & 255).toBe(0);
            const unshadowed = await capture("spot-unshadowed");
            expect(unshadowed[0] - lit[0],
                "the transient shadow map must darken at least part of the spot-lit scene").toBeGreaterThan(0.01);
            await command("set r_spotLightShadows 1");
            await expect.poll(async () => ((await shadowState()) >>> 8) & 255).toBe(1);
            // Prove clearing against the unchanged pose before recovery's
            // explicit frame pump advances canonical animation and HUD time.
            await call(page, "_KisakWeb_TestTransientLights", 0);
            await expect.poll(state).toBe(0);
            const clearedBeforeRecovery = await capture("cleared-before-recovery");
            for (let channel = 0; channel < 3; ++channel)
                expect(Math.abs(clearedBeforeRecovery[channel] - baseline[channel])).toBeLessThan(0.1);
            await call(page, "_KisakWeb_TestTransientLights", 2);
            await expect.poll(async () => ((await shadowState()) >>> 8) & 255).toBe(1);
            expect(await call(page, "_KisakWeb_TestLoseWebGLContext")).toBe(1);
            await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state)).toBe("renderer-lost");
            expect(await call(page, "_KisakWeb_TestRestoreWebGLContext")).toBe(1);
            await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.state), { timeout: 60_000 }).toBe("running");
            await expect.poll(async () => (await state()) & 65535,
                { timeout: 60_000 }).toBeGreaterThan(0);
            await expect.poll(async () => ((await shadowState()) >>> 8) & 255,
                { timeout: 60_000 }).toBe(1);
            // A paused client has no new cgame view to submit after context
            // recreation. Pump one frame, then freeze the recovered image.
            await command("pause");
            await expect.poll(() => call(page, "_KisakWeb_TestUiState", 5)).toBe(0);
            await page.waitForTimeout(500);
            await command("pause");
            await expect.poll(() => call(page, "_KisakWeb_TestUiState", 5)).toBe(1);
            const recovered = await capture("recovered");
            expect(recovered[0] - baseline[0]).toBeGreaterThan(0.05);
            await call(page, "_KisakWeb_TestTransientLights", 0);
            await expect.poll(state).toBe(0);
            baseline = await capture("recovered-baseline");
            expect(recovered[0] - baseline[0]).toBeGreaterThan(0.05);
        }
    }
    await command("set r_dlightLimit 0");
    await expect.poll(state).toBe(0);
    await command("set r_dlightLimit 4");
    await expect.poll(async () => ((await state()) >>> 16) & 255).toBe(4);
    await call(page, "_KisakWeb_TestTransientLights", 0);
    await expect.poll(state).toBe(0);
    const cleared = await capture("cleared");
    for (let channel = 0; channel < 3; ++channel)
        expect(Math.abs(cleared[channel] - baseline[channel])).toBeLessThan(0.1);
    // Exercise the retained native attenuation image, not just synthetic white.
    const attenuationPixel = await call(page, "_KisakWeb_TestDynamicLightPixel", 7, 255, 1, 0);
    expect(attenuationPixel >>> 24).toBe(0);
    expect(attenuationPixel & 255).toBeGreaterThan(0);
    // The authored paused camera need not contain a DynEntity. Use an
    // isolated renderer view of a linked owned model for positive admission.
    try {
        expect(await call(page, "_KisakWeb_TestDynEntityCamera", 1)).toBeGreaterThan(0);
        await expect.poll(() => call(page, "_KisakWeb_TestDynEntityCamera", -1)).toBe(1);
        await expect.poll(() => page.locator("#boot-log").innerText()).toMatch(
            /Canonical DynEntity model scene: [1-9]\d* models/);
    } finally {
        await call(page, "_KisakWeb_TestDynEntityCamera", 0);
    }
    const rendererLog = await page.locator("#boot-log").innerText();
    await writeFile(testInfo.outputPath("renderer-log.txt"), rendererLog);
    expect(rendererLog).toMatch(/Canonical portal scene admission: valid=1 DObj linked=[1-9]\d* admitted=\d+ brush linked=\d+ admitted=\d+/);
    expect(rendererLog).toMatch(/Canonical DObj post-pose admission: tested=[1-9]\d* plane-rejected=\d+ cell-rejected=\d+/);
    expect(rendererLog).toMatch(/Canonical DynEntity portal admission: models=\d+ admitted=\d+ brushes=\d+ admitted=\d+/);
    expect(rendererLog).toMatch(/Canonical DynEntity model scene: [1-9]\d* models/);
    await testInfo.attach("renderer-log", { body: rendererLog, contentType: "text/plain" });
    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.dispose());
});
