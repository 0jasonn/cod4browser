import { chromium, expect, test as base } from "@playwright/test";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";

const retailRoot = process.env.KISAK_COD4_RETAIL_ROOT;
const test = base.extend({
    retailPage: async ({}, use, testInfo) => {
        const profile = await mkdtemp(join(tmpdir(), "kisakcod-cinematic-"));
        const context = await chromium.launchPersistentContext(profile, {
            baseURL: testInfo.project.use.baseURL,
            headless: testInfo.project.use.headless ?? true,
            viewport: testInfo.project.use.viewport,
            ...(process.env.KISAK_BROWSER_CHANNEL ? { channel: process.env.KISAK_BROWSER_CHANNEL } : {}),
        });
        try { await use(context.pages()[0] ?? await context.newPage()); }
        finally {
            try { await context.close(); }
            finally { await rm(profile, { recursive: true, force: true, maxRetries: 5 }); }
        }
    },
});
test.skip(!retailRoot, "RETAIL_ROOT_MISSING: select a legally owned COD4 installation");

async function call(page, operation)
{
    return page.evaluate((operation) => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestCinematicState", operation), operation);
}

// No movie fixture or generated story state. The command is Kisak's existing
// cinematic command; the only diagnostic mutations exercise its pause API.
test("owned Killhouse movie renders, pauses, completes and accepts native skip", { tag: "@retail-cinematic" },
    async ({ retailPage: page }, testInfo) => {
        test.skip(process.env.KISAK_WEB_PRODUCT_TEST === "1", "Diagnostic pause/state controls are excluded from production.");
        test.setTimeout(480_000);
        const failures = [];
        page.on("pageerror", (error) => failures.push(String(error)));
        await page.addInitScript(() => {
            globalThis.__movieEvents = [];
            globalThis.addEventListener("kisakcod:cinematic", ({ detail }) =>
                globalThis.__movieEvents.push({ ...detail, wallTime: performance.now() }));
        });
        await page.goto("/");
        await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");
        const chooser = page.waitForEvent("filechooser");
        await page.locator("#portable-install-button").click();
        await (await chooser).setFiles(retailRoot);
        await expect.poll(() => page.evaluate(() => {
            const assets = globalThis.__KISAKCOD_WEB__?.assets;
            if (assets?.state === "failed") throw new Error(assets.message);
            return assets?.state;
        }), { timeout: 300_000 }).toBe("ready");
        await expect.poll(() => page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.filesystemState),
            { timeout: 300_000 }).toBe("mounted");
        expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.assets.manifest.files
            .some(({ path }) => path === "main/video/killhouse_load.bik"))).toBe(true);
        await page.locator("#game-canvas").click({ position: { x: 5, y: 5 } });
        await page.evaluate(() => {
            const driver = globalThis.__KISAKCOD_WEB__.module.audioDriver;
            const original = driver.handleCommand.bind(driver);
            globalThis.__movieAudio = { uploads: 0, plays: 0, pauses: 0 };
            driver.handleCommand = (command) => {
                if (command.op === "buffer-upload") ++globalThis.__movieAudio.uploads;
                if (command.op === "source-play") ++globalThis.__movieAudio.plays;
                if (command.op === "source-pause") ++globalThis.__movieAudio.pauses;
                return original(command);
            };
        });
        const submit = async () => {
            await page.locator("#engine-command-input").fill("cinematic killhouse_load");
            await page.locator("#engine-command-form").evaluate((form) => form.requestSubmit());
            await expect(page.locator("#engine-command-status")).toHaveText("Accepted: cinematic killhouse_load");
            await expect.poll(() => call(page, 3)).toBe(1);
        };
        await submit();
        await expect.poll(() => call(page, 0), { timeout: 15_000 }).toBeGreaterThan(2_000);
        const first = await page.locator("#game-canvas").screenshot({ path: testInfo.outputPath("movie-early.png") });
        const pausedAt = await call(page, 1);
        await page.waitForTimeout(700);
        expect(await call(page, 0)).toBe(pausedAt);
        expect(await call(page, 3)).toBe(1);
        await call(page, 2);
        await expect.poll(() => call(page, 0), { timeout: 15_000 }).toBeGreaterThan(pausedAt + 4_000);
        const second = await page.locator("#game-canvas").screenshot({ path: testInfo.outputPath("movie-middle.png") });
        expect(second.equals(first)).toBe(false);
        const audio = await page.evaluate(() => ({ ...globalThis.__movieAudio,
            state: globalThis.__KISAKCOD_WEB__.module.audioDriver.context?.state }));
        expect(audio.uploads).toBeGreaterThan(0);
        expect(audio.plays).toBeGreaterThan(0);
        expect(audio.pauses).toBeGreaterThan(0);
        expect(audio.state).toBe("running");
        await expect.poll(() => page.evaluate(() => globalThis.__movieEvents.some(({ state }) => state === "ended")),
            { timeout: 60_000 }).toBe(true);
        const events = await page.evaluate(() => globalThis.__movieEvents);
        expect(events.filter(({ state }) => state === "failed" || state === "skipped")).toEqual([]);
        const elapsed = events.find(({ state }) => state === "ended").wallTime -
            events.find(({ state }) => state === "started").wallTime;
        expect(elapsed).toBeGreaterThan(37_000 + 700);
        expect(elapsed).toBeLessThan(42_000);
        await submit();
        await expect.poll(() => call(page, 0)).toBeGreaterThan(300);
        await page.locator("#game-canvas").focus();
        await page.keyboard.press("Escape");
        await expect.poll(() => call(page, 3)).toBe(2);
        expect(failures).toEqual([]);
        await writeFile(testInfo.outputPath("cinematic-evidence.json"),
            JSON.stringify({ events, elapsed, audio, browser: testInfo.project.name }, null, 2));
        await testInfo.attach("cinematic-evidence", { contentType: "application/json",
            body: Buffer.from(JSON.stringify({ events, elapsed, audio, browser: testInfo.project.name })) });
    });

test("production movie plays owned frames and completes through the shipped command", {
    tag: ["@retail-cinematic", "@product"],
}, async ({ retailPage: page }, testInfo) => {
    test.skip(process.env.KISAK_WEB_PRODUCT_TEST !== "1", "Runs against the production site.");
    test.setTimeout(480_000);
    await page.addInitScript(() => {
        globalThis.__movieEvents = [];
        globalThis.addEventListener("kisakcod:cinematic", ({ detail }) =>
            globalThis.__movieEvents.push({ ...detail, wallTime: performance.now() }));
    });
    await page.goto("/");
    await expect(page.locator("html")).toHaveAttribute("data-runtime-state", "running");
    const chooser = page.waitForEvent("filechooser");
    await page.locator("#portable-install-button").click();
    await (await chooser).setFiles(retailRoot);
    await expect(page.locator(".asset-control")).toHaveAttribute("data-asset-state", "ready", { timeout: 300_000 });
    await expect(page.locator("#boot-log")).toContainText("canonical runtime started", { timeout: 300_000 });
    await page.locator("#game-canvas").click({ position: { x: 5, y: 5 } });
    await page.evaluate(async () => {
        const { WebAudioDriver } = await import("/web_audio_driver.mjs");
        const original = WebAudioDriver.prototype.handleCommand;
        globalThis.__movieAudioStarted = false;
        WebAudioDriver.prototype.handleCommand = function (command) {
            const result = original.call(this, command);
            if (command.op === "source-play" && command.aliasName === "$cinematic")
                globalThis.__movieAudioStarted = result && this.context?.state === "running";
            return result;
        };
    });
    await page.locator("#engine-command-input").fill("cinematic killhouse_load");
    await page.locator("#engine-command-submit").click();
    await expect.poll(() => page.evaluate(() => globalThis.__movieEvents.some(({ state }) => state === "started")))
        .toBe(true);
    await expect.poll(() => page.evaluate(() => globalThis.__movieAudioStarted)).toBe(true);
    await page.waitForTimeout(2_000);
    const first = await page.locator("#game-canvas").screenshot({ path: testInfo.outputPath("production-movie-early.png") });
    await page.waitForTimeout(4_000);
    const second = await page.locator("#game-canvas").screenshot({ path: testInfo.outputPath("production-movie-middle.png") });
    expect(second.equals(first)).toBe(false);
    await expect.poll(() => page.evaluate(() => globalThis.__movieEvents.some(({ state }) => state === "ended")),
        { timeout: 60_000 }).toBe(true);
    const events = await page.evaluate(() => globalThis.__movieEvents);
    expect(events.filter(({ state }) => state === "failed" || state === "skipped")).toEqual([]);
    const elapsed = events.find(({ state }) => state === "ended").wallTime -
        events.find(({ state }) => state === "started").wallTime;
    expect(elapsed).toBeGreaterThan(37_000);
    expect(elapsed).toBeLessThan(41_000);
    expect(await page.evaluate(() => "__KISAKCOD_WEB__" in globalThis)).toBe(false);
    await writeFile(testInfo.outputPath("production-cinematic-evidence.json"), JSON.stringify({ events, elapsed }, null, 2));
});
