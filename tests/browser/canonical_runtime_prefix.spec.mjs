import { expect, test } from "@playwright/test";

test("real Com_Init publishes the canonical runtime-prefix trace", { tag: "@smoke" }, async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__gate3InitTraces = [];
        globalThis.addEventListener("kisakcod:canonical-runtime-prefix", (event) => {
            globalThis.__gate3InitTraces.push(structuredClone(event.detail));
        });
    });
    await page.goto("/");
    await expect.poll(
        () => page.evaluate(() => globalThis.__gate3InitTraces.length),
    ).toBe(1);

    const trace = await page.evaluate(() => structuredClone(globalThis.__gate3InitTraces[0]));
    expect(trace).toMatchObject({
        stopStage: "DB_LoadXAssets/engine-filesystem-mount",
        stageCount: 14,
        startupVariableCount: 1,
        commandCount: 6,
        dvarCount: 22,
        commands: ["wait", "vstr", "exec", "cmdlist", "seta", "set"],
        physicalMemorySize: 0x8000000,
        pmemLowPosition: 0,
        pmemHighPosition: 0x8000000,
        pmemHighAllocationCount: 1,
        databaseInitializing: true,
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
            "PMem_Init",
            "DB_SetInitializing",
            "PMem_BeginAlloc",
            "Com_InitXAssets",
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
