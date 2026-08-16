#include <web/web_retail_census_job.h>

#include <web/web_filesystem.h>
#include <web/web_engine_asset.h>
#include <web/web_engine_world_surface.h>
#include <web/web_retail_fastfile_census.h>
#include <web/web_renderer.h>
#include <web/web_shader_compatibility.h>
#include <web/web_system.h>

#include <emscripten.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{
constexpr const char *CODE_POST_GFX_PATH = "zone/english/code_post_gfx.ff";
constexpr const char *COMMON_FASTFILE_PATH = "zone/english/common.ff";
constexpr const char *WORLD_FASTFILE_PATH = "zone/english/killhouse.ff";
// Synthetic browser fixtures are intentionally only a few hundred bytes and
// end at the old pre-world boundary. Real retail Killhouse is about 70 MiB;
// this conservative envelope avoids treating a test prefix as a complete zone.
constexpr std::uint32_t MIN_COMPLETE_WORLD_FASTFILE_BYTES = 1024u * 1024u;

enum class Dataset : std::uint8_t
{
    CodePostGfx,
    CommonPrerequisite,
    WorldInventory,
};

enum class Phase : std::uint8_t
{
    Idle,
    NeedStat,
    WaitingStat,
    NeedRead,
    WaitingRead,
    Parse,
    Finished,
    Failed,
};

struct RetailCensusRuntime
{
    Phase phase = Phase::Idle;
    Dataset dataset = Dataset::CodePostGfx;
    std::uint32_t generation = 0u;
    WebFsRequestId requestId = 0u;
    std::uint32_t fileSize = 0u;
    std::uint32_t readOffset = 0u;
    std::uint32_t codePostFileSize = 0u;
    std::uint32_t codePostSourceBytesRead = 0u;
    std::uint32_t worldFileSize = 0u;
    std::uint32_t worldSourceBytesRead = 0u;
    bool completionReady = false;
    WebFsStatus completionStatus = WebFsStatus::Pending;
    std::vector<std::uint8_t> completionBytes;
    kisak::fastfile::RetailFastfileCensusJob parser;
    kisak::fastfile::RetailSoundAliasCatalog soundCatalog;
    kisak::fastfile::RetailFastfileCensus result;
    std::shared_ptr<kisak::fastfile::RetailFastfileCensus> commonPrerequisite;
    kisak::fastfile::RetailFastfileCensus worldInventory;
};

RetailCensusRuntime g_runtime;

struct RetailGfxWorldSurfacePublication
{
    const char *state = "fallback";
    const char *message = "No canonical GfxWorld surface was submitted";
    WebEngineGfxWorldSurfaceResult result =
        WebEngineGfxWorldSurfaceResult::InvalidWorld;
    WebEngineGfxWorldSurfacePublication surface;
};

const char *CurrentPath() noexcept
{
    switch (g_runtime.dataset)
    {
    case Dataset::CodePostGfx: return CODE_POST_GFX_PATH;
    case Dataset::CommonPrerequisite: return COMMON_FASTFILE_PATH;
    case Dataset::WorldInventory: return WORLD_FASTFILE_PATH;
    }
    return WORLD_FASTFILE_PATH;
}

const char *CurrentTraversal() noexcept
{
    return g_runtime.dataset == Dataset::CodePostGfx
        ? "two-techsets-one-material"
        : g_runtime.dataset == Dataset::CommonPrerequisite
            ? "prerequisite-zone-assets"
            : "native-order-world-gfxworld";
}

EM_JS(void, DispatchRetailCensusLoading,
    (uint32_t generation, const char *stage, const char *path, const char *traversal), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:retail-census", {
        detail: {
            state: "loading",
            stage: UTF8ToString(stage),
            generation: generation >>> 0,
            path: UTF8ToString(path),
            message: "Reading a bounded retail fastfile prefix through the browser VFS",
            maxSourceChunkBytes: 64 * 1024,
            maxInflatedPrefixBytes: 512 * 1024,
            maxStepBytes: 64 * 1024,
            maxStepRecords: 64,
            assetBodyTraversal: UTF8ToString(traversal)
        }
    }));
});

EM_JS(
    void,
    BeginRetailCensusReady,
    (uint32_t generation,
     uint32_t fileSize,
     uint32_t sourceBytesRead,
     double sourceBytesConsumed,
     uint32_t sourceFeedCount,
     uint32_t version,
     uint32_t xfileSize,
     uint32_t externalSize,
     double declaredBlockBytes,
     uint32_t scriptStringCount,
     uint32_t scriptStringBytes,
     uint32_t assetCount,
     uint32_t inflatedPrefixBytes,
     uint32_t inlineReferences,
     uint32_t sharedReferences,
     uint32_t aliasReferences,
     uint32_t nullReferences,
     uint32_t firstBodyIndex,
     uint32_t firstBodyType,
     const char *firstBodyTypeName,
     uint32_t firstBodyReference),
    {
        globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__ = {
            state: "ready",
            stage: "asset-boundary",
            generation: generation >>> 0,
            path: "zone/english/code_post_gfx.ff",
            message: "Resolved the startup material and published the first retail map technique set",
            fileSize: fileSize >>> 0,
            sourceBytesRead: sourceBytesRead >>> 0,
            sourceBytesConsumed,
            sourceFeedCount: sourceFeedCount >>> 0,
            version: version >>> 0,
            xfileSize: xfileSize >>> 0,
            externalSize: externalSize >>> 0,
            declaredBlockBytes,
            scriptStringCount: scriptStringCount >>> 0,
            scriptStringBytes: scriptStringBytes >>> 0,
            assetCount: assetCount >>> 0,
            inflatedPrefixBytes: inflatedPrefixBytes >>> 0,
            inlineReferences: inlineReferences >>> 0,
            sharedReferences: sharedReferences >>> 0,
            aliasReferences: aliasReferences >>> 0,
            nullReferences: nullReferences >>> 0,
            firstTraversedAssetIndex: firstBodyIndex >>> 0,
            firstTraversedAssetType: firstBodyType >>> 0,
            firstTraversedAssetTypeName: UTF8ToString(firstBodyTypeName),
            firstTraversedAssetReference: firstBodyReference >>> 0,
            stoppedBeforeAssetBody: false,
            assetBodiesEntered: 3,
            maxSourceChunkBytes: 64 * 1024,
            maxInflatedPrefixBytes: 512 * 1024,
            maxStepBytes: 64 * 1024,
            maxStepRecords: 64,
            blockSizes: [],
            typeCounts: []
        };
    });

EM_JS(
    void,
    AppendRetailTechniqueTraversal,
    (const char *techniqueSetName,
     uint32_t firstTechniqueSlot,
     uint32_t techniquePassCount,
     uint32_t vertexStreamCount,
     uint32_t vertexStreamRoutingHash,
     const char *vertexShaderName,
     uint32_t vertexShaderProgramDwords,
     uint32_t vertexShaderProgramHash,
     uint32_t assetTableBlock4Offset,
     uint32_t techniqueSetBlock0Offset,
     uint32_t techniqueBlock4Offset,
     uint32_t vertexDeclarationBlock4Offset,
     uint32_t vertexShaderBlock4Offset,
     uint32_t vertexShaderProgramBlock4Offset,
     uint32_t block0HighWater,
     uint32_t block4Cursor,
     uint32_t completedAssetCount,
     int techniqueSetPublished,
     int vertexDeclarationPrepared,
     int stoppedBeforeShaderCreation,
     const char *unsupportedOperation),
    {
        const detail = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__;
        if (detail) {
            detail.techniqueSetName = UTF8ToString(techniqueSetName);
            detail.firstTechniqueSlot = firstTechniqueSlot >>> 0;
            detail.techniquePassCount = techniquePassCount >>> 0;
            detail.vertexStreamCount = vertexStreamCount >>> 0;
            detail.vertexStreamRoutingHash = vertexStreamRoutingHash >>> 0;
            detail.vertexShaderName = UTF8ToString(vertexShaderName);
            detail.vertexShaderProgramDwords = vertexShaderProgramDwords >>> 0;
            detail.vertexShaderProgramHash = vertexShaderProgramHash >>> 0;
            detail.assetTableBlock4Offset = assetTableBlock4Offset >>> 0;
            detail.techniqueSetBlock0Offset = techniqueSetBlock0Offset >>> 0;
            detail.techniqueBlock4Offset = techniqueBlock4Offset >>> 0;
            detail.vertexDeclarationBlock4Offset = vertexDeclarationBlock4Offset >>> 0;
            detail.vertexShaderBlock4Offset = vertexShaderBlock4Offset >>> 0;
            detail.vertexShaderProgramBlock4Offset = vertexShaderProgramBlock4Offset >>> 0;
            detail.block0HighWaterAtBoundary = block0HighWater >>> 0;
            detail.block4CursorAtBoundary = block4Cursor >>> 0;
            detail.completedAssetCount = completedAssetCount >>> 0;
            detail.techniqueSetPublished = Boolean(techniqueSetPublished);
            detail.vertexDeclarationPrepared = Boolean(vertexDeclarationPrepared);
            detail.stoppedBeforeShaderCreation = Boolean(stoppedBeforeShaderCreation);
            detail.unsupportedOperation = stoppedBeforeShaderCreation
                ? UTF8ToString(unsupportedOperation)
                : null;
            detail.traversesAssetBodies = true;
        }
    });

EM_JS(
    void,
    AppendRetailMaterialBinding,
    (const char *materialTechniqueSetName,
     const char *materialName,
     const char *imageName,
     const char *imagePath,
     uint32_t materialAssetIndex,
     uint32_t materialTextureCount,
     uint32_t imageWidth,
     uint32_t imageHeight,
     uint32_t imageDepth,
     uint32_t imageFormat,
     uint32_t imageResourceBytes,
     uint32_t compatibilityTechniqueSetIdentity,
     uint32_t materialTechniqueSetIdentity,
     uint32_t materialIdentity,
     uint32_t imageIdentity,
     uint32_t registryAssetCount,
     uint32_t registryAliasCount,
     uint32_t registryDefinedAliasCount,
     uint32_t materialTechniqueSetBlock0Offset,
     uint32_t materialTechniqueBlock4Offset,
     uint32_t materialBlock0Offset,
     uint32_t materialNameBlock4Offset,
     uint32_t materialTextureTableBlock4Offset,
     uint32_t imageBlock0Offset,
     uint32_t imageNameBlock4Offset,
     uint32_t imageTextureInsertPointerBlock4Offset,
     uint32_t imageLoadDefBlock0Offset,
     uint32_t materialStateBitsBlock4Offset,
     int materialTechniqueSetPublished,
     int materialPublished,
     int imagePublished,
     int materialImageResolved),
    {
        const detail = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__;
        if (detail) {
            detail.materialTechniqueSetName = UTF8ToString(materialTechniqueSetName);
            detail.materialName = UTF8ToString(materialName);
            detail.imageName = UTF8ToString(imageName);
            detail.materialImagePath = UTF8ToString(imagePath);
            detail.materialAssetIndex = materialAssetIndex >>> 0;
            detail.materialTextureCount = materialTextureCount >>> 0;
            detail.materialImage = {
                name: detail.imageName,
                path: detail.materialImagePath,
                width: imageWidth >>> 0,
                height: imageHeight >>> 0,
                depth: imageDepth >>> 0,
                serializedFormat: imageFormat >>> 0,
                resourceBytes: imageResourceBytes >>> 0,
                identity: imageIdentity >>> 0,
            };
            detail.compatibilityTechniqueSetIdentity = compatibilityTechniqueSetIdentity >>> 0;
            detail.materialTechniqueSetIdentity = materialTechniqueSetIdentity >>> 0;
            detail.materialIdentity = materialIdentity >>> 0;
            detail.imageIdentity = imageIdentity >>> 0;
            detail.registryAssetCount = registryAssetCount >>> 0;
            detail.registryAliasCount = registryAliasCount >>> 0;
            detail.registryDefinedAliasCount = registryDefinedAliasCount >>> 0;
            detail.materialTechniqueSetBlock0Offset = materialTechniqueSetBlock0Offset >>> 0;
            detail.materialTechniqueBlock4Offset = materialTechniqueBlock4Offset >>> 0;
            detail.materialBlock0Offset = materialBlock0Offset >>> 0;
            detail.materialNameBlock4Offset = materialNameBlock4Offset >>> 0;
            detail.materialTextureTableBlock4Offset = materialTextureTableBlock4Offset >>> 0;
            detail.imageBlock0Offset = imageBlock0Offset >>> 0;
            detail.imageNameBlock4Offset = imageNameBlock4Offset >>> 0;
            detail.imageTextureInsertPointerBlock4Offset =
                imageTextureInsertPointerBlock4Offset >>> 0;
            detail.imageLoadDefBlock0Offset = imageLoadDefBlock0Offset >>> 0;
            detail.materialStateBitsBlock4Offset = materialStateBitsBlock4Offset >>> 0;
            detail.materialTechniqueSetPublished = Boolean(materialTechniqueSetPublished);
            detail.materialPublished = Boolean(materialPublished);
            detail.imagePublished = Boolean(imagePublished);
            detail.materialImageResolved = Boolean(materialImageResolved);
        }
    });

EM_JS(
    void,
    AppendRetailShaderCompatibility,
    (const char *pixelShaderName,
     uint32_t vertexInstructionCount,
     uint32_t vertexConstantCount,
     uint32_t pixelProgramDwords,
     uint32_t pixelProgramHash,
     uint32_t pixelInstructionCount,
     uint32_t pixelConstantCount,
     uint32_t shaderArgumentCount,
     uint32_t shaderArgumentHash,
     const char *techniqueName,
     const char *substitutionId,
     uint32_t vertexGlslHash,
     uint32_t fragmentGlslHash,
     uint32_t pixelShaderBlock4Offset,
     uint32_t pixelProgramBlock4Offset,
     uint32_t argumentBlock4Offset,
     int compatibilitySelected),
    {
        const detail = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__;
        if (detail) {
            detail.pixelShaderName = UTF8ToString(pixelShaderName);
            detail.vertexShaderInstructionCount = vertexInstructionCount >>> 0;
            detail.vertexShaderConstantCount = vertexConstantCount >>> 0;
            detail.pixelShaderProgramDwords = pixelProgramDwords >>> 0;
            detail.pixelShaderProgramHash = pixelProgramHash >>> 0;
            detail.pixelShaderInstructionCount = pixelInstructionCount >>> 0;
            detail.pixelShaderConstantCount = pixelConstantCount >>> 0;
            detail.shaderArgumentCount = shaderArgumentCount >>> 0;
            detail.shaderArgumentHash = shaderArgumentHash >>> 0;
            detail.techniqueName = UTF8ToString(techniqueName);
            detail.shaderSubstitutionId = UTF8ToString(substitutionId);
            detail.vertexGlslHash = vertexGlslHash >>> 0;
            detail.fragmentGlslHash = fragmentGlslHash >>> 0;
            detail.pixelShaderBlock4Offset = pixelShaderBlock4Offset >>> 0;
            detail.pixelShaderProgramBlock4Offset = pixelProgramBlock4Offset >>> 0;
            detail.shaderArgumentsBlock4Offset = argumentBlock4Offset >>> 0;
            detail.shaderCompatibilitySelected = Boolean(compatibilitySelected);
        }
    });

EM_JS(
    void,
    AppendRetailWorldInventory,
    (uint32_t fileSize,
     uint32_t sourceBytesRead,
     double sourceBytesConsumed,
     uint32_t sourceFeedCount,
     uint32_t assetCount,
     uint32_t inflatedPrefixBytes,
     uint32_t assetTableOrderHash,
     uint32_t firstGfxWorldAssetIndex,
     uint32_t firstGfxWorldReference,
     uint32_t inlineBeforeWorld,
     uint32_t sharedBeforeWorld,
     uint32_t aliasBeforeWorld,
     uint32_t nullBeforeWorld,
     uint32_t firstBodyType,
     uint32_t firstBodyReference,
     uint32_t nextBodyType,
     uint32_t nextBodyReference,
     uint32_t nextBodyIndex,
     uint32_t assetBodiesEntered,
     uint32_t completedAssetCount,
     int stoppedBeforeDifferentAssetType,
     uint32_t block0HighWater,
     uint32_t block4Cursor,
     const char *techniqueSetName,
     uint32_t worldVertFormat,
     uint32_t remapReference,
     uint32_t techniqueSetBlock0Offset,
     uint32_t techniqueSetNameBlock4Offset,
     uint32_t techniqueSetBoundaryInflatedOffset,
     uint32_t firstTechniqueSlot,
     uint32_t firstTechniqueReference,
     uint32_t nullTechniqueReferences,
     uint32_t inlineTechniqueReferences,
     uint32_t sharedTechniqueReferences,
     uint32_t aliasTechniqueReferences,
     uint32_t techniqueSetIdentity,
     uint32_t registryAliasCount,
     uint32_t registryDefinedAliasCount,
     int techniqueSetPublished,
     int stoppedBeforeTechniqueDependency,
     const char *unsupportedOperation),
    {
        const detail = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__;
        if (detail) {
            detail.worldInventory = {
                state: "ready",
                stage: "asset-boundary",
                path: "zone/english/killhouse.ff",
                message: "Ran the reusable bounded asset loaders through the supported top-level sequence",
                fileSize: fileSize >>> 0,
                sourceBytesRead: sourceBytesRead >>> 0,
                sourceBytesConsumed,
                sourceFeedCount: sourceFeedCount >>> 0,
                assetCount: assetCount >>> 0,
                inflatedPrefixBytes: inflatedPrefixBytes >>> 0,
                assetTableOrderHash: assetTableOrderHash >>> 0,
                firstGfxWorldAssetIndex: firstGfxWorldAssetIndex >>> 0,
                firstGfxWorldReference: firstGfxWorldReference >>> 0,
                assetsBeforeFirstGfxWorld: firstGfxWorldAssetIndex >>> 0,
                referencesBeforeFirstGfxWorld: {
                    inline: inlineBeforeWorld >>> 0,
                    shared: sharedBeforeWorld >>> 0,
                    alias: aliasBeforeWorld >>> 0,
                    null: nullBeforeWorld >>> 0,
                },
                firstBodyType: firstBodyType >>> 0,
                firstBodyReference: firstBodyReference >>> 0,
                nextBodyIndex: nextBodyIndex >>> 0,
                nextBodyType: nextBodyType >>> 0,
                nextBodyReference: nextBodyReference >>> 0,
                block0HighWaterAtBoundary: block0HighWater >>> 0,
                block4CursorAtBoundary: block4Cursor >>> 0,
                stoppedBeforeAssetBody: false,
                assetBodiesEntered: assetBodiesEntered >>> 0,
                completedAssetCount: completedAssetCount >>> 0,
                stoppedBeforeDifferentAssetType: Boolean(stoppedBeforeDifferentAssetType),
                stoppedBeforeTechniqueDependency: Boolean(stoppedBeforeTechniqueDependency),
                techniqueSets: [],
                postXModelTechniqueSet: null,
                xmodels: [],
                fxEffects: [],
                firstXModel: null,
                firstTechniqueSet: {
                    name: UTF8ToString(techniqueSetName),
                    worldVertFormat: worldVertFormat >>> 0,
                    remapReference: remapReference >>> 0,
                    block0Offset: techniqueSetBlock0Offset >>> 0,
                    nameBlock4Offset: techniqueSetNameBlock4Offset >>> 0,
                    boundaryInflatedOffset: techniqueSetBoundaryInflatedOffset >>> 0,
                    firstTechniqueSlot: firstTechniqueSlot >>> 0,
                    firstTechniqueReference: firstTechniqueReference >>> 0,
                    references: {
                        null: nullTechniqueReferences >>> 0,
                        inline: inlineTechniqueReferences >>> 0,
                        shared: sharedTechniqueReferences >>> 0,
                        alias: aliasTechniqueReferences >>> 0,
                    },
                    identity: techniqueSetIdentity >>> 0,
                    registryAliasCount: registryAliasCount >>> 0,
                    registryDefinedAliasCount: registryDefinedAliasCount >>> 0,
                    published: Boolean(techniqueSetPublished),
                    stoppedBeforeDependency: Boolean(
                        stoppedBeforeTechniqueDependency && (completedAssetCount >>> 0) === 0),
                    unsupportedOperation: UTF8ToString(unsupportedOperation),
                },
                typeCounts: [],
                typesBeforeFirstGfxWorld: [],
            };
        }
    });

EM_JS(
    void,
    AppendRetailGfxWorldSurface,
    (const char *state, const char *message, const char *worldName,
     const char *materialName, uint32_t surfaceIndex,
     uint32_t vertexCount, uint32_t triangleCount,
     uint32_t horizontalAxis, uint32_t verticalAxis, uint32_t depthAxis,
     double minX, double minY, double minZ,
     double maxX, double maxY, double maxZ),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        if (!inventory) return;
        inventory.gfxWorld = {
            name: UTF8ToString(worldName),
            rendererSurface: {
                state: UTF8ToString(state),
                message: UTF8ToString(message),
                submissionState: UTF8ToString(state) === "submitted"
                    ? "submitted" : "not-submitted",
                renderState: "pending-first-frame",
                rendered: false,
                surfaceIndex: surfaceIndex >>> 0,
                materialName: UTF8ToString(materialName),
                vertexCount: vertexCount >>> 0,
                triangleCount: triangleCount >>> 0,
                horizontalAxis: horizontalAxis >>> 0,
                verticalAxis: verticalAxis >>> 0,
                depthAxis: depthAxis >>> 0,
                mins: [minX, minY, minZ],
                maxs: [maxX, maxY, maxZ],
            },
        };
    });

EM_JS(
    void,
    AppendRetailWorldFxEffect,
    (uint32_t assetIndex, const char *name, int flags, int totalSize,
     int msecLoopingLife, uint32_t loopingCount, uint32_t oneShotCount,
     uint32_t emissionCount, uint32_t identity,
     uint32_t boundaryInflatedOffset, uint32_t materialCount, int published),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        if (!inventory) return;
        inventory.fxEffects.push({
            assetIndex: assetIndex >>> 0,
            name: UTF8ToString(name),
            flags: flags | 0,
            totalSize: totalSize | 0,
            msecLoopingLife: msecLoopingLife | 0,
            elementCounts: {
                looping: loopingCount >>> 0,
                oneShot: oneShotCount >>> 0,
                emission: emissionCount >>> 0,
            },
            identity: identity >>> 0,
            boundaryInflatedOffset: boundaryInflatedOffset >>> 0,
            materialCount: materialCount >>> 0,
            published: Boolean(published),
            elements: [],
        });
    });

EM_JS(
    void,
    AppendRetailWorldFxElem,
    (uint32_t assetIndex, uint32_t elemIndex, uint32_t elemType,
     uint32_t visualCount, uint32_t velocityIntervalCount,
     uint32_t visualStateIntervalCount, uint32_t velocitySamplesHash,
     uint32_t visualSamplesHash, uint32_t trailPayloadHash,
     uint32_t trailVertexCount, uint32_t trailIndexCount, int traversed),
    {
        const effects = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__
            ?.worldInventory?.fxEffects;
        const effect = effects?.find(
            (entry) => entry.assetIndex === (assetIndex >>> 0));
        if (!effect) return;
        effect.elements.push({
            index: elemIndex >>> 0,
            type: elemType >>> 0,
            visualCount: visualCount >>> 0,
            velocityIntervalCount: velocityIntervalCount >>> 0,
            visualStateIntervalCount: visualStateIntervalCount >>> 0,
            velocitySamplesHash: velocitySamplesHash >>> 0,
            visualSamplesHash: visualSamplesHash >>> 0,
            trailPayloadHash: trailPayloadHash >>> 0,
            trailVertexCount: trailVertexCount >>> 0,
            trailIndexCount: trailIndexCount >>> 0,
            traversed: Boolean(traversed),
        });
    });

EM_JS(
    void,
    FinalizeRetailPostXModelTechniqueSetRun,
    (uint32_t xmodelAssetIndex,
     uint32_t firstAssetIndex,
     uint32_t bodiesEntered,
     uint32_t completedCount),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        if (!inventory) return;
        const post = inventory.techniqueSets.filter(
            (entry) => entry.assetIndex > (xmodelAssetIndex >>> 0));
        inventory.postXModelTechniqueSets = post.map(
            (entry) => ({...entry, source: "generated-loader-after-first-xmodel"}));
        inventory.postXModelTechniqueSet = post.length !== 0
            ? {...post[0], source: "generated-loader-after-first-xmodel"}
            : null;
        inventory.postXModelTechniqueSetRun = {
            firstAssetIndex: firstAssetIndex >>> 0,
            bodiesEntered: bodiesEntered >>> 0,
            completedCount: completedCount >>> 0,
            nextBodyIndex: inventory.nextBodyIndex >>> 0,
            nextBodyType: inventory.nextBodyType >>> 0,
            nextBodyReference: inventory.nextBodyReference >>> 0,
            stoppedBeforeDifferentAssetType: Boolean(
                inventory.stoppedBeforeDifferentAssetType),
            stoppedBeforeTechniqueDependency: Boolean(
                inventory.stoppedBeforeTechniqueDependency),
        };
    });

EM_JS(
    void,
    AppendRetailWorldTechniqueSet,
    (uint32_t assetIndex,
     const char *name,
     uint32_t worldVertFormat,
     uint32_t remapReference,
     uint32_t block0Offset,
     uint32_t nameBlock4Offset,
     uint32_t boundaryInflatedOffset,
     uint32_t firstTechniqueSlot,
     uint32_t firstTechniqueReference,
     uint32_t nullTechniqueReferences,
     uint32_t inlineTechniqueReferences,
     uint32_t sharedTechniqueReferences,
     uint32_t aliasTechniqueReferences,
     uint32_t identity,
     int published),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        if (!inventory) return;
        inventory.techniqueSets.push({
            assetIndex: assetIndex >>> 0,
            name: UTF8ToString(name),
            worldVertFormat: worldVertFormat >>> 0,
            remapReference: remapReference >>> 0,
            block0Offset: block0Offset >>> 0,
            nameBlock4Offset: nameBlock4Offset >>> 0,
            boundaryInflatedOffset: boundaryInflatedOffset >>> 0,
            firstTechniqueSlot: firstTechniqueSlot >>> 0,
            firstTechniqueReference: firstTechniqueReference >>> 0,
            references: {
                null: nullTechniqueReferences >>> 0,
                inline: inlineTechniqueReferences >>> 0,
                shared: sharedTechniqueReferences >>> 0,
                alias: aliasTechniqueReferences >>> 0,
            },
            identity: identity >>> 0,
            published: Boolean(published),
            techniques: [],
        });
    });

EM_JS(
    void,
    AppendRetailWorldMaterialTechnique,
    (uint32_t assetIndex, uint32_t slot, const char *name,
     uint32_t flags, uint32_t passCount,
     uint32_t headerBlock4Offset, uint32_t passArrayBlock4Offset,
     uint32_t nameBlock4Offset, uint32_t argumentCount,
     uint32_t vertexProgramDwords, uint32_t pixelProgramDwords,
     uint32_t boundaryInflatedOffset, int completed),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        if (!inventory) return;
        const set = inventory.techniqueSets.find(
            (entry) => entry.assetIndex === (assetIndex >>> 0));
        if (!set) return;
        set.techniques.push({
            slot: slot >>> 0,
            name: UTF8ToString(name),
            flags: flags >>> 0,
            passCount: passCount >>> 0,
            headerBlock4Offset: headerBlock4Offset >>> 0,
            passArrayBlock4Offset: passArrayBlock4Offset >>> 0,
            nameBlock4Offset: nameBlock4Offset >>> 0,
            argumentCount: argumentCount >>> 0,
            vertexProgramDwords: vertexProgramDwords >>> 0,
            pixelProgramDwords: pixelProgramDwords >>> 0,
            boundaryInflatedOffset: boundaryInflatedOffset >>> 0,
            completed: Boolean(completed),
        });
    });

EM_JS(
    void,
    BeginRetailWorldXModel,
    (uint32_t assetIndex, const char *name,
     uint32_t numBones, uint32_t numRootBones, uint32_t surfaceCount,
     uint32_t lodRampType, uint32_t boneNamesReference,
     uint32_t parentListReference, uint32_t quatsReference,
     uint32_t transReference, uint32_t partClassificationReference,
     uint32_t baseMatReference, uint32_t surfacesReference,
     uint32_t materialHandlesReference, uint32_t collisionSurfacesReference,
     uint32_t collisionSurfaceCount, uint32_t contents,
     uint32_t boneInfoReference, double radius,
     double minX, double minY, double minZ,
     double maxX, double maxY, double maxZ,
     int32_t lodCount, int32_t collisionLod, uint32_t memoryUsage,
     uint32_t flags, int bad, uint32_t physPresetReference,
     uint32_t physGeomsReference, uint32_t headerBlock0Offset,
     uint32_t nameBlock4Offset, uint32_t boneNamesBlock4Offset,
     uint32_t parentListBlock4Offset, uint32_t quatsBlock4Offset,
     uint32_t transBlock4Offset, uint32_t partClassificationBlock4Offset,
     uint32_t baseMatBlock4Offset, uint32_t surfacesBlock4Offset,
     uint32_t materialHandlesBlock4Offset, uint32_t boundaryInflatedOffset,
     uint32_t totalVertices, uint32_t totalTriangles,
     uint32_t totalRigidVertLists, uint32_t totalCollisionNodes,
     uint32_t totalCollisionLeaves, uint32_t surfacePayloadBytes,
     int headerTraversed, int skeletonPrefixTraversed,
     int surfaceHeadersTraversed, int surfaceDependenciesTraversed,
     int materialHandlesTraversed, int stoppedBeforeSurfaceArray,
     int stoppedBeforeMaterialDependency, const char *unsupportedOperation,
     int modelIndex),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        if (!inventory) return;
        modelIndex = modelIndex >>> 0;
        const model = {
            assetIndex: assetIndex >>> 0,
            name: UTF8ToString(name),
            numBones: numBones >>> 0,
            numRootBones: numRootBones >>> 0,
            surfaceCount: surfaceCount >>> 0,
            lodRampType: lodRampType >>> 0,
            references: {
                boneNames: boneNamesReference >>> 0,
                parentList: parentListReference >>> 0,
                quats: quatsReference >>> 0,
                trans: transReference >>> 0,
                partClassification: partClassificationReference >>> 0,
                baseMat: baseMatReference >>> 0,
                surfaces: surfacesReference >>> 0,
                materialHandles: materialHandlesReference >>> 0,
                collisionSurfaces: collisionSurfacesReference >>> 0,
                boneInfo: boneInfoReference >>> 0,
                physPreset: physPresetReference >>> 0,
                physGeoms: physGeomsReference >>> 0,
            },
            collisionSurfaceCount: collisionSurfaceCount >>> 0,
            contents: contents >>> 0,
            radius,
            mins: [minX, minY, minZ],
            maxs: [maxX, maxY, maxZ],
            lodCount,
            collisionLod,
            memoryUsage: memoryUsage >>> 0,
            flags: flags >>> 0,
            bad: Boolean(bad),
            physPresetIdentity: 0,
            physPreset: null,
            offsets: {
                headerBlock0: headerBlock0Offset >>> 0,
                nameBlock4: nameBlock4Offset >>> 0,
                boneNamesBlock4: boneNamesBlock4Offset >>> 0,
                parentListBlock4: parentListBlock4Offset >>> 0,
                quatsBlock4: quatsBlock4Offset >>> 0,
                transBlock4: transBlock4Offset >>> 0,
                partClassificationBlock4: partClassificationBlock4Offset >>> 0,
                baseMatBlock4: baseMatBlock4Offset >>> 0,
                surfacesBlock4: surfacesBlock4Offset >>> 0,
                materialHandlesBlock4: materialHandlesBlock4Offset >>> 0,
            },
            totals: {
                vertices: totalVertices >>> 0,
                triangles: totalTriangles >>> 0,
                rigidVertLists: totalRigidVertLists >>> 0,
                collisionNodes: totalCollisionNodes >>> 0,
                collisionLeaves: totalCollisionLeaves >>> 0,
                surfacePayloadBytes: surfacePayloadBytes >>> 0,
            },
            boundaryInflatedOffset: boundaryInflatedOffset >>> 0,
            identity: 0,
            published: false,
            headerTraversed: Boolean(headerTraversed),
            skeletonPrefixTraversed: Boolean(skeletonPrefixTraversed),
            surfaceHeadersTraversed: Boolean(surfaceHeadersTraversed),
            surfaceDependenciesTraversed: Boolean(surfaceDependenciesTraversed),
            materialHandlesTraversed: Boolean(materialHandlesTraversed),
            stoppedBeforeSurfaceArray: Boolean(stoppedBeforeSurfaceArray),
            stoppedBeforeMaterialDependency: Boolean(stoppedBeforeMaterialDependency),
            unsupportedOperation: UTF8ToString(unsupportedOperation),
            lods: [],
            boneNames: [],
            surfaces: [],
            materialReferences: [],
        };
        inventory.xmodels[modelIndex] = model;
        if (modelIndex === 0) inventory.firstXModel = model;
    });

EM_JS(
    void,
    AppendRetailWorldXModelLod,
    (uint32_t index, double distance, uint32_t surfaceCount,
     uint32_t surfaceIndex, uint32_t partBits0, uint32_t partBits1,
     uint32_t partBits2, uint32_t partBits3, uint32_t lod,
     uint32_t smcIndexPlusOne, uint32_t smcAllocBits, int modelIndex),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        const model = inventory?.xmodels?.[modelIndex >>> 0];
        if (!model) return;
        model.lods.push({
            index: index >>> 0,
            distance,
            surfaceCount: surfaceCount >>> 0,
            surfaceIndex: surfaceIndex >>> 0,
            partBits: [partBits0 >>> 0, partBits1 >>> 0,
                partBits2 >>> 0, partBits3 >>> 0],
            lod: lod >>> 0,
            smcIndexPlusOne: smcIndexPlusOne >>> 0,
            smcAllocBits: smcAllocBits >>> 0,
        });
    });

EM_JS(
    void,
    AppendRetailWorldXModelBone,
    (uint32_t index, uint32_t scriptStringIndex,
     const char *name, uint32_t classification, int modelIndex),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        const model = inventory?.xmodels?.[modelIndex >>> 0];
        if (!model) return;
        model.boneNames.push({
            index: index >>> 0,
            scriptStringIndex: scriptStringIndex >>> 0,
            name: UTF8ToString(name),
            classification: classification >>> 0,
        });
    });

EM_JS(
    void,
    AppendRetailWorldXSurface,
    (uint32_t index, uint32_t tileMode, int deformed,
     uint32_t vertCount, uint32_t triCount, uint32_t zoneHandle,
     uint32_t baseTriIndex, uint32_t baseVertIndex,
     uint32_t triIndicesReference, uint32_t vertsBlendReference,
     uint32_t vertsReference, uint32_t vertListCount,
     uint32_t vertListReference, uint32_t blendWordCount,
     uint32_t verticesBlock7Offset, uint32_t vertListsBlock4Offset,
     uint32_t indicesBlock8Offset, uint32_t verticesHash,
     uint32_t indicesHash, int dependenciesTraversed, int modelIndex),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        const model = inventory?.xmodels?.[modelIndex >>> 0];
        if (!model) return;
        model.surfaces.push({
            index: index >>> 0,
            tileMode: tileMode >>> 0,
            deformed: Boolean(deformed),
            vertCount: vertCount >>> 0,
            triCount: triCount >>> 0,
            zoneHandle: zoneHandle >>> 0,
            baseTriIndex: baseTriIndex >>> 0,
            baseVertIndex: baseVertIndex >>> 0,
            references: {
                triIndices: triIndicesReference >>> 0,
                vertsBlend: vertsBlendReference >>> 0,
                vertices: vertsReference >>> 0,
                vertLists: vertListReference >>> 0,
            },
            vertListCount: vertListCount >>> 0,
            blendWordCount: blendWordCount >>> 0,
            offsets: {
                verticesBlock7: verticesBlock7Offset >>> 0,
                vertListsBlock4: vertListsBlock4Offset >>> 0,
                indicesBlock8: indicesBlock8Offset >>> 0,
            },
            verticesHash: verticesHash >>> 0,
            indicesHash: indicesHash >>> 0,
            dependenciesTraversed: Boolean(dependenciesTraversed),
            rigidVertLists: [],
        });
    });

EM_JS(
    void,
    AppendRetailWorldXSurfaceRigidList,
    (uint32_t surfaceIndex, uint32_t index, uint32_t boneOffset,
     uint32_t vertCount, uint32_t triOffset, uint32_t triCount,
     uint32_t treeReference, double transX, double transY, double transZ,
     double scaleX, double scaleY, double scaleZ,
     uint32_t nodeCount, uint32_t nodesReference,
     uint32_t leafCount, uint32_t leafsReference,
     uint32_t nodesHash, uint32_t leafsHash, int treeTraversed,
     int modelIndex),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        const surface = inventory?.xmodels?.[modelIndex >>> 0]
            ?.surfaces?.[surfaceIndex >>> 0];
        if (!surface) return;
        surface.rigidVertLists.push({
            index: index >>> 0,
            boneOffset: boneOffset >>> 0,
            vertCount: vertCount >>> 0,
            triOffset: triOffset >>> 0,
            triCount: triCount >>> 0,
            collisionTree: {
                reference: treeReference >>> 0,
                translation: [transX, transY, transZ],
                scale: [scaleX, scaleY, scaleZ],
                nodeCount: nodeCount >>> 0,
                nodesReference: nodesReference >>> 0,
                leafCount: leafCount >>> 0,
                leafsReference: leafsReference >>> 0,
                nodesHash: nodesHash >>> 0,
                leafsHash: leafsHash >>> 0,
                traversed: Boolean(treeTraversed),
            },
        });
    });

EM_JS(
    void,
    AppendRetailWorldXModelMaterialReference,
    (uint32_t index, uint32_t reference, uint32_t identity, int modelIndex),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        const model = inventory?.xmodels?.[modelIndex >>> 0];
        if (!model) return;
        model.materialReferences.push({
            index: index >>> 0,
            reference: reference >>> 0,
            identity: identity >>> 0,
        });
    });

EM_JS(
    void,
    CompleteRetailWorldXModelDependencies,
    (uint32_t identity, uint32_t collisionSurfacesBlock4Offset,
     uint32_t boneInfoBlock4Offset, uint32_t collisionTriangleCount,
     uint32_t collisionPayloadBytes, uint32_t boneInfoHash,
     int materialsTraversed, int collisionSurfacesTraversed,
     int boneInfoTraversed, int physPresetTraversed,
     int physGeomsTraversed, int published, int modelIndex),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        const model = inventory?.xmodels?.[modelIndex >>> 0];
        if (!model) return;
        model.identity = identity >>> 0;
        model.offsets.collisionSurfacesBlock4 = collisionSurfacesBlock4Offset >>> 0;
        model.offsets.boneInfoBlock4 = boneInfoBlock4Offset >>> 0;
        model.totals.collisionTriangles = collisionTriangleCount >>> 0;
        model.totals.collisionPayloadBytes = collisionPayloadBytes >>> 0;
        model.boneInfoHash = boneInfoHash >>> 0;
        model.materialsTraversed = Boolean(materialsTraversed);
        model.collisionSurfacesTraversed = Boolean(collisionSurfacesTraversed);
        model.boneInfoTraversed = Boolean(boneInfoTraversed);
        model.physPresetTraversed = Boolean(physPresetTraversed);
        model.physGeomsTraversed = Boolean(physGeomsTraversed);
        model.published = Boolean(published);
        model.materials = [];
        model.collisionSurfaces = [];
    });

EM_JS(
    void,
    SetRetailWorldXModelPhysPreset,
    (int modelIndex, uint32_t identity, const char *name,
     const char *soundAliasPrefix, int32_t type,
     double mass, double bounce, double friction,
     double bulletForceScale, double explosiveForceScale,
     double piecesSpreadFraction, double piecesUpwardVelocity,
     int tempDefaultToCylinder, uint32_t nameReference,
     uint32_t soundAliasPrefixReference, uint32_t headerBlock0Offset,
     uint32_t nameBlock4Offset, uint32_t soundAliasPrefixBlock4Offset,
     uint32_t insertPointerBlock4Offset, int traversed, int published),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        const model = inventory?.xmodels?.[modelIndex >>> 0];
        if (!model) return;
        model.physPresetIdentity = identity >>> 0;
        if ((identity >>> 0) === 0 && !traversed && !published) return;
        model.physPreset = {
            identity: identity >>> 0,
            name: UTF8ToString(name),
            soundAliasPrefix: UTF8ToString(soundAliasPrefix),
            type,
            mass,
            bounce,
            friction,
            bulletForceScale,
            explosiveForceScale,
            piecesSpreadFraction,
            piecesUpwardVelocity,
            tempDefaultToCylinder: Boolean(tempDefaultToCylinder),
            references: {
                name: nameReference >>> 0,
                soundAliasPrefix: soundAliasPrefixReference >>> 0,
            },
            offsets: {
                headerBlock0: headerBlock0Offset >>> 0,
                nameBlock4: nameBlock4Offset >>> 0,
                soundAliasPrefixBlock4: soundAliasPrefixBlock4Offset >>> 0,
                insertPointerBlock4: insertPointerBlock4Offset >>> 0,
            },
            traversed: Boolean(traversed),
            published: Boolean(published),
        };
    });

EM_JS(
    void,
    AppendRetailWorldXModelMaterial,
    (uint32_t handleIndex, const char *name, uint32_t techniqueSetReference,
     uint32_t techniqueSetIdentity, uint32_t textureCount,
     uint32_t constantCount, uint32_t stateBitsCount,
     uint32_t headerBlock0Offset, uint32_t nameBlock4Offset,
     uint32_t textureTableBlock4Offset, uint32_t constantTableBlock4Offset,
     uint32_t stateBitsTableBlock4Offset, uint32_t constantsHash,
     uint32_t stateBitsHash, uint32_t identity, int published,
     int modelIndex),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        const model = inventory?.xmodels?.[modelIndex >>> 0];
        if (!model) return;
        model.materials.push({
            handleIndex: handleIndex >>> 0,
            name: UTF8ToString(name),
            techniqueSetReference: techniqueSetReference >>> 0,
            techniqueSetIdentity: techniqueSetIdentity >>> 0,
            textureCount: textureCount >>> 0,
            constantCount: constantCount >>> 0,
            stateBitsCount: stateBitsCount >>> 0,
            offsets: {
                headerBlock0: headerBlock0Offset >>> 0,
                nameBlock4: nameBlock4Offset >>> 0,
                textureTableBlock4: textureTableBlock4Offset >>> 0,
                constantTableBlock4: constantTableBlock4Offset >>> 0,
                stateBitsTableBlock4: stateBitsTableBlock4Offset >>> 0,
            },
            constantsHash: constantsHash >>> 0,
            stateBitsHash: stateBitsHash >>> 0,
            identity: identity >>> 0,
            published: Boolean(published),
            textures: [],
            images: [],
        });
    });

EM_JS(
    void,
    AppendRetailWorldXModelMaterialTexture,
    (uint32_t materialIndex, uint32_t index, uint32_t nameHash,
     uint32_t nameStart, uint32_t nameEnd, uint32_t samplerState,
     uint32_t semantic, uint32_t imageReference,
     uint32_t imageIdentity, int resolved, int modelIndex),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        const material = inventory?.xmodels?.[modelIndex >>> 0]
            ?.materials?.[materialIndex >>> 0];
        if (!material) return;
        material.textures.push({
            index: index >>> 0,
            nameHash: nameHash >>> 0,
            nameStart: nameStart >>> 0,
            nameEnd: nameEnd >>> 0,
            samplerState: samplerState >>> 0,
            semantic: semantic >>> 0,
            imageReference: imageReference >>> 0,
            imageIdentity: imageIdentity >>> 0,
            resolved: Boolean(resolved),
        });
    });

EM_JS(
    void,
    AppendRetailWorldXModelImage,
    (uint32_t materialIndex, uint32_t textureIndex, const char *name,
     uint32_t mapType, uint32_t textureReference, uint32_t width,
     uint32_t height, uint32_t depth, uint32_t format,
     uint32_t resourceBytes, uint32_t headerBlock0Offset,
     uint32_t nameBlock4Offset, uint32_t textureInsertPointerBlock4Offset,
     uint32_t loadDefBlock0Offset,
     uint32_t identity, int loadDefTraversed, int published,
     int modelIndex),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        const material = inventory?.xmodels?.[modelIndex >>> 0]
            ?.materials?.[materialIndex >>> 0];
        if (!material) return;
        material.images.push({
            textureIndex: textureIndex >>> 0,
            name: UTF8ToString(name),
            mapType: mapType >>> 0,
            textureReference: textureReference >>> 0,
            dimensions: [width >>> 0, height >>> 0, depth >>> 0],
            format: format >>> 0,
            resourceBytes: resourceBytes >>> 0,
            offsets: {
                headerBlock0: headerBlock0Offset >>> 0,
                nameBlock4: nameBlock4Offset >>> 0,
                textureInsertPointerBlock4:
                    textureInsertPointerBlock4Offset >>> 0,
                loadDefBlock0: loadDefBlock0Offset >>> 0,
            },
            identity: identity >>> 0,
            loadDefTraversed: Boolean(loadDefTraversed),
            published: Boolean(published),
        });
    });

EM_JS(
    void,
    AppendRetailWorldXModelCollisionSurface,
    (uint32_t index, uint32_t trianglesReference, uint32_t triangleCount,
     double minX, double minY, double minZ,
     double maxX, double maxY, double maxZ,
     int32_t boneIndex, int32_t contents, int32_t surfaceFlags,
     uint32_t trianglesBlock4Offset, uint32_t trianglesHash,
     int traversed, int modelIndex),
    {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        const model = inventory?.xmodels?.[modelIndex >>> 0];
        if (!model) return;
        model.collisionSurfaces.push({
            index: index >>> 0,
            trianglesReference: trianglesReference >>> 0,
            triangleCount: triangleCount >>> 0,
            mins: [minX, minY, minZ],
            maxs: [maxX, maxY, maxZ],
            boneIndex,
            contents,
            surfaceFlags,
            trianglesBlock4Offset: trianglesBlock4Offset >>> 0,
            trianglesHash: trianglesHash >>> 0,
            traversed: Boolean(traversed),
        });
    });

EM_JS(void, AppendRetailWorldInventoryType,
    (uint32_t type, const char *name, uint32_t total, uint32_t beforeWorld), {
        const inventory = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.worldInventory;
        if (!inventory) return;
        if (total) inventory.typeCounts.push({
            type: type >>> 0,
            name: UTF8ToString(name),
            count: total >>> 0,
        });
        if (beforeWorld) inventory.typesBeforeFirstGfxWorld.push({
            type: type >>> 0,
            name: UTF8ToString(name),
            count: beforeWorld >>> 0,
        });
    });

EM_JS(void, AppendRetailCensusBlock, (uint32_t block, uint32_t size), {
    globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.blockSizes.push({
        block: block >>> 0,
        size: size >>> 0
    });
});

EM_JS(
    void,
    AppendRetailCensusType,
    (uint32_t type, const char *name, uint32_t count),
    {
        globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__?.typeCounts.push({
            type: type >>> 0,
            name: UTF8ToString(name),
            count: count >>> 0
        });
    });

EM_JS(void, EndRetailCensusReady, (), {
    const detail = globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__;
    delete globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__;
    if (detail) {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:retail-census", { detail }));
    }
});

EM_JS(void, DiscardRetailCensusReady, (), {
    delete globalThis.__KISAKCOD_RETAIL_CENSUS_DETAIL__;
});

EM_JS(
    void,
    DispatchRetailCensusFailure,
    (uint32_t generation, const char *stage, const char *path,
     const char *error, const char *message),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:retail-census", {
            detail: {
                state: "failed",
                stage: UTF8ToString(stage),
                generation: generation >>> 0,
                path: UTF8ToString(path),
                error: UTF8ToString(error),
                message: UTF8ToString(message),
                completedAssetCount: 0,
                techniqueSetPublished: false,
                failClosed: true
            }
        }));
    });

EM_JS(void, DispatchRetailCensusIdle, (uint32_t generation), {
    globalThis.dispatchEvent(new CustomEvent("kisakcod:retail-census", {
        detail: {
            state: "idle",
            stage: "idle",
            generation: generation >>> 0,
            path: "zone/english/code_post_gfx.ff",
            message: "Waiting for qcommon pre-database startup",
            assetBodyTraversal: "two-techsets-one-material"
        }
    }));
});

const char *WebFsStatusString(WebFsStatus status)
{
    switch (status)
    {
    case WebFsStatus::Success: return "success";
    case WebFsStatus::Pending: return "pending";
    case WebFsStatus::NotReady: return "filesystem bridge is not ready";
    case WebFsStatus::InvalidArgument: return "invalid filesystem request";
    case WebFsStatus::NoRequestSlots: return "filesystem request table is full";
    case WebFsStatus::InvalidRange: return "filesystem range is invalid";
    case WebFsStatus::NotFound: return "retail fastfile was not found";
    case WebFsStatus::StaleSource: return "browser asset import changed during census";
    case WebFsStatus::IoError: return "browser filesystem I/O failed";
    case WebFsStatus::ProtocolError: return "browser filesystem protocol failed";
    case WebFsStatus::Cancelled: return "filesystem request was cancelled";
    }
    return "unknown filesystem error";
}

void Reset(bool keepGeneration)
{
    const std::uint32_t generation = keepGeneration ? g_runtime.generation : 0u;
    g_runtime = RetailCensusRuntime{};
    g_runtime.generation = generation;
}

void CompleteRequest(const WebFsCompletion &completion, void *userData)
{
    const auto generation = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(userData));
    if (generation != g_runtime.generation || completion.requestId != g_runtime.requestId)
        return;
    g_runtime.requestId = 0u;
    g_runtime.completionReady = true;
    g_runtime.completionStatus = completion.status;
    if (completion.status != WebFsStatus::Success) return;
    if (completion.operation == WebFsOperation::Stat)
    {
        g_runtime.fileSize = completion.fileSize;
        return;
    }
    try
    {
        g_runtime.completionBytes.assign(
            completion.data, completion.data + completion.dataLength);
    }
    catch (...)
    {
        g_runtime.completionStatus = WebFsStatus::IoError;
    }
}

void Fail(const char *context, const char *reason)
{
    char message[384]{};
    std::snprintf(message, sizeof(message), "%s: %s", context, reason);
    message[sizeof(message) - 1u] = '\0';
    if (g_runtime.requestId != 0u)
    {
        (void)WebFs_Cancel(g_runtime.requestId);
        g_runtime.requestId = 0u;
    }
    g_runtime.phase = Phase::Failed;
    const auto stage = g_runtime.parser.Stage();
    DispatchRetailCensusFailure(
        g_runtime.generation,
        kisak::fastfile::RetailCensusStageString(stage),
        CurrentPath(),
        reason,
        message);
    Web_Log(WebLogLevel::Error, "[kisakcod-web] Retail fastfile census failed: %s\n", message);
}

RetailGfxWorldSurfacePublication PublishRetailGfxWorldSurface(bool shaderReady)
{
    RetailGfxWorldSurfacePublication publication;
    if (!shaderReady)
    {
        publication.message =
            "The retail shader binding was unavailable; canonical world geometry was not submitted";
        return publication;
    }
    const auto &worlds = g_runtime.worldInventory.worldGfxWorlds;
    if (worlds.empty() || !worlds.front().published || !worlds.front().asset)
    {
        publication.message = "The canonical GfxWorld was not atomically published";
        return publication;
    }

    publication.result = WebEngine_BuildGfxWorldSurface(
        *worlds.front().asset, publication.surface);
    if (publication.result != WebEngineGfxWorldSurfaceResult::Success)
    {
        publication.message =
            WebEngine_GfxWorldSurfaceResultString(publication.result);
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Canonical GfxWorld renderer adaptation failed: %s.\n",
            publication.message);
        return publication;
    }

    const auto &converted = publication.surface.rendererSurface;
    const WebRendererSurfaceDesc descriptor{
        converted.vertices.data(),
        static_cast<std::uint32_t>(converted.vertices.size()),
        converted.indices.data(),
        static_cast<std::uint32_t>(converted.indices.size()),
    };
    const WebRendererSurfaceResult submission =
        WebRenderer_SetSurface(descriptor, converted.draw);
    if (submission != WebRendererSurfaceResult::Success)
    {
        publication.message = WebRenderer_SurfaceResultString(submission);
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Canonical GfxWorld surface submission failed: %s.\n",
            publication.message);
        return publication;
    }

    publication.state = "submitted";
    publication.message =
        "Canonical Killhouse geometry was submitted; rendering is confirmed by the subsequent first-frame event";
    const GfxWorld &world = *worlds.front().asset;
    Web_Log(
        WebLogLevel::Info,
        "[kisakcod-web] Submitted canonical GfxWorld '%s' surface %u "
        "(%u vertices, %u triangles, material '%s', identity %u) to WebGL2; first-frame rendering is pending.\n",
        world.name ? world.name : "",
        publication.surface.surfaceIndex,
        publication.surface.vertexCount,
        publication.surface.triangleCount,
        publication.surface.materialName ? publication.surface.materialName : "",
        publication.surface.surfaceIndex < worlds.front().surfaceMaterialIdentities.size()
            ? worlds.front().surfaceMaterialIdentities[publication.surface.surfaceIndex]
            : 0u);
    return publication;
}

void PublishReady()
{
    const auto &result = g_runtime.result;
    bool shaderReady = false;
    kisak::web::WebGL2ShaderSubstitution substitution;
    if (!kisak::web::LookupWebGL2ShaderSubstitution(
            result.shaderSubstitutionId, substitution) ||
        substitution.vertexSourceHash != result.vertexGlslHash ||
        substitution.fragmentSourceHash != result.fragmentGlslHash)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Published shader compatibility metadata did not resolve to its registry source.\n");
    }
    else
    {
        const WebRendererShaderResult shaderResult =
            WebRenderer_SetShaderCompatibility(substitution);
        if (shaderResult != WebRendererShaderResult::Success)
        {
            Web_Log(
                WebLogLevel::Error,
                "[kisakcod-web] Renderer rejected %s: %s.\n",
                substitution.id,
                WebRenderer_ShaderResultString(shaderResult));
        }
        else
        {
            shaderReady = true;
        }
    }
    const RetailGfxWorldSurfacePublication gfxWorldSurface =
        PublishRetailGfxWorldSurface(shaderReady);
    BeginRetailCensusReady(
        g_runtime.generation,
        g_runtime.codePostFileSize,
        g_runtime.codePostSourceBytesRead,
        static_cast<double>(result.sourceBytesConsumed),
        result.sourceFeedCount,
        result.version,
        result.xfileSize,
        result.externalSize,
        static_cast<double>(result.declaredBlockBytes),
        result.scriptStringCount,
        result.scriptStringBytes,
        result.assetCount,
        result.inflatedPrefixBytes,
        result.inlineAssetReferences,
        result.sharedAssetReferences,
        result.aliasAssetReferences,
        result.nullAssetReferences,
        result.firstBodyIndex,
        result.firstBodyType,
        kisak::fastfile::RetailAssetTypeName(result.firstBodyType),
        result.firstBodyReference);
    AppendRetailTechniqueTraversal(
        result.techniqueSetName.c_str(),
        result.firstTechniqueSlot,
        result.techniquePassCount,
        result.vertexStreamCount,
        result.vertexStreamRoutingHash,
        result.vertexShaderName.c_str(),
        result.vertexShaderProgramDwords,
        result.vertexShaderProgramHash,
        result.assetTableBlock4Offset,
        result.techniqueSetBlock0Offset,
        result.techniqueBlock4Offset,
        result.vertexDeclarationBlock4Offset,
        result.vertexShaderBlock4Offset,
        result.vertexShaderProgramBlock4Offset,
        result.block0HighWaterAtBoundary,
        result.block4CursorAtBoundary,
        result.completedAssetCount,
        result.techniqueSetPublished ? 1 : 0,
        result.vertexDeclarationPrepared ? 1 : 0,
        result.stoppedBeforeShaderCreation ? 1 : 0,
        result.unsupportedOperation ? result.unsupportedOperation : "unknown");
    AppendRetailShaderCompatibility(
        result.pixelShaderName.c_str(),
        result.vertexShaderInstructionCount,
        result.vertexShaderConstantCount,
        result.pixelShaderProgramDwords,
        result.pixelShaderProgramHash,
        result.pixelShaderInstructionCount,
        result.pixelShaderConstantCount,
        result.shaderArgumentCount,
        result.shaderArgumentHash,
        result.techniqueName.c_str(),
        result.shaderSubstitutionId.c_str(),
        result.vertexGlslHash,
        result.fragmentGlslHash,
        result.pixelShaderBlock4Offset,
        result.pixelShaderProgramBlock4Offset,
        result.shaderArgumentsBlock4Offset,
        result.shaderCompatibilitySelected ? 1 : 0);
    AppendRetailMaterialBinding(
        result.materialTechniqueSetName.c_str(),
        result.materialName.c_str(),
        result.imageName.c_str(),
        result.imagePath.c_str(),
        result.materialAssetIndex,
        result.materialTextureCount,
        result.imageWidth,
        result.imageHeight,
        result.imageDepth,
        result.imageFormat,
        result.imageResourceBytes,
        result.compatibilityTechniqueSetIdentity,
        result.materialTechniqueSetIdentity,
        result.materialIdentity,
        result.imageIdentity,
        result.registryAssetCount,
        result.registryAliasCount,
        result.registryDefinedAliasCount,
        result.materialTechniqueSetBlock0Offset,
        result.materialTechniqueBlock4Offset,
        result.materialBlock0Offset,
        result.materialNameBlock4Offset,
        result.materialTextureTableBlock4Offset,
        result.imageBlock0Offset,
        result.imageNameBlock4Offset,
        result.imageTextureInsertPointerBlock4Offset,
        result.imageLoadDefBlock0Offset,
        result.materialStateBitsBlock4Offset,
        result.materialTechniqueSetPublished ? 1 : 0,
        result.materialPublished ? 1 : 0,
        result.imagePublished ? 1 : 0,
        result.materialImageResolved ? 1 : 0);
    const auto &world = g_runtime.worldInventory;
    AppendRetailWorldInventory(
        g_runtime.worldFileSize,
        g_runtime.worldSourceBytesRead,
        static_cast<double>(world.sourceBytesConsumed),
        world.sourceFeedCount,
        world.assetCount,
        world.inflatedPrefixBytes,
        world.assetTableOrderHash,
        world.firstGfxWorldAssetIndex,
        world.firstGfxWorldReference,
        world.inlineReferencesBeforeFirstGfxWorld,
        world.sharedReferencesBeforeFirstGfxWorld,
        world.aliasReferencesBeforeFirstGfxWorld,
        world.nullReferencesBeforeFirstGfxWorld,
        world.firstBodyType,
        world.firstBodyReference,
        world.nextBodyType,
        world.nextBodyReference,
        world.nextBodyIndex,
        world.worldTechniqueSetBodiesEntered,
        world.completedAssetCount,
        world.stoppedBeforeDifferentWorldAssetType ? 1 : 0,
        world.block0HighWaterAtBoundary,
        world.block4CursorAtBoundary,
        world.worldFirstTechniqueSetName.c_str(),
        world.worldFirstTechniqueSetWorldVertFormat,
        world.worldFirstTechniqueSetRemapReference,
        world.worldFirstTechniqueSetBlock0Offset,
        world.worldFirstTechniqueSetNameBlock4Offset,
        world.worldFirstTechniqueSetBoundaryInflatedOffset,
        world.worldFirstTechniqueSlot,
        world.worldFirstTechniqueReference,
        world.worldTechniqueNullReferences,
        world.worldTechniqueInlineReferences,
        world.worldTechniqueSharedReferences,
        world.worldTechniqueAliasReferences,
        world.worldFirstTechniqueSetIdentity,
        world.worldRegistryAliasCount,
        world.worldRegistryDefinedAliasCount,
        world.worldFirstTechniqueSetPublished ? 1 : 0,
        world.stoppedBeforeWorldTechniqueDependency ? 1 : 0,
        world.unsupportedOperation ? world.unsupportedOperation : "");
    const GfxWorld *publishedWorld =
        !world.worldGfxWorlds.empty() && world.worldGfxWorlds.front().asset
            ? world.worldGfxWorlds.front().asset.get()
            : nullptr;
    AppendRetailGfxWorldSurface(
        gfxWorldSurface.state,
        gfxWorldSurface.message,
        publishedWorld && publishedWorld->name ? publishedWorld->name : "",
        gfxWorldSurface.surface.materialName
            ? gfxWorldSurface.surface.materialName : "",
        gfxWorldSurface.surface.surfaceIndex,
        gfxWorldSurface.surface.vertexCount,
        gfxWorldSurface.surface.triangleCount,
        gfxWorldSurface.surface.horizontalAxis,
        gfxWorldSurface.surface.verticalAxis,
        gfxWorldSurface.surface.depthAxis,
        gfxWorldSurface.surface.mins[0],
        gfxWorldSurface.surface.mins[1],
        gfxWorldSurface.surface.mins[2],
        gfxWorldSurface.surface.maxs[0],
        gfxWorldSurface.surface.maxs[1],
        gfxWorldSurface.surface.maxs[2]);
    for (const auto &techniqueSet : world.worldTechniqueSets)
    {
        AppendRetailWorldTechniqueSet(
            techniqueSet.assetIndex,
            techniqueSet.name.c_str(),
            techniqueSet.worldVertFormat,
            techniqueSet.remapReference,
            techniqueSet.block0Offset,
            techniqueSet.nameBlock4Offset,
            techniqueSet.boundaryInflatedOffset,
            techniqueSet.firstTechniqueSlot,
            techniqueSet.firstTechniqueReference,
            techniqueSet.nullTechniqueReferences,
            techniqueSet.inlineTechniqueReferences,
            techniqueSet.sharedTechniqueReferences,
            techniqueSet.aliasTechniqueReferences,
            techniqueSet.identity,
            techniqueSet.published ? 1 : 0);
        for (const auto &technique : techniqueSet.techniques)
        {
            AppendRetailWorldMaterialTechnique(
                techniqueSet.assetIndex,
                technique.slot,
                technique.name.c_str(),
                technique.flags,
                technique.passCount,
                technique.headerBlock4Offset,
                technique.passArrayBlock4Offset,
                technique.nameBlock4Offset,
                technique.argumentCount,
                technique.vertexProgramDwords,
                technique.pixelProgramDwords,
                technique.boundaryInflatedOffset,
                technique.completed ? 1 : 0);
        }
    }
    for (const auto &effect : world.worldFxEffects)
    {
        AppendRetailWorldFxEffect(
            effect.assetIndex,
            effect.name.c_str(),
            effect.flags,
            effect.totalSize,
            effect.msecLoopingLife,
            effect.loopingElemCount,
            effect.oneShotElemCount,
            effect.emissionElemCount,
            effect.identity,
            effect.boundaryInflatedOffset,
            static_cast<std::uint32_t>(effect.materials.size()),
            effect.published ? 1 : 0);
        for (std::size_t elemIndex = 0u;
             elemIndex < effect.elemDefs.size(); ++elemIndex)
        {
            const auto &elem = effect.elemDefs[elemIndex];
            AppendRetailWorldFxElem(
                effect.assetIndex,
                static_cast<std::uint32_t>(elemIndex),
                elem.elemType,
                static_cast<std::uint32_t>(elem.visualReferences.size()),
                elem.velocityIntervalCount,
                elem.visualStateIntervalCount,
                elem.velocitySamplesHash,
                elem.visualSamplesHash,
                elem.trailPayloadHash,
                elem.trailVertexCount,
                elem.trailIndexCount,
                elem.traversed ? 1 : 0);
        }
    }
    if (!world.worldXModels.empty())
    {
        const auto &xmodel = world.worldXModels.front();
        if (xmodel.headerTraversed)
        {
        FinalizeRetailPostXModelTechniqueSetRun(
            xmodel.assetIndex,
            world.worldPostXModelTechniqueSetAssetIndex,
            world.worldPostXModelTechniqueSetBodiesEntered,
            world.worldPostXModelTechniqueSetCompletedCount);
        BeginRetailWorldXModel(
            xmodel.assetIndex,
            xmodel.name.c_str(),
            xmodel.numBones,
            xmodel.numRootBones,
            xmodel.surfaceCount,
            xmodel.lodRampType,
            xmodel.boneNamesReference,
            xmodel.parentListReference,
            xmodel.quatsReference,
            xmodel.transReference,
            xmodel.partClassificationReference,
            xmodel.baseMatReference,
            xmodel.surfacesReference,
            xmodel.materialHandlesReference,
            xmodel.collisionSurfacesReference,
            xmodel.collisionSurfaceCount,
            xmodel.contents,
            xmodel.boneInfoReference,
            xmodel.radius,
            xmodel.mins[0], xmodel.mins[1], xmodel.mins[2],
            xmodel.maxs[0], xmodel.maxs[1], xmodel.maxs[2],
            xmodel.lodCount,
            xmodel.collisionLod,
            xmodel.memoryUsage,
            xmodel.flags,
            xmodel.bad ? 1 : 0,
            xmodel.physPresetReference,
            xmodel.physGeomsReference,
            xmodel.headerBlock0Offset,
            xmodel.nameBlock4Offset,
            xmodel.boneNamesBlock4Offset,
            xmodel.parentListBlock4Offset,
            xmodel.quatsBlock4Offset,
            xmodel.transBlock4Offset,
            xmodel.partClassificationBlock4Offset,
            xmodel.baseMatBlock4Offset,
            xmodel.surfacesBlock4Offset,
            xmodel.materialHandlesBlock4Offset,
            xmodel.boundaryInflatedOffset,
            xmodel.totalVertices,
            xmodel.totalTriangles,
            xmodel.totalRigidVertLists,
            xmodel.totalCollisionNodes,
            xmodel.totalCollisionLeaves,
            xmodel.surfacePayloadBytes,
            xmodel.headerTraversed ? 1 : 0,
            xmodel.skeletonPrefixTraversed ? 1 : 0,
            xmodel.surfaceHeadersTraversed ? 1 : 0,
            xmodel.surfaceDependenciesTraversed ? 1 : 0,
            xmodel.materialHandlesTraversed ? 1 : 0,
            xmodel.stoppedBeforeSurfaceArray ? 1 : 0,
            xmodel.stoppedBeforeMaterialDependency ? 1 : 0,
            world.unsupportedOperation ? world.unsupportedOperation : "",
            0);
        CompleteRetailWorldXModelDependencies(
            xmodel.identity,
            xmodel.collisionSurfacesBlock4Offset,
            xmodel.boneInfoBlock4Offset,
            xmodel.collisionTriangleCount,
            xmodel.collisionPayloadBytes,
            xmodel.boneInfoHash,
            xmodel.materialsTraversed ? 1 : 0,
            xmodel.collisionSurfacesTraversed ? 1 : 0,
            xmodel.boneInfoTraversed ? 1 : 0,
            xmodel.physPresetTraversed ? 1 : 0,
            xmodel.physGeomsTraversed ? 1 : 0,
            xmodel.published ? 1 : 0,
            0);
        SetRetailWorldXModelPhysPreset(
            0,
            xmodel.physPresetIdentity,
            xmodel.physPreset.name.c_str(),
            xmodel.physPreset.soundAliasPrefix.c_str(),
            xmodel.physPreset.type,
            xmodel.physPreset.mass,
            xmodel.physPreset.bounce,
            xmodel.physPreset.friction,
            xmodel.physPreset.bulletForceScale,
            xmodel.physPreset.explosiveForceScale,
            xmodel.physPreset.piecesSpreadFraction,
            xmodel.physPreset.piecesUpwardVelocity,
            xmodel.physPreset.tempDefaultToCylinder ? 1 : 0,
            xmodel.physPreset.nameReference,
            xmodel.physPreset.soundAliasPrefixReference,
            xmodel.physPreset.headerBlock0Offset,
            xmodel.physPreset.nameBlock4Offset,
            xmodel.physPreset.soundAliasPrefixBlock4Offset,
            xmodel.physPreset.insertPointerBlock4Offset,
            xmodel.physPreset.traversed ? 1 : 0,
            xmodel.physPreset.published ? 1 : 0);
        for (std::size_t index = 0u; index < xmodel.lods.size(); ++index)
        {
            const auto &lod = xmodel.lods[index];
            AppendRetailWorldXModelLod(
                static_cast<std::uint32_t>(index),
                lod.distance,
                lod.surfaceCount,
                lod.surfaceIndex,
                lod.partBits[0], lod.partBits[1],
                lod.partBits[2], lod.partBits[3],
                lod.lod,
                lod.smcIndexPlusOne,
                lod.smcAllocBits,
                0);
        }
        for (std::size_t index = 0u; index < xmodel.boneNames.size(); ++index)
        {
            const std::uint8_t classification =
                index < xmodel.partClassification.size()
                    ? xmodel.partClassification[index] : 0u;
            AppendRetailWorldXModelBone(
                static_cast<std::uint32_t>(index),
                xmodel.boneNameScriptStringIndices[index],
                xmodel.boneNames[index].c_str(),
                classification,
                0);
        }
        for (const auto &surface : xmodel.surfaces)
        {
            AppendRetailWorldXSurface(
                surface.index,
                surface.tileMode,
                surface.deformed ? 1 : 0,
                surface.vertCount,
                surface.triCount,
                surface.zoneHandle,
                surface.baseTriIndex,
                surface.baseVertIndex,
                surface.triIndicesReference,
                surface.vertsBlendReference,
                surface.vertsReference,
                surface.vertListCount,
                surface.vertListReference,
                surface.blendWordCount,
                surface.verticesBlock7Offset,
                surface.vertListsBlock4Offset,
                surface.indicesBlock8Offset,
                surface.verticesHash,
                surface.indicesHash,
                surface.dependenciesTraversed ? 1 : 0,
                0);
            for (std::size_t index = 0u;
                 index < surface.rigidVertLists.size(); ++index)
            {
                const auto &list = surface.rigidVertLists[index];
                const auto &tree = list.collisionTree;
                AppendRetailWorldXSurfaceRigidList(
                    surface.index,
                    static_cast<std::uint32_t>(index),
                    list.boneOffset,
                    list.vertCount,
                    list.triOffset,
                    list.triCount,
                    tree.reference,
                    tree.translation[0], tree.translation[1], tree.translation[2],
                    tree.scale[0], tree.scale[1], tree.scale[2],
                    tree.nodeCount,
                    tree.nodesReference,
                    tree.leafCount,
                    tree.leafsReference,
                    tree.nodesHash,
                    tree.leafsHash,
                    tree.traversed ? 1 : 0,
                    0);
            }
        }
        for (std::size_t index = 0u;
             index < xmodel.materialReferences.size(); ++index)
        {
            AppendRetailWorldXModelMaterialReference(
                static_cast<std::uint32_t>(index),
                xmodel.materialReferences[index],
                index < xmodel.materialIdentities.size()
                    ? xmodel.materialIdentities[index] : 0u,
                0);
        }
        for (std::size_t materialIndex = 0u;
             materialIndex < xmodel.materials.size(); ++materialIndex)
        {
            const auto &material = xmodel.materials[materialIndex];
            AppendRetailWorldXModelMaterial(
                material.handleIndex,
                material.name.c_str(),
                material.techniqueSetReference,
                material.techniqueSetIdentity,
                material.textureCount,
                material.constantCount,
                material.stateBitsCount,
                material.headerBlock0Offset,
                material.nameBlock4Offset,
                material.textureTableBlock4Offset,
                material.constantTableBlock4Offset,
                material.stateBitsTableBlock4Offset,
                material.constantsHash,
                material.stateBitsHash,
                material.identity,
                material.published ? 1 : 0,
                0);
            for (std::size_t textureIndex = 0u;
                 textureIndex < material.textures.size(); ++textureIndex)
            {
                const auto &texture = material.textures[textureIndex];
                AppendRetailWorldXModelMaterialTexture(
                    static_cast<std::uint32_t>(materialIndex),
                    static_cast<std::uint32_t>(textureIndex),
                    texture.nameHash,
                    texture.nameStart,
                    texture.nameEnd,
                    texture.samplerState,
                    texture.semantic,
                    texture.imageReference,
                    texture.imageIdentity,
                    texture.resolved ? 1 : 0,
                    0);
            }
            for (const auto &image : material.images)
            {
                AppendRetailWorldXModelImage(
                    static_cast<std::uint32_t>(materialIndex),
                    image.textureIndex,
                    image.name.c_str(),
                    image.mapType,
                    image.textureReference,
                    image.width,
                    image.height,
                    image.depth,
                    image.format,
                    image.resourceBytes,
                    image.headerBlock0Offset,
                    image.nameBlock4Offset,
                    image.textureInsertPointerBlock4Offset,
                    image.loadDefBlock0Offset,
                    image.identity,
                    image.loadDefTraversed ? 1 : 0,
                    image.published ? 1 : 0,
                    0);
            }
        }
        for (const auto &surface : xmodel.collisionSurfaces)
        {
            AppendRetailWorldXModelCollisionSurface(
                surface.index,
                surface.trianglesReference,
                surface.triangleCount,
                surface.mins[0], surface.mins[1], surface.mins[2],
                surface.maxs[0], surface.maxs[1], surface.maxs[2],
                surface.boneIndex,
                surface.contents,
                surface.surfaceFlags,
                surface.trianglesBlock4Offset,
                surface.trianglesHash,
                surface.traversed ? 1 : 0,
                0);
        }
        }
    }
    for (std::size_t xmodelIndex = 1u;
         xmodelIndex < world.worldXModels.size(); ++xmodelIndex)
    {
        const auto &secondXModel = world.worldXModels[xmodelIndex];
        if (secondXModel.headerTraversed)
        {
        BeginRetailWorldXModel(
            secondXModel.assetIndex,
            secondXModel.name.c_str(),
            secondXModel.numBones,
            secondXModel.numRootBones,
            secondXModel.surfaceCount,
            secondXModel.lodRampType,
            secondXModel.boneNamesReference,
            secondXModel.parentListReference,
            secondXModel.quatsReference,
            secondXModel.transReference,
            secondXModel.partClassificationReference,
            secondXModel.baseMatReference,
            secondXModel.surfacesReference,
            secondXModel.materialHandlesReference,
            secondXModel.collisionSurfacesReference,
            secondXModel.collisionSurfaceCount,
            secondXModel.contents,
            secondXModel.boneInfoReference,
            secondXModel.radius,
            secondXModel.mins[0], secondXModel.mins[1], secondXModel.mins[2],
            secondXModel.maxs[0], secondXModel.maxs[1], secondXModel.maxs[2],
            secondXModel.lodCount,
            secondXModel.collisionLod,
            secondXModel.memoryUsage,
            secondXModel.flags,
            secondXModel.bad ? 1 : 0,
            secondXModel.physPresetReference,
            secondXModel.physGeomsReference,
            secondXModel.headerBlock0Offset,
            secondXModel.nameBlock4Offset,
            secondXModel.boneNamesBlock4Offset,
            secondXModel.parentListBlock4Offset,
            secondXModel.quatsBlock4Offset,
            secondXModel.transBlock4Offset,
            secondXModel.partClassificationBlock4Offset,
            secondXModel.baseMatBlock4Offset,
            secondXModel.surfacesBlock4Offset,
            secondXModel.materialHandlesBlock4Offset,
            secondXModel.boundaryInflatedOffset,
            secondXModel.totalVertices,
            secondXModel.totalTriangles,
            secondXModel.totalRigidVertLists,
            secondXModel.totalCollisionNodes,
            secondXModel.totalCollisionLeaves,
            secondXModel.surfacePayloadBytes,
            secondXModel.headerTraversed ? 1 : 0,
            secondXModel.skeletonPrefixTraversed ? 1 : 0,
            secondXModel.surfaceHeadersTraversed ? 1 : 0,
            secondXModel.surfaceDependenciesTraversed ? 1 : 0,
            secondXModel.materialHandlesTraversed ? 1 : 0,
            secondXModel.stoppedBeforeSurfaceArray ? 1 : 0,
            secondXModel.stoppedBeforeMaterialDependency ? 1 : 0,
            world.unsupportedOperation ? world.unsupportedOperation : "",
            static_cast<int>(xmodelIndex));
        CompleteRetailWorldXModelDependencies(
            secondXModel.identity,
            secondXModel.collisionSurfacesBlock4Offset,
            secondXModel.boneInfoBlock4Offset,
            secondXModel.collisionTriangleCount,
            secondXModel.collisionPayloadBytes,
            secondXModel.boneInfoHash,
            secondXModel.materialsTraversed ? 1 : 0,
            secondXModel.collisionSurfacesTraversed ? 1 : 0,
            secondXModel.boneInfoTraversed ? 1 : 0,
            secondXModel.physPresetTraversed ? 1 : 0,
            secondXModel.physGeomsTraversed ? 1 : 0,
            secondXModel.published ? 1 : 0,
            static_cast<int>(xmodelIndex));
        SetRetailWorldXModelPhysPreset(
            static_cast<int>(xmodelIndex),
            secondXModel.physPresetIdentity,
            secondXModel.physPreset.name.c_str(),
            secondXModel.physPreset.soundAliasPrefix.c_str(),
            secondXModel.physPreset.type,
            secondXModel.physPreset.mass,
            secondXModel.physPreset.bounce,
            secondXModel.physPreset.friction,
            secondXModel.physPreset.bulletForceScale,
            secondXModel.physPreset.explosiveForceScale,
            secondXModel.physPreset.piecesSpreadFraction,
            secondXModel.physPreset.piecesUpwardVelocity,
            secondXModel.physPreset.tempDefaultToCylinder ? 1 : 0,
            secondXModel.physPreset.nameReference,
            secondXModel.physPreset.soundAliasPrefixReference,
            secondXModel.physPreset.headerBlock0Offset,
            secondXModel.physPreset.nameBlock4Offset,
            secondXModel.physPreset.soundAliasPrefixBlock4Offset,
            secondXModel.physPreset.insertPointerBlock4Offset,
            secondXModel.physPreset.traversed ? 1 : 0,
            secondXModel.physPreset.published ? 1 : 0);
        for (std::size_t index = 0u; index < secondXModel.lods.size(); ++index)
        {
            const auto &lod = secondXModel.lods[index];
            AppendRetailWorldXModelLod(
                static_cast<std::uint32_t>(index),
                lod.distance,
                lod.surfaceCount,
                lod.surfaceIndex,
                lod.partBits[0], lod.partBits[1],
                lod.partBits[2], lod.partBits[3],
                lod.lod,
                lod.smcIndexPlusOne,
                lod.smcAllocBits,
                static_cast<int>(xmodelIndex));
        }
        for (std::size_t index = 0u;
             index < secondXModel.boneNames.size(); ++index)
        {
            const std::uint8_t classification =
                index < secondXModel.partClassification.size()
                    ? secondXModel.partClassification[index] : 0u;
            AppendRetailWorldXModelBone(
                static_cast<std::uint32_t>(index),
                secondXModel.boneNameScriptStringIndices[index],
                secondXModel.boneNames[index].c_str(),
                classification,
                static_cast<int>(xmodelIndex));
        }
        for (const auto &surface : secondXModel.surfaces)
        {
            AppendRetailWorldXSurface(
                surface.index,
                surface.tileMode,
                surface.deformed ? 1 : 0,
                surface.vertCount,
                surface.triCount,
                surface.zoneHandle,
                surface.baseTriIndex,
                surface.baseVertIndex,
                surface.triIndicesReference,
                surface.vertsBlendReference,
                surface.vertsReference,
                surface.vertListCount,
                surface.vertListReference,
                surface.blendWordCount,
                surface.verticesBlock7Offset,
                surface.vertListsBlock4Offset,
                surface.indicesBlock8Offset,
                surface.verticesHash,
                surface.indicesHash,
                surface.dependenciesTraversed ? 1 : 0,
                static_cast<int>(xmodelIndex));
            for (std::size_t index = 0u;
                 index < surface.rigidVertLists.size(); ++index)
            {
                const auto &list = surface.rigidVertLists[index];
                const auto &tree = list.collisionTree;
                AppendRetailWorldXSurfaceRigidList(
                    surface.index,
                    static_cast<std::uint32_t>(index),
                    list.boneOffset,
                    list.vertCount,
                    list.triOffset,
                    list.triCount,
                    tree.reference,
                    tree.translation[0], tree.translation[1], tree.translation[2],
                    tree.scale[0], tree.scale[1], tree.scale[2],
                    tree.nodeCount,
                    tree.nodesReference,
                    tree.leafCount,
                    tree.leafsReference,
                    tree.nodesHash,
                    tree.leafsHash,
                    tree.traversed ? 1 : 0,
                    static_cast<int>(xmodelIndex));
            }
        }
        for (std::size_t index = 0u;
             index < secondXModel.materialReferences.size(); ++index)
        {
            AppendRetailWorldXModelMaterialReference(
                static_cast<std::uint32_t>(index),
                secondXModel.materialReferences[index],
                index < secondXModel.materialIdentities.size()
                    ? secondXModel.materialIdentities[index] : 0u,
                static_cast<int>(xmodelIndex));
        }
        for (std::size_t materialIndex = 0u;
             materialIndex < secondXModel.materials.size(); ++materialIndex)
        {
            const auto &material = secondXModel.materials[materialIndex];
            AppendRetailWorldXModelMaterial(
                material.handleIndex,
                material.name.c_str(),
                material.techniqueSetReference,
                material.techniqueSetIdentity,
                material.textureCount,
                material.constantCount,
                material.stateBitsCount,
                material.headerBlock0Offset,
                material.nameBlock4Offset,
                material.textureTableBlock4Offset,
                material.constantTableBlock4Offset,
                material.stateBitsTableBlock4Offset,
                material.constantsHash,
                material.stateBitsHash,
                material.identity,
                material.published ? 1 : 0,
                static_cast<int>(xmodelIndex));
            for (std::size_t textureIndex = 0u;
                 textureIndex < material.textures.size(); ++textureIndex)
            {
                const auto &texture = material.textures[textureIndex];
                AppendRetailWorldXModelMaterialTexture(
                    static_cast<std::uint32_t>(materialIndex),
                    static_cast<std::uint32_t>(textureIndex),
                    texture.nameHash,
                    texture.nameStart,
                    texture.nameEnd,
                    texture.samplerState,
                    texture.semantic,
                    texture.imageReference,
                    texture.imageIdentity,
                    texture.resolved ? 1 : 0,
                    static_cast<int>(xmodelIndex));
            }
            for (const auto &image : material.images)
            {
                AppendRetailWorldXModelImage(
                    static_cast<std::uint32_t>(materialIndex),
                    image.textureIndex,
                    image.name.c_str(),
                    image.mapType,
                    image.textureReference,
                    image.width,
                    image.height,
                    image.depth,
                    image.format,
                    image.resourceBytes,
                    image.headerBlock0Offset,
                    image.nameBlock4Offset,
                    image.textureInsertPointerBlock4Offset,
                    image.loadDefBlock0Offset,
                    image.identity,
                    image.loadDefTraversed ? 1 : 0,
                    image.published ? 1 : 0,
                    static_cast<int>(xmodelIndex));
            }
        }
        for (const auto &surface : secondXModel.collisionSurfaces)
        {
            AppendRetailWorldXModelCollisionSurface(
                surface.index,
                surface.trianglesReference,
                surface.triangleCount,
                surface.mins[0], surface.mins[1], surface.mins[2],
                surface.maxs[0], surface.maxs[1], surface.maxs[2],
                surface.boneIndex,
                surface.contents,
                surface.surfaceFlags,
                surface.trianglesBlock4Offset,
                surface.trianglesHash,
                surface.traversed ? 1 : 0,
                static_cast<int>(xmodelIndex));
        }
        }
    }
    for (std::uint32_t type = 0u; type < world.typeCounts.size(); ++type)
    {
        if (world.typeCounts[type] != 0u ||
            world.typesBeforeFirstGfxWorld[type] != 0u)
        {
            AppendRetailWorldInventoryType(
                type,
                kisak::fastfile::RetailAssetTypeName(type),
                world.typeCounts[type],
                world.typesBeforeFirstGfxWorld[type]);
        }
    }
    for (std::uint32_t block = 0u; block < result.blockSizes.size(); ++block)
        AppendRetailCensusBlock(block, result.blockSizes[block]);
    for (std::uint32_t type = 0u; type < result.typeCounts.size(); ++type)
    {
        if (result.typeCounts[type] != 0u)
            AppendRetailCensusType(
                type, kisak::fastfile::RetailAssetTypeName(type), result.typeCounts[type]);
    }
    EndRetailCensusReady();
    Web_Log(
        WebLogLevel::Info,
        "[kisakcod-web] Retail census found %u startup assets and %u map assets; first GfxWorld is table index %u, and material %s resolved %s.\n",
        result.assetCount,
        world.assetCount,
        world.firstGfxWorldAssetIndex,
        result.materialName.c_str(),
        result.imagePath.c_str());
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_StartRetailCensus()
{
    WebRetailCensusJob_Start();
}

extern "C" EMSCRIPTEN_KEEPALIVE void KisakWeb_CancelRetailCensus()
{
    WebRetailCensusJob_Cancel();
}

void WebRetailCensusJob_Start()
{
    DiscardRetailCensusReady();
    (void)WebRenderer_ClearShaderCompatibility();
    if (g_runtime.requestId != 0u) (void)WebFs_Cancel(g_runtime.requestId);
    const std::uint32_t generation = g_runtime.generation == UINT32_MAX
        ? 1u : g_runtime.generation + 1u;
    Reset(false);
    g_runtime.generation = generation;
    if (const auto error = g_runtime.parser.BeginStreaming();
        error != kisak::fastfile::RetailCensusError::None)
    {
        Fail("could not start retail census", kisak::fastfile::RetailCensusErrorString(error));
        return;
    }
    g_runtime.phase = Phase::NeedStat;
    DispatchRetailCensusLoading(
        generation, "stat", CurrentPath(), CurrentTraversal());
}

void WebRetailCensusJob_Cancel()
{
    DiscardRetailCensusReady();
    (void)WebRenderer_ClearShaderCompatibility();
    if (g_runtime.requestId != 0u) (void)WebFs_Cancel(g_runtime.requestId);
    const std::uint32_t generation = g_runtime.generation == UINT32_MAX
        ? 1u : g_runtime.generation + 1u;
    Reset(false);
    g_runtime.generation = generation;
    DispatchRetailCensusIdle(generation);
}

WebRetailCensusFrameResult WebRetailCensusJob_Frame()
{
    using namespace kisak::fastfile;
    switch (g_runtime.phase)
    {
    case Phase::Idle:
    case Phase::Finished:
    case Phase::Failed:
        return {};
    case Phase::NeedStat:
    {
        const WebFsStatus status = WebFs_BeginStat(
            CurrentPath(), CompleteRequest,
            reinterpret_cast<void *>(static_cast<std::uintptr_t>(g_runtime.generation)),
            &g_runtime.requestId);
        if (status != WebFsStatus::Pending) Fail("could not stat retail fastfile", WebFsStatusString(status));
        else g_runtime.phase = Phase::WaitingStat;
        return {};
    }
    case Phase::WaitingStat:
    {
        if (!g_runtime.completionReady) return {};
        g_runtime.completionReady = false;
        if (g_runtime.completionStatus != WebFsStatus::Success)
        {
            Fail("could not stat retail fastfile", WebFsStatusString(g_runtime.completionStatus));
            return {};
        }
        const std::uint32_t maximumFileSize = g_runtime.dataset == Dataset::CodePostGfx
            ? 16u * 1024u * 1024u : 128u * 1024u * 1024u;
        if (g_runtime.fileSize < 14u || g_runtime.fileSize > maximumFileSize)
        {
            Fail("could not census retail fastfile", "file size is outside the bounded census envelope");
            return {};
        }
        if (g_runtime.dataset == Dataset::WorldInventory &&
            g_runtime.fileSize < MIN_COMPLETE_WORLD_FASTFILE_BYTES &&
            !g_runtime.parser.ConfigureGfxWorldLoading(false))
        {
            Fail("could not configure world traversal",
                "GfxWorld policy changed after source traversal began");
            return {};
        }
        g_runtime.phase = Phase::NeedRead;
        return {};
    }
    case Phase::NeedRead:
    {
        if (g_runtime.readOffset >= g_runtime.fileSize)
        {
            Fail("could not census retail fastfile", "compressed prefix ended before the asset table");
            return {};
        }
        const std::uint32_t length = std::min<std::uint32_t>(
            WEB_FS_MAX_READ_SIZE, g_runtime.fileSize - g_runtime.readOffset);
        const WebFsStatus status = WebFs_BeginRead(
            CurrentPath(), g_runtime.readOffset, length, CompleteRequest,
            reinterpret_cast<void *>(static_cast<std::uintptr_t>(g_runtime.generation)),
            &g_runtime.requestId);
        if (status != WebFsStatus::Pending) Fail("could not read retail fastfile", WebFsStatusString(status));
        else g_runtime.phase = Phase::WaitingRead;
        return {};
    }
    case Phase::WaitingRead:
        if (!g_runtime.completionReady) return {};
        g_runtime.completionReady = false;
        if (g_runtime.completionStatus != WebFsStatus::Success)
        {
            Fail("could not read retail fastfile", WebFsStatusString(g_runtime.completionStatus));
            return {};
        }
        if (g_runtime.completionBytes.empty())
        {
            Fail("could not read retail fastfile", "filesystem returned an empty bounded read");
            return {};
        }
        {
            const bool final = g_runtime.readOffset + g_runtime.completionBytes.size() == g_runtime.fileSize;
            const auto error = g_runtime.parser.FeedSource(g_runtime.completionBytes, final);
            if (error != RetailCensusError::None)
            {
                Fail("could not feed retail fastfile", RetailCensusErrorString(error));
                return {};
            }
            g_runtime.readOffset += static_cast<std::uint32_t>(g_runtime.completionBytes.size());
            g_runtime.completionBytes.clear();
            g_runtime.phase = Phase::Parse;
        }
        return {};
    case Phase::Parse:
    {
        const RetailCensusStepReport report = g_runtime.parser.Step();
        const std::uint32_t bytesUsed = std::max({
            report.sourceBytesConsumed,
            report.inflatedBytesProduced,
            report.traversedBytes});
        if (report.progress == RetailCensusProgress::Failed)
        {
            Fail("could not traverse retail fastfile prefix", RetailCensusErrorString(report.error));
        }
        else if (report.progress == RetailCensusProgress::Succeeded)
        {
            RetailFastfileCensus completed;
            if (!g_runtime.parser.TakeResult(completed))
            {
                Fail("could not publish retail fastfile census", "completed result was unavailable");
            }
            else if (g_runtime.dataset == Dataset::CodePostGfx)
            {
                g_runtime.result = std::move(completed);
                g_runtime.codePostFileSize = g_runtime.fileSize;
                g_runtime.codePostSourceBytesRead = g_runtime.readOffset;
                g_runtime.dataset = Dataset::CommonPrerequisite;
                g_runtime.fileSize = 0u;
                g_runtime.readOffset = 0u;
                g_runtime.completionReady = false;
                g_runtime.completionStatus = WebFsStatus::Pending;
                g_runtime.completionBytes.clear();
                RetailCensusLimits limits;
                limits.maxFileBytes = 128u * 1024u * 1024u;
                limits.maxInflatedPrefixBytes = 128u * 1024u * 1024u;
                if (const auto error = g_runtime.parser.BeginStreaming(
                        RetailCensusMode::PrerequisiteZone, limits);
                    error != RetailCensusError::None)
                {
                    Fail("could not start prerequisite-zone traversal",
                        RetailCensusErrorString(error));
                }
                else
                {
                    g_runtime.phase = Phase::NeedStat;
                    DispatchRetailCensusLoading(
                        g_runtime.generation, "common-stat", CurrentPath(), CurrentTraversal());
                }
            }
            else if (g_runtime.dataset == Dataset::CommonPrerequisite)
            {
                try
                {
                    g_runtime.commonPrerequisite =
                        std::make_shared<RetailFastfileCensus>(
                            std::move(completed));
                }
                catch (...)
                {
                    Fail("could not retain prerequisite zone", "allocation failed");
                    return {bytesUsed, report.recordsProcessed};
                }
                g_runtime.soundCatalog.Reset();
                for (const RetailPublishedSoundAliasList &entry :
                     g_runtime.commonPrerequisite->worldSoundAliasLists)
                {
                    if (!entry.published || entry.pointerAlias ||
                        entry.databaseAlias ||
                        !entry.asset || !entry.storage ||
                        !entry.storage->aliasName)
                    {
                        continue;
                    }
                    const RetailSoundAliasCatalogError publishError =
                        g_runtime.soundCatalog.Publish(
                            *entry.storage->aliasName,
                            entry.asset.get(),
                            g_runtime.commonPrerequisite);
                    if (publishError != RetailSoundAliasCatalogError::None)
                    {
                        Fail("could not index common-zone sound asset",
                            RetailSoundAliasCatalogErrorString(publishError));
                        return {bytesUsed, report.recordsProcessed};
                    }
                }
                g_runtime.dataset = Dataset::WorldInventory;
                g_runtime.fileSize = 0u;
                g_runtime.readOffset = 0u;
                g_runtime.completionReady = false;
                g_runtime.completionStatus = WebFsStatus::Pending;
                g_runtime.completionBytes.clear();
                RetailCensusLimits limits;
                limits.maxFileBytes = 128u * 1024u * 1024u;
                limits.maxInflatedPrefixBytes = 128u * 1024u * 1024u;
                limits.maxImageResourceBytes = 64u * 1024u * 1024u;
                limits.loadGfxWorld = true;
                if (const auto error = g_runtime.parser.BeginStreaming(
                        RetailCensusMode::WorldAssetLoader, limits,
                        g_runtime.soundCatalog.Lookup());
                    error != RetailCensusError::None)
                {
                    Fail("could not start world asset inventory",
                        RetailCensusErrorString(error));
                }
                else
                {
                    g_runtime.phase = Phase::NeedStat;
                    DispatchRetailCensusLoading(
                        g_runtime.generation, "world-stat", CurrentPath(), CurrentTraversal());
                }
            }
            else
            {
                g_runtime.worldInventory = std::move(completed);
                g_runtime.worldFileSize = g_runtime.fileSize;
                g_runtime.worldSourceBytesRead = g_runtime.readOffset;
                g_runtime.phase = Phase::Finished;
                PublishReady();
            }
        }
        else if (report.needsSource)
        {
            g_runtime.phase = Phase::NeedRead;
        }
        return {bytesUsed, report.recordsProcessed};
    }
    }
    return {};
}
