import { expect, test } from "@playwright/test";
import { crc32 } from "node:zlib";
import { createInstallDirectory as createM12InstallDirectory } from "./install_fixture.mjs";
import {
    createSyntheticIwd,
    ZIP_METHOD_DEFLATE,
    ZIP_METHOD_STORE,
} from "./synthetic_iwd.mjs";

const STORED_MEMBER = Object.freeze({
    path: "synthetic/stored.txt",
    contents: Buffer.from("Synthetic stored member.\n", "utf8"),
    method: ZIP_METHOD_STORE,
});

const DEFLATED_MEMBER = Object.freeze({
    path: "synthetic/deflated.txt",
    contents: Buffer.from("Synthetic raw-deflate member.\n", "utf8"),
    method: ZIP_METHOD_DEFLATE,
});

// Keep the archive larger than one 64 KiB adapter read so the success path
// also covers assembly of a terminal ZIP window whose absolute offset is not
// zero. This member is enumerated but is not one of the representative methods
// selected for decoding.
const TAIL_WINDOW_PADDING_MEMBER = Object.freeze({
    path: "synthetic/tail-window-padding.bin",
    contents: Buffer.alloc(70 * 1024, 0xa5),
    method: ZIP_METHOD_STORE,
});

function expectedMember(entry)
{
    return {
        path: entry.path,
        method: entry.method,
        size: entry.contents.length,
        crc32: crc32(entry.contents) >>> 0,
    };
}

async function createInstallDirectory(testInfo, name, iwd)
{
    return createM12InstallDirectory(testInfo, name, { primaryIwd: iwd });
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

async function observeArchive(page, { readDelayMilliseconds = 0 } = {})
{
    await page.addInitScript(({ delay }) => {
        globalThis.__syntheticArchiveEvents = [];
        globalThis.__syntheticArchiveReadDelays = [];
        globalThis.__syntheticArchivePublishSamples = [];
        globalThis.__syntheticArchiveRafTicks = 0;
        const countFrame = () => {
            globalThis.__syntheticArchiveRafTicks += 1;
            const pendingDetail = globalThis.__KISAKCOD_ARCHIVE_DETAIL__;
            if (pendingDetail) {
                globalThis.__syntheticArchivePublishSamples.push({
                    frame: globalThis.__syntheticArchiveRafTicks,
                    entries: pendingDetail.entries.length,
                });
            }
            globalThis.requestAnimationFrame(countFrame);
        };
        globalThis.requestAnimationFrame(countFrame);
        globalThis.addEventListener("kisakcod:archive", (event) => {
            globalThis.__syntheticArchiveEvents.push(structuredClone(event.detail));
        });

        if (delay <= 0) {
            return;
        }
        const originalArrayBuffer = Blob.prototype.arrayBuffer;
        Object.defineProperty(Blob.prototype, "arrayBuffer", {
            configurable: true,
            writable: true,
            async value() {
                const runtime = globalThis.__KISAKCOD_WEB__;
                const archiveState = runtime?.archive?.state;
                const isArchiveRead = runtime?.assets?.state === "ready" ||
                    (archiveState && archiveState !== "ready" && archiveState !== "failed");
                if (!isArchiveRead) {
                    return originalArrayBuffer.call(this);
                }

                const beforeFrame = globalThis.__syntheticArchiveRafTicks;
                await new Promise((resolve) => globalThis.setTimeout(resolve, delay));
                const bytes = await originalArrayBuffer.call(this);
                globalThis.__syntheticArchiveReadDelays.push({
                    beforeFrame,
                    afterFrame: globalThis.__syntheticArchiveRafTicks,
                    size: this.size,
                });
                return bytes;
            },
        });
    }, { delay: readDelayMilliseconds });
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

async function waitForArchive(page, state)
{
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.archive?.state,
    ), { message: `the synthetic archive should reach ${state}` }).toBe(state);
}

async function chooseDirectory(page, directory)
{
    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#select-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(directory);
}

function collectAssetNetworkRequests(page)
{
    const requests = [];
    page.on("request", (request) => {
        const url = new URL(request.url());
        const pathName = url.pathname.toLowerCase();
        if (url.protocol !== "data:" && url.protocol !== "blob:" &&
            (url.origin !== "http://127.0.0.1:8000" ||
                pathName.endsWith("/localization.txt") || pathName.endsWith(".iwd") ||
                pathName.endsWith(".ff"))) {
            requests.push(`${request.method()} ${request.url()}`);
        }
    });
    return requests;
}

function entryPath(entry)
{
    return typeof entry === "string" ? entry : entry.path;
}

test("enumerates and verifies stored and deflated members without blocking frames", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeArchive(page, { readDelayMilliseconds: 80 });
    const archive = createSyntheticIwd([
        STORED_MEMBER,
        DEFLATED_MEMBER,
        TAIL_WINDOW_PADDING_MEMBER,
    ]);
    const directory = await createInstallDirectory(testInfo, "archive-success", archive);
    const assetNetworkRequests = collectAssetNetworkRequests(page);

    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    await chooseDirectory(page, directory);
    await waitForAssets(page, "ready");
    await waitForArchive(page, "ready");

    const result = await page.evaluate(() => ({
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
        events: globalThis.__syntheticArchiveEvents,
        delayedReads: globalThis.__syntheticArchiveReadDelays,
    }));
    expect(result.archive.entries.map(entryPath)).toEqual([
        STORED_MEMBER.path,
        DEFLATED_MEMBER.path,
        TAIL_WINDOW_PADDING_MEMBER.path,
    ]);
    expect(result.archive.verifiedMembers).toHaveLength(2);
    expect(result.archive.verifiedMembers).toEqual(expect.arrayContaining([
        expect.objectContaining(expectedMember(STORED_MEMBER)),
        expect.objectContaining(expectedMember(DEFLATED_MEMBER)),
    ]));
    expect(result.events.some((event) => event.state === "ready")).toBe(true);
    expect(result.delayedReads.length).toBeGreaterThan(0);
    expect(result.delayedReads.some(
        ({ beforeFrame, afterFrame }) => afterFrame > beforeFrame,
    )).toBe(true);
    expect(assetNetworkRequests).toEqual([]);
});

test("publishes a large archive index incrementally without changing the ready detail", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeArchive(page);
    const bulkMembers = Array.from({ length: 1024 }, (_, index) => ({
        path: `synthetic/bulk/entry-${index.toString().padStart(4, "0")}.txt`,
        contents: Buffer.alloc(0),
        method: ZIP_METHOD_STORE,
    }));
    const entries = [STORED_MEMBER, DEFLATED_MEMBER, ...bulkMembers];
    const archive = createSyntheticIwd(entries);
    const directory = await createInstallDirectory(testInfo, "archive-incremental-ready", archive);

    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    await chooseDirectory(page, directory);
    await waitForAssets(page, "ready");
    await waitForArchive(page, "ready");

    const result = await page.evaluate(() => ({
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
        events: globalThis.__syntheticArchiveEvents,
        publishSamples: globalThis.__syntheticArchivePublishSamples,
    }));
    expect(result.archive).toMatchObject({
        state: "ready",
        recordCount: entries.length,
        uniqueEntries: entries.length,
    });
    expect(result.archive.entries.map(entryPath)).toEqual(entries.map((entry) => entry.path));
    expect(result.archive.verifiedMembers).toEqual(expect.arrayContaining([
        expect.objectContaining(expectedMember(STORED_MEMBER)),
        expect.objectContaining(expectedMember(DEFLATED_MEMBER)),
    ]));
    expect(result.events.filter((event) => event.state === "ready")).toHaveLength(1);
    expect(result.publishSamples.length).toBeGreaterThan(2);
    expect(new Set(result.publishSamples.map(({ frame }) => frame)).size).toBeGreaterThan(2);
    expect(Math.max(...result.publishSamples.map(({ entries: count }) => count))).toBeGreaterThan(
        Math.min(...result.publishSamples.map(({ entries: count }) => count)),
    );
});

test("reports a member CRC failure without invalidating the outer asset import", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeArchive(page);
    const expectedCrc = crc32(DEFLATED_MEMBER.contents) >>> 0;
    const archive = createSyntheticIwd([
        STORED_MEMBER,
        {
            ...DEFLATED_MEMBER,
            declaredCrc32: (expectedCrc ^ 1) >>> 0,
        },
    ]);
    const directory = await createInstallDirectory(testInfo, "archive-crc-failure", archive);
    const assetNetworkRequests = collectAssetNetworkRequests(page);

    await page.goto("/");
    await waitForEngine(page);
    await waitForAssets(page, "empty");
    await chooseDirectory(page, directory);
    await waitForAssets(page, "ready");
    await waitForArchive(page, "failed");

    const result = await page.evaluate(() => ({
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
        assets: structuredClone(globalThis.__KISAKCOD_WEB__.assets),
        events: globalThis.__syntheticArchiveEvents,
    }));
    expect(JSON.stringify(result.archive)).toMatch(/crc/i);
    expect(result.events.some((event) => event.state === "failed")).toBe(true);
    expect(result.assets).toMatchObject({
        state: "ready",
        manifest: { archiveProbe: { entriesDeclared: 22, archivesProbed: 21 } },
    });
    await expect(page.locator("#asset-state-label")).toHaveText("Local installation ready");
    expect(assetNetworkRequests).toEqual([]);
});
