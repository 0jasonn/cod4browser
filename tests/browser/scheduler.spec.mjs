import { expect, test } from "@playwright/test";

test("runs every engine job through the deterministic cooperative scheduler", async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__scheduleEvents = [];
        globalThis.addEventListener("kisakcod:schedule", (event) => {
            globalThis.__scheduleEvents.push(structuredClone(event.detail));
        });
    });
    await page.goto("/");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("running");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.scheduler?.state),
    ).toBe("running");

    const result = await page.evaluate(() => ({
        latest: structuredClone(globalThis.__KISAKCOD_WEB__.scheduler),
        samples: structuredClone(globalThis.__KISAKCOD_WEB__.schedulerSamples),
        observed: structuredClone(globalThis.__scheduleEvents),
        world: structuredClone(globalThis.__KISAKCOD_WEB__.engineWorldSurface),
        renderer: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
    }));
    expect(result.latest).toMatchObject({
        state: "running",
        registeredTasks: 8,
        runnableTasks: 8,
        taskCalls: 8,
        reservedBytes: 266_254,
        reservedRecords: 267,
        starvedTasks: 0,
        starvationWarnings: 0,
        protocolViolations: 0,
        budgetExhausted: false,
        maxTaskCallsPerFrame: 8,
        maxReservedBytesPerFrame: 320 * 1024,
        maxReservedRecordsPerFrame: 320,
        maxWallMicrosecondsPerFrame: 12 * 1000,
        starvationWarningFrames: 3,
        deterministic: true,
    });
    expect(result.latest.schedulerGeneration).toBeGreaterThan(0);
    expect(result.latest.elapsedMicroseconds).toBeGreaterThanOrEqual(0);
    expect(result.latest.trace.map(({ name }) => name)).toEqual([
        "filesystem-completions",
        "qcommon",
        "retail-census",
        "archive",
        "engine-asset",
        "command-buffer",
        "world-surface",
        "renderer",
    ]);
    expect(result.latest.trace.map(({ order }) => order)).toEqual([
        10, 20, 30, 40, 50, 60, 70, 80,
    ]);
    expect(result.latest.trace.map(({ reservedBytes, reservedRecords }) => ({
        reservedBytes,
        reservedRecords,
    }))).toEqual([
        { reservedBytes: 0, reservedRecords: 8 },
        { reservedBytes: 14, reservedRecords: 1 },
        { reservedBytes: 64 * 1024, reservedRecords: 64 },
        { reservedBytes: 64 * 1024, reservedRecords: 64 },
        { reservedBytes: 64 * 1024, reservedRecords: 64 },
        { reservedBytes: 4 * 1024, reservedRecords: 1 },
        { reservedBytes: 64 * 1024, reservedRecords: 64 },
        { reservedBytes: 0, reservedRecords: 1 },
    ]);
    expect(result.latest.trace.every((entry) => entry.taskGeneration > 0)).toBe(true);
    expect(result.samples.length).toBeGreaterThan(0);
    expect(result.observed.length).toBeGreaterThan(0);
    for (let index = 1; index < result.samples.length; index += 1) {
        expect(result.samples[index].frame).toBeGreaterThan(result.samples[index - 1].frame);
    }

    // Scheduler integration must not change the established engine products.
    expect(result.world).toMatchObject({
        state: "ready",
        convertedVertexCount: 4,
        convertedIndexCount: 6,
    });
    expect(result.renderer).toMatchObject({
        state: "ready",
        vertexCount: 4,
        drawIndexCount: 6,
        resident: true,
    });
});
