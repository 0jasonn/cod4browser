import { expect, test } from "@playwright/test";
import { deflateSync, inflateSync } from "node:zlib";
import {
    createInstallDirectory,
    createSyntheticFastfileHeader,
    createSyntheticFxInventoryFastfile,
    createSyntheticRetailCensusFastfile,
    createSyntheticWorldInventoryFastfile,
} from "./install_fixture.mjs";
import {
    createSyntheticIwd,
    createSyntheticIwi,
    IWI_FLAG_NO_MIPMAPS,
    IWI_FORMAT_DXT1,
} from "./synthetic_iwd.mjs";

const M19_DXT1_IWI = createSyntheticIwi({
    format: IWI_FORMAT_DXT1,
    flags: IWI_FLAG_NO_MIPMAPS | 0x01,
    width: 4,
    height: 4,
    payload: Buffer.from([
        0x00, 0xf8, 0xe0, 0x07,
        0xe4, 0xe4, 0xe4, 0xe4,
    ]),
});

test("publishes a checked FX mark-visual family", async ({ page }, testInfo) => {
    await importInstall(page, testInfo, "retail-fx-family", {
        overrides: new Map([["zone/english/killhouse.ff",
            createSyntheticFxInventoryFastfile()]]),
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("ready");
    const inventory = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus.worldInventory),
    );
    expect(inventory).toMatchObject({
        completedAssetCount: 2,
        nextBodyIndex: 2,
        nextBodyType: 16,
        fxEffects: [{
            assetIndex: 1,
            name: "web/fx_mark",
            identity: 4,
            materialCount: 2,
            published: true,
            elementCounts: { looping: 0, oneShot: 1, emission: 0 },
            elements: [{
                index: 0,
                type: 9,
                visualCount: 2,
                traversed: true,
            }],
        }],
    });
});

const M20_PRIMARY_IWD = createSyntheticIwd([
    {
        path: "images/$black.iwi",
        contents: M19_DXT1_IWI,
        method: "deflate",
    },
    {
        path: "images/synthetic_engine_asset.iwi",
        contents: M19_DXT1_IWI,
        method: "deflate",
    },
]);

async function importInstall(page, testInfo, name, options = {})
{
    const directory = await createInstallDirectory(testInfo, name, options);
    await page.addInitScript(() => {
        Object.defineProperty(globalThis, "showDirectoryPicker", {
            configurable: true,
            value: undefined,
        });
    });
    await page.goto("/");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.assets?.state),
    ).toBe("empty");
    await page.evaluate(() => {
        globalThis.__retailCensusEvents = [];
        globalThis.__retailCensusArchiveEvents = [];
        globalThis.__retailRendererShaderEvents = [];
        globalThis.addEventListener("kisakcod:retail-census", (event) => {
            globalThis.__retailCensusEvents.push(structuredClone(event.detail));
        });
        globalThis.addEventListener("kisakcod:archive", (event) => {
            globalThis.__retailCensusArchiveEvents.push({
                state: event.detail.state,
                censusState: globalThis.__KISAKCOD_WEB__.retailCensus.state,
            });
        });
        globalThis.addEventListener("kisakcod:renderer-shader", (event) => {
            globalThis.__retailRendererShaderEvents.push(structuredClone(event.detail));
        });
    });
    const chooserPromise = page.waitForEvent("filechooser");
    await page.locator("#select-install-button").click();
    const chooser = await chooserPromise;
    await chooser.setFiles(directory);
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.assets?.state),
        { timeout: 30_000 },
    ).toBe("ready");
}

async function observeRetailShaderRenderer(page)
{
    await page.addInitScript(() => {
        globalThis.__retailShaderSources = [];
        globalThis.__retailShaderBindings = [];
        globalThis.__retailShaderMatrices = [];
        globalThis.__retailShaderLinkCount = 0;
        globalThis.__retailShaderUseCount = 0;
        globalThis.__retailShaderDrawCount = 0;
        globalThis.__retailSurfaceUploads = [];
        const originalBufferData = WebGL2RenderingContext.prototype.bufferData;
        Object.defineProperty(WebGL2RenderingContext.prototype, "bufferData", {
            configurable: true,
            writable: true,
            value(...args) {
                const source = args[1];
                if (this.canvas?.id === "game-canvas" && ArrayBuffer.isView(source)) {
                    const bytesPerElement = Number(source.BYTES_PER_ELEMENT ?? 1);
                    const sourceLength = Number(
                        source.length ?? source.byteLength / bytesPerElement,
                    );
                    const sourceOffset = args.length >= 4 ? Number(args[3]) : 0;
                    const elementCount = args.length >= 5
                        ? Number(args[4])
                        : sourceLength - sourceOffset;
                    const byteLength = elementCount * bytesPerElement;
                    if (byteLength <= 256 * 1024) {
                        globalThis.__retailSurfaceUploads.push({
                            target: args[0],
                            bytes: Array.from(new Uint8Array(
                                source.buffer,
                                source.byteOffset + sourceOffset * bytesPerElement,
                                byteLength,
                            )),
                        });
                    }
                }
                return originalBufferData.apply(this, args);
            },
        });
        const shaderSources = new WeakMap();
        const programShaders = new WeakMap();
        const compatibilityPrograms = new WeakSet();
        let currentCompatibilityProgram = false;

        const originalShaderSource = WebGL2RenderingContext.prototype.shaderSource;
        Object.defineProperty(WebGL2RenderingContext.prototype, "shaderSource", {
            configurable: true,
            writable: true,
            value(shader, source) {
                shaderSources.set(shader, String(source));
                if (String(source).includes("u_viewProjectionMatrix") ||
                    String(source).includes("u_colorMapSampler")) {
                    globalThis.__retailShaderSources.push(String(source));
                }
                return originalShaderSource.call(this, shader, source);
            },
        });
        const originalAttachShader = WebGL2RenderingContext.prototype.attachShader;
        Object.defineProperty(WebGL2RenderingContext.prototype, "attachShader", {
            configurable: true,
            writable: true,
            value(program, shader) {
                const shaders = programShaders.get(program) ?? [];
                shaders.push(shader);
                programShaders.set(program, shaders);
                return originalAttachShader.call(this, program, shader);
            },
        });
        const originalLinkProgram = WebGL2RenderingContext.prototype.linkProgram;
        Object.defineProperty(WebGL2RenderingContext.prototype, "linkProgram", {
            configurable: true,
            writable: true,
            value(program) {
                const sources = (programShaders.get(program) ?? [])
                    .map((shader) => shaderSources.get(shader) ?? "");
                if (sources.some((source) => source.includes("u_viewProjectionMatrix")) &&
                    sources.some((source) => source.includes("u_colorMapSampler"))) {
                    compatibilityPrograms.add(program);
                    globalThis.__retailShaderLinkCount += 1;
                }
                return originalLinkProgram.call(this, program);
            },
        });
        for (const method of ["getAttribLocation", "getUniformLocation"]) {
            const original = WebGL2RenderingContext.prototype[method];
            Object.defineProperty(WebGL2RenderingContext.prototype, method, {
                configurable: true,
                writable: true,
                value(program, name) {
                    if (compatibilityPrograms.has(program)) {
                        globalThis.__retailShaderBindings.push({ method, name: String(name) });
                    }
                    return original.call(this, program, name);
                },
            });
        }
        const originalUseProgram = WebGL2RenderingContext.prototype.useProgram;
        Object.defineProperty(WebGL2RenderingContext.prototype, "useProgram", {
            configurable: true,
            writable: true,
            value(program) {
                currentCompatibilityProgram = compatibilityPrograms.has(program);
                if (currentCompatibilityProgram) globalThis.__retailShaderUseCount += 1;
                return originalUseProgram.call(this, program);
            },
        });
        const originalUniformMatrix4fv = WebGL2RenderingContext.prototype.uniformMatrix4fv;
        Object.defineProperty(WebGL2RenderingContext.prototype, "uniformMatrix4fv", {
            configurable: true,
            writable: true,
            value(...args) {
                const [location, transpose, values] = args;
                if (currentCompatibilityProgram &&
                    globalThis.__retailShaderMatrices.length < 2) {
                    const sourceOffset = args.length >= 4 ? Number(args[3]) : 0;
                    globalThis.__retailShaderMatrices.push({
                        transpose: Boolean(transpose),
                        values: Array.from(values.subarray(
                            sourceOffset,
                            sourceOffset + 16,
                        )),
                    });
                }
                return originalUniformMatrix4fv.apply(this, args);
            },
        });
        const originalDrawElements = WebGL2RenderingContext.prototype.drawElements;
        Object.defineProperty(WebGL2RenderingContext.prototype, "drawElements", {
            configurable: true,
            writable: true,
            value(...args) {
                if (currentCompatibilityProgram) globalThis.__retailShaderDrawCount += 1;
                return originalDrawElements.apply(this, args);
            },
        });
    });
}

test("publishes the retail census and canonical renderer boundary", { tag: "@smoke" }, async ({ page }, testInfo) => {
    await observeRetailShaderRenderer(page);
    await importInstall(page, testInfo, "retail-census-success", {
        primaryIwd: M20_PRIMARY_IWD,
        overrides: new Map([
            ["zone/english/killhouse.ff",
                createSyntheticWorldInventoryFastfile()],
        ]),
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("ready");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.archive?.state),
        { timeout: 30_000 },
    ).toBe("ready");
    await expect.poll(
        () => page.evaluate(
            () => globalThis.__KISAKCOD_WEB__?.rendererShader?.firstDrawCompleted,
        ),
    ).toBe(true);
    const result = await page.evaluate(() => ({
        census: structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
        events: structuredClone(globalThis.__retailCensusEvents),
        archiveEvents: structuredClone(globalThis.__retailCensusArchiveEvents),
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
        rendererShader: structuredClone(globalThis.__KISAKCOD_WEB__.rendererShader),
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        rendererTexture: structuredClone(globalThis.__KISAKCOD_WEB__.rendererTexture),
        rendererSurface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
        rendererShaderEvents: structuredClone(globalThis.__retailRendererShaderEvents),
        shaderSources: [...globalThis.__retailShaderSources],
        shaderBindings: structuredClone(globalThis.__retailShaderBindings),
        shaderMatrices: structuredClone(globalThis.__retailShaderMatrices),
        shaderLinkCount: globalThis.__retailShaderLinkCount,
        shaderUseCount: globalThis.__retailShaderUseCount,
        shaderDrawCount: globalThis.__retailShaderDrawCount,
        surfaceUploads: structuredClone(globalThis.__retailSurfaceUploads),
        log: document.querySelector("#boot-log").textContent,
    }));
    expect(result.census).toMatchObject({
        state: "ready",
        stage: "asset-boundary",
        path: "zone/english/code_post_gfx.ff",
        version: 5,
        xfileSize: 1_378_265,
        externalSize: 950_499,
        declaredBlockBytes: 910_932,
        scriptStringCount: 3,
        scriptStringBytes: 15,
        assetCount: 5,
        inflatedPrefixBytes: 127,
        inlineReferences: 3,
        sharedReferences: 0,
        aliasReferences: 1,
        nullReferences: 1,
        firstTraversedAssetIndex: 0,
        firstTraversedAssetType: 5,
        firstTraversedAssetTypeName: "techset",
        firstTraversedAssetReference: 0xffff_ffff,
        stoppedBeforeAssetBody: false,
        assetBodiesEntered: 3,
        techniqueSetName: "web/synthetic_techset",
        firstTechniqueSlot: 4,
        techniquePassCount: 1,
        vertexStreamCount: 3,
        vertexStreamRoutingHash: 0x5bc9_b27c,
        vertexDeclarationPrepared: true,
        vertexShaderName: "web_synthetic_vs",
        vertexShaderProgramDwords: 101,
        vertexShaderProgramHash: 0x2d83_7139,
        vertexShaderInstructionCount: 15,
        vertexShaderConstantCount: 2,
        pixelShaderName: "web_synthetic_vs",
        pixelShaderProgramDwords: 50,
        pixelShaderInstructionCount: 6,
        pixelShaderConstantCount: 1,
        shaderArgumentCount: 3,
        techniqueName: "web_synthetic2d",
        shaderSubstitutionId: "webgl2.vertcol_simple2d.v1",
        shaderCompatibilitySelected: true,
        assetTableBlock4Offset: 28,
        techniqueSetBlock0Offset: 0,
        techniqueBlock4Offset: 92,
        vertexDeclarationBlock4Offset: 120,
        vertexShaderBlock4Offset: 220,
        vertexShaderProgramBlock4Offset: 256,
        pixelShaderBlock4Offset: 660,
        pixelShaderProgramBlock4Offset: 676,
        shaderArgumentsBlock4Offset: 876,
        block0HighWaterAtBoundary: 148,
        block4CursorAtBoundary: 1688,
        completedAssetCount: 3,
        techniqueSetPublished: true,
        materialTechniqueSetName: "web/material_techset",
        materialName: "web_cursor",
        imageName: "synthetic_engine_asset",
        materialImagePath: "images/synthetic_engine_asset.iwi",
        materialAssetIndex: 2,
        materialTextureCount: 1,
        materialImage: {
            name: "synthetic_engine_asset",
            path: "images/synthetic_engine_asset.iwi",
            width: 4,
            height: 4,
            depth: 1,
            serializedFormat: 0x3154_5844,
            resourceBytes: 0,
            identity: 3,
        },
        compatibilityTechniqueSetIdentity: 1,
        materialTechniqueSetIdentity: 2,
        imageIdentity: 3,
        materialIdentity: 4,
        registryAssetCount: 4,
        registryAliasCount: 4,
        registryDefinedAliasCount: 4,
        materialTechniqueSetBlock0Offset: 0,
        materialTechniqueBlock4Offset: 940,
        materialBlock0Offset: 0,
        materialNameBlock4Offset: 1628,
        materialTextureTableBlock4Offset: 1640,
        imageBlock0Offset: 80,
        imageNameBlock4Offset: 1652,
        imageTextureInsertPointerBlock4Offset: 1676,
        imageLoadDefBlock0Offset: 116,
        materialStateBitsBlock4Offset: 1680,
        materialTechniqueSetPublished: true,
        materialPublished: true,
        imagePublished: true,
        materialImageResolved: true,
        stoppedBeforeShaderCreation: false,
        unsupportedOperation: null,
        traversesAssetBodies: true,
        maxSourceChunkBytes: 64 * 1024,
        maxInflatedPrefixBytes: 512 * 1024,
        maxStepBytes: 64 * 1024,
        maxStepRecords: 64,
        worldInventory: {
            state: "ready",
            stage: "asset-boundary",
            path: "zone/english/killhouse.ff",
            assetCount: 7,
            inflatedPrefixBytes: 131,
            assetTableOrderHash: 0xd354_ab59,
            firstGfxWorldAssetIndex: 6,
            firstGfxWorldReference: 0xffff_ffff,
            assetsBeforeFirstGfxWorld: 6,
            referencesBeforeFirstGfxWorld: {
                inline: 6,
                shared: 0,
                alias: 0,
                null: 0,
            },
            firstBodyType: 5,
            firstBodyReference: 0xffff_ffff,
            nextBodyIndex: 6,
            nextBodyType: 16,
            nextBodyReference: 0xffff_ffff,
            block0HighWaterAtBoundary: 352,
            block4CursorAtBoundary: 1408,
            stoppedBeforeAssetBody: false,
            assetBodiesEntered: 4,
            completedAssetCount: 6,
            stoppedBeforeDifferentAssetType: true,
            stoppedBeforeTechniqueDependency: false,
            techniqueSets: [
                {
                    assetIndex: 0,
                    name: ",web/mc_l_sm_r0c0s0",
                    worldVertFormat: 0,
                    remapReference: 0,
                    block0Offset: 0,
                    nameBlock4Offset: 72,
                    boundaryInflatedOffset: 299,
                    firstTechniqueSlot: 0xffff_ffff,
                    firstTechniqueReference: 0,
                    references: { null: 34, inline: 0, shared: 0, alias: 0 },
                    identity: 1,
                    published: true,
                },
                {
                    assetIndex: 1,
                    name: ",web/mc_l_sm_r0c0s1",
                    worldVertFormat: 0,
                    remapReference: 0,
                    block0Offset: 0,
                    nameBlock4Offset: 92,
                    boundaryInflatedOffset: 467,
                    firstTechniqueSlot: 0xffff_ffff,
                    firstTechniqueReference: 0,
                    references: { null: 34, inline: 0, shared: 0, alias: 0 },
                    identity: 2,
                    published: true,
                },
                {
                    assetIndex: 3,
                    name: ",web/mc_l_sm_r0c0n0s0",
                    worldVertFormat: 0,
                    remapReference: 0,
                    block0Offset: 0,
                    nameBlock4Offset: 928,
                    boundaryInflatedOffset: 2517,
                    firstTechniqueSlot: 0xffff_ffff,
                    firstTechniqueReference: 0,
                    references: { null: 34, inline: 0, shared: 0, alias: 0 },
                    identity: 7,
                    published: true,
                },
                {
                    assetIndex: 4,
                    name: ",web/mc_l_sm_r0c0n0s1",
                    worldVertFormat: 0,
                    remapReference: 0,
                    block0Offset: 0,
                    nameBlock4Offset: 950,
                    boundaryInflatedOffset: 2687,
                    firstTechniqueSlot: 0xffff_ffff,
                    firstTechniqueReference: 0,
                    references: { null: 34, inline: 0, shared: 0, alias: 0 },
                    identity: 8,
                    published: true,
                },
            ],
            postXModelTechniqueSets: [
                {
                    assetIndex: 3,
                    name: ",web/mc_l_sm_r0c0n0s0",
                    identity: 7,
                    published: true,
                    source: "generated-loader-after-first-xmodel",
                },
                {
                    assetIndex: 4,
                    name: ",web/mc_l_sm_r0c0n0s1",
                    identity: 8,
                    published: true,
                    source: "generated-loader-after-first-xmodel",
                },
            ],
            postXModelTechniqueSet: {
                assetIndex: 3,
                name: ",web/mc_l_sm_r0c0n0s0",
                identity: 7,
                published: true,
                references: { null: 34, inline: 0, shared: 0, alias: 0 },
                source: "generated-loader-after-first-xmodel",
            },
            postXModelTechniqueSetRun: {
                firstAssetIndex: 3,
                bodiesEntered: 2,
                completedCount: 2,
                nextBodyIndex: 6,
                nextBodyType: 16,
                nextBodyReference: 0xffff_ffff,
                stoppedBeforeDifferentAssetType: true,
                stoppedBeforeTechniqueDependency: false,
            },
            firstTechniqueSet: {
                name: ",web/mc_l_sm_r0c0s0",
                worldVertFormat: 0,
                remapReference: 0,
                block0Offset: 0,
                nameBlock4Offset: 72,
                boundaryInflatedOffset: 299,
                firstTechniqueSlot: 0xffff_ffff,
                firstTechniqueReference: 0,
                references: { null: 34, inline: 0, shared: 0, alias: 0 },
                identity: 1,
                registryAliasCount: 11,
                registryDefinedAliasCount: 11,
                published: true,
                stoppedBeforeDependency: false,
                unsupportedOperation: "",
            },
            firstXModel: {
                assetIndex: 2,
                name: "web/xmodel_wall",
                numBones: 1,
                numRootBones: 1,
                surfaceCount: 6,
                references: {
                    collisionSurfaces: 0xffff_ffff,
                    physPreset: 0,
                    physGeoms: 0,
                },
                collisionSurfaceCount: 1,
                offsets: {
                    materialHandlesBlock4: 640,
                    collisionSurfacesBlock4: 796,
                    boneInfoBlock4: 888,
                },
                totals: {
                    vertices: 19,
                    triangles: 7,
                    rigidVertLists: 6,
                    collisionNodes: 1,
                    collisionLeaves: 1,
                    surfacePayloadBytes: 1140,
                    collisionTriangles: 1,
                    collisionPayloadBytes: 92,
                },
                boundaryInflatedOffset: 2347,
                headerTraversed: true,
                skeletonPrefixTraversed: true,
                surfaceHeadersTraversed: true,
                surfaceDependenciesTraversed: true,
                materialHandlesTraversed: true,
                materialsTraversed: true,
                collisionSurfacesTraversed: true,
                boneInfoTraversed: true,
                physPresetTraversed: true,
                physGeomsTraversed: true,
                published: true,
                identity: 6,
                stoppedBeforeSurfaceArray: false,
                stoppedBeforeMaterialDependency: false,
                unsupportedOperation: "",
                boneNames: [{
                    index: 0,
                    scriptStringIndex: 0,
                    name: "tag_origin",
                    classification: 0,
                }],
                materialReferences: [
                    { index: 0, reference: 0xffff_ffff, identity: 4 },
                    { index: 1, reference: 0xffff_ffff, identity: 5 },
                    { index: 2, reference: 0x4000_0281, identity: 4 },
                    { index: 3, reference: 0x4000_0285, identity: 5 },
                    { index: 4, reference: 0x4000_0281, identity: 4 },
                    { index: 5, reference: 0x4000_0285, identity: 5 },
                ],
                materials: [
                    {
                        handleIndex: 0,
                        name: "web/material_a",
                        techniqueSetIdentity: 1,
                        textureCount: 1,
                        constantCount: 1,
                        stateBitsCount: 1,
                        identity: 4,
                        published: true,
                        images: [{
                            textureIndex: 0,
                            name: "synthetic_engine_asset",
                            mapType: 3,
                            dimensions: [4, 4, 1],
                            format: 0x3154_5844,
                            resourceBytes: 0,
                            identity: 3,
                            loadDefTraversed: true,
                            published: true,
                        }],
                    },
                    {
                        handleIndex: 1,
                        name: "web/material_b",
                        techniqueSetIdentity: 2,
                        textureCount: 1,
                        constantCount: 0,
                        stateBitsCount: 1,
                        identity: 5,
                        published: true,
                        images: [],
                    },
                ],
                collisionSurfaces: [{
                    index: 0,
                    triangleCount: 1,
                    boneIndex: 0,
                    traversed: true,
                }],
            },
            fxEffects: [],
            xmodels: [
                { assetIndex: 2, identity: 6, published: true },
                {
                    assetIndex: 5,
                    name: "web/xmodel_second",
                    numBones: 1,
                    numRootBones: 1,
                    surfaceCount: 3,
                    lodCount: 1,
                    headerTraversed: true,
                    skeletonPrefixTraversed: true,
                    surfaceHeadersTraversed: true,
                    surfaceDependenciesTraversed: true,
                    materialHandlesTraversed: true,
                    materialsTraversed: true,
                    collisionSurfacesTraversed: true,
                    boneInfoTraversed: true,
                    physPresetTraversed: true,
                    physGeomsTraversed: true,
                    published: true,
                    identity: 10,
                    stoppedBeforeSurfaceArray: false,
                    stoppedBeforeMaterialDependency: false,
                    unsupportedOperation: "",
                    totals: {
                    vertices: 9,
                    triangles: 3,
                    rigidVertLists: 3,
                    collisionNodes: 0,
                    collisionLeaves: 0,
                    collisionTriangles: 1,
                    collisionPayloadBytes: 92,
                },
                boneNames: [{
                    index: 0,
                    scriptStringIndex: 0,
                    name: "tag_origin",
                    classification: 0,
                }],
                materialReferences: [
                    { index: 0, reference: 0xffff_ffff, identity: 9 },
                    { index: 1, reference: 0x4000_04f1, identity: 9 },
                    { index: 2, reference: 0x4000_04f1, identity: 9 },
                ],
                materials: [{
                    handleIndex: 0,
                    name: "web/material_second",
                    techniqueSetIdentity: 1,
                    textureCount: 1,
                    constantCount: 0,
                    stateBitsCount: 0,
                    identity: 9,
                    published: true,
                }],
                collisionSurfaces: [{
                    index: 0,
                    triangleCount: 1,
                    boneIndex: 0,
                    traversed: true,
                }],
                },
            ],
            typeCounts: [
                { type: 3, name: "xmodel", count: 2 },
                { type: 5, name: "techset", count: 4 },
                { type: 16, name: "gfx_map", count: 1 },
            ],
            typesBeforeFirstGfxWorld: [
                { type: 3, name: "xmodel", count: 2 },
                { type: 5, name: "techset", count: 4 },
            ],
        },
    });
    expect(result.census.generation).toBeGreaterThan(0);
    expect(result.census.sourceBytesRead).toBeGreaterThan(12);
    expect(result.census.sourceBytesConsumed).toBeGreaterThan(12);
    expect(result.census.sourceBytesConsumed).toBeLessThanOrEqual(result.census.sourceBytesRead);
    expect(result.census.sourceFeedCount).toBeGreaterThan(0);
    expect(result.census.worldInventory.fileSize).toBeGreaterThan(12);
    expect(result.census.worldInventory.firstXModel.surfaces[0].verticesHash)
        .not.toBe(0x811c_9dc5);
    expect(result.census.worldInventory.firstXModel.surfaces[0].indicesHash)
        .not.toBe(0x811c_9dc5);
    expect(result.census.worldInventory.sourceBytesRead).toBeGreaterThan(12);
    expect(result.census.worldInventory.sourceBytesConsumed).toBeLessThanOrEqual(
        result.census.worldInventory.sourceBytesRead,
    );
    expect(result.census.worldInventory.sourceFeedCount).toBeGreaterThan(0);
    expect(result.census.worldInventory.xmodels[0].materials[0].images[0]
        .offsets.textureInsertPointerBlock4).not.toBe(0xffff_ffff);
    expect(result.census.worldInventory.xmodels[1].materials[0].textures[0])
        .toMatchObject({
            imageReference: 0x4000_02b1,
            imageIdentity: 3,
            resolved: true,
        });
    expect(result.census.pixelShaderProgramHash).toBeGreaterThan(0);
    expect(result.census.shaderArgumentHash).toBeGreaterThan(0);
    expect(result.census.vertexGlslHash).toBeGreaterThan(0);
    expect(result.census.fragmentGlslHash).toBeGreaterThan(0);
    expect(result.rendererShader).toMatchObject({
        substitutionId: "webgl2.vertcol_simple2d.v1",
        vertexSourceHash: result.census.vertexGlslHash,
        fragmentSourceHash: result.census.fragmentGlslHash,
        retained: true,
        firstDrawCompleted: true,
        vertexAttributes: ["a_position", "a_color", "a_texcoord0"],
        uniforms: ["u_viewProjectionMatrix", "u_worldMatrix", "u_colorMapSampler"],
        textureUnit: 0,
    });
    expect(result.archive).toMatchObject({
        state: "ready",
        path: "main/iw_00.iwd",
        targetMember: null,
        targetMemberAvailable: true,
    });
    expect(result.engineAsset).toMatchObject({
        state: "ready",
        path: "images/$black.iwi",
        selectionSource: "archive-probe",
    });
    expect(result.rendererTexture).toMatchObject({
        state: "ready",
        path: "images/$black.iwi",
        sourceFormat: IWI_FORMAT_DXT1,
        width: 4,
        height: 4,
        payloadBytes: 64,
        gpuFormat: "rgba8",
        recoveryBytes: 64,
        resident: true,
    });
    expect(result.rendererSurface).toMatchObject({
        state: "ready",
        vertexCount: 4,
        indexCount: 6,
        drawIndexCount: 6,
        drawCount: 1,
        textureCount: 1,
        topology: "triangle-list",
        textureBinding: "engine-image",
        resident: true,
    });
    expect(result.rendererSurface.submissionGeneration).toBe(1);
    expect(["ready", "lost"]).toContain(result.rendererShader.state);
    expect(result.rendererShader.resident).toBe(result.rendererShader.state === "ready");
    expect(result.rendererShader.submissionGeneration).toBeGreaterThan(0);
    expect(result.rendererShader.resourceGeneration).toBeGreaterThan(0);
    expect(result.rendererShader.drawCount).toBeGreaterThan(0);
    expect(result.rendererShaderEvents.some(
        (event) => event.state === "ready" && !event.firstDrawCompleted,
    )).toBe(true);
    expect(result.rendererShaderEvents.some(
        (event) => event.state === "ready" && event.firstDrawCompleted,
    )).toBe(true);
    expect(result.census.blockSizes).toEqual([
        { block: 0, size: 498_816 },
        { block: 1, size: 0 },
        { block: 2, size: 0 },
        { block: 3, size: 0 },
        { block: 4, size: 407_412 },
        { block: 5, size: 0 },
        { block: 6, size: 0 },
        { block: 7, size: 4_224 },
        { block: 8, size: 480 },
    ]);
    expect(result.census.typeCounts).toEqual([
        { type: 4, name: "material", count: 1 },
        { type: 5, name: "techset", count: 2 },
        { type: 22, name: "localize", count: 1 },
        { type: 32, name: "stringtable", count: 1 },
    ]);
    expect(result.events.map(({ stage }) => stage)).toEqual(expect.arrayContaining([
        "stat",
        "world-stat",
        "asset-boundary",
    ]));
    expect(result.archiveEvents.every(({ censusState }) => censusState === "ready")).toBe(true);
    const firstGeneration = result.census.generation;
    await page.evaluate(() => {
        const runtime = globalThis.__KISAKCOD_WEB__;
        runtime.module._KisakWeb_StartRetailCensus();
        runtime.module._KisakWeb_CancelRetailCensus();
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__.retailCensus.state),
    ).toBe("idle");
    const cancelledGeneration = await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.retailCensus.generation,
    );
    expect(cancelledGeneration).toBeGreaterThan(firstGeneration);
    await page.evaluate(() => {
        globalThis.__KISAKCOD_WEB__.module._KisakWeb_StartRetailCensus();
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__.retailCensus.state),
        { timeout: 30_000 },
    ).toBe("ready");
    const restarted = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(restarted.generation).toBeGreaterThan(cancelledGeneration);
    expect(restarted.assetCount).toBe(5);
});

test("a truncated retail prefix fails closed and does not start the archive", async ({ page }, testInfo) => {
    const overrides = new Map([
        ["zone/english/code_post_gfx.ff", createSyntheticFastfileHeader()],
    ]);
    await importInstall(page, testInfo, "retail-census-truncated", { overrides });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const result = await page.evaluate(() => ({
        census: structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
    }));
    expect(result.census).toMatchObject({
        state: "failed",
        error: "zlib prefix truncated",
        completedAssetCount: 0,
        techniqueSetPublished: false,
        failClosed: true,
    });
    expect(result.archive.state).toBe("idle");
});

test("a WebGL2 binding failure keeps the bootstrap renderer active", async ({ page }, testInfo) => {
    await page.addInitScript(() => {
        globalThis.__KISAKCOD_WORKER_TEST_CONFIG__ = { failCompatibilityBinding: true };
        globalThis.__retailFallbackDraws = 0;
        const compatibilityPrograms = new WeakSet();
        const shaderSources = new WeakMap();
        const programShaders = new WeakMap();
        const originalShaderSource = WebGL2RenderingContext.prototype.shaderSource;
        Object.defineProperty(WebGL2RenderingContext.prototype, "shaderSource", {
            configurable: true,
            writable: true,
            value(shader, source) {
                shaderSources.set(shader, String(source));
                return originalShaderSource.call(this, shader, source);
            },
        });
        const originalAttachShader = WebGL2RenderingContext.prototype.attachShader;
        Object.defineProperty(WebGL2RenderingContext.prototype, "attachShader", {
            configurable: true,
            writable: true,
            value(program, shader) {
                const shaders = programShaders.get(program) ?? [];
                shaders.push(shader);
                programShaders.set(program, shaders);
                return originalAttachShader.call(this, program, shader);
            },
        });
        const originalLinkProgram = WebGL2RenderingContext.prototype.linkProgram;
        Object.defineProperty(WebGL2RenderingContext.prototype, "linkProgram", {
            configurable: true,
            writable: true,
            value(program) {
                if ((programShaders.get(program) ?? []).some(
                    (shader) => shaderSources.get(shader)?.includes("u_viewProjectionMatrix"),
                )) compatibilityPrograms.add(program);
                return originalLinkProgram.call(this, program);
            },
        });
        const originalGetAttribLocation = WebGL2RenderingContext.prototype.getAttribLocation;
        Object.defineProperty(WebGL2RenderingContext.prototype, "getAttribLocation", {
            configurable: true,
            writable: true,
            value(program, name) {
                if (compatibilityPrograms.has(program) && name === "a_texcoord0") return -1;
                return originalGetAttribLocation.call(this, program, name);
            },
        });
        const originalDrawElements = WebGL2RenderingContext.prototype.drawElements;
        Object.defineProperty(WebGL2RenderingContext.prototype, "drawElements", {
            configurable: true,
            writable: true,
            value(...args) {
                globalThis.__retailFallbackDraws += 1;
                return originalDrawElements.apply(this, args);
            },
        });
    });
    await importInstall(page, testInfo, "retail-shader-binding-failure");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("ready");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.rendererShader?.state),
    ).toBe("failed");
    const result = await page.evaluate(() => ({
        shader: structuredClone(globalThis.__KISAKCOD_WEB__.rendererShader),
        census: structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
        surface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
        draws: globalThis.__retailFallbackDraws,
        runtimeState: globalThis.__KISAKCOD_WEB__.state,
    }));
    expect(result.shader).toMatchObject({
        state: "failed",
        substitutionId: "webgl2.vertcol_simple2d.v1",
        retained: false,
        resident: false,
        firstDrawCompleted: false,
    });
    expect(result.surface).toMatchObject({
        vertexCount: 4,
        indexCount: 6,
        submissionGeneration: 1,
    });
    expect(result.runtimeState).toBe("running");
});

test("rebuilds the selected shader program after WebGL2 context loss", async ({ page }, testInfo) => {
    await observeRetailShaderRenderer(page);
    await importInstall(page, testInfo, "retail-shader-context-recovery", {
        primaryIwd: M20_PRIMARY_IWD,
    });
    await expect.poll(
        () => page.evaluate(
            () => globalThis.__KISAKCOD_WEB__?.rendererShader?.firstDrawCompleted,
        ),
    ).toBe(true);
    const extensionAvailable = await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestLoseWebGLContext"));
    expect(Boolean(extensionAvailable)).toBe(true);
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.rendererShader?.state),
    ).toBe("lost");
    await page.evaluate(() =>
        globalThis.__KISAKCOD_WEB__.module.call("_KisakWeb_TestRestoreWebGLContext"));
    await expect.poll(
        () => page.evaluate(() => ({
            state: globalThis.__KISAKCOD_WEB__?.rendererShader?.state,
            resident: globalThis.__KISAKCOD_WEB__?.rendererShader?.resident,
            recoveries: globalThis.__KISAKCOD_WEB__?.rendererShader?.recoveryCount,
        })),
        { timeout: 15_000 },
    ).toMatchObject({ state: "ready", resident: true, recoveries: 1 });
    const result = await page.evaluate(() => ({
        shader: structuredClone(globalThis.__KISAKCOD_WEB__.rendererShader),
        links: globalThis.__retailShaderLinkCount,
        events: structuredClone(globalThis.__retailRendererShaderEvents),
    }));
    expect(result.shader).toMatchObject({
        substitutionId: "webgl2.vertcol_simple2d.v1",
        retained: true,
        resident: true,
        firstDrawCompleted: true,
        recoveryCount: 1,
    });
    expect(result.events.some((event) => event.state === "lost")).toBe(true);
    expect(result.events.some(
        (event) => event.state === "ready" && event.recoveryCount === 1,
    )).toBe(true);
});

test("an invalid vertex program fails before shader creation or asset publication", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const valid = createSyntheticRetailCensusFastfile();
    const inflated = inflateSync(valid.subarray(12));
    inflated.writeUInt32LE(0x0000_0101, 458);
    const malformed = Uint8Array.from([
        ...valid.subarray(0, 12),
        ...deflateSync(inflated, { level: 9 }),
    ]);
    const overrides = new Map([
        ["zone/english/code_post_gfx.ff", malformed],
    ]);
    await importInstall(page, testInfo, "retail-census-bad-shader", { overrides });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const result = await page.evaluate(() => ({
        census: structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
    }));
    expect(result.census).toMatchObject({
        state: "failed",
        stage: "failed",
        error: "invalid Direct3D vertex shader signature",
        completedAssetCount: 0,
        techniqueSetPublished: false,
        failClosed: true,
    });
    expect(result.archive.state).toBe("idle");
});

test("a map table without GfxWorld fails before publishing startup assets", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([
        ["zone/english/killhouse.ff",
            createSyntheticWorldInventoryFastfile({ includeWorld: false })],
    ]);
    await importInstall(page, testInfo, "retail-census-missing-world", { overrides });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const result = await page.evaluate(() => ({
        census: structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
        rendererShader: structuredClone(globalThis.__KISAKCOD_WEB__.rendererShader),
    }));
    expect(result.census).toMatchObject({
        state: "failed",
        path: "zone/english/killhouse.ff",
        stage: "failed",
        error: "fastfile contains no GfxWorld asset",
        completedAssetCount: 0,
        techniqueSetPublished: false,
        failClosed: true,
    });
    expect(result.archive.state).toBe("idle");
    expect(result.rendererShader.substitutionId).not.toBe("webgl2.vertcol_simple2d.v1");
});

test("an incomplete reusable technique dependency fails closed", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([
        ["zone/english/killhouse.ff",
            createSyntheticWorldInventoryFastfile({ secondTechniqueDependency: true })],
    ]);
    await importInstall(page, testInfo, "retail-census-later-technique-dependency", {
        overrides,
        primaryIwd: M20_PRIMARY_IWD,
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        stage: "failed",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("a forged first-XModel bone-array alias fails closed", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([
        ["zone/english/killhouse.ff",
            createSyntheticWorldInventoryFastfile({ unsupportedXModelBoneNames: true })],
    ]);
    await importInstall(page, testInfo, "retail-census-xmodel-dependency", {
        overrides,
        primaryIwd: M20_PRIMARY_IWD,
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        error: "invalid XModel bone script-string array alias",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("invalid XModel bounds fail without exposing prior technique publications", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([
        ["zone/english/killhouse.ff",
            createSyntheticWorldInventoryFastfile({ invalidXModelBounds: true })],
    ]);
    await importInstall(page, testInfo, "retail-census-invalid-xmodel-bounds", {
        overrides,
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        path: "zone/english/killhouse.ff",
        stage: "failed",
        error: "invalid XModel bounds",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("an out-of-range XModel bone script string fails closed", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([
        ["zone/english/killhouse.ff",
            createSyntheticWorldInventoryFastfile({ invalidXModelBoneString: true })],
    ]);
    await importInstall(page, testInfo, "retail-census-invalid-xmodel-bone", {
        overrides,
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        path: "zone/english/killhouse.ff",
        stage: "failed",
        error: "invalid XModel bone script string",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("an XSurface pointer/count mismatch fails without a partial model", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([
        ["zone/english/killhouse.ff",
            createSyntheticWorldInventoryFastfile({ invalidXSurfaceLayout: true })],
    ]);
    await importInstall(page, testInfo, "retail-census-invalid-xsurface", { overrides });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        path: "zone/english/killhouse.ff",
        stage: "failed",
        error: "unsupported XSurface layout",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("an invalid XSurface collision tree fails closed", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([
        ["zone/english/killhouse.ff",
            createSyntheticWorldInventoryFastfile({ invalidXSurfaceCollision: true })],
    ]);
    await importInstall(page, testInfo, "retail-census-invalid-xsurface-tree", { overrides });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        path: "zone/english/killhouse.ff",
        stage: "failed",
        error: "invalid XSurface collision tree",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

for (const [title, fixtureOptions, expectedError] of [
    [
        "an undefined XModel image alias fails closed",
        { invalidXModelMaterialAlias: true },
        "invalid XModel GfxImage dependency alias",
    ],
    [
        "invalid XModel collision bounds fail closed",
        { invalidXModelCollisionBounds: true },
        "invalid XModel collision surface",
    ],
    [
        "invalid XModel bone info fails closed",
        { invalidXModelBoneInfo: true },
        "invalid XModel bone info",
    ],
]) {
    test(title, { tag: "@native-covered" }, async ({ page }, testInfo) => {
        const overrides = new Map([[
            "zone/english/killhouse.ff",
            createSyntheticWorldInventoryFastfile(fixtureOptions),
        ]]);
        await importInstall(page, testInfo, `m26-${title.replaceAll(" ", "-")}`, {
            overrides,
        });
        await expect.poll(
            () => page.evaluate(
                () => globalThis.__KISAKCOD_WEB__?.retailCensus?.state,
            ),
            { timeout: 30_000 },
        ).toBe("failed");
        const census = await page.evaluate(
            () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
        );
        expect(census).toMatchObject({
            state: "failed",
            path: "zone/english/killhouse.ff",
            stage: "failed",
            error: expectedError,
            completedAssetCount: 0,
            failClosed: true,
        });
        expect(census.worldInventory).toBeUndefined();
    });
}

test("an inline physics preset publishes before its XModel", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([[
        "zone/english/killhouse.ff",
        createSyntheticWorldInventoryFastfile({ xModelPhysPreset: true }),
    ]]);
    await importInstall(page, testInfo, "m39-inline-physics", { overrides });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("ready");
    const world = await page.evaluate(
        () => structuredClone(
            globalThis.__KISAKCOD_WEB__.retailCensus.worldInventory,
        ),
    );
    expect(world).toMatchObject({
        completedAssetCount: 3,
        firstTechniqueSet: {
            registryAliasCount: 6,
            registryDefinedAliasCount: 6,
            unsupportedOperation: "",
        },
        firstXModel: {
            identity: 7,
            published: true,
            physPresetIdentity: 6,
            physPresetTraversed: true,
            physGeomsTraversed: true,
            physPreset: {
                identity: 6,
                name: "web/phys_sandbag",
                soundAliasPrefix: "sandbag",
                mass: 100,
                bounce: 0.25,
                friction: 0.5,
                tempDefaultToCylinder: true,
                traversed: true,
                published: true,
            },
            unsupportedOperation: "",
        },
    });
});

for (const [title, fixtureOptions, expectedError] of [
    [
        "non-finite physics preset values fail closed",
        { xModelPhysPreset: true, invalidXModelPhysPresetValues: true },
        "invalid physics preset values",
    ],
    [
        "invalid physics preset sound aliases fail closed",
        { xModelPhysPreset: true, invalidXModelPhysPresetSoundAlias: true },
        "invalid physics preset sound alias prefix",
    ],
]) {
    test(title, { tag: "@native-covered" }, async ({ page }, testInfo) => {
        const overrides = new Map([[
            "zone/english/killhouse.ff",
            createSyntheticWorldInventoryFastfile(fixtureOptions),
        ]]);
        await importInstall(page, testInfo, `m39-${title.replaceAll(" ", "-")}`, {
            overrides,
        });
        await expect.poll(
            () => page.evaluate(
                () => globalThis.__KISAKCOD_WEB__?.retailCensus?.state,
            ),
            { timeout: 30_000 },
        ).toBe("failed");
        const census = await page.evaluate(
            () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
        );
        expect(census).toMatchObject({
            state: "failed",
            path: "zone/english/killhouse.ff",
            stage: "failed",
            error: expectedError,
            completedAssetCount: 0,
            failClosed: true,
        });
        expect(census.worldInventory).toBeUndefined();
    });
}

test("an invalid map technique-set header fails before publishing asset zero", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([
        ["zone/english/killhouse.ff",
            createSyntheticWorldInventoryFastfile({ invalidTechniqueSet: true })],
    ]);
    await importInstall(page, testInfo, "retail-census-invalid-world-techset", { overrides });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        path: "zone/english/killhouse.ff",
        stage: "failed",
        error: "unsupported technique-set layout",
        completedAssetCount: 0,
        techniqueSetPublished: false,
        failClosed: true,
    });
});

test("an invalid later map technique-set header exposes no partial prefix", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([
        ["zone/english/killhouse.ff",
            createSyntheticWorldInventoryFastfile({ invalidSecondTechniqueSet: true })],
    ]);
    await importInstall(page, testInfo, "retail-census-invalid-later-world-techset", {
        overrides,
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        path: "zone/english/killhouse.ff",
        stage: "failed",
        error: "unsupported technique-set layout",
        completedAssetCount: 0,
        techniqueSetPublished: false,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("publishes the consecutive typed technique-set run after the XModel", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    await importInstall(page, testInfo, "m31-post-xmodel-techset-run");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("ready");
    const world = await page.evaluate(
        () => structuredClone(
            globalThis.__KISAKCOD_WEB__.retailCensus.worldInventory,
        ),
    );
    expect(world).toMatchObject({
        completedAssetCount: 6,
        nextBodyIndex: 6,
        nextBodyType: 16,
        stoppedBeforeDifferentAssetType: true,
        postXModelTechniqueSets: [
            {
                assetIndex: 3,
                name: ",web/mc_l_sm_r0c0n0s0",
                identity: 7,
                published: true,
            },
            {
                assetIndex: 4,
                name: ",web/mc_l_sm_r0c0n0s1",
                identity: 8,
                published: true,
            },
        ],
        postXModelTechniqueSet: {
            assetIndex: 3,
            name: ",web/mc_l_sm_r0c0n0s0",
            firstTechniqueSlot: 0xffff_ffff,
            references: { null: 34, inline: 0, shared: 0, alias: 0 },
            identity: 7,
            published: true,
            source: "generated-loader-after-first-xmodel",
        },
        postXModelTechniqueSetRun: {
            firstAssetIndex: 3,
            bodiesEntered: 2,
            completedCount: 2,
            nextBodyIndex: 6,
            nextBodyType: 16,
            stoppedBeforeDifferentAssetType: true,
            stoppedBeforeTechniqueDependency: false,
        },
        firstXModel: { assetIndex: 2, identity: 6, published: true },
    });
});

test("invalid second-XModel material alias exposes no partial public result", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([[
        "zone/english/killhouse.ff",
        createSyntheticWorldInventoryFastfile({
            invalidSecondXModelMaterialAlias: true,
        }),
    ]]);
    await importInstall(page, testInfo, "m34-invalid-second-material", {
        overrides,
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        path: "zone/english/killhouse.ff",
        error: "material technique-set reference is invalid",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("resolves a reusable shared GfxImage alias after insertion-pointer planning", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    await importInstall(page, testInfo, "m38-shared-image-alias");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("ready");
    const world = await page.evaluate(
        () => structuredClone(
            globalThis.__KISAKCOD_WEB__.retailCensus.worldInventory,
        ),
    );
    const sourceImage = world.xmodels[0].materials[0].images[0];
    expect(sourceImage).toMatchObject({
        textureReference: 0xffff_fffe,
        identity: 3,
        published: true,
    });
    expect(sourceImage.offsets.textureInsertPointerBlock4)
        .not.toBe(0xffff_ffff);
    expect(world.xmodels[1].materials[0].textures[0]).toMatchObject({
        imageReference: 0x4000_02b1,
        imageIdentity: 3,
        resolved: true,
    });
    expect(world.xmodels[1]).toMatchObject({
        assetIndex: 5,
        identity: 10,
        published: true,
    });
});

test("undefined reusable GfxImage alias fails before parent publication", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([[
        "zone/english/killhouse.ff",
        createSyntheticWorldInventoryFastfile({
            invalidSecondXModelImageAlias: true,
        }),
    ]]);
    await importInstall(page, testInfo, "m38-invalid-image-alias", {
        overrides,
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        path: "zone/english/killhouse.ff",
        error: "invalid XModel GfxImage dependency alias",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("invalid second-XSurface layout exposes no partial public result", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([[
        "zone/english/killhouse.ff",
        createSyntheticWorldInventoryFastfile({
            invalidSecondXSurfaceLayout: true,
        }),
    ]]);
    await importInstall(page, testInfo, "m33-invalid-second-xsurface", {
        overrides,
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        path: "zone/english/killhouse.ff",
        error: "unsupported XSurface layout",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("invalid second-XModel bounds expose no partial public result", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([[
        "zone/english/killhouse.ff",
        createSyntheticWorldInventoryFastfile({ invalidSecondXModelBounds: true }),
    ]]);
    await importInstall(page, testInfo, "m32-invalid-second-xmodel", { overrides });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        path: "zone/english/killhouse.ff",
        error: "invalid XModel bounds",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("a forged second-XModel bone-array alias exposes no partial world", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([[
        "zone/english/killhouse.ff",
        createSyntheticWorldInventoryFastfile({
            unsupportedSecondXModelBoneNames: true,
        }),
    ]]);
    await importInstall(page, testInfo, "m32-unsupported-second-xmodel", {
        overrides,
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        error: "invalid XModel bone script-string array alias",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("a malformed post-XModel technique set fails without publishing the prior prefix", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([[
        "zone/english/killhouse.ff",
        createSyntheticWorldInventoryFastfile({ invalidPostXModelTechniqueSet: true }),
    ]]);
    await importInstall(page, testInfo, "m30-invalid-post-xmodel-techset", { overrides });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        path: "zone/english/killhouse.ff",
        stage: "failed",
        error: "unsupported technique-set layout",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("a malformed later post-XModel technique set exposes no partial run", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([[
        "zone/english/killhouse.ff",
        createSyntheticWorldInventoryFastfile({
            invalidLaterPostXModelTechniqueSet: true,
        }),
    ]]);
    await importInstall(page, testInfo, "m31-invalid-later-post-xmodel-techset", {
        overrides,
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        path: "zone/english/killhouse.ff",
        stage: "failed",
        error: "unsupported technique-set layout",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("completes both reusable MaterialTechnique dependencies before publishing", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([[
        "zone/english/killhouse.ff",
        createSyntheticWorldInventoryFastfile({
            completePostXModelTechniqueDependencies: true,
        }),
    ]]);
    await importInstall(page, testInfo, "m37-material-technique-loader", { overrides });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("ready");
    const world = await page.evaluate(
        () => structuredClone(
            globalThis.__KISAKCOD_WEB__.retailCensus.worldInventory,
        ),
    );
    expect(world).toMatchObject({
        completedAssetCount: 7,
        nextBodyIndex: 7,
        nextBodyType: 16,
        stoppedBeforeDifferentAssetType: true,
        firstTechniqueSet: {
            registryAliasCount: 12,
            registryDefinedAliasCount: 12,
            unsupportedOperation: "",
        },
        postXModelTechniqueSetRun: {
            firstAssetIndex: 3,
            bodiesEntered: 3,
            completedCount: 3,
            nextBodyIndex: 7,
            nextBodyType: 16,
            stoppedBeforeTechniqueDependency: false,
        },
        xmodels: [
            { assetIndex: 2, identity: 6, published: true },
            { assetIndex: 5, identity: 10, published: true },
        ],
    });
    expect(world.techniqueSets.find((entry) => entry.assetIndex === 6))
        .toMatchObject({
            assetIndex: 6,
            identity: 11,
            published: true,
            firstTechniqueSlot: 4,
            references: { null: 32, inline: 2, shared: 0, alias: 0 },
            techniques: [
                {
                    slot: 4,
                    name: "web/reusable_first",
                    passCount: 1,
                    argumentCount: 1,
                    completed: true,
                },
                {
                    slot: 28,
                    name: "web/reusable_second",
                    passCount: 1,
                    argumentCount: 1,
                    completed: true,
                },
            ],
        });
});

test("an invalid second MaterialTechnique cannot publish its parent", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([[
        "zone/english/killhouse.ff",
        createSyntheticWorldInventoryFastfile({
            completePostXModelTechniqueDependencies: true,
            invalidSecondPostXModelTechnique: true,
        }),
    ]]);
    await importInstall(page, testInfo, "m37-invalid-second-technique", { overrides });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        stage: "failed",
        error: "invalid Direct3D vertex shader signature",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});

test("an incomplete later reusable technique dependency fails closed", { tag: "@native-covered" }, async ({ page }, testInfo) => {
    const overrides = new Map([[
        "zone/english/killhouse.ff",
        createSyntheticWorldInventoryFastfile({
            laterPostXModelTechniqueDependency: true,
        }),
    ]]);
    await importInstall(page, testInfo, "m31-dependent-later-post-xmodel-techset", {
        overrides,
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
        { timeout: 30_000 },
    ).toBe("failed");
    const census = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(census).toMatchObject({
        state: "failed",
        stage: "failed",
        completedAssetCount: 0,
        failClosed: true,
    });
    expect(census.worldInventory).toBeUndefined();
});
