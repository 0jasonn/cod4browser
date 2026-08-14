import { expect, test } from "@playwright/test";

function expectProductionWorldSurfaceUploads(uploads)
{
    const vertexUpload = uploads.find(
        (upload) => upload.target === "array" && upload.byteLength === 112,
    );
    const indexUpload = uploads.find(
        (upload) => upload.target === "element-array" && upload.byteLength === 12,
    );
    expect(vertexUpload, "the converted world vertices should be uploaded").toBeTruthy();
    expect(indexUpload, "the converted world indices should be uploaded").toBeTruthy();
    expect(vertexUpload.bytes).toHaveLength(112);
    expect(indexUpload.bytes).toHaveLength(12);

    const vertexValues = new Float32Array(Uint8Array.from(vertexUpload.bytes).buffer);
    const expectedVertices = [
        [-0.5, 0.5, 224 / 255, 96 / 255, 32 / 255, 0, 0],
        [-0.5, -0.5, 48 / 255, 176 / 255, 80 / 255, 0, 1],
        [0.5, -0.5, 64 / 255, 112 / 255, 232 / 255, 1, 1],
        [0.5, 0.5, 240 / 255, 208 / 255, 72 / 255, 1, 0],
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

test("boots the headless engine slice and renders through WebGL2", async ({ page }) => {
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
        hasWebGL2: Boolean(document.querySelector("#game-canvas").getContext("webgl2")),
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
        framePumpTick: 2,
    });
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
        compressedBytesConsumed: runtimeSnapshot.engineWorldSurface.compressedBytes,
        inflatedBytesProduced: runtimeSnapshot.engineWorldSurface.inflatedBytes,
        parsedBytes: 1434,
    });
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
        vertexBytes: 112,
        indexBytes: 12,
        recoveryBytes: 124,
        topology: "triangle-list",
        textureBinding: "engine-image",
        resident: true,
    });
    expect(runtimeSnapshot.rendererSurface).not.toHaveProperty("vertices");
    expect(runtimeSnapshot.rendererSurface).not.toHaveProperty("indices");
    expect(runtimeSnapshot.surfaceBufferUploads).toEqual(expect.arrayContaining([
        expect.objectContaining({ target: "array", byteLength: 112 }),
        expect.objectContaining({ target: "element-array", byteLength: 12 }),
    ]));
    expectProductionWorldSurfaceUploads(runtimeSnapshot.surfaceBufferUploads);
    expect(runtimeSnapshot.surfaceDrawElementsCount).toBeGreaterThan(0);
    expect(runtimeSnapshot.surfaceDrawArraysCount).toBe(0);
    expect(runtimeSnapshot.surfaceLastDraw).toEqual({
        mode: "triangles",
        count: 6,
        type: "uint16",
        offset: 0,
    });
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
        recoveryBytes: 124,
        resident: false,
    });
    expect(runtimeSnapshot.surfaceEvents[readySurfaceIndex]).toMatchObject({
        submissionGeneration: runtimeSnapshot.rendererSurface.submissionGeneration,
        recoveryBytes: 124,
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
    const traverseEventIndex = runtimeSnapshot.engineWorldSurfaceEvents.findIndex(
        (event) => event.state === "loading" && event.pipelineStage === "traverse",
    );
    const completeEventIndex = runtimeSnapshot.engineWorldSurfaceEvents.findIndex(
        (event) => event.state === "ready" && event.pipelineStage === "complete",
    );
    expect(beginEventIndex).toBeGreaterThanOrEqual(0);
    expect(inflateEventIndex).toBeGreaterThan(beginEventIndex);
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
        });
        expect(progress.stepInputBytes).toBeLessThanOrEqual(progress.maxStepBytes);
        expect(progress.stepOutputBytes).toBeLessThanOrEqual(progress.maxStepBytes);
        expect(progress.stepParsedBytes).toBeLessThanOrEqual(progress.maxStepBytes);
        expect(progress.stepRecords).toBeLessThanOrEqual(progress.maxStepRecords);
        if (index > 0) {
            const previous = progressEvents[index - 1];
            expect(progress.framePumpTick).toBeGreaterThan(previous.framePumpTick);
            expect(progress.stepCount).toBe(previous.stepCount + 1);
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
    const firstUploadLifecycleIndex = runtimeSnapshot.surfaceLifecycle.findIndex(
        (event) => event.kind === "buffer-upload",
    );
    const runtimeReadyLifecycleIndex = runtimeSnapshot.surfaceLifecycle.findIndex(
        (event, index) => index > readyLifecycleIndex &&
            event.kind === "runtime" && event.detail.state === "runtime-ready",
    );
    const firstDrawLifecycleIndex = runtimeSnapshot.surfaceLifecycle.findIndex(
        (event, index) => index > runtimeReadyLifecycleIndex && event.kind === "draw",
    );
    const runningLifecycleIndex = runtimeSnapshot.surfaceLifecycle.findIndex(
        (event, index) => index > firstDrawLifecycleIndex &&
            event.kind === "runtime" && event.detail.state === "running",
    );
    const firstFrameLifecycleIndex = runtimeSnapshot.surfaceLifecycle.findIndex(
        (event, index) => index > runningLifecycleIndex && event.kind === "frame",
    );
    expect(retainedLifecycleIndex).toBeGreaterThanOrEqual(0);
    expect(lastProgressLifecycleIndex).toBeGreaterThanOrEqual(0);
    expect(retainedLifecycleIndex).toBeGreaterThan(lastProgressLifecycleIndex);
    expect(conversionLifecycleIndex).toBeGreaterThan(retainedLifecycleIndex);
    expect(firstUploadLifecycleIndex).toBeGreaterThan(conversionLifecycleIndex);
    expect(readyLifecycleIndex).toBeGreaterThan(conversionLifecycleIndex);
    expect(readyLifecycleIndex).toBeGreaterThan(firstUploadLifecycleIndex);
    expect(runtimeReadyLifecycleIndex).toBeGreaterThan(readyLifecycleIndex);
    expect(firstDrawLifecycleIndex).toBeGreaterThan(runtimeReadyLifecycleIndex);
    expect(runningLifecycleIndex).toBeGreaterThan(firstDrawLifecycleIndex);
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

test("reports and recovers from WebGL2 context loss", async ({ page }) => {
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

    const canSimulateContextLoss = await page.evaluate(() => {
        const canvas = document.querySelector("#game-canvas");
        const extension = canvas.getContext("webgl2")?.getExtension("WEBGL_lose_context");
        if (!extension) {
            return false;
        }
        globalThis.__KISAKCOD_CONTEXT_LOSS_TEST__ = extension;
        extension.loseContext();
        return true;
    });
    expect(canSimulateContextLoss).toBe(true);

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
        recoveryBytes: 124,
        submissionGeneration: beforeLoss.surface.submissionGeneration,
        resident: false,
    });

    await page.evaluate(() => globalThis.__KISAKCOD_CONTEXT_LOSS_TEST__.restoreContext());
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("running");
    await expect.poll(() => page.evaluate(
        () => globalThis.__KISAKCOD_WEB__?.rendererSurface?.state,
    )).toBe("ready");
    await expect.poll(() => page.evaluate(
        () => globalThis.__syntheticSurfaceDrawElementsCount,
    )).toBeGreaterThan(beforeLoss.drawElementsCount);
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
        recoveryBytes: 124,
        submissionGeneration: beforeLoss.surface.submissionGeneration,
        resident: true,
    });
    expect(recovered.surface.resourceGeneration)
        .toBeGreaterThan(beforeLoss.surface.resourceGeneration);
    expect(recovered.surface.recoveryCount)
        .toBe(beforeLoss.surface.recoveryCount + 1);
    expect(recovered.bufferUploads.filter(
        (upload) => upload.target === "array" && upload.byteLength === 112,
    ).length).toBeGreaterThan(
        beforeLoss.bufferUploads.filter(
            (upload) => upload.target === "array" && upload.byteLength === 112,
        ).length,
    );
    expect(recovered.bufferUploads.filter(
        (upload) => upload.target === "element-array" && upload.byteLength === 12,
    ).length).toBeGreaterThan(
        beforeLoss.bufferUploads.filter(
            (upload) => upload.target === "element-array" && upload.byteLength === 12,
        ).length,
    );
    expect(recovered.drawArraysCount).toBe(0);
    expect(recovered.lastDraw).toEqual({
        mode: "triangles",
        count: 6,
        type: "uint16",
        offset: 0,
    });
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

        const originalBufferData = WebGL2RenderingContext.prototype.bufferData;
        let injectedFailure = false;
        WebGL2RenderingContext.prototype.bufferData = function bufferData(...args) {
            if (globalThis.__KISAKCOD_FAIL_SURFACE_RESTORE__ && !injectedFailure &&
                args[0] === this.ELEMENT_ARRAY_BUFFER) {
                injectedFailure = true;
                return originalBufferData.call(this, args[0], -1, args[2]);
            }
            return originalBufferData.apply(this, args);
        };

        const originalDrawElements = WebGL2RenderingContext.prototype.drawElements;
        WebGL2RenderingContext.prototype.drawElements = function drawElements(...args) {
            if (this.canvas?.id === "game-canvas") {
                globalThis.__KISAKCOD_SURFACE_RESTORE_DRAWS__ += 1;
            }
            return originalDrawElements.apply(this, args);
        };
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

    const canSimulateContextLoss = await page.evaluate(() => {
        const extension = document.querySelector("#game-canvas")
            ?.getContext("webgl2")?.getExtension("WEBGL_lose_context");
        if (!extension) {
            return false;
        }
        globalThis.__KISAKCOD_FAILED_SURFACE_CONTEXT__ = extension;
        extension.loseContext();
        return true;
    });
    expect(canSimulateContextLoss).toBe(true);
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("renderer-lost");
    const drawsAtLoss = await page.evaluate(
        () => globalThis.__KISAKCOD_SURFACE_RESTORE_DRAWS__,
    );

    await page.evaluate(() => {
        globalThis.__KISAKCOD_FAIL_SURFACE_RESTORE__ = true;
        globalThis.__KISAKCOD_FAILED_SURFACE_CONTEXT__.restoreContext();
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

    await page.waitForTimeout(150);
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
    expect(failed.draws).toBe(drawsAtLoss);
    const lifecycle = failed.events.slice(beforeLoss.eventCount);
    const failureIndex = lifecycle.findIndex((event) => event.state === "failed");
    expect(failureIndex).toBeGreaterThanOrEqual(0);
    expect(lifecycle.slice(failureIndex + 1).some(
        (event) => event.state === "ready",
    )).toBe(false);
});

test("does not recover a renderer whose initial pipeline failed", async ({ page }) => {
    await page.addInitScript(() => {
        globalThis.__KISAKCOD_INIT_FAILURE_STATES__ = [];
        globalThis.addEventListener("kisakcod:state", (event) => {
            globalThis.__KISAKCOD_INIT_FAILURE_STATES__.push(event.detail.state);
        });

        const originalShaderSource = WebGL2RenderingContext.prototype.shaderSource;
        let rejectedInitialShader = false;
        WebGL2RenderingContext.prototype.shaderSource = function shaderSource(shader, source) {
            if (!rejectedInitialShader && this.canvas?.id === "game-canvas") {
                rejectedInitialShader = true;
                return originalShaderSource.call(this, shader, "forced invalid shader source");
            }
            return originalShaderSource.call(this, shader, source);
        };
    });

    await page.goto("/");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.state),
    ).toBe("failed");
    await expect(page.locator("#boot-log")).toContainText("Shader compilation failed");

    const canSimulateContextLoss = await page.evaluate(() => {
        const context = document.querySelector("#game-canvas").getContext("webgl2");
        const extension = context?.getExtension("WEBGL_lose_context");
        if (!extension) {
            return false;
        }
        globalThis.__KISAKCOD_FAILED_CONTEXT_TEST__ = extension;
        extension.loseContext();
        return true;
    });
    expect(canSimulateContextLoss).toBe(true);

    await page.waitForTimeout(100);
    await page.evaluate(() => globalThis.__KISAKCOD_FAILED_CONTEXT_TEST__.restoreContext());
    await page.waitForTimeout(250);

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

test("shows a useful failure when the generated module is missing", async ({ page }) => {
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
