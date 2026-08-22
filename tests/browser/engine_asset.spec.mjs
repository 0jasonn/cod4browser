import { expect, test } from "@playwright/test";
import { installGate2OracleRequest } from "./gate2_oracle.mjs";
import { crc32 } from "node:zlib";
import { createInstallDirectory as createM12InstallDirectory } from "./install_fixture.mjs";
import {
    createSyntheticIwd,
    createSyntheticIwi,
    IWI_FLAG_NO_MIPMAPS,
    IWI_FORMAT_ARGB,
    IWI_FORMAT_DXT1,
    ZIP_METHOD_DEFLATE,
    ZIP_METHOD_STORE,
} from "./synthetic_iwd.mjs";

const ENGINE_ASSET_CACHE_LIMIT = 4 * 1024 * 1024;
const IWI_PATH = "images/synthetic_engine_asset.iwi";
function expectProductionWorldSurfaceUploads(uploads)
{
    const vertexUpload = uploads.find(
        (upload) => upload.target === "array" && upload.byteLength === 288,
    );
    const indexUpload = uploads.find(
        (upload) => upload.target === "element-array" && upload.byteLength === 12,
    );
    expect(vertexUpload, "the converted world vertices should be uploaded").toBeTruthy();
    expect(indexUpload, "the converted world indices should be uploaded").toBeTruthy();
    expect(vertexUpload.bytes).toHaveLength(288);
    expect(indexUpload.bytes).toHaveLength(12);

    const vertexValues = new Float32Array(Uint8Array.from(vertexUpload.bytes).buffer);
    const expectedVertices = [
        [-0.5, 0.5, 0, 224 / 255, 96 / 255, 32 / 255, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [-0.5, -0.5, 0, 48 / 255, 176 / 255, 80 / 255, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0],
        [0.5, -0.5, 0, 64 / 255, 112 / 255, 232 / 255, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0],
        [0.5, 0.5, 0, 240 / 255, 208 / 255, 72 / 255, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0],
    ].flat();
    expect([...vertexValues]).toHaveLength(expectedVertices.length);
    for (let index = 0; index < expectedVertices.length; index += 1) {
        expect(vertexValues[index]).toBeCloseTo(expectedVertices[index], 6);
    }

    const indexValues = new Uint16Array(Uint8Array.from(indexUpload.bytes).buffer);
    expect([...indexValues]).toEqual([0, 1, 2, 2, 3, 0]);
}

// These members make the existing representative archive verification choose
// known-good stored and deflated streams before it reaches a deliberately
// malformed or oversized IWI used by the engine-facing cache tests.
const ARCHIVE_SENTINELS = Object.freeze([
    {
        path: "synthetic/archive-sentinel-stored.txt",
        contents: "Synthetic stored archive sentinel.\n",
        method: ZIP_METHOD_STORE,
    },
    {
        path: "synthetic/archive-sentinel-deflated.txt",
        contents: "Synthetic deflated archive sentinel.\n",
        method: ZIP_METHOD_DEFLATE,
    },
]);

function deterministicBytes(length)
{
    const bytes = Buffer.allocUnsafe(length);
    let state = 0x6d2b79f5;
    for (let index = 0; index < bytes.length; ++index) {
        state ^= state << 13;
        state ^= state >>> 17;
        state ^= state << 5;
        bytes[index] = state & 0xff;
    }
    return bytes;
}

function engineArchive(entries)
{
    return createSyntheticIwd([...ARCHIVE_SENTINELS, ...entries]);
}

async function createInstallDirectory(testInfo, name, archive)
{
    return createM12InstallDirectory(testInfo, name, { primaryIwd: archive });
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

async function observeEngineAsset(page)
{
    await installGate2OracleRequest(page);
    await page.addInitScript(() => {
        globalThis.__syntheticEngineAssetEvents = [];
        globalThis.__syntheticRendererTextureEvents = [];
        globalThis.__syntheticRendererSurfaceEvents = [];
        globalThis.__syntheticEngineWorldSurfaceEvents = [];
        globalThis.__syntheticEngineArchiveEvents = [];
        globalThis.__syntheticEngineAssetReadSamples = [];
        globalThis.__syntheticEngineAssetRafTicks = 0;
        globalThis.__syntheticAllBlobArrayBufferReads = 0;
        globalThis.__syntheticTexImage2DCalls = [];
        globalThis.__syntheticSurfaceBufferUploads = [];
        globalThis.__syntheticSurfaceDrawElementsCount = 0;
        globalThis.__syntheticSurfaceDrawArraysCount = 0;
        globalThis.__syntheticSurfaceLastDraw = null;
        let observedGeneration = null;
        const countFrame = () => {
            globalThis.__syntheticEngineAssetRafTicks += 1;
            globalThis.requestAnimationFrame(countFrame);
        };
        globalThis.requestAnimationFrame(countFrame);

        globalThis.addEventListener("kisakcod:engine-asset", (event) => {
            const detail = {
                ...structuredClone(event.detail),
                observedRafTick: globalThis.__syntheticEngineAssetRafTicks,
            };
            globalThis.__syntheticEngineAssetEvents.push(detail);
            if (detail.state === "loading") {
                observedGeneration = detail.generation;
            }
        });
        globalThis.addEventListener("kisakcod:renderer-texture", (event) => {
            globalThis.__syntheticRendererTextureEvents.push({
                ...structuredClone(event.detail),
                observedRafTick: globalThis.__syntheticEngineAssetRafTicks,
            });
        });
        globalThis.addEventListener("kisakcod:renderer-surface", (event) => {
            globalThis.__syntheticRendererSurfaceEvents.push({
                ...structuredClone(event.detail),
                observedRafTick: globalThis.__syntheticEngineAssetRafTicks,
            });
        });
        globalThis.addEventListener("kisakcod:engine-world-surface", (event) => {
            globalThis.__syntheticEngineWorldSurfaceEvents.push({
                ...structuredClone(event.detail),
                observedRafTick: globalThis.__syntheticEngineAssetRafTicks,
            });
        });
        globalThis.addEventListener("kisakcod:archive", (event) => {
            globalThis.__syntheticEngineArchiveEvents.push(structuredClone(event.detail));
        });

        const originalArrayBuffer = Blob.prototype.arrayBuffer;
        Object.defineProperty(Blob.prototype, "arrayBuffer", {
            configurable: true,
            writable: true,
            async value() {
                globalThis.__syntheticAllBlobArrayBufferReads += 1;
                const isEngineAssetRead = observedGeneration !== null;
                if (!isEngineAssetRead) {
                    return originalArrayBuffer.call(this);
                }

                const beforeFrame = globalThis.__syntheticEngineAssetRafTicks;
                const bytes = await originalArrayBuffer.call(this);
                globalThis.__syntheticEngineAssetReadSamples.push({
                    generation: observedGeneration,
                    beforeFrame,
                    afterFrame: globalThis.__syntheticEngineAssetRafTicks,
                    size: this.size,
                });
                return bytes;
            },
        });

        const originalTexImage2D = WebGL2RenderingContext.prototype.texImage2D;
        Object.defineProperty(WebGL2RenderingContext.prototype, "texImage2D", {
            configurable: true,
            writable: true,
            value(...args) {
                if (this.canvas?.id === "game-canvas") {
                    globalThis.__syntheticTexImage2DCalls.push({
                        width: Number(args[3]),
                        height: Number(args[4]),
                    });
                }
                return originalTexImage2D.apply(this, args);
            },
        });

        const originalBufferData = WebGL2RenderingContext.prototype.bufferData;
        Object.defineProperty(WebGL2RenderingContext.prototype, "bufferData", {
            configurable: true,
            writable: true,
            value(...args) {
                if (this.canvas?.id === "game-canvas") {
                    const source = args[1];
                    let byteLength = typeof source === "number"
                        ? Number(source)
                        : Number(source?.byteLength ?? 0);
                    let bytes = null;
                    if (typeof source !== "number" && source != null) {
                        try {
                            if (ArrayBuffer.isView(source)) {
                                const bytesPerElement = Number(source.BYTES_PER_ELEMENT ?? 1);
                                const sourceLength = Number(
                                    source.length ?? source.byteLength / bytesPerElement,
                                );
                                const sourceOffset = args.length >= 4 ? Number(args[3]) : 0;
                                const elementCount = args.length >= 5
                                    ? Number(args[4])
                                    : sourceLength - sourceOffset;
                                const snapshotOffset = source.byteOffset +
                                    sourceOffset * bytesPerElement;
                                byteLength = elementCount * bytesPerElement;
                                if (byteLength <= 4096) {
                                    bytes = Array.from(new Uint8Array(
                                        source.buffer,
                                        snapshotOffset,
                                        byteLength,
                                    ));
                                }
                            } else if (source instanceof ArrayBuffer) {
                                byteLength = source.byteLength;
                                if (byteLength <= 4096) {
                                    bytes = Array.from(new Uint8Array(source));
                                }
                            }
                        } catch {
                            // The observer must not alter bufferData behavior when
                            // an unfamiliar overload or source view is encountered.
                            bytes = null;
                        }
                    }
                    globalThis.__syntheticSurfaceBufferUploads.push({
                        target: args[0] === this.ARRAY_BUFFER
                            ? "array"
                            : args[0] === this.ELEMENT_ARRAY_BUFFER
                                ? "element-array"
                                : Number(args[0]),
                        byteLength,
                        bytes,
                    });
                }
                return originalBufferData.apply(this, args);
            },
        });

        const originalDrawElements = WebGL2RenderingContext.prototype.drawElements;
        Object.defineProperty(WebGL2RenderingContext.prototype, "drawElements", {
            configurable: true,
            writable: true,
            value(mode, count, type, offset) {
                if (this.canvas?.id === "game-canvas") {
                    globalThis.__syntheticSurfaceDrawElementsCount += 1;
                    globalThis.__syntheticSurfaceLastDraw = {
                        mode: mode === this.TRIANGLES ? "triangles" : Number(mode),
                        count: Number(count),
                        type: type === this.UNSIGNED_SHORT ? "uint16" : Number(type),
                        offset: Number(offset),
                    };
                }
                return originalDrawElements.call(this, mode, count, type, offset);
            },
        });

        const originalDrawArrays = WebGL2RenderingContext.prototype.drawArrays;
        Object.defineProperty(WebGL2RenderingContext.prototype, "drawArrays", {
            configurable: true,
            writable: true,
            value(...args) {
                if (this.canvas?.id === "game-canvas") {
                    globalThis.__syntheticSurfaceDrawArraysCount += 1;
                }
                return originalDrawArrays.apply(this, args);
            },
        });
    });
}

async function waitForRuntime(page)
{
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state,
    )).toBe("running");
}

async function waitForState(page, property, state)
{
    await expect.poll(() => page.evaluate(
        ({ key }) => globalThis.__KISAKCOD_WEB__?.[key]?.state,
        { key: property },
    ), { message: `${property} should reach ${state}` }).toBe(state);
}

async function chooseDirectory(page, directory)
{
    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#select-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(directory);
}

async function importArchive(page, directory)
{
    await page.goto("/");
    await waitForRuntime(page);
    await waitForState(page, "assets", "empty");
    await chooseDirectory(page, directory);
    await waitForState(page, "assets", "ready");
    await waitForState(page, "archive", "ready");
}

function expectedReady(pathName, contents, compressionMethod, metadata)
{
    return {
        state: "ready",
        path: pathName,
        kind: "iwi",
        size: contents.length,
        compressionMethod,
        crc32: crc32(contents) >>> 0,
        format: metadata.format,
        flags: metadata.flags,
        width: metadata.width,
        height: metadata.height,
        depth: metadata.depth,
        mipCount: metadata.mipCount,
        cacheRetainedBytes: 0,
    };
}

test("loads a stored IWI through the engine cache and releases its bytes", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeEngineAsset(page);
    const storedBgra = [
        [0x17, 0x6d, 0xe3, 0xff],
        [0x24, 0xd1, 0x39, 0xff],
        [0xf0, 0x47, 0x22, 0xff],
        [0x09, 0xb8, 0xf4, 0xff],
    ];
    const iwi = createSyntheticIwi({
        payload: Buffer.from(storedBgra.flat()),
    });
    const archive = engineArchive([{
        path: IWI_PATH,
        contents: iwi,
        method: ZIP_METHOD_STORE,
    }]);
    const directory = await createInstallDirectory(testInfo, "engine-iwi-stored", archive);

    await importArchive(page, directory);
    await waitForState(page, "engineAsset", "ready");
    await waitForState(page, "rendererTexture", "ready");

    const result = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        rendererTexture: structuredClone(globalThis.__KISAKCOD_WEB__.rendererTexture),
        events: globalThis.__syntheticEngineAssetEvents,
        rendererEvents: structuredClone(globalThis.__syntheticRendererTextureEvents),
        engineWorldSurface: structuredClone(globalThis.__KISAKCOD_WEB__.engineWorldSurface),
        engineWorldSurfaceEvents: structuredClone(
            globalThis.__syntheticEngineWorldSurfaceEvents,
        ),
        rendererSurface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
        surfaceEvents: structuredClone(globalThis.__syntheticRendererSurfaceEvents),
        surfaceBufferUploads: structuredClone(globalThis.__syntheticSurfaceBufferUploads),
        surfaceDrawElementsCount: globalThis.__syntheticSurfaceDrawElementsCount,
        surfaceDrawArraysCount: globalThis.__syntheticSurfaceDrawArraysCount,
        surfaceLastDraw: structuredClone(globalThis.__syntheticSurfaceLastDraw),
        blobReads: globalThis.__syntheticAllBlobArrayBufferReads,
        texImage2DCalls: structuredClone(globalThis.__syntheticTexImage2DCalls),
    }));
    expect(result.engineAsset).toMatchObject(expectedReady(IWI_PATH, iwi, ZIP_METHOD_STORE, {
        format: IWI_FORMAT_ARGB,
        flags: IWI_FLAG_NO_MIPMAPS,
        width: 2,
        height: 2,
        depth: 1,
        mipCount: 1,
    }));
    expect(result.engineAsset).not.toHaveProperty("bytes");
    expect(result.engineAsset).not.toHaveProperty("data");
    expect(result.rendererTexture).toMatchObject({
        state: "ready",
        generation: result.engineAsset.generation,
        path: IWI_PATH,
        sourceFormat: IWI_FORMAT_ARGB,
        width: 2,
        height: 2,
        mipLevel: 0,
        payloadBytes: 16,
        gpuFormat: "rgba8",
        recoveryBytes: 16,
        resident: true,
    });
    expect(result.rendererTexture).not.toHaveProperty("pixels");
    expect(result.engineWorldSurface).toMatchObject({
        state: "ready",
        pipelineStage: "complete",
        sourceRepresentation: "fastfile-gfxworld",
        sourceContainer: "IWffu100",
        vertexFormat: "base-world",
        projection: "affine-world-to-clip-2d",
        synthetic: true,
        fastfileVersion: 5,
        inflatedBytes: 1246,
        declaredZoneBytes: 1112,
        zoneBlock0Bytes: 732,
        zoneBlock4Bytes: 380,
        sourceAssetCount: 2,
        materialAssetIndex: 0,
        worldAssetIndex: 1,
        materialIdentity: 1,
        worldIdentity: 2,
        registeredAssetCount: 2,
        sourceSurfaceIndex: 0,
        worldVertexCount: 6,
        worldIndexCount: 12,
        worldSurfaceCount: 1,
        firstVertex: 1,
        vertexCount: 4,
        baseIndex: 3,
        triangleCount: 2,
        materialReferenceKind: "alias-to-inline-shared",
        materialName: "web/synthetic",
        convertedVertexCount: 4,
        convertedIndexCount: 6,
    });
    expect(result.engineWorldSurface.extractionGeneration).toBeGreaterThan(0);
    expect(result.engineWorldSurface.conversionGeneration).toBeGreaterThan(0);
    expect(result.engineWorldSurface.compressedBytes).toBeGreaterThan(0);
    expect(result.engineWorldSurface.fastfileBytes)
        .toBe(result.engineWorldSurface.compressedBytes + 12);
    expect(result.engineWorldSurface).toMatchObject({
        maxStepBytes: 64 * 1024,
        maxStepRecords: 64,
        maxSourceChunkBytes: 37,
        needsSource: false,
        compressedBytesConsumed: result.engineWorldSurface.compressedBytes,
        inflatedBytesProduced: result.engineWorldSurface.inflatedBytes,
        parsedBytes: 1434,
    });
    expect(result.engineWorldSurface.sourceBytesReceived)
        .toBe(result.engineWorldSurface.fastfileBytes);
    expect(result.engineWorldSurface.sourceBytesConsumed)
        .toBe(result.engineWorldSurface.fastfileBytes);
    expect(result.engineWorldSurface.sourceFeedCount)
        .toBe(Math.ceil(result.engineWorldSurface.fastfileBytes / 37));
    expect(result.engineWorldSurface.sourceFeedCount).toBeGreaterThan(1);
    expect(result.engineWorldSurface.stepCount).toBeGreaterThanOrEqual(2);
    expect(result.engineWorldSurface.recordsProcessed).toBeGreaterThan(0);
    expect(Object.values(result.engineWorldSurface).some(Array.isArray)).toBe(false);
    expect(Object.keys(result.engineWorldSurface).some(
        (key) => /(?:vertices|indices|pointer|ptr)$/i.test(key),
    )).toBe(false);
    expect(result.engineWorldSurfaceEvents.filter(
        (event) => event.state === "ready",
    )).toHaveLength(1);
    const sourceWaitEvents = result.engineWorldSurfaceEvents.filter(
        (event) => event.state === "loading" && event.pipelineStage === "source-wait",
    );
    expect(sourceWaitEvents.length).toBeGreaterThan(1);
    expect(sourceWaitEvents.every(
        (event) => event.needsSource && event.stepSourceBytes === 0,
    )).toBe(true);
    expect(result.rendererSurface).toMatchObject({
        state: "ready",
        vertexCount: 4,
        indexCount: 6,
        drawFirstIndex: 0,
        drawIndexCount: 6,
        vertexBytes: 288,
        indexBytes: 12,
        recoveryBytes: 300,
        drawCount: 1,
        topology: "triangle-list",
        textureBinding: "engine-image",
        resident: true,
    });
    expect(result.rendererSurface).not.toHaveProperty("vertices");
    expect(result.rendererSurface).not.toHaveProperty("indices");
    expect(result.events.some((event) => event.state === "loading")).toBe(true);
    expect(result.events.some((event) => event.state === "ready")).toBe(true);
    const resourceGeneration = result.rendererTexture.resourceGeneration;
    const baselineRecoveryCount = result.rendererTexture.recoveryCount;
    const rendererEventCount = result.rendererEvents.length;
    const surfaceResourceGeneration = result.rendererSurface.resourceGeneration;
    const surfaceRecoveryCount = result.rendererSurface.recoveryCount;
    const surfaceSubmissionGeneration = result.rendererSurface.submissionGeneration;
    const surfaceEventCount = result.surfaceEvents.length;
    const engineWorldSurfaceEventCount = result.engineWorldSurfaceEvents.length;
    const worldSurfaceConversionGeneration =
        result.engineWorldSurface.conversionGeneration;
    const worldSurfaceExtractionGeneration =
        result.engineWorldSurface.extractionGeneration;
    const worldSurfaceStepCount = result.engineWorldSurface.stepCount;
    const worldSurfaceCompressedBytesConsumed =
        result.engineWorldSurface.compressedBytesConsumed;
    const worldSurfaceInflatedBytesProduced =
        result.engineWorldSurface.inflatedBytesProduced;
    const worldSurfaceParsedBytes = result.engineWorldSurface.parsedBytes;
    const worldSurfaceRecordsProcessed = result.engineWorldSurface.recordsProcessed;
    const canLoseContext = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestLoseWebGLContext"));
    expect(Boolean(canLoseContext)).toBe(true);
    await waitForState(page, "rendererTexture", "lost");
    await waitForState(page, "rendererSurface", "lost");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state,
    )).toBe("renderer-lost");

    await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestRestoreWebGLContext"));
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.state,
    )).toBe("running");
    await waitForState(page, "rendererTexture", "ready");
    await waitForState(page, "rendererSurface", "ready");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.rendererSurface?.resourceGeneration,
    )).toBeGreaterThan(surfaceResourceGeneration);
    const recovered = await page.evaluate(() => ({
        rendererTexture: structuredClone(globalThis.__KISAKCOD_WEB__.rendererTexture),
        rendererEvents: structuredClone(globalThis.__syntheticRendererTextureEvents),
        engineWorldSurface: structuredClone(globalThis.__KISAKCOD_WEB__.engineWorldSurface),
        engineWorldSurfaceEvents: structuredClone(
            globalThis.__syntheticEngineWorldSurfaceEvents,
        ),
        rendererSurface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
        surfaceEvents: structuredClone(globalThis.__syntheticRendererSurfaceEvents),
        surfaceBufferUploads: structuredClone(globalThis.__syntheticSurfaceBufferUploads),
        surfaceDrawArraysCount: globalThis.__syntheticSurfaceDrawArraysCount,
        surfaceLastDraw: structuredClone(globalThis.__syntheticSurfaceLastDraw),
        texImage2DCalls: structuredClone(globalThis.__syntheticTexImage2DCalls),
        blobReads: globalThis.__syntheticAllBlobArrayBufferReads,
    }));
    expect(recovered.rendererTexture).toMatchObject({
        generation: result.engineAsset.generation,
        path: IWI_PATH,
        recoveryBytes: 16,
        resident: true,
    });
    expect(recovered.rendererTexture.recoveryCount)
        .toBeGreaterThanOrEqual(baselineRecoveryCount + 1);
    expect(recovered.rendererTexture.resourceGeneration).toBeGreaterThan(resourceGeneration);
    expect(recovered.blobReads).toBe(result.blobReads);
    expect(recovered.rendererSurface).toMatchObject({
        state: "ready",
        vertexCount: 4,
        indexCount: 6,
        recoveryBytes: 300,
        submissionGeneration: surfaceSubmissionGeneration,
        resident: true,
    });
    expect(recovered.rendererSurface.resourceGeneration)
        .toBeGreaterThan(surfaceResourceGeneration);
    expect(recovered.rendererSurface.recoveryCount).toBe(surfaceRecoveryCount + 1);

    const lifecycle = recovered.rendererEvents.slice(rendererEventCount);
    const lostIndex = lifecycle.findIndex((event) => event.state === "lost");
    const readyIndex = lifecycle.findIndex(
        (event, index) => index > lostIndex && event.state === "ready",
    );
    expect(lostIndex).toBeGreaterThanOrEqual(0);
    expect(readyIndex).toBeGreaterThan(lostIndex);
    expect(lifecycle[lostIndex]).toMatchObject({
        recoveryBytes: 16,
        resident: false,
    });
    expect(lifecycle[lostIndex].recoveryCount)
        .toBeGreaterThanOrEqual(baselineRecoveryCount);
    expect(lifecycle[lostIndex].resourceGeneration)
        .toBeGreaterThanOrEqual(resourceGeneration);
    expect(lifecycle[readyIndex]).toMatchObject({
        recoveryBytes: 16,
        resident: true,
    });
    expect(lifecycle[readyIndex].recoveryCount)
        .toBe(lifecycle[lostIndex].recoveryCount + 1);
    expect(lifecycle[readyIndex].resourceGeneration)
        .toBeGreaterThan(lifecycle[lostIndex].resourceGeneration);

    const surfaceLifecycle = recovered.surfaceEvents.slice(surfaceEventCount);
    const surfaceLostIndex = surfaceLifecycle.findIndex((event) => event.state === "lost");
    const surfaceReadyIndex = surfaceLifecycle.findIndex(
        (event, index) => index > surfaceLostIndex && event.state === "ready",
    );
    expect(surfaceLostIndex).toBeGreaterThanOrEqual(0);
    expect(surfaceReadyIndex).toBeGreaterThan(surfaceLostIndex);
    expect(surfaceLifecycle[surfaceLostIndex]).toMatchObject({
        recoveryBytes: 300,
        submissionGeneration: surfaceSubmissionGeneration,
        resident: false,
    });
    expect(surfaceLifecycle[surfaceReadyIndex]).toMatchObject({
        recoveryBytes: 300,
        submissionGeneration: surfaceSubmissionGeneration,
        resident: true,
    });
    expect(recovered.engineWorldSurface).toEqual(result.engineWorldSurface);
    expect(recovered.engineWorldSurface.extractionGeneration)
        .toBe(worldSurfaceExtractionGeneration);
    expect(recovered.engineWorldSurface.conversionGeneration)
        .toBe(worldSurfaceConversionGeneration);
    expect(recovered.engineWorldSurface.stepCount).toBe(worldSurfaceStepCount);
    expect(recovered.engineWorldSurface.compressedBytesConsumed)
        .toBe(worldSurfaceCompressedBytesConsumed);
    expect(recovered.engineWorldSurface.inflatedBytesProduced)
        .toBe(worldSurfaceInflatedBytesProduced);
    expect(recovered.engineWorldSurface.parsedBytes).toBe(worldSurfaceParsedBytes);
    expect(recovered.engineWorldSurface.recordsProcessed).toBe(worldSurfaceRecordsProcessed);
    expect(recovered.engineWorldSurfaceEvents).toHaveLength(engineWorldSurfaceEventCount);

});

test("streams a deflated IWI while animation frames advance", { tag: "@smoke" }, async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeEngineAsset(page);
    const iwi = createSyntheticIwi({
        width: 384,
        height: 256,
        payload: deterministicBytes(384 * 256 * 4),
    });
    const archive = engineArchive([{
        path: IWI_PATH,
        contents: iwi,
        method: ZIP_METHOD_DEFLATE,
    }]);
    const directory = await createInstallDirectory(testInfo, "engine-iwi-deflated", archive);

    await importArchive(page, directory);
    await waitForState(page, "engineAsset", "ready");
    await waitForState(page, "rendererTexture", "ready");

    const result = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        rendererTexture: structuredClone(globalThis.__KISAKCOD_WEB__.rendererTexture),
        events: structuredClone(globalThis.__syntheticEngineAssetEvents),
        reads: globalThis.__syntheticEngineAssetReadSamples,
    }));
    expect(result.engineAsset).toMatchObject(expectedReady(IWI_PATH, iwi, ZIP_METHOD_DEFLATE, {
        format: IWI_FORMAT_ARGB,
        flags: IWI_FLAG_NO_MIPMAPS,
        width: 384,
        height: 256,
        depth: 1,
        mipCount: 1,
    }));
    const loadingFrame = result.events.find((event) => event.state === "loading")
        ?.observedRafTick;
    const readyFrame = result.events.findLast((event) => event.state === "ready")
        ?.observedRafTick;
    expect(readyFrame).toBeGreaterThan(loadingFrame);
    expect(result.rendererTexture).toMatchObject({
        state: "ready",
        generation: result.engineAsset.generation,
        width: 384,
        height: 256,
        payloadBytes: 384 * 256 * 4,
        recoveryBytes: 384 * 256 * 4,
        resident: true,
    });
});

test("a synchronous ready listener cannot publish texture state after cancellation", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeEngineAsset(page);
    const iwi = createSyntheticIwi();
    const archive = engineArchive([{
        path: IWI_PATH,
        contents: iwi,
        method: ZIP_METHOD_STORE,
    }]);
    const directory = await createInstallDirectory(
        testInfo,
        "engine-iwi-reentrant-cancel",
        archive,
    );

    await page.goto("/");
    await waitForRuntime(page);
    await waitForState(page, "assets", "empty");
    await page.evaluate(() => {
        globalThis.__syntheticReentrantCancelGeneration = 0;
        globalThis.addEventListener("kisakcod:engine-asset", (event) => {
            if (event.detail.state !== "ready" ||
                globalThis.__syntheticReentrantCancelGeneration !== 0) {
                return;
            }
            globalThis.__syntheticReentrantCancelGeneration = event.detail.generation;
            globalThis.__KISAKCOD_WEB__.module._KisakWeb_CancelArchiveJob();
        });
    });
    await chooseDirectory(page, directory);
    await waitForState(page, "assets", "ready");
    await expect.poll(() => page.evaluate(
        () => globalThis.__syntheticReentrantCancelGeneration,
    )).toBeGreaterThan(0);
    await waitForState(page, "engineAsset", "idle");
    await waitForState(page, "rendererTexture", "idle");
    const frameAtCancel = await page.evaluate(
        () => globalThis.__syntheticEngineAssetRafTicks,
    );
    await expect.poll(() => page.evaluate(
        () => globalThis.__syntheticEngineAssetRafTicks,
    )).toBeGreaterThan(frameAtCancel + 2);

    const result = await page.evaluate(() => ({
        cancelledGeneration: globalThis.__syntheticReentrantCancelGeneration,
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        rendererTexture: structuredClone(globalThis.__KISAKCOD_WEB__.rendererTexture),
        rendererEvents: structuredClone(globalThis.__syntheticRendererTextureEvents),
        rendererSurface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
        surfaceEvents: structuredClone(globalThis.__syntheticRendererSurfaceEvents),
    }));
    expect(result.engineAsset.state).toBe("idle");
    expect(result.engineAsset.generation).toBeGreaterThan(result.cancelledGeneration);
    expect(result.rendererTexture).toMatchObject({
        state: "idle",
        generation: result.engineAsset.generation,
        recoveryBytes: 0,
        resident: false,
    });
    expect(result.rendererSurface).toMatchObject({
        state: "ready",
        vertexCount: 4,
        indexCount: 6,
        recoveryBytes: 300,
        resident: true,
    });
    expect(result.surfaceEvents.some((event) =>
        ["idle", "failed"].includes(event.state)
    )).toBe(false);
});

test("decodes a DXT1 IWI through the renderer-owned texture path", { tag: "@smoke" }, async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeEngineAsset(page);
    const iwi = createSyntheticIwi({
        format: IWI_FORMAT_DXT1,
        width: 4,
        height: 4,
        payload: Buffer.alloc(8, 0x5a),
    });
    const archive = engineArchive([{
        path: IWI_PATH,
        contents: iwi,
        method: ZIP_METHOD_DEFLATE,
    }]);
    const directory = await createInstallDirectory(
        testInfo,
        "engine-iwi-renderer-dxt1",
        archive,
    );

    await importArchive(page, directory);
    await waitForState(page, "engineAsset", "ready");
    await waitForState(page, "rendererTexture", "ready");
    const frameAtResult = await page.evaluate(
        () => globalThis.__syntheticEngineAssetRafTicks,
    );
    await expect.poll(() => page.evaluate(
        () => globalThis.__syntheticEngineAssetRafTicks,
    )).toBeGreaterThan(frameAtResult + 2);

    const result = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        rendererTexture: structuredClone(globalThis.__KISAKCOD_WEB__.rendererTexture),
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
        assets: structuredClone(globalThis.__KISAKCOD_WEB__.assets),
        runtimeState: globalThis.__KISAKCOD_WEB__.state,
        texImage2DCalls: structuredClone(globalThis.__syntheticTexImage2DCalls),
    }));
    expect(result.engineAsset).toMatchObject(expectedReady(
        IWI_PATH,
        iwi,
        ZIP_METHOD_DEFLATE,
        {
            format: IWI_FORMAT_DXT1,
            flags: IWI_FLAG_NO_MIPMAPS,
            width: 4,
            height: 4,
            depth: 1,
            mipCount: 1,
        },
    ));
    expect(result.rendererTexture).toMatchObject({
        state: "ready",
        generation: result.engineAsset.generation,
        path: IWI_PATH,
        sourceFormat: IWI_FORMAT_DXT1,
        width: 4,
        height: 4,
        mipLevel: 0,
        payloadBytes: 64,
        gpuFormat: "rgba8",
        recoveryBytes: 64,
        resident: true,
    });
    expect(result.rendererTexture.message).toMatch(/uploaded|retained/i);
    expect(result.archive.state).toBe("ready");
    expect(result.assets.state).toBe("ready");
    expect(result.runtimeState).toBe("running");
});

test("rejects textures above the backend dimension floor before WebGL upload", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeEngineAsset(page);
    const width = 2049;
    const iwi = createSyntheticIwi({
        width,
        height: 1,
        payload: deterministicBytes(width * 4),
    });
    const archive = engineArchive([{
        path: IWI_PATH,
        contents: iwi,
        method: ZIP_METHOD_STORE,
    }]);
    const directory = await createInstallDirectory(
        testInfo,
        "engine-iwi-renderer-dimension-limit",
        archive,
    );

    await importArchive(page, directory);
    await waitForState(page, "engineAsset", "ready");

    const result = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        rendererTexture: structuredClone(globalThis.__KISAKCOD_WEB__.rendererTexture),
        archiveState: globalThis.__KISAKCOD_WEB__.archive.state,
        runtimeState: globalThis.__KISAKCOD_WEB__.state,
        texImage2DCalls: structuredClone(globalThis.__syntheticTexImage2DCalls),
    }));
    expect(result.engineAsset).toMatchObject(expectedReady(
        IWI_PATH,
        iwi,
        ZIP_METHOD_STORE,
        {
            format: IWI_FORMAT_ARGB,
            flags: IWI_FLAG_NO_MIPMAPS,
            width,
            height: 1,
            depth: 1,
            mipCount: 1,
        },
    ));
    expect(result.engineAsset).toMatchObject({
        rendererReplacementState: "unsupported",
    });
    expect(result.engineAsset.rendererReplacementMessage).toMatch(/dimension|backend limit/i);
    expect(result.rendererTexture).toMatchObject({
        state: "unsupported",
    });
    expect(result.texImage2DCalls.some(
        (call) => call.width === width && call.height === 1,
    )).toBe(false);
    expect(result.archiveState).toBe("ready");
    expect(result.runtimeState).toBe("running");
});

test("budgets a maximum-ratio cached IWI across animation frames", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeEngineAsset(page);
    const iwi = createSyntheticIwi({
        payload: Buffer.alloc(ENGINE_ASSET_CACHE_LIMIT - 28, 0x5a),
    });
    const archive = engineArchive([{
        path: IWI_PATH,
        contents: iwi,
        method: ZIP_METHOD_DEFLATE,
    }]);
    const directory = await createInstallDirectory(
        testInfo,
        "engine-iwi-high-ratio",
        archive,
    );

    await importArchive(page, directory);
    await waitForState(page, "engineAsset", "ready");

    const result = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        events: globalThis.__syntheticEngineAssetEvents,
    }));
    expect(result.engineAsset).toMatchObject(expectedReady(
        IWI_PATH,
        iwi,
        ZIP_METHOD_DEFLATE,
        {
            format: IWI_FORMAT_ARGB,
            flags: IWI_FLAG_NO_MIPMAPS,
            width: 2,
            height: 2,
            depth: 1,
            mipCount: 1,
        },
    ));
    const loading = result.events.find(
        (event) => event.state === "loading" &&
            event.generation === result.engineAsset.generation,
    );
    const ready = result.events.find(
        (event) => event.state === "ready" && event.generation === loading?.generation,
    );
    expect(loading).toBeTruthy();
    expect(ready).toBeTruthy();
    // A 4 MiB decoded member requires at least 64 bounded 64 KiB steps. Leave
    // headroom for event/frame ordering while still ruling out one-frame inflate.
    expect(ready.observedRafTick - loading.observedRafTick).toBeGreaterThanOrEqual(48);
});

test("reports a malformed IWI without invalidating the mounted archive", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeEngineAsset(page);
    const malformedIwi = createSyntheticIwi({ tag: "BAD" });
    const archive = engineArchive([{
        path: IWI_PATH,
        contents: malformedIwi,
        method: ZIP_METHOD_DEFLATE,
    }]);
    const directory = await createInstallDirectory(testInfo, "engine-iwi-malformed", archive);

    await importArchive(page, directory);
    await waitForState(page, "engineAsset", "failed");

    const result = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
        assets: structuredClone(globalThis.__KISAKCOD_WEB__.assets),
    }));
    expect(JSON.stringify(result.engineAsset)).toMatch(/tag|IW image|IWI/i);
    expect(result.archive.state).toBe("ready");
    expect(result.assets.state).toBe("ready");
});

test("rejects an IWI local header that overlaps the central directory", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeEngineAsset(page);
    const iwi = createSyntheticIwi();
    const archive = engineArchive([{
        path: IWI_PATH,
        contents: iwi,
        method: ZIP_METHOD_DEFLATE,
    }]);
    const localPathOffset = archive.indexOf(Buffer.from(IWI_PATH, "utf8"));
    expect(localPathOffset).toBeGreaterThanOrEqual(30);
    archive.writeUInt16LE(0xffff, localPathOffset - 2);
    const directory = await createInstallDirectory(
        testInfo,
        "engine-iwi-local-range",
        archive,
    );

    await importArchive(page, directory);
    const frameBeforeFailure = await page.evaluate(
        () => globalThis.__syntheticEngineAssetRafTicks,
    );
    await waitForState(page, "engineAsset", "failed");
    await expect.poll(() => page.evaluate(
        () => globalThis.__syntheticEngineAssetRafTicks,
    )).toBeGreaterThan(frameBeforeFailure + 2);

    const result = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        archiveState: globalThis.__KISAKCOD_WEB__.archive.state,
    }));
    expect(JSON.stringify(result.engineAsset)).toMatch(/header|range|archive/i);
    expect(result.archiveState).toBe("ready");
});

test("reports an archive with no IWI as unavailable", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeEngineAsset(page);
    const directory = await createInstallDirectory(
        testInfo,
        "engine-iwi-missing",
        engineArchive([]),
    );

    await importArchive(page, directory);
    await waitForState(page, "engineAsset", "unavailable");

    const result = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        archiveState: globalThis.__KISAKCOD_WEB__.archive.state,
        assetState: globalThis.__KISAKCOD_WEB__.assets.state,
    }));
    expect(JSON.stringify(result.engineAsset)).toMatch(/IWI|image/i);
    expect(result.archiveState).toBe("ready");
    expect(result.assetState).toBe("ready");
});

test("excludes an oversized IWI before allocating or decoding it", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeEngineAsset(page);
    const iwi = createSyntheticIwi();
    const archive = engineArchive([{
        path: IWI_PATH,
        contents: iwi,
        method: ZIP_METHOD_DEFLATE,
        declaredUncompressedSize: ENGINE_ASSET_CACHE_LIMIT + 1,
    }]);
    const directory = await createInstallDirectory(testInfo, "engine-iwi-oversized", archive);

    await importArchive(page, directory);
    await waitForState(page, "engineAsset", "unavailable");

    const result = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        archiveState: globalThis.__KISAKCOD_WEB__.archive.state,
        reads: globalThis.__syntheticEngineAssetReadSamples,
    }));
    expect(JSON.stringify(result.engineAsset)).toMatch(/bounded|limit|IWI|image/i);
    expect(result.archiveState).toBe("ready");
    expect(result.reads).toEqual([]);
});

test("a cancelled delayed cache read cannot publish ready after assets are cleared", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeEngineAsset(page);
    const iwi = createSyntheticIwi();
    const archive = engineArchive([{
        path: IWI_PATH,
        contents: iwi,
        method: ZIP_METHOD_DEFLATE,
    }]);
    const directory = await createInstallDirectory(testInfo, "engine-iwi-cancelled", archive);

    await page.goto("/");
    await waitForRuntime(page);
    await waitForState(page, "assets", "empty");
    await page.evaluate(() => {
        globalThis.__cancelledEngineAsset = null;
        globalThis.addEventListener("kisakcod:engine-asset", (event) => {
            if (event.detail.state !== "loading" || globalThis.__cancelledEngineAsset) return;
            globalThis.__cancelledEngineAsset = structuredClone(event.detail);
            globalThis.__KISAKCOD_WEB__.module._KisakWeb_CancelArchiveJob();
        });
    });
    await chooseDirectory(page, directory);
    await waitForState(page, "assets", "ready");
    await expect.poll(() => page.evaluate(() => globalThis.__cancelledEngineAsset))
        .not.toBeNull();
    await waitForState(page, "engineAsset", "idle");
    const started = await page.evaluate(() => globalThis.__cancelledEngineAsset);
    page.once("dialog", (dialog) => dialog.accept());
    await page.locator("#clear-assets-button").click();
    await waitForState(page, "assets", "empty");

    const releasedAt = await page.evaluate(
        () => globalThis.__syntheticEngineAssetRafTicks,
    );
    await expect.poll(() => page.evaluate(
        () => globalThis.__syntheticEngineAssetRafTicks,
    )).toBeGreaterThan(releasedAt + 2);

    const result = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        events: globalThis.__syntheticEngineAssetEvents,
    }));
    expect(started.generation).toBeGreaterThan(0);
    expect(result.engineAsset.state).toBe("idle");
    expect(result.events.filter(
        (event) => event.generation === started.generation && event.state === "ready",
    )).toEqual([]);
});

test("a stale engine read invalidates the mount without publishing a late ready result", async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeEngineAsset(page);
    const iwi = createSyntheticIwi();
    const archive = engineArchive([{
        path: IWI_PATH,
        contents: iwi,
        method: ZIP_METHOD_DEFLATE,
    }]);
    const directory = await createInstallDirectory(testInfo, "engine-iwi-stale", archive);

    await page.goto("/");
    await waitForRuntime(page);
    await waitForState(page, "assets", "empty");
    await page.evaluate(() => {
        globalThis.__invalidatedEngineAsset = null;
        globalThis.addEventListener("kisakcod:engine-asset", (event) => {
            if (event.detail.state !== "loading" || globalThis.__invalidatedEngineAsset) return;
            globalThis.__invalidatedEngineAsset = structuredClone(event.detail);
            void globalThis.__KISAKCOD_WEB__.filesystemBridge.invalidate();
        });
    });
    await chooseDirectory(page, directory);
    await waitForState(page, "assets", "ready");
    await expect.poll(() => page.evaluate(() => globalThis.__invalidatedEngineAsset))
        .not.toBeNull();
    const started = await page.evaluate(() => globalThis.__invalidatedEngineAsset);
    await waitForState(page, "engineAsset", "failed");
    await waitForState(page, "archive", "ready");
    const failedState = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
    }));
    const initialArchiveGeneration = failedState.archive.generation;

    const afterLateRead = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
        engineEvents: globalThis.__syntheticEngineAssetEvents,
        archiveEvents: globalThis.__syntheticEngineArchiveEvents,
    }));
    expect(failedState.engineAsset.state).toBe("failed");
    expect(JSON.stringify(failedState.engineAsset)).toMatch(/filesystem read|mount|stale/i);
    expect(failedState.archive).toMatchObject({
        state: "ready",
        generation: initialArchiveGeneration,
    });
    expect(afterLateRead.engineAsset).toEqual(failedState.engineAsset);
    expect(afterLateRead.archive).toEqual(failedState.archive);
    expect(afterLateRead.engineEvents.filter(
        (event) => event.generation === started.generation && event.state === "ready",
    )).toEqual([]);
    expect(afterLateRead.archiveEvents.some(
        (event) => event.generation === initialArchiveGeneration && event.state === "ready",
    )).toBe(true);

    // The same persisted import can be explicitly mounted again after the
    // stale generation has been retired.
    await page.evaluate(async () => {
        const runtime = globalThis.__KISAKCOD_WEB__;
        await runtime.module.mount(runtime.assets.manifest);
        runtime.module._KisakWeb_StartArchiveJob();
    });
    await waitForState(page, "archive", "ready");
    await waitForState(page, "engineAsset", "ready");
    const recovered = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
        engineEvents: globalThis.__syntheticEngineAssetEvents,
        archiveEvents: globalThis.__syntheticEngineArchiveEvents,
    }));
    expect(recovered.archive.generation).toBeGreaterThan(initialArchiveGeneration);
    expect(recovered.engineAsset.generation).toBeGreaterThan(started.generation);
    expect(recovered.engineAsset).toMatchObject(expectedReady(
        IWI_PATH,
        iwi,
        ZIP_METHOD_DEFLATE,
        {
            format: IWI_FORMAT_ARGB,
            flags: IWI_FLAG_NO_MIPMAPS,
            width: 2,
            height: 2,
            depth: 1,
            mipCount: 1,
        },
    ));
    expect(recovered.engineEvents.filter(
        (event) => event.generation === started.generation && event.state === "ready",
    )).toEqual([]);
    expect(recovered.archiveEvents.some((event) =>
        event.generation === recovered.archive.generation && event.state === "ready"
    )).toBe(true);
});

test("a cancelled read from an old import cannot overwrite the replacement IWI", { tag: "@smoke" }, async ({ page }, testInfo) => {
    await usePortableFolderPicker(page);
    await observeEngineAsset(page);
    const oldIwi = createSyntheticIwi();
    const newIwi = createSyntheticIwi({
        width: 4,
        height: 2,
        payload: deterministicBytes(4 * 2 * 4),
    });
    const oldDirectory = await createInstallDirectory(testInfo, "engine-iwi-old", engineArchive([{
        path: IWI_PATH,
        contents: oldIwi,
        method: ZIP_METHOD_DEFLATE,
    }]));
    const newDirectory = await createInstallDirectory(testInfo, "engine-iwi-new", engineArchive([{
        path: IWI_PATH,
        contents: newIwi,
        method: ZIP_METHOD_DEFLATE,
    }]));

    await page.goto("/");
    await waitForRuntime(page);
    await waitForState(page, "assets", "empty");
    await page.evaluate(() => {
        globalThis.__cancelledOldEngineAsset = null;
        globalThis.addEventListener("kisakcod:engine-asset", (event) => {
            if (event.detail.state !== "loading" || globalThis.__cancelledOldEngineAsset) return;
            globalThis.__cancelledOldEngineAsset = structuredClone(event.detail);
            globalThis.__KISAKCOD_WEB__.module._KisakWeb_CancelArchiveJob();
        });
    });
    await chooseDirectory(page, oldDirectory);
    await waitForState(page, "assets", "ready");
    await expect.poll(() => page.evaluate(
        () => globalThis.__cancelledOldEngineAsset,
    )).not.toBeNull();
    await waitForState(page, "engineAsset", "idle");
    const started = await page.evaluate(() => globalThis.__cancelledOldEngineAsset);
    const oldImportId = await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.assets.manifest.importId,
    );

    await chooseDirectory(page, newDirectory);
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.assets?.manifest?.importId,
    )).not.toBe(oldImportId);
    await waitForState(page, "assets", "ready");
    await waitForState(page, "archive", "ready");
    await waitForState(page, "engineAsset", "ready");

    const readyBeforeLateRead = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
    );
    const releasedAt = await page.evaluate(
        () => globalThis.__syntheticEngineAssetRafTicks,
    );
    await expect.poll(() => page.evaluate(
        () => globalThis.__syntheticEngineAssetRafTicks,
    )).toBeGreaterThan(releasedAt + 2);

    const result = await page.evaluate(() => ({
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        events: globalThis.__syntheticEngineAssetEvents,
    }));
    expect(readyBeforeLateRead).toMatchObject(expectedReady(
        IWI_PATH,
        newIwi,
        ZIP_METHOD_DEFLATE,
        {
            format: IWI_FORMAT_ARGB,
            flags: IWI_FLAG_NO_MIPMAPS,
            width: 4,
            height: 2,
            depth: 1,
            mipCount: 1,
        },
    ));
    expect(result.engineAsset).toEqual(readyBeforeLateRead);
    expect(result.events.filter(
        (event) => event.generation === started.generation && event.state === "ready",
    )).toEqual([]);
});
