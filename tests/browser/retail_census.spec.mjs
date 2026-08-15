import { expect, test } from "@playwright/test";
import { deflateSync, inflateSync } from "node:zlib";
import {
    createInstallDirectory,
    createSyntheticFastfileHeader,
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

const M28_IMAGE_IWD = createSyntheticIwd([{
    path: "images/synthetic_xmodel_color.iwi",
    contents: M19_DXT1_IWI,
    method: "deflate",
}]);

const M29_IMAGE_IWD = createSyntheticIwd([
    {
        path: "images/synthetic_xmodel_color.iwi",
        contents: M19_DXT1_IWI,
        method: "deflate",
    },
    {
        path: "images/synthetic_xmodel_color_second.iwi",
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

test("publishes one retail material and binds its resolved image", async ({ page }, testInfo) => {
    await observeRetailShaderRenderer(page);
    await importInstall(page, testInfo, "retail-census-success", {
        primaryIwd: M20_PRIMARY_IWD,
        overrides: new Map([
            ["main/iw_03.iwd", M28_IMAGE_IWD],
            ["zone/english/killhouse.ff",
                createSyntheticWorldInventoryFastfile({
                    externalColorMap: true,
                    colorMapName: "synthetic_xmodel_color",
                })],
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
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.rendererMaterial?.geometrySource),
    ).toBe("retail-xmodel");

    const result = await page.evaluate(() => ({
        census: structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
        events: structuredClone(globalThis.__retailCensusEvents),
        archiveEvents: structuredClone(globalThis.__retailCensusArchiveEvents),
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
        rendererShader: structuredClone(globalThis.__KISAKCOD_WEB__.rendererShader),
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        rendererTexture: structuredClone(globalThis.__KISAKCOD_WEB__.rendererTexture),
        rendererMaterial: structuredClone(globalThis.__KISAKCOD_WEB__.rendererMaterial),
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
        block4CursorAtBoundary: 1684,
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
        imageLoadDefBlock0Offset: 116,
        materialStateBitsBlock4Offset: 1676,
        materialTechniqueSetPublished: true,
        materialPublished: true,
        imagePublished: true,
        materialImageResolved: true,
        stoppedBeforeShaderCreation: false,
        unsupportedOperation: null,
        traversesAssetBodies: true,
        maxSourceChunkBytes: 64 * 1024,
        maxInflatedPrefixBytes: 256 * 1024,
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
                renderSurface: {
                    state: "ready",
                    surfaceIndex: 0,
                    materialIdentity: 4,
                    vertexCount: 4,
                    triangleCount: 2,
                    projection: "largest-axes-orthographic-fit",
                    horizontalAxis: 0,
                    verticalAxis: 1,
                    mins: [-2, -1, 0],
                    maxs: [2, 1, 0],
                    geometrySource: "retail-xmodel",
                    fallbackPreserved: false,
                    drawList: {
                        state: "ready",
                        firstLodSurfaceIndex: 0,
                        firstLodSurfaceCount: 2,
                        drawCount: 2,
                        textureCount: 1,
                        totalVertices: 7,
                        totalIndices: 9,
                        draws: [
                            { surfaceIndex: 0, textureSlot: 0, retained: true },
                            { surfaceIndex: 1, textureSlot: 0, retained: true },
                        ],
                        textures: [{
                            textureSlot: 0,
                            imagePath: "images/synthetic_xmodel_color.iwi",
                        }],
                    },
                    colorMap: {
                        state: "selected",
                        materialName: "web/material_a",
                        imageName: "synthetic_xmodel_color",
                        imagePath: "images/synthetic_xmodel_color.iwi",
                        materialIdentity: 4,
                        imageIdentity: 3,
                        semantic: 2,
                        selectionSource: "typed-material-image-identity",
                    },
                },
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
                            name: "synthetic_xmodel_color",
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
        path: "main/iw_03.iwd",
        targetMember: "images/synthetic_xmodel_color.iwi",
        targetMemberAvailable: true,
    });
    expect(result.engineAsset).toMatchObject({
        state: "ready",
        path: "images/synthetic_xmodel_color.iwi",
        selectionSource: "retail-material",
        materialName: "web/material_a",
        imageName: "synthetic_xmodel_color",
        materialIdentity: 4,
        imageIdentity: 3,
    });
    expect(result.rendererTexture).toMatchObject({
        state: "ready",
        path: "images/synthetic_xmodel_color.iwi",
        sourceFormat: IWI_FORMAT_DXT1,
        width: 4,
        height: 4,
        payloadBytes: 64,
        gpuFormat: "rgba8",
        recoveryBytes: 64,
        resident: true,
    });
    expect(result.rendererMaterial).toMatchObject({
        state: "ready",
        materialSource: "retail-xmodel",
        materialName: "web/material_a",
        materialIdentity: 4,
        imageName: "synthetic_xmodel_color",
        imageIdentity: 3,
        imageSemantic: 2,
        shaderSubstitutionId: "webgl2.vertcol_simple2d.v1",
        sampler: "u_colorMapSampler",
        textureUnit: 0,
        imagePath: "images/synthetic_xmodel_color.iwi",
        sourceFormat: IWI_FORMAT_DXT1,
        decodedFormat: "rgba8",
        compressedSource: true,
        recoveryBytes: 64,
        geometrySource: "retail-xmodel",
    });
    expect(result.rendererSurface).toMatchObject({
        state: "ready",
        vertexCount: 7,
        indexCount: 9,
        drawIndexCount: 6,
        drawCount: 2,
        textureCount: 1,
        topology: "triangle-list",
        textureBinding: "engine-image",
        resident: true,
    });
    expect(result.rendererSurface.submissionGeneration).toBeGreaterThanOrEqual(2);
    const vertexUploads = result.surfaceUploads.filter(({ target }) => target === 0x8892);
    const indexUploads = result.surfaceUploads.filter(({ target }) => target === 0x8893);
    expect(vertexUploads.length).toBeGreaterThanOrEqual(2);
    expect(indexUploads.length).toBeGreaterThanOrEqual(2);
    const retailVertices = new Float32Array(Uint8Array.from(vertexUploads.at(-1).bytes).buffer);
    expect([...retailVertices]).toHaveLength(7 * 8);
    expect(retailVertices[0]).toBeCloseTo(-0.82, 5);
    expect(retailVertices[1]).toBeCloseTo(-0.41, 5);
    expect(retailVertices[2]).toBeCloseTo(0, 5);
    expect(retailVertices[8]).toBeCloseTo(-0.82, 5);
    expect(retailVertices[9]).toBeCloseTo(0.41, 5);
    expect(retailVertices[10]).toBeCloseTo(0, 5);
    expect([...new Uint16Array(Uint8Array.from(indexUploads.at(-1).bytes).buffer)])
        .toEqual([0, 1, 2, 2, 3, 0, 4, 5, 6]);
    expect(["ready", "lost"]).toContain(result.rendererShader.state);
    expect(result.rendererShader.resident).toBe(result.rendererShader.state === "ready");
    expect(result.rendererShader.submissionGeneration).toBeGreaterThan(0);
    expect(result.rendererShader.resourceGeneration).toBeGreaterThan(0);
    expect(result.rendererShader.drawCount).toBeGreaterThan(0);
    expect(result.shaderSources).toHaveLength(2);
    expect(result.shaderSources[0]).toContain("#version 300 es");
    expect(result.shaderSources.join("\n")).toContain("u_viewProjectionMatrix");
    expect(result.shaderSources.join("\n")).toContain("u_colorMapSampler");
    expect(result.shaderLinkCount).toBe(1);
    expect(result.shaderUseCount).toBeGreaterThan(0);
    expect(result.shaderDrawCount).toBeGreaterThan(0);
    expect(new Set(result.shaderBindings.map(({ name }) => name))).toEqual(new Set([
        "a_position", "a_color", "a_texcoord0",
        "u_viewProjectionMatrix", "u_worldMatrix", "u_colorMapSampler",
    ]));
    const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    expect(result.shaderMatrices.length).toBeGreaterThanOrEqual(2);
    const [viewProjection, world] = result.shaderMatrices.slice(0, 2);
    expect(viewProjection.transpose).toBe(false);
    expect(viewProjection.values[0]).toBeCloseTo(1, 5);
    expect(viewProjection.values[5]).toBeGreaterThan(0);
    expect(viewProjection.values[5]).toBeLessThan(1);
    expect(viewProjection.values.filter((_, index) => ![0, 5, 10, 15].includes(index)))
        .toEqual(new Array(12).fill(0));
    expect(viewProjection.values[10]).toBe(1);
    expect(viewProjection.values[15]).toBe(1);
    expect(world).toEqual({ transpose: false, values: identity });
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
    expect(result.log).toContain(
        "XModel material web/material_a selected 1 resident color map(s)",
    );

    const firstGeneration = result.census.generation;
    await page.evaluate(() => {
        const runtime = globalThis.__KISAKCOD_WEB__;
        const original = globalThis.__KISAKCOD_WEB_FS_BRIDGE__;
        runtime.__retailCensusOriginalBridge = original;
        runtime.__retailCensusHeldRead = 0;
        globalThis.__KISAKCOD_WEB_FS_BRIDGE__ = {
            stat: (...args) => original.stat(...args),
            read(requestId, path, ...args) {
                if (path === "zone/english/code_post_gfx.ff") {
                    runtime.__retailCensusHeldRead = requestId;
                    return true;
                }
                return original.read(requestId, path, ...args);
            },
            cancel(requestId) {
                if (requestId === runtime.__retailCensusHeldRead) {
                    runtime.__retailCensusHeldRead = 0;
                    return true;
                }
                return original.cancel(requestId);
            },
        };
        runtime.module._KisakWeb_StartRetailCensus();
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__.retailCensus.generation),
    ).toBe(firstGeneration + 1);
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__.__retailCensusHeldRead),
    ).toBeGreaterThan(0);
    await page.evaluate(() => {
        globalThis.__KISAKCOD_WEB__.module._KisakWeb_CancelRetailCensus();
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__.retailCensus.state),
    ).toBe("idle");
    await page.evaluate(() => {
        const runtime = globalThis.__KISAKCOD_WEB__;
        globalThis.__KISAKCOD_WEB_FS_BRIDGE__ = runtime.__retailCensusOriginalBridge;
        runtime.module._KisakWeb_StartRetailCensus();
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__.retailCensus.state),
        { timeout: 30_000 },
    ).toBe("ready");
    const restarted = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
    );
    expect(restarted.generation).toBe(firstGeneration + 3);
    expect(restarted.assetCount).toBe(5);
});

test("loads independent color maps for every supported first-LOD draw", async ({ page }, testInfo) => {
    await importInstall(page, testInfo, "retail-xmodel-draw-list", {
        primaryIwd: M20_PRIMARY_IWD,
        overrides: new Map([
            ["main/iw_03.iwd", M29_IMAGE_IWD],
            ["zone/english/killhouse.ff",
                createSyntheticWorldInventoryFastfile({
                    externalColorMap: true,
                    colorMapName: "synthetic_xmodel_color",
                    secondExternalColorMap: true,
                    secondColorMapName: "synthetic_xmodel_color_second",
                })],
        ]),
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.rendererMaterial?.state),
        { timeout: 30_000 },
    ).toBe("ready");
    const result = await page.evaluate(() => ({
        drawList: structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus
            .worldInventory.firstXModel.renderSurface.drawList),
        surface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
        textures: structuredClone(globalThis.__KISAKCOD_WEB__.rendererTextures),
        material: structuredClone(globalThis.__KISAKCOD_WEB__.rendererMaterial),
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
    }));
    expect(result.drawList).toMatchObject({
        state: "ready",
        firstLodSurfaceCount: 2,
        drawCount: 2,
        textureCount: 2,
        totalVertices: 7,
        totalIndices: 9,
        draws: [
            { surfaceIndex: 0, textureSlot: 0, retained: true },
            { surfaceIndex: 1, textureSlot: 1, retained: true },
        ],
    });
    expect(result.surface).toMatchObject({
        state: "ready",
        drawCount: 2,
        textureCount: 2,
        vertexCount: 7,
        indexCount: 9,
    });
    expect(result.textures).toMatchObject([
        {
            state: "ready",
            textureSlot: 0,
            path: "images/synthetic_xmodel_color.iwi",
            resident: true,
        },
        {
            state: "ready",
            textureSlot: 1,
            path: "images/synthetic_xmodel_color_second.iwi",
            resident: true,
        },
    ]);
    expect(result.material).toMatchObject({
        state: "ready",
        drawCount: 2,
        textureCount: 2,
        recoveryBytes: 128,
    });
    expect(result.engineAsset).toMatchObject({ state: "ready", textureSlot: 1 });
    expect(result.archive).toMatchObject({
        state: "ready",
        path: "main/iw_03.iwd",
        targetMember: "images/synthetic_xmodel_color_second.iwi",
        targetMemberAvailable: true,
    });
});

test("recreates every first-LOD texture slot after context loss", async ({ page }, testInfo) => {
    await page.addInitScript(() => {
        globalThis.__m29TextureUploads = 0;
        const original = WebGL2RenderingContext.prototype.texImage2D;
        Object.defineProperty(WebGL2RenderingContext.prototype, "texImage2D", {
            configurable: true,
            writable: true,
            value(...args) {
                if (this.canvas?.id === "game-canvas") globalThis.__m29TextureUploads += 1;
                return original.apply(this, args);
            },
        });
    });
    await importInstall(page, testInfo, "retail-xmodel-draw-list-recovery", {
        primaryIwd: M20_PRIMARY_IWD,
        overrides: new Map([
            ["main/iw_03.iwd", M29_IMAGE_IWD],
            ["zone/english/killhouse.ff",
                createSyntheticWorldInventoryFastfile({
                    externalColorMap: true,
                    colorMapName: "synthetic_xmodel_color",
                    secondExternalColorMap: true,
                    secondColorMapName: "synthetic_xmodel_color_second",
                })],
        ]),
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.rendererMaterial?.state),
        { timeout: 30_000 },
    ).toBe("ready");
    const before = await page.evaluate(() => globalThis.__m29TextureUploads);
    await page.evaluate(() => {
        const extension = document.querySelector("#game-canvas")
            .getContext("webgl2").getExtension("WEBGL_lose_context");
        globalThis.__m29LossExtension = extension;
        extension.loseContext();
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.rendererShader?.state),
    ).toBe("lost");
    await page.evaluate(() => globalThis.__m29LossExtension.restoreContext());
    await expect.poll(
        () => page.evaluate(() => ({
            shader: globalThis.__KISAKCOD_WEB__?.rendererShader?.state,
            surface: globalThis.__KISAKCOD_WEB__?.rendererSurface?.state,
            recoveryCount: globalThis.__KISAKCOD_WEB__?.rendererSurface?.recoveryCount,
        })),
        { timeout: 15_000 },
    ).toMatchObject({ shader: "ready", surface: "ready", recoveryCount: 1 });
    const after = await page.evaluate(() => globalThis.__m29TextureUploads);
    expect(after - before).toBeGreaterThanOrEqual(2);
});

test("keeps the current texture when the selected XModel color map is built in", async ({ page }, testInfo) => {
    await importInstall(page, testInfo, "retail-xmodel-builtin-color", {
        primaryIwd: M20_PRIMARY_IWD,
        overrides: new Map([["zone/english/killhouse.ff",
            createSyntheticWorldInventoryFastfile({ externalColorMap: false })]]),
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.retailCensus?.state),
    ).toBe("ready");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.archive?.state),
    ).toBe("ready");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.engineAsset?.state),
    ).toBe("unavailable");

    const result = await page.evaluate(() => ({
        colorMap: structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus
            .worldInventory.firstXModel.renderSurface.colorMap),
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        rendererSurface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
    }));
    expect(result.colorMap).toMatchObject({
        state: "unsupported",
        materialName: "",
        imageName: "",
        imagePath: "",
    });
    expect(result.colorMap.message).toMatch(/built-in image/i);
    expect(result.engineAsset.message).toMatch(/no supported external color-map IWI/i);
    expect(result.rendererSurface).toMatchObject({
        state: "ready",
        resident: true,
        textureBinding: "engine-image",
    });
});

test("keeps the current texture when the typed XModel IWI is absent", async ({ page }, testInfo) => {
    const missingPath = "images/synthetic_missing_color.iwi";
    await importInstall(page, testInfo, "retail-xmodel-missing-iwi", {
        primaryIwd: M20_PRIMARY_IWD,
        overrides: new Map([["zone/english/killhouse.ff",
            createSyntheticWorldInventoryFastfile({
                externalColorMap: true,
                colorMapName: "synthetic_missing_color",
            })]]),
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.archive?.state),
    ).toBe("ready");
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.engineAsset?.state),
    ).toBe("unavailable");

    const result = await page.evaluate(() => ({
        colorMap: structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus
            .worldInventory.firstXModel.renderSurface.colorMap),
        archive: structuredClone(globalThis.__KISAKCOD_WEB__.archive),
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        rendererSurface: structuredClone(globalThis.__KISAKCOD_WEB__.rendererSurface),
    }));
    expect(result.colorMap).toMatchObject({
        state: "selected",
        imagePath: missingPath,
        semantic: 2,
    });
    expect(result.archive).toMatchObject({
        state: "ready",
        path: "main/iw_00.iwd",
        targetMember: missingPath,
        targetMemberAvailable: false,
    });
    expect(result.engineAsset.message).toMatch(/material-selected IWI member is not available/i);
    expect(result.rendererSurface).toMatchObject({
        state: "ready",
        resident: true,
        textureBinding: "engine-image",
    });
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
    expect(result.census.worldInventory.firstXModel.renderSurface).toMatchObject({
        state: "fallback",
        fallbackPreserved: true,
        message: "The retail shader binding was unavailable; the bootstrap surface remains active",
    });
    expect(result.surface).toMatchObject({
        vertexCount: 4,
        indexCount: 6,
        submissionGeneration: 1,
    });
    expect(result.draws).toBeGreaterThan(0);
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
    const extensionAvailable = await page.evaluate(() => Boolean(
        document.querySelector("#game-canvas")
            ?.getContext("webgl2")?.getExtension("WEBGL_lose_context"),
    ));
    expect(extensionAvailable).toBe(true);
    await page.evaluate(() => {
        const extension = document.querySelector("#game-canvas")
            .getContext("webgl2").getExtension("WEBGL_lose_context");
        globalThis.__retailShaderLossExtension = extension;
        extension.loseContext();
    });
    await expect.poll(
        () => page.evaluate(() => globalThis.__KISAKCOD_WEB__?.rendererShader?.state),
    ).toBe("lost");
    await page.evaluate(() => globalThis.__retailShaderLossExtension.restoreContext());
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
    expect(result.links).toBeGreaterThanOrEqual(2);
    expect(result.events.some((event) => event.state === "lost")).toBe(true);
    expect(result.events.some(
        (event) => event.state === "ready" && event.recoveryCount === 1,
    )).toBe(true);
});

test("an invalid vertex program fails before shader creation or asset publication", async ({ page }, testInfo) => {
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

test("a map table without GfxWorld fails before publishing startup assets", async ({ page }, testInfo) => {
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

test("stops after retaining consecutive publications when a later technique set has a dependency", async ({ page }, testInfo) => {
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
    ).toBe("ready");
    const inventory = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus.worldInventory),
    );
    expect(inventory).toMatchObject({
        assetBodiesEntered: 2,
        completedAssetCount: 1,
        nextBodyIndex: 1,
        nextBodyType: 5,
        stoppedBeforeDifferentAssetType: false,
        stoppedBeforeTechniqueDependency: true,
        firstTechniqueSet: {
            published: true,
            registryAliasCount: 2,
            registryDefinedAliasCount: 1,
            stoppedBeforeDependency: false,
            unsupportedOperation: "Load_MaterialTechnique",
        },
        techniqueSets: [
            { assetIndex: 0, identity: 1, published: true },
            {
                assetIndex: 1,
                identity: 0,
                published: false,
                firstTechniqueSlot: 4,
                firstTechniqueReference: 0xffff_ffff,
                references: { null: 33, inline: 1, shared: 0, alias: 0 },
            },
        ],
    });
});

test("retains the fixed XModel header when its first bone dependency is unsupported", async ({ page }, testInfo) => {
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
    ).toBe("ready");
    const inventory = await page.evaluate(
        () => structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus.worldInventory),
    );
    expect(inventory).toMatchObject({
        completedAssetCount: 2,
        nextBodyIndex: 2,
        nextBodyType: 3,
        block0HighWaterAtBoundary: 220,
        block4CursorAtBoundary: 128,
        firstTechniqueSet: {
            registryAliasCount: 3,
            registryDefinedAliasCount: 2,
            unsupportedOperation: "Load_ScriptStringArray",
        },
        firstXModel: {
            assetIndex: 2,
            name: "web/xmodel_wall",
            headerTraversed: true,
            skeletonPrefixTraversed: false,
            stoppedBeforeSurfaceArray: false,
            boundaryInflatedOffset: 703,
            unsupportedOperation: "Load_ScriptStringArray",
            boneNames: [],
        },
    });
});

test("invalid XModel bounds fail without exposing prior technique publications", async ({ page }, testInfo) => {
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

test("an out-of-range XModel bone script string fails closed", async ({ page }, testInfo) => {
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

test("an XSurface pointer/count mismatch fails without a partial model", async ({ page }, testInfo) => {
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

test("an invalid XSurface collision tree fails closed", async ({ page }, testInfo) => {
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
        "an undefined XModel material alias fails closed",
        { invalidXModelMaterialAlias: true },
        "invalid XModel material dependency alias",
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
    test(title, async ({ page }, testInfo) => {
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

test("an inline physics preset keeps the XModel unpublished", async ({ page }, testInfo) => {
    const overrides = new Map([[
        "zone/english/killhouse.ff",
        createSyntheticWorldInventoryFastfile({ unsupportedXModelPhysPreset: true }),
    ]]);
    await importInstall(page, testInfo, "m26-unsupported-physics", { overrides });
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
        completedAssetCount: 2,
        firstTechniqueSet: {
            registryAliasCount: 6,
            registryDefinedAliasCount: 5,
            unsupportedOperation: "Load_PhysPreset",
        },
        firstXModel: {
            published: false,
            physPresetTraversed: false,
            physGeomsTraversed: false,
            unsupportedOperation: "Load_PhysPreset",
        },
    });
});

test("an invalid map technique-set header fails before publishing asset zero", async ({ page }, testInfo) => {
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

test("an invalid later map technique-set header exposes no partial prefix", async ({ page }, testInfo) => {
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

test("publishes the consecutive typed technique-set run after the XModel", async ({ page }, testInfo) => {
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

test("selects between complete retained XModels without reparsing", async ({ page }, testInfo) => {
    await importInstall(page, testInfo, "m34-second-xmodel-dependencies");
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
        selectedXModelIndex: 0,
        firstXModel: {
            assetIndex: 2,
            name: "web/xmodel_wall",
            identity: 6,
            published: true,
            rendererPayloadSelected: true,
            rendererPayloadAvailable: true,
            renderSurface: { state: "ready", geometrySource: "retail-xmodel" },
        },
        xmodels: [
            { assetIndex: 2, identity: 6, published: true },
            {
                assetIndex: 5,
                name: "web/xmodel_second",
                numBones: 1,
                numRootBones: 1,
                surfaceCount: 3,
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
                rendererPayloadSelected: false,
                rendererPayloadAvailable: true,
                identity: 10,
                stoppedBeforeSurfaceArray: false,
                stoppedBeforeMaterialDependency: false,
                unsupportedOperation: "",
                totals: {
                vertices: 9,
                triangles: 3,
                rigidVertLists: 3,
                collisionTriangles: 1,
                collisionPayloadBytes: 92,
            },
            boneNames: [{ name: "tag_origin", scriptStringIndex: 0 }],
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
        firstTechniqueSet: {
            registryAliasCount: 11,
            registryDefinedAliasCount: 11,
            unsupportedOperation: "",
        },
    });
    expect(world.xmodels[1].surfaces).toHaveLength(3);
    expect(world.xmodels[1].surfaces).toEqual(
        expect.arrayContaining([
            expect.objectContaining({
                vertCount: 3,
                triCount: 1,
                dependenciesTraversed: true,
                rigidVertLists: [expect.objectContaining({
                    vertCount: 3,
                    triCount: 1,
                })],
            }),
        ]),
    );
    expect(world.xmodels[1].renderSurface).toBeUndefined();

    const selector = page.locator("#xmodel-select");
    await expect(selector).toBeEnabled();
    await expect(selector.locator("option")).toHaveCount(2);
    await expect(selector).toHaveValue("0");
    const priorSubmission = await page.evaluate(
        () => globalThis.__KISAKCOD_WEB__.rendererSurface
            .submissionGeneration,
    );
    await selector.selectOption("1");
    await expect(selector).toHaveValue("1");
    await expect.poll(() => page.evaluate(() => ({
        selected: globalThis.__KISAKCOD_WEB__.retailCensus
            .worldInventory.selectedXModelIndex,
        name: globalThis.__KISAKCOD_WEB__.retailCensus
            .worldInventory.selectedXModel?.name,
        state: globalThis.__KISAKCOD_WEB__.retailCensus
            .worldInventory.selectedXModel?.renderSurface?.state,
        draws: globalThis.__KISAKCOD_WEB__.retailCensus
            .worldInventory.selectedXModel?.renderSurface?.drawList?.drawCount,
        submission: globalThis.__KISAKCOD_WEB__.rendererSurface
            .submissionGeneration,
    })), { timeout: 10_000 }).toMatchObject({
        selected: 1,
        name: "web/xmodel_second",
        state: "ready",
        draws: 3,
        submission: priorSubmission + 1,
    });
    const selectedWorld = await page.evaluate(
        () => structuredClone(
            globalThis.__KISAKCOD_WEB__.retailCensus.worldInventory,
        ),
    );
    expect(selectedWorld.firstXModel.name).toBe("web/xmodel_wall");
    expect(selectedWorld.xmodels[0].rendererPayloadSelected).toBe(false);
    expect(selectedWorld.xmodels[1]).toMatchObject({
        rendererPayloadSelected: true,
        renderSurface: {
            state: "ready",
            vertexCount: 3,
            triangleCount: 1,
            drawList: { drawCount: 3, totalVertices: 9, totalIndices: 9 },
        },
    });
});

test("invalid second-XModel material alias exposes no partial public result", async ({ page }, testInfo) => {
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

test("invalid second-XSurface layout exposes no partial public result", async ({ page }, testInfo) => {
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

test("invalid second-XModel bounds expose no partial public result", async ({ page }, testInfo) => {
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

test("an unsupported second-XModel skeleton dependency preserves the first model", async ({ page }, testInfo) => {
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
    ).toBe("ready");
    const world = await page.evaluate(
        () => structuredClone(
            globalThis.__KISAKCOD_WEB__.retailCensus.worldInventory,
        ),
    );
    expect(world).toMatchObject({
        firstXModel: { identity: 6, published: true },
        xmodels: [
            { identity: 6, published: true },
            {
                assetIndex: 5,
                headerTraversed: true,
                skeletonPrefixTraversed: false,
                unsupportedOperation: "Load_ScriptStringArray",
            },
        ],
        firstTechniqueSet: {
            registryAliasCount: 9,
            registryDefinedAliasCount: 8,
            unsupportedOperation: "Load_ScriptStringArray",
        },
    });
});

test("a malformed post-XModel technique set fails without publishing the prior prefix", async ({ page }, testInfo) => {
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

test("a malformed later post-XModel technique set exposes no partial run", async ({ page }, testInfo) => {
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

test("a dependent post-XModel technique set stops before its nested technique", async ({ page }, testInfo) => {
    const overrides = new Map([[
        "zone/english/killhouse.ff",
        createSyntheticWorldInventoryFastfile({ postXModelTechniqueDependency: true }),
    ]]);
    await importInstall(page, testInfo, "m30-dependent-post-xmodel-techset", { overrides });
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
        nextBodyIndex: 3,
        nextBodyType: 5,
        firstTechniqueSet: {
            registryAliasCount: 7,
            registryDefinedAliasCount: 6,
            unsupportedOperation: "Load_MaterialTechnique",
        },
        postXModelTechniqueSet: {
            assetIndex: 3,
            identity: 0,
            published: false,
            firstTechniqueSlot: 4,
            references: { null: 33, inline: 1, shared: 0, alias: 0 },
        },
        firstXModel: { identity: 6, published: true },
    });
});

test("a later dependent post-XModel set preserves the published run prefix", async ({ page }, testInfo) => {
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
    ).toBe("ready");
    const world = await page.evaluate(
        () => structuredClone(
            globalThis.__KISAKCOD_WEB__.retailCensus.worldInventory,
        ),
    );
    expect(world).toMatchObject({
        completedAssetCount: 4,
        nextBodyIndex: 4,
        nextBodyType: 5,
        postXModelTechniqueSetRun: {
            firstAssetIndex: 3,
            bodiesEntered: 2,
            completedCount: 1,
            nextBodyIndex: 4,
            nextBodyType: 5,
            stoppedBeforeDifferentAssetType: false,
            stoppedBeforeTechniqueDependency: true,
        },
        postXModelTechniqueSets: [
            {
                assetIndex: 3,
                identity: 7,
                published: true,
            },
            {
                assetIndex: 4,
                identity: 0,
                published: false,
                firstTechniqueSlot: 7,
                references: { null: 33, inline: 1, shared: 0, alias: 0 },
            },
        ],
        firstTechniqueSet: {
            registryAliasCount: 8,
            registryDefinedAliasCount: 7,
            unsupportedOperation: "Load_MaterialTechnique",
        },
        firstXModel: { identity: 6, published: true },
    });
});
