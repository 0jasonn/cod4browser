import { mkdtemp, rm } from "node:fs/promises";
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
                await context.close();
            } finally {
                await rm(profile, { recursive: true, force: true, maxRetries: 5 });
            }
        }
    },
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

test("canonical retail main menu starts without a map", { tag: "@retail-ui" },
    async ({ retailPage: page }) => {
        test.setTimeout(300_000);
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
    });
