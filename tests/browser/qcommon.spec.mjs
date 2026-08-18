import { expect, test } from "@playwright/test";
import { createInstallDirectory } from "./install_fixture.mjs";

async function importInstall(page, testInfo, name)
{
    const directory = await createInstallDirectory(testInfo, name);
    await page.addInitScript(() => {
        Object.defineProperty(globalThis, "showDirectoryPicker", {
            configurable: true,
            value: undefined,
        });
    });
    await page.goto("/");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.assets?.state),
    ).toBe("empty");
    await page.evaluate(() => {
        globalThis.__qcommonEvents = [];
        globalThis.__qcommonArchiveEvents = [];
        globalThis.addEventListener("kisakcod:qcommon", (event) => {
            globalThis.__qcommonEvents.push(structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:archive", (event) => {
            globalThis.__qcommonArchiveEvents.push({
                state: event.detail.state,
                qcommonState: globalThis.__KISAKCOD_WEB__.qcommon.state,
            });
        });
    });
    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#select-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(directory);
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.assets?.state),
        { timeout: 30_000 },
    ).toBe("ready");
    return directory;
}

test("qcommon reaches its bounded canonical database handoff", { tag: "@smoke" }, async ({ page }, testInfo) => {
    await importInstall(page, testInfo, "qcommon-success");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.qcommon?.state),
        { timeout: 30_000 },
    ).toBe("ready");
    const result = await page.evaluate(() => ({
        qcommon: structuredClone(globalThis.__KISAKCOD_WEB__.qcommon),
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
        events: structuredClone(globalThis.__qcommonEvents),
        archiveEvents: structuredClone(globalThis.__qcommonArchiveEvents),
        log: document.querySelector("#boot-log").textContent,
        status: document.querySelector("#physics-status").textContent,
    }));
    expect(result.qcommon).toMatchObject({
        state: "ready",
        stage: "pre-database",
        error: "none",
        filesChecked: 25,
        totalFiles: 25,
        probeBytesRead: 134,
        actionsIssued: 53,
        arenaBytes: 256 * 1024,
        eventCapacity: 64,
        commandDvarCount: 5,
        currentPath: null,
        actionPending: false,
        cooperative: true,
        asyncify: false,
        pthreads: false,
        retailTraversal: false,
    });
    expect(result.qcommon.generation).toBeGreaterThan(0);
    expect(result.qcommon.framePumpTick).toBeGreaterThan(0);
    expect(result.qcommon.eventsQueued).toBeGreaterThan(0);
    expect(result.qcommon.eventsProcessed).toBeGreaterThan(0);
    expect(result.events.map((event) => event.stage)).toEqual(expect.arrayContaining([
        "memory",
        "events",
        "commands",
        "filesystem-stat",
        "filesystem-read",
        "pre-database",
    ]));
    expect(result.archive).toMatchObject({ state: "idle" });
    expect(result.archiveEvents).toEqual([]);
    const loadingEvents = result.events.filter((event) => event.state === "loading");
    for (let index = 1; index < loadingEvents.length; index += 1) {
        expect(loadingEvents[index].framePumpTick)
            .toBeGreaterThanOrEqual(loadingEvents[index - 1].framePumpTick);
    }
    expect(result.log).toContain("Portable qcommon reached the pre-database boundary");
    expect(result.status).toBe("ODE + qcommon pre-database ready");
});

test("qcommon startup is repeatable, cancellable, and reports typed VFS failure", async ({ page }, testInfo) => {
    await importInstall(page, testInfo, "qcommon-lifecycle");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.qcommon?.state),
        { timeout: 30_000 },
    ).toBe("ready");
    const firstGeneration = await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.qcommon.generation,
    );

    await page.evaluate(() => {
        const runtime = globalThis.__KISAKCOD_WEB__;
        runtime.module._KisakWeb_StartQcommonRuntime();
        runtime.module._KisakWeb_CancelQcommonRuntime();
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__.qcommon.state),
    ).toBe("idle");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__.qcommon.stage),
    ).toBe("cancelled");
    const cancelled = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.qcommon),
    );
    expect(cancelled).toMatchObject({
        error: "startup was cancelled",
        actionPending: false,
    });

    await page.evaluate(async () => {
        const runtime = globalThis.__KISAKCOD_WEB__;
        await runtime.module.testControl({ failReadPath: "zone/english/common.ff" });
        runtime.module._KisakWeb_StartQcommonRuntime();
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__.qcommon.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const failed = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.qcommon),
    );
    expect(failed).toMatchObject({
        stage: "failed",
        error: "startup file read failed",
        currentPath: "zone/english/common.ff",
        filesChecked: 24,
        totalFiles: 25,
        actionPending: false,
        retailTraversal: false,
    });
    expect(failed.message).toContain("zone/english/common.ff");
});
