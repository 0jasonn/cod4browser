import { expect, test } from "@playwright/test";
import { mkdir, readdir, writeFile } from "node:fs/promises";
import path from "node:path";
import {
    createInstallDirectory as createM12InstallDirectory,
    createSyntheticFastfileHeader,
    createSyntheticWorldInventoryFastfile,
    SYNTHETIC_LOCALIZATION,
} from "./install_fixture.mjs";
import { REQUIRED_ASSETS } from "../../web/asset_store.mjs";
import { createSyntheticIwd } from "./synthetic_iwd.mjs";

const SYNTHETIC_MAP_FASTFILE_SIZE = createSyntheticWorldInventoryFastfile().length;

async function createInstallDirectory(testInfo, name, { localization, iwd } = {})
{
    return createM12InstallDirectory(testInfo, name, {
        localization: localization ?? SYNTHETIC_LOCALIZATION,
        primaryIwd: iwd ?? createSyntheticIwd(),
    });
}

async function usePortableFolderPicker(page)
{
    await page.addInitScript(() => {
        Object.defineProperty(globalThis, "showDirectoryPicker", {
            configurable: true,
            value: undefined,
        });
    });
}

async function waitForEngine(page)
{
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state,
    )).toBe("running");
}

async function waitForAssets(page, state)
{
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.assets?.state,
    )).toBe(state);
}

async function chooseDirectory(page, directory)
{
    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#select-install-button").click();
    const chooser = await chooserPromise;
    expect(chooser.isMultiple()).toBe(true);
    await chooser.setFiles(directory);
}

test("imports synthetic files through the portable picker and restores them after reload", { tag: "@smoke" }, async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    const directory = await createInstallDirectory(testInfo, "synthetic-install");
    const assetNetworkRequests = [];
    page.on("request", (request) => {
        const url = new URL(request.url());
        const pathName = url.pathname.toLowerCase();
        if (url.origin !== "http://127.0.0.1:8000" || request.method() !== "GET" ||
            pathName.endsWith("/localization.txt") || pathName.endsWith(".iwd") ||
            pathName.endsWith(".ff")) {
            assetNetworkRequests.push(`${request.method()} ${request.url()}`);
        }
    });

    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    await chooseDirectory(page, directory);
    await waitForAssets(page, "ready");

    await expect(page.locator("#asset-state-label")).toHaveText("Local installation ready");
    await expect(page.locator("#asset-message")).toHaveText("Installation persisted and verified");
    await expect(page.locator("#asset-manifest")).toContainText("english");
    await expect(page.locator("#asset-manifest")).toContainText("localization.txt");
    await expect(page.locator("#asset-manifest")).toContainText("main/iw_00.iwd");
    await expect(page.locator("#asset-manifest")).toContainText("sp-killhouse-english-v1");
    await expect(page.locator("#asset-manifest")).toContainText("zone/english/killhouse.ff");
    await expect(page.locator("#asset-manifest")).toContainText("Fastfiles verified");
    await expect(page.locator("#asset-manifest")).toContainText("IWD entries declared");
    await expect(page.locator("#asset-manifest")).toContainText("1");
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.assets)).toMatchObject({
        state: "ready",
        source: "selection",
        manifest: {
            schema: 3,
            language: "english",
            profile: {
                id: "sp-killhouse-english-v1",
                map: "killhouse",
                archiveCount: 21,
                zoneCount: 4,
            },
            archiveProbe: { entriesDeclared: 21, archivesProbed: 21 },
            zoneProbe: { filesProbed: 4, version: 5, compression: "zlib" },
        },
    });
    expect(await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.assets.manifest.files
            .map(({ path: filePath }) => filePath).sort(),
    )).toEqual(REQUIRED_ASSETS.map(({ path: assetPath }) => assetPath).sort());

    await page.reload();
    await waitForEngine(page);
    await waitForAssets(page, "ready");
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.assets)).toMatchObject({
        state: "ready",
        source: "restored",
        manifest: { language: "english" },
    });
    await expect(page.locator("#asset-message")).toHaveText(
        "Persisted installation reopened and verified",
    );
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.engineWorldSurface?.state,
    )).toBe("ready");
    const worldBeforeVfsReads = await page.evaluate(() => ({
        extractionGeneration:
            globalThis.__KISAKCOD_WEB__.engineWorldSurface.extractionGeneration,
        conversionGeneration:
            globalThis.__KISAKCOD_WEB__.engineWorldSurface.conversionGeneration,
        synthetic: globalThis.__KISAKCOD_WEB__.engineWorldSurface.synthetic,
    }));
    const adapterProbe = await page.evaluate(async () => {
        const store = globalThis.__KISAKCOD_WEB__.assetStore;
        const localizationStat = await store.stat("localization.txt");
        const archiveStat = await store.stat("main/iw_00.iwd");
        const localization = new TextDecoder().decode(await store.read("localization.txt"));
        const archiveSignature = Array.from(await store.read(
            "main/iw_00.iwd",
            { offset: 0, length: 4 },
        ));
        const mapStat = await store.stat("zone/english/killhouse.ff");
        const mapHeader = Array.from(await store.read(
            "zone/english/killhouse.ff",
            { offset: 0, length: 14 },
        ));
        let invalidRange = null;
        try {
            await store.read("main/iw_00.iwd", { offset: 0, length: 1024 * 1024 + 1 });
        } catch (error) {
            invalidRange = error.code;
        }
        return {
            localizationStat,
            archiveStat,
            localization,
            archiveSignature,
            mapStat,
            mapHeader,
            invalidRange,
        };
    });
    expect(adapterProbe.localizationStat.size).toBe(Buffer.byteLength(SYNTHETIC_LOCALIZATION));
    expect(adapterProbe.archiveStat.size).toBe(createSyntheticIwd().length);
    expect(adapterProbe.localization).toBe(SYNTHETIC_LOCALIZATION);
    expect(adapterProbe.archiveSignature).toEqual([0x50, 0x4b, 0x03, 0x04]);
    expect(adapterProbe.mapStat).toMatchObject({
        path: "zone/english/killhouse.ff",
        size: SYNTHETIC_MAP_FASTFILE_SIZE,
    });
    expect(adapterProbe.mapHeader).toEqual([
        0x49, 0x57, 0x66, 0x66, 0x75, 0x31, 0x30, 0x30,
        0x05, 0x00, 0x00, 0x00, 0x78, 0xda,
    ]);
    expect(await page.evaluate(() => ({
        extractionGeneration:
            globalThis.__KISAKCOD_WEB__.engineWorldSurface.extractionGeneration,
        conversionGeneration:
            globalThis.__KISAKCOD_WEB__.engineWorldSurface.conversionGeneration,
        synthetic: globalThis.__KISAKCOD_WEB__.engineWorldSurface.synthetic,
    }))).toEqual(worldBeforeVfsReads);
    expect(worldBeforeVfsReads.synthetic).toBe(true);
    expect(adapterProbe.invalidRange).toBe("INVALID_RANGE");
    expect(assetNetworkRequests).toEqual([]);
});

test("uses the native directory picker boundary without enumerating unrelated files", async ({ page }) => {
    const iwdBytes = Array.from(createSyntheticIwd());
    const fastfileBytes = Array.from(createSyntheticFastfileHeader());
    const descriptors = REQUIRED_ASSETS.map((requirement) => ({
        path: requirement.path,
        kind: requirement.kind,
        bytes: requirement.kind === "iwd"
            ? iwdBytes
            : requirement.kind === "fastfile"
                ? fastfileBytes
                : null,
    }));
    const expectedAccesses = [];
    const seenDirectories = new Set();
    for (const descriptor of descriptors) {
        const segments = descriptor.path.split("/");
        let prefix = "";
        for (const segment of segments.slice(0, -1)) {
            prefix = prefix ? `${prefix}/${segment}` : segment;
            if (!seenDirectories.has(prefix)) {
                seenDirectories.add(prefix);
                expectedAccesses.push(`directory:${prefix}`);
            }
        }
        expectedAccesses.push(`file:${descriptor.path}`);
    }
    await page.addInitScript(({ assets, localization }) => {
        const files = new Map(assets.map((asset) => [asset.path, new File(
            [asset.kind === "localization"
                ? localization
                : new Uint8Array(asset.bytes)],
            asset.path.split("/").at(-1),
            { type: asset.kind === "iwd" ? "application/zip" : "application/octet-stream" },
        )]));
        const directories = new Set([""]);
        for (const asset of assets) {
            const segments = asset.path.split("/");
            let prefix = "";
            for (const segment of segments.slice(0, -1)) {
                prefix = prefix ? `${prefix}/${segment}` : segment;
                directories.add(prefix);
            }
        }
        globalThis.__pickerAccesses = [];
        globalThis.__pickerInvocations = 0;
        globalThis.__pickerOptions = null;
        const makeDirectoryHandle = (prefix, name) => ({
            kind: "directory",
            name,
            async getFileHandle(name) {
                const child = prefix ? `${prefix}/${name}` : name;
                globalThis.__pickerAccesses.push(`file:${child}`);
                const file = files.get(child);
                if (!file) {
                    throw new DOMException("Missing", "NotFoundError");
                }
                return {
                    kind: "file",
                    name,
                    async getFile() { return file; },
                };
            },
            async getDirectoryHandle(name) {
                const child = prefix ? `${prefix}/${name}` : name;
                globalThis.__pickerAccesses.push(`directory:${child}`);
                if (!directories.has(child)) {
                    throw new DOMException("Missing", "NotFoundError");
                }
                return makeDirectoryHandle(child, name);
            },
        });
        const rootHandle = makeDirectoryHandle("", "Synthetic COD4");
        globalThis.showDirectoryPicker = async (options) => {
            globalThis.__pickerInvocations += 1;
            globalThis.__pickerOptions = options;
            return rootHandle;
        };
    }, { assets: descriptors, localization: SYNTHETIC_LOCALIZATION });

    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    await page.locator("#select-install-button").click();
    await waitForAssets(page, "ready");
    expect(await page.evaluate(() => ({
        invocations: globalThis.__pickerInvocations,
        accesses: globalThis.__pickerAccesses,
        options: globalThis.__pickerOptions,
    }))).toEqual({
        invocations: 1,
        accesses: expectedAccesses,
        options: { id: "kisakcod-install", mode: "read" },
    });
});

test("rejects a folder missing the required archive and persists nothing", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    const directory = testInfo.outputPath("missing-archive");
    await mkdir(directory, { recursive: true });
    await writeFile(path.join(directory, "localization.txt"), SYNTHETIC_LOCALIZATION, "utf8");

    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    await chooseDirectory(page, directory);
    await waitForAssets(page, "failed");
    await expect(page.locator("#asset-message")).toContainText("missing main/iw_00.iwd");

    await page.reload();
    await waitForEngine(page);
    await waitForAssets(page, "empty");
});

test("requires the selected single-player map fastfile", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    const directory = await createM12InstallDirectory(
        testInfo,
        "missing-killhouse-zone",
        { omit: ["zone/english/killhouse.ff"] },
    );

    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    await chooseDirectory(page, directory);
    await waitForAssets(page, "failed");
    await expect(page.locator("#asset-message")).toContainText(
        "missing zone/english/killhouse.ff",
    );
});

test("rejects malformed retail fastfile framing before persistence", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    const malformed = createSyntheticFastfileHeader();
    malformed[0] = 0x58;
    const directory = await createM12InstallDirectory(
        testInfo,
        "malformed-killhouse-zone",
        { overrides: new Map([["zone/english/killhouse.ff", malformed]]) },
    );

    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    await chooseDirectory(page, directory);
    await waitForAssets(page, "failed");
    await expect(page.locator("#asset-message")).toContainText(
        "zone/english/killhouse.ff",
    );
    await expect(page.locator("#asset-message")).toContainText("IWffu100 header");
    expect(await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.assets.error,
    )).toBe("PROBE_40");
});

test("rejects an oversized map fastfile before reading or copying it", async ({ page }) => {
    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");

    const result = await page.evaluate(async ({
        localization,
        requirements,
        iwd,
        ff,
    }) => {
        let touched = false;
        const oversized = {
            size: 512 * 1024 * 1024 + 1,
            slice() {
                touched = true;
                throw new Error("oversized source must not be read");
            },
            stream() {
                touched = true;
                throw new Error("oversized source must not be copied");
            },
        };
        const entries = new Map(requirements.map((requirement) => {
            if (requirement.path === "zone/english/killhouse.ff") {
                return [requirement.path, oversized];
            }
            const contents = requirement.kind === "localization"
                ? localization
                : new Uint8Array(requirement.kind === "iwd" ? iwd : ff);
            return [
                requirement.path,
                new File([contents], requirement.path.split("/").at(-1)),
            ];
        }));
        try {
            await globalThis.__KISAKCOD_WEB__.assetStore.importEntries(entries);
            return { code: "accepted", touched };
        } catch (error) {
            return { code: error.code, touched };
        }
    }, {
        localization: SYNTHETIC_LOCALIZATION,
        requirements: REQUIRED_ASSETS.map(({ path: assetPath, kind }) => ({
            path: assetPath,
            kind,
        })),
        iwd: Array.from(createSyntheticIwd()),
        ff: Array.from(createSyntheticFastfileHeader()),
    });
    expect(result).toEqual({ code: "INVALID_SIZE", touched: false });
});

test("rejects an unsupported localization marker and commits no active import", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    const directory = await createInstallDirectory(testInfo, "bad-localization", {
        localization: "esperanto\n\nSYNTHETIC_KEY\n\"Synthetic value\"\n",
    });

    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    await chooseDirectory(page, directory);
    await waitForAssets(page, "failed");
    await expect(page.locator("#asset-message")).toContainText(
        "names a language this engine build does not support",
    );

    await page.reload();
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    await expect(page.locator("#asset-message")).toHaveText(
        "No local installation has been imported",
    );
});

test("rejects a plausible IWD with a malformed central-directory envelope", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    const malformedIwd = createSyntheticIwd();
    malformedIwd.writeUInt32LE(0, malformedIwd.length - 22);
    const directory = await createInstallDirectory(testInfo, "bad-iwd", { iwd: malformedIwd });

    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    await chooseDirectory(page, directory);
    await waitForAssets(page, "failed");
    await expect(page.locator("#asset-message")).toContainText(
        "no valid ZIP end-of-central-directory record",
    );
});

test("the Wasm probe rejects unsafe synthetic ZIP32 variants before copying", { tag: "@native-covered" }, async ({ page }) => {
    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");

    const variants = [];
    const multiDisk = createSyntheticIwd();
    multiDisk.writeUInt16LE(1, multiDisk.length - 18);
    variants.push({ name: "multi-disk", bytes: Array.from(multiDisk), code: "PROBE_22" });

    const badRange = createSyntheticIwd();
    badRange.writeUInt32LE(0xfffffff0, badRange.length - 6);
    variants.push({ name: "central-range", bytes: Array.from(badRange), code: "PROBE_25" });

    const encrypted = createSyntheticIwd();
    encrypted.writeUInt16LE(1, 6);
    encrypted.writeUInt16LE(1, 31 + 8);
    variants.push({ name: "encrypted", bytes: Array.from(encrypted), code: "PROBE_29" });

    const unsupportedCompression = createSyntheticIwd();
    unsupportedCompression.writeUInt16LE(99, 8);
    unsupportedCompression.writeUInt16LE(99, 31 + 10);
    variants.push({
        name: "compression",
        bytes: Array.from(unsupportedCompression),
        code: "PROBE_30",
    });

    const results = await page.evaluate(async ({ localization, cases, requirements, iwd, ff }) => {
        const output = [];
        for (const variant of cases) {
            const entries = new Map(requirements.map((requirement) => {
                const bytes = requirement.path === "main/iw_00.iwd"
                    ? variant.bytes
                    : requirement.kind === "iwd"
                        ? iwd
                        : ff;
                const contents = requirement.kind === "localization"
                    ? localization
                    : new Uint8Array(bytes);
                return [
                    requirement.path,
                    new File([contents], requirement.path.split("/").at(-1)),
                ];
            }));
            try {
                await globalThis.__KISAKCOD_WEB__.assetStore.importEntries(entries);
                output.push({ name: variant.name, code: "accepted" });
            } catch (error) {
                output.push({ name: variant.name, code: error.code });
            }
        }
        return output;
    }, {
        localization: SYNTHETIC_LOCALIZATION,
        cases: variants,
        requirements: REQUIRED_ASSETS.map(({ path: assetPath, kind }) => ({
            path: assetPath,
            kind,
        })),
        iwd: Array.from(createSyntheticIwd()),
        ff: Array.from(createSyntheticFastfileHeader()),
    });
    expect(results).toEqual(variants.map(({ name, code }) => ({ name, code })));
});

test("a failed replacement preserves the previously committed installation", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    const validDirectory = await createInstallDirectory(testInfo, "valid-first");
    const malformedIwd = createSyntheticIwd();
    malformedIwd.writeUInt32LE(0xfffffff0, malformedIwd.length - 6);
    const invalidDirectory = await createInstallDirectory(testInfo, "invalid-replacement", {
        iwd: malformedIwd,
    });

    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    await chooseDirectory(page, validDirectory);
    await waitForAssets(page, "ready");
    const firstImportId = await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.assets.manifest.importId,
    );

    await chooseDirectory(page, invalidDirectory);
    await waitForAssets(page, "failed");
    expect(await page.evaluate(() => globalThis.__KISAKCOD_WEB__.assets)).toMatchObject({
        state: "failed",
        retained: true,
        manifest: { importId: firstImportId },
    });

    await page.reload();
    await waitForEngine(page);
    await waitForAssets(page, "ready");
    expect(await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.assets.manifest.importId,
    )).toBe(firstImportId);
});

test("a replacement corrupted during its OPFS copy rolls back atomically", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    const validDirectory = await createInstallDirectory(testInfo, "valid-before-copy-failure");
    const selectedIwd = createSyntheticIwd();
    const copiedIwd = createSyntheticIwd();
    copiedIwd.writeUInt32LE(0, copiedIwd.length - 22);

    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    await chooseDirectory(page, validDirectory);
    await waitForAssets(page, "ready");
    const firstImportId = await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.assets.manifest.importId,
    );

    const result = await page.evaluate(async ({
        localization,
        selected,
        copied,
        requirements,
        iwd,
        ff,
    }) => {
        const selectedArchive = new File([new Uint8Array(selected)], "iw_00.iwd");
        const copiedArchive = new File([new Uint8Array(copied)], "iw_00.iwd");
        const changesAfterProbe = {
            size: selectedArchive.size,
            slice(...arguments_) { return selectedArchive.slice(...arguments_); },
            stream() { return copiedArchive.stream(); },
        };
        const entries = new Map(requirements.map((requirement) => {
            if (requirement.path === "main/iw_00.iwd") {
                return [requirement.path, changesAfterProbe];
            }
            const contents = requirement.kind === "localization"
                ? localization
                : new Uint8Array(requirement.kind === "iwd" ? iwd : ff);
            return [
                requirement.path,
                new File([contents], requirement.path.split("/").at(-1)),
            ];
        }));
        try {
            await globalThis.__KISAKCOD_WEB__.assetStore.importEntries(entries);
            return { code: "accepted" };
        } catch (error) {
            return {
                code: error.code,
                state: globalThis.__KISAKCOD_WEB__.assets,
            };
        }
    }, {
        localization: SYNTHETIC_LOCALIZATION,
        selected: Array.from(selectedIwd),
        copied: Array.from(copiedIwd),
        requirements: REQUIRED_ASSETS.map(({ path: assetPath, kind }) => ({
            path: assetPath,
            kind,
        })),
        iwd: Array.from(createSyntheticIwd()),
        ff: Array.from(createSyntheticFastfileHeader()),
    });
    expect(result).toMatchObject({
        code: "PROBE_21",
        state: {
            state: "failed",
            retained: true,
            manifest: { importId: firstImportId },
        },
    });

    await page.reload();
    await waitForEngine(page);
    await waitForAssets(page, "ready");
    expect(await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.assets.manifest.importId,
    )).toBe(firstImportId);
});

test("serializes cross-tab changes and clear removes committed and abandoned imports", { tag: "@smoke" }, async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    const directory = await createInstallDirectory(testInfo, "cross-tab-install");
    const secondPage = await page.context().newPage();

    await Promise.all([page.goto("/"), secondPage.goto("/")]);
    await Promise.all([waitForEngine(page), waitForEngine(secondPage)]);
    await Promise.all([waitForAssets(page, "empty"), waitForAssets(secondPage, "empty")]);

    await chooseDirectory(page, directory);
    await waitForAssets(page, "ready");
    await waitForAssets(secondPage, "ready");

    await secondPage.evaluate(async () => {
        const root = await navigator.storage.getDirectory();
        const app = await root.getDirectoryHandle("kisakcod-web", { create: true });
        const imports = await app.getDirectoryHandle("imports", { create: true });
        const orphan = await imports.getDirectoryHandle(
            "11111111-1111-4111-8111-111111111111",
            { create: true },
        );
        const garbage = await orphan.getFileHandle("partial.tmp", { create: true });
        const writable = await garbage.createWritable();
        await writable.write(new Uint8Array([1, 2, 3]));
        await writable.close();
        await globalThis.__KISAKCOD_WEB__.assetStore.clear();
    });

    await Promise.all([waitForAssets(page, "empty"), waitForAssets(secondPage, "empty")]);
    expect(await secondPage.evaluate(async () => {
        const root = await navigator.storage.getDirectory();
        const app = await root.getDirectoryHandle("kisakcod-web");
        const imports = await app.getDirectoryHandle("imports");
        const names = [];
        for await (const [name] of imports.entries()) {
            names.push(name);
        }
        return names;
    })).toEqual([]);

    await Promise.all([page.reload(), secondPage.reload()]);
    await Promise.all([waitForEngine(page), waitForEngine(secondPage)]);
    await Promise.all([waitForAssets(page, "empty"), waitForAssets(secondPage, "empty")]);
});

test("repairs malformed manifest metadata and notifies other tabs", async ({ page }) => {
    const secondPage = await page.context().newPage();
    await Promise.all([page.goto("/"), secondPage.goto("/")]);
    await Promise.all([waitForEngine(page), waitForEngine(secondPage)]);
    await Promise.all([waitForAssets(page, "empty"), waitForAssets(secondPage, "empty")]);

    await page.evaluate(async () => {
        const root = await navigator.storage.getDirectory();
        const app = await root.getDirectoryHandle("kisakcod-web", { create: true });
        const imports = await app.getDirectoryHandle("imports", { create: true });
        await imports.getDirectoryHandle("22222222-2222-4222-8222-222222222222", {
            create: true,
        });
        await new Promise((resolve, reject) => {
            const request = indexedDB.open("kisakcod-web", 1);
            request.onerror = () => reject(request.error);
            request.onsuccess = () => {
                const transaction = request.result.transaction("metadata", "readwrite");
                transaction.objectStore("metadata").put({
                    schema: 1,
                    importId: "22222222-2222-4222-8222-222222222222",
                    files: { malformed: true },
                }, "active-import");
                transaction.oncomplete = resolve;
                transaction.onerror = () => reject(transaction.error);
            };
        });
    });

    await page.reload();
    await waitForEngine(page);
    await waitForAssets(page, "invalid");
    await waitForAssets(secondPage, "empty");
    expect(await secondPage.evaluate(async () => {
        const root = await navigator.storage.getDirectory();
        const app = await root.getDirectoryHandle("kisakcod-web");
        const imports = await app.getDirectoryHandle("imports");
        const names = [];
        for await (const [name] of imports.entries()) {
            names.push(name);
        }
        return names;
    })).toEqual([]);
});

test("fails safely when Web Locks are unavailable", async ({ page }) => {
    await page.addInitScript(() => {
        Object.defineProperty(navigator, "locks", {
            configurable: true,
            value: undefined,
        });
    });
    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "unsupported");
    await expect(page.locator("#asset-message")).toContainText("Web Locks");
    await expect(page.locator("#select-install-button")).toBeDisabled();
});

test("an external refresh failure leaves an actionable state instead of checking", async ({ page }) => {
    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    const replaced = await page.evaluate(() => {
        try {
            navigator.locks.request = async () => {
                throw new DOMException("Synthetic lock failure", "AbortError");
            };
            return true;
        } catch {
            return false;
        }
    });
    expect(replaced).toBe(true);
    await page.evaluate(() => {
        const channel = new BroadcastChannel("kisakcod-web-assets");
        channel.postMessage({ type: "changed" });
        channel.close();
    });
    await waitForAssets(page, "failed");
    await expect(page.locator("#asset-message")).toContainText(
        "could not be refreshed",
    );
    await expect(page.locator("#select-install-button")).toBeEnabled();
});

async function listFilesRecursively(directory)
{
    const entries = await readdir(directory, { withFileTypes: true });
    const nested = await Promise.all(entries.map(async (entry) => {
        const entryPath = path.join(directory, entry.name);
        return entry.isDirectory() ? listFilesRecursively(entryPath) : [entryPath];
    }));
    return nested.flat();
}

test("the generated site contains no retail game data or proprietary runtime binaries", { tag: "@smoke" }, async () => {
    const scanRoots = ["build/web/site", "web", "tests", "src/web", "scripts/web"];
    const files = (await Promise.all(scanRoots.map((root) =>
        listFilesRecursively(path.resolve(root))))).flat();
    const prohibitedExtensions = new Set([".iwd", ".ff", ".d3dbsp", ".bik"]);
    const prohibitedNames = new Set([
        "localization.txt",
        "binkw32.dll",
        "mss32.dll",
        "steam_api.dll",
    ]);
    const violations = files.filter((file) => {
        const name = path.basename(file).toLowerCase();
        return prohibitedNames.has(name) || prohibitedExtensions.has(path.extname(name));
    });
    expect(violations).toEqual([]);
});
