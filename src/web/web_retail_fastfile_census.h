#pragma once

#include <EffectsCore/fx_types.h>
#include <database/db_semantic_trace.h>
#include <database/localize_types.h>
#include <bgame/weapon_types.h>
#include <gfx_d3d/gfx_light_types.h>
#include <gfx_d3d/gfx_world_types.h>
#include <gfx_d3d/material_types.h>
#include <qcommon/com_world_types.h>
#include <sound/snd_alias_types.h>
#include <web/web_sound_alias_catalog.h>
#include <xanim/xanim_types.h>
#include <xanim/xmodel_types.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kisak::fastfile
{

struct CanonicalClipMapStorage;
struct CanonicalComWorldStorage;
struct CanonicalLightDefStorage;
struct CanonicalGfxWorldStorage;

inline constexpr std::uint32_t RETAIL_CENSUS_ASSET_TYPE_COUNT = 33u;
inline constexpr std::uint32_t RETAIL_CENSUS_MAX_STEP_BYTES = 64u * 1024u;
inline constexpr std::uint32_t RETAIL_CENSUS_MAX_STEP_RECORDS = 64u;

struct RetailCensusLimits
{
    // Gate 2 is opt-in while callers that intentionally stop at the first
    // GfxWorld body retain their historical boundary contract.
    bool loadGfxWorld = false;
    std::uint32_t maxFileBytes = 16u * 1024u * 1024u;
    std::uint32_t maxSourceChunkBytes = RETAIL_CENSUS_MAX_STEP_BYTES;
    // Retain enough checked prefix to cross large retail dependency runs while
    // remaining well below the declared aggregate zone-stream ceiling.
    std::uint32_t maxInflatedPrefixBytes = 64u * 1024u * 1024u;
    std::uint32_t maxBlockBytes = 512u * 1024u * 1024u;
    std::uint64_t maxTotalBlockBytes = 1536ull * 1024ull * 1024ull;
    std::uint32_t maxScriptStrings = 4096u;
    std::uint32_t maxScriptStringBytes = 4096u;
    std::uint32_t maxTotalScriptStringBytes = 256u * 1024u;
    std::uint32_t maxAssets = 16384u;
    std::uint32_t maxRegistryAssets = 16384u;
    std::uint32_t maxRegistryAliases = 65536u;
    std::uint32_t maxRegistryNameBytes = 8u * 1024u * 1024u;
    std::uint32_t maxTechniqueNameBytes = 255u;
    std::uint32_t maxTechniquePasses = 16u;
    std::uint32_t maxShaderNameBytes = 255u;
    std::uint32_t maxShaderProgramDwords = 16384u;
    std::uint32_t maxMaterialNameBytes = 255u;
    std::uint32_t maxImageNameBytes = 255u;
    std::uint32_t maxMaterialTextures = 8u;
    std::uint32_t maxImageResourceBytes = 4u * 1024u * 1024u;
    std::uint32_t maxMaterialConstants = 64u;
    std::uint32_t maxMaterialStateBits = 64u;
    std::uint32_t maxXModelNameBytes = 255u;
    std::uint32_t maxPhysPresetNameBytes = 255u;
    std::uint32_t maxPhysPresetSoundAliasBytes = 255u;
    std::uint32_t maxWorldXModels = 4096u;
    std::uint32_t maxXModelCollisionSurfaces = 4096u;
    std::uint32_t maxXModelSurfaceVertices = 1024u * 1024u;
    std::uint32_t maxXModelSurfaceTriangles = 1024u * 1024u;
    std::uint32_t maxXModelRigidVertLists = 4096u;
    std::uint32_t maxXModelCollisionNodes = 1024u * 1024u;
    std::uint32_t maxXModelCollisionLeaves = 1024u * 1024u;
    std::uint32_t maxXModelSurfacePayloadBytes = 64u * 1024u * 1024u;
    std::uint32_t maxRetainedXModelRendererBytes = 16u * 1024u * 1024u;
    std::uint32_t maxXModelCollisionTriangles = 1024u * 1024u;
    std::uint32_t maxXModelCollisionPayloadBytes = 64u * 1024u * 1024u;
    std::uint32_t maxXModelPhysGeoms = 4096u;
    std::uint32_t maxPhysGeomBrushSides = 65536u;
    std::uint32_t maxPhysGeomBrushEdges = 1024u * 1024u;
    std::uint32_t maxPhysGeomPayloadBytes = 64u * 1024u * 1024u;
    std::uint32_t maxFxEffects = 4096u;
    std::uint32_t maxFxElemDefs = 65536u;
    std::uint32_t maxFxVisuals = 65536u;
    std::uint32_t maxFxSampleBytes = 64u * 1024u * 1024u;
    std::uint32_t maxFxTrailVertices = 65536u;
    std::uint32_t maxFxTrailIndices = 131072u;
    std::uint32_t maxRawFiles = 4096u;
    std::uint32_t maxRawFileNameBytes = 255u;
    std::uint32_t maxRawFileBytes = 8u * 1024u * 1024u;
    std::uint32_t maxRetainedRawFileBytes = 32u * 1024u * 1024u;
    std::uint32_t maxXAnimParts = 4096u;
    std::uint32_t maxXAnimNameBytes = 255u;
    std::uint32_t maxXAnimIndices = 16u * 1024u * 1024u;
    std::uint32_t maxXAnimPayloadBytes = 64u * 1024u * 1024u;
    std::uint32_t maxRetainedXAnimBytes = 128u * 1024u * 1024u;
    std::uint32_t maxWeapons = 4096u;
    std::uint32_t maxWeaponStringBytes = 4096u;
    std::uint32_t maxWeaponAccuracyKnots = 65536u;
    std::uint32_t maxWeaponPayloadBytes = 4u * 1024u * 1024u;
    std::uint32_t maxRetainedWeaponBytes = 128u * 1024u * 1024u;
    std::uint32_t maxLocalizeEntries = 8192u;
    std::uint32_t maxLocalizeStringBytes = 64u * 1024u;
    std::uint32_t maxRetainedLocalizeBytes = 32u * 1024u * 1024u;
    std::uint32_t maxSoundAliasLists = 16384u;
    std::uint32_t maxSoundAliasesPerList = 4096u;
    std::uint32_t maxSoundStringBytes = 4096u;
    std::uint32_t maxRetainedSoundBytes = 128u * 1024u * 1024u;
    std::uint32_t maxClipMaps = 8u;
    std::uint32_t maxClipMapNameBytes = 255u;
    std::uint32_t maxClipMapArrayElements = 16u * 1024u * 1024u;
    std::uint32_t maxClipMapPayloadBytes = 512u * 1024u * 1024u;
    std::uint32_t maxRetainedClipMapBytes = 512u * 1024u * 1024u;
    std::uint32_t maxComWorlds = 8u;
    std::uint32_t maxComWorldNameBytes = 255u;
    std::uint32_t maxComWorldPrimaryLights = 4096u;
    std::uint32_t maxComWorldLightDefNameBytes = 255u;
    std::uint32_t maxComWorldStringBytes = 1024u * 1024u;
    std::uint32_t maxComWorldPayloadBytes = 2u * 1024u * 1024u;
    std::uint32_t maxLightDefs = 256u;
    std::uint32_t maxLightDefNameBytes = 255u;
    std::uint32_t maxGfxWorlds = 4u;
    std::uint32_t maxGfxWorldNameBytes = 1024u;
    std::uint32_t maxGfxWorldArrayElements = 16u * 1024u * 1024u;
    std::uint32_t maxGfxWorldVertices = 4u * 1024u * 1024u;
    std::uint32_t maxGfxWorldIndices = 16u * 1024u * 1024u;
    std::uint32_t maxGfxWorldSurfaces = 2u * 1024u * 1024u;
    std::uint32_t maxGfxWorldCells = 65536u;
    std::uint32_t maxGfxWorldLightmaps = 65536u;
    std::uint32_t maxGfxWorldStaticModels = 2u * 1024u * 1024u;
    std::uint32_t maxGfxWorldMaterialMemory = 2u * 1024u * 1024u;
    std::uint32_t maxGfxWorldPayloadBytes = 768u * 1024u * 1024u;
    std::uint32_t maxRetainedGfxWorldBytes = 768u * 1024u * 1024u;
    std::uint32_t maxSemanticTraceEntries = 65536u;
};

enum class RetailCensusError : std::uint8_t
{
    None = 0,
    InvalidArgument,
    InvalidStepBudget,
    SourceChunkTooLarge,
    SourceBackpressure,
    SourceAlreadyFinal,
    FileTooLarge,
    PrefixTruncated,
    InvalidMagic,
    AuthenticatedUnsupported,
    UnsupportedVersion,
    InflateInit,
    InflateData,
    InflateTruncated,
    InflatedPrefixLimit,
    RecordTruncated,
    BlockSizeLimit,
    TotalBlockSizeLimit,
    ScriptStringCountInvalid,
    ScriptStringCountLimit,
    ScriptStringArrayInvalid,
    ScriptStringReferenceUnsupported,
    ScriptStringTooLong,
    ScriptStringBytesLimit,
    AssetCountInvalid,
    AssetCountLimit,
    AssetArrayInvalid,
    AssetTypeInvalid,
    ZoneStreamInvalid,
    ZoneBlockOverflow,
    FirstAssetUnsupported,
    AssetPrefixUnsupported,
    AssetRegistryInvalid,
    TechniqueSetLayoutUnsupported,
    TechniqueSetNameInvalid,
    TechniqueSetNameTooLong,
    TechniqueReferenceUnsupported,
    TechniqueLayoutUnsupported,
    TechniquePassCountLimit,
    MaterialPassUnsupported,
    VertexDeclarationUnsupported,
    VertexShaderLayoutUnsupported,
    VertexShaderNameInvalid,
    VertexShaderNameTooLong,
    ShaderProgramSizeInvalid,
    ShaderProgramSizeLimit,
    ShaderProgramSignatureInvalid,
    PixelShaderLayoutUnsupported,
    ShaderContractInvalid,
    ShaderSubstitutionUnsupported,
    ShaderArgumentLayoutUnsupported,
    TechniqueNameInvalid,
    TechniqueAliasInvalid,
    MaterialLayoutUnsupported,
    MaterialNameInvalid,
    MaterialNameTooLong,
    MaterialTechniqueSetInvalid,
    MaterialTextureCountLimit,
    MaterialTextureLayoutUnsupported,
    ImageLayoutUnsupported,
    ImageNameInvalid,
    ImageNameTooLong,
    ImageResourceSizeInvalid,
    ImageResourceSizeLimit,
    MaterialStateBitsUnsupported,
    GfxWorldMissing,
    XModelLayoutUnsupported,
    XModelNameInvalid,
    XModelNameTooLong,
    XModelCountInvalid,
    XModelCollectionLimit,
    XModelBoundsInvalid,
    XModelScriptStringInvalid,
    XModelScriptStringAliasInvalid,
    XModelArrayAliasInvalid,
    XModelDependencyUnsupported,
    XSurfaceLayoutUnsupported,
    XSurfaceCountInvalid,
    XSurfacePayloadLimit,
    XSurfaceCollisionInvalid,
    XModelMaterialAliasInvalid,
    XModelImageAliasInvalid,
    XModelCollisionInvalid,
    XModelCollisionPayloadLimit,
    XModelBoneInfoInvalid,
    XModelPhysicsUnsupported,
    PhysPresetLayoutUnsupported,
    PhysPresetNameInvalid,
    PhysPresetNameTooLong,
    PhysPresetSoundAliasInvalid,
    PhysPresetSoundAliasTooLong,
    PhysPresetValuesInvalid,
    PhysPresetAliasInvalid,
    PhysGeomLayoutUnsupported,
    PhysGeomCountLimit,
    PhysGeomValuesInvalid,
    PhysGeomBrushInvalid,
    PhysGeomPayloadLimit,
    FxEffectLayoutUnsupported,
    FxEffectNameInvalid,
    FxEffectNameTooLong,
    FxEffectCountLimit,
    FxElemLayoutUnsupported,
    FxElemSampleLimit,
    FxElemVisualInvalid,
    FxStringReferenceInvalid,
    FxTrailInvalid,
    FxMaterialUnsupported,
    RawFileLayoutUnsupported,
    RawFileNameInvalid,
    RawFileNameTooLong,
    RawFileSizeInvalid,
    RawFilePayloadLimit,
    RawFileCollectionLimit,
    XAnimLayoutUnsupported,
    XAnimNameInvalid,
    XAnimNameTooLong,
    XAnimCollectionLimit,
    XAnimScriptStringInvalid,
    XAnimPayloadLimit,
    XAnimDeltaInvalid,
    XAnimAliasInvalid,
    WeaponLayoutUnsupported,
    WeaponNameInvalid,
    WeaponStringInvalid,
    WeaponStringTooLong,
    WeaponCollectionLimit,
    WeaponScriptStringInvalid,
    WeaponDependencyUnsupported,
    WeaponSoundNameInvalid,
    WeaponSoundNameTooLong,
    WeaponSoundLookupFailed,
    WeaponAccuracyInvalid,
    WeaponPayloadLimit,
    WeaponAliasInvalid,
    LocalizeLayoutUnsupported,
    LocalizeCollectionLimit,
    LocalizeStringInvalid,
    LocalizeStringTooLong,
    LocalizePayloadLimit,
    LocalizeAliasInvalid,
    SoundAliasLayoutUnsupported,
    SoundAliasCollectionLimit,
    SoundAliasCountLimit,
    SoundAliasStringInvalid,
    SoundAliasStringTooLong,
    SoundAliasDependencyUnsupported,
    SoundAliasPayloadLimit,
    SoundAliasCatalogPublishFailed,
    ClipMapLayoutUnsupported,
    ClipMapCollectionLimit,
    ClipMapNameInvalid,
    ClipMapNameTooLong,
    ClipMapCountInvalid,
    ClipMapPayloadLimit,
    ClipMapPointerInvalid,
    ClipMapDependencyUnsupported,
    ClipMapAliasInvalid,
    ComWorldLayoutUnsupported,
    ComWorldCollectionLimit,
    ComWorldNameInvalid,
    ComWorldNameTooLong,
    ComWorldLightCountInvalid,
    ComWorldLightNameInvalid,
    ComWorldLightNameTooLong,
    ComWorldStringBytesLimit,
    ComWorldPayloadLimit,
    ComWorldAliasInvalid,
    LightDefLayoutUnsupported,
    LightDefCollectionLimit,
    LightDefNameInvalid,
    LightDefNameTooLong,
    LightDefImageInvalid,
    LightDefAliasInvalid,
    GfxWorldLayoutUnsupported,
    GfxWorldCollectionLimit,
    GfxWorldNameInvalid,
    GfxWorldNameTooLong,
    GfxWorldCountInvalid,
    GfxWorldPayloadLimit,
    GfxWorldPointerInvalid,
    GfxWorldImageInvalid,
    GfxWorldMaterialInvalid,
    GfxWorldModelInvalid,
    GfxWorldLightDefInvalid,
    GfxWorldCellInvalid,
    GfxWorldSurfaceInvalid,
    GfxWorldAliasInvalid,
    PostXModelAssetUnsupported,
    SemanticTraceLimit,
    AllocationFailed,
};

const char *RetailCensusErrorString(RetailCensusError error) noexcept;
const char *RetailAssetTypeName(std::uint32_t type) noexcept;

enum class RetailCensusProgress : std::uint8_t
{
    NotStarted = 0,
    Running,
    Succeeded,
    Failed,
};

enum class RetailCensusMode : std::uint8_t
{
    CodePostGfxMaterial = 0,
    WorldAssetInventory,
    WorldTechniqueSetPrefix,
    WorldFirstTechniqueSet = WorldTechniqueSetPrefix,
    WorldXModelPrefix,
    WorldXSurfacePrefix,
    WorldXModelDependencies,
    WorldPostXModelTechniqueSet,
    WorldSecondXModelPrefix,
    WorldSecondXSurfacePrefix,
    WorldSecondXModelDependencies,
    // Runs the reusable bounded XModel loader whenever the supported top-level
    // dispatcher encounters an inline XModel. Consecutive and separated model
    // runs share the same operation; compatible technique-set bodies may occur
    // between them.
    WorldXModelLoader,
    // The reusable dispatcher now also traverses supported FxEffectDef bodies
    // and their nested material/XModel dependencies.
    WorldAssetLoader = WorldXModelLoader,
    WorldXModelCollection = WorldXModelLoader,
    PrerequisiteZone,
};

enum class RetailCensusStage : std::uint8_t
{
    NotStarted = 0,
    Prefix,
    Inflate,
    XFile,
    AssetList,
    ScriptStringPointers,
    ScriptStrings,
    AssetTable,
    TechniqueSet,
    TechniqueSetName,
    Technique,
    MaterialPasses,
    VertexDeclaration,
    VertexShader,
    VertexShaderName,
    VertexShaderProgram,
    PixelShader,
    PixelShaderProgram,
    ShaderArguments,
    TechniqueName,
    SecondTechniqueSet,
    SecondTechniqueSetName,
    SecondTechnique,
    SecondMaterialPasses,
    SecondVertexShader,
    SecondVertexShaderProgram,
    SecondPixelShader,
    SecondPixelShaderProgram,
    SecondShaderArguments,
    SecondTechniqueName,
    Material,
    MaterialName,
    MaterialTextureTable,
    Image,
    ImageName,
    ImageLoadDef,
    ImageResource,
    MaterialStateBits,
    WorldTechniqueSet,
    WorldTechniqueSetName,
    WorldFirstTechniqueSet = WorldTechniqueSet,
    WorldFirstTechniqueSetName = WorldTechniqueSetName,
    WorldXModel,
    WorldXModelName,
    WorldXModelBoneNames,
    WorldXModelParentList,
    WorldXModelQuats,
    WorldXModelTrans,
    WorldXModelPartClassification,
    WorldXModelBaseMat,
    WorldXModelSurfaceHeaders,
    WorldXModelSurfaceBlendInfo,
    WorldXModelSurfaceVertices,
    WorldXModelSurfaceVertLists,
    WorldXModelSurfaceCollisionTree,
    WorldXModelSurfaceCollisionNodes,
    WorldXModelSurfaceCollisionLeaves,
    WorldXModelSurfaceIndices,
    WorldXModelMaterialHandles,
    WorldXModelMaterial,
    WorldXModelMaterialName,
    WorldXModelMaterialTextures,
    WorldXModelImage,
    WorldXModelImageName,
    WorldXModelImageLoadDef,
    WorldXModelImageResource,
    WorldXModelMaterialConstants,
    WorldXModelMaterialStateBits,
    WorldXModelCollisionSurfaces,
    WorldXModelCollisionTriangles,
    WorldXModelBoneInfo,
    WorldXModelPhysPreset,
    WorldPhysPreset,
    WorldPhysPresetName,
    WorldPhysPresetSoundAlias,
    WorldXModelPhysGeoms,
    WorldPhysGeomList,
    WorldPhysGeomInfos,
    WorldPhysGeomBrush,
    WorldPhysGeomBrushSides,
    WorldPhysGeomBrushSidePlane,
    WorldPhysGeomBrushAdjacent,
    WorldPhysGeomBrushPlanes,
    WorldXModelPublish,
    WorldMaterialTechnique,
    WorldMaterialPasses,
    WorldMaterialVertexDeclaration,
    WorldMaterialVertexShader,
    WorldMaterialVertexShaderName,
    WorldMaterialVertexShaderProgram,
    WorldMaterialPixelShader,
    WorldMaterialPixelShaderName,
    WorldMaterialPixelShaderProgram,
    WorldMaterialShaderArguments,
    WorldMaterialLiteralConstant,
    WorldMaterialTechniqueName,
    WorldFxEffect,
    WorldFxEffectName,
    WorldFxElemHeaders,
    WorldFxElemVelocitySamples,
    WorldFxElemVisualSamples,
    WorldFxElemVisualArray,
    WorldFxElemVisuals,
    WorldFxString,
    WorldFxTrail,
    WorldFxTrailVertices,
    WorldFxTrailIndices,
    WorldFxPublish,
    WorldFxImpactTable,
    WorldFxImpactName,
    WorldFxImpactEntries,
    WorldFxImpactPublish,
    WorldMenuTasks,
    WorldRawFile,
    WorldRawFileName,
    WorldRawFileBuffer,
    WorldRawFilePublish,
    WorldXAnimParts,
    WorldXAnimName,
    WorldXAnimPayload,
    WorldXAnimPublish,
    WorldWeaponDef,
    WorldWeaponString,
    WorldWeaponSoundNameCell,
    WorldWeaponSoundName,
    WorldWeaponBounceSoundCells,
    WorldWeaponAccuracyKnots,
    WorldWeaponPublish,
    WorldLocalizeEntry,
    WorldLocalizeValue,
    WorldLocalizeName,
    WorldLocalizePublish,
    WorldSoundAliasList,
    WorldSoundAliasListName,
    WorldSoundAliasHeaders,
    WorldSoundAliasString,
    WorldSoundAliasFile,
    WorldSoundLoadedSound,
    WorldSoundLoadedSoundName,
    WorldSoundLoadedSoundData,
    WorldSoundAliasCurve,
    WorldSoundAliasSpeakerMap,
    WorldSoundAliasPublish,
    WorldClipMap,
    WorldComWorld,
    WorldLightDef,
    WorldGfxWorld,
    AssetBoundary,
    Failed,
};

const char *RetailCensusStageString(RetailCensusStage stage) noexcept;

struct RetailCensusStepBudget
{
    std::uint32_t maxBytes = RETAIL_CENSUS_MAX_STEP_BYTES;
    std::uint32_t maxRecords = RETAIL_CENSUS_MAX_STEP_RECORDS;
};

struct RetailCensusStepReport
{
    RetailCensusProgress progress = RetailCensusProgress::NotStarted;
    RetailCensusStage stage = RetailCensusStage::NotStarted;
    RetailCensusError error = RetailCensusError::None;
    std::uint32_t sourceBytesConsumed = 0u;
    std::uint32_t inflatedBytesProduced = 0u;
    std::uint32_t traversedBytes = 0u;
    std::uint32_t recordsProcessed = 0u;
    bool needsSource = false;
};

struct RetailWorldMaterialTechnique
{
    std::uint32_t slot = UINT32_MAX;
    std::string name;
    std::uint16_t flags = 0u;
    std::uint16_t passCount = 0u;
    std::uint32_t headerBlock4Offset = 0u;
    std::uint32_t passArrayBlock4Offset = 0u;
    std::uint32_t nameBlock4Offset = 0u;
    std::uint32_t argumentCount = 0u;
    std::uint32_t vertexProgramDwords = 0u;
    std::uint32_t pixelProgramDwords = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    bool completed = false;
};

struct RetailWorldTechniqueSet
{
    std::uint32_t assetIndex = 0u;
    std::string name;
    std::uint32_t nameReference = 0u;
    std::uint32_t worldVertFormat = 0u;
    std::uint32_t remapReference = 0u;
    std::uint32_t block0Offset = 0u;
    std::uint32_t nameBlock4Offset = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    std::uint32_t firstTechniqueSlot = UINT32_MAX;
    std::uint32_t firstTechniqueReference = 0u;
    std::uint32_t nullTechniqueReferences = 0u;
    std::uint32_t inlineTechniqueReferences = 0u;
    std::uint32_t sharedTechniqueReferences = 0u;
    std::uint32_t aliasTechniqueReferences = 0u;
    std::uint32_t identity = 0u;
    std::vector<RetailWorldMaterialTechnique> techniques;
    bool published = false;
};

struct RetailXModelLod
{
    float distance = 0.0f;
    std::uint16_t surfaceCount = 0u;
    std::uint16_t surfaceIndex = 0u;
    std::array<std::uint32_t, 4> partBits{};
    std::uint8_t lod = 0u;
    std::uint8_t smcIndexPlusOne = 0u;
    std::uint8_t smcAllocBits = 0u;
};

struct RetailXSurfaceCollisionTree
{
    std::uint32_t reference = 0u;
    std::array<float, 3> translation{};
    std::array<float, 3> scale{};
    std::uint32_t nodeCount = 0u;
    std::uint32_t nodesReference = 0u;
    std::uint32_t leafCount = 0u;
    std::uint32_t leafsReference = 0u;
    std::uint32_t headerBlock4Offset = 0u;
    std::uint32_t nodesBlock4Offset = 0u;
    std::uint32_t leafsBlock4Offset = 0u;
    std::uint32_t nodesHash = 2166136261u;
    std::uint32_t leafsHash = 2166136261u;
    bool traversed = false;
};

struct RetailXRigidVertList
{
    std::uint16_t boneOffset = 0u;
    std::uint16_t vertCount = 0u;
    std::uint16_t triOffset = 0u;
    std::uint16_t triCount = 0u;
    RetailXSurfaceCollisionTree collisionTree;
};

struct RetailXSurface
{
    std::uint32_t index = 0u;
    std::uint8_t tileMode = 0u;
    bool deformed = false;
    std::uint16_t vertCount = 0u;
    std::uint16_t triCount = 0u;
    std::uint8_t zoneHandle = 0u;
    std::uint16_t baseTriIndex = 0u;
    std::uint16_t baseVertIndex = 0u;
    std::uint32_t triIndicesReference = 0u;
    std::array<std::int16_t, 4> blendVertCounts{};
    std::uint32_t vertsBlendReference = 0u;
    std::uint32_t vertsReference = 0u;
    std::uint32_t vertListCount = 0u;
    std::uint32_t vertListReference = 0u;
    std::array<std::uint32_t, 4> partBits{};
    std::uint32_t blendWordCount = 0u;
    std::uint32_t blendInfoBlock4Offset = 0u;
    std::uint32_t verticesBlock7Offset = 0u;
    std::uint32_t vertListsBlock4Offset = 0u;
    std::uint32_t indicesBlock8Offset = 0u;
    std::uint32_t verticesHash = 2166136261u;
    std::uint32_t indicesHash = 2166136261u;
    // M29 retains only bounded surfaces in the first declared LOD. These remain
    // serialized bytes until the engine-side draw-list converter validates the
    // complete model and each selected material dependency.
    std::vector<std::uint8_t> retainedPackedVertices;
    std::vector<std::uint8_t> retainedPackedIndices;
    std::vector<RetailXRigidVertList> rigidVertLists;
    bool renderPayloadRetained = false;
    bool dependenciesTraversed = false;
};

struct RetailPublishedGfxImage
{
    std::uint32_t ownerAssetIndex = 0u;
    std::uint32_t serializedReference = 0u;
    std::uint32_t textureIndex = 0u;
    std::string name;
    std::uint32_t nameReference = 0u;
    std::uint32_t mapType = 0u;
    std::uint32_t textureReference = 0u;
    std::uint16_t width = 0u;
    std::uint16_t height = 0u;
    std::uint16_t depth = 0u;
    std::uint32_t format = 0u;
    std::uint32_t resourceBytes = 0u;
    std::uint32_t headerBlock0Offset = 0u;
    std::uint32_t nameBlock4Offset = 0u;
    // DB_InsertPointer storage for a shared (-2) GfxImagePtr. This belongs to
    // the database pointer envelope, distinct from the image texture/load-def
    // insertion cell below.
    std::uint32_t assetInsertPointerBlock4Offset = UINT32_MAX;
    std::uint32_t textureInsertPointerBlock4Offset = UINT32_MAX;
    std::uint32_t loadDefBlock0Offset = 0u;
    std::uint32_t identity = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    std::shared_ptr<std::string> canonicalName;
    std::shared_ptr<GfxImage> asset;
    bool pointerAlias = false;
    bool nullRoot = false;
    bool loadDefTraversed = false;
    bool published = false;
};

using RetailXModelImage = RetailPublishedGfxImage;

struct RetailXModelMaterialTexture
{
    std::uint32_t nameHash = 0u;
    std::uint8_t nameStart = 0u;
    std::uint8_t nameEnd = 0u;
    std::uint8_t samplerState = 0u;
    std::uint8_t semantic = 0u;
    std::uint32_t imageReference = 0u;
    std::uint32_t imageIdentity = 0u;
    bool resolved = false;
};

struct RetailXModelMaterial
{
    std::uint32_t handleIndex = 0u;
    std::string name;
    std::uint32_t techniqueSetReference = 0u;
    std::uint32_t techniqueSetIdentity = 0u;
    std::uint8_t textureCount = 0u;
    std::uint8_t constantCount = 0u;
    std::uint8_t stateBitsCount = 0u;
    std::uint32_t headerBlock0Offset = 0u;
    std::uint32_t nameBlock4Offset = 0u;
    std::uint32_t textureTableBlock4Offset = 0u;
    std::uint32_t constantTableBlock4Offset = 0u;
    std::uint32_t stateBitsTableBlock4Offset = 0u;
    std::uint32_t constantsHash = 2166136261u;
    std::uint32_t stateBitsHash = 2166136261u;
    std::uint32_t identity = 0u;
    std::vector<RetailXModelMaterialTexture> textures;
    std::vector<RetailXModelImage> images;
    std::shared_ptr<std::string> canonicalName;
    std::shared_ptr<Material> asset;
    bool published = false;
};

struct RetailXModelCollisionSurface
{
    std::uint32_t index = 0u;
    std::uint32_t trianglesReference = 0u;
    std::uint32_t triangleCount = 0u;
    std::array<float, 3> mins{};
    std::array<float, 3> maxs{};
    std::int32_t boneIndex = 0;
    std::int32_t contents = 0;
    std::int32_t surfaceFlags = 0;
    std::uint32_t trianglesBlock4Offset = 0u;
    std::uint32_t trianglesHash = 2166136261u;
    bool traversed = false;
};

struct RetailXModelPhysPreset
{
    std::string name;
    std::string soundAliasPrefix;
    std::int32_t type = 0;
    float mass = 0.0f;
    float bounce = 0.0f;
    float friction = 0.0f;
    float bulletForceScale = 0.0f;
    float explosiveForceScale = 0.0f;
    float piecesSpreadFraction = 0.0f;
    float piecesUpwardVelocity = 0.0f;
    bool tempDefaultToCylinder = false;
    std::uint32_t nameReference = 0u;
    std::uint32_t soundAliasPrefixReference = 0u;
    std::uint32_t headerBlock0Offset = 0u;
    std::uint32_t nameBlock4Offset = 0u;
    std::uint32_t soundAliasPrefixBlock4Offset = 0u;
    std::uint32_t insertPointerBlock4Offset = 0u;
    std::uint32_t identity = 0u;
    bool traversed = false;
    bool published = false;
};

struct RetailWorldXModel
{
    std::uint32_t assetIndex = 0u;
    std::uint32_t registrySourceIndex = 0u;
    std::string name;
    std::uint8_t numBones = 0u;
    std::uint8_t numRootBones = 0u;
    std::uint8_t surfaceCount = 0u;
    std::uint8_t lodRampType = 0u;
    std::uint32_t boneNamesReference = 0u;
    std::uint32_t parentListReference = 0u;
    std::uint32_t quatsReference = 0u;
    std::uint32_t transReference = 0u;
    std::uint32_t partClassificationReference = 0u;
    std::uint32_t baseMatReference = 0u;
    std::uint32_t surfacesReference = 0u;
    std::uint32_t materialHandlesReference = 0u;
    std::array<RetailXModelLod, 4> lods{};
    std::uint32_t collisionSurfacesReference = 0u;
    std::uint32_t collisionSurfaceCount = 0u;
    std::uint32_t contents = 0u;
    std::uint32_t boneInfoReference = 0u;
    float radius = 0.0f;
    std::array<float, 3> mins{};
    std::array<float, 3> maxs{};
    std::int16_t lodCount = 0;
    std::int16_t collisionLod = 0;
    std::uint32_t memoryUsage = 0u;
    std::uint8_t flags = 0u;
    bool bad = false;
    std::uint32_t physPresetReference = 0u;
    std::uint32_t physGeomsReference = 0u;
    std::uint32_t physPresetIdentity = 0u;
    RetailXModelPhysPreset physPreset;
    std::uint32_t physGeomCount = 0u;
    std::uint32_t physGeomHeaderBlock4Offset = 0u;
    std::uint32_t physGeomInfosBlock4Offset = 0u;
    std::uint32_t physGeomBrushCount = 0u;
    std::uint32_t physGeomBrushSideCount = 0u;
    std::uint32_t physGeomPlaneCount = 0u;
    std::uint32_t physGeomEdgeCount = 0u;
    std::uint32_t physGeomPayloadBytes = 0u;
    std::vector<std::uint16_t> boneNameScriptStringIndices;
    std::vector<std::string> boneNames;
    std::vector<std::uint8_t> parentList;
    std::vector<std::int16_t> quats;
    std::vector<float> trans;
    std::vector<std::uint8_t> partClassification;
    std::vector<float> baseMat;
    std::vector<std::uint8_t> boneInfoData;
    std::vector<RetailXSurface> surfaces;
    std::vector<std::uint32_t> materialReferences;
    std::vector<std::uint32_t> materialIdentities;
    std::vector<RetailXModelMaterial> materials;
    // Deduplicated published material metadata reachable by this model,
    // including aliases to materials owned by earlier XModels.
    std::vector<RetailXModelMaterial> resolvedMaterials;
    // Deduplicated published image metadata reachable by this model's
    // materials, including aliases to images owned by earlier XModels.
    std::vector<RetailXModelImage> resolvedImages;
    std::vector<RetailXModelCollisionSurface> collisionSurfaces;
    std::uint32_t totalVertices = 0u;
    std::uint32_t totalTriangles = 0u;
    std::uint32_t totalRigidVertLists = 0u;
    std::uint32_t totalCollisionNodes = 0u;
    std::uint32_t totalCollisionLeaves = 0u;
    std::uint32_t surfacePayloadBytes = 0u;
    std::uint32_t headerBlock0Offset = 0u;
    std::uint32_t nameBlock4Offset = 0u;
    std::uint32_t boneNamesBlock4Offset = 0u;
    std::uint32_t parentListBlock4Offset = 0u;
    std::uint32_t quatsBlock4Offset = 0u;
    std::uint32_t transBlock4Offset = 0u;
    std::uint32_t partClassificationBlock4Offset = 0u;
    std::uint32_t baseMatBlock4Offset = 0u;
    std::uint32_t surfacesBlock4Offset = 0u;
    std::uint32_t materialHandlesBlock4Offset = 0u;
    std::uint32_t collisionSurfacesBlock4Offset = 0u;
    std::uint32_t boneInfoBlock4Offset = 0u;
    std::uint32_t collisionTriangleCount = 0u;
    std::uint32_t collisionPayloadBytes = 0u;
    std::uint32_t boneInfoHash = 2166136261u;
    std::uint32_t identity = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    std::shared_ptr<std::string> canonicalName;
    std::shared_ptr<std::vector<Material *>> canonicalMaterialHandles;
    std::shared_ptr<XModel> asset;
    bool headerTraversed = false;
    bool skeletonPrefixTraversed = false;
    bool surfaceHeadersTraversed = false;
    bool surfaceDependenciesTraversed = false;
    bool materialHandlesTraversed = false;
    bool materialsTraversed = false;
    bool collisionSurfacesTraversed = false;
    bool boneInfoTraversed = false;
    bool physPresetTraversed = false;
    bool physGeomsTraversed = false;
    // Renderer selection is a caller policy, not an XModel parsing rule.
    // Eligible models can retain bounded payloads; entry zero starts selected.
    bool rendererPayloadSelected = false;
    bool rendererPayloadAvailable = false;
    bool published = false;
    bool topLevelAsset = true;
    bool stoppedBeforeSurfaceArray = false;
    bool stoppedBeforeMaterialDependency = false;
};

using RetailWorldFxMaterial = RetailXModelMaterial;

struct RetailWorldFxElemDef
{
    std::uint32_t flags = 0u;
    std::uint8_t elemType = 0u;
    std::uint8_t visualCount = 0u;
    std::uint8_t velocityIntervalCount = 0u;
    std::uint8_t visualStateIntervalCount = 0u;
    std::uint32_t velocitySamplesReference = 0u;
    std::uint32_t visualSamplesReference = 0u;
    std::uint32_t visualsReference = 0u;
    std::array<std::uint32_t, 3> effectReferences{};
    std::uint32_t trailReference = 0u;
    std::uint32_t headerBlock4Offset = 0u;
    std::uint32_t velocitySamplesBlock4Offset = 0u;
    std::uint32_t visualSamplesBlock4Offset = 0u;
    std::uint32_t visualArrayBlock4Offset = 0u;
    std::uint32_t velocitySamplesHash = 2166136261u;
    std::uint32_t visualSamplesHash = 2166136261u;
    std::uint32_t trailPayloadHash = 2166136261u;
    std::uint32_t trailVertexCount = 0u;
    std::uint32_t trailIndexCount = 0u;
    std::vector<std::uint32_t> visualReferences;
    std::vector<std::uint32_t> visualIdentities;
    bool traversed = false;
};

struct RetailWorldFxEffectDef
{
    std::uint32_t assetIndex = 0u;
    std::string name;
    std::int32_t flags = 0;
    std::int32_t totalSize = 0;
    std::int32_t msecLoopingLife = 0;
    std::uint32_t loopingElemCount = 0u;
    std::uint32_t oneShotElemCount = 0u;
    std::uint32_t emissionElemCount = 0u;
    std::uint32_t elemDefsReference = 0u;
    std::uint32_t headerBlock0Offset = 0u;
    std::uint32_t nameBlock4Offset = 0u;
    std::uint32_t elemDefsBlock4Offset = 0u;
    std::uint32_t identity = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    std::vector<RetailWorldFxElemDef> elemDefs;
    std::vector<RetailWorldFxMaterial> materials;
    std::shared_ptr<std::string> canonicalName;
    // The current FX traversal owns the exact contiguous 252-byte element
    // headers as aligned words. Their nested samples/visuals remain governed
    // by the existing bounded loader while the canonical top-level handle is
    // exposed to database consumers.
    std::shared_ptr<std::vector<std::uint32_t>> canonicalElemDefWords;
    std::shared_ptr<FxEffectDef> asset;
    bool published = false;
};

// Temporary ownership for a canonical RawFile published by the browser
// database path. The shared storage keeps the canonical pointers valid when a
// completed census result is moved or copied; gameplay-facing code sees the
// real engine type through `asset`, not a parallel web asset definition.
struct RetailWorldRawFile
{
    std::uint32_t assetIndex = 0u;
    std::string name;
    std::int32_t length = 0;
    std::uint32_t nameReference = 0u;
    std::uint32_t bufferReference = 0u;
    std::uint32_t headerBlock0Offset = 0u;
    std::uint32_t nameBlock4Offset = 0u;
    std::uint32_t bufferBlock4Offset = 0u;
    std::uint32_t identity = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    std::shared_ptr<std::string> nameStorage;
    std::shared_ptr<std::vector<char>> bufferStorage;
    std::shared_ptr<RawFile> asset;
    bool published = false;
};

// Ownership-only backing for the canonical XAnimParts pointer graph. This is
// not a second animation representation: every decoded field is published on
// XAnimParts and its canonical child structures. Shared ownership keeps those
// pointers stable across result moves/copies until a real Kisak zone allocator
// replaces this traversal scaffold.
struct CanonicalXAnimPartsStorage
{
    std::shared_ptr<std::string> name;
    std::shared_ptr<std::vector<std::uint16_t>> names;
    std::shared_ptr<std::vector<XAnimNotifyInfo>> notify;
    std::shared_ptr<XAnimDeltaPart> deltaPart;
    std::shared_ptr<XAnimPartTrans> deltaTrans;
    std::shared_ptr<XAnimDeltaPartQuat> deltaQuat;
    std::shared_ptr<std::vector<std::uint8_t>> deltaTransByteFrames;
    std::shared_ptr<std::vector<std::uint16_t>> deltaTransShortFrames;
    std::shared_ptr<std::vector<std::int16_t>> deltaQuatFrames;
    std::shared_ptr<std::vector<std::uint8_t>> dataByte;
    std::shared_ptr<std::vector<std::int16_t>> dataShort;
    std::shared_ptr<std::vector<int>> dataInt;
    std::shared_ptr<std::vector<std::int16_t>> randomDataShort;
    std::shared_ptr<std::vector<std::uint8_t>> randomDataByte;
    std::shared_ptr<std::vector<int>> randomDataInt;
    std::shared_ptr<std::vector<std::uint8_t>> byteIndices;
    std::shared_ptr<std::vector<std::uint16_t>> shortIndices;
};

struct RetailPublishedXAnimParts
{
    std::uint32_t assetIndex = 0u;
    std::uint32_t serializedReference = 0u;
    std::uint32_t headerBlock0Offset = 0u;
    std::uint32_t nameBlock4Offset = 0u;
    std::uint32_t insertPointerBlock4Offset = UINT32_MAX;
    std::uint32_t payloadBytes = 0u;
    std::uint32_t identity = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    std::shared_ptr<CanonicalXAnimPartsStorage> storage;
    std::shared_ptr<XAnimParts> asset;
    bool pointerAlias = false;
    bool published = false;
};

// Ownership-only backing for canonical WeaponDef pointers. The fixed record is
// decoded into WeaponDef itself; this storage keeps variable-length XStrings,
// sound-name indirections/bounce handles, and vec2 accuracy-knot arrays alive
// across result moves/copies.
struct CanonicalWeaponDefStorage
{
    std::array<std::shared_ptr<std::string>, 48> strings{};
    std::array<std::shared_ptr<std::string>, 48> soundNames{};
    std::array<std::shared_ptr<std::string>, 29> bounceSoundNames{};
    std::shared_ptr<std::vector<snd_alias_list_t *>> bounceSounds;
    std::array<std::shared_ptr<std::vector<std::array<float, 2>>>, 4>
        accuracyKnots{};
};

struct RetailPublishedWeaponDef
{
    std::uint32_t assetIndex = 0u;
    std::uint32_t serializedReference = 0u;
    std::uint32_t headerBlock0Offset = 0u;
    std::uint32_t insertPointerBlock4Offset = UINT32_MAX;
    std::array<std::uint32_t, 48> stringBlock4Offsets{};
    std::array<std::uint32_t, 48> soundNameCellBlock4Offsets{};
    std::array<std::uint32_t, 48> soundNameStringBlock4Offsets{};
    std::array<std::uint32_t, 29> bounceSoundNameCellBlock4Offsets{};
    std::array<std::uint32_t, 29> bounceSoundNameStringBlock4Offsets{};
    std::uint32_t bounceSoundArrayBlock4Offset = UINT32_MAX;
    std::array<std::uint32_t, 4> accuracyKnotBlock4Offsets{};
    std::uint32_t payloadBytes = 0u;
    std::uint32_t identity = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    std::shared_ptr<CanonicalWeaponDefStorage> storage;
    std::shared_ptr<WeaponDef> asset;
    bool pointerAlias = false;
    bool published = false;
};

struct CanonicalLocalizeEntryStorage
{
    std::shared_ptr<std::string> value;
    std::shared_ptr<std::string> name;
};

struct RetailPublishedLocalizeEntry
{
    std::uint32_t assetIndex = 0u;
    std::uint32_t serializedReference = 0u;
    std::uint32_t headerBlock0Offset = 0u;
    std::uint32_t insertPointerBlock4Offset = UINT32_MAX;
    std::uint32_t valueBlock4Offset = UINT32_MAX;
    std::uint32_t nameBlock4Offset = UINT32_MAX;
    std::uint32_t payloadBytes = 0u;
    std::uint32_t identity = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    std::shared_ptr<CanonicalLocalizeEntryStorage> storage;
    std::shared_ptr<LocalizeEntry> asset;
    bool pointerAlias = false;
    bool published = false;
};

// Ownership backing for the canonical sound graph published by a zone. The
// catalog only indexes `asset`; all names and child objects remain owned here
// with the same lifetime as the prerequisite-zone result.
struct CanonicalSoundAliasListStorage
{
    std::shared_ptr<std::string> aliasName;
    std::shared_ptr<std::vector<snd_alias_t>> aliases;
    std::vector<std::array<std::shared_ptr<std::string>, 4>> aliasStrings;
    std::vector<std::array<std::shared_ptr<std::string>, 2>> fileStrings;
    std::vector<std::shared_ptr<SoundFile>> soundFiles;
    std::vector<std::uint32_t> soundFileBlock4Offsets;
    std::vector<std::shared_ptr<LoadedSound>> loadedSounds;
    std::vector<std::shared_ptr<std::string>> loadedSoundNames;
    std::vector<std::shared_ptr<SndCurve>> curves;
    std::vector<std::shared_ptr<std::string>> curveNames;
    std::vector<std::shared_ptr<SpeakerMap>> speakerMaps;
    std::vector<std::shared_ptr<std::string>> speakerMapNames;
};

struct RetailPublishedSoundAliasList
{
    std::uint32_t assetIndex = 0u;
    std::uint32_t serializedReference = 0u;
    std::uint32_t headerBlock0Offset = 0u;
    std::uint32_t insertPointerBlock4Offset = UINT32_MAX;
    std::uint32_t nameBlock4Offset = UINT32_MAX;
    std::uint32_t aliasesBlock4Offset = UINT32_MAX;
    std::uint32_t payloadBytes = 0u;
    std::uint32_t identity = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    std::shared_ptr<CanonicalSoundAliasListStorage> storage;
    std::shared_ptr<snd_alias_list_t> asset;
    bool pointerAlias = false;
    bool databaseAlias = false;
    bool published = false;
};

struct RetailPublishedClipMap
{
    std::uint32_t assetIndex = 0u;
    std::uint32_t assetType = 0u;
    std::uint32_t serializedReference = 0u;
    std::uint32_t headerBlock0Offset = 0u;
    std::uint32_t insertPointerBlock4Offset = UINT32_MAX;
    std::uint32_t nameBlock4Offset = UINT32_MAX;
    std::uint32_t payloadBytes = 0u;
    std::uint32_t identity = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    std::shared_ptr<CanonicalClipMapStorage> storage;
    std::shared_ptr<clipMap_t> asset;
    bool pointerAlias = false;
    bool published = false;
};

struct RetailPublishedComWorld
{
    std::uint32_t assetIndex = 0u;
    std::uint32_t serializedReference = 0u;
    std::uint32_t headerBlock0Offset = UINT32_MAX;
    std::uint32_t insertPointerBlock4Offset = UINT32_MAX;
    std::uint32_t nameBlock4Offset = UINT32_MAX;
    std::uint32_t primaryLightsBlock4Offset = UINT32_MAX;
    std::uint32_t payloadBytes = 0u;
    std::uint32_t identity = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    std::vector<std::uint32_t> lightDefNameBlock4Offsets;
    std::shared_ptr<CanonicalComWorldStorage> storage;
    std::shared_ptr<ComWorld> asset;
    bool nullRoot = false;
    bool pointerAlias = false;
    bool published = false;
};

struct RetailPublishedLightDef
{
    std::uint32_t assetIndex = 0u;
    std::uint32_t serializedReference = 0u;
    std::uint32_t headerBlock0Offset = UINT32_MAX;
    std::uint32_t insertPointerBlock4Offset = UINT32_MAX;
    std::uint32_t nameBlock4Offset = UINT32_MAX;
    std::uint32_t attenuationImageIdentity = 0u;
    std::uint32_t identity = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    std::shared_ptr<CanonicalLightDefStorage> storage;
    std::shared_ptr<GfxLightDef> asset;
    bool nullRoot = false;
    bool pointerAlias = false;
    bool published = false;
};

struct RetailPublishedGfxWorld
{
    std::uint32_t assetIndex = 0u;
    std::uint32_t serializedReference = 0u;
    std::uint32_t headerBlock0Offset = UINT32_MAX;
    std::uint32_t insertPointerBlock4Offset = UINT32_MAX;
    std::uint32_t nameBlock4Offset = UINT32_MAX;
    std::uint32_t baseNameBlock4Offset = UINT32_MAX;
    std::uint32_t identity = 0u;
    std::uint32_t boundaryInflatedOffset = 0u;
    std::uint32_t payloadBytes = 0u;
    std::uint32_t block0HighWaterAtPublication = 0u;
    std::uint32_t block1HighWaterAtPublication = 0u;
    std::uint32_t block4CursorAtPublication = 0u;
    std::uint32_t registryAssetCountAtPublication = 0u;
    std::uint32_t registryAliasCountAtPublication = 0u;
    std::uint32_t registryDefinedAliasCountAtPublication = 0u;
    // Canonical registry identities resolved for dpvs.surfaces in serialized
    // order. Keeping these beside the owned graph makes native/Wasm pointer
    // identity differences observable without introducing a second world IR.
    std::vector<std::uint32_t> surfaceMaterialIdentities;
    std::shared_ptr<CanonicalGfxWorldStorage> storage;
    std::shared_ptr<GfxWorld> asset;
    bool nullRoot = false;
    bool pointerAlias = false;
    bool published = false;
};

struct RetailFastfileCensus
{
    std::uint32_t version = 0u;
    std::uint32_t xfileSize = 0u;
    std::uint32_t externalSize = 0u;
    std::array<std::uint32_t, 9> blockSizes{};
    std::uint64_t declaredBlockBytes = 0u;
    std::uint32_t scriptStringCount = 0u;
    std::uint32_t scriptStringBytes = 0u;
    std::uint32_t assetCount = 0u;
    std::array<std::uint32_t, RETAIL_CENSUS_ASSET_TYPE_COUNT> typeCounts{};
    std::array<std::uint32_t, RETAIL_CENSUS_ASSET_TYPE_COUNT> firstTypeIndices = [] {
        std::array<std::uint32_t, RETAIL_CENSUS_ASSET_TYPE_COUNT> indices{};
        indices.fill(UINT32_MAX);
        return indices;
    }();
    std::array<std::uint32_t, RETAIL_CENSUS_ASSET_TYPE_COUNT> typesBeforeFirstGfxWorld{};
    std::vector<RetailPublishedLocalizeEntry> worldLocalizeEntries;
    std::vector<RetailXModelMaterial> worldMaterials;
    std::uint32_t assetTableOrderHash = 2166136261u;
    std::uint32_t firstGfxWorldAssetIndex = UINT32_MAX;
    std::uint32_t firstGfxWorldReference = 0u;
    std::uint32_t inlineReferencesBeforeFirstGfxWorld = 0u;
    std::uint32_t sharedReferencesBeforeFirstGfxWorld = 0u;
    std::uint32_t aliasReferencesBeforeFirstGfxWorld = 0u;
    std::uint32_t nullReferencesBeforeFirstGfxWorld = 0u;
    std::string worldFirstTechniqueSetName;
    std::uint32_t worldFirstTechniqueSetWorldVertFormat = 0u;
    std::uint32_t worldFirstTechniqueSetRemapReference = 0u;
    std::uint32_t worldFirstTechniqueSetBlock0Offset = 0u;
    std::uint32_t worldFirstTechniqueSetNameBlock4Offset = 0u;
    std::uint32_t worldFirstTechniqueSetBoundaryInflatedOffset = 0u;
    std::uint32_t worldFirstTechniqueSlot = UINT32_MAX;
    std::uint32_t worldFirstTechniqueReference = 0u;
    std::uint32_t worldTechniqueNullReferences = 0u;
    std::uint32_t worldTechniqueInlineReferences = 0u;
    std::uint32_t worldTechniqueSharedReferences = 0u;
    std::uint32_t worldTechniqueAliasReferences = 0u;
    std::uint32_t worldRegistryAliasCount = 0u;
    std::uint32_t worldRegistryDefinedAliasCount = 0u;
    std::uint32_t worldFirstTechniqueSetIdentity = 0u;
    std::vector<RetailWorldTechniqueSet> worldTechniqueSets;
    std::uint32_t worldTechniqueSetBodiesEntered = 0u;
    std::uint32_t worldNextAssetIndex = 0u;
    std::uint32_t worldPostXModelTechniqueSetAssetIndex = UINT32_MAX;
    std::uint32_t worldPostXModelTechniqueSetBodiesEntered = 0u;
    std::uint32_t worldPostXModelTechniqueSetCompletedCount = 0u;
    std::vector<RetailWorldXModel> worldXModels;
    std::vector<RetailWorldFxEffectDef> worldFxEffects;
    bool worldFirstTechniqueSetHeaderTraversed = false;
    bool worldFirstTechniqueSetPublished = false;
    bool stoppedBeforeWorldTechniqueDependency = false;
    bool stoppedBeforeDifferentWorldAssetType = false;
    bool stoppedAfterCanonicalRawFile = false;
    bool stoppedBeforeWorldXModelDependency = false;
    bool worldPostXModelTechniqueSetPublished = false;
    std::uint32_t inlineAssetReferences = 0u;
    std::uint32_t sharedAssetReferences = 0u;
    std::uint32_t aliasAssetReferences = 0u;
    std::uint32_t nullAssetReferences = 0u;
    std::uint32_t firstBodyIndex = 0u;
    std::uint32_t firstBodyType = 0u;
    std::uint32_t firstBodyReference = 0u;
    std::uint32_t nextBodyIndex = 1u;
    std::uint32_t nextBodyType = 0u;
    std::uint32_t nextBodyReference = 0u;
    std::uint32_t inflatedPrefixBytes = 0u;
    std::uint64_t sourceBytesConsumed = 0u;
    std::uint32_t sourceFeedCount = 0u;
    bool stoppedBeforeAssetBody = false;
    std::string techniqueSetName;
    std::string vertexShaderName;
    std::string pixelShaderName;
    std::string techniqueName;
    std::uint32_t firstTechniqueSlot = 0u;
    std::uint32_t techniquePassCount = 0u;
    std::uint32_t vertexStreamCount = 0u;
    std::array<std::uint8_t, 32> vertexStreamRouting{};
    std::uint32_t vertexStreamRoutingHash = 0u;
    std::uint32_t vertexShaderProgramDwords = 0u;
    std::uint32_t vertexShaderProgramHash = 0u;
    std::uint32_t vertexShaderInstructionCount = 0u;
    std::uint32_t vertexShaderConstantCount = 0u;
    std::uint32_t pixelShaderProgramDwords = 0u;
    std::uint32_t pixelShaderProgramHash = 0u;
    std::uint32_t pixelShaderInstructionCount = 0u;
    std::uint32_t pixelShaderConstantCount = 0u;
    std::uint32_t shaderArgumentCount = 0u;
    std::uint32_t shaderArgumentHash = 0u;
    std::string shaderSubstitutionId;
    std::uint32_t vertexGlslHash = 0u;
    std::uint32_t fragmentGlslHash = 0u;
    std::uint32_t assetTableBlock4Offset = 0u;
    std::uint32_t techniqueSetBlock0Offset = 0u;
    std::uint32_t techniqueBlock4Offset = 0u;
    std::uint32_t vertexDeclarationBlock4Offset = 0u;
    std::uint32_t vertexShaderBlock4Offset = 0u;
    std::uint32_t vertexShaderProgramBlock4Offset = 0u;
    std::uint32_t pixelShaderBlock4Offset = 0u;
    std::uint32_t pixelShaderProgramBlock4Offset = 0u;
    std::uint32_t shaderArgumentsBlock4Offset = 0u;
    std::uint32_t techniqueNameBlock4Offset = 0u;
    std::string materialTechniqueSetName;
    std::string materialName;
    std::string imageName;
    std::string imagePath;
    std::uint32_t materialAssetIndex = 0u;
    std::uint32_t materialTextureCount = 0u;
    std::uint32_t imageWidth = 0u;
    std::uint32_t imageHeight = 0u;
    std::uint32_t imageDepth = 0u;
    std::uint32_t imageFormat = 0u;
    std::uint32_t imageResourceBytes = 0u;
    std::uint32_t materialTechniqueSetBlock0Offset = 0u;
    std::uint32_t materialTechniqueBlock4Offset = 0u;
    std::uint32_t materialBlock0Offset = 0u;
    std::uint32_t materialNameBlock4Offset = 0u;
    std::uint32_t materialTextureTableBlock4Offset = 0u;
    std::uint32_t imageBlock0Offset = 0u;
    std::uint32_t imageNameBlock4Offset = 0u;
    std::uint32_t imageTextureInsertPointerBlock4Offset = UINT32_MAX;
    std::uint32_t imageLoadDefBlock0Offset = 0u;
    std::uint32_t materialStateBitsBlock4Offset = 0u;
    std::uint32_t compatibilityTechniqueSetIdentity = 0u;
    std::uint32_t materialTechniqueSetIdentity = 0u;
    std::uint32_t materialIdentity = 0u;
    std::uint32_t imageIdentity = 0u;
    std::uint32_t registryAssetCount = 0u;
    std::uint32_t registryAliasCount = 0u;
    std::uint32_t registryDefinedAliasCount = 0u;
    std::uint32_t block0HighWaterAtBoundary = 0u;
    std::uint32_t block4CursorAtBoundary = 0u;
    std::uint32_t completedAssetCount = 0u;
    std::vector<RetailWorldRawFile> worldRawFiles;
    std::vector<RetailPublishedXAnimParts> worldXAnimParts;
    std::vector<RetailPublishedWeaponDef> worldWeapons;
    std::vector<RetailPublishedSoundAliasList> worldSoundAliasLists;
    std::vector<RetailPublishedClipMap> worldClipMaps;
    std::vector<RetailPublishedComWorld> worldComWorlds;
    std::vector<RetailPublishedGfxImage> worldImages;
    std::vector<RetailPublishedLightDef> worldLightDefs;
    std::vector<RetailPublishedGfxWorld> worldGfxWorlds;
    std::vector<kisak::database::SemanticTraceEntry> semanticTrace;
    std::uint32_t semanticTraceHash = 2166136261u;
    std::uint32_t semanticTraceContractHash = 2166136261u;
    bool techniqueSetPublished = false;
    bool vertexDeclarationPrepared = false;
    bool stoppedBeforeShaderCreation = false;
    bool shaderCompatibilitySelected = false;
    bool materialTechniqueSetPublished = false;
    bool materialPublished = false;
    bool imagePublished = false;
    bool materialImageResolved = false;
    const char *unsupportedOperation = nullptr;
};

// A deliberately narrow retail reader. It validates the unsigned v5/zlib
// envelope, XFile, ScriptStringList and complete XAsset table, then follows the
// generated loader for the exact leading two-technique-set/one-material prefix.
// The first technique set selects the owned WebGL2 compatibility program. The
// second is traversed as the material's serialized dependency; one texture-table
// entry and its inline GfxImage are then validated and published through stable
// registry identities. WorldAssetInventory instead requires a GfxWorld table
// entry and stops before body zero while retaining the exact intervening order.
// WorldTechniqueSetPrefix enters consecutive inline technique-set bodies,
// publishes each zero-dependency set, and stops before the first technique
// dependency or different top-level asset type.
// WorldXModelPrefix continues into the first inline XModel, validates its fixed
// header and bounded skeleton prefix, then stops before XSurface traversal.
// WorldXSurfacePrefix additionally walks bounded surface, rigid-list,
// collision, vertex/index, and material-handle records in generated-loader order.
// WorldXModelDependencies continues through checked material/image dependencies,
// collision triangles, bone info, and the first model's null physics references,
// publishing the XModel alias only after the complete dependency chain succeeds.
// WorldPostXModelTechniqueSet resumes the generated top-level loader after that
// publication, enters the consecutive inline MaterialTechniqueSet run, and
// stops before the first different top-level asset, non-inline set, or nested
// MaterialTechnique.
// WorldSecondXModelPrefix continues through that run, validates the next inline
// XModel header and bounded skeleton prefix, then stops before its XSurface
// dependency without replacing the published first-model result.
// WorldSecondXSurfacePrefix additionally traverses that model's bounded
// XSurface payloads and material-handle ordering, stopping before an inline
// material body. This reuses the engine-facing XModel parser rather than
// introducing a standalone model-viewer path.
// WorldSecondXModelDependencies completes that model's checked material/image,
// collision, bone-info, and null-physics chain and publishes its reserved alias.
// WorldXModelLoader stores each model in a bounded collection and invokes the
// same complete dependency path whenever the supported top-level dispatcher
// encounters an inline XModel, including after intervening technique-set runs.
// It also completes bounded inline MaterialTechnique dependencies before
// publishing their parent set, models shared GfxImage insertion-pointer cells
// so typed image aliases resolve at canonical block-4 addresses, then returns
// to that same dispatcher. WorldAssetLoader is the canonical alias once the
// same dispatcher also reaches inline FxEffectDef bodies. The FX path validates
// bounded samples, visuals, name references, trails, engine-owned materials,
// and nested XModels before publishing its parent effect.
// Renderer selection is explicit per model. Eligible first-LOD payloads share
// an aggregate byte ceiling and may be switched without reparsing; decoding and
// graphics submission remain separate engine-side work. WorldXModelCollection
// remains an API-compatible alias for the former milestone name.
// Native D3D9 creation is never invoked.
class RetailFastfileCensusJob
{
public:
    RetailFastfileCensusJob() noexcept;
    ~RetailFastfileCensusJob();
    RetailFastfileCensusJob(RetailFastfileCensusJob &&) noexcept;
    RetailFastfileCensusJob &operator=(RetailFastfileCensusJob &&) noexcept;
    RetailFastfileCensusJob(const RetailFastfileCensusJob &) = delete;
    RetailFastfileCensusJob &operator=(const RetailFastfileCensusJob &) = delete;

    RetailCensusError BeginStreaming(
        const RetailCensusLimits &limits = {}) noexcept;
    RetailCensusError BeginStreaming(
        RetailCensusMode mode,
        const RetailCensusLimits &limits = {},
        RetailSoundAliasLookup soundLookup = {}) noexcept;
    // May be changed only before the first source byte is supplied. Browser
    // fixture zones deliberately stop at the historical pre-GfxWorld boundary;
    // a full retail zone enables the canonical body after its stat completes.
    bool ConfigureGfxWorldLoading(bool enabled) noexcept;
    RetailCensusError FeedSource(
        std::span<const std::uint8_t> bytes,
        bool final) noexcept;
    RetailCensusStepReport Step(
        const RetailCensusStepBudget &budget = {}) noexcept;
    RetailCensusProgress Progress() const noexcept;
    RetailCensusStage Stage() const noexcept;
    RetailCensusError Failure() const noexcept;
    std::uint32_t CurrentAssetIndex() const noexcept;
    std::uint32_t CurrentAssetType() const noexcept;
    bool NeedsSource() const noexcept;
    std::uint64_t SourceBytesReceived() const noexcept;
    bool TakeResult(RetailFastfileCensus &destination) noexcept;
    void Reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    RetailCensusProgress progress_ = RetailCensusProgress::NotStarted;
    RetailCensusStage stage_ = RetailCensusStage::NotStarted;
    RetailCensusError failure_ = RetailCensusError::None;
    bool resultAvailable_ = false;
};

} // namespace kisak::fastfile
