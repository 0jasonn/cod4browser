import { expect, test } from "@playwright/test";

test("real Com_Init publishes the Gate 3 canonical prefix trace", { tag: "@smoke" }, async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__gate3InitTraces = [];
        globalThis.addEventListener("kisakcod:gate3-init", (event) => {
            globalThis.__gate3InitTraces.push(structuredClone(event.detail));
        });
    });
    await page.goto("/");
    await expect.poll(
        () => page.evaluate(() => globalThis.__gate3InitTraces.length),
    ).toBe(1);

    const trace = await page.evaluate(() => structuredClone(globalThis.__gate3InitTraces[0]));
    expect(trace).toMatchObject({
        stopStage: "PMem_Init/DB_SetInitializing",
        stageCount: 10,
        startupVariableCount: 3,
        commandCount: 4,
        dvarCount: 22,
        stages: [
            "Com_Init entered",
            "Com_ParseCommandLine",
            "SL_Init",
            "Swap_Init",
            "Cbuf_Init",
            "Cmd_Init",
            "Com_StartupVariable",
            "Com_InitDvars",
            "CCS_InitConstantConfigStrings",
            "stop",
        ],
    });
    expect(trace.dvars).toEqual(expect.arrayContaining([
        "com_maxfps",
        "useFastFile",
        "sys_lockThreads",
        "sys_smp_allowed",
        "developer",
        "com_timescale",
        "sv_running",
        "wideScreen",
    ]));
});
