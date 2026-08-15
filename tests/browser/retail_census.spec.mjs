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
    ).toBe("synthetic");

    const result = await page.evaluate(() => ({
        census: structuredClone(globalThis.__KISAKCOD_WEB__.retailCensus),
        events: structuredClone(globalThis.__retailCensusEvents),
        archiveEvents: structuredClone(globalThis.__retailCensusArchiveEvents),
        rendererShader: structuredClone(globalThis.__KISAKCOD_WEB__.rendererShader),
        engineAsset: structuredClone(globalThis.__KISAKCOD_WEB__.engineAsset),
        rendererTexture: structuredClone(globalThis.__KISAKCOD_WEB__.rendererTexture),
        rendererMaterial: structuredClone(globalThis.__KISAKCOD_WEB__.rendererMaterial),
        rendererShaderEvents: structuredClone(globalThis.__retailRendererShaderEvents),
        shaderSources: [...globalThis.__retailShaderSources],
        shaderBindings: structuredClone(globalThis.__retailShaderBindings),
        shaderMatrices: structuredClone(globalThis.__retailShaderMatrices),
        shaderLinkCount: globalThis.__retailShaderLinkCount,
        shaderUseCount: globalThis.__retailShaderUseCount,
        shaderDrawCount: globalThis.__retailShaderDrawCount,
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
            assetTableOrderHash: 0x7cbd_671b,
            firstGfxWorldAssetIndex: 5,
            firstGfxWorldReference: 0xffff_ffff,
            assetsBeforeFirstGfxWorld: 5,
            referencesBeforeFirstGfxWorld: {
                inline: 5,
                shared: 0,
                alias: 0,
                null: 0,
            },
            firstBodyType: 5,
            firstBodyReference: 0xffff_ffff,
            nextBodyIndex: 2,
            nextBodyType: 3,
            nextBodyReference: 0xffff_ffff,
            block0HighWaterAtBoundary: 220,
            block4CursorAtBoundary: 376,
            stoppedBeforeAssetBody: false,
            assetBodiesEntered: 2,
            completedAssetCount: 2,
            stoppedBeforeDifferentAssetType: false,
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
            ],
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
                registryAliasCount: 3,
                registryDefinedAliasCount: 2,
                published: true,
                stoppedBeforeDependency: false,
                unsupportedOperation: "Load_Material",
            },
            firstXModel: {
                assetIndex: 2,
                name: "web/xmodel_wall",
                numBones: 1,
                numRootBones: 1,
                surfaceCount: 2,
                lodRampType: 0,
                references: {
                    boneNames: 0xffff_ffff,
                    parentList: 0,
                    quats: 0,
                    trans: 0,
                    partClassification: 0xffff_ffff,
                    baseMat: 0xffff_ffff,
                    surfaces: 0xffff_ffff,
                    materialHandles: 0xffff_ffff,
                    collisionSurfaces: 0,
                    boneInfo: 0xffff_ffff,
                    physPreset: 0,
                    physGeoms: 0,
                },
                collisionSurfaceCount: 0,
                contents: 0,
                radius: 10,
                mins: [-1, -2, -3],
                maxs: [1, 2, 3],
                lodCount: 1,
                collisionLod: 0,
                memoryUsage: 100,
                flags: 0,
                bad: false,
                offsets: {
                    headerBlock0: 0,
                    nameBlock4: 112,
                    boneNamesBlock4: 128,
                    parentListBlock4: 0,
                    quatsBlock4: 0,
                    transBlock4: 0,
                    partClassificationBlock4: 130,
                    baseMatBlock4: 132,
                    surfacesBlock4: 164,
                    materialHandlesBlock4: 368,
                },
                totals: {
                    vertices: 6,
                    triangles: 2,
                    rigidVertLists: 2,
                    collisionNodes: 1,
                    collisionLeaves: 1,
                    surfacePayloadBytes: 406,
                },
                boundaryInflatedOffset: 1144,
                headerTraversed: true,
                skeletonPrefixTraversed: true,
                surfaceHeadersTraversed: true,
                surfaceDependenciesTraversed: true,
                materialHandlesTraversed: true,
                stoppedBeforeSurfaceArray: false,
                stoppedBeforeMaterialDependency: true,
                unsupportedOperation: "Load_Material",
                lods: [
                    {
                        index: 0, distance: 800, surfaceCount: 2, surfaceIndex: 0,
                        partBits: [0x8000_0000, 0, 0, 0], lod: 0,
                        smcIndexPlusOne: 0, smcAllocBits: 0,
                    },
                    {
                        index: 1, distance: 0, surfaceCount: 0, surfaceIndex: 0,
                        partBits: [0, 0, 0, 0], lod: 0,
                        smcIndexPlusOne: 0, smcAllocBits: 0,
                    },
                    {
                        index: 2, distance: 0, surfaceCount: 0, surfaceIndex: 0,
                        partBits: [0, 0, 0, 0], lod: 0,
                        smcIndexPlusOne: 0, smcAllocBits: 0,
                    },
                    {
                        index: 3, distance: 0, surfaceCount: 0, surfaceIndex: 0,
                        partBits: [0, 0, 0, 0], lod: 0,
                        smcIndexPlusOne: 0, smcAllocBits: 0,
                    },
                ],
                boneNames: [{
                    index: 0,
                    scriptStringIndex: 0,
                    name: "tag_origin",
                    classification: 0,
                }],
                surfaces: [
                    {
                        index: 0,
                        tileMode: 1,
                        deformed: false,
                        vertCount: 3,
                        triCount: 1,
                        baseTriIndex: 0,
                        baseVertIndex: 0,
                        vertListCount: 1,
                        references: {
                            triIndices: 0xffff_ffff,
                            vertsBlend: 0,
                            vertices: 0xffff_ffff,
                            vertLists: 0xffff_ffff,
                        },
                        offsets: {
                            verticesBlock7: 0,
                            vertListsBlock4: 276,
                            indicesBlock8: 0,
                        },
                        dependenciesTraversed: true,
                        rigidVertLists: [{
                            index: 0,
                            boneOffset: 0,
                            vertCount: 3,
                            triOffset: 0,
                            triCount: 1,
                            collisionTree: {
                                reference: 0xffff_ffff,
                                translation: [0, 0, 0],
                                scale: [1, 1, 1],
                                nodeCount: 1,
                                leafCount: 1,
                                traversed: true,
                            },
                        }],
                    },
                    {
                        index: 1,
                        tileMode: 0,
                        vertCount: 3,
                        triCount: 1,
                        baseTriIndex: 1,
                        baseVertIndex: 3,
                        vertListCount: 1,
                        offsets: {
                            verticesBlock7: 96,
                            vertListsBlock4: 356,
                            indicesBlock8: 16,
                        },
                        dependenciesTraversed: true,
                        rigidVertLists: [{
                            index: 0,
                            collisionTree: {
                                reference: 0,
                                traversed: false,
                            },
                        }],
                    },
                ],
                materialReferences: [
                    { index: 0, reference: 0xffff_ffff },
                    { index: 1, reference: 0x4000_0001 },
                ],
            },
            typeCounts: [
                { type: 2, name: "xanim", count: 1 },
                { type: 3, name: "xmodel", count: 1 },
                { type: 5, name: "techset", count: 2 },
                { type: 16, name: "gfx_map", count: 1 },
                { type: 31, name: "rawfile", count: 1 },
                { type: 32, name: "stringtable", count: 1 },
            ],
            typesBeforeFirstGfxWorld: [
                { type: 2, name: "xanim", count: 1 },
                { type: 3, name: "xmodel", count: 1 },
                { type: 5, name: "techset", count: 2 },
                { type: 31, name: "rawfile", count: 1 },
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
    expect(result.engineAsset).toMatchObject({
        state: "ready",
        path: "images/synthetic_engine_asset.iwi",
        selectionSource: "retail-material",
        materialName: "web_cursor",
        imageName: "synthetic_engine_asset",
        materialIdentity: 4,
        imageIdentity: 3,
    });
    expect(result.rendererTexture).toMatchObject({
        state: "ready",
        path: "images/synthetic_engine_asset.iwi",
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
        materialSource: "retail-fastfile",
        materialName: "web_cursor",
        materialIdentity: 4,
        imageName: "synthetic_engine_asset",
        imageIdentity: 3,
        shaderSubstitutionId: "webgl2.vertcol_simple2d.v1",
        sampler: "u_colorMapSampler",
        textureUnit: 0,
        imagePath: "images/synthetic_engine_asset.iwi",
        sourceFormat: IWI_FORMAT_DXT1,
        decodedFormat: "rgba8",
        compressedSource: true,
        recoveryBytes: 64,
        geometrySource: "synthetic",
    });
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
    expect(result.shaderMatrices.slice(0, 2)).toEqual([
        { transpose: false, values: identity },
        { transpose: false, values: identity },
    ]);
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
        "material web_cursor selected images/synthetic_engine_asset.iwi",
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
    expect(result.draws).toBeGreaterThan(0);
    expect(result.runtimeState).toBe("running");
});

test("rebuilds the selected shader program after WebGL2 context loss", async ({ page }, testInfo) => {
    await observeRetailShaderRenderer(page);
    await importInstall(page, testInfo, "retail-shader-context-recovery");
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
