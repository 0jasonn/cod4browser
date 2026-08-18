import { expect, test } from "@playwright/test";
import { createInstallDirectory } from "./install_fixture.mjs";
import { createSyntheticIwd, ZIP_METHOD_DEFLATE } from "./synthetic_iwd.mjs";

function fnv1a(text)
{
    let hash = 0x811c9dc5;
    for (const byte of new TextEncoder().encode(text)) {
        hash ^= byte;
        hash = Math.imul(hash, 0x01000193) >>> 0;
    }
    return hash >>> 0;
}

async function callPathProbe(page, functionName, path, values = [])
{
    return page.evaluate(async ({ functionName, path, values }) => {
        const bytes = new TextEncoder().encode(`${path}\0`);
        return globalThis.__KISAKCOD_WEB__.module.callProbe(
            functionName,
            [bytes],
            [{ kind: "pointer", index: 0 }, ...values.map((value) => ({
                kind: "value",
                value,
            }))],
        );
    }, { functionName, path, values });
}

async function callListProbe(page, path, extension = "")
{
    return page.evaluate(async ({ path, extension }) => {
        const encoder = new TextEncoder();
        return globalThis.__KISAKCOD_WEB__.module.callProbe(
            "_KisakWeb_CanonicalFsListCount",
            [encoder.encode(`${path}\0`), encoder.encode(`${extension}\0`)],
            [{ kind: "pointer", index: 0 }, { kind: "pointer", index: 1 }],
        );
    }, { path, extension });
}

test("canonical FS_InitFilesystem owns Worker search paths and IWD precedence", {
    tag: "@smoke",
}, async ({ page }, testInfo) => {
    const iw00 = createSyntheticIwd([
        { path: "collision.txt", contents: "from-iw00", method: ZIP_METHOD_DEFLATE },
        { path: "archive-zero.txt", contents: "zero", method: ZIP_METHOD_DEFLATE },
    ]);
    const iw01 = createSyntheticIwd([
        { path: "collision.txt", contents: "from-iw01", method: ZIP_METHOD_DEFLATE },
        { path: "seek.txt", contents: "0123456789", method: ZIP_METHOD_DEFLATE },
    ]);
    const directory = await createInstallDirectory(testInfo, "canonical-filesystem", {
        primaryIwd: iw00,
        overrides: new Map([["main/iw_01.iwd", iw01]]),
        extraFiles: new Map([
            ["main/collision.txt", "from-loose"],
            ["main/loose.txt", "loose-file"],
            ["main/config.cfg", "set synthetic_fs 1\n"],
            ["main/unpacked/only.dat", "enumerated-loose-file"],
        ]),
    });
    await page.addInitScript(() => {
        Object.defineProperty(globalThis, "showDirectoryPicker", {
            configurable: true,
            value: undefined,
        });
        globalThis.__canonicalFsEvents = [];
        globalThis.addEventListener("kisakcod:canonical-filesystem", (event) => {
            globalThis.__canonicalFsEvents.push(structuredClone(event.detail));
        });
    });
    await page.goto("/");
    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#select-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(directory);

    await expect.poll(() => page.evaluate(() =>
        globalThis.__canonicalFsEvents.at(-1)?.state)).toBe("ready");
    await expect.poll(() => page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__?.database?.stopStage)).not.toBe("");

    const evidence = await page.evaluate(() =>
        structuredClone(globalThis.__canonicalFsEvents.at(-1)));
    expect(evidence).toMatchObject({
        canonical: true,
        asyncify: false,
        browserOwnedSearchPaths: false,
        archiveCount: 21,
    });
    expect(evidence.searchPathCount).toBeGreaterThan(evidence.archiveCount);
    const iw01Index = evidence.searchPaths.findIndex((entry) => entry.includes("iw_01"));
    const iw00Index = evidence.searchPaths.findIndex((entry) => entry.includes("iw_00"));
    const looseMainIndex = evidence.searchPaths.indexOf("dir:main");
    expect(iw01Index).toBeGreaterThanOrEqual(0);
    expect(iw01Index).toBeLessThan(iw00Index);
    expect(iw00Index).toBeLessThan(looseMainIndex);

    expect(await callPathProbe(page,
        "_KisakWeb_CanonicalFsFileSize", "collision.txt")).toBe(9);
    expect((await callPathProbe(page,
        "_KisakWeb_CanonicalFsReadHash", "collision.txt", [0, 9])) >>> 0)
        .toBe(fnv1a("from-iw01"));
    expect(await callPathProbe(page,
        "_KisakWeb_CanonicalFsFileSize", "loose.txt")).toBe(10);
    expect((await callPathProbe(page,
        "_KisakWeb_CanonicalFsReadHash", "seek.txt", [3, 4])) >>> 0)
        .toBe(fnv1a("3456"));
    expect(await callPathProbe(page,
        "_KisakWeb_CanonicalFsFileSize", "missing.txt")).toBe(-1);

    expect(await callListProbe(page, "", "txt")).toBe(4);

    expect(await callListProbe(page, "not-present")).toBe(0);

    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.testControl({
        failSyncOpenPath: "main/loose.txt",
    }));
    expect(await callPathProbe(page,
        "_KisakWeb_CanonicalFsFileSize", "loose.txt")).toBe(-1);

    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.testControl({
        failSyncOpenPath: null,
        failSyncReadPath: "main/iw_01.iwd",
    }));
    expect(await callPathProbe(page,
        "_KisakWeb_CanonicalFsReadHash", "collision.txt", [0, 9])).toBe(0);

    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.testControl({
        failSyncReadPath: null,
        failSyncSeekPath: "main/iw_01.iwd",
    }));
    expect(await callPathProbe(page,
        "_KisakWeb_CanonicalFsReadHash", "seek.txt", [3, 4])).toBe(0);

    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.testControl({
        failSyncSeekPath: null,
        failSyncListPath: "main/unpacked",
    }));
    expect(await callListProbe(page, "unpacked")).toBe(0);

    await page.evaluate(() => globalThis.__KISAKCOD_WEB__.module.testControl({
        failSyncListPath: null,
    }));
    expect(await callListProbe(page, "unpacked")).toBe(1);
});
