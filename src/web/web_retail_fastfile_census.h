#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace kisak::fastfile
{

inline constexpr std::uint32_t RETAIL_CENSUS_ASSET_TYPE_COUNT = 33u;
inline constexpr std::uint32_t RETAIL_CENSUS_MAX_STEP_BYTES = 64u * 1024u;
inline constexpr std::uint32_t RETAIL_CENSUS_MAX_STEP_RECORDS = 64u;

struct RetailCensusLimits
{
    std::uint32_t maxFileBytes = 16u * 1024u * 1024u;
    std::uint32_t maxSourceChunkBytes = RETAIL_CENSUS_MAX_STEP_BYTES;
    std::uint32_t maxInflatedPrefixBytes = 256u * 1024u;
    std::uint32_t maxBlockBytes = 512u * 1024u * 1024u;
    std::uint64_t maxTotalBlockBytes = 1536ull * 1024ull * 1024ull;
    std::uint32_t maxScriptStrings = 4096u;
    std::uint32_t maxScriptStringBytes = 4096u;
    std::uint32_t maxTotalScriptStringBytes = 256u * 1024u;
    std::uint32_t maxAssets = 16384u;
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
    std::uint32_t maxXModelCollisionSurfaces = 4096u;
    std::uint32_t maxXModelSurfaceVertices = 1024u * 1024u;
    std::uint32_t maxXModelSurfaceTriangles = 1024u * 1024u;
    std::uint32_t maxXModelRigidVertLists = 4096u;
    std::uint32_t maxXModelCollisionNodes = 1024u * 1024u;
    std::uint32_t maxXModelCollisionLeaves = 1024u * 1024u;
    std::uint32_t maxXModelSurfacePayloadBytes = 64u * 1024u * 1024u;
    std::uint32_t maxXModelCollisionTriangles = 1024u * 1024u;
    std::uint32_t maxXModelCollisionPayloadBytes = 64u * 1024u * 1024u;
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
    XModelBoundsInvalid,
    XModelScriptStringInvalid,
    XModelDependencyUnsupported,
    XSurfaceLayoutUnsupported,
    XSurfaceCountInvalid,
    XSurfacePayloadLimit,
    XSurfaceCollisionInvalid,
    XModelMaterialAliasInvalid,
    XModelCollisionInvalid,
    XModelCollisionPayloadLimit,
    XModelBoneInfoInvalid,
    XModelPhysicsUnsupported,
    PostXModelAssetUnsupported,
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
    WorldXModelPhysGeoms,
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

struct RetailWorldTechniqueSet
{
    std::uint32_t assetIndex = 0u;
    std::string name;
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

struct RetailXModelImage
{
    std::uint32_t textureIndex = 0u;
    std::string name;
    std::uint32_t mapType = 0u;
    std::uint32_t textureReference = 0u;
    std::uint16_t width = 0u;
    std::uint16_t height = 0u;
    std::uint16_t depth = 0u;
    std::uint32_t format = 0u;
    std::uint32_t resourceBytes = 0u;
    std::uint32_t headerBlock0Offset = 0u;
    std::uint32_t nameBlock4Offset = 0u;
    std::uint32_t loadDefBlock0Offset = 0u;
    std::uint32_t identity = 0u;
    bool loadDefTraversed = false;
    bool published = false;
};

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

struct RetailWorldXModel
{
    std::uint32_t assetIndex = 0u;
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
    std::vector<std::uint16_t> boneNameScriptStringIndices;
    std::vector<std::string> boneNames;
    std::vector<std::uint8_t> parentList;
    std::vector<std::uint8_t> partClassification;
    std::vector<RetailXSurface> surfaces;
    std::vector<std::uint32_t> materialReferences;
    std::vector<std::uint32_t> materialIdentities;
    std::vector<RetailXModelMaterial> materials;
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
    bool published = false;
    bool stoppedBeforeSurfaceArray = false;
    bool stoppedBeforeMaterialDependency = false;
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
    std::array<std::uint32_t, RETAIL_CENSUS_ASSET_TYPE_COUNT> typesBeforeFirstGfxWorld{};
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
    RetailWorldXModel worldFirstXModel;
    RetailWorldXModel worldSecondXModel;
    bool worldFirstTechniqueSetHeaderTraversed = false;
    bool worldFirstTechniqueSetPublished = false;
    bool stoppedBeforeWorldTechniqueDependency = false;
    bool stoppedBeforeDifferentWorldAssetType = false;
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
// It retains serialized vertex/index bytes only for a renderer-bounded first
// surface; decoding and graphics submission remain separate engine-side work.
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
        const RetailCensusLimits &limits = {}) noexcept;
    RetailCensusError FeedSource(
        std::span<const std::uint8_t> bytes,
        bool final) noexcept;
    RetailCensusStepReport Step(
        const RetailCensusStepBudget &budget = {}) noexcept;
    RetailCensusProgress Progress() const noexcept;
    RetailCensusStage Stage() const noexcept;
    RetailCensusError Failure() const noexcept;
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
