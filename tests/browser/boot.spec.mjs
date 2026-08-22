import { expect, test } from "@playwright/test";

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

async function observeIndexedSurface(page)
{
    await page.addInitScript(() => {
        globalThis.__syntheticSurfaceBufferUploads = [];
        globalThis.__syntheticSurfaceDrawElementsCount = 0;
        globalThis.__syntheticSurfaceDrawArraysCount = 0;
        globalThis.__syntheticSurfaceLastDraw = null;
        globalThis.__syntheticSurfaceEvents = [];
        globalThis.__syntheticEngineWorldSurfaceEvents = [];
        globalThis.__syntheticSurfaceLifecycle = [];
        let lifecycleSequence = 0;
        const recordLifecycle = (kind, detail) => {
            globalThis.__syntheticSurfaceLifecycle.push({
                kind,
                sequence: ++lifecycleSequence,
                detail,
            });
        };
        globalThis.addEventListener("kisakcod:renderer-surface", (event) => {
            const detail = structuredClone(event.detail);
            globalThis.__syntheticSurfaceEvents.push(detail);
            recordLifecycle("renderer", detail);
        });
        globalThis.addEventListener("kisakcod:engine-world-surface", (event) => {
            const detail = structuredClone(event.detail);
            globalThis.__syntheticEngineWorldSurfaceEvents.push(detail);
            recordLifecycle("engine-world", detail);
        });
        globalThis.addEventListener("kisakcod:state", (event) => {
            recordLifecycle("runtime", structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:frame", (event) => {
            recordLifecycle("frame", structuredClone(event.detail));
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
                            // Recording must never change bufferData behavior for an
                            // overload or source type the observer did not anticipate.
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
                    recordLifecycle("buffer-upload", { byteLength, target: args[0] });
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
                    recordLifecycle("draw", {
                        count: Number(count),
                        offset: Number(offset),
                    });
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

test("boots the headless engine slice and renders through WebGL2", { tag: "@smoke" }, async ({ page }) => {
    await observeIndexedSurface(page);
    const pageErrors = [];
    const consoleErrors = [];
    const foreignRequests = [];
    let wasmResponse = null;

    page.on("pageerror", (error) => pageErrors.push(error.message));
    page.on("console", (message) => {
        if (message.type() === "error") {
            consoleErrors.push(message.text());
        }
    });
    page.on("request", (request) => {
        const url = new URL(request.url());
        if (url.protocol !== "data:" && url.protocol !== "blob:" && url.origin !== "http://127.0.0.1:8000") {
            foreignRequests.push(request.url());
        }
    });
    page.on("response", (response) => {
        if (new URL(response.url()).pathname.endsWith("/kisakcod.wasm")) {
            wasmResponse = response;
        }
    });

    await page.goto("/");

    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
        { message: "the browser-owned C++ frame pump should report running" },
    ).toBe("running");

    await expect(page.locator("#boot-log")).toContainText("ODE physics math verified in WebAssembly");
    await expect(page.locator("#boot-log")).toContainText("qcommon command/dvar smoke test passed");
    await expect(page.locator("#boot-log")).toContainText(
        "Command buffer advanced across browser frames",
    );
    await expect(page.locator("#boot-log")).toContainText(
        "First converted engine world surface rendered",
    );
    await expect(page.locator("#physics-status")).toHaveText("ODE + qcommon verified");
    // Headless Chromium may recycle its GPU context while the assertions run;
    // both states prove that the renderer is live, and recovery has its own
    // dedicated test below.
    await expect(page.locator("#renderer-status")).toHaveText(
        /WebGL2 (?:\+ world surface ready|restored)/,
    );
    await expect(page.locator("#system-status")).toContainText("RAF tick");
    await expect(page.locator("#game-canvas")).toBeVisible();

    const runtimeSnapshot = await page.evaluate(() => ({
        frame: globalThis.__KISAKCOD_WEB__.lastFrame?.frame ?? 0,
        canvasWidth: document.querySelector("#game-canvas").width,
        canvasHeight: document.querySelector("#game-canvas").height,
        hasWebGL2: globalThis.__KISAKCOD_WEB__.rendererSurface?.state === "ready",
        engine: globalThis.__KISAKCOD_WEB__.engine,
        system: globalThis.__KISAKCOD_WEB__.system,
        systemSamples: globalThis.__KISAKCOD_WEB__.systemSamples,
        engineWorldSurface: structuredClone(globalThis.__KISAKCOD_WEB__.engineWorldSurface),
        rendererSurface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
        surfaceBufferUploads: structuredClone(globalThis.__syntheticSurfaceBufferUploads),
        surfaceDrawElementsCount: globalThis.__syntheticSurfaceDrawElementsCount,
        surfaceDrawArraysCount: globalThis.__syntheticSurfaceDrawArraysCount,
        surfaceLastDraw: structuredClone(globalThis.__syntheticSurfaceLastDraw),
        surfaceEvents: structuredClone(globalThis.__syntheticSurfaceEvents),
        engineWorldSurfaceEvents: structuredClone(
            globalThis.__syntheticEngineWorldSurfaceEvents,
        ),
        surfaceLifecycle: structuredClone(globalThis.__syntheticSurfaceLifecycle),
    }));

    expect(runtimeSnapshot.frame).toBeGreaterThan(0);
    expect(runtimeSnapshot.canvasWidth).toBeGreaterThan(0);
    expect(runtimeSnapshot.canvasHeight).toBeGreaterThan(0);
    expect(runtimeSnapshot.hasWebGL2).toBe(true);
    expect(runtimeSnapshot.engine).toMatchObject({
        state: "ready",
        commandDvar: "ready",
        frameCommandDvar: "executed",
    });
    expect(runtimeSnapshot.engine.framePumpTick).toBeGreaterThanOrEqual(2);
    expect(runtimeSnapshot.system.state).toBe("running");
    expect(runtimeSnapshot.system.framePumpTicks).toBeGreaterThanOrEqual(2);
    expect(runtimeSnapshot.engineWorldSurface).toMatchObject({
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
    expect(runtimeSnapshot.engineWorldSurface.extractionGeneration).toBeGreaterThan(0);
    expect(runtimeSnapshot.engineWorldSurface.conversionGeneration).toBeGreaterThan(0);
    expect(runtimeSnapshot.engineWorldSurface.compressedBytes).toBeGreaterThan(0);
    expect(runtimeSnapshot.engineWorldSurface.fastfileBytes)
        .toBe(runtimeSnapshot.engineWorldSurface.compressedBytes + 12);
    expect(runtimeSnapshot.engineWorldSurface).toMatchObject({
        maxStepBytes: 64 * 1024,
        maxStepRecords: 64,
        maxSourceChunkBytes: 37,
        needsSource: false,
        compressedBytesConsumed: runtimeSnapshot.engineWorldSurface.compressedBytes,
        inflatedBytesProduced: runtimeSnapshot.engineWorldSurface.inflatedBytes,
        parsedBytes: 1434,
    });
    expect(runtimeSnapshot.engineWorldSurface.sourceBytesReceived)
        .toBe(runtimeSnapshot.engineWorldSurface.fastfileBytes);
    expect(runtimeSnapshot.engineWorldSurface.sourceBytesConsumed)
        .toBe(runtimeSnapshot.engineWorldSurface.fastfileBytes);
    expect(runtimeSnapshot.engineWorldSurface.sourceFeedCount)
        .toBe(Math.ceil(runtimeSnapshot.engineWorldSurface.fastfileBytes / 37));
    expect(runtimeSnapshot.engineWorldSurface.sourceFeedCount).toBeGreaterThan(1);
    expect(runtimeSnapshot.engineWorldSurface.stepCount).toBeGreaterThanOrEqual(2);
    expect(runtimeSnapshot.engineWorldSurface.recordsProcessed).toBeGreaterThan(0);
    expect(Object.values(runtimeSnapshot.engineWorldSurface).some(Array.isArray)).toBe(false);
    expect(Object.keys(runtimeSnapshot.engineWorldSurface).some(
        (key) => /(?:vertices|indices|pointer|ptr)$/i.test(key),
    )).toBe(false);
    expect(runtimeSnapshot.rendererSurface).toMatchObject({
        state: "ready",
        vertexCount: 4,
        indexCount: 6,
        drawFirstIndex: 0,
        drawIndexCount: 6,
        vertexBytes: 288,
        indexBytes: 12,
        recoveryBytes: 300,
        topology: "triangle-list",
        textureBinding: "engine-image",
        resident: true,
    });
    expect(runtimeSnapshot.rendererSurface).not.toHaveProperty("vertices");
    expect(runtimeSnapshot.rendererSurface).not.toHaveProperty("indices");
    // WebGL now lives in the dedicated engine Worker. Renderer publication is
    // the observable boundary; main-thread WebGL prototype interception cannot
    // observe an OffscreenCanvas context in another realm.
    const retainedSurfaceIndex = runtimeSnapshot.surfaceEvents.findIndex(
        (event) => event.state === "retained",
    );
    const readySurfaceIndex = runtimeSnapshot.surfaceEvents.findIndex(
        (event, index) => index > retainedSurfaceIndex && event.state === "ready",
    );
    expect(retainedSurfaceIndex).toBeGreaterThanOrEqual(0);
    expect(readySurfaceIndex).toBeGreaterThan(retainedSurfaceIndex);
    expect(runtimeSnapshot.surfaceEvents[retainedSurfaceIndex]).toMatchObject({
        submissionGeneration: runtimeSnapshot.rendererSurface.submissionGeneration,
        resourceGeneration: 0,
        recoveryBytes: 300,
        resident: false,
    });
    expect(runtimeSnapshot.surfaceEvents[readySurfaceIndex]).toMatchObject({
        submissionGeneration: runtimeSnapshot.rendererSurface.submissionGeneration,
        recoveryBytes: 300,
        resident: true,
    });
    expect(runtimeSnapshot.surfaceEvents[readySurfaceIndex].resourceGeneration)
        .toBeGreaterThan(runtimeSnapshot.surfaceEvents[retainedSurfaceIndex].resourceGeneration);
    const readyConversions = runtimeSnapshot.engineWorldSurfaceEvents.filter(
        (event) => event.state === "ready",
    );
    expect(readyConversions).toHaveLength(1);
    expect(readyConversions[0]).toEqual(runtimeSnapshot.engineWorldSurface);
    const progressEvents = runtimeSnapshot.engineWorldSurfaceEvents.filter(
        (event) => event.state === "loading" && event.framePumpTick > 0,
    );
    const beginEventIndex = runtimeSnapshot.engineWorldSurfaceEvents.findIndex(
        (event) => event.state === "loading" && event.pipelineStage === "begin",
    );
    const inflateEventIndex = runtimeSnapshot.engineWorldSurfaceEvents.findIndex(
        (event) => event.state === "loading" && event.pipelineStage === "inflate",
    );
    const sourceWaitEventIndex = runtimeSnapshot.engineWorldSurfaceEvents.findIndex(
        (event) => event.state === "loading" && event.pipelineStage === "source-wait",
    );
    const traverseEventIndex = runtimeSnapshot.engineWorldSurfaceEvents.findIndex(
        (event) => event.state === "loading" && event.pipelineStage === "traverse",
    );
    const completeEventIndex = runtimeSnapshot.engineWorldSurfaceEvents.findIndex(
        (event) => event.state === "ready" && event.pipelineStage === "complete",
    );
    expect(beginEventIndex).toBeGreaterThanOrEqual(0);
    expect(sourceWaitEventIndex).toBeGreaterThan(beginEventIndex);
    expect(inflateEventIndex).toBeGreaterThan(sourceWaitEventIndex);
    expect(traverseEventIndex).toBeGreaterThan(inflateEventIndex);
    expect(completeEventIndex).toBeGreaterThan(traverseEventIndex);
    expect(progressEvents.length).toBeGreaterThanOrEqual(2);
    expect(new Set(progressEvents.map((event) => event.framePumpTick)).size)
        .toBe(progressEvents.length);
    for (let index = 0; index < progressEvents.length; index += 1) {
        const progress = progressEvents[index];
        expect(progress).toMatchObject({
            extractionGeneration: runtimeSnapshot.engineWorldSurface.extractionGeneration,
            maxStepBytes: 64 * 1024,
            maxStepRecords: 64,
            maxSourceChunkBytes: 37,
        });
        expect(progress.stepInputBytes).toBeLessThanOrEqual(progress.maxStepBytes);
        expect(progress.stepOutputBytes).toBeLessThanOrEqual(progress.maxStepBytes);
        expect(progress.stepParsedBytes).toBeLessThanOrEqual(progress.maxStepBytes);
        expect(progress.stepRecords).toBeLessThanOrEqual(progress.maxStepRecords);
        expect(progress.stepSourceBytes).toBeLessThanOrEqual(progress.maxSourceChunkBytes);
        if (progress.pipelineStage === "source-wait") {
            expect(progress.needsSource).toBe(true);
            expect(progress.stepSourceBytes).toBe(0);
        }
        if (index > 0) {
            const previous = progressEvents[index - 1];
            expect(progress.framePumpTick).toBeGreaterThan(previous.framePumpTick);
            expect(progress.stepCount).toBeGreaterThanOrEqual(previous.stepCount);
            expect(progress.stepCount).toBeLessThanOrEqual(previous.stepCount + 1);
            expect(progress.sourceFeedCount).toBeGreaterThanOrEqual(previous.sourceFeedCount);
            expect(progress.sourceBytesReceived)
                .toBeGreaterThanOrEqual(previous.sourceBytesReceived);
            expect(progress.sourceBytesConsumed)
                .toBeGreaterThanOrEqual(previous.sourceBytesConsumed);
            expect(progress.compressedBytesConsumed)
                .toBeGreaterThanOrEqual(previous.compressedBytesConsumed);
            expect(progress.inflatedBytesProduced)
                .toBeGreaterThanOrEqual(previous.inflatedBytesProduced);
            expect(progress.parsedBytes).toBeGreaterThanOrEqual(previous.parsedBytes);
            expect(progress.recordsProcessed).toBeGreaterThanOrEqual(previous.recordsProcessed);
        }
    }
    const retainedLifecycleIndex = runtimeSnapshot.surfaceLifecycle.findIndex(
        (event) => event.kind === "renderer" && event.detail.state === "retained",
    );
    const conversionLifecycleIndex = runtimeSnapshot.surfaceLifecycle.findIndex(
        (event) => event.kind === "engine-world" && event.detail.state === "ready",
    );
    const readyLifecycleIndex = runtimeSnapshot.surfaceLifecycle.findIndex(
        (event, index) => index > conversionLifecycleIndex &&
            event.kind === "renderer" && event.detail.state === "ready",
    );
    const lastProgressLifecycleIndex = runtimeSnapshot.surfaceLifecycle.findLastIndex(
        (event) => event.kind === "engine-world" && event.detail.state === "loading" &&
            event.detail.framePumpTick > 0,
    );
    const runtimeReadyLifecycleIndex = runtimeSnapshot.surfaceLifecycle.findIndex(
        (event, index) => index > readyLifecycleIndex &&
            event.kind === "runtime" && event.detail.state === "runtime-ready",
    );
    const runningLifecycleIndex = runtimeSnapshot.surfaceLifecycle.findIndex(
        (event, index) => index > runtimeReadyLifecycleIndex &&
            event.kind === "runtime" && event.detail.state === "running",
    );
    const firstFrameLifecycleIndex = runtimeSnapshot.surfaceLifecycle.findIndex(
        (event, index) => index > runningLifecycleIndex && event.kind === "frame",
    );
    expect(retainedLifecycleIndex).toBeGreaterThanOrEqual(0);
    expect(lastProgressLifecycleIndex).toBeGreaterThanOrEqual(0);
    expect(retainedLifecycleIndex).toBeGreaterThan(lastProgressLifecycleIndex);
    expect(conversionLifecycleIndex).toBeGreaterThan(retainedLifecycleIndex);
    expect(readyLifecycleIndex).toBeGreaterThan(conversionLifecycleIndex);
    expect(runtimeReadyLifecycleIndex).toBeGreaterThan(readyLifecycleIndex);
    expect(runningLifecycleIndex).toBeGreaterThan(runtimeReadyLifecycleIndex);
    expect(firstFrameLifecycleIndex).toBeGreaterThan(runningLifecycleIndex);

    const firstMonotonicSample = runtimeSnapshot.system.monotonicMilliseconds;
    await expect.poll(
        () => page.evaluate(
            () => globalThis.__KISAKCOD_WEB__.system?.monotonicMilliseconds ?? 0,
        ),
        { message: "the browser system clock should advance monotonically" },
    ).toBeGreaterThan(firstMonotonicSample);
    const monotonicSamples = await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.systemSamples.map(
            (sample) => sample.monotonicMilliseconds,
        ),
    );
    expect(monotonicSamples.length).toBeGreaterThanOrEqual(3);
    for (let index = 1; index < monotonicSamples.length; index += 1) {
        expect(monotonicSamples[index]).toBeGreaterThanOrEqual(monotonicSamples[index - 1]);
    }
    expect(wasmResponse, "the page should request its Wasm binary").not.toBeNull();
    expect(wasmResponse.ok()).toBe(true);
    expect(wasmResponse.headers()["content-type"]).toContain("application/wasm");
    expect(foreignRequests).toEqual([]);
    expect(pageErrors).toEqual([]);
    expect(consoleErrors).toEqual([]);
});

test("reports and recovers from WebGL2 context loss", { tag: "@smoke" }, async ({ page }) => {
    await observeIndexedSurface(page);
    await page.goto("/");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("running");

    const beforeLoss = await page.evaluate(() => ({
        engineWorldSurface: structuredClone(globalThis.__KISAKCOD_WEB__.engineWorldSurface),
        surface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
        bufferUploads: structuredClone(globalThis.__syntheticSurfaceBufferUploads),
        drawElementsCount: globalThis.__syntheticSurfaceDrawElementsCount,
        eventCount: globalThis.__syntheticSurfaceEvents.length,
        engineEventCount: globalThis.__syntheticEngineWorldSurfaceEvents.length,
    }));

    const canSimulateContextLoss = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestLoseWebGLContext"));
    expect(Boolean(canSimulateContextLoss)).toBe(true);

    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("renderer-lost");
    await expect(page.locator("#renderer-status")).toHaveText("WebGL2 context lost");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.rendererSurface?.state,
    )).toBe("lost");
    const lostSurface = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
    );
    expect(lostSurface).toMatchObject({
        vertexCount: 4,
        indexCount: 6,
        recoveryBytes: 300,
        submissionGeneration: beforeLoss.surface.submissionGeneration,
        resident: false,
    });

    await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestRestoreWebGLContext"));
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("running");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.rendererSurface?.state,
    )).toBe("ready");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.rendererSurface?.resourceGeneration,
    )).toBeGreaterThan(beforeLoss.surface.resourceGeneration);
    await expect(page.locator("#renderer-status")).toHaveText("WebGL2 restored");
    await expect(page.locator("#boot-log")).toContainText(
        "WebGL2 context restored; renderer rebuilt",
    );

    const recovered = await page.evaluate(() => ({
        surface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
        bufferUploads: structuredClone(globalThis.__syntheticSurfaceBufferUploads),
        drawArraysCount: globalThis.__syntheticSurfaceDrawArraysCount,
        lastDraw: structuredClone(globalThis.__syntheticSurfaceLastDraw),
        events: structuredClone(globalThis.__syntheticSurfaceEvents),
        engineWorldSurface: structuredClone(globalThis.__KISAKCOD_WEB__.engineWorldSurface),
        engineEvents: structuredClone(globalThis.__syntheticEngineWorldSurfaceEvents),
    }));
    expect(recovered.surface).toMatchObject({
        state: "ready",
        vertexCount: 4,
        indexCount: 6,
        recoveryBytes: 300,
        submissionGeneration: beforeLoss.surface.submissionGeneration,
        resident: true,
    });
    expect(recovered.surface.resourceGeneration)
        .toBeGreaterThan(beforeLoss.surface.resourceGeneration);
    expect(recovered.surface.recoveryCount)
        .toBe(beforeLoss.surface.recoveryCount + 1);
    const lifecycle = recovered.events.slice(beforeLoss.eventCount);
    const lostIndex = lifecycle.findIndex((event) => event.state === "lost");
    const readyIndex = lifecycle.findIndex(
        (event, index) => index > lostIndex && event.state === "ready",
    );
    expect(lostIndex).toBeGreaterThanOrEqual(0);
    expect(readyIndex).toBeGreaterThan(lostIndex);
    expect(recovered.engineWorldSurface).toEqual(beforeLoss.engineWorldSurface);
    expect(recovered.engineWorldSurface.extractionGeneration)
        .toBe(beforeLoss.engineWorldSurface.extractionGeneration);
    expect(recovered.engineWorldSurface.conversionGeneration)
        .toBe(beforeLoss.engineWorldSurface.conversionGeneration);
    expect(recovered.engineWorldSurface.stepCount)
        .toBe(beforeLoss.engineWorldSurface.stepCount);
    expect(recovered.engineWorldSurface.compressedBytesConsumed)
        .toBe(beforeLoss.engineWorldSurface.compressedBytesConsumed);
    expect(recovered.engineWorldSurface.inflatedBytesProduced)
        .toBe(beforeLoss.engineWorldSurface.inflatedBytesProduced);
    expect(recovered.engineWorldSurface.parsedBytes)
        .toBe(beforeLoss.engineWorldSurface.parsedBytes);
    expect(recovered.engineWorldSurface.recordsProcessed)
        .toBe(beforeLoss.engineWorldSurface.recordsProcessed);
    expect(recovered.engineEvents).toHaveLength(beforeLoss.engineEventCount);
});

test("does not resume when indexed surface recovery fails", async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__KISAKCOD_FAIL_SURFACE_RESTORE__ = false;
        globalThis.__KISAKCOD_SURFACE_RESTORE_EVENTS__ = [];
        globalThis.__KISAKCOD_SURFACE_RESTORE_DRAWS__ = 0;
        globalThis.addEventListener("kisakcod:renderer-surface", (event) => {
            globalThis.__KISAKCOD_SURFACE_RESTORE_EVENTS__.push(
                structuredClone(event.detail),
            );
        });

    });

    await page.goto("/");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("running");
    const beforeLoss = await page.evaluate(() => ({
        surface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
        draws: globalThis.__KISAKCOD_SURFACE_RESTORE_DRAWS__,
        eventCount: globalThis.__KISAKCOD_SURFACE_RESTORE_EVENTS__.length,
    }));

    const canSimulateContextLoss = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestLoseWebGLContext"));
    expect(Boolean(canSimulateContextLoss)).toBe(true);
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("renderer-lost");
    const drawsAtLoss = await page.evaluate(
        () => globalThis.__KISAKCOD_SURFACE_RESTORE_DRAWS__,
    );

    await page.evaluate(async () => {
        await globalThis.__KISAKCOD_WEB__.module.testControl({ failSurfaceRestore: true });
        await globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestRestoreWebGLContext");
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("failed");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.rendererSurface?.state,
    )).toBe("failed");
    await expect(page.locator("#boot-log")).toContainText(
        "WebGL2 indexed surface creation failed",
    );

    const failed = await page.evaluate(() => ({
        surface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
        draws: globalThis.__KISAKCOD_SURFACE_RESTORE_DRAWS__,
        events: structuredClone(globalThis.__KISAKCOD_SURFACE_RESTORE_EVENTS__),
    }));
    expect(failed.surface).toMatchObject({
        state: "failed",
        vertexCount: 4,
        indexCount: 6,
        submissionGeneration: beforeLoss.surface.submissionGeneration,
        resourceGeneration: beforeLoss.surface.resourceGeneration,
        recoveryCount: beforeLoss.surface.recoveryCount,
        resident: false,
    });
    const lifecycle = failed.events.slice(beforeLoss.eventCount);
    const failureIndex = lifecycle.findIndex((event) => event.state === "failed");
    expect(failureIndex).toBeGreaterThanOrEqual(0);
    expect(lifecycle.slice(failureIndex + 1).some(
        (event) => event.state === "ready",
    )).toBe(false);
});

test("does not recover a renderer whose initial pipeline failed", async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ = { failInitialShader: true };
        globalThis.__KISAKCOD_INIT_FAILURE_STATES__ = [];
        globalThis.addEventListener("kisakcod:state", (event) => {
            globalThis.__KISAKCOD_INIT_FAILURE_STATES__.push(event.detail.state);
        });
    });

    await page.goto("/");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("failed");
    await expect(page.locator("#boot-log")).toContainText("Shader compilation failed");

    await page.evaluate(async () => {
        await globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestLoseWebGLContext");
        await globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestRestoreWebGLContext");
    });
    const terminalState = await page.evaluate(() => ({
        current: globalThis.__KISAKCOD_WEB__?.state,
        lastFrame: globalThis.__KISAKCOD_WEB__?.lastFrame ?? null,
        states: globalThis.__KISAKCOD_INIT_FAILURE_STATES__,
    }));
    expect(terminalState.current).toBe("failed");
    expect(terminalState.lastFrame).toBeNull();
    expect(terminalState.states).not.toContain("renderer-lost");
    expect(terminalState.states).not.toContain("running");
});

test("shows a useful failure when the generated module is missing", { tag: "@smoke" }, async ({ page }) => {
    await page.route("**/kisakcod.mjs", (route) => route.abort("failed"));
    await page.goto("/");

    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("failed");
    await expect(page.locator("#runtime-message")).toHaveText(
        "The WebAssembly module could not start",
    );
    await expect(page.locator("#boot-log")).toContainText("kisakcod.mjs");
});
