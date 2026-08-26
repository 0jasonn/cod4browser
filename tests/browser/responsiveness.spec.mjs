import { expect, test } from "@playwright/test";

test("a slow canonical command yields telemetry and the frame pump recovers", async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ = { observeInput: true };
        globalThis.__slowFrameSystems = [];
        globalThis.__slowFrameInputs = [];
        globalThis.__mainThreadTurns = 0;
        setInterval(() => { ++globalThis.__mainThreadTurns; }, 0);
        globalThis.addEventListener("kisakcod:system", (event) => {
            globalThis.__slowFrameSystems.push(structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:input", (event) => {
            globalThis.__slowFrameInputs.push(structuredClone(event.detail));
        });
    });
    await page.goto("/");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state)).toBe("running");

    const before = await page.evaluate(() => ({
        frame: globalThis.__KISAKCOD_WEB__.system?.framePumpTicks ?? 0,
        turns: globalThis.__mainThreadTurns,
    }));
    const scheduled = await page.evaluate(async () => {
        const module = globalThis.__KISAKCOD_WEB__.module;
        const slow = await module.call("_KisakWeb_TestSlowNextCommand", 60);
        const command = new TextEncoder().encode("echo slow-command-complete\0");
        const accepted = module.callProbe(
            "_KisakWeb_SubmitCanonicalCommand",
            [command],
            [{ kind: "pointer", index: 0 }],
        );
        const input = module.input({ type: "key", key: 0x77, down: true });
        return { slow, accepted: await accepted, input: await input };
    });
    expect(scheduled).toEqual({ slow: 1, accepted: 1, input: true });

    await expect.poll(() => page.evaluate(() =>
        globalThis.__slowFrameSystems.some(
            ({ callbackMilliseconds }) => callbackMilliseconds >= 40))).toBe(true);
    await expect.poll(() => page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.system?.framePumpTicks ?? 0)).toBeGreaterThan(before.frame);

    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.call(
        "_KisakWeb_TestSubmitSurface"))).toBe(1);
    await expect.poll(() => page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.rendererSurface?.drawCount ?? 0)).toBeGreaterThan(0);

    const evidence = await page.evaluate(() => {
        const ticks = globalThis.__slowFrameSystems
            .filter(({ state }) => state === "running")
            .map(({ framePumpTicks }) => framePumpTicks);
        return {
            mainThreadTurns: globalThis.__mainThreadTurns,
            input: structuredClone(globalThis.__slowFrameInputs),
            ticks,
            state: globalThis.__KISAKCOD_WEB__.state,
        };
    });
    expect(evidence.mainThreadTurns).toBeGreaterThan(before.turns);
    expect(evidence.input).toContainEqual({ type: "key", key: 0x77, down: true });
    expect(evidence.ticks.every((tick, index) => index === 0 ||
        tick > evidence.ticks[index - 1])).toBe(true);
    expect(new Set(evidence.ticks).size).toBe(evidence.ticks.length);
    expect(evidence.state).toBe("running");
});
