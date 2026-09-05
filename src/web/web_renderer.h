#pragma once

#include <web/web_renderer_surface.h>
#include <web/web_renderer_lighting.h>

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <vector>

struct WebFrameInfo;
struct GfxImage;
struct Material;
struct XModel;
struct water_t;

// The reference clip repeats continuously at roughly 1 Hz, without a hold.
// Use canonical scene time so pause, timescale and saved games remain coherent.
inline float WebRenderer_PickupSheen(float seconds) noexcept
{
    if (!std::isfinite(seconds)) return 0.0f;
    const float phase = std::fmod(seconds, 1.0f);
    return 0.5f - 0.5f * std::cos(phase * 6.28318530718f);
}

struct WebRendererPrimaryLightDesc;
struct GfxLight;

enum class WebRendererShadowEntityKind : std::uint8_t
{
    None = 0u,
    SceneEntity,
    DynEntModel,
    DynEntBrush,
};

using WebRendererDynamicShadowVisibility = bool (*)(
    WebRendererShadowEntityKind kind, std::uint32_t entityId,
    std::uint32_t localClientNum,
    std::uint32_t primaryLightIndex) noexcept;

struct WebRendererSceneViewDesc
{
    // Completed canonical camera slot 0 for this exact view. An empty mask is
    // valid; unavailable computation uses conservative camera submission.
    const std::uint8_t *staticModelVisibility = nullptr;
    std::uint32_t staticModelVisibilityCount = 0u;
    bool staticModelVisibilityComputed = false;
    // World geometry requires a completed result; unavailable is rejected.
    const std::uint8_t *worldSurfaceVisibility = nullptr;
    std::uint32_t worldSurfaceVisibilityCount = 0u;
    bool worldSurfaceVisibilityComputed = false;

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
    bool filmEnabled;
    // Canonical GfxGlow after R_SetGlowInfo's tweak and renderer-dvar gates.
    // The backend owns the quarter-resolution filter targets; these values
    // remain authored campaign vision data rather than a browser effect model.
    float glowBloomCutoff;
    float glowBloomCutoffRescale;
    float glowBloomDesaturation;
    float glowBloomIntensity;
    float glowRadius;
    bool glowEnabled;
    // Canonical GfxDepthOfField after R_SetDepthOfField's tweak/enable gate.
    WebRendererDepthOfFieldSettings depthOfField;
    float depthHackZNear;
    // Canonical refdef blur radius. Native applies this to the resolved 3D
    // scene before the HUD, so the backend must not blur 2D commands.
    float blurRadius;
    std::int32_t localClientNum;
    const char *worldName;
    std::uint32_t worldSurfaceCount;
    std::uint32_t worldVertexCount;
    std::uint32_t worldIndexCount;
    bool geometrySubmitted;
    // Native consumes refdef.primaryLights for every scene, after cgame has
    // applied scripted color, radius, direction, and position changes. The
    // world command owns immutable geometry and attenuation images; this
    // frame payload keeps its light constants synchronized with cgame.
    const WebRendererPrimaryLightDesc *primaryLights = nullptr;
    std::uint32_t primaryLightCount = 0u;
    const GfxLight *const *dynamicLights = nullptr;
    std::uint32_t dynamicLightCount = 0u;
    const GfxImage *dynamicLightAttenuation = nullptr;
    // Native gives only added light zero an emissive spot shadow. Preserve its
    // identity after camera culling and importance selection; UINT32_MAX means
    // the canonical first light is absent from this view.
    std::uint32_t dynamicSpotLightIndex = UINT32_MAX;
    float dynamicSpotLightNearPlaneOffset = 0.0f;
    WebRendererDynamicShadowVisibility dynamicShadowVisibility = nullptr;
};

// Static opaque-world command at the renderer backend boundary. Canonical BSP
// surface indices are 16-bit local values plus firstVertex; combining many
// surfaces into one fallback-material draw therefore requires 32-bit indices.
// These limits are independent of the small retained Gate 2/bootstrap oracle.
constexpr std::uint32_t WEB_RENDERER_MAX_WORLD_VERTICES = 1'000'000u;
constexpr std::uint32_t WEB_RENDERER_MAX_WORLD_INDICES = 3'000'000u;
// The all-authored-LOD command retains each canonical XSurface only once.
// One million vertices / three million indices bounds that immutable geometry
// at roughly 81 MiB. Native IW3 permits up to 65,536 static model instances;
// preserve that canonical cardinality at the portable renderer boundary.
constexpr std::uint32_t WEB_RENDERER_MAX_STATIC_MODEL_VERTICES = 1'000'000u;
constexpr std::uint32_t WEB_RENDERER_MAX_STATIC_MODEL_INDICES = 3'000'000u;
constexpr std::uint32_t WEB_RENDERER_MAX_STATIC_MODEL_INSTANCES = 65'536u;
// AC130's authored DObjs plus retained brushes exceed the former 250k/500k
// scene cap before FX. Keep logical brush occupancy and bound the complete
// dynamic command at 40 MB (72-byte vertices plus 32-bit indices).
constexpr std::uint32_t WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES = 500'000u;
constexpr std::uint32_t WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES = 1'000'000u;
// Native GfxScene::sceneDObj is a fixed 512-entry array. Keep the same bound
// at the browser frontend seam so ordinary entity DObjs are not silently
// limited to a smaller browser-only subset.
constexpr std::uint32_t WEB_RENDERER_MAX_DYNAMIC_DOBJ_SUBMISSIONS = 512u;
// Native GfxScene::sceneBrush is a separate fixed 512-entry array.
constexpr std::uint32_t WEB_RENDERER_MAX_DYNAMIC_BMODEL_SUBMISSIONS = 512u;
constexpr std::uint32_t WEB_RENDERER_MAX_UI_VERTICES = 65'536u;
constexpr std::uint32_t WEB_RENDERER_MAX_UI_INDICES = 98'304u;
constexpr std::uint32_t WEB_RENDERER_MAX_PRIMARY_LIGHTS = 255u;

struct WebRendererModelLightingAtlasDesc
{
    const std::uint8_t *pixels;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t depth;
    std::uint32_t entryCount;
    std::size_t byteLength;
};

// Canonical primary-light state. Attenuation identity is retained with the
// world command, while the remaining fields are refreshed from the current
// refdef every scene because scripts can animate or disable primary lights.
struct WebRendererPrimaryLightDesc
{
    const GfxImage *attenuationImage = nullptr;
    float color[3];
    float direction[3];
    float origin[3];
    float radius;
    float cosHalfFovOuter;
    float cosHalfFovInner;
    // Native copies every light definition's 1D attenuation curve into the
    // secondary lightmap atlas and samples it with this normalized placement.
    float falloffScale;
    float falloffShift;
    std::uint8_t type;
    std::uint8_t exponent;
    std::uint8_t canUseShadowMap;
    std::uint8_t attenuationSamplerState;
};

// One canonical BSP surface selected by GfxWorld::shadowGeom for a local
// primary light. The range points into the portable world index buffer and
// batchIndex preserves the material state needed by alpha-tested casters.
struct WebRendererSpotShadowCasterDesc
{
    std::uint32_t primaryLightIndex;
    std::uint32_t firstIndex;
    std::uint32_t indexCount;
    std::uint32_t batchIndex;
    std::uint32_t stateBits0;
};

struct WebRendererSpotShadowStaticModelDesc
{
    std::uint32_t primaryLightIndex;
    std::uint32_t canonicalInstanceIndex;
};

// Immutable index-buffer spans preserve canonical surface identity after batching.
// Camera runs reference these spans; shadow passes retain the original batches.
struct WebRendererWorldSurfaceRange
{
    std::uint32_t canonicalSurfaceIndex;
    std::uint32_t batchIndex;
    std::uint32_t firstIndex;
    std::uint32_t indexCount;
    float mins[3];
    float maxs[3];
    bool cameraSurface = true;
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
    const WebRendererPrimaryLightDesc *primaryLights = nullptr;
    std::uint32_t primaryLightCount = 0u;
    std::uint32_t sunPrimaryLightIndex = 0u;
    const WebRendererSpotShadowCasterDesc *spotShadowCasters = nullptr;
    std::uint32_t spotShadowCasterCount = 0u;
    const WebRendererSpotShadowStaticModelDesc *spotShadowStaticModels =
        nullptr;
    std::uint32_t spotShadowStaticModelCount = 0u;
    const WebRendererWorldSurfaceRange *surfaceRanges = nullptr;
    std::uint32_t surfaceRangeCount = 0u;
    std::uint32_t canonicalSurfaceCount = 0u;
    // Immutable code-image inputs owned by the canonical GfxWorld. Dynamic
    // particle clouds reference these after the world geometry command has
    // been retained; browser-generated pixels remain behind GfxImage.
    const GfxImage *outdoorImage = nullptr;
    float outdoorLookupMatrix[4][4]{};
};

enum class WebRendererWorldTechnique : std::uint8_t
{
    BackendFallback = 0,
    BaseTexture,
    BaseTextureLightmap,
    BaseTextureLightmapNormal,
    // Canonical shader-model-3 lightmapped material families. These retain
    // the authored semantic-8 specular map and per-surface reflection probe
    // instead of collapsing the pass to its shader-model-2 diffuse subset.
    BaseTextureLightmapSpecular,
    BaseTextureLightmapNormalSpecular,
    // Canonical lp_*s0_sm3 XModel passes. These use the model-light-grid
    // volume rather than a world lightmap, but share the authored semantic-8
    // specular/reflection-probe environment term.
    BaseTextureSpecular,
    BaseTextureNormalSpecular,
    VertexColorMultiply,
    VertexColorAdditive,
    // Native vertcol_simple_fog_df vertex program. It attenuates authored
    // vertex color/alpha between two material colors by camera distance
    // before the ordinary fogged texture pass and canonical blend state.
    VertexColorDistanceFalloff,
    // Canonical water_l_sun pass: animated FFT height field, reflection
    // probe, Fresnel water color, sun specular, and fog.
    WaterLitSun,
    // Portable subset of IW3's reflexsight shader. Its DXT1 color texture is
    // intentionally opaque; source opacity is reconstructed from intensity.
    ReflexSight,
    // Native R_SetupMaterial skips the complete material group when the
    // primary-light-selected technique is absent. Retain that negative
    // selection explicitly so the backend does not invent fallback geometry.
    NativeTechniqueUnavailable,
    Cinematic,
    // mul.hlsl followed by mul_fog.hlsl with per-pass authored state.
    VertexColorMultiplyFog,
};

constexpr bool WebRenderer_SkipsNativeDraw(
    WebRendererWorldTechnique technique) noexcept
{
    return technique ==
        WebRendererWorldTechnique::NativeTechniqueUnavailable;
}

constexpr bool WebRenderer_UsesSecondaryDirectionalLightmap(
    WebRendererWorldTechnique technique) noexcept
{
    return technique == WebRendererWorldTechnique::BaseTextureLightmap ||
        technique == WebRendererWorldTechnique::BaseTextureLightmapNormal ||
        technique == WebRendererWorldTechnique::BaseTextureLightmapSpecular ||
        technique ==
            WebRendererWorldTechnique::BaseTextureLightmapNormalSpecular;
}

constexpr bool WebRenderer_UsesWorldNormalMap(
    WebRendererWorldTechnique technique) noexcept
{
    return technique == WebRendererWorldTechnique::BaseTextureLightmapNormal ||
        technique ==
            WebRendererWorldTechnique::BaseTextureLightmapNormalSpecular ||
        technique == WebRendererWorldTechnique::BaseTextureNormalSpecular;
}

constexpr bool WebRenderer_UsesWorldSpecularMap(
    WebRendererWorldTechnique technique) noexcept
{
    return technique == WebRendererWorldTechnique::BaseTextureLightmapSpecular ||
        technique ==
            WebRendererWorldTechnique::BaseTextureLightmapNormalSpecular ||
        technique == WebRendererWorldTechnique::BaseTextureSpecular ||
        technique == WebRendererWorldTechnique::BaseTextureNormalSpecular;
}

constexpr bool WebRenderer_UsesModelEnvironmentSpecular(
    WebRendererWorldTechnique technique) noexcept
{
    return technique == WebRendererWorldTechnique::BaseTextureSpecular ||
        technique == WebRendererWorldTechnique::BaseTextureNormalSpecular;
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
    SunSprite,
    SunFlare,
};

// Backend-owned brush mesh handle plus the current canonical rigid placement.
// Handles are valid only until world retirement; no game/entity state is retained.
struct WebRendererBrushModelInstanceDesc
{
    std::uint32_t geometryIndex;
    float axis[3][3];
    float origin[3];
    // Current canonical GfxBrushModel::writable world bounds, independent of
    // the retained mesh and its transformed shadow-caster bounds.
    float receiverMins[3]{};
    float receiverMaxs[3]{};
    WebRendererShadowEntityKind shadowEntityKind =
        WebRendererShadowEntityKind::None;
    std::uint32_t shadowEntityId = UINT32_MAX;
};

constexpr bool WebRenderer_IsCameraVisibleXModelSurface(
    WebRendererSceneBatchKind kind, std::uint8_t cameraRegion) noexcept
{
    const bool cameraXModel =
        kind == WebRendererSceneBatchKind::StaticXModel ||
        kind == WebRendererSceneBatchKind::DynamicDObj;
    return !cameraXModel || cameraRegion != 3u;
}

constexpr bool WebRenderer_IsFxVertexColorBatch(
    WebRendererSceneBatchKind kind) noexcept
{
    return kind == WebRendererSceneBatchKind::FxCodeMesh ||
        kind == WebRendererSceneBatchKind::FxXModel ||
        kind == WebRendererSceneBatchKind::FxParticleCloud ||
        kind == WebRendererSceneBatchKind::FxMarkMesh ||
        kind == WebRendererSceneBatchKind::SunSprite ||
        kind == WebRendererSceneBatchKind::SunFlare;
}

constexpr bool WebRenderer_IsTransientLightReceiver(
    WebRendererSceneBatchKind kind) noexcept
{
    return kind == WebRendererSceneBatchKind::DynamicDObj ||
        kind == WebRendererSceneBatchKind::DynamicXModel ||
        kind == WebRendererSceneBatchKind::DynamicBModel ||
        kind == WebRendererSceneBatchKind::FxXModel;
}

constexpr bool WebRenderer_IsSunBillboardBatch(
    WebRendererSceneBatchKind kind) noexcept
{
    return kind == WebRendererSceneBatchKind::SunSprite ||
        kind == WebRendererSceneBatchKind::SunFlare;
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
    const GfxImage *detailImage;
    const GfxImage *normalImage;
    const GfxImage *specularImage;
    const GfxImage *lightmapImage;
    const GfxImage *secondaryLightmapImage;
    const water_t *water;
    const GfxImage *reflectionProbeImage;
    std::uint32_t stateBits[2];
    std::uint8_t samplerState;
    std::uint8_t detailSamplerState;
    std::uint8_t normalSamplerState;
    std::uint8_t specularSamplerState;
    std::uint8_t waterSamplerState;
    std::uint8_t reflectionProbeIndex;
    std::uint8_t lightmapIndex;
    // Canonical GfxDrawSurf primary-light selection for this surface group.
    // Zero is unlit, the world sun index selects the sun family, and higher
    // values select the matching spot/omni light retained with the world.
    std::uint8_t primaryLightIndex;
    WebRendererSceneBatchKind sourceKind;
    WebRendererShadowEntityKind shadowEntityKind =
        WebRendererShadowEntityKind::None;
    std::uint32_t shadowEntityId = UINT32_MAX;
    WebRendererWorldTechnique technique;
    WebRendererWorldLightingMode lightingMode;
    // Canonical frontend technique identity. The backend copies the name and
    // numeric slot for diagnostics; it never retains the technique pointer or
    // treats the name as a browser shader identifier.
    const char *techniqueName;
    std::uint8_t techniqueType;
    std::uint8_t customSamplerFlags;
    std::uint16_t techniqueFlags;
    // Native XModel camera region 3 is reserved for shadow-only geometry.
    // The backend keeps it resident for shadow draws but omits it from the
    // visible camera pass.
    std::uint8_t cameraRegion;
    bool depthHack;
    bool pickupSheen = false;
    // Native receiver-list surf type (SF_*). The backend combines this with
    // the canonical material key before R_ReverseSortDrawSurfs-equivalent
    // ordering; it never identifies or owns geometry.
    std::uint8_t dynamicLightSurfType;
    bool excludeTransientSpotLight;
    // Native sceneModel / DynEntity receiver sphere, copied at submission.
    // Radius -1 leaves other scene kinds on their existing receiver path.
    float transientLightSphere[4] = {0.0f, 0.0f, 0.0f, -1.0f};
    float transientLightMins[3]{};
    float transientLightMaxs[3]{};
    bool transientLightBoundsEnabled = false;
    // Native lp_amb_* adds model-lighting alpha times sun diffuse without
    // the normal-dependent term used by the other lp_* families.
    bool ambientProbeLighting;
    // Canonical GfxWorldDpvsStatic::surfaceCastsSunShadow membership. This is
    // frontend visibility intent, not a backend material heuristic.
    bool castsSunShadow;
    // Build-shadowmap technique/state availability. Dynamic spot BSP caster
    // membership is light-volume based and does not use the sun bitset.
    bool castsSpotShadow;
    // State from TECHNIQUE_BUILD_SHADOWMAP_DEPTH, kept separate from the
    // visible receiver technique selected for the camera pass.
    std::uint32_t shadowStateBits0;
    const char *vertexShaderName;
    std::uint32_t vertexShaderProgramHash;
    const char *pixelShaderName;
    std::uint32_t pixelShaderProgramHash;
    float envMapParms[4];
    float detailScale[4];
    float waterColor[4];
    float falloffParms[4];
    float falloffBeginColor[4];
    float falloffEndColor[4];
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
    float modelScale;
    float modelCullDistance;
    std::uint32_t canonicalInstanceIndex;
};
static_assert(sizeof(WebRendererStaticModelInstanceDesc) == 72u);

// CPU-only canonical bounds for light-space partition selection. They stay
// outside the instanced GPU payload and camera DPVS packing.
struct WebRendererStaticModelShadowBounds
{
    float mins[3];
    float maxs[3];
};
static_assert(sizeof(WebRendererStaticModelShadowBounds) == 24u);

struct WebRendererStaticModelBatchDesc
{
    WebRendererWorldBatchDesc draw;
    std::uint32_t instanceOffset;
    std::uint32_t instanceCount;
    std::uint8_t lodIndex;
};

struct WebRendererStaticModelSceneDesc
{
    const WebRendererSurfaceVertex *vertices;
    std::uint32_t vertexCount;
    const std::uint32_t *indices;
    std::uint32_t indexCount;
    const WebRendererStaticModelInstanceDesc *instances;
    std::uint32_t instanceCount;
    const WebRendererStaticModelShadowBounds *shadowBounds;
    std::uint32_t shadowBoundsCount;
    const WebRendererStaticModelBatchDesc *batches;
    std::uint32_t batchCount;
    std::uint32_t modelCount;
    std::uint32_t surfaceCount;
    const WebRendererModelLightingAtlasDesc *modelLightingAtlas;
};

enum class WebRendererUiCommand : std::uint8_t
{
    Draw,
    SaveScreen,
    ShellShockBlurred,
    ShellShockFlashed,
};

// Ordered framebuffer operations at the 2D renderer boundary. Effect lifetime
// and intensities remain in canonical cg_shellshock; no gameplay state lives here.
struct WebRendererSavedScreenCommand
{
    WebRendererUiCommand command = WebRendererUiCommand::Draw;
    std::uint32_t timerId = 0u;
    int sceneTime = 0;
    int fadeMsec = 0;
    float region[4]{0.0f, 0.0f, 1.0f, 1.0f};
};

struct WebRendererUiBatchDesc
{
    WebRendererSavedScreenCommand savedScreen;
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
void WebRenderer_Shutdown();
void WebRenderer_UnloadWorldResources();

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
// identities remain attached to each batch. Retained brush placements are
// inserted before brushInsertBatch without occupying the streamed vertex buffer.
WebRendererSurfaceResult WebRenderer_SetDynamicModelScene(
    const WebRendererWorldSurfaceDesc &scene,
    const WebRendererBrushModelInstanceDesc *brushInstances = nullptr,
    std::uint32_t brushInstanceCount = 0u,
    std::uint32_t brushInsertBatch = 0u);

// Transfers final per-frame vertex/index storage into the backend after the
// same descriptor validation. On failure, both source vectors are restored.
WebRendererSurfaceResult WebRenderer_SetDynamicModelSceneOwned(
    const WebRendererWorldSurfaceDesc &scene,
    std::vector<WebRendererSurfaceVertex> &vertices,
    std::vector<std::uint32_t> &indices,
    const WebRendererBrushModelInstanceDesc *brushInstances = nullptr,
    std::uint32_t brushInstanceCount = 0u,
    std::uint32_t brushInsertBatch = 0u);

// Validated, immutable geometry/material resources owned until world retirement.
// Current transforms, visibility, and animated primary-light values are not cached.
WebRendererSurfaceResult WebRenderer_RetainBrushModelGeometry(
    const WebRendererWorldSurfaceDesc &geometry, std::uint32_t &geometryIndex);

// Replaces the current frame's canonical 2D material/text command stream.
WebRendererSurfaceResult WebRenderer_SetUiScene(
    const WebRendererUiSceneDesc &scene);

#if KISAK_WEB_DIAGNOSTICS
std::uint32_t WebRenderer_TestDrawPixel(int x, int y);
std::uint32_t WebRenderer_TestCinematicPixel(bool ui, int field);
#endif

// Draws one non-blocking browser frame. Engine work remains outside this seam.
// Display gamma applies after world effects and 2D, including menu-only frames.
bool WebRenderer_SetDisplayGamma(float gamma);
bool WebRenderer_DrawFrame(const WebFrameInfo &frame);

// Platform screenshot/image boundary; ownership and names remain in Kisak.
bool WebRenderer_ReadSaveGameShot(std::vector<std::uint8_t> &rgba, const char *mapName);
void WebSaveImage_CapturePending();
void WebSaveImage_CancelPending();
bool WebRenderer_SetRawUiImage(const GfxImage *image, const std::uint8_t *rgba,
    std::size_t byteLength);
bool WebRenderer_UpdateUiImage(const GfxImage *image, const std::uint8_t *rgba,
    std::size_t byteLength, unsigned components = 4);
void WebRenderer_ReleaseUiImage(const GfxImage *image);
