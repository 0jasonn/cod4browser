#pragma once

#include <web/web_renderer_surface.h>
#include <web/web_shader_compatibility.h>

#include <cstddef>
#include <cstdint>

struct WebFrameInfo;
struct GfxImage;
struct Material;
struct XModel;

// The bootstrap renderer deliberately keeps one bounded CPU-side RGBA8 copy
// so its texture can be recreated after browser context loss. These limits
// bound that recovery allocation independently of the active graphics backend.
constexpr std::uint32_t WEB_RENDERER_MAX_RGBA8_DIMENSION = 2048u;
constexpr std::size_t WEB_RENDERER_MAX_RETAINED_TEXTURE_BYTES =
    4u * 1024u * 1024u;

struct WebRendererRgba8TextureDesc
{
    std::uint32_t width;
    std::uint32_t height;
    const std::uint8_t *pixels;
    std::size_t byteLength;
    // Original COD4 MaterialTextureDef sampler byte. Zero keeps the bootstrap
    // fallback behavior; imported material bindings provide the checked value.
    std::uint8_t samplerState = 0u;
};

struct WebRendererTextureState
{
    std::uint32_t width;
    std::uint32_t height;
    std::size_t retainedByteCount;
    std::uint32_t uploadGeneration;
    std::uint32_t rebuildGeneration;
    std::uint32_t recoveryCount;
    bool sourceTextureActive;
    bool resident;
};

struct WebRendererShaderState
{
    const char *substitutionId;
    std::uint32_t vertexSourceHash;
    std::uint32_t fragmentSourceHash;
    std::uint32_t submissionGeneration;
    std::uint32_t resourceGeneration;
    std::uint32_t recoveryCount;
    std::uint32_t drawCount;
    bool retained;
    bool resident;
    bool firstDrawCompleted;
};

struct WebRendererSceneViewDesc
{
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t width;
    std::uint32_t height;
    float tanHalfFovX;
    float tanHalfFovY;
    float viewOrigin[3];
    float viewAxis[3][3];
    // Canonical Kisak row-vector view * projection matrix. WebGL receives the
    // same contiguous values as a column-major uniform, which supplies the
    // transpose required by GLSL's matrix * column-vector convention.
    float viewProjectionMatrix[4][4];
    // Native renderFxFlags bit 1 selects a smaller near clip and a reserved
    // depth range for first-person models. Keep the alternate projection at
    // the renderer boundary so ordinary scene geometry remains unchanged.
    float depthHackViewProjectionMatrix[4][4];
    std::int32_t time;
    float zNear;
    // Canonical frame fog after R_UpdateFrameFog interpolation. Color is
    // unpacked from GfxFog's BGRA byte layout at the frontend boundary.
    float fogColor[4];
    float fogStart;
    float fogDensity;
    bool fogEnabled;
    // Canonical directional light selected by R_InitPrimaryLights. The WebGL
    // backend owns shadow-map allocation and API depth conversion, while the
    // light identity and world bounds remain frontend/DB data.
    float sunDirection[3];
    float sunColor[3];
    float worldMins[3];
    float worldMaxs[3];
    bool sunShadowEnabled;
    // Canonical CONST_SRC_CODE_COLOR_* values calculated by the renderer
    // frontend from the active campaign vision set. Film is applied to the
    // resolved 3D scene before 2D; display gamma is applied after 2D.
    float colorBias[4];
    float colorTintBase[4];
    float colorTintDelta[4];
    float displayGammaExponent;
    bool filmEnabled;
    std::int32_t localClientNum;
    const char *worldName;
    std::uint32_t worldSurfaceCount;
    std::uint32_t worldVertexCount;
    std::uint32_t worldIndexCount;
    bool geometrySubmitted;
};

// Static opaque-world command at the renderer backend boundary. Canonical BSP
// surface indices are 16-bit local values plus firstVertex; combining many
// surfaces into one fallback-material draw therefore requires 32-bit indices.
// These limits are independent of the small retained Gate 2/bootstrap oracle.
constexpr std::uint32_t WEB_RENDERER_MAX_WORLD_VERTICES = 1'000'000u;
constexpr std::uint32_t WEB_RENDERER_MAX_WORLD_INDICES = 3'000'000u;
constexpr std::uint32_t WEB_RENDERER_MAX_STATIC_MODEL_VERTICES = 500'000u;
constexpr std::uint32_t WEB_RENDERER_MAX_STATIC_MODEL_INDICES = 1'500'000u;
constexpr std::uint32_t WEB_RENDERER_MAX_STATIC_MODEL_INSTANCES = 20'000u;
constexpr std::uint32_t WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES = 250'000u;
constexpr std::uint32_t WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES = 500'000u;
// Native GfxScene::sceneDObj is a fixed 512-entry array. Keep the same bound
// at the browser frontend seam so ordinary entity DObjs are not silently
// limited to a smaller browser-only subset.
constexpr std::uint32_t WEB_RENDERER_MAX_DYNAMIC_DOBJ_SUBMISSIONS = 512u;
// Native GfxScene::sceneBrush is a separate fixed 512-entry array.
constexpr std::uint32_t WEB_RENDERER_MAX_DYNAMIC_BMODEL_SUBMISSIONS = 512u;
constexpr std::uint32_t WEB_RENDERER_MAX_UI_VERTICES = 65'536u;
constexpr std::uint32_t WEB_RENDERER_MAX_UI_INDICES = 98'304u;

struct WebRendererModelLightingAtlasDesc
{
    const std::uint8_t *pixels;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t depth;
    std::uint32_t entryCount;
    std::size_t byteLength;
};

struct WebRendererWorldSurfaceDesc
{
    const WebRendererSurfaceVertex *vertices;
    std::uint32_t vertexCount;
    const std::uint32_t *indices;
    std::uint32_t indexCount;
    const struct WebRendererWorldBatchDesc *batches;
    std::uint32_t batchCount;
    const WebRendererModelLightingAtlasDesc *modelLightingAtlas;
};

enum class WebRendererWorldTechnique : std::uint8_t
{
    BackendFallback = 0,
    BaseTexture,
    BaseTextureLightmap,
    BaseTextureLightmapNormal,
    VertexColorMultiply,
    VertexColorAdditive,
    // Portable subset of IW3's reflexsight shader. Its DXT1 color texture is
    // intentionally opaque; source opacity is reconstructed from intensity.
    ReflexSight,
};

constexpr bool WebRenderer_UsesSecondaryDirectionalLightmap(
    WebRendererWorldTechnique technique) noexcept
{
    return technique == WebRendererWorldTechnique::BaseTextureLightmap ||
        technique == WebRendererWorldTechnique::BaseTextureLightmapNormal;
}

constexpr bool WebRenderer_UsesWorldNormalMap(
    WebRendererWorldTechnique technique) noexcept
{
    return technique == WebRendererWorldTechnique::BaseTextureLightmapNormal;
}

constexpr bool WebRenderer_UsesColorIntensityOpacity(
    WebRendererWorldTechnique technique) noexcept
{
    return technique == WebRendererWorldTechnique::ReflexSight;
}

// Portable description of the native pixel-lighting path selected by the
// canonical material pass. This is intent data, not a WebGL shader handle.
enum class WebRendererWorldLightingMode : std::uint8_t
{
    None = 0,
    SecondaryDirectional,
    ModelLightGrid,
};

enum class WebRendererSceneBatchKind : std::uint8_t
{
    WorldSurface = 0,
    StaticXModel,
    DynamicDObj,
    DynamicXModel,
    DynamicBModel,
    FxCodeMesh,
    FxXModel,
    FxParticleCloud,
    FxMarkMesh,
};

constexpr bool WebRenderer_IsFxVertexColorBatch(
    WebRendererSceneBatchKind kind) noexcept
{
    return kind == WebRendererSceneBatchKind::FxCodeMesh ||
        kind == WebRendererSceneBatchKind::FxXModel ||
        kind == WebRendererSceneBatchKind::FxParticleCloud ||
        kind == WebRendererSceneBatchKind::FxMarkMesh;
}

constexpr std::size_t WebRenderer_FxDiagnosticIndex(
    WebRendererSceneBatchKind kind) noexcept
{
    switch (kind)
    {
    case WebRendererSceneBatchKind::FxCodeMesh: return 0u;
    case WebRendererSceneBatchKind::FxXModel: return 1u;
    case WebRendererSceneBatchKind::FxParticleCloud: return 2u;
    case WebRendererSceneBatchKind::FxMarkMesh: return 3u;
    default: return 4u;
    }
}

// Callback-scoped canonical identity and portable first-pass state for one
// contiguous world draw. WebRenderer_SetWorldSurface copies names/state and
// consumes the DB-owned image load definitions before returning.
struct WebRendererWorldBatchDesc
{
    std::uint32_t firstIndex;
    std::uint32_t indexCount;
    std::uint32_t surfaceCount;
    std::uint32_t firstSurfaceIndex;
    std::uint32_t lastSurfaceIndex;
    const Material *materialIdentity;
    const char *materialName;
    const XModel *modelIdentity;
    const char *modelName;
    std::uint32_t firstInstanceIndex;
    std::uint32_t lastInstanceIndex;
    const GfxImage *baseImage;
    const GfxImage *normalImage;
    const GfxImage *lightmapImage;
    const GfxImage *secondaryLightmapImage;
    std::uint32_t stateBits[2];
    std::uint8_t samplerState;
    std::uint8_t normalSamplerState;
    std::uint8_t lightmapIndex;
    WebRendererSceneBatchKind sourceKind;
    WebRendererWorldTechnique technique;
    WebRendererWorldLightingMode lightingMode;
    // Canonical frontend technique identity. The backend copies the name and
    // numeric slot for diagnostics; it never retains the technique pointer or
    // treats the name as a browser shader identifier.
    const char *techniqueName;
    std::uint8_t techniqueType;
    std::uint8_t customSamplerFlags;
    std::uint16_t techniqueFlags;
    bool depthHack;
    // Canonical GfxWorldDpvsStatic::surfaceCastsSunShadow membership. This is
    // frontend visibility intent, not a backend material heuristic.
    bool castsSunShadow;
    const char *pixelShaderName;
    std::uint32_t pixelShaderProgramHash;
    // CONST_SRC_CODE_BASE_LIGHTING_COORDS for non-instanced DObj draws.
    // Static XModels carry the same value per instance below.
    float modelLightingCoordinates[3];
};

// Static XModel geometry remains shared per canonical XModel/LOD. Placements
// are carried separately so the WebGL2 backend can issue instanced draws
// without duplicating the 12k Killhouse world-model meshes.
struct WebRendererStaticModelInstanceDesc
{
    float axis[3][3];
    float origin[3];
    float modelLightingCoordinates[3];
    std::uint32_t canonicalInstanceIndex;
};

struct WebRendererStaticModelBatchDesc
{
    WebRendererWorldBatchDesc draw;
    std::uint32_t instanceOffset;
    std::uint32_t instanceCount;
};

struct WebRendererStaticModelSceneDesc
{
    const WebRendererSurfaceVertex *vertices;
    std::uint32_t vertexCount;
    const std::uint32_t *indices;
    std::uint32_t indexCount;
    const WebRendererStaticModelInstanceDesc *instances;
    std::uint32_t instanceCount;
    const WebRendererStaticModelBatchDesc *batches;
    std::uint32_t batchCount;
    std::uint32_t modelCount;
    std::uint32_t surfaceCount;
    const WebRendererModelLightingAtlasDesc *modelLightingAtlas;
};

struct WebRendererUiBatchDesc
{
    std::uint32_t firstIndex;
    std::uint32_t indexCount;
    const Material *materialIdentity;
    const char *materialName;
    const GfxImage *image;
    std::uint8_t samplerState;
    bool hasMaterialState;
    std::uint32_t stateBits[2];
    float color[4];
};

struct WebRendererUiSceneDesc
{
    const WebRendererSurfaceVertex *vertices;
    std::uint32_t vertexCount;
    const std::uint32_t *indices;
    std::uint32_t indexCount;
    const WebRendererUiBatchDesc *batches;
    std::uint32_t batchCount;
};

enum class WebRendererShaderResult : std::uint8_t
{
    Success = 0,
    InvalidDescriptor,
    UnsupportedSubstitution,
    AllocationFailed,
    BackendFailure,
};

enum class WebRendererTextureResult : std::uint8_t
{
    Success = 0,
    InvalidDescriptor,
    UnsupportedDimensions,
    OutputTooLarge,
    AllocationFailed,
    BackendFailure,
};

const char *WebRenderer_TextureResultString(WebRendererTextureResult result) noexcept;
const char *WebRenderer_ShaderResultString(WebRendererShaderResult result) noexcept;

// Validates and copies one callback-scoped indexed surface plus its draw. The
// renderer retains only bounded backend-neutral values, never the caller's
// pointers. The previous surface remains active if validation, allocation, or
// immediate backend upload fails.
WebRendererSurfaceResult WebRenderer_SetSurface(
    const WebRendererSurfaceDesc &surface,
    const WebRendererDrawDesc &draw);

// Creates the browser renderer and backend resources from any retained surface
// and texture descriptions.
bool WebRenderer_Initialize();

// Copies a tightly packed RGBA8 image into bounded renderer-owned recovery
// storage and displays it on a submitted surface that requests the engine-image
// binding. Pixel rows are ordered from top to bottom. The previous image remains
// active if validation or an immediate backend upload fails.
WebRendererTextureResult WebRenderer_SetBootstrapTexture(
    const WebRendererRgba8TextureDesc &texture);

// Returns submitted surfaces to their vertex-color fallback and releases
// any imported recovery pixels. This is used when an asset generation is
// cancelled or replaced so stale content cannot remain visible.
bool WebRenderer_ClearBootstrapTexture();

// Reports only backend-neutral ownership/residency information. A retained
// texture can be non-resident while the browser WebGL context is lost.
WebRendererTextureState WebRenderer_GetBootstrapTextureState();
// Retains one registry-owned WebGL2 compatibility program and atomically
// replaces its GPU program when a context is available. Imported files select
// the stable ID only; shader source always comes from compiled-in port code.
WebRendererShaderResult WebRenderer_SetShaderCompatibility(
    const kisak::web::WebGL2ShaderSubstitution &substitution);

// Drops the retail compatibility program and returns drawing to the bootstrap
// pipeline. Context-loss recovery never keeps a stale imported generation.
bool WebRenderer_ClearShaderCompatibility();

WebRendererShaderState WebRenderer_GetShaderCompatibilityState();

// Accepts the backend-neutral view command produced by the canonical renderer
// frontend. This does not select or convert world geometry; that remains a
// renderer-frontend responsibility and is deliberately separate from the
// frozen bounded world-surface adapter.
bool WebRenderer_SubmitSceneView(const WebRendererSceneViewDesc &view);

// Retains the canonical GfxWorld sky cubemap at the renderer/backend boundary.
// The DB image identity remains canonical; only decoded face pixels and the
// WebGL cubemap object are backend-owned.
WebRendererTextureResult WebRenderer_SetSkyImage(
    const GfxImage *image,
    std::uint8_t samplerState);

// Atomically retains and uploads the renderer-frontend material-aware world
// command. Geometry uses 32-bit indices; each batch preserves canonical
// Material/GfxImage identity while GPU objects and fallback programs remain
// private to the WebGL2 backend.
WebRendererSurfaceResult WebRenderer_SetWorldSurface(
    const WebRendererWorldSurfaceDesc &surface);

// Retains shared canonical XSurface meshes plus immutable GfxWorld placements
// and uploads them as an instanced static-model pass.
WebRendererSurfaceResult WebRenderer_SetStaticModelScene(
    const WebRendererStaticModelSceneDesc &scene);

// Replaces the current per-frame DObj surface command. Geometry has already
// been posed/skinned by the canonical frontend; canonical model/material/image
// identities remain attached to each batch.
WebRendererSurfaceResult WebRenderer_SetDynamicModelScene(
    const WebRendererWorldSurfaceDesc &scene);

// Replaces the current frame's canonical 2D material/text command stream.
WebRendererSurfaceResult WebRenderer_SetUiScene(
    const WebRendererUiSceneDesc &scene);

// Draws one non-blocking browser frame. Engine work remains outside this seam.
void WebRenderer_DrawFrame(const WebFrameInfo &frame);
