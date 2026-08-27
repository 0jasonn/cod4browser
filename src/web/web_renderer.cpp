#include <web/web_renderer.h>
#include <web/web_frame_profile.h>
#include <web/web_renderer_surface_storage.h>
#include <web/web_renderer_context.h>
#include <web/web_renderer_lod.h>
#include <web/web_renderer_world_scene.h>
#include <web/web_system.h>

#include <qcommon/qcommon.h>
#include <gfx_d3d/gfx_image_types.h>
#include <gfx_d3d/r_dvars.h>
#include <universal/q_shared.h>
#include <gfx_d3d/r_water.h>
#include <database/db_generated_image_platform.h>
#include <qcommon/iwi_image.h>
#include <universal/com_files.h>

#include <GLES3/gl3.h>
#include <emscripten.h>
#if KISAK_WEB_DIAGNOSTICS
#include <emscripten/heap.h>
#include <malloc.h>
#endif
#include <emscripten/html5.h>
#include <webgl/webgl1_ext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint32_t INVALID_WORLD_IMAGE = UINT32_MAX;
constexpr std::size_t MAX_SPOT_SHADOWS = 4u;
constexpr GLsizei SUN_SHADOW_SIZE = 1024;
constexpr GLsizei SPOT_SHADOW_SIZE = 512;
// Retail Killhouse's full static XModel base/normal/specular material set
// expands to roughly 766 MiB after canonical DXT textures cross the portable
// RGBA8 boundary. Keep decoded admission bounded just above that measured set
// while the recovery copy retains the much smaller canonical encoded source.
constexpr std::size_t WEB_RENDERER_MAX_DECODED_TEXTURE_BYTES =
    800u * 1024u * 1024u;

enum class WebRendererImageRecoverySource : std::uint8_t
{
    DecodedRgba8,
    LoadDef,
    IwiMember,
};

struct WebRendererRetainedWorldImage
{
    const GfxImage *canonicalIdentity = nullptr;
    std::string canonicalName;
    std::vector<std::uint8_t> pixels;
    std::vector<std::vector<std::uint8_t>> mipPixels;
    std::vector<std::uint8_t> encodedSource;
    std::int32_t sourceFormat = 0;
    std::int16_t sourceDimensions[3]{};
    std::size_t decodedByteLength = 0u;
    std::size_t uploadByteLength = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    GLuint texture = 0u;
    WebRendererImageRecoverySource recoverySource =
        WebRendererImageRecoverySource::DecodedRgba8;
    std::uint8_t sourceFlags = 0u;
    bool mipmapsAllowed = true;
    bool authoredMipChain = false;
    bool supported = false;
};

struct WebRendererRetainedSkyImage
{
    const GfxImage *canonicalIdentity = nullptr;
    std::string canonicalName;
    kisak::iwi::Rgba8Cube cube;
    std::uint8_t samplerState = 0u;
    GLuint texture = 0u;
    bool active = false;
};

struct WebRendererRetainedWorldBatch
{
    std::uint32_t firstIndex = 0u;
    std::uint32_t indexCount = 0u;
    std::uint32_t surfaceCount = 0u;
    std::uint32_t firstSurfaceIndex = 0u;
    std::uint32_t lastSurfaceIndex = 0u;
    const Material *materialIdentity = nullptr;
    std::string materialName;
    const XModel *modelIdentity = nullptr;
    std::string modelName;
    std::uint32_t firstInstanceIndex = UINT32_MAX;
    std::uint32_t lastInstanceIndex = UINT32_MAX;
    std::uint32_t baseImageIndex = INVALID_WORLD_IMAGE;
    std::uint32_t detailImageIndex = INVALID_WORLD_IMAGE;
    std::uint32_t normalImageIndex = INVALID_WORLD_IMAGE;
    std::uint32_t specularImageIndex = INVALID_WORLD_IMAGE;
    std::uint32_t lightmapImageIndex = INVALID_WORLD_IMAGE;
    std::uint32_t secondaryLightmapImageIndex = INVALID_WORLD_IMAGE;
    std::uint32_t stateBits[2]{};
    std::uint8_t samplerState = 0u;
    std::uint8_t detailSamplerState = 0u;
    std::uint8_t normalSamplerState = 0u;
    std::uint8_t specularSamplerState = 0u;
    std::uint8_t lightmapIndex = 31u;
    std::uint8_t primaryLightIndex = 0u;
    WebRendererSceneBatchKind sourceKind =
        WebRendererSceneBatchKind::WorldSurface;
    WebRendererWorldTechnique technique =
        WebRendererWorldTechnique::BackendFallback;
    WebRendererWorldLightingMode lightingMode =
        WebRendererWorldLightingMode::None;
    std::string techniqueName;
    std::uint8_t techniqueType = 0xffu;
    std::uint8_t customSamplerFlags = 0u;
    std::uint16_t techniqueFlags = 0u;
    std::uint8_t cameraRegion = 0u;
    bool depthHack = false;
    bool ambientProbeLighting = false;
    bool castsSunShadow = false;
    std::uint32_t shadowStateBits0 = 0u;
    std::string vertexShaderName;
    std::uint32_t vertexShaderProgramHash = 0u;
    std::string pixelShaderName;
    std::uint32_t pixelShaderProgramHash = 0u;
    float modelLightingCoordinates[3]{};
    std::vector<complex_s> waterH0;
    std::vector<float> waterWTerm;
    std::vector<std::uint8_t> waterPixels;
    std::int32_t waterM = 0;
    std::int32_t waterN = 0;
    std::uint8_t waterSamplerState = 0u;
    std::uint8_t reflectionProbeIndex = 0u;
    float envMapParms[4]{};
    float detailScale[4]{};
    float waterColor[4]{};
    float falloffParms[4]{};
    float falloffBeginColor[4]{};
    float falloffEndColor[4]{};
    kisak::iwi::Rgba8Cube reflectionCube;
    GLuint waterTexture = 0u;
    GLuint reflectionTexture = 0u;
    bool ownsReflectionTexture = false;
    float waterTextureTime = std::numeric_limits<float>::quiet_NaN();
};

struct WebRendererRetainedPrimaryLight
{
    float color[3]{};
    float direction[3]{};
    float origin[3]{};
    float radius = 0.0f;
    float cosHalfFovOuter = 0.0f;
    float cosHalfFovInner = 0.0f;
    float falloffScale = 0.0f;
    float falloffShift = 0.0f;
    std::uint8_t type = 0u;
    std::uint8_t exponent = 0u;
    bool canUseShadowMap = false;
};

using WebRendererRetainedSpotShadowCaster =
    WebRendererSpotShadowCasterDesc;
using WebRendererRetainedSpotShadowStaticModel =
    WebRendererSpotShadowStaticModelDesc;

struct WebRendererRetainedModelLightingAtlas
{
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t depth = 0u;
    std::uint32_t entryCount = 0u;
    GLuint texture = 0u;
};

struct WebRendererRetainedStaticModelBatch
{
    WebRendererRetainedWorldBatch draw;
    std::uint32_t sourceInstanceOffset = 0u;
    std::uint32_t sourceInstanceCount = 0u;
    std::uint32_t instanceOffset = 0u;
    std::uint32_t instanceCount = 0u;
    std::uint8_t lodIndex = 0u;
};

struct WebRendererRetainedUiBatch
{
    std::uint32_t firstIndex = 0u;
    std::uint32_t indexCount = 0u;
    const Material *materialIdentity = nullptr;
    std::string materialName;
    std::uint32_t imageIndex = INVALID_WORLD_IMAGE;
    std::uint8_t samplerState = 0u;
    bool hasMaterialState = false;
    std::uint32_t stateBits[2]{};
    float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
};

struct WebRendererState
{
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
    float maxTextureAnisotropy = 1.0f;
    bool textureAnisotropySupported = false;
    GLuint program = 0;
    GLuint skyProgram = 0;
    GLuint postProcessProgram = 0;
    GLuint glowProgram = 0;
    GLuint shadowProgram = 0;
    GLuint compatibilityProgram = 0;
    GLuint sceneFramebuffer = 0;
    GLuint sceneColorTexture = 0;
    GLuint sceneDepthTexture = 0;
    GLuint multisampleFramebuffer = 0;
    GLuint multisampleColorRenderbuffer = 0;
    GLuint multisampleDepthRenderbuffer = 0;
    GLuint compositeFramebuffer = 0;
    GLuint compositeColorTexture = 0;
    GLuint glowFramebuffers[2]{};
    GLuint glowColorTextures[2]{};
    GLuint shadowFramebuffer = 0;
    GLuint shadowDepthTexture = 0;
    GLuint shadowFarFramebuffer = 0;
    GLuint shadowFarDepthTexture = 0;
    std::array<GLuint, MAX_SPOT_SHADOWS> spotShadowFramebuffers{};
    std::array<GLuint, MAX_SPOT_SHADOWS> spotShadowDepthTextures{};
    GLuint sunVisibilityQueries[2]{};
    bool sunVisibilityQueryIssued[2]{};
    std::uint32_t sunVisibilityQueryIndex = 0u;
    float sunVisibility = 0.0f;
    float sunFlareIntensity = 0.0f;
    float sunBlindIntensity = 0.0f;
    float sunBlindDarken = 0.0f;
    float sunGlareIntensity = 0.0f;
    float sunGlareLighten = 0.0f;
    double sunEffectLastMilliseconds = 0.0;
    float sunFlareFadeInMilliseconds = 0.0f;
    float sunFlareFadeOutMilliseconds = 0.0f;
    float sunBlindLerp = 0.0f;
    float sunBlindMaxDarken = 0.0f;
    float sunBlindFadeInMilliseconds = 0.0f;
    float sunBlindFadeOutMilliseconds = 0.0f;
    float sunGlareLerp = 0.0f;
    float sunGlareMaxLighten = 0.0f;
    float sunGlareFadeInMilliseconds = 0.0f;
    float sunGlareFadeOutMilliseconds = 0.0f;
    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;
    GLuint indexBuffer = 0;
    GLuint texture = 0;
    GLint aspectUniform = -1;
    GLint textureUniform = -1;
    GLint textureEnabledUniform = -1;
    GLint detailMapUniform = -1;
    GLint detailMapEnabledUniform = -1;
    GLint detailScaleUniform = -1;
    GLint normalMapUniform = -1;
    GLint normalMapEnabledUniform = -1;
    GLint specularMapUniform = -1;
    GLint specularMapEnabledUniform = -1;
    GLint viewProjectionUniform = -1;
    GLint sceneFallbackUniform = -1;
    GLint lightmapEnabledUniform = -1;
    GLint secondaryLightmapUniform = -1;
    GLint secondaryLightmapEnabledUniform = -1;
    GLint modelLightingUniform = -1;
    GLint modelLightingEnabledUniform = -1;
    GLint modelLightingBaseCoordinatesUniform = -1;
    GLint modelLightingLookupScaleUniform = -1;
    GLint premultiplyAlphaUniform = -1;
    GLint colorIntensityAlphaUniform = -1;
    GLint materialModeUniform = -1;
    GLint falloffParmsUniform = -1;
    GLint falloffBeginColorUniform = -1;
    GLint falloffEndColorUniform = -1;
    GLint alphaTestUniform = -1;
    GLint instanceEnabledUniform = -1;
    GLint uiColorUniform = -1;
    GLint fogEnabledUniform = -1;
    GLint viewOriginUniform = -1;
    GLint fogColorUniform = -1;
    GLint fogParamsUniform = -1;
    GLint shadowMapUniform = -1;
    GLint shadowFarMapUniform = -1;
    GLint shadowMatrixUniform = -1;
    GLint shadowFarMatrixUniform = -1;
    GLint sunShadowEnabledUniform = -1;
    GLint sunDirectionUniform = -1;
    GLint sunColorUniform = -1;
    GLint primaryLightmapUniform = -1;
    GLint primaryLightFalloffPlacementUniform = -1;
    GLint primaryLightEnabledUniform = -1;
    GLint primaryLightPositionRadiusUniform = -1;
    GLint primaryLightDiffuseUniform = -1;
    GLint primaryLightSpotDirectionUniform = -1;
    GLint primaryLightSpotFactorsUniform = -1;
    GLint spotShadowMapUniform = -1;
    GLint spotShadowMatrixUniform = -1;
    GLint spotShadowEnabledUniform = -1;
    GLint waterMapUniform = -1;
    GLint reflectionProbeUniform = -1;
    GLint envMapParmsUniform = -1;
    GLint waterColorUniform = -1;
    GLint shadowDepthMatrixUniform = -1;
    GLint shadowDepthTextureUniform = -1;
    GLint shadowDepthTextureEnabledUniform = -1;
    GLint shadowDepthAlphaTestUniform = -1;
    GLint shadowDepthInstanceEnabledUniform = -1;
    GLint skyTextureUniform = -1;
    GLint skyTanHalfFovUniform = -1;
    GLint skyForwardUniform = -1;
    GLint skyRightUniform = -1;
    GLint skyUpUniform = -1;
    GLint postProcessTextureUniform = -1;
    GLint postProcessFilmEnabledUniform = -1;
    GLint postProcessColorBiasUniform = -1;
    GLint postProcessColorTintBaseUniform = -1;
    GLint postProcessColorTintDeltaUniform = -1;
    GLint postProcessGammaExponentUniform = -1;
    GLint postProcessBlurScaleUniform = -1;
    GLint postProcessBlindDarkenUniform = -1;
    GLint postProcessGlareLightenUniform = -1;
    GLint postProcessGlowTextureUniform = -1;
    GLint postProcessGlowIntensityUniform = -1;
    GLint postProcessDepthTextureUniform = -1;
    GLint postProcessDofEnabledUniform = -1;
    GLint postProcessDofViewModelUniform = -1;
    GLint postProcessDofNearUniform = -1;
    GLint postProcessDofFarUniform = -1;
    GLint postProcessDofDepthUniform = -1;
    GLint glowTextureUniform = -1;
    GLint glowModeUniform = -1;
    GLint glowTexelDeltaUniform = -1;
    GLint glowWeightsUniform = -1;
    GLint glowSetupUniform = -1;
    GLint compatibilityViewProjectionUniform = -1;
    GLint compatibilityWorldUniform = -1;
    GLint compatibilityTextureUniform = -1;
    int frameNumber = 0;
    int canvasWidth = 0;
    int canvasHeight = 0;
    int postProcessWidth = 0;
    int postProcessHeight = 0;
    int multisampleWidth = 0;
    int multisampleHeight = 0;
    int aaConfiguredSamples = -1;
    int aaRequestedSamples = 1;
    int aaActiveSamples = 1;
    int aaMaxSamples = 1;
    std::uint32_t aaResourceGeneration = 0u;
    std::uint32_t contextGeneration = 0u;
    bool contextLost = false;
    bool initialized = false;
    std::vector<WebRendererSurfaceVertex> retainedVertices;
    std::vector<std::uint16_t> retainedIndices;
    std::vector<std::uint32_t> retainedWorldIndices;
    std::vector<WebRendererRetainedWorldBatch> retainedWorldBatches;
    std::vector<WebRendererRetainedSpotShadowCaster>
        retainedWorldSpotShadowCasters;
    std::vector<WebRendererRetainedSpotShadowStaticModel>
        retainedWorldSpotShadowStaticModels;
    std::vector<WebRendererRetainedWorldImage> retainedWorldImages;
    std::vector<WebRendererRetainedPrimaryLight> retainedPrimaryLights;
    std::uint32_t retainedSunPrimaryLightIndex = 0u;
    WebRendererRetainedSkyImage retainedSky;
    GLuint staticModelVertexArray = 0u;
    GLuint staticModelVertexBuffer = 0u;
    GLuint staticModelIndexBuffer = 0u;
    GLuint staticModelInstanceBuffer = 0u;
    std::vector<WebRendererSurfaceVertex> retainedStaticModelVertices;
    std::vector<std::uint32_t> retainedStaticModelIndices;
    std::vector<WebRendererStaticModelInstanceDesc>
        retainedStaticModelSourceInstances;
    std::vector<WebRendererStaticModelInstanceDesc> retainedStaticModelInstances;
    std::vector<std::int8_t> retainedStaticModelSelectedLods;
    std::vector<WebRendererRetainedStaticModelBatch> retainedStaticModelBatches;
    std::vector<WebRendererRetainedWorldImage> retainedStaticModelImages;
    WebRendererRetainedModelLightingAtlas retainedStaticModelLighting;
    std::uint32_t staticModelCount = 0u;
    std::uint32_t staticModelSurfaceCount = 0u;
    bool staticModelSceneActive = false;
    GLuint dynamicModelVertexArray = 0u;
    GLuint dynamicModelVertexBuffer = 0u;
    GLuint dynamicModelIndexBuffer = 0u;
    std::vector<WebRendererSurfaceVertex> retainedDynamicModelVertices;
    std::vector<std::uint32_t> retainedDynamicModelIndices;
    std::vector<WebRendererRetainedWorldBatch> retainedDynamicModelBatches;
    std::vector<WebRendererRetainedWorldImage> retainedDynamicModelImages;
    WebRendererRetainedModelLightingAtlas retainedDynamicModelLighting;
    bool dynamicModelSceneActive = false;
    bool dynamicModelFirstSubmissionReported = false;
    bool dynamicFxSourceReported[4]{};
    GLuint uiVertexArray = 0u;
    GLuint uiVertexBuffer = 0u;
    GLuint uiIndexBuffer = 0u;
    std::vector<WebRendererSurfaceVertex> retainedUiVertices;
    std::vector<std::uint32_t> retainedUiIndices;
    std::vector<WebRendererRetainedUiBatch> retainedUiBatches;
    std::vector<WebRendererRetainedWorldImage> retainedUiImages;
    bool uiSceneActive = false;
    WebRendererDrawDesc draw{
        WebRendererPrimitiveTopology::TriangleList,
        0u,
        0u,
        WebRendererTextureBinding::None,
    };
    std::uint32_t surfaceSubmissionGeneration = 0;
    std::uint32_t surfaceDrawnSubmissionGeneration = 0;
    std::uint32_t surfaceResourceGeneration = 0;
    std::uint32_t surfaceRecoveryCount = 0;
    bool surfaceActive = false;
    bool worldSurfaceActive = false;
    std::vector<std::uint8_t> retainedPixels;
    std::uint32_t textureWidth = 0;
    std::uint32_t textureHeight = 0;
    std::uint8_t textureSamplerState = 0u;
    std::uint32_t uploadGeneration = 0;
    std::uint32_t rebuildGeneration = 0;
    std::uint32_t recoveryCount = 0;
    bool sourceTextureActive = false;
    std::string compatibilityId;
    std::string compatibilityVertexSource;
    std::string compatibilityFragmentSource;
    std::uint32_t compatibilityVertexSourceHash = 0u;
    std::uint32_t compatibilityFragmentSourceHash = 0u;
    std::uint32_t compatibilitySubmissionGeneration = 0u;
    std::uint32_t compatibilityResourceGeneration = 0u;
    std::uint32_t compatibilityRecoveryCount = 0u;
    std::uint32_t compatibilityDrawCount = 0u;
    bool compatibilityActive = false;
    bool compatibilityFirstDrawCompleted = false;
    std::uint32_t sceneViewSubmissionGeneration = 0u;
    std::uint32_t sceneViewSurfaceSubmissionGeneration = 0u;
    std::uint32_t sceneViewDrawnSubmissionGeneration = 0u;
    std::uint32_t sceneViewSurfaceCount = 0u;
    std::uint32_t sceneViewVertexCount = 0u;
    std::uint32_t sceneViewIndexCount = 0u;
    std::array<float, 16> sceneViewProjection{};
    std::array<float, 16> sceneDepthHackViewProjection{};
    std::array<float, 2> sceneTanHalfFov{};
    std::array<float, 9> sceneViewAxis{};
    std::array<float, 3> sceneViewOrigin{};
    std::array<float, 4> sceneFogColor{};
    std::array<float, 2> sceneFogParams{};
    std::array<float, 3> sceneSunDirection{};
    std::array<float, 3> sceneSunColor{};
    float sceneViewTimeSeconds = 0.0f;
    std::array<float, 3> sceneWorldMins{};
    std::array<float, 3> sceneWorldMaxs{};
    std::array<float, 16> sceneSunShadowMatrix{};
    std::array<float, 16> sceneSunShadowFarMatrix{};
    std::array<std::array<float, 16>, MAX_SPOT_SHADOWS>
        sceneSpotShadowMatrices{};
    std::array<std::uint32_t, MAX_SPOT_SHADOWS> sceneSpotShadowLightIndices{};
    std::uint32_t sceneSpotShadowCount = 0u;
    std::array<float, 4> sceneColorBias{};
    std::array<float, 4> sceneColorTintBase{};
    std::array<float, 4> sceneColorTintDelta{};
    float sceneDisplayGammaExponent = 1.0f;
    float sceneBlurRadius = 0.0f;
    float sceneGlowBloomCutoff = 0.0f;
    float sceneGlowBloomCutoffRescale = 0.0f;
    float sceneGlowBloomDesaturation = 0.0f;
    float sceneGlowBloomIntensity = 0.0f;
    float sceneGlowRadius = 0.0f;
    WebRendererDepthOfFieldSettings sceneDepthOfField{};
    float sceneZNear = 4.0f;
    float sceneDepthHackZNear = 0.1f;
    std::uint32_t sceneViewX = 0u;
    std::uint32_t sceneViewY = 0u;
    std::uint32_t sceneViewWidth = 0u;
    std::uint32_t sceneViewHeight = 0u;
    std::string sceneViewWorldName;
    bool sceneViewActive = false;
    bool sceneViewGeometrySubmitted = false;
    bool sceneFogEnabled = false;
    bool sceneFilmEnabled = false;
    bool sceneGlowEnabled = false;
    bool sceneSunShadowEnabled = false;
    bool sceneViewFirstDrawCompleted = false;
    bool sceneViewWaitReported = false;
};

WebRendererState g_renderer;
constexpr std::uint8_t FALLBACK_TEXTURE_RGBA[] = {255u, 255u, 255u, 255u};
bool HandleWebGLContextLost(int, const void *, void *);
bool HandleWebGLContextRestored(int, const void *, void *);
void DeleteWorldTextureObjects(
    std::vector<WebRendererRetainedWorldImage> &images);
void DeleteWaterTextureObjects(
    std::vector<WebRendererRetainedWorldBatch> &batches);
void DeleteSurfaceObjects(
    GLuint vertexArray, GLuint vertexBuffer, GLuint indexBuffer);
void DeleteStaticModelObjects(
    GLuint vertexArray, GLuint vertexBuffer, GLuint indexBuffer,
    GLuint instanceBuffer);
void BindStaticModelInstanceRange(std::uint32_t instanceOffset);
void AttachRetainedWorldReflectionTextures() noexcept;

#if KISAK_WEB_DIAGNOSTICS
enum class FrameProfileDrawBucket : std::uint8_t
{
    None,
    World,
    StaticModel,
    DynamicModel,
    FxModel,
    Particle,
    Mark,
    Shadow,
    Ui,
    PostProcess,
    Query,
};

struct FrameProfileGpuQuery
{
    GLuint query = 0u;
    std::uint32_t pumpTick = 0u;
    std::uint32_t contextGeneration = 0u;
    std::uint32_t viewSubmissionGeneration = 0u;
    std::uint32_t lagFrames = 0u;
    bool pending = false;
};

constexpr std::size_t FRAME_PROFILE_GPU_QUERY_COUNT = 8u;
std::array<FrameProfileGpuQuery, FRAME_PROFILE_GPU_QUERY_COUNT>
    g_frameProfileGpuQueries{};
bool g_frameProfileGpuSupported = false;
FrameProfileGpuQuery *g_frameProfileActiveGpuQuery = nullptr;
FrameProfileDrawBucket g_frameProfileDrawBucket = FrameProfileDrawBucket::None;
GLuint g_frameProfileLastProgram = std::numeric_limits<GLuint>::max();

void ResetFrameProfileGpuQueries(bool deleteObjects)
{
    if (deleteObjects)
    {
        for (FrameProfileGpuQuery &slot : g_frameProfileGpuQueries)
        {
            if (slot.query != 0u) glDeleteQueries(1, &slot.query);
        }
    }
    g_frameProfileGpuQueries = {};
    g_frameProfileGpuSupported = false;
    g_frameProfileActiveGpuQuery = nullptr;
    g_frameProfileLastProgram = std::numeric_limits<GLuint>::max();
}

void InitializeFrameProfileGpuQueries()
{
    ResetFrameProfileGpuQueries(false);
    g_frameProfileGpuSupported = emscripten_webgl_enable_extension(
        g_renderer.context, "EXT_disjoint_timer_query_webgl2");
    if (!g_frameProfileGpuSupported) return;
    for (FrameProfileGpuQuery &slot : g_frameProfileGpuQueries)
        glGenQueries(1, &slot.query);
    g_frameProfileGpuSupported = std::all_of(
        g_frameProfileGpuQueries.begin(), g_frameProfileGpuQueries.end(),
        [](const FrameProfileGpuQuery &slot) { return slot.query != 0u; });
    if (!g_frameProfileGpuSupported) ResetFrameProfileGpuQueries(true);
}

void PollFrameProfileGpuQueries()
{
    if (!g_frameProfileGpuSupported) return;
    GLboolean disjoint = GL_FALSE;
    glGetBooleanv(GL_GPU_DISJOINT_EXT, &disjoint);
    for (FrameProfileGpuQuery &slot : g_frameProfileGpuQueries)
    {
        if (!slot.pending) continue;
        ++slot.lagFrames;
        if (disjoint == GL_TRUE)
        {
            WebFrameProfile_PublishGpuResult(
                slot.pumpTick,
                slot.contextGeneration,
                slot.viewSubmissionGeneration,
                0.0,
                slot.lagFrames,
                "disjoint");
            slot.pending = false;
            continue;
        }
        GLuint available = GL_FALSE;
        glGetQueryObjectuiv(slot.query, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available != GL_TRUE) continue;
        GLuint64 nanoseconds = 0u;
        glGetQueryObjectui64vEXT(slot.query, GL_QUERY_RESULT, &nanoseconds);
        const bool stale = slot.contextGeneration !=
            g_renderer.contextGeneration;
        WebFrameProfile_PublishGpuResult(
            slot.pumpTick,
            slot.contextGeneration,
            slot.viewSubmissionGeneration,
            stale ? 0.0
                : static_cast<double>(nanoseconds) / 1'000'000.0,
            slot.lagFrames,
            stale ? "stale-context" : "valid");
        slot.pending = false;
    }
}

void BeginFrameProfileGpuQuery(WebFrameProfileSample &profile)
{
    profile.gpuTimingsAvailable = g_frameProfileGpuSupported;
    if (!g_frameProfileGpuSupported) return;
    auto slot = std::find_if(g_frameProfileGpuQueries.begin(),
        g_frameProfileGpuQueries.end(),
        [](const FrameProfileGpuQuery &candidate) {
            return !candidate.pending;
        });
    if (slot == g_frameProfileGpuQueries.end())
    {
        profile.gpuQueryDropped = true;
        return;
    }
    slot->pumpTick = profile.pumpTick;
    slot->contextGeneration = g_renderer.contextGeneration;
    slot->viewSubmissionGeneration =
        g_renderer.sceneViewSubmissionGeneration;
    slot->lagFrames = 0u;
    glBeginQuery(GL_TIME_ELAPSED_EXT, slot->query);
    g_frameProfileActiveGpuQuery = &*slot;
    profile.gpuQueryIssued = true;
}

void EndFrameProfileGpuQuery()
{
    if (!g_frameProfileActiveGpuQuery) return;
    glEndQuery(GL_TIME_ELAPSED_EXT);
    g_frameProfileActiveGpuQuery->pending = true;
    g_frameProfileActiveGpuQuery = nullptr;
}

FrameProfileDrawBucket ProfileBucketForKind(
    WebRendererSceneBatchKind kind) noexcept
{
    switch (kind)
    {
    case WebRendererSceneBatchKind::FxXModel:
        return FrameProfileDrawBucket::FxModel;
    case WebRendererSceneBatchKind::FxParticleCloud:
        return FrameProfileDrawBucket::Particle;
    case WebRendererSceneBatchKind::FxCodeMesh:
    case WebRendererSceneBatchKind::FxMarkMesh:
        return FrameProfileDrawBucket::Mark;
    default:
        return FrameProfileDrawBucket::DynamicModel;
    }
}

void AddProfileDynamicTime(WebRendererSceneBatchKind kind, double milliseconds)
{
    WebFrameProfileSample *const profile = WebFrameProfile_Current();
    if (!profile) return;
    switch (kind)
    {
    case WebRendererSceneBatchKind::FxXModel:
        profile->fxModelsMs += milliseconds;
        break;
    case WebRendererSceneBatchKind::FxParticleCloud:
        profile->particlesMs += milliseconds;
        break;
    case WebRendererSceneBatchKind::FxCodeMesh:
    case WebRendererSceneBatchKind::FxMarkMesh:
        profile->marksMs += milliseconds;
        break;
    default:
        profile->dynamicModelsMs += milliseconds;
        break;
    }
}

void AddProfileDraw(
    std::uint64_t elementCount, std::uint64_t instanceCount, bool indexed)
{
    WebFrameProfileSample *const profile = WebFrameProfile_Current();
    if (!profile) return;
    const std::uint64_t multipliedElements = elementCount * instanceCount;
    if (indexed) profile->submittedIndices += multipliedElements;
    profile->submittedTriangles += multipliedElements / 3u;
    switch (g_frameProfileDrawBucket)
    {
    case FrameProfileDrawBucket::World: ++profile->worldDrawCalls; break;
    case FrameProfileDrawBucket::StaticModel:
        ++profile->staticModelDrawCalls;
        profile->staticModelInstanceDraws += instanceCount;
        break;
    case FrameProfileDrawBucket::DynamicModel:
        ++profile->dynamicDrawCalls;
        ++profile->dynamicBatchesDrawn;
        break;
    case FrameProfileDrawBucket::FxModel:
        ++profile->fxDrawCalls;
        ++profile->fxModelBatchesDrawn;
        break;
    case FrameProfileDrawBucket::Particle:
        ++profile->fxDrawCalls;
        ++profile->particleBatchesDrawn;
        break;
    case FrameProfileDrawBucket::Mark:
        ++profile->fxDrawCalls;
        ++profile->markBatchesDrawn;
        break;
    case FrameProfileDrawBucket::Shadow:
        ++profile->shadowDrawCalls;
        profile->shadowCasterDraws += instanceCount;
        break;
    case FrameProfileDrawBucket::Ui: ++profile->uiDrawCalls; break;
    case FrameProfileDrawBucket::PostProcess:
        ++profile->postProcessDrawCalls;
        break;
    case FrameProfileDrawBucket::Query: ++profile->queryDrawCalls; break;
    default: break;
    }
}

void ProfileBindTexture(GLenum target, GLuint texture)
{
    glBindTexture(target, texture);
    if (WebFrameProfileSample *const profile = WebFrameProfile_Current())
        ++profile->textureBindCalls;
}

void ProfileUseProgram(GLuint program)
{
    glUseProgram(program);
    if (g_frameProfileLastProgram != program)
    {
        if (WebFrameProfileSample *const profile = WebFrameProfile_Current())
            ++profile->programSwitches;
        g_frameProfileLastProgram = program;
    }
}

void ProfileBufferData(
    GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    glBufferData(target, size, data, usage);
    if (data && size > 0)
    {
        if (WebFrameProfileSample *const profile = WebFrameProfile_Current())
            profile->bufferUploadBytes += static_cast<std::uint64_t>(size);
    }
}

void ProfileBufferSubData(
    GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
    glBufferSubData(target, offset, size, data);
    if (data && size > 0)
    {
        if (WebFrameProfileSample *const profile = WebFrameProfile_Current())
            profile->bufferUploadBytes += static_cast<std::uint64_t>(size);
    }
}

std::uint64_t ProfilePixelBytes(
    GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type)
{
    if (width <= 0 || height <= 0 || depth <= 0) return 0u;
    std::uint64_t channels = 0u;
    switch (format)
    {
    case GL_RED: channels = 1u; break;
    case GL_RG: channels = 2u; break;
    case GL_RGB: channels = 3u; break;
    case GL_RGBA: channels = 4u; break;
    default: return 0u;
    }
    std::uint64_t componentBytes = 0u;
    switch (type)
    {
    case GL_UNSIGNED_BYTE: componentBytes = 1u; break;
    case GL_UNSIGNED_SHORT: componentBytes = 2u; break;
    case GL_FLOAT: componentBytes = 4u; break;
    default: return 0u;
    }
    return static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height) *
        static_cast<std::uint64_t>(depth) * channels * componentBytes;
}

void AddProfileTextureUpload(
    GLsizei width, GLsizei height, GLsizei depth,
    GLenum format, GLenum type, const void *pixels)
{
    if (!pixels) return;
    WebFrameProfileSample *const profile = WebFrameProfile_Current();
    if (!profile) return;
    const std::uint64_t bytes =
        ProfilePixelBytes(width, height, depth, format, type);
    if (bytes != 0u) profile->textureUploadBytes += bytes;
    else ++profile->unmeasuredTextureUploads;
}

void ProfileTexImage2D(GLenum target, GLint level, GLint internalFormat,
    GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type,
    const void *pixels)
{
    glTexImage2D(target, level, internalFormat, width, height, border,
        format, type, pixels);
    AddProfileTextureUpload(width, height, 1, format, type, pixels);
}

void ProfileTexImage3D(GLenum target, GLint level, GLint internalFormat,
    GLsizei width, GLsizei height, GLsizei depth, GLint border,
    GLenum format, GLenum type, const void *pixels)
{
    glTexImage3D(target, level, internalFormat, width, height, depth, border,
        format, type, pixels);
    AddProfileTextureUpload(width, height, depth, format, type, pixels);
}

void ProfileTexSubImage2D(GLenum target, GLint level, GLint xOffset,
    GLint yOffset, GLsizei width, GLsizei height, GLenum format, GLenum type,
    const void *pixels)
{
    glTexSubImage2D(target, level, xOffset, yOffset, width, height,
        format, type, pixels);
    AddProfileTextureUpload(width, height, 1, format, type, pixels);
}

void ProfileDrawElements(
    GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    glDrawElements(mode, count, type, indices);
    if (count > 0)
        AddProfileDraw(static_cast<std::uint64_t>(count), 1u, true);
}

void ProfileDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
    const void *indices, GLsizei instanceCount)
{
    glDrawElementsInstanced(mode, count, type, indices, instanceCount);
    if (count > 0 && instanceCount > 0)
        AddProfileDraw(static_cast<std::uint64_t>(count),
            static_cast<std::uint64_t>(instanceCount), true);
}

void ProfileDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    glDrawArrays(mode, first, count);
    if (count > 0)
        AddProfileDraw(static_cast<std::uint64_t>(count), 1u, false);
}

void ProfileBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
    GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
    GLbitfield mask, GLenum filter)
{
    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
        dstX0, dstY0, dstX1, dstY1, mask, filter);
    if (WebFrameProfileSample *const profile = WebFrameProfile_Current())
        ++profile->resolveBlits;
}

#define glBindTexture ProfileBindTexture
#define glUseProgram ProfileUseProgram
#define glBufferData ProfileBufferData
#define glBufferSubData ProfileBufferSubData
#define glTexImage2D ProfileTexImage2D
#define glTexImage3D ProfileTexImage3D
#define glTexSubImage2D ProfileTexSubImage2D
#define glDrawElements ProfileDrawElements
#define glDrawElementsInstanced ProfileDrawElementsInstanced
#define glDrawArrays ProfileDrawArrays
#define glBlitFramebuffer ProfileBlitFramebuffer
#endif

#if KISAK_WEB_DIAGNOSTICS
EM_JS(
    void,
    DispatchRendererFxDiagnostic,
    (const char *sourceKind,
     std::uint32_t batchCount,
     std::uint32_t vertexCount,
     std::uint32_t indexCount,
     const char *modelName,
     const char *materialName),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:renderer-fx", {
            detail: {
                state: "retained",
                source: "canonical-renderer-frontend",
                sourceKind: UTF8ToString(sourceKind),
                batchCount: batchCount >>> 0,
                vertexCount: vertexCount >>> 0,
                indexCount: indexCount >>> 0,
                modelName: UTF8ToString(modelName),
                materialName: UTF8ToString(materialName),
                bounded: true
            }
        }));
    });
#endif


const char *FxSourceKindName(WebRendererSceneBatchKind kind) noexcept
{
    switch (kind)
    {
    case WebRendererSceneBatchKind::FxCodeMesh: return "FxCodeMesh";
    case WebRendererSceneBatchKind::FxXModel: return "FxXModel";
    case WebRendererSceneBatchKind::FxParticleCloud: return "FxParticleCloud";
    case WebRendererSceneBatchKind::FxMarkMesh: return "FxMarkMesh";
    case WebRendererSceneBatchKind::SunSprite: return "SunSprite";
    case WebRendererSceneBatchKind::SunFlare: return "SunFlare";
    default: return "";
    }
}

void DeleteModelLightingTexture(
    WebRendererRetainedModelLightingAtlas &atlas)
{
    if (atlas.texture != 0u)
        glDeleteTextures(1, &atlas.texture);
    atlas.texture = 0u;
}

bool CopyModelLightingAtlas(
    const WebRendererModelLightingAtlasDesc *source,
    WebRendererRetainedModelLightingAtlas &destination)
{
    if (!source)
    {
        destination = {};
        return true;
    }
    const std::uint64_t expectedBytes =
        static_cast<std::uint64_t>(source->width) * source->height *
        source->depth * 4u;
    if (!source->pixels || source->width != 256u ||
        source->height < 4u || source->height > 4096u ||
        source->depth != 4u || source->entryCount == 0u ||
        source->entryCount > (source->width / 4u) *
            (source->height / 4u) ||
        expectedBytes != source->byteLength ||
        expectedBytes > std::numeric_limits<std::size_t>::max())
        return false;
    WebRendererRetainedModelLightingAtlas replacement;
    try
    {
        replacement.pixels.assign(
            source->pixels, source->pixels + source->byteLength);
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    replacement.width = source->width;
    replacement.height = source->height;
    replacement.depth = source->depth;
    replacement.entryCount = source->entryCount;
    destination = std::move(replacement);
    return true;
}

bool CreateModelLightingTexture(
    WebRendererRetainedModelLightingAtlas &atlas)
{
    if (atlas.pixels.empty()) return true;
    if (atlas.texture != 0u) return true;
    while (glGetError() != GL_NO_ERROR)
    {
    }
    GLuint texture = 0u;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_3D, texture);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage3D(
        GL_TEXTURE_3D, 0, GL_RGBA8,
        static_cast<GLsizei>(atlas.width),
        static_cast<GLsizei>(atlas.height),
        static_cast<GLsizei>(atlas.depth),
        0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.pixels.data());
    const GLenum error = glGetError();
    if (texture == 0u || error != GL_NO_ERROR)
    {
        if (texture != 0u) glDeleteTextures(1, &texture);
        Web_Log(WebLogLevel::Error,
            "[kisakcod-web] WebGL2 model-lighting volume upload failed "
            "(0x%x).\n", static_cast<unsigned int>(error));
        return false;
    }
    atlas.texture = texture;
    return true;
}

#if KISAK_WEB_DIAGNOSTICS
void ReportRetainedDynamicFx()
{
    constexpr std::array<WebRendererSceneBatchKind, 4> fxKinds = {{
        WebRendererSceneBatchKind::FxCodeMesh,
        WebRendererSceneBatchKind::FxXModel,
        WebRendererSceneBatchKind::FxParticleCloud,
        WebRendererSceneBatchKind::FxMarkMesh,
    }};
    for (std::size_t kindIndex = 0u; kindIndex < fxKinds.size(); ++kindIndex)
    {
        if (g_renderer.dynamicFxSourceReported[
                WebRenderer_FxDiagnosticIndex(fxKinds[kindIndex])])
            continue;
        const WebRendererSceneBatchKind kind = fxKinds[kindIndex];
        std::uint32_t batchCount = 0u;
        std::uint32_t vertexCount = 0u;
        std::uint32_t indexCount = 0u;
        std::string modelName;
        std::string materialName;
        for (const WebRendererRetainedWorldBatch &batch :
             g_renderer.retainedDynamicModelBatches)
        {
            if (batch.sourceKind != kind)
                continue;
            ++batchCount;
            indexCount += batch.indexCount;
            if (modelName.empty() && !batch.modelName.empty())
                modelName = batch.modelName;
            if (materialName.empty() && !batch.materialName.empty())
                materialName = batch.materialName;
            if (batch.firstIndex > g_renderer.retainedDynamicModelIndices.size() ||
                batch.indexCount > g_renderer.retainedDynamicModelIndices.size() -
                    batch.firstIndex)
                continue;
            std::uint32_t batchMinVertex = UINT32_MAX;
            std::uint32_t batchMaxVertex = 0u;
            for (std::uint32_t index = 0u; index < batch.indexCount; ++index)
            {
                const std::uint32_t vertex = g_renderer.retainedDynamicModelIndices[
                    batch.firstIndex + index];
                if (vertex >= g_renderer.retainedDynamicModelVertices.size())
                    continue;
                batchMinVertex = std::min(batchMinVertex, vertex);
                batchMaxVertex = std::max(batchMaxVertex, vertex);
            }
            if (batchMinVertex != UINT32_MAX)
                vertexCount += batchMaxVertex - batchMinVertex + 1u;
        }
        if (batchCount == 0u)
            continue;
        const std::string model = modelName.empty() ? "<unnamed>" : modelName;
        const std::string material = materialName.empty()
            ? "<unnamed>" : materialName;
        g_renderer.dynamicFxSourceReported[
            WebRenderer_FxDiagnosticIndex(kind)] = true;
        DispatchRendererFxDiagnostic(FxSourceKindName(kind), batchCount,
            vertexCount, indexCount, model.c_str(), material.c_str());
    }
}
#endif

EM_JS(
    void,
    DispatchRendererTextureLifecycle,
    (const char *state,
     const char *message,
     std::uint32_t width,
     std::uint32_t height,
     std::uint32_t recoveryBytes,
     std::uint32_t uploadGeneration,
     std::uint32_t resourceGeneration,
     std::uint32_t recoveryCount,
     bool resident),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:renderer-texture", {
            detail: {
                state: UTF8ToString(state),
                message: UTF8ToString(message),
                width,
                height,
                payloadBytes: recoveryBytes >>> 0,
                gpuFormat: "rgba8",
                recoveryBytes: recoveryBytes >>> 0,
                uploadGeneration: uploadGeneration >>> 0,
                resourceGeneration: resourceGeneration >>> 0,
                recoveryCount: recoveryCount >>> 0,
                resident: Boolean(resident)
            }
        }));
    });

EM_JS(
    void,
    DispatchRendererAaLifecycle,
    (const char *state,
     const char *message,
     std::int32_t configuredSamples,
     std::int32_t requestedSamples,
     std::int32_t activeSamples,
     std::int32_t maxSamples,
     std::int32_t width,
     std::int32_t height,
     std::uint32_t resourceGeneration,
     bool resident),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:renderer-aa", {
            detail: {
                state: UTF8ToString(state),
                message: UTF8ToString(message),
                configuredSamples: configuredSamples | 0,
                requestedSamples: requestedSamples | 0,
                activeSamples: activeSamples | 0,
                maxSamples: maxSamples | 0,
                width: width | 0,
                height: height | 0,
                resourceGeneration: resourceGeneration >>> 0,
                resident: Boolean(resident),
                backend: "webgl2-multisample-renderbuffer",
                resolveBoundary: "scene-before-postfx-and-ui"
            }
        }));
    });

EM_JS(
    void,
    DispatchRendererShaderLifecycle,
    (const char *state,
     const char *message,
     const char *substitutionId,
     std::uint32_t vertexSourceHash,
     std::uint32_t fragmentSourceHash,
     std::uint32_t submissionGeneration,
     std::uint32_t resourceGeneration,
     std::uint32_t recoveryCount,
     std::uint32_t drawCount,
     bool retained,
     bool resident,
     bool firstDrawCompleted),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:renderer-shader", {
            detail: {
                state: UTF8ToString(state),
                message: UTF8ToString(message),
                substitutionId: UTF8ToString(substitutionId),
                vertexSourceHash: vertexSourceHash >>> 0,
                fragmentSourceHash: fragmentSourceHash >>> 0,
                submissionGeneration: submissionGeneration >>> 0,
                resourceGeneration: resourceGeneration >>> 0,
                recoveryCount: recoveryCount >>> 0,
                drawCount: drawCount >>> 0,
                retained: Boolean(retained),
                resident: Boolean(resident),
                firstDrawCompleted: Boolean(firstDrawCompleted),
                vertexAttributes: ["a_position", "a_color", "a_texcoord0"],
                uniforms: [
                    "u_viewProjectionMatrix",
                    "u_worldMatrix",
                    "u_colorMapSampler"
                ],
                textureUnit: 0
            }
        }));
    });

EM_JS(
    void,
    DispatchRendererSurfaceLifecycle,
    (const char *state,
     const char *message,
     std::uint32_t vertexCount,
     std::uint32_t indexCount,
     std::uint32_t drawFirstIndex,
     std::uint32_t drawIndexCount,
     std::uint32_t vertexBytes,
     std::uint32_t indexBytes,
     const char *topology,
     const char *textureBinding,
     std::uint32_t submissionGeneration,
     std::uint32_t resourceGeneration,
     std::uint32_t recoveryCount,
     std::uint32_t drawCount,
     std::uint32_t textureCount,
     bool resident),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:renderer-surface", {
            detail: {
                state: UTF8ToString(state),
                message: UTF8ToString(message),
                vertexCount,
                indexCount,
                drawFirstIndex,
                drawIndexCount,
                vertexBytes: vertexBytes >>> 0,
                indexBytes: indexBytes >>> 0,
                recoveryBytes: (vertexBytes + indexBytes) >>> 0,
                topology: UTF8ToString(topology),
                textureBinding: UTF8ToString(textureBinding),
                submissionGeneration: submissionGeneration >>> 0,
                resourceGeneration: resourceGeneration >>> 0,
                recoveryCount: recoveryCount >>> 0,
                drawCount: drawCount >>> 0,
                textureCount: textureCount >>> 0,
                resident: Boolean(resident)
            }
        }));
    });

EM_JS(
    void,
    DispatchRendererSceneView,
    (const char *worldName,
     std::uint32_t x,
     std::uint32_t y,
     std::uint32_t width,
     std::uint32_t height,
     float tanHalfFovX,
     float tanHalfFovY,
     float originX,
     float originY,
     float originZ,
     float forwardX,
     float forwardY,
     float forwardZ,
     std::int32_t time,
     float zNear,
     std::uint32_t submissionGeneration,
     std::uint32_t surfaceCount,
     std::uint32_t vertexCount,
     std::uint32_t indexCount,
     bool geometrySubmitted),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:renderer-scene-view", {
            detail: {
                state: "submitted",
                source: "canonical-cgame-refdef",
                worldName: UTF8ToString(worldName),
                viewport: { x, y, width, height },
                tanHalfFovX,
                tanHalfFovY,
                viewOrigin: [originX, originY, originZ],
                viewForward: [forwardX, forwardY, forwardZ],
                time: time | 0,
                zNear,
                localClientNum: 0,
                submissionGeneration: submissionGeneration >>> 0,
                geometrySubmitted: Boolean(geometrySubmitted),
                worldSurfaceCount: surfaceCount >>> 0,
                worldVertexCount: vertexCount >>> 0,
                worldIndexCount: indexCount >>> 0
            }
        }));
    });

EM_JS(
    void,
    DispatchRendererSceneFrame,
    (const char *worldName,
     std::uint32_t viewSubmissionGeneration,
     std::uint32_t surfaceSubmissionGeneration,
     std::uint32_t resourceGeneration,
     std::uint32_t surfaceCount,
     std::uint32_t vertexCount,
     std::uint32_t indexCount),
    {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:renderer-scene-frame", {
            detail: {
                state: "drawn",
                source: "canonical-cgame-refdef",
                worldName: UTF8ToString(worldName),
                viewSubmissionGeneration: viewSubmissionGeneration >>> 0,
                surfaceSubmissionGeneration: surfaceSubmissionGeneration >>> 0,
                resourceGeneration: resourceGeneration >>> 0,
                worldSurfaceCount: surfaceCount >>> 0,
                worldVertexCount: vertexCount >>> 0,
                worldIndexCount: indexCount >>> 0,
                geometrySubmitted: true,
                backend: "webgl2"
            }
        }));
    });

EM_JS(
    void,
    DispatchRendererSurfaceDraw,
    (std::uint32_t vertexCount, std::uint32_t indexCount,
     std::uint32_t drawFirstIndex, std::uint32_t drawIndexCount,
     std::uint32_t submissionGeneration, std::uint32_t resourceGeneration,
     bool resident),
    {
        globalThis.dispatchEvent(new CustomEvent(
            "kisakcod:renderer-surface-draw", { detail: {
                state: "drawn",
                message: "The current engine-owned indexed surface was issued through WebGL2",
                vertexCount: vertexCount >>> 0,
                indexCount: indexCount >>> 0,
                drawFirstIndex: drawFirstIndex >>> 0,
                drawIndexCount: drawIndexCount >>> 0,
                topology: "triangle-list",
                submissionGeneration: submissionGeneration >>> 0,
                resourceGeneration: resourceGeneration >>> 0,
                resident: Boolean(resident)
            }}));
    });

const char *SurfaceTopologyString(WebRendererPrimitiveTopology topology) noexcept
{
    return topology == WebRendererPrimitiveTopology::TriangleList
        ? "triangle-list"
        : "unsupported";
}

#if KISAK_WEB_DIAGNOSTICS
extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestLoseWebGLContext()
{
    return HandleWebGLContextLost(0, nullptr, nullptr) ? 1 : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestRestoreWebGLContext()
{
    return HandleWebGLContextRestored(0, nullptr, nullptr) ? 1 : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestSetAaSamples(int samples)
{
    if (!r_aaSamples)
        return 0;
    Dvar_SetInt(r_aaSamples, samples);
    return r_aaSamples->current.integer;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestSubmitSurface()
{
    constexpr WebRendererSurfaceVertex vertices[] = {
        {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f}, {}, {0.0f, 0.0f, 1.0f}, {}, 1.0f},
        {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f},
            {1.0f, 0.0f}, {}, {0.0f, 0.0f, 1.0f}, {}, 1.0f},
        {{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f},
            {0.5f, 1.0f}, {}, {0.0f, 0.0f, 1.0f}, {}, 1.0f},
    };
    constexpr std::uint16_t indices[] = {0u, 1u, 2u};
    const WebRendererSurfaceDesc surface{vertices, 3u, indices, 3u};
    const WebRendererDrawDesc draw{
        WebRendererPrimitiveTopology::TriangleList,
        0u,
        3u,
        WebRendererTextureBinding::None,
    };
    return WebRenderer_SetSurface(surface, draw) ==
        WebRendererSurfaceResult::Success ? 1 : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestUnloadWorldResources()
{
    WebRenderer_UnloadWorldResources();
    return 1;
}

extern "C" EMSCRIPTEN_KEEPALIVE double KisakWeb_TestHeapBytes()
{
    return static_cast<double>(emscripten_get_heap_size());
}
#endif

const char *SurfaceTextureBindingString(WebRendererTextureBinding binding) noexcept
{
    switch (binding)
    {
    case WebRendererTextureBinding::None: return "none";
    case WebRendererTextureBinding::EngineImage: return "engine-image";
    }
    return "unsupported";
}

void EmitSurfaceLifecycle(const char *state, const char *message)
{
    if (!g_renderer.surfaceActive)
    {
        return;
    }
    const std::size_t vertexBytes =
        g_renderer.retainedVertices.size() * sizeof(WebRendererSurfaceVertex);
    const std::size_t indexBytes =
        g_renderer.worldSurfaceActive
            ? g_renderer.retainedWorldIndices.size() * sizeof(std::uint32_t)
            : g_renderer.retainedIndices.size() * sizeof(std::uint16_t);
    if (vertexBytes > UINT32_MAX || indexBytes > UINT32_MAX)
    {
        return;
    }
    DispatchRendererSurfaceLifecycle(
        state,
        message,
        static_cast<std::uint32_t>(g_renderer.retainedVertices.size()),
        static_cast<std::uint32_t>(g_renderer.worldSurfaceActive
            ? g_renderer.retainedWorldIndices.size()
            : g_renderer.retainedIndices.size()),
        g_renderer.draw.firstIndex,
        g_renderer.draw.indexCount,
        static_cast<std::uint32_t>(vertexBytes),
        static_cast<std::uint32_t>(indexBytes),
        SurfaceTopologyString(g_renderer.draw.topology),
        SurfaceTextureBindingString(g_renderer.draw.textureBinding),
        g_renderer.surfaceSubmissionGeneration,
        g_renderer.surfaceResourceGeneration,
        g_renderer.surfaceRecoveryCount,
        g_renderer.worldSurfaceActive
            ? static_cast<std::uint32_t>(g_renderer.retainedWorldBatches.size())
            : 1u,
        g_renderer.worldSurfaceActive
            ? static_cast<std::uint32_t>(g_renderer.retainedWorldImages.size())
            : 1u,
        g_renderer.initialized && !g_renderer.contextLost &&
            g_renderer.vertexArray != 0 && g_renderer.vertexBuffer != 0 &&
            g_renderer.indexBuffer != 0);
}

void EmitSurfaceDraw()
{
    DispatchRendererSurfaceDraw(
        static_cast<std::uint32_t>(g_renderer.retainedVertices.size()),
        static_cast<std::uint32_t>(g_renderer.worldSurfaceActive
            ? g_renderer.retainedWorldIndices.size()
            : g_renderer.retainedIndices.size()),
        g_renderer.draw.firstIndex,
        g_renderer.draw.indexCount,
        g_renderer.surfaceSubmissionGeneration,
        g_renderer.surfaceResourceGeneration,
        g_renderer.initialized && !g_renderer.contextLost &&
            g_renderer.vertexArray != 0 && g_renderer.vertexBuffer != 0 &&
            g_renderer.indexBuffer != 0);
}

void EmitTextureLifecycle(const char *state, const char *message)
{
    if (!g_renderer.sourceTextureActive ||
        g_renderer.retainedPixels.size() > UINT32_MAX)
    {
        return;
    }
    DispatchRendererTextureLifecycle(
        state,
        message,
        g_renderer.textureWidth,
        g_renderer.textureHeight,
        static_cast<std::uint32_t>(g_renderer.retainedPixels.size()),
        g_renderer.uploadGeneration,
        g_renderer.rebuildGeneration,
        g_renderer.recoveryCount,
        g_renderer.initialized && !g_renderer.contextLost && g_renderer.texture != 0);
}

void EmitShaderLifecycle(const char *state, const char *message)
{
    DispatchRendererShaderLifecycle(
        state,
        message,
        g_renderer.compatibilityId.c_str(),
        g_renderer.compatibilityVertexSourceHash,
        g_renderer.compatibilityFragmentSourceHash,
        g_renderer.compatibilitySubmissionGeneration,
        g_renderer.compatibilityResourceGeneration,
        g_renderer.compatibilityRecoveryCount,
        g_renderer.compatibilityDrawCount,
        g_renderer.compatibilityActive,
        g_renderer.initialized && !g_renderer.contextLost &&
            g_renderer.compatibilityProgram != 0,
        g_renderer.compatibilityFirstDrawCompleted);
}

#if KISAK_WEB_DIAGNOSTICS
EM_JS(void, DispatchRendererMemory, (
    const char *state,
    double worldImageRecoveryBytes,
    double staticModelImageRecoveryBytes,
    double dynamicModelImageRecoveryBytes,
    double uiImageRecoveryBytes,
    double supplementalTextureRecoveryBytes,
    double worldImageDecodedBytes,
    double staticModelImageDecodedBytes,
    double dynamicModelImageDecodedBytes,
    double uiImageDecodedBytes,
    double textureRecoverySourceBytes,
    double encodedImageRecoveryBytes,
    double decodedTextureSourceBytes,
    double gpuTextureEstimateBytes,
    double geometryBytes,
    double recoveryCopyBytes,
    double shaderProgramCacheEstimateBytes,
    double temporaryUploadBytes,
    double decodedTextureAdmissionBudgetBytes,
    bool wasmHeapStatsSampled,
    double wasmProgramBreakOffsetBytes,
    double wasmLinearMemoryCapacityBytes,
    double wasmLinearMemoryMaximumBytes,
    bool wasmAllocatorStatsSampled,
    double wasmAllocatorInUseBytes,
    double wasmAllocatorFreeBytes,
    double wasmAllocatorFootprintBytes,
    double wasmAllocatorTopFreeBytes,
    double imageLoadDefCacheEntryCount,
    double imageLoadDefCacheEncodedPayloadBytes,
    double imageLoadDefCacheBudgetBytes,
    double imageLoadDefCacheEvictionCount,
    double loadDefImageCount,
    double loadDefImageRecoveryBytes,
    double loadDefImageDecodedBytes,
    double iwiImageCount,
    double iwiImageRecoveryBytes,
    double iwiImageDecodedBytes,
    double rawImageCount,
    double rawImageRecoveryBytes,
    double rawImageDecodedBytes), {
        let webglRendererIdentity = null;
        try {
            const gl = (typeof GL !== "undefined" && GL.currentContext)
                ? GL.currentContext.GLctx : Module.ctx;
            if (gl) {
                const extension = gl.getExtension("WEBGL_debug_renderer_info");
                const unmaskedVendor = extension
                    ? gl.getParameter(extension.UNMASKED_VENDOR_WEBGL) : null;
                const unmaskedRenderer = extension
                    ? gl.getParameter(extension.UNMASKED_RENDERER_WEBGL) : null;
                const identity = `${unmaskedVendor ?? ""} ${unmaskedRenderer ?? ""}`;
                webglRendererIdentity = {
                    vendor: gl.getParameter(gl.VENDOR),
                    renderer: gl.getParameter(gl.RENDERER),
                    unmaskedVendor,
                    unmaskedRenderer,
                    version: gl.getParameter(gl.VERSION),
                    angleBackend: /ANGLE/i.test(unmaskedRenderer ?? "")
                        ? unmaskedRenderer : null,
                    hardwareSoftwareIndication:
                        /swiftshader|llvmpipe|software|basic render/i.test(identity)
                            ? "software" : (unmaskedRenderer ? "hardware-or-driver" : "unknown"),
                };
            }
        } catch (_) {}
        globalThis.dispatchEvent(new CustomEvent("kisakcod:renderer-memory", {
            detail: {
                state: UTF8ToString(state),
                worldImageRecoveryBytes,
                staticModelImageRecoveryBytes,
                dynamicModelImageRecoveryBytes,
                uiImageRecoveryBytes,
                supplementalTextureRecoveryBytes,
                worldImageDecodedBytes,
                staticModelImageDecodedBytes,
                dynamicModelImageDecodedBytes,
                uiImageDecodedBytes,
                textureRecoverySourceBytes,
                encodedImageRecoveryBytes,
                decodedTextureSourceBytes,
                gpuTextureEstimateBytes,
                geometryBytes,
                recoveryCopyBytes,
                shaderProgramCacheEstimateBytes,
                temporaryUploadBytes,
                decodedTextureAdmissionBudgetBytes,
                recoveryBudgetBytes: decodedTextureAdmissionBudgetBytes,
                wasmProgramBreakOffsetBytes: wasmHeapStatsSampled
                    ? wasmProgramBreakOffsetBytes : null,
                wasmLinearMemoryCapacityBytes: wasmHeapStatsSampled
                    ? wasmLinearMemoryCapacityBytes : null,
                wasmLinearMemoryMaximumBytes: wasmHeapStatsSampled
                    ? wasmLinearMemoryMaximumBytes : null,
                wasmAllocatorStatsSampled: Boolean(wasmAllocatorStatsSampled),
                wasmAllocatorInUseBytes: wasmAllocatorStatsSampled
                    ? wasmAllocatorInUseBytes : null,
                wasmAllocatorFreeBytes: wasmAllocatorStatsSampled
                    ? wasmAllocatorFreeBytes : null,
                wasmAllocatorFootprintBytes: wasmAllocatorStatsSampled
                    ? wasmAllocatorFootprintBytes : null,
                wasmAllocatorTopFreeBytes: wasmAllocatorStatsSampled
                    ? wasmAllocatorTopFreeBytes : null,
                imageLoadDefCacheEntryCount,
                imageLoadDefCacheEncodedPayloadBytes,
                imageLoadDefCacheBudgetBytes,
                imageLoadDefCacheEvictionCount,
                imageRecoverySources: {
                    loadDef: {
                        imageCount: loadDefImageCount,
                        recoveryBytes: loadDefImageRecoveryBytes,
                        decodedBytes: loadDefImageDecodedBytes,
                    },
                    iwiMember: {
                        imageCount: iwiImageCount,
                        recoveryBytes: iwiImageRecoveryBytes,
                        decodedBytes: iwiImageDecodedBytes,
                    },
                    decodedRgba8: {
                        imageCount: rawImageCount,
                        recoveryBytes: rawImageRecoveryBytes,
                        decodedBytes: rawImageDecodedBytes,
                    },
                },
                webglRendererIdentity
            }
        }));
    });
#else
EM_JS(void, DispatchRendererMemory, (
    const char *state,
    double worldImageRecoveryBytes,
    double staticModelImageRecoveryBytes,
    double dynamicModelImageRecoveryBytes,
    double uiImageRecoveryBytes,
    double supplementalTextureRecoveryBytes,
    double decodedTextureSourceBytes,
    double gpuTextureEstimateBytes,
    double geometryBytes,
    double recoveryCopyBytes,
    double shaderProgramCacheEstimateBytes,
    double temporaryUploadBytes,
    double recoveryBudgetBytes), {
        let webglRendererIdentity = null;
        try {
            const gl = (typeof GL !== "undefined" && GL.currentContext)
                ? GL.currentContext.GLctx : Module.ctx;
            if (gl) {
                const extension = gl.getExtension("WEBGL_debug_renderer_info");
                const unmaskedVendor = extension
                    ? gl.getParameter(extension.UNMASKED_VENDOR_WEBGL) : null;
                const unmaskedRenderer = extension
                    ? gl.getParameter(extension.UNMASKED_RENDERER_WEBGL) : null;
                const identity = `${unmaskedVendor ?? ""} ${unmaskedRenderer ?? ""}`;
                webglRendererIdentity = {
                    vendor: gl.getParameter(gl.VENDOR),
                    renderer: gl.getParameter(gl.RENDERER),
                    unmaskedVendor,
                    unmaskedRenderer,
                    version: gl.getParameter(gl.VERSION),
                    angleBackend: /ANGLE/i.test(unmaskedRenderer ?? "")
                        ? unmaskedRenderer : null,
                    hardwareSoftwareIndication:
                        /swiftshader|llvmpipe|software|basic render/i.test(identity)
                            ? "software" : (unmaskedRenderer ? "hardware-or-driver" : "unknown"),
                };
            }
        } catch (_) {}
        globalThis.dispatchEvent(new CustomEvent("kisakcod:renderer-memory", {
            detail: {
                state: UTF8ToString(state),
                worldImageRecoveryBytes,
                staticModelImageRecoveryBytes,
                dynamicModelImageRecoveryBytes,
                uiImageRecoveryBytes,
                supplementalTextureRecoveryBytes,
                decodedTextureSourceBytes,
                gpuTextureEstimateBytes,
                geometryBytes,
                recoveryCopyBytes,
                shaderProgramCacheEstimateBytes,
                temporaryUploadBytes,
                recoveryBudgetBytes,
                webglRendererIdentity
            }
        }));
    });
#endif

#if KISAK_WEB_DIAGNOSTICS
EM_JS(void, DispatchRendererLifecycle, (
    const char *state, double oldMapBytesReleased,
    std::uint32_t contextGenerationBefore,
    std::uint32_t contextGenerationAfter, double recoveryBytes), {
        globalThis.dispatchEvent(new CustomEvent("kisakcod:renderer-lifecycle", {
            detail: {
                state: UTF8ToString(state),
                oldMapBytesReleased,
                contextGenerationBefore: contextGenerationBefore >>> 0,
                contextGenerationAfter: contextGenerationAfter >>> 0,
                recoveryBytes,
                contextGenerationUnchanged:
                    contextGenerationBefore === contextGenerationAfter
            }
        }));
    });
#else
void DispatchRendererLifecycle(
    const char *, double, std::uint32_t, std::uint32_t, double)
{
}
#endif

std::size_t RetainedCubeBytes(const kisak::iwi::Rgba8Cube &cube)
{
    std::size_t bytes = 0u;
    for (const auto &face : cube.faces) bytes += face.size();
    for (const auto &level : cube.mipFaces)
        for (const auto &face : level) bytes += face.size();
    return bytes;
}

struct RetainedImageMemoryStats
{
    std::size_t recoveryBytes = 0u;
    std::size_t decodedBytes = 0u;
    std::size_t encodedBytes = 0u;
    std::size_t temporaryUploadBytes = 0u;
    std::size_t loadDefRecoveryBytes = 0u;
    std::size_t loadDefDecodedBytes = 0u;
    std::size_t iwiRecoveryBytes = 0u;
    std::size_t iwiDecodedBytes = 0u;
    std::size_t rawRecoveryBytes = 0u;
    std::size_t rawDecodedBytes = 0u;
    std::size_t loadDefCount = 0u;
    std::size_t iwiCount = 0u;
    std::size_t rawCount = 0u;
};

RetainedImageMemoryStats RetainedImageStats(
    const std::vector<WebRendererRetainedWorldImage> &images)
{
    RetainedImageMemoryStats stats;
    for (const WebRendererRetainedWorldImage &image : images)
    {
        if (!image.supported) continue;
        std::size_t rawBytes = image.pixels.size();
        for (const auto &mip : image.mipPixels) rawBytes += mip.size();
        stats.recoveryBytes += rawBytes + image.encodedSource.size();
        stats.decodedBytes += image.decodedByteLength;
        stats.encodedBytes += image.encodedSource.size();
        if (!image.encodedSource.empty())
            stats.temporaryUploadBytes = std::max(
                stats.temporaryUploadBytes, image.uploadByteLength);
        switch (image.recoverySource)
        {
        case WebRendererImageRecoverySource::LoadDef:
            ++stats.loadDefCount;
            stats.loadDefRecoveryBytes += image.encodedSource.size();
            stats.loadDefDecodedBytes += image.decodedByteLength;
            break;
        case WebRendererImageRecoverySource::IwiMember:
            ++stats.iwiCount;
            stats.iwiRecoveryBytes += image.encodedSource.size();
            stats.iwiDecodedBytes += image.decodedByteLength;
            break;
        case WebRendererImageRecoverySource::DecodedRgba8:
            ++stats.rawCount;
            stats.rawRecoveryBytes += rawBytes;
            stats.rawDecodedBytes += image.decodedByteLength;
            break;
        }
    }
    return stats;
}

std::size_t EmitRendererMemory(const char *state, bool sampleAllocator = true)
{
    const std::size_t geometryBytes =
        g_renderer.retainedVertices.size() * sizeof(WebRendererSurfaceVertex) +
        g_renderer.retainedIndices.size() * sizeof(std::uint16_t) +
        g_renderer.retainedWorldIndices.size() * sizeof(std::uint32_t) +
        g_renderer.retainedStaticModelVertices.size() * sizeof(WebRendererSurfaceVertex) +
        g_renderer.retainedStaticModelIndices.size() * sizeof(std::uint32_t) +
        g_renderer.retainedStaticModelInstances.size() *
            sizeof(WebRendererStaticModelInstanceDesc) +
        g_renderer.retainedDynamicModelVertices.size() * sizeof(WebRendererSurfaceVertex) +
        g_renderer.retainedDynamicModelIndices.size() * sizeof(std::uint32_t) +
        g_renderer.retainedUiVertices.size() * sizeof(WebRendererSurfaceVertex) +
        g_renderer.retainedUiIndices.size() * sizeof(std::uint32_t);
    const RetainedImageMemoryStats worldImages =
        RetainedImageStats(g_renderer.retainedWorldImages);
    const RetainedImageMemoryStats staticModelImages =
        RetainedImageStats(g_renderer.retainedStaticModelImages);
    const RetainedImageMemoryStats dynamicModelImages =
        RetainedImageStats(g_renderer.retainedDynamicModelImages);
    const RetainedImageMemoryStats uiImages =
        RetainedImageStats(g_renderer.retainedUiImages);
    std::size_t supplementalTextureRecoveryBytes =
        RetainedCubeBytes(g_renderer.retainedSky.cube) +
        g_renderer.retainedStaticModelLighting.pixels.size() +
        g_renderer.retainedDynamicModelLighting.pixels.size() +
        g_renderer.retainedPixels.size();
    for (const WebRendererRetainedWorldBatch &batch : g_renderer.retainedWorldBatches)
    {
        supplementalTextureRecoveryBytes += batch.waterPixels.size();
        supplementalTextureRecoveryBytes += RetainedCubeBytes(batch.reflectionCube);
    }
    const std::size_t imageRecoverySourceBytes =
        worldImages.recoveryBytes + staticModelImages.recoveryBytes +
        dynamicModelImages.recoveryBytes + uiImages.recoveryBytes;
    const std::size_t textureRecoverySourceBytes =
        imageRecoverySourceBytes + supplementalTextureRecoveryBytes;
    const std::size_t decodedTextureSourceBytes =
        worldImages.decodedBytes + staticModelImages.decodedBytes +
        dynamicModelImages.decodedBytes + uiImages.decodedBytes +
        supplementalTextureRecoveryBytes;
    const std::size_t encodedImageRecoveryBytes =
        worldImages.encodedBytes + staticModelImages.encodedBytes +
        dynamicModelImages.encodedBytes + uiImages.encodedBytes;
    const std::size_t temporaryUploadBytes = std::max({
        worldImages.temporaryUploadBytes,
        staticModelImages.temporaryUploadBytes,
        dynamicModelImages.temporaryUploadBytes,
        uiImages.temporaryUploadBytes,
    });
    const std::size_t loadDefImageCount = worldImages.loadDefCount +
        staticModelImages.loadDefCount + dynamicModelImages.loadDefCount +
        uiImages.loadDefCount;
    const std::size_t loadDefRecoveryBytes = worldImages.loadDefRecoveryBytes +
        staticModelImages.loadDefRecoveryBytes +
        dynamicModelImages.loadDefRecoveryBytes + uiImages.loadDefRecoveryBytes;
    const std::size_t loadDefDecodedBytes = worldImages.loadDefDecodedBytes +
        staticModelImages.loadDefDecodedBytes +
        dynamicModelImages.loadDefDecodedBytes + uiImages.loadDefDecodedBytes;
    const std::size_t iwiImageCount = worldImages.iwiCount +
        staticModelImages.iwiCount + dynamicModelImages.iwiCount +
        uiImages.iwiCount;
    const std::size_t iwiRecoveryBytes = worldImages.iwiRecoveryBytes +
        staticModelImages.iwiRecoveryBytes + dynamicModelImages.iwiRecoveryBytes +
        uiImages.iwiRecoveryBytes;
    const std::size_t iwiDecodedBytes = worldImages.iwiDecodedBytes +
        staticModelImages.iwiDecodedBytes + dynamicModelImages.iwiDecodedBytes +
        uiImages.iwiDecodedBytes;
    const std::size_t rawImageCount = worldImages.rawCount +
        staticModelImages.rawCount + dynamicModelImages.rawCount +
        uiImages.rawCount;
    const std::size_t rawRecoveryBytes = worldImages.rawRecoveryBytes +
        staticModelImages.rawRecoveryBytes + dynamicModelImages.rawRecoveryBytes +
        uiImages.rawRecoveryBytes;
    const std::size_t rawDecodedBytes = worldImages.rawDecodedBytes +
        staticModelImages.rawDecodedBytes + dynamicModelImages.rawDecodedBytes +
        uiImages.rawDecodedBytes;
    const std::size_t shaderProgramCacheEstimateBytes =
        g_renderer.compatibilityId.size() +
        g_renderer.compatibilityVertexSource.size() +
        g_renderer.compatibilityFragmentSource.size();
    std::size_t renderTargetEstimateBytes = 0u;
    if (g_renderer.initialized && !g_renderer.contextLost)
    {
        const std::size_t width = static_cast<std::size_t>(
            std::max(g_renderer.postProcessWidth, 0));
        const std::size_t height = static_cast<std::size_t>(
            std::max(g_renderer.postProcessHeight, 0));
        // Scene RGBA8 + depth, composite RGBA8, and two quarter-size glow targets.
        renderTargetEstimateBytes = width * height * 10u;
        renderTargetEstimateBytes +=
            static_cast<std::size_t>(SUN_SHADOW_SIZE) * SUN_SHADOW_SIZE * 8u;
        renderTargetEstimateBytes += MAX_SPOT_SHADOWS *
            static_cast<std::size_t>(SPOT_SHADOW_SIZE) * SPOT_SHADOW_SIZE * 4u;
    }
    const std::size_t recoveryCopyBytes =
        textureRecoverySourceBytes + geometryBytes + shaderProgramCacheEstimateBytes;
#if KISAK_WEB_DIAGNOSTICS
    const WebDbImageLoadDefStats imageLoadDefStats =
        DB_WebGetImageLoadDefStats();
    bool wasmHeapStatsSampled = false;
    bool wasmAllocatorStatsSampled = false;
    double wasmProgramBreakOffsetBytes = 0.0;
    double wasmLinearMemoryCapacityBytes = 0.0;
    double wasmLinearMemoryMaximumBytes = 0.0;
    double wasmAllocatorInUseBytes = 0.0;
    double wasmAllocatorFreeBytes = 0.0;
    double wasmAllocatorFootprintBytes = 0.0;
    double wasmAllocatorTopFreeBytes = 0.0;
    wasmHeapStatsSampled = true;
    wasmProgramBreakOffsetBytes = static_cast<double>(
        *emscripten_get_sbrk_ptr());
    wasmLinearMemoryCapacityBytes = static_cast<double>(
        emscripten_get_heap_size());
    wasmLinearMemoryMaximumBytes = static_cast<double>(
        emscripten_get_heap_max());
    if (sampleAllocator)
    {
        const struct mallinfo allocator = mallinfo();
        wasmAllocatorStatsSampled = true;
        wasmAllocatorInUseBytes = static_cast<double>(allocator.uordblks);
        wasmAllocatorFreeBytes = static_cast<double>(allocator.fordblks);
        wasmAllocatorFootprintBytes =
            wasmAllocatorInUseBytes + wasmAllocatorFreeBytes;
        wasmAllocatorTopFreeBytes = static_cast<double>(allocator.keepcost);
    }
    DispatchRendererMemory(
        state,
        static_cast<double>(worldImages.recoveryBytes),
        static_cast<double>(staticModelImages.recoveryBytes),
        static_cast<double>(dynamicModelImages.recoveryBytes),
        static_cast<double>(uiImages.recoveryBytes),
        static_cast<double>(supplementalTextureRecoveryBytes),
        static_cast<double>(worldImages.decodedBytes),
        static_cast<double>(staticModelImages.decodedBytes),
        static_cast<double>(dynamicModelImages.decodedBytes),
        static_cast<double>(uiImages.decodedBytes),
        static_cast<double>(textureRecoverySourceBytes),
        static_cast<double>(encodedImageRecoveryBytes),
        static_cast<double>(decodedTextureSourceBytes),
        static_cast<double>(decodedTextureSourceBytes + renderTargetEstimateBytes),
        static_cast<double>(geometryBytes),
        static_cast<double>(recoveryCopyBytes),
        static_cast<double>(shaderProgramCacheEstimateBytes),
        static_cast<double>(temporaryUploadBytes),
        static_cast<double>(WEB_RENDERER_MAX_DECODED_TEXTURE_BYTES),
        wasmHeapStatsSampled,
        wasmProgramBreakOffsetBytes,
        wasmLinearMemoryCapacityBytes,
        wasmLinearMemoryMaximumBytes,
        wasmAllocatorStatsSampled,
        wasmAllocatorInUseBytes,
        wasmAllocatorFreeBytes,
        wasmAllocatorFootprintBytes,
        wasmAllocatorTopFreeBytes,
        static_cast<double>(imageLoadDefStats.entryCount),
        static_cast<double>(imageLoadDefStats.encodedPayloadBytes),
        static_cast<double>(imageLoadDefStats.budgetBytes),
        static_cast<double>(imageLoadDefStats.evictionCount),
        static_cast<double>(loadDefImageCount),
        static_cast<double>(loadDefRecoveryBytes),
        static_cast<double>(loadDefDecodedBytes),
        static_cast<double>(iwiImageCount),
        static_cast<double>(iwiRecoveryBytes),
        static_cast<double>(iwiDecodedBytes),
        static_cast<double>(rawImageCount),
        static_cast<double>(rawRecoveryBytes),
        static_cast<double>(rawDecodedBytes));
#else
    (void)sampleAllocator;
    DispatchRendererMemory(
        state,
        static_cast<double>(worldImages.recoveryBytes),
        static_cast<double>(staticModelImages.recoveryBytes),
        static_cast<double>(dynamicModelImages.recoveryBytes),
        static_cast<double>(uiImages.recoveryBytes),
        static_cast<double>(supplementalTextureRecoveryBytes),
        static_cast<double>(decodedTextureSourceBytes),
        static_cast<double>(decodedTextureSourceBytes + renderTargetEstimateBytes),
        static_cast<double>(geometryBytes),
        static_cast<double>(recoveryCopyBytes),
        static_cast<double>(shaderProgramCacheEstimateBytes),
        static_cast<double>(temporaryUploadBytes),
        static_cast<double>(WEB_RENDERER_MAX_DECODED_TEXTURE_BYTES));
#endif
    return recoveryCopyBytes;
}

#if KISAK_WEB_DIAGNOSTICS
extern "C" EMSCRIPTEN_KEEPALIVE double KisakWeb_TestEmitRendererMemory()
{
    return static_cast<double>(EmitRendererMemory("diagnostic-snapshot"));
}
#endif

void ResetGpuHandles()
{
    g_renderer.program = 0;
    g_renderer.skyProgram = 0;
    g_renderer.postProcessProgram = 0;
    g_renderer.glowProgram = 0;
    g_renderer.shadowProgram = 0;
    g_renderer.compatibilityProgram = 0;
    g_renderer.sceneFramebuffer = 0;
    g_renderer.sceneColorTexture = 0;
    g_renderer.sceneDepthTexture = 0;
    g_renderer.multisampleFramebuffer = 0;
    g_renderer.multisampleColorRenderbuffer = 0;
    g_renderer.multisampleDepthRenderbuffer = 0;
    g_renderer.compositeFramebuffer = 0;
    g_renderer.compositeColorTexture = 0;
    std::fill_n(g_renderer.glowFramebuffers, 2u, 0u);
    std::fill_n(g_renderer.glowColorTextures, 2u, 0u);
    g_renderer.shadowFramebuffer = 0;
    g_renderer.shadowDepthTexture = 0;
    g_renderer.shadowFarFramebuffer = 0;
    g_renderer.shadowFarDepthTexture = 0;
    g_renderer.spotShadowFramebuffers.fill(0u);
    g_renderer.spotShadowDepthTextures.fill(0u);
    std::fill_n(g_renderer.sunVisibilityQueries, 2u, 0u);
    std::fill_n(g_renderer.sunVisibilityQueryIssued, 2u, false);
    g_renderer.sunVisibilityQueryIndex = 0u;
    g_renderer.sunVisibility = 0.0f;
    g_renderer.sunFlareIntensity = 0.0f;
    g_renderer.sunBlindIntensity = 0.0f;
    g_renderer.sunBlindDarken = 0.0f;
    g_renderer.sunGlareIntensity = 0.0f;
    g_renderer.sunGlareLighten = 0.0f;
    g_renderer.sunEffectLastMilliseconds = 0.0;
    g_renderer.sunFlareFadeInMilliseconds = 0.0f;
    g_renderer.sunFlareFadeOutMilliseconds = 0.0f;
    g_renderer.sunBlindLerp = 0.0f;
    g_renderer.sunBlindMaxDarken = 0.0f;
    g_renderer.sunBlindFadeInMilliseconds = 0.0f;
    g_renderer.sunBlindFadeOutMilliseconds = 0.0f;
    g_renderer.sunGlareLerp = 0.0f;
    g_renderer.sunGlareMaxLighten = 0.0f;
    g_renderer.sunGlareFadeInMilliseconds = 0.0f;
    g_renderer.sunGlareFadeOutMilliseconds = 0.0f;
    g_renderer.vertexArray = 0;
    g_renderer.vertexBuffer = 0;
    g_renderer.indexBuffer = 0;
    g_renderer.staticModelVertexArray = 0u;
    g_renderer.staticModelVertexBuffer = 0u;
    g_renderer.staticModelIndexBuffer = 0u;
    g_renderer.staticModelInstanceBuffer = 0u;
    g_renderer.dynamicModelVertexArray = 0u;
    g_renderer.dynamicModelVertexBuffer = 0u;
    g_renderer.dynamicModelIndexBuffer = 0u;
    g_renderer.uiVertexArray = 0u;
    g_renderer.uiVertexBuffer = 0u;
    g_renderer.uiIndexBuffer = 0u;
    g_renderer.texture = 0;
    g_renderer.aspectUniform = -1;
    g_renderer.textureUniform = -1;
    g_renderer.textureEnabledUniform = -1;
    g_renderer.detailMapUniform = -1;
    g_renderer.detailMapEnabledUniform = -1;
    g_renderer.detailScaleUniform = -1;
    g_renderer.normalMapUniform = -1;
    g_renderer.normalMapEnabledUniform = -1;
    g_renderer.specularMapUniform = -1;
    g_renderer.specularMapEnabledUniform = -1;
    g_renderer.viewProjectionUniform = -1;
    g_renderer.sceneFallbackUniform = -1;
    g_renderer.lightmapEnabledUniform = -1;
    g_renderer.secondaryLightmapUniform = -1;
    g_renderer.secondaryLightmapEnabledUniform = -1;
    g_renderer.modelLightingUniform = -1;
    g_renderer.modelLightingEnabledUniform = -1;
    g_renderer.modelLightingBaseCoordinatesUniform = -1;
    g_renderer.modelLightingLookupScaleUniform = -1;
    g_renderer.premultiplyAlphaUniform = -1;
    g_renderer.colorIntensityAlphaUniform = -1;
    g_renderer.materialModeUniform = -1;
    g_renderer.falloffParmsUniform = -1;
    g_renderer.falloffBeginColorUniform = -1;
    g_renderer.falloffEndColorUniform = -1;
    g_renderer.alphaTestUniform = -1;
    g_renderer.instanceEnabledUniform = -1;
    g_renderer.uiColorUniform = -1;
    g_renderer.fogEnabledUniform = -1;
    g_renderer.viewOriginUniform = -1;
    g_renderer.fogColorUniform = -1;
    g_renderer.fogParamsUniform = -1;
    g_renderer.shadowMapUniform = -1;
    g_renderer.shadowFarMapUniform = -1;
    g_renderer.shadowMatrixUniform = -1;
    g_renderer.shadowFarMatrixUniform = -1;
    g_renderer.sunShadowEnabledUniform = -1;
    g_renderer.sunDirectionUniform = -1;
    g_renderer.sunColorUniform = -1;
    g_renderer.primaryLightmapUniform = -1;
    g_renderer.primaryLightFalloffPlacementUniform = -1;
    g_renderer.primaryLightEnabledUniform = -1;
    g_renderer.primaryLightPositionRadiusUniform = -1;
    g_renderer.primaryLightDiffuseUniform = -1;
    g_renderer.primaryLightSpotDirectionUniform = -1;
    g_renderer.primaryLightSpotFactorsUniform = -1;
    g_renderer.spotShadowMapUniform = -1;
    g_renderer.spotShadowMatrixUniform = -1;
    g_renderer.spotShadowEnabledUniform = -1;
    g_renderer.waterMapUniform = -1;
    g_renderer.reflectionProbeUniform = -1;
    g_renderer.envMapParmsUniform = -1;
    g_renderer.waterColorUniform = -1;
    g_renderer.shadowDepthMatrixUniform = -1;
    g_renderer.shadowDepthTextureUniform = -1;
    g_renderer.shadowDepthTextureEnabledUniform = -1;
    g_renderer.shadowDepthAlphaTestUniform = -1;
    g_renderer.shadowDepthInstanceEnabledUniform = -1;
    g_renderer.skyTextureUniform = -1;
    g_renderer.skyTanHalfFovUniform = -1;
    g_renderer.skyForwardUniform = -1;
    g_renderer.skyRightUniform = -1;
    g_renderer.skyUpUniform = -1;
    g_renderer.postProcessTextureUniform = -1;
    g_renderer.postProcessFilmEnabledUniform = -1;
    g_renderer.postProcessColorBiasUniform = -1;
    g_renderer.postProcessColorTintBaseUniform = -1;
    g_renderer.postProcessColorTintDeltaUniform = -1;
    g_renderer.postProcessGammaExponentUniform = -1;
    g_renderer.postProcessBlurScaleUniform = -1;
    g_renderer.postProcessBlindDarkenUniform = -1;
    g_renderer.postProcessGlareLightenUniform = -1;
    g_renderer.postProcessGlowTextureUniform = -1;
    g_renderer.postProcessGlowIntensityUniform = -1;
    g_renderer.postProcessDepthTextureUniform = -1;
    g_renderer.postProcessDofEnabledUniform = -1;
    g_renderer.postProcessDofViewModelUniform = -1;
    g_renderer.postProcessDofNearUniform = -1;
    g_renderer.postProcessDofFarUniform = -1;
    g_renderer.postProcessDofDepthUniform = -1;
    g_renderer.glowTextureUniform = -1;
    g_renderer.glowModeUniform = -1;
    g_renderer.glowTexelDeltaUniform = -1;
    g_renderer.glowWeightsUniform = -1;
    g_renderer.glowSetupUniform = -1;
    g_renderer.compatibilityViewProjectionUniform = -1;
    g_renderer.compatibilityWorldUniform = -1;
    g_renderer.compatibilityTextureUniform = -1;
    g_renderer.canvasWidth = 0;
    g_renderer.canvasHeight = 0;
    g_renderer.postProcessWidth = 0;
    g_renderer.postProcessHeight = 0;
    g_renderer.multisampleWidth = 0;
    g_renderer.multisampleHeight = 0;
    g_renderer.aaConfiguredSamples = -1;
    g_renderer.aaRequestedSamples = 1;
    g_renderer.aaActiveSamples = 1;
    g_renderer.aaMaxSamples = 1;
    g_renderer.sceneSpotShadowCount = 0u;
    g_renderer.sceneSpotShadowLightIndices.fill(0u);
    for (WebRendererRetainedWorldImage &image : g_renderer.retainedWorldImages)
        image.texture = 0u;
    for (WebRendererRetainedWorldImage &image :
         g_renderer.retainedStaticModelImages)
        image.texture = 0u;
    for (WebRendererRetainedWorldImage &image :
         g_renderer.retainedDynamicModelImages)
        image.texture = 0u;
    for (WebRendererRetainedWorldImage &image : g_renderer.retainedUiImages)
        image.texture = 0u;
    for (WebRendererRetainedWorldBatch &batch :
         g_renderer.retainedWorldBatches)
    {
        batch.waterTexture = 0u;
        batch.reflectionTexture = 0u;
        batch.ownsReflectionTexture = false;
        batch.waterTextureTime = std::numeric_limits<float>::quiet_NaN();
    }
    for (WebRendererRetainedStaticModelBatch &batch :
         g_renderer.retainedStaticModelBatches)
        batch.draw.reflectionTexture = 0u;
    for (WebRendererRetainedWorldBatch &batch :
         g_renderer.retainedDynamicModelBatches)
        batch.reflectionTexture = 0u;
    g_renderer.retainedSky.texture = 0u;
    g_renderer.retainedStaticModelLighting.texture = 0u;
    g_renderer.retainedDynamicModelLighting.texture = 0u;
}

GLuint CompileShader(GLenum type, const char *source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE)
    {
        return shader;
    }

    char log[1024] = {};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    Web_Log(WebLogLevel::Error, "[kisakcod-web] Shader compilation failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
}

bool LinkProgram(
    const char *label,
    const char *vertexSource,
    const char *fragmentSource,
    GLuint &programOut)
{
    const GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0)
    {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        char log[1024] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] %s program link failed: %s\n",
            label,
            log);
        glDeleteProgram(program);
        return false;
    }
    programOut = program;
    return true;
}

bool CreateCompatibilityProgram(
    const char *vertexSource,
    const char *fragmentSource,
    GLuint &programOut,
    GLint &viewProjectionOut,
    GLint &worldOut,
    GLint &textureOut)
{
    GLuint program = 0;
    if (!LinkProgram("WebGL2 compatibility", vertexSource, fragmentSource, program))
        return false;

    while (glGetError() != GL_NO_ERROR)
    {
    }
    const GLint position = glGetAttribLocation(program, "a_position");
    const GLint color = glGetAttribLocation(program, "a_color");
    const GLint texcoord = glGetAttribLocation(program, "a_texcoord0");
    const GLint viewProjection =
        glGetUniformLocation(program, "u_viewProjectionMatrix");
    const GLint world = glGetUniformLocation(program, "u_worldMatrix");
    const GLint texture = glGetUniformLocation(program, "u_colorMapSampler");
    const GLenum error = glGetError();
    if (position != 0 || color != 1 || texcoord != 2 ||
        viewProjection < 0 || world < 0 || texture < 0 || error != GL_NO_ERROR)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] WebGL2 compatibility binding validation failed "
            "(attributes %d/%d/%d, uniforms %d/%d/%d, error 0x%x).\n",
            position,
            color,
            texcoord,
            viewProjection,
            world,
            texture,
            static_cast<unsigned int>(error));
        glDeleteProgram(program);
        return false;
    }
    programOut = program;
    viewProjectionOut = viewProjection;
    worldOut = world;
    textureOut = texture;
    return true;
}

void DeleteModelLightingTexture(
    WebRendererRetainedModelLightingAtlas &atlas);

void DeletePipelineObjects(
    GLuint program,
    GLuint skyProgram,
    GLuint postProcessProgram,
    GLuint glowProgram,
    GLuint compatibilityProgram,
    GLuint vertexArray,
    GLuint vertexBuffer,
    GLuint indexBuffer,
    GLuint texture)
{
    if (texture != 0)
    {
        glDeleteTextures(1, &texture);
    }
    if (vertexBuffer != 0)
    {
        glDeleteBuffers(1, &vertexBuffer);
    }
    if (indexBuffer != 0)
    {
        glDeleteBuffers(1, &indexBuffer);
    }
    if (vertexArray != 0)
    {
        glDeleteVertexArrays(1, &vertexArray);
    }
    if (program != 0)
    {
        glDeleteProgram(program);
    }
    if (skyProgram != 0)
    {
        glDeleteProgram(skyProgram);
    }
    if (postProcessProgram != 0)
    {
        glDeleteProgram(postProcessProgram);
    }
    if (glowProgram != 0)
    {
        glDeleteProgram(glowProgram);
    }
    if (compatibilityProgram != 0)
    {
        glDeleteProgram(compatibilityProgram);
    }
}

void DeleteGlowTargetObjects(
    const GLuint framebuffers[2], const GLuint textures[2])
{
    if (framebuffers[0] != 0u || framebuffers[1] != 0u)
        glDeleteFramebuffers(2, framebuffers);
    if (textures[0] != 0u || textures[1] != 0u)
        glDeleteTextures(2, textures);
}

void DeletePostProcessTargetObjects(
    GLuint sceneFramebuffer,
    GLuint sceneColorTexture,
    GLuint sceneDepthTexture,
    GLuint compositeFramebuffer,
    GLuint compositeColorTexture)
{
    if (sceneFramebuffer != 0u)
        glDeleteFramebuffers(1, &sceneFramebuffer);
    if (compositeFramebuffer != 0u)
        glDeleteFramebuffers(1, &compositeFramebuffer);
    if (sceneDepthTexture != 0u)
        glDeleteTextures(1, &sceneDepthTexture);
    if (sceneColorTexture != 0u)
        glDeleteTextures(1, &sceneColorTexture);
    if (compositeColorTexture != 0u)
        glDeleteTextures(1, &compositeColorTexture);
}

void DeleteMultisampleTargetObjects(
    GLuint framebuffer,
    GLuint colorRenderbuffer,
    GLuint depthRenderbuffer)
{
    if (framebuffer != 0u)
        glDeleteFramebuffers(1, &framebuffer);
    if (colorRenderbuffer != 0u)
        glDeleteRenderbuffers(1, &colorRenderbuffer);
    if (depthRenderbuffer != 0u)
        glDeleteRenderbuffers(1, &depthRenderbuffer);
}

void DeleteShadowObjects(
    GLuint shadowFramebuffer,
    GLuint shadowDepthTexture,
    GLuint shadowProgram)
{
    if (shadowFramebuffer != 0u)
        glDeleteFramebuffers(1, &shadowFramebuffer);
    if (shadowDepthTexture != 0u)
        glDeleteTextures(1, &shadowDepthTexture);
    if (shadowProgram != 0u)
        glDeleteProgram(shadowProgram);
}

void DestroyWebGLContext()
{
    if (g_renderer.context <= 0)
    {
        ResetGpuHandles();
        g_renderer.contextLost = false;
        g_renderer.initialized = false;
        return;
    }

    // Callback registration can fail partway through initialization. Remove
    // both handlers before destroying the context so a queued loss/restore
    // event cannot publish a recovered runtime for an initialization that
    // never completed.
    WebRendererContext_UnregisterCallbacks();

    if (WebRendererContext_MakeCurrent(g_renderer.context))
    {
        DeleteWorldTextureObjects(g_renderer.retainedWorldImages);
        DeleteWaterTextureObjects(g_renderer.retainedWorldBatches);
        DeleteWorldTextureObjects(g_renderer.retainedStaticModelImages);
        DeleteWorldTextureObjects(g_renderer.retainedDynamicModelImages);
        DeleteWorldTextureObjects(g_renderer.retainedUiImages);
        if (g_renderer.retainedSky.texture != 0u)
            glDeleteTextures(1, &g_renderer.retainedSky.texture);
        g_renderer.retainedSky.texture = 0u;
        DeleteModelLightingTexture(g_renderer.retainedStaticModelLighting);
        DeleteModelLightingTexture(g_renderer.retainedDynamicModelLighting);
        if (g_renderer.staticModelInstanceBuffer != 0u)
            glDeleteBuffers(1, &g_renderer.staticModelInstanceBuffer);
        DeleteSurfaceObjects(
            g_renderer.staticModelVertexArray,
            g_renderer.staticModelVertexBuffer,
            g_renderer.staticModelIndexBuffer);
        DeleteSurfaceObjects(
            g_renderer.dynamicModelVertexArray,
            g_renderer.dynamicModelVertexBuffer,
            g_renderer.dynamicModelIndexBuffer);
        DeleteSurfaceObjects(g_renderer.uiVertexArray,
            g_renderer.uiVertexBuffer, g_renderer.uiIndexBuffer);
        DeletePostProcessTargetObjects(
            g_renderer.sceneFramebuffer,
            g_renderer.sceneColorTexture,
            g_renderer.sceneDepthTexture,
            g_renderer.compositeFramebuffer,
            g_renderer.compositeColorTexture);
        DeleteMultisampleTargetObjects(
            g_renderer.multisampleFramebuffer,
            g_renderer.multisampleColorRenderbuffer,
            g_renderer.multisampleDepthRenderbuffer);
        DeleteGlowTargetObjects(
            g_renderer.glowFramebuffers, g_renderer.glowColorTextures);
        DeleteShadowObjects(
            g_renderer.shadowFramebuffer,
            g_renderer.shadowDepthTexture,
            g_renderer.shadowProgram);
        DeleteShadowObjects(
            g_renderer.shadowFarFramebuffer,
            g_renderer.shadowFarDepthTexture,
            0u);
        for (std::size_t index = 0u; index < MAX_SPOT_SHADOWS; ++index)
        {
            DeleteShadowObjects(g_renderer.spotShadowFramebuffers[index],
                g_renderer.spotShadowDepthTextures[index], 0u);
        }
        if (g_renderer.sunVisibilityQueries[0] != 0u ||
            g_renderer.sunVisibilityQueries[1] != 0u)
        {
            glDeleteQueries(2, g_renderer.sunVisibilityQueries);
        }
#if KISAK_WEB_DIAGNOSTICS
        ResetFrameProfileGpuQueries(true);
#endif
        DeletePipelineObjects(
            g_renderer.program,
            g_renderer.skyProgram,
            g_renderer.postProcessProgram,
            g_renderer.glowProgram,
            g_renderer.compatibilityProgram,
            g_renderer.vertexArray,
            g_renderer.vertexBuffer,
            g_renderer.indexBuffer,
            g_renderer.texture);
    }
    ResetGpuHandles();
    WebRendererContext_Destroy(g_renderer.context);
    g_renderer.contextLost = false;
    g_renderer.initialized = false;
}

void DeleteSurfaceObjects(GLuint vertexArray, GLuint vertexBuffer, GLuint indexBuffer)
{
    if (vertexBuffer != 0)
    {
        glDeleteBuffers(1, &vertexBuffer);
    }
    if (indexBuffer != 0)
    {
        glDeleteBuffers(1, &indexBuffer);
    }
    if (vertexArray != 0)
    {
        glDeleteVertexArrays(1, &vertexArray);
    }
}

void DeleteStaticModelObjects(
    GLuint vertexArray,
    GLuint vertexBuffer,
    GLuint indexBuffer,
    GLuint instanceBuffer)
{
    if (instanceBuffer != 0u)
        glDeleteBuffers(1, &instanceBuffer);
    DeleteSurfaceObjects(vertexArray, vertexBuffer, indexBuffer);
}

void DeleteWorldTextureObjects(
    std::vector<WebRendererRetainedWorldImage> &images)
{
    for (WebRendererRetainedWorldImage &image : images)
    {
        if (image.texture != 0u)
            glDeleteTextures(1, &image.texture);
        image.texture = 0u;
    }
}

void DeleteWaterTextureObjects(
    std::vector<WebRendererRetainedWorldBatch> &batches)
{
    for (WebRendererRetainedWorldBatch &batch : batches)
    {
        if (batch.waterTexture != 0u)
            glDeleteTextures(1, &batch.waterTexture);
        if (batch.ownsReflectionTexture && batch.reflectionTexture != 0u)
            glDeleteTextures(1, &batch.reflectionTexture);
        batch.waterTexture = 0u;
        batch.reflectionTexture = 0u;
        batch.ownsReflectionTexture = false;
        batch.waterTextureTime = std::numeric_limits<float>::quiet_NaN();
    }
}

template <typename Index>
bool CreateSurfaceObjects(
    const std::vector<WebRendererSurfaceVertex> &vertices,
    const std::vector<Index> &indices,
    GLuint &vertexArrayOut,
    GLuint &vertexBufferOut,
    GLuint &indexBufferOut)
{
    while (glGetError() != GL_NO_ERROR)
    {
    }

    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;
    GLuint indexBuffer = 0;
    glGenVertexArrays(1, &vertexArray);
    glBindVertexArray(vertexArray);
    glGenBuffers(1, &vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(WebRendererSurfaceVertex)),
        vertices.data(),
        GL_STATIC_DRAW);
    glGenBuffers(1, &indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(Index)),
        indices.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(WebRendererSurfaceVertex),
        reinterpret_cast<const void *>(offsetof(WebRendererSurfaceVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(WebRendererSurfaceVertex),
        reinterpret_cast<const void *>(offsetof(WebRendererSurfaceVertex, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(WebRendererSurfaceVertex),
        reinterpret_cast<const void *>(offsetof(WebRendererSurfaceVertex, textureCoordinate)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(
        3,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(WebRendererSurfaceVertex),
        reinterpret_cast<const void *>(offsetof(WebRendererSurfaceVertex, lightmapCoordinate)));
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(
        8,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(WebRendererSurfaceVertex),
        reinterpret_cast<const void *>(
            offsetof(WebRendererSurfaceVertex, normal)));
    glEnableVertexAttribArray(10);
    glVertexAttribPointer(
        10,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(WebRendererSurfaceVertex),
        reinterpret_cast<const void *>(
            offsetof(WebRendererSurfaceVertex, tangent)));
    glEnableVertexAttribArray(11);
    glVertexAttribPointer(
        11,
        1,
        GL_FLOAT,
        GL_FALSE,
        sizeof(WebRendererSurfaceVertex),
        reinterpret_cast<const void *>(
            offsetof(WebRendererSurfaceVertex, binormalSign)));

    const GLenum error = glGetError();
    if (vertexArray == 0 || vertexBuffer == 0 || indexBuffer == 0 ||
        error != GL_NO_ERROR)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] WebGL2 indexed surface creation failed (0x%x).\n",
            static_cast<unsigned int>(error));
        DeleteSurfaceObjects(vertexArray, vertexBuffer, indexBuffer);
        return false;
    }

    vertexArrayOut = vertexArray;
    vertexBufferOut = vertexBuffer;
    indexBufferOut = indexBuffer;
    return true;
}

bool CreateStaticModelObjects(
    const std::vector<WebRendererSurfaceVertex> &vertices,
    const std::vector<std::uint32_t> &indices,
    const std::vector<WebRendererStaticModelInstanceDesc> &instances,
    GLuint &vertexArrayOut,
    GLuint &vertexBufferOut,
    GLuint &indexBufferOut,
    GLuint &instanceBufferOut)
{
    GLuint vertexArray = 0u;
    GLuint vertexBuffer = 0u;
    GLuint indexBuffer = 0u;
    if (!CreateSurfaceObjects(
        vertices, indices, vertexArray, vertexBuffer, indexBuffer))
    {
        return false;
    }
    glBindVertexArray(vertexArray);
    GLuint instanceBuffer = 0u;
    glGenBuffers(1, &instanceBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(
            instances.size() * sizeof(WebRendererStaticModelInstanceDesc)),
        instances.data(),
        GL_DYNAMIC_DRAW);
    constexpr std::size_t AXIS_OFFSET =
        offsetof(WebRendererStaticModelInstanceDesc, axis);
    for (GLuint row = 0u; row < 3u; ++row)
    {
        const GLuint location = 4u + row;
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(
            location,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(WebRendererStaticModelInstanceDesc),
            reinterpret_cast<const void *>(
                AXIS_OFFSET + row * 3u * sizeof(float)));
        glVertexAttribDivisor(location, 1u);
    }
    glEnableVertexAttribArray(7u);
    glVertexAttribPointer(
        7u,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(WebRendererStaticModelInstanceDesc),
        reinterpret_cast<const void *>(
            offsetof(WebRendererStaticModelInstanceDesc, origin)));
    glVertexAttribDivisor(7u, 1u);
    glEnableVertexAttribArray(9u);
    glVertexAttribPointer(
        9u,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(WebRendererStaticModelInstanceDesc),
        reinterpret_cast<const void *>(offsetof(
            WebRendererStaticModelInstanceDesc,
            modelLightingCoordinates)));
    glVertexAttribDivisor(9u, 1u);
    const GLenum error = glGetError();
    if (instanceBuffer == 0u || error != GL_NO_ERROR)
    {
        Web_Log(WebLogLevel::Error,
            "[kisakcod-web] WebGL2 static XModel instance-buffer creation "
            "failed (0x%x).\n",
            static_cast<unsigned int>(error));
        DeleteStaticModelObjects(
            vertexArray, vertexBuffer, indexBuffer, instanceBuffer);
        return false;
    }
    vertexArrayOut = vertexArray;
    vertexBufferOut = vertexBuffer;
    indexBufferOut = indexBuffer;
    instanceBufferOut = instanceBuffer;
    return true;
}

bool CreateTextureObject(
    const std::uint8_t *pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::uint8_t samplerState,
    GLuint &textureOut)
{
    while (glGetError() != GL_NO_ERROR)
    {
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    const GLenum allocationError = glGetError();
    if (texture == 0 || allocationError != GL_NO_ERROR)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] WebGL2 texture allocation failed (0x%x).\n",
            static_cast<unsigned int>(allocationError));
        if (texture != 0)
        {
            glDeleteTextures(1, &texture);
        }
        return false;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    const std::uint8_t filter = samplerState & 0x07u;
    const GLint glFilter = samplerState == 0u || filter == 1u
        ? GL_NEAREST
        : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        samplerState == 0u || (samplerState & 0x20u) != 0u
            ? GL_CLAMP_TO_EDGE
            : GL_REPEAT);
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        samplerState == 0u || (samplerState & 0x40u) != 0u
            ? GL_CLAMP_TO_EDGE
            : GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels);

    const GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] WebGL2 rejected the RGBA8 texture upload (0x%x).\n",
            static_cast<unsigned int>(error));
        glDeleteTextures(1, &texture);
        return false;
    }

    textureOut = texture;
    return true;
}

bool CreateCubeTextureObject(
    const kisak::iwi::Rgba8Cube &cube,
    std::uint8_t samplerState,
    GLenum textureUnit,
    const char *label,
    GLuint &textureOut)
{
    if (cube.edgeLength == 0u) return false;
    if (textureOut != 0u) return true;
    const std::size_t expectedFaceBytes =
        static_cast<std::size_t>(cube.edgeLength) *
        cube.edgeLength * 4u;
    for (const auto &face : cube.faces)
        if (face.size() != expectedFaceBytes) return false;
    for (std::size_t mip = 0u; mip < cube.mipFaces.size(); ++mip)
    {
        const std::uint32_t edgeLength = std::max<std::uint32_t>(
            static_cast<std::uint32_t>(cube.edgeLength) >> (mip + 1u), 1u);
        const std::size_t expectedMipFaceBytes =
            static_cast<std::size_t>(edgeLength) * edgeLength * 4u;
        for (const auto &face : cube.mipFaces[mip])
            if (face.size() != expectedMipFaceBytes) return false;
    }

    while (glGetError() != GL_NO_ERROR)
    {
    }
    GLuint texture = 0u;
    glGenTextures(1, &texture);
    glActiveTexture(textureUnit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    const std::uint8_t filter = samplerState & 0x07u;
    const GLint glFilter = filter == 1u ? GL_NEAREST : GL_LINEAR;
    const bool hasMipmaps = cube.edgeLength > 1u;
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
        hasMipmaps
            ? (glFilter == GL_LINEAR
                ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST)
            : glFilter);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, glFilter);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (std::size_t face = 0u; face < cube.faces.size(); ++face)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X +
                static_cast<GLenum>(face),
            0, GL_RGBA8, static_cast<GLsizei>(cube.edgeLength),
            static_cast<GLsizei>(cube.edgeLength), 0, GL_RGBA,
            GL_UNSIGNED_BYTE, cube.faces[face].data());
    }
    for (std::size_t mip = 0u; mip < cube.mipFaces.size(); ++mip)
    {
        const std::uint32_t edgeLength = std::max<std::uint32_t>(
            static_cast<std::uint32_t>(cube.edgeLength) >> (mip + 1u), 1u);
        for (std::size_t face = 0u; face < cube.faces.size(); ++face)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X +
                    static_cast<GLenum>(face),
                static_cast<GLint>(mip + 1u), GL_RGBA8,
                static_cast<GLsizei>(edgeLength),
                static_cast<GLsizei>(edgeLength), 0, GL_RGBA,
                GL_UNSIGNED_BYTE, cube.mipFaces[mip][face].data());
        }
    }
    if (hasMipmaps && cube.mipFaces.empty())
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    const GLenum error = glGetError();
    glActiveTexture(GL_TEXTURE0);
    if (texture == 0u || error != GL_NO_ERROR)
    {
        if (texture != 0u) glDeleteTextures(1, &texture);
        Web_Log(WebLogLevel::Error,
            "[kisakcod-web] WebGL2 %s cubemap upload failed (0x%x).\n",
            label,
            static_cast<unsigned int>(error));
        return false;
    }
    textureOut = texture;
    return true;
}

bool CreateSkyTextureObject(WebRendererRetainedSkyImage &sky)
{
    if (!sky.active) return true;
    return CreateCubeTextureObject(sky.cube, sky.samplerState, GL_TEXTURE4,
        "sky", sky.texture);
}

bool CreateWaterTextureObjects(
    std::vector<WebRendererRetainedWorldBatch> &batches)
{
    for (std::size_t batchIndex = 0u;
         batchIndex < batches.size(); ++batchIndex)
    {
        WebRendererRetainedWorldBatch &batch = batches[batchIndex];
        if (batch.technique == WebRendererWorldTechnique::WaterLitSun)
        {
            water_t water{};
            water.H0 = batch.waterH0.data();
            water.wTerm = batch.waterWTerm.data();
            water.M = batch.waterM;
            water.N = batch.waterN;
            if (!R_GenerateWaterPixelsR8(&water, 0.0f,
                    batch.waterPixels.data(), batch.waterPixels.size()))
                return false;

            glGenTextures(1, &batch.waterTexture);
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, batch.waterTexture);
            const std::uint8_t filter = batch.waterSamplerState & 0x07u;
            const GLint glFilter = filter == 1u ? GL_NEAREST : GL_LINEAR;
            const bool mipped =
                (batch.waterSamplerState & 0x18u) != 0u;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                mipped
                    ? (glFilter == GL_LINEAR
                        ? GL_LINEAR_MIPMAP_LINEAR
                        : GL_NEAREST_MIPMAP_NEAREST)
                    : glFilter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                (batch.waterSamplerState & 0x20u) != 0u
                    ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                (batch.waterSamplerState & 0x40u) != 0u
                    ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, batch.waterM, batch.waterN,
                0, GL_RED, GL_UNSIGNED_BYTE, batch.waterPixels.data());
            glGenerateMipmap(GL_TEXTURE_2D);
            batch.waterTextureTime = 0.0f;
        }
        if (batch.reflectionCube.edgeLength != 0u)
        {
            if (!CreateCubeTextureObject(batch.reflectionCube, 2u,
                    GL_TEXTURE8, "reflection-probe",
                    batch.reflectionTexture))
            {
                DeleteWaterTextureObjects(batches);
                return false;
            }
            batch.ownsReflectionTexture = true;
        }
        else if (batch.technique == WebRendererWorldTechnique::WaterLitSun ||
            WebRenderer_UsesWorldSpecularMap(batch.technique))
        {
            const auto shared = std::find_if(batches.begin(),
                batches.begin() + static_cast<std::ptrdiff_t>(batchIndex),
                [&batch](const WebRendererRetainedWorldBatch &candidate)
                {
                    return candidate.reflectionProbeIndex ==
                            batch.reflectionProbeIndex &&
                        candidate.reflectionTexture != 0u;
                });
            if (shared != batches.begin() +
                    static_cast<std::ptrdiff_t>(batchIndex))
            {
                batch.reflectionTexture = shared->reflectionTexture;
            }
        }
        const bool requiresReflection =
            batch.technique == WebRendererWorldTechnique::WaterLitSun ||
            WebRenderer_UsesWorldSpecularMap(batch.technique);
        if ((batch.technique == WebRendererWorldTechnique::WaterLitSun &&
                batch.waterTexture == 0u) ||
            (requiresReflection && batch.reflectionTexture == 0u) ||
            glGetError() != GL_NO_ERROR)
        {
            DeleteWaterTextureObjects(batches);
            return false;
        }
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glActiveTexture(GL_TEXTURE0);
    return true;
}

bool CreateWorldTextureObjects(
    std::vector<WebRendererRetainedWorldImage> &images)
{
    for (WebRendererRetainedWorldImage &image : images)
    {
        if (!image.supported) continue;
        if (image.texture != 0u) continue;
        kisak::iwi::Rgba8Image decoded;
        kisak::iwi::Error decodeError = kisak::iwi::Error::None;
        if (image.recoverySource == WebRendererImageRecoverySource::LoadDef)
        {
            decodeError = kisak::iwi::DecodeLoadDefRgba8(
                image.sourceFormat,
                image.sourceFlags,
                static_cast<std::uint16_t>(image.sourceDimensions[0]),
                static_cast<std::uint16_t>(image.sourceDimensions[1]),
                static_cast<std::uint16_t>(image.sourceDimensions[2]),
                image.encodedSource,
                decoded);
        }
        else if (image.recoverySource ==
            WebRendererImageRecoverySource::IwiMember)
        {
            decodeError = kisak::iwi::DecodeRgba8(
                image.encodedSource, decoded);
        }
        const bool encoded = image.recoverySource !=
            WebRendererImageRecoverySource::DecodedRgba8;
        if (encoded && (decodeError != kisak::iwi::Error::None ||
                decoded.width != image.width || decoded.height != image.height))
        {
            Web_Log(WebLogLevel::Error,
                "[kisakcod-web] Canonical recovery source '%s' could not "
                "be decoded for WebGL upload: %s.\n",
                image.canonicalName.c_str(),
                kisak::iwi::ErrorString(decodeError));
            DeleteWorldTextureObjects(images);
            return false;
        }
        const std::vector<std::uint8_t> &pixels = encoded
            ? decoded.pixels : image.pixels;
        const std::vector<std::vector<std::uint8_t>> &mipPixels =
            encoded && image.authoredMipChain
            ? decoded.mipPixels : image.mipPixels;
        if (!CreateTextureObject(
                pixels.data(), image.width, image.height, 2u,
                image.texture))
        {
            DeleteWorldTextureObjects(images);
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, image.texture);
        if (mipPixels.empty() && image.mipmapsAllowed)
        {
            glGenerateMipmap(GL_TEXTURE_2D);
        }
        else if (mipPixels.empty())
        {
            // Native Load_Texture creates a one-level D3D texture when the
            // canonical load definition carries IMG_FLAG_NOMIPMAPS. Do not
            // manufacture averaged levels at the WebGL boundary: alpha-cutout
            // art deliberately relies on its authored base-level coverage.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        }
        else
        {
            for (std::size_t mip = 0u; mip < mipPixels.size(); ++mip)
            {
                const std::uint32_t width = std::max<std::uint32_t>(
                    image.width >> (mip + 1u), 1u);
                const std::uint32_t height = std::max<std::uint32_t>(
                    image.height >> (mip + 1u), 1u);
                const std::size_t expectedBytes =
                    static_cast<std::size_t>(width) * height * 4u;
                if (mipPixels[mip].size() != expectedBytes)
                {
                    DeleteWorldTextureObjects(images);
                    return false;
                }
                glTexImage2D(GL_TEXTURE_2D,
                    static_cast<GLint>(mip + 1u), GL_RGBA8,
                    static_cast<GLsizei>(width),
                    static_cast<GLsizei>(height), 0, GL_RGBA,
                    GL_UNSIGNED_BYTE, mipPixels[mip].data());
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                static_cast<GLint>(mipPixels.size()));
        }
        if (glGetError() != GL_NO_ERROR)
        {
            DeleteWorldTextureObjects(images);
            return false;
        }
    }
    return true;
}

bool CreateMultisampleTarget(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    const int configuredSamples = r_aaSamples
        ? std::max(1, r_aaSamples->current.integer)
        : 1;
    const int requestedSamples = std::min(configuredSamples, 4);
    if (g_renderer.aaConfiguredSamples == configuredSamples &&
        g_renderer.multisampleWidth == width &&
        g_renderer.multisampleHeight == height)
    {
        return g_renderer.aaActiveSamples > 1 &&
            g_renderer.multisampleFramebuffer != 0u;
    }

    DeleteMultisampleTargetObjects(
        g_renderer.multisampleFramebuffer,
        g_renderer.multisampleColorRenderbuffer,
        g_renderer.multisampleDepthRenderbuffer);
    g_renderer.multisampleFramebuffer = 0u;
    g_renderer.multisampleColorRenderbuffer = 0u;
    g_renderer.multisampleDepthRenderbuffer = 0u;
    g_renderer.multisampleWidth = width;
    g_renderer.multisampleHeight = height;
    g_renderer.aaConfiguredSamples = configuredSamples;
    g_renderer.aaRequestedSamples = requestedSamples;
    g_renderer.aaActiveSamples = 1;

    GLint maxSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    g_renderer.aaMaxSamples = std::max(1, static_cast<int>(maxSamples));

    if (configuredSamples > 4)
    {
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] r_aaSamples=%d is capped to the browser "
            "port's supported 4x maximum.\n",
            configuredSamples);
    }

    if (requestedSamples <= 1)
    {
        DispatchRendererAaLifecycle(
            "disabled",
            "Anti-aliasing is disabled because r_aaSamples is 1",
            configuredSamples,
            requestedSamples,
            1,
            g_renderer.aaMaxSamples,
            width,
            height,
            g_renderer.aaResourceGeneration,
            false);
        return false;
    }

    const int firstCandidate = std::min(
        requestedSamples, g_renderer.aaMaxSamples);
    while (glGetError() != GL_NO_ERROR)
    {
        // Framebuffer allocation reports its own error below. Do not let a
        // stale error from an unrelated draw reject a supported sample count.
    }
    for (int samples = firstCandidate; samples >= 2; --samples)
    {
        GLuint framebuffer = 0u;
        GLuint colorRenderbuffer = 0u;
        GLuint depthRenderbuffer = 0u;
        glGenFramebuffers(1, &framebuffer);
        glGenRenderbuffers(1, &colorRenderbuffer);
        glGenRenderbuffers(1, &depthRenderbuffer);

        glBindRenderbuffer(GL_RENDERBUFFER, colorRenderbuffer);
        glRenderbufferStorageMultisample(
            GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
        glRenderbufferStorageMultisample(
            GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0u);

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_RENDERBUFFER, colorRenderbuffer);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            GL_RENDERBUFFER, depthRenderbuffer);
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        const GLenum error = glGetError();
        glBindFramebuffer(GL_FRAMEBUFFER, 0u);

        if (status == GL_FRAMEBUFFER_COMPLETE && error == GL_NO_ERROR)
        {
            g_renderer.multisampleFramebuffer = framebuffer;
            g_renderer.multisampleColorRenderbuffer = colorRenderbuffer;
            g_renderer.multisampleDepthRenderbuffer = depthRenderbuffer;
            g_renderer.aaActiveSamples = samples;
            ++g_renderer.aaResourceGeneration;
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Using %dx anti-aliasing "
                "(requested=%d, WebGL2 max=%d, %dx%d).\n",
                samples, requestedSamples, g_renderer.aaMaxSamples,
                width, height);
            DispatchRendererAaLifecycle(
                "ready",
                "COD4 scene multisampling is active and resolves before post effects",
                configuredSamples,
                requestedSamples,
                samples,
                g_renderer.aaMaxSamples,
                width,
                height,
                g_renderer.aaResourceGeneration,
                true);
            return true;
        }

        DeleteMultisampleTargetObjects(
            framebuffer, colorRenderbuffer, depthRenderbuffer);
    }

    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] WebGL2 could not create the requested %dx "
        "anti-aliasing target; rendering without anti-aliasing.\n",
        requestedSamples);
    DispatchRendererAaLifecycle(
        "fallback",
        "WebGL2 rejected every multisample count, so rendering fell back to 1x",
        configuredSamples,
        requestedSamples,
        1,
        g_renderer.aaMaxSamples,
        width,
        height,
        g_renderer.aaResourceGeneration,
        false);
    return false;
}

bool ResolveMultisampleTarget(
    GLuint destinationFramebuffer,
    int width,
    int height,
    bool resolveDepth)
{
    if (g_renderer.multisampleFramebuffer == 0u ||
        g_renderer.aaActiveSamples <= 1)
        return false;

    glBindFramebuffer(
        GL_READ_FRAMEBUFFER, g_renderer.multisampleFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destinationFramebuffer);
    const GLbitfield mask = GL_COLOR_BUFFER_BIT |
        (resolveDepth ? GL_DEPTH_BUFFER_BIT : 0u);
    glBlitFramebuffer(
        0, 0, width, height,
        0, 0, width, height,
        mask, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, destinationFramebuffer);
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR)
        return true;

    Web_Log(WebLogLevel::Error,
        "[kisakcod-web] %dx anti-aliasing resolve failed (0x%x); "
        "the target will be rebuilt.\n",
        g_renderer.aaActiveSamples,
        static_cast<unsigned int>(error));
    const int configuredSamples = g_renderer.aaConfiguredSamples;
    DeleteMultisampleTargetObjects(
        g_renderer.multisampleFramebuffer,
        g_renderer.multisampleColorRenderbuffer,
        g_renderer.multisampleDepthRenderbuffer);
    g_renderer.multisampleFramebuffer = 0u;
    g_renderer.multisampleColorRenderbuffer = 0u;
    g_renderer.multisampleDepthRenderbuffer = 0u;
    g_renderer.aaConfiguredSamples = -1;
    g_renderer.aaActiveSamples = 1;
    DispatchRendererAaLifecycle(
        "failed",
        "The multisample resolve failed and the target will be rebuilt",
        configuredSamples,
        g_renderer.aaRequestedSamples,
        1,
        g_renderer.aaMaxSamples,
        width,
        height,
        g_renderer.aaResourceGeneration,
        false);
    return false;
}

bool CreatePostProcessTargets(int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    if (g_renderer.sceneFramebuffer != 0u &&
        g_renderer.compositeFramebuffer != 0u &&
        g_renderer.glowFramebuffers[0] != 0u &&
        g_renderer.glowFramebuffers[1] != 0u &&
        g_renderer.postProcessWidth == width &&
        g_renderer.postProcessHeight == height)
        return true;

    DeletePostProcessTargetObjects(
        g_renderer.sceneFramebuffer,
        g_renderer.sceneColorTexture,
        g_renderer.sceneDepthTexture,
        g_renderer.compositeFramebuffer,
        g_renderer.compositeColorTexture);
    DeleteGlowTargetObjects(
        g_renderer.glowFramebuffers, g_renderer.glowColorTextures);
    g_renderer.sceneFramebuffer = 0u;
    g_renderer.sceneColorTexture = 0u;
    g_renderer.sceneDepthTexture = 0u;
    g_renderer.compositeFramebuffer = 0u;
    g_renderer.compositeColorTexture = 0u;
    std::fill_n(g_renderer.glowFramebuffers, 2u, 0u);
    std::fill_n(g_renderer.glowColorTextures, 2u, 0u);
    g_renderer.postProcessWidth = 0;
    g_renderer.postProcessHeight = 0;

    GLuint sceneFramebuffer = 0u;
    GLuint sceneColorTexture = 0u;
    GLuint sceneDepthTexture = 0u;
    GLuint compositeFramebuffer = 0u;
    GLuint compositeColorTexture = 0u;
    GLuint glowFramebuffers[2]{};
    GLuint glowColorTextures[2]{};
    while (glGetError() != GL_NO_ERROR)
    {
    }
    glGenTextures(1, &sceneColorTexture);
    glBindTexture(GL_TEXTURE_2D, sceneColorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenTextures(1, &sceneDepthTexture);
    glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
        GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glGenFramebuffers(1, &sceneFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, sceneColorTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D, sceneDepthTexture, 0);
    const GLenum sceneStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    glGenTextures(1, &compositeColorTexture);
    glBindTexture(GL_TEXTURE_2D, compositeColorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &compositeFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, compositeFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, compositeColorTexture, 0);
    const GLenum compositeStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    const int glowWidth = std::max(1, width / 4);
    const int glowHeight = std::max(1, height / 4);
    GLenum glowStatus[2]{};
    glGenTextures(2, glowColorTextures);
    glGenFramebuffers(2, glowFramebuffers);
    for (std::size_t index = 0u; index < 2u; ++index)
    {
        glBindTexture(GL_TEXTURE_2D, glowColorTextures[index]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
            glowWidth, glowHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, glowFramebuffers[index]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, glowColorTextures[index], 0);
        glowStatus[index] = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    }
    const GLenum error = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0u);
    glBindTexture(GL_TEXTURE_2D, 0u);

    if (sceneFramebuffer == 0u || sceneColorTexture == 0u ||
        sceneDepthTexture == 0u || compositeFramebuffer == 0u ||
        compositeColorTexture == 0u ||
        glowFramebuffers[0] == 0u || glowFramebuffers[1] == 0u ||
        glowColorTextures[0] == 0u || glowColorTextures[1] == 0u ||
        sceneStatus != GL_FRAMEBUFFER_COMPLETE ||
        compositeStatus != GL_FRAMEBUFFER_COMPLETE ||
        glowStatus[0] != GL_FRAMEBUFFER_COMPLETE ||
        glowStatus[1] != GL_FRAMEBUFFER_COMPLETE ||
        error != GL_NO_ERROR)
    {
        DeletePostProcessTargetObjects(sceneFramebuffer, sceneColorTexture,
            sceneDepthTexture, compositeFramebuffer,
            compositeColorTexture);
        DeleteGlowTargetObjects(glowFramebuffers, glowColorTextures);
        Web_Log(WebLogLevel::Error,
            "[kisakcod-web] WebGL2 post-effect target creation failed "
            "(scene=0x%x composite=0x%x glow=0x%x/0x%x error=0x%x).\n",
            static_cast<unsigned int>(sceneStatus),
            static_cast<unsigned int>(compositeStatus),
            static_cast<unsigned int>(glowStatus[0]),
            static_cast<unsigned int>(glowStatus[1]),
            static_cast<unsigned int>(error));
        return false;
    }

    g_renderer.sceneFramebuffer = sceneFramebuffer;
    g_renderer.sceneColorTexture = sceneColorTexture;
    g_renderer.sceneDepthTexture = sceneDepthTexture;
    g_renderer.compositeFramebuffer = compositeFramebuffer;
    g_renderer.compositeColorTexture = compositeColorTexture;
    std::copy_n(glowFramebuffers, 2u, g_renderer.glowFramebuffers);
    std::copy_n(glowColorTextures, 2u, g_renderer.glowColorTextures);
    g_renderer.postProcessWidth = width;
    g_renderer.postProcessHeight = height;
    return true;
}

bool CreateShadowTarget(
    GLsizei shadowSize,
    const char *label,
    GLuint &framebufferOut,
    GLuint &depthTextureOut)
{
    GLuint framebuffer = 0u;
    GLuint depthTexture = 0u;
    while (glGetError() != GL_NO_ERROR)
    {
    }
    glGenTextures(1, &depthTexture);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
        GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
        shadowSize, shadowSize, 0, GL_DEPTH_COMPONENT,
        GL_UNSIGNED_INT, nullptr);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D, depthTexture, 0);
    const GLenum noColor = GL_NONE;
    glDrawBuffers(1, &noColor);
    glReadBuffer(GL_NONE);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    const GLenum error = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0u);
    glBindTexture(GL_TEXTURE_2D, 0u);
    if (framebuffer == 0u || depthTexture == 0u ||
        status != GL_FRAMEBUFFER_COMPLETE || error != GL_NO_ERROR)
    {
        DeleteShadowObjects(framebuffer, depthTexture, 0u);
        Web_Log(WebLogLevel::Error,
            "[kisakcod-web] WebGL2 %s-shadow target creation failed "
            "(status=0x%x error=0x%x).\n",
            label,
            static_cast<unsigned int>(status),
            static_cast<unsigned int>(error));
        return false;
    }
    framebufferOut = framebuffer;
    depthTextureOut = depthTexture;
    return true;
}

bool CreateRendererResources()
{
    constexpr const char *vertexSource = R"glsl(#version 300 es
        precision highp float;
        precision highp int;
        layout(location = 0) in vec3 a_position;
        layout(location = 1) in vec4 a_color;
        layout(location = 2) in vec2 a_texcoord;
        layout(location = 3) in vec2 a_lightmap_coord;
        layout(location = 4) in vec3 a_instance_axis0;
        layout(location = 5) in vec3 a_instance_axis1;
        layout(location = 6) in vec3 a_instance_axis2;
        layout(location = 7) in vec3 a_instance_origin;
        layout(location = 8) in vec3 a_normal;
        layout(location = 9) in vec3 a_instance_model_lighting_coords;
        layout(location = 10) in vec3 a_tangent;
        layout(location = 11) in float a_binormal_sign;
        uniform float u_aspect;
        uniform mat4 u_view_projection;
        uniform float u_scene_fallback;
        uniform float u_instance_enabled;
        uniform vec3 u_model_lighting_base_coords;
        uniform int u_material_mode;
        uniform vec3 u_view_origin;
        uniform vec4 u_falloff_parms;
        uniform vec4 u_falloff_begin_color;
        uniform vec4 u_falloff_end_color;
        out vec4 v_color;
        out vec2 v_texcoord;
        out vec3 v_world_position;
        out vec2 v_lightmap_coord;
        out vec3 v_model_normal;
        out vec3 v_model_tangent;
        out float v_binormal_sign;
        out vec3 v_model_lighting_coords;

        void main()
        {
            vec3 position = a_position;
            vec3 model_normal = a_normal;
            vec3 model_tangent = a_tangent;
            v_model_lighting_coords = u_model_lighting_base_coords;
            if (u_instance_enabled > 0.5)
            {
                position = a_instance_origin +
                    a_position.x * a_instance_axis0 +
                    a_position.y * a_instance_axis1 +
                    a_position.z * a_instance_axis2;
                model_normal =
                    a_normal.x * a_instance_axis0 +
                    a_normal.y * a_instance_axis1 +
                    a_normal.z * a_instance_axis2;
                model_tangent =
                    a_tangent.x * a_instance_axis0 +
                    a_tangent.y * a_instance_axis1 +
                    a_tangent.z * a_instance_axis2;
                v_model_lighting_coords =
                    a_instance_model_lighting_coords;
            }
            vec3 world_position = position;
            position.x *= min(1.0, u_aspect);
            position.y *= min(1.0, 1.0 / u_aspect);
            gl_Position = u_view_projection * vec4(position, 1.0);
            v_color = a_color;
            if (u_material_mode == 4)
            {
                // Exact vertcol_simple_fog_df vertex arithmetic. The native
                // declaration routes the same TEXCOORD0 pair used for the
                // base texture into the horizontal distance calculation.
                float falloff = clamp(u_falloff_parms.x *
                    length(u_view_origin.xy - a_texcoord.xy) +
                    u_falloff_parms.y, 0.0, 1.0);
                v_color.rgb *= mix(u_falloff_end_color.rgb,
                    u_falloff_begin_color.rgb, falloff);
                v_color.a *= falloff;
            }
            v_texcoord = a_texcoord;
            v_world_position = world_position;
            v_lightmap_coord = a_lightmap_coord;
            v_model_normal = model_normal;
            v_model_tangent = model_tangent;
            v_binormal_sign = a_binormal_sign;
        }
    )glsl";

    constexpr const char *fragmentSource = R"glsl(#version 300 es
        precision highp float;
        precision highp int;
        precision highp sampler3D;
        in vec4 v_color;
        in vec2 v_texcoord;
        in vec3 v_world_position;
        in vec2 v_lightmap_coord;
        in vec3 v_model_normal;
        in vec3 v_model_tangent;
        in float v_binormal_sign;
        in vec3 v_model_lighting_coords;
        uniform sampler2D u_texture;
        uniform sampler2D u_detail_map;
        uniform sampler2D u_secondary_lightmap;
        uniform sampler3D u_model_lighting;
        uniform sampler2D u_normal_map;
        uniform sampler2D u_specular_map;
        uniform float u_texture_enabled;
        uniform float u_detail_map_enabled;
        uniform vec4 u_detail_scale;
        uniform float u_lightmap_enabled;
        uniform float u_secondary_lightmap_enabled;
        uniform float u_model_lighting_enabled;
        uniform float u_normal_map_enabled;
        uniform float u_specular_map_enabled;
        uniform vec3 u_model_lighting_lookup_scale;
        uniform float u_premultiply_alpha;
        uniform float u_color_intensity_alpha;
        uniform int u_material_mode;
        uniform vec4 u_falloff_parms;
        uniform float u_scene_fallback;
        uniform int u_alpha_test;
        uniform vec4 u_ui_color;
        uniform float u_fog_enabled;
        uniform vec3 u_view_origin;
        uniform vec3 u_fog_color;
        uniform vec2 u_fog_params;
        uniform highp sampler2DShadow u_shadow_map;
        uniform highp sampler2DShadow u_shadow_far_map;
        uniform sampler2D u_primary_lightmap;
        uniform vec2 u_primary_light_falloff_placement;
        uniform sampler2D u_water_map;
        uniform samplerCube u_reflection_probe;
        uniform mat4 u_shadow_matrix;
        uniform mat4 u_shadow_far_matrix;
        uniform float u_sun_shadow_enabled;
        uniform vec3 u_sun_direction;
        uniform vec3 u_sun_color;
        uniform float u_primary_light_enabled;
        uniform vec4 u_primary_light_position_radius;
        uniform vec3 u_primary_light_diffuse;
        uniform vec3 u_primary_light_spot_direction;
        uniform vec3 u_primary_light_spot_factors;
        uniform highp sampler2DShadow u_spot_shadow_map;
        uniform mat4 u_spot_shadow_matrix;
        uniform float u_spot_shadow_enabled;
        uniform vec4 u_env_map_parms;
        uniform vec4 u_water_color;
        out vec4 out_color;

        float water_height(vec2 uv)
        {
            return texture(u_water_map, uv).r +
                texture(u_water_map, uv * 3.7).r * 0.6 +
                texture(u_water_map, uv * 13.69).r * 0.36;
        }

        float sample_near_sun_shadow(vec2 uv, float receiver_depth)
        {
            // lm_sm_sun_* issues four taps. Each tap goes through D3D9's
            // linear hardware shadow sampler and therefore already owns a
            // 2x2 PCF footprint. The previous implementation reconstructed
            // only one of those taps, leaving a one-texel staircase.
            const vec2 texel = vec2(1.0 / 1024.0);
            float visibility = 0.0;
            visibility += texture(u_shadow_map, vec3(
                uv + texel * vec2(-0.5, -0.5), receiver_depth));
            visibility += texture(u_shadow_map, vec3(
                uv + texel * vec2( 0.5, -0.5), receiver_depth));
            visibility += texture(u_shadow_map, vec3(
                uv + texel * vec2(-0.5,  0.5), receiver_depth));
            visibility += texture(u_shadow_map, vec3(
                uv + texel * vec2( 0.5,  0.5), receiver_depth));
            return visibility * 0.25;
        }

        float sample_far_sun_shadow(vec2 uv, float receiver_depth)
        {
            const vec2 texel = vec2(1.0 / 1024.0);
            float visibility = 0.0;
            visibility += texture(u_shadow_far_map, vec3(
                uv + texel * vec2(-0.5, -0.5), receiver_depth));
            visibility += texture(u_shadow_far_map, vec3(
                uv + texel * vec2( 0.5, -0.5), receiver_depth));
            visibility += texture(u_shadow_far_map, vec3(
                uv + texel * vec2(-0.5,  0.5), receiver_depth));
            visibility += texture(u_shadow_far_map, vec3(
                uv + texel * vec2( 0.5,  0.5), receiver_depth));
            return visibility * 0.25;
        }

        float sample_sun_shadow(
            vec3 world_position, float authored_visibility)
        {
            // lm_sm_sun_* first samples the primary lightmap. A zero texel
            // bypasses all dynamic comparisons, and uncovered partition
            // borders fall back to that authored visibility instead of
            // becoming fully lit.
            if (authored_visibility <= 0.0) return 0.0;
            vec3 near_projected = (u_shadow_matrix *
                vec4(world_position, 1.0)).xyz;
            vec2 near_uv = near_projected.xy * 0.5 + 0.5;
            float near_depth = near_projected.z * 0.5 + 0.5;
            const float border = 1.0 / 1024.0;
            if (near_uv.x > border && near_uv.x < 1.0 - border &&
                near_uv.y > border && near_uv.y < 1.0 - border &&
                near_depth > 0.0 && near_depth < 1.0)
            {
                return sample_near_sun_shadow(near_uv, near_depth);
            }
            vec3 far_projected = (u_shadow_far_matrix *
                vec4(world_position, 1.0)).xyz;
            vec2 far_uv = far_projected.xy * 0.5 + 0.5;
            float far_depth = far_projected.z * 0.5 + 0.5;
            if (far_uv.x <= border || far_uv.x >= 1.0 - border ||
                far_uv.y <= border || far_uv.y >= 1.0 - border ||
                far_depth <= 0.0 || far_depth >= 1.0)
                return authored_visibility;
            return sample_far_sun_shadow(far_uv, far_depth);
        }

        float sample_spot_shadow(vec3 world_position)
        {
            if (u_spot_shadow_enabled < 0.5) return 1.0;
            vec4 clip = u_spot_shadow_matrix * vec4(world_position, 1.0);
            if (clip.w <= 0.0) return 1.0;
            vec3 projected = clip.xyz / clip.w;
            vec2 uv = projected.xy * 0.5 + 0.5;
            float receiver_depth = projected.z * 0.5 + 0.5;
            const float border = 1.0 / 512.0;
            if (uv.x <= border || uv.x >= 1.0 - border ||
                uv.y <= border || uv.y >= 1.0 - border ||
                receiver_depth <= 0.0 || receiver_depth >= 1.0)
                return 1.0;
            const vec2 texel = vec2(1.0 / 512.0);
            float visibility = 0.0;
            // The native shadow sampler is linear for spot maps too. Each of
            // the four lm_spot_sm taps must interpolate binary comparisons;
            // comparing one nearest depth per tap creates the jagged indoor
            // light wedge seen in Killhouse.
            visibility += texture(u_spot_shadow_map, vec3(
                uv + texel * vec2(-0.5, -0.5), receiver_depth));
            visibility += texture(u_spot_shadow_map, vec3(
                uv + texel * vec2( 0.5, -0.5), receiver_depth));
            visibility += texture(u_spot_shadow_map, vec3(
                uv + texel * vec2(-0.5,  0.5), receiver_depth));
            visibility += texture(u_spot_shadow_map, vec3(
                uv + texel * vec2( 0.5,  0.5), receiver_depth));
            return visibility * 0.25;
        }

        void main()
        {
            vec4 texel = texture(u_texture, v_texcoord);
            if (u_detail_map_enabled > 0.5)
            {
                // Exact d0 arithmetic: detail and color are added, biased by
                // -0.5, then multiplied by vertex and lighting downstream.
                texel.rgb += texture(
                    u_detail_map, v_texcoord * u_detail_scale.xy).rgb - 0.5;
            }
            float source_alpha = u_color_intensity_alpha > 0.5
                ? max(texel.r, max(texel.g, texel.b))
                : texel.a;
            vec4 bootstrap_color = v_color;
            vec3 environment_reflection = vec3(0.0);
            if (u_scene_fallback > 0.5)
            {
                // The canonical world command currently has no compiled
                // material/lightmap passes. Use a deliberately simple,
                // two-sided face-normal fallback so adjacent world surfaces
                // remain readable without creating a second world model.
                vec3 face_normal = normalize(cross(
                    dFdx(v_world_position),
                    dFdy(v_world_position)));
                vec3 orientation = abs(face_normal);
                float light = 0.45 + 0.55 * abs(dot(
                    face_normal,
                    normalize(vec3(0.35, 0.45, 0.82))));
                vec3 fallback_color =
                    (vec3(0.28, 0.34, 0.39) + orientation * 0.20) * light;
                bootstrap_color = vec4(fallback_color, 1.0);
            }
            else if (u_material_mode == 3)
            {
                // Exact arithmetic recovered from IW3 water_l_sun. The
                // animated L8 field is generated by the shared native FFT
                // implementation; this branch owns only the WebGL boundary.
                vec3 view_direction = normalize(
                    v_world_position - u_view_origin);
                float parallax_height = texture(
                    u_water_map, v_texcoord * 0.5).r;
                vec2 water_uv = v_texcoord +
                    (0.5 - parallax_height) * view_direction.xy *
                    (3.0 / 128.0);
                float center = water_height(water_uv);
                vec2 slope = vec2(
                    water_height(water_uv + vec2(1.0 / 256.0, 0.0)) -
                        center,
                    water_height(water_uv + vec2(0.0, 1.0 / 256.0)) -
                        center);
                vec3 water_normal = normalize(vec3(slope, 1.0));
                float view_normal = dot(view_direction, water_normal);
                vec3 reflection_direction = view_direction -
                    water_normal * (2.0 * view_normal);
                vec4 reflection_sample = texture(u_reflection_probe,
                    vec3(reflection_direction.xy,
                        abs(reflection_direction.z)));
                vec3 reflection_color = clamp(
                    reflection_sample.a * reflection_sample.rgb * 4.0,
                    0.0, 1.0);
                vec3 water_base = u_water_color.rgb * water_normal.z;
                float fresnel_power = pow(
                    1.0 - abs(view_normal), u_env_map_parms.z);
                float fresnel = clamp(mix(
                    u_env_map_parms.x, u_env_map_parms.y,
                    fresnel_power), 0.0, 1.0);
                vec3 water_lighting = mix(
                    water_base, reflection_color, fresnel);
                float sun_reflection = max(dot(
                    reflection_direction,
                    normalize(u_sun_direction)) + 0.00075, 0.0);
                float sun_specular = pow(
                    fresnel * sun_reflection, 64.0);
                water_lighting += u_sun_color * sun_specular *
                    u_env_map_parms.w;
                bootstrap_color = vec4(water_lighting, v_color.a);
            }
            else if (u_texture_enabled > 0.5)
            {
                if ((u_alpha_test == 1 && source_alpha <= 0.0) ||
                    (u_alpha_test == 2 && source_alpha >= (128.0 / 255.0)) ||
                    (u_alpha_test == 3 && source_alpha < (128.0 / 255.0)))
                    discard;
                if (u_material_mode == 1)
                {
                    // Native mul.hlsl emits a white-to-texture control color;
                    // fixed-function ZERO/SRC_COLOR blending then multiplies
                    // the framebuffer by it. Vertex alpha is the control.
                    bootstrap_color = vec4(mix(vec3(1.0),
                        texel.rgb * v_color.rgb, v_color.a), 1.0);
                }
                else
                {
                    bootstrap_color =
                        vec4(texel.rgb, source_alpha) * v_color;
                }
                if (u_material_mode != 1 && u_lightmap_enabled > 0.5 &&
                    u_secondary_lightmap_enabled > 0.5)
                {
                    // Exact pre-fog math from native lm_r0c0_sm2. The
                    // secondary RGBA atlas stores two vertically stacked
                    // lobes; their alpha channels encode the directional
                    // weighting. D3D9 samples these normalized values
                    // directly, without an sRGB sampler conversion.
                    vec4 secondary_lobe0 = texture(
                        u_secondary_lightmap,
                        vec2(v_lightmap_coord.x,
                            v_lightmap_coord.y * 0.5));
                    vec4 secondary_lobe1 = texture(
                        u_secondary_lightmap,
                        vec2(v_lightmap_coord.x,
                            v_lightmap_coord.y * 0.5 + 0.5));
                    vec2 encoded_direction = vec2(
                        secondary_lobe0.a * 4.08 - 2.08,
                        secondary_lobe1.a * 4.06451607 - 2.06451607);
                    float inverse_light_length = inversesqrt(
                        dot(encoded_direction, encoded_direction) + 1.0);
                    vec3 lighting;
                    if (u_normal_map_enabled > 0.5)
                    {
                        // Native lm_[rt]0c0n0_sm2 decodes DXT5nm AG as
                        // slope-space coordinates. This is deliberately not
                        // the tangent-basis reconstruction used by XModels.
                        vec4 normal_texel = texture(
                            u_normal_map, v_texcoord);
                        vec2 encoded_normal = vec2(
                            normal_texel.a * 4.08 - 2.08,
                            normal_texel.g * 4.06451607 - 2.06451607);
                        float inverse_normal_length = inversesqrt(
                            dot(encoded_normal, encoded_normal) + 1.0);
                        float directional_weight = clamp(
                            (dot(encoded_direction, encoded_normal) + 1.0) *
                                inverse_light_length * inverse_normal_length,
                            0.0, 1.0);
                        lighting = secondary_lobe0.rgb *
                                inverse_normal_length +
                            secondary_lobe1.rgb * directional_weight;
                    }
                    else
                    {
                        float directional_weight = clamp(
                            inverse_light_length, 0.0, 1.0);
                        lighting = secondary_lobe0.rgb +
                            secondary_lobe1.rgb * directional_weight;
                    }
                    if (u_primary_light_enabled > 0.5)
                    {
                        // Native lm_spot_[rt]0c0[n0]_sm2 selects this path
                        // from the draw surf's primary-light index. Its D3D9
                        // token stream multiplies the authored L8 falloff by
                        // the surface's primary lightmap visibility, the spot
                        // cone, and saturated N dot L before adding the result
                        // to the decoded two-lobe baked lighting above.
                        vec3 delta_to_light =
                            u_primary_light_position_radius.xyz -
                            v_world_position;
                        float distance_to_light = length(delta_to_light);
                        vec3 light_direction = delta_to_light /
                            max(distance_to_light, 0.000001);
                        float spot_coordinate = dot(light_direction,
                                u_primary_light_spot_direction) *
                                u_primary_light_spot_factors.x +
                            u_primary_light_spot_factors.y;
                        float spot_attenuation = pow(clamp(
                            spot_coordinate, 0.0, 1.0),
                            u_primary_light_spot_factors.z);
                        // Native lm_spot samples the attenuation curve copied
                        // into the secondary lightmap atlas. c10 is
                        // (width/512, 0, lmapLookupStart/512, 0); it does not
                        // sample the standalone GfxLightDef image here.
                        float radial_coordinate = clamp(distance_to_light *
                            u_primary_light_position_radius.w, 0.0, 1.0) *
                            u_primary_light_falloff_placement.x +
                            u_primary_light_falloff_placement.y;
                        vec3 radial_attenuation = texture(
                            u_secondary_lightmap,
                            vec2(radial_coordinate, 0.0)).rgb;
                        // The native vertex program forwards the authored
                        // primary-lightmap coordinates unchanged.
                        float primary_visibility = texture(
                            u_primary_lightmap, v_lightmap_coord).r;
                        // The retail lm_spot_sm program selects the dynamic
                        // comparison when a shadow map is active; without one
                        // it keeps the authored primary-lightmap visibility.
                        float local_visibility = mix(
                            primary_visibility,
                            sample_spot_shadow(v_world_position),
                            clamp(u_spot_shadow_enabled, 0.0, 1.0));
                        vec3 primary_normal = normalize(v_model_normal);
                        if (u_normal_map_enabled > 0.5)
                        {
                            vec4 normal_texel = texture(
                                u_normal_map, v_texcoord);
                            vec2 encoded_normal = vec2(
                                normal_texel.a * 4.08 - 2.08,
                                normal_texel.g * 4.06451607 - 2.06451607);
                            vec3 tangent = normalize(v_model_tangent);
                            vec3 binormal = normalize(cross(
                                primary_normal, tangent)) * v_binormal_sign;
                            primary_normal = normalize(primary_normal +
                                tangent * encoded_normal.x +
                                binormal * encoded_normal.y);
                        }
                        float diffuse = max(dot(
                            light_direction, primary_normal), 0.0);
                        lighting += u_primary_light_diffuse * diffuse *
                            spot_attenuation * radial_attenuation *
                            local_visibility;
                    }
                    if (u_sun_shadow_enabled > 0.5)
                    {
                        vec3 sun_normal = normalize(v_model_normal);
                        if (u_normal_map_enabled > 0.5)
                        {
                            vec4 normal_texel = texture(
                                u_normal_map, v_texcoord);
                            vec2 tangent_xy = normal_texel.ag * 2.0 - 1.0;
                            float tangent_z = sqrt(max(
                                1.0 - dot(tangent_xy, tangent_xy), 0.0));
                            vec3 tangent = normalize(v_model_tangent);
                            vec3 binormal = normalize(cross(
                                sun_normal, tangent)) * v_binormal_sign;
                            sun_normal = normalize(
                                tangent * tangent_xy.x +
                                binormal * tangent_xy.y +
                                sun_normal * tangent_z);
                        }
                        float sun_amount = max(dot(
                            normalize(u_sun_direction), sun_normal), 0.0);
                        float authored_sun_visibility = texture(
                            u_primary_lightmap, v_lightmap_coord).r;
                        lighting += u_sun_color * sun_amount *
                            sample_sun_shadow(v_world_position,
                                authored_sun_visibility);
                    }
                    bootstrap_color.rgb *= lighting;
                }
                if (u_material_mode != 1 &&
                    u_specular_map_enabled > 0.5)
                {
                    // Exact lm_*s0_sm3/lp_*s0_sm3 environment term recovered
                    // from the retail D3D9 bytecode. World materials use the
                    // slope-space normal convention; XModels use DXT5nm AG
                    // tangent-space reconstruction.
                    vec3 world_normal = normalize(v_model_normal);
                    if (u_normal_map_enabled > 0.5)
                    {
                        vec4 normal_texel = texture(
                            u_normal_map, v_texcoord);
                        vec3 tangent = normalize(v_model_tangent);
                        vec3 binormal = normalize(cross(
                            world_normal, tangent)) * v_binormal_sign;
                        if (u_model_lighting_enabled > 0.5)
                        {
                            vec2 tangent_xy = normal_texel.ag * 2.0 - 1.0;
                            float tangent_z = sqrt(max(
                                1.0 - dot(tangent_xy, tangent_xy), 0.0));
                            world_normal = normalize(
                                tangent * tangent_xy.x +
                                binormal * tangent_xy.y +
                                world_normal * tangent_z);
                        }
                        else
                        {
                            vec2 normal_slope = vec2(
                                normal_texel.a * 4.08 - 2.08,
                                normal_texel.g * 4.06451607 - 2.06451607);
                            float inverse_normal_length = inversesqrt(
                                dot(normal_slope, normal_slope) + 1.0);
                            world_normal = normalize(
                                tangent * normal_slope.x *
                                    inverse_normal_length +
                                binormal * normal_slope.y *
                                    inverse_normal_length +
                                world_normal * inverse_normal_length);
                        }
                    }
                    vec3 view_direction = normalize(
                        v_world_position - u_view_origin);
                    float view_normal = dot(view_direction, world_normal);
                    vec3 reflection_direction = view_direction -
                        world_normal * (2.0 * view_normal);
                    vec4 specular_sample = texture(
                        u_specular_map, v_texcoord);
                    float reflection_lod =
                        specular_sample.a * -8.0 + 6.0;
                    vec4 reflection_sample = textureLod(
                        u_reflection_probe, reflection_direction,
                        reflection_lod);
                    float fresnel_power = pow(
                        1.0 - abs(view_normal), u_env_map_parms.z);
                    float reflection_factor = mix(
                        u_env_map_parms.x, u_env_map_parms.y,
                        fresnel_power);
                    environment_reflection = reflection_sample.rgb *
                        reflection_sample.a * specular_sample.rgb *
                        reflection_factor;
                }
                if (u_material_mode != 1 &&
                    u_model_lighting_enabled > 0.5)
                {
                    // Native lp_t0c0[_n0]_sm2 cube-projects the world normal
                    // into the entry's 4x4x4 model-lighting block. D3D9 then
                    // multiplies base*vertex*lighting by two before fog.
                    vec3 model_normal = normalize(v_model_normal);
                    if (u_normal_map_enabled > 0.5)
                    {
                        // IW3 PC normal maps use the DXT5nm convention:
                        // tangent X in alpha, tangent Y in green, and a
                        // reconstructed positive Z. The packed XSurface sign
                        // reconstructs the same binormal handedness as native.
                        vec4 normal_texel = texture(
                            u_normal_map, v_texcoord);
                        vec2 tangent_xy = normal_texel.ag * 2.0 - 1.0;
                        float tangent_z = sqrt(max(
                            1.0 - dot(tangent_xy, tangent_xy), 0.0));
                        vec3 tangent = normalize(v_model_tangent);
                        vec3 binormal = normalize(cross(
                            model_normal, tangent)) * v_binormal_sign;
                        model_normal = normalize(
                            tangent * tangent_xy.x +
                            binormal * tangent_xy.y +
                            model_normal * tangent_z);
                    }
                    float major_axis = max(max(abs(model_normal.x),
                        abs(model_normal.y)), abs(model_normal.z));
                    vec3 lookup_direction = model_normal /
                        max(major_axis, 0.000001);
                    vec4 model_lighting = texture(
                        u_model_lighting,
                        v_model_lighting_coords + lookup_direction *
                            u_model_lighting_lookup_scale);
                    vec3 model_lighting_factor = model_lighting.rgb * 2.0;
                    if (u_primary_light_enabled > 0.5 ||
                        u_material_mode == 6)
                    {
                        // The model-lighting volume stores canonical primary
                        // visibility in alpha. Native lp_amb_* adds sunDiffuse
                        // after doubling ambient model lighting and without a
                        // normal term; the remaining lp_* programs retain
                        // their directional N dot L contribution inside x2.
                        float primary_diffuse = u_material_mode == 6
                            ? 1.0
                            : max(dot(normalize(u_sun_direction),
                                model_normal), 0.0);
                        vec3 primary_lighting = u_sun_color *
                            model_lighting.a * primary_diffuse;
                        model_lighting_factor += u_material_mode == 6
                            ? primary_lighting
                            : primary_lighting * 2.0;
                    }
                    bootstrap_color.rgb *= model_lighting_factor;
                }
                // Retail lp_* bytecode adds the environment term after
                // base*vertex*modelLighting*2. World passes have no model
                // lighting, so this preserves their established ordering.
                bootstrap_color.rgb += environment_reflection;
            }
            vec4 final_color = bootstrap_color * u_ui_color;
            if (u_fog_enabled > 0.5 && u_material_mode != 1)
            {
                // R_SetFrameFog supplies (start, density). Campaign scripts
                // define density as ln(2)/halfwayDistance, so exp(-density *
                // distancePastStart) reaches 50% at the requested distance.
                float distance_from_view = length(
                    v_world_position - u_view_origin);
                float visibility = clamp(exp(
                    (u_fog_params.x - distance_from_view) *
                        u_fog_params.y), 0.0, 1.0);
                final_color.rgb = mix(
                    u_fog_color, final_color.rgb, visibility);
            }
            if (u_premultiply_alpha > 0.5)
                final_color.rgb *= final_color.a;
            out_color = final_color;
        }
    )glsl";

    constexpr const char *shadowVertexSource = R"glsl(#version 300 es
        precision highp float;
        layout(location = 0) in vec3 a_position;
        layout(location = 2) in vec2 a_texcoord;
        layout(location = 4) in vec3 a_instance_axis0;
        layout(location = 5) in vec3 a_instance_axis1;
        layout(location = 6) in vec3 a_instance_axis2;
        layout(location = 7) in vec3 a_instance_origin;
        uniform mat4 u_shadow_depth_matrix;
        uniform float u_shadow_depth_instance_enabled;
        out vec2 v_shadow_texcoord;
        void main()
        {
            vec3 position = a_position;
            if (u_shadow_depth_instance_enabled > 0.5)
            {
                position = a_instance_origin +
                    a_position.x * a_instance_axis0 +
                    a_position.y * a_instance_axis1 +
                    a_position.z * a_instance_axis2;
            }
            gl_Position = u_shadow_depth_matrix * vec4(position, 1.0);
            v_shadow_texcoord = a_texcoord;
        }
    )glsl";
    constexpr const char *shadowFragmentSource = R"glsl(#version 300 es
        precision highp float;
        in vec2 v_shadow_texcoord;
        uniform sampler2D u_shadow_depth_texture;
        uniform float u_shadow_depth_texture_enabled;
        uniform int u_shadow_depth_alpha_test;
        void main()
        {
            if (u_shadow_depth_texture_enabled > 0.5 &&
                u_shadow_depth_alpha_test != 0)
            {
                float alpha = texture(
                    u_shadow_depth_texture, v_shadow_texcoord).a;
                if ((u_shadow_depth_alpha_test == 1 && alpha <= 0.0) ||
                    (u_shadow_depth_alpha_test == 2 &&
                        alpha >= (128.0 / 255.0)) ||
                    (u_shadow_depth_alpha_test == 3 &&
                        alpha < (128.0 / 255.0)))
                    discard;
            }
        }
    )glsl";

    constexpr const char *skyVertexSource = R"glsl(#version 300 es
        precision highp float;
        out vec2 v_ndc;
        void main()
        {
            const vec2 positions[3] = vec2[3](
                vec2(-1.0, -1.0),
                vec2(3.0, -1.0),
                vec2(-1.0, 3.0));
            v_ndc = positions[gl_VertexID];
            gl_Position = vec4(v_ndc, 1.0, 1.0);
        }
    )glsl";
    constexpr const char *skyFragmentSource = R"glsl(#version 300 es
        precision highp float;
        precision highp samplerCube;
        in vec2 v_ndc;
        uniform samplerCube u_sky;
        uniform vec2 u_tan_half_fov;
        uniform vec3 u_forward;
        uniform vec3 u_right;
        uniform vec3 u_up;
        out vec4 out_color;
        void main()
        {
            // MatrixForViewer maps world right to negative view X and world
            // up to positive view Y. Reconstruct the same canonical camera
            // ray without translation so the sky remains infinitely distant.
            vec3 direction = normalize(u_forward -
                v_ndc.x * u_tan_half_fov.x * u_right +
                v_ndc.y * u_tan_half_fov.y * u_up);
            out_color = texture(u_sky, direction);
        }
    )glsl";

    constexpr const char *postProcessFragmentSource = R"glsl(#version 300 es
        precision highp float;
        in vec2 v_ndc;
        uniform sampler2D u_source;
        uniform float u_film_enabled;
        uniform vec4 u_color_bias;
        uniform vec4 u_color_tint_base;
        uniform vec4 u_color_tint_delta;
        uniform float u_gamma_exponent;
        uniform vec2 u_blur_scale;
        uniform float u_blind_darken;
        uniform float u_glare_lighten;
        uniform sampler2D u_glow;
        uniform float u_glow_intensity;
        uniform sampler2D u_depth;
        uniform float u_dof_enabled;
        uniform vec4 u_dof_view_model;
        uniform vec4 u_dof_near;
        uniform vec4 u_dof_far;
        uniform vec4 u_dof_depth;
        out vec4 out_color;
        void main()
        {
            vec2 uv = v_ndc * 0.5 + 0.5;
            vec4 source = texture(u_source, uv);
            vec2 blur_scale = u_blur_scale;
            if (u_dof_enabled > 0.5)
            {
                const float infinite_scale = 2047.0 / 2048.0;
                const float viewmodel_depth_range = 1.0 / 64.0;
                float stored_depth = texture(u_depth, uv).r;
                bool viewmodel =
                    u_dof_view_model.y > u_dof_view_model.x + 1.0 &&
                    stored_depth < viewmodel_depth_range;
                float normalized_depth = viewmodel
                    ? stored_depth / viewmodel_depth_range : stored_depth;
                float near_clip = viewmodel
                    ? u_dof_depth.y : u_dof_depth.x;
                float view_distance = near_clip * infinite_scale /
                    max(infinite_scale - normalized_depth, 0.000001);
                float coc_radius = 0.0;
                if (viewmodel)
                {
                    coc_radius = clamp(
                        (u_dof_view_model.y - view_distance) /
                        (u_dof_view_model.y - u_dof_view_model.x),
                        0.0, 1.0) * u_dof_view_model.z;
                }
                else
                {
                    if (u_dof_near.y > u_dof_near.x + 1.0)
                    {
                        coc_radius = clamp(
                            (u_dof_near.y - view_distance) /
                            (u_dof_near.y - u_dof_near.x),
                            0.0, 1.0) * u_dof_near.z;
                    }
                    if (u_dof_far.y > u_dof_far.x + 1.0 &&
                        u_dof_far.z > 0.0)
                    {
                        coc_radius = max(coc_radius, clamp(
                            (view_distance - u_dof_far.x) /
                            (u_dof_far.y - u_dof_far.x),
                            0.0, 1.0) * u_dof_far.z);
                    }
                }
                blur_scale = max(blur_scale,
                    coc_radius * u_dof_depth.zw);
            }
            if (max(blur_scale.x, blur_scale.y) > 0.0)
            {
                // Native RB_BlurScreen applies a Gaussian filter to the
                // resolved 3D scene before 2D. A bounded disk kernel keeps
                // that ownership/order in one WebGL2 pass while scaling the
                // authored 640x480 radius to the actual scene target.
                source *= 0.38;
                source += texture(u_source,
                    uv + vec2( blur_scale.x * 0.5, 0.0)) * 0.09;
                source += texture(u_source,
                    uv + vec2(-blur_scale.x * 0.5, 0.0)) * 0.09;
                source += texture(u_source,
                    uv + vec2(0.0,  blur_scale.y * 0.5)) * 0.09;
                source += texture(u_source,
                    uv + vec2(0.0, -blur_scale.y * 0.5)) * 0.09;
                source += texture(u_source,
                    uv + vec2( blur_scale.x, 0.0)) * 0.04;
                source += texture(u_source,
                    uv + vec2(-blur_scale.x, 0.0)) * 0.04;
                source += texture(u_source,
                    uv + vec2(0.0,  blur_scale.y)) * 0.04;
                source += texture(u_source,
                    uv + vec2(0.0, -blur_scale.y)) * 0.04;
                source += texture(u_source,
                    uv + vec2( blur_scale.x,  blur_scale.y) * 0.7) * 0.025;
                source += texture(u_source,
                    uv + vec2(-blur_scale.x,  blur_scale.y) * 0.7) * 0.025;
                source += texture(u_source,
                    uv + vec2( blur_scale.x, -blur_scale.y) * 0.7) * 0.025;
                source += texture(u_source,
                    uv + vec2(-blur_scale.x, -blur_scale.y) * 0.7) * 0.025;
            }
            vec3 color = source.rgb;
            if (u_film_enabled > 0.5)
            {
                // Native postfx_color uses the IW3 intensity coefficients
                // and the exact constants produced by
                // R_UpdateColorManipulation. Their reciprocal encoding keeps
                // the no-desaturation case representable in ps_2_0.
                float intensity = dot(color,
                    vec3(0.2989, 0.5870, 0.1140));
                vec3 desaturated = color * u_color_bias.w +
                    vec3(intensity);
                vec3 tint = u_color_tint_base.rgb +
                    u_color_tint_delta.rgb * intensity;
                color = desaturated * tint + u_color_bias.rgb;
            }
            color += texture(u_glow, uv).rgb * u_glow_intensity;
            color = pow(clamp(color, 0.0, 1.0),
                vec3(u_gamma_exponent));
            color = color * (1.0 - clamp(u_blind_darken, 0.0, 1.0)) +
                vec3(clamp(u_glare_lighten, 0.0, 1.0));
            out_color = vec4(color, source.a);
        }
    )glsl";

    constexpr const char *glowFragmentSource = R"glsl(#version 300 es
        precision highp float;
        in vec2 v_ndc;
        uniform sampler2D u_source;
        uniform int u_mode;
        uniform vec2 u_texel_delta;
        uniform float u_weights[9];
        uniform vec4 u_setup;
        out vec4 out_color;
        void main()
        {
            vec2 uv = v_ndc * 0.5 + 0.5;
            if (u_mode == 0)
            {
                vec3 source = (
                    texture(u_source, uv + vec2(
                        u_texel_delta.x, u_texel_delta.y)).rgb +
                    texture(u_source, uv + vec2(
                       -u_texel_delta.x, u_texel_delta.y)).rgb +
                    texture(u_source, uv + vec2(
                        u_texel_delta.x, -u_texel_delta.y)).rgb +
                    texture(u_source, uv - u_texel_delta).rgb) * 0.25;
                vec3 bloom = clamp(
                    (source - vec3(u_setup.x)) * u_setup.y,
                    0.0, 1.0);
                float intensity = dot(bloom,
                    vec3(0.2989, 0.5870, 0.1140));
                bloom = mix(bloom, vec3(intensity),
                    clamp(u_setup.w, 0.0, 1.0));
                out_color = vec4(bloom, 1.0);
                return;
            }
            vec4 filtered = texture(u_source, uv) * u_weights[0];
            for (int tap = 1; tap < 9; ++tap)
            {
                vec2 offset = u_texel_delta * float(tap);
                filtered += (texture(u_source, uv + offset) +
                    texture(u_source, uv - offset)) * u_weights[tap];
            }
            out_color = filtered;
        }
    )glsl";

    GLuint program = 0;
    if (!LinkProgram("bootstrap", vertexSource, fragmentSource, program)) return false;
    GLuint skyProgram = 0u;
    if (!LinkProgram("canonical sky", skyVertexSource,
            skyFragmentSource, skyProgram))
    {
        glDeleteProgram(program);
        return false;
    }
    GLuint shadowProgram = 0u;
    if (!LinkProgram("canonical sun shadow", shadowVertexSource,
            shadowFragmentSource, shadowProgram))
    {
        glDeleteProgram(program);
        glDeleteProgram(skyProgram);
        return false;
    }
    GLuint postProcessProgram = 0u;
    if (!LinkProgram("canonical film/gamma", skyVertexSource,
            postProcessFragmentSource, postProcessProgram))
    {
        glDeleteProgram(program);
        glDeleteProgram(skyProgram);
        glDeleteProgram(shadowProgram);
        return false;
    }
    GLuint glowProgram = 0u;
    if (!LinkProgram("canonical glow", skyVertexSource,
            glowFragmentSource, glowProgram))
    {
        glDeleteProgram(program);
        glDeleteProgram(skyProgram);
        glDeleteProgram(shadowProgram);
        glDeleteProgram(postProcessProgram);
        return false;
    }

    while (glGetError() != GL_NO_ERROR)
    {
    }

    const GLint aspectUniform = glGetUniformLocation(program, "u_aspect");
    const GLint textureUniform = glGetUniformLocation(program, "u_texture");
    const GLint textureEnabledUniform =
        glGetUniformLocation(program, "u_texture_enabled");
    const GLint detailMapUniform =
        glGetUniformLocation(program, "u_detail_map");
    const GLint detailMapEnabledUniform =
        glGetUniformLocation(program, "u_detail_map_enabled");
    const GLint detailScaleUniform =
        glGetUniformLocation(program, "u_detail_scale");
    const GLint normalMapUniform =
        glGetUniformLocation(program, "u_normal_map");
    const GLint normalMapEnabledUniform =
        glGetUniformLocation(program, "u_normal_map_enabled");
    const GLint specularMapUniform =
        glGetUniformLocation(program, "u_specular_map");
    const GLint specularMapEnabledUniform =
        glGetUniformLocation(program, "u_specular_map_enabled");
    const GLint viewProjectionUniform =
        glGetUniformLocation(program, "u_view_projection");
    const GLint sceneFallbackUniform =
        glGetUniformLocation(program, "u_scene_fallback");
    const GLint lightmapEnabledUniform =
        glGetUniformLocation(program, "u_lightmap_enabled");
    const GLint secondaryLightmapUniform =
        glGetUniformLocation(program, "u_secondary_lightmap");
    const GLint secondaryLightmapEnabledUniform =
        glGetUniformLocation(program, "u_secondary_lightmap_enabled");
    const GLint modelLightingUniform =
        glGetUniformLocation(program, "u_model_lighting");
    const GLint modelLightingEnabledUniform =
        glGetUniformLocation(program, "u_model_lighting_enabled");
    const GLint modelLightingBaseCoordinatesUniform =
        glGetUniformLocation(program, "u_model_lighting_base_coords");
    const GLint modelLightingLookupScaleUniform =
        glGetUniformLocation(program, "u_model_lighting_lookup_scale");
    const GLint premultiplyAlphaUniform =
        glGetUniformLocation(program, "u_premultiply_alpha");
    const GLint colorIntensityAlphaUniform =
        glGetUniformLocation(program, "u_color_intensity_alpha");
    const GLint materialModeUniform =
        glGetUniformLocation(program, "u_material_mode");
    const GLint falloffParmsUniform =
        glGetUniformLocation(program, "u_falloff_parms");
    const GLint falloffBeginColorUniform =
        glGetUniformLocation(program, "u_falloff_begin_color");
    const GLint falloffEndColorUniform =
        glGetUniformLocation(program, "u_falloff_end_color");
    const GLint alphaTestUniform = glGetUniformLocation(program, "u_alpha_test");
    const GLint instanceEnabledUniform =
        glGetUniformLocation(program, "u_instance_enabled");
    const GLint uiColorUniform = glGetUniformLocation(program, "u_ui_color");
    const GLint fogEnabledUniform =
        glGetUniformLocation(program, "u_fog_enabled");
    const GLint viewOriginUniform =
        glGetUniformLocation(program, "u_view_origin");
    const GLint fogColorUniform =
        glGetUniformLocation(program, "u_fog_color");
    const GLint fogParamsUniform =
        glGetUniformLocation(program, "u_fog_params");
    const GLint shadowMapUniform =
        glGetUniformLocation(program, "u_shadow_map");
    const GLint shadowFarMapUniform =
        glGetUniformLocation(program, "u_shadow_far_map");
    const GLint shadowMatrixUniform =
        glGetUniformLocation(program, "u_shadow_matrix");
    const GLint shadowFarMatrixUniform =
        glGetUniformLocation(program, "u_shadow_far_matrix");
    const GLint sunShadowEnabledUniform =
        glGetUniformLocation(program, "u_sun_shadow_enabled");
    const GLint sunDirectionUniform =
        glGetUniformLocation(program, "u_sun_direction");
    const GLint sunColorUniform =
        glGetUniformLocation(program, "u_sun_color");
    const GLint primaryLightmapUniform =
        glGetUniformLocation(program, "u_primary_lightmap");
    const GLint primaryLightFalloffPlacementUniform =
        glGetUniformLocation(program, "u_primary_light_falloff_placement");
    const GLint primaryLightEnabledUniform =
        glGetUniformLocation(program, "u_primary_light_enabled");
    const GLint primaryLightPositionRadiusUniform =
        glGetUniformLocation(program, "u_primary_light_position_radius");
    const GLint primaryLightDiffuseUniform =
        glGetUniformLocation(program, "u_primary_light_diffuse");
    const GLint primaryLightSpotDirectionUniform =
        glGetUniformLocation(program, "u_primary_light_spot_direction");
    const GLint primaryLightSpotFactorsUniform =
        glGetUniformLocation(program, "u_primary_light_spot_factors");
    const GLint spotShadowMapUniform =
        glGetUniformLocation(program, "u_spot_shadow_map");
    const GLint spotShadowMatrixUniform =
        glGetUniformLocation(program, "u_spot_shadow_matrix");
    const GLint spotShadowEnabledUniform =
        glGetUniformLocation(program, "u_spot_shadow_enabled");
    const GLint waterMapUniform =
        glGetUniformLocation(program, "u_water_map");
    const GLint reflectionProbeUniform =
        glGetUniformLocation(program, "u_reflection_probe");
    const GLint envMapParmsUniform =
        glGetUniformLocation(program, "u_env_map_parms");
    const GLint waterColorUniform =
        glGetUniformLocation(program, "u_water_color");
    const GLint shadowDepthMatrixUniform =
        glGetUniformLocation(shadowProgram, "u_shadow_depth_matrix");
    const GLint shadowDepthTextureUniform =
        glGetUniformLocation(shadowProgram, "u_shadow_depth_texture");
    const GLint shadowDepthTextureEnabledUniform =
        glGetUniformLocation(shadowProgram,
            "u_shadow_depth_texture_enabled");
    const GLint shadowDepthAlphaTestUniform =
        glGetUniformLocation(shadowProgram, "u_shadow_depth_alpha_test");
    const GLint shadowDepthInstanceEnabledUniform =
        glGetUniformLocation(
            shadowProgram, "u_shadow_depth_instance_enabled");
    const GLint skyTextureUniform =
        glGetUniformLocation(skyProgram, "u_sky");
    const GLint skyTanHalfFovUniform =
        glGetUniformLocation(skyProgram, "u_tan_half_fov");
    const GLint skyForwardUniform =
        glGetUniformLocation(skyProgram, "u_forward");
    const GLint skyRightUniform =
        glGetUniformLocation(skyProgram, "u_right");
    const GLint skyUpUniform = glGetUniformLocation(skyProgram, "u_up");
    const GLint postProcessTextureUniform =
        glGetUniformLocation(postProcessProgram, "u_source");
    const GLint postProcessFilmEnabledUniform =
        glGetUniformLocation(postProcessProgram, "u_film_enabled");
    const GLint postProcessColorBiasUniform =
        glGetUniformLocation(postProcessProgram, "u_color_bias");
    const GLint postProcessColorTintBaseUniform =
        glGetUniformLocation(postProcessProgram, "u_color_tint_base");
    const GLint postProcessColorTintDeltaUniform =
        glGetUniformLocation(postProcessProgram, "u_color_tint_delta");
    const GLint postProcessGammaExponentUniform =
        glGetUniformLocation(postProcessProgram, "u_gamma_exponent");
    const GLint postProcessBlurScaleUniform =
        glGetUniformLocation(postProcessProgram, "u_blur_scale");
    const GLint postProcessBlindDarkenUniform =
        glGetUniformLocation(postProcessProgram, "u_blind_darken");
    const GLint postProcessGlareLightenUniform =
        glGetUniformLocation(postProcessProgram, "u_glare_lighten");
    const GLint postProcessGlowTextureUniform =
        glGetUniformLocation(postProcessProgram, "u_glow");
    const GLint postProcessGlowIntensityUniform =
        glGetUniformLocation(postProcessProgram, "u_glow_intensity");
    const GLint postProcessDepthTextureUniform =
        glGetUniformLocation(postProcessProgram, "u_depth");
    const GLint postProcessDofEnabledUniform =
        glGetUniformLocation(postProcessProgram, "u_dof_enabled");
    const GLint postProcessDofViewModelUniform =
        glGetUniformLocation(postProcessProgram, "u_dof_view_model");
    const GLint postProcessDofNearUniform =
        glGetUniformLocation(postProcessProgram, "u_dof_near");
    const GLint postProcessDofFarUniform =
        glGetUniformLocation(postProcessProgram, "u_dof_far");
    const GLint postProcessDofDepthUniform =
        glGetUniformLocation(postProcessProgram, "u_dof_depth");
    const GLint glowTextureUniform =
        glGetUniformLocation(glowProgram, "u_source");
    const GLint glowModeUniform =
        glGetUniformLocation(glowProgram, "u_mode");
    const GLint glowTexelDeltaUniform =
        glGetUniformLocation(glowProgram, "u_texel_delta");
    const GLint glowWeightsUniform =
        glGetUniformLocation(glowProgram, "u_weights[0]");
    const GLint glowSetupUniform =
        glGetUniformLocation(glowProgram, "u_setup");
    const GLenum pipelineError = glGetError();

    const std::uint8_t *texturePixels = FALLBACK_TEXTURE_RGBA;
    std::uint32_t textureWidth = 1u;
    std::uint32_t textureHeight = 1u;
    std::uint8_t textureSamplerState = 0u;
    if (g_renderer.sourceTextureActive)
    {
        texturePixels = g_renderer.retainedPixels.data();
        textureWidth = g_renderer.textureWidth;
        textureHeight = g_renderer.textureHeight;
        textureSamplerState = g_renderer.textureSamplerState;
    }

    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;
    GLuint indexBuffer = 0;
    const bool surfaceReady = !g_renderer.surfaceActive ||
        (g_renderer.worldSurfaceActive
            ? CreateSurfaceObjects(
                g_renderer.retainedVertices,
                g_renderer.retainedWorldIndices,
                vertexArray,
                vertexBuffer,
                indexBuffer)
            : CreateSurfaceObjects(
                g_renderer.retainedVertices,
                g_renderer.retainedIndices,
                vertexArray,
                vertexBuffer,
                indexBuffer));

    GLuint texture = 0;
    const bool textureReady = CreateTextureObject(
        texturePixels, textureWidth, textureHeight, textureSamplerState, texture);
    const bool worldTexturesReady = textureReady &&
        (!g_renderer.worldSurfaceActive ||
         CreateWorldTextureObjects(g_renderer.retainedWorldImages));
    const bool waterTexturesReady = worldTexturesReady &&
        (!g_renderer.worldSurfaceActive ||
         CreateWaterTextureObjects(g_renderer.retainedWorldBatches));
    if (waterTexturesReady)
        AttachRetainedWorldReflectionTextures();
    const bool skyTextureReady = waterTexturesReady &&
        CreateSkyTextureObject(g_renderer.retainedSky);
    GLuint staticModelVertexArray = 0u;
    GLuint staticModelVertexBuffer = 0u;
    GLuint staticModelIndexBuffer = 0u;
    GLuint staticModelInstanceBuffer = 0u;
    const bool staticModelObjectsReady = !g_renderer.staticModelSceneActive ||
        CreateStaticModelObjects(
            g_renderer.retainedStaticModelVertices,
            g_renderer.retainedStaticModelIndices,
            g_renderer.retainedStaticModelInstances,
            staticModelVertexArray,
            staticModelVertexBuffer,
            staticModelIndexBuffer,
            staticModelInstanceBuffer);
    const bool staticModelTexturesReady = staticModelObjectsReady &&
        (!g_renderer.staticModelSceneActive ||
         CreateWorldTextureObjects(g_renderer.retainedStaticModelImages));
    const bool staticModelLightingReady = staticModelTexturesReady &&
        (!g_renderer.staticModelSceneActive ||
         CreateModelLightingTexture(
            g_renderer.retainedStaticModelLighting));
    GLuint dynamicModelVertexArray = 0u;
    GLuint dynamicModelVertexBuffer = 0u;
    GLuint dynamicModelIndexBuffer = 0u;
    const bool dynamicModelObjectsReady =
        !g_renderer.dynamicModelSceneActive || CreateSurfaceObjects(
            g_renderer.retainedDynamicModelVertices,
            g_renderer.retainedDynamicModelIndices,
            dynamicModelVertexArray,
            dynamicModelVertexBuffer,
            dynamicModelIndexBuffer);
    const bool dynamicModelTexturesReady = dynamicModelObjectsReady &&
        (!g_renderer.dynamicModelSceneActive ||
         CreateWorldTextureObjects(g_renderer.retainedDynamicModelImages));
    const bool dynamicModelLightingReady = dynamicModelTexturesReady &&
        (!g_renderer.dynamicModelSceneActive ||
         CreateModelLightingTexture(
            g_renderer.retainedDynamicModelLighting));
    GLuint uiVertexArray = 0u;
    GLuint uiVertexBuffer = 0u;
    GLuint uiIndexBuffer = 0u;
    const bool uiObjectsReady = !g_renderer.uiSceneActive ||
        CreateSurfaceObjects(g_renderer.retainedUiVertices,
            g_renderer.retainedUiIndices, uiVertexArray, uiVertexBuffer,
            uiIndexBuffer);
    const bool uiTexturesReady = uiObjectsReady &&
        (!g_renderer.uiSceneActive ||
         CreateWorldTextureObjects(g_renderer.retainedUiImages));
    GLuint shadowFramebuffer = 0u;
    GLuint shadowDepthTexture = 0u;
    GLuint shadowFarFramebuffer = 0u;
    GLuint shadowFarDepthTexture = 0u;
    const bool shadowTargetReady = CreateShadowTarget(
        SUN_SHADOW_SIZE, "sun", shadowFramebuffer, shadowDepthTexture);
    const bool shadowFarTargetReady = shadowTargetReady &&
        CreateShadowTarget(SUN_SHADOW_SIZE, "sun-far",
            shadowFarFramebuffer, shadowFarDepthTexture);
    std::array<GLuint, MAX_SPOT_SHADOWS> spotShadowFramebuffers{};
    std::array<GLuint, MAX_SPOT_SHADOWS> spotShadowDepthTextures{};
    bool spotShadowTargetsReady = shadowFarTargetReady;
    for (std::size_t index = 0u;
         spotShadowTargetsReady && index < MAX_SPOT_SHADOWS; ++index)
    {
        spotShadowTargetsReady = CreateShadowTarget(
            SPOT_SHADOW_SIZE, "spot",
            spotShadowFramebuffers[index], spotShadowDepthTextures[index]);
    }
    GLuint compatibilityProgram = 0;
    GLint compatibilityViewProjection = -1;
    GLint compatibilityWorld = -1;
    GLint compatibilityTexture = -1;
    const bool compatibilityReady = !g_renderer.compatibilityActive ||
        CreateCompatibilityProgram(
            g_renderer.compatibilityVertexSource.c_str(),
            g_renderer.compatibilityFragmentSource.c_str(),
            compatibilityProgram,
            compatibilityViewProjection,
            compatibilityWorld,
            compatibilityTexture);
    if (aspectUniform < 0 || textureUniform < 0 || textureEnabledUniform < 0 ||
        detailMapUniform < 0 || detailMapEnabledUniform < 0 ||
        detailScaleUniform < 0 ||
        normalMapUniform < 0 || normalMapEnabledUniform < 0 ||
        specularMapUniform < 0 || specularMapEnabledUniform < 0 ||
        viewProjectionUniform < 0 || sceneFallbackUniform < 0 ||
        lightmapEnabledUniform < 0 ||
        secondaryLightmapUniform < 0 ||
        secondaryLightmapEnabledUniform < 0 ||
        modelLightingUniform < 0 || modelLightingEnabledUniform < 0 ||
        modelLightingBaseCoordinatesUniform < 0 ||
        modelLightingLookupScaleUniform < 0 ||
        premultiplyAlphaUniform < 0 || colorIntensityAlphaUniform < 0 ||
        materialModeUniform < 0 || falloffParmsUniform < 0 ||
        falloffBeginColorUniform < 0 || falloffEndColorUniform < 0 ||
        alphaTestUniform < 0 || instanceEnabledUniform < 0 ||
        uiColorUniform < 0 || fogEnabledUniform < 0 ||
        viewOriginUniform < 0 || fogColorUniform < 0 ||
        fogParamsUniform < 0 || shadowMapUniform < 0 ||
        shadowFarMapUniform < 0 || shadowMatrixUniform < 0 ||
        shadowFarMatrixUniform < 0 || sunShadowEnabledUniform < 0 ||
        sunDirectionUniform < 0 || sunColorUniform < 0 ||
        primaryLightmapUniform < 0 ||
        primaryLightFalloffPlacementUniform < 0 ||
        primaryLightEnabledUniform < 0 ||
        primaryLightPositionRadiusUniform < 0 ||
        primaryLightDiffuseUniform < 0 ||
        primaryLightSpotDirectionUniform < 0 ||
        primaryLightSpotFactorsUniform < 0 ||
        spotShadowMapUniform < 0 || spotShadowMatrixUniform < 0 ||
        spotShadowEnabledUniform < 0 ||
        waterMapUniform < 0 || reflectionProbeUniform < 0 ||
        envMapParmsUniform < 0 || waterColorUniform < 0 ||
        shadowDepthMatrixUniform < 0 || shadowDepthTextureUniform < 0 ||
        shadowDepthTextureEnabledUniform < 0 ||
        shadowDepthAlphaTestUniform < 0 ||
        shadowDepthInstanceEnabledUniform < 0 ||
        skyTextureUniform < 0 || skyTanHalfFovUniform < 0 ||
        skyForwardUniform < 0 || skyRightUniform < 0 ||
        skyUpUniform < 0 || postProcessTextureUniform < 0 ||
        postProcessFilmEnabledUniform < 0 ||
        postProcessColorBiasUniform < 0 ||
        postProcessColorTintBaseUniform < 0 ||
        postProcessColorTintDeltaUniform < 0 ||
        postProcessGammaExponentUniform < 0 ||
        postProcessBlurScaleUniform < 0 ||
        postProcessBlindDarkenUniform < 0 ||
        postProcessGlareLightenUniform < 0 ||
        postProcessGlowTextureUniform < 0 ||
        postProcessGlowIntensityUniform < 0 ||
        postProcessDepthTextureUniform < 0 ||
        postProcessDofEnabledUniform < 0 ||
        postProcessDofViewModelUniform < 0 ||
        postProcessDofNearUniform < 0 ||
        postProcessDofFarUniform < 0 ||
        postProcessDofDepthUniform < 0 ||
        glowTextureUniform < 0 || glowModeUniform < 0 ||
        glowTexelDeltaUniform < 0 || glowWeightsUniform < 0 ||
        glowSetupUniform < 0 ||
        pipelineError != GL_NO_ERROR ||
        !surfaceReady || !textureReady || !worldTexturesReady ||
        !waterTexturesReady ||
        !skyTextureReady ||
        !staticModelObjectsReady || !staticModelTexturesReady ||
        !staticModelLightingReady ||
        !dynamicModelObjectsReady || !dynamicModelTexturesReady ||
        !dynamicModelLightingReady ||
        !uiObjectsReady || !uiTexturesReady ||
        !shadowTargetReady || !shadowFarTargetReady ||
        !spotShadowTargetsReady ||
        !compatibilityReady)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] WebGL2 resource creation failed (0x%x).\n",
            static_cast<unsigned int>(pipelineError));
        DeletePipelineObjects(
            program,
            skyProgram,
            postProcessProgram,
            glowProgram,
            compatibilityProgram,
            vertexArray,
            vertexBuffer,
            indexBuffer,
            texture);
        DeleteShadowObjects(
            shadowFramebuffer, shadowDepthTexture, shadowProgram);
        DeleteShadowObjects(
            shadowFarFramebuffer, shadowFarDepthTexture, 0u);
        for (std::size_t index = 0u; index < MAX_SPOT_SHADOWS; ++index)
            DeleteShadowObjects(spotShadowFramebuffers[index],
                spotShadowDepthTextures[index], 0u);
        DeleteWorldTextureObjects(g_renderer.retainedWorldImages);
        DeleteWaterTextureObjects(g_renderer.retainedWorldBatches);
        DeleteWorldTextureObjects(g_renderer.retainedStaticModelImages);
        DeleteWorldTextureObjects(g_renderer.retainedDynamicModelImages);
        DeleteWorldTextureObjects(g_renderer.retainedUiImages);
        if (g_renderer.retainedSky.texture != 0u)
            glDeleteTextures(1, &g_renderer.retainedSky.texture);
        g_renderer.retainedSky.texture = 0u;
        DeleteModelLightingTexture(g_renderer.retainedStaticModelLighting);
        DeleteModelLightingTexture(g_renderer.retainedDynamicModelLighting);
        DeleteStaticModelObjects(
            staticModelVertexArray,
            staticModelVertexBuffer,
            staticModelIndexBuffer,
            staticModelInstanceBuffer);
        DeleteSurfaceObjects(dynamicModelVertexArray,
            dynamicModelVertexBuffer, dynamicModelIndexBuffer);
        DeleteSurfaceObjects(uiVertexArray, uiVertexBuffer, uiIndexBuffer);
        return false;
    }

    g_renderer.program = program;
    g_renderer.skyProgram = skyProgram;
    g_renderer.postProcessProgram = postProcessProgram;
    g_renderer.glowProgram = glowProgram;
    g_renderer.shadowProgram = shadowProgram;
    g_renderer.compatibilityProgram = compatibilityProgram;
    g_renderer.vertexArray = vertexArray;
    g_renderer.vertexBuffer = vertexBuffer;
    g_renderer.indexBuffer = indexBuffer;
    g_renderer.staticModelVertexArray = staticModelVertexArray;
    g_renderer.staticModelVertexBuffer = staticModelVertexBuffer;
    g_renderer.staticModelIndexBuffer = staticModelIndexBuffer;
    g_renderer.staticModelInstanceBuffer = staticModelInstanceBuffer;
    g_renderer.dynamicModelVertexArray = dynamicModelVertexArray;
    g_renderer.dynamicModelVertexBuffer = dynamicModelVertexBuffer;
    g_renderer.dynamicModelIndexBuffer = dynamicModelIndexBuffer;
    g_renderer.uiVertexArray = uiVertexArray;
    g_renderer.uiVertexBuffer = uiVertexBuffer;
    g_renderer.uiIndexBuffer = uiIndexBuffer;
    g_renderer.texture = texture;
    g_renderer.shadowFramebuffer = shadowFramebuffer;
    g_renderer.shadowDepthTexture = shadowDepthTexture;
    g_renderer.shadowFarFramebuffer = shadowFarFramebuffer;
    g_renderer.shadowFarDepthTexture = shadowFarDepthTexture;
    g_renderer.spotShadowFramebuffers = spotShadowFramebuffers;
    g_renderer.spotShadowDepthTextures = spotShadowDepthTextures;
    g_renderer.aspectUniform = aspectUniform;
    g_renderer.textureUniform = textureUniform;
    g_renderer.textureEnabledUniform = textureEnabledUniform;
    g_renderer.detailMapUniform = detailMapUniform;
    g_renderer.detailMapEnabledUniform = detailMapEnabledUniform;
    g_renderer.detailScaleUniform = detailScaleUniform;
    g_renderer.normalMapUniform = normalMapUniform;
    g_renderer.normalMapEnabledUniform = normalMapEnabledUniform;
    g_renderer.specularMapUniform = specularMapUniform;
    g_renderer.specularMapEnabledUniform = specularMapEnabledUniform;
    g_renderer.viewProjectionUniform = viewProjectionUniform;
    g_renderer.sceneFallbackUniform = sceneFallbackUniform;
    g_renderer.lightmapEnabledUniform = lightmapEnabledUniform;
    g_renderer.secondaryLightmapUniform = secondaryLightmapUniform;
    g_renderer.secondaryLightmapEnabledUniform =
        secondaryLightmapEnabledUniform;
    g_renderer.modelLightingUniform = modelLightingUniform;
    g_renderer.modelLightingEnabledUniform = modelLightingEnabledUniform;
    g_renderer.modelLightingBaseCoordinatesUniform =
        modelLightingBaseCoordinatesUniform;
    g_renderer.modelLightingLookupScaleUniform =
        modelLightingLookupScaleUniform;
    g_renderer.premultiplyAlphaUniform = premultiplyAlphaUniform;
    g_renderer.colorIntensityAlphaUniform = colorIntensityAlphaUniform;
    g_renderer.materialModeUniform = materialModeUniform;
    g_renderer.falloffParmsUniform = falloffParmsUniform;
    g_renderer.falloffBeginColorUniform = falloffBeginColorUniform;
    g_renderer.falloffEndColorUniform = falloffEndColorUniform;
    g_renderer.alphaTestUniform = alphaTestUniform;
    g_renderer.instanceEnabledUniform = instanceEnabledUniform;
    g_renderer.uiColorUniform = uiColorUniform;
    g_renderer.fogEnabledUniform = fogEnabledUniform;
    g_renderer.viewOriginUniform = viewOriginUniform;
    g_renderer.fogColorUniform = fogColorUniform;
    g_renderer.fogParamsUniform = fogParamsUniform;
    g_renderer.shadowMapUniform = shadowMapUniform;
    g_renderer.shadowFarMapUniform = shadowFarMapUniform;
    g_renderer.shadowMatrixUniform = shadowMatrixUniform;
    g_renderer.shadowFarMatrixUniform = shadowFarMatrixUniform;
    g_renderer.sunShadowEnabledUniform = sunShadowEnabledUniform;
    g_renderer.sunDirectionUniform = sunDirectionUniform;
    g_renderer.sunColorUniform = sunColorUniform;
    g_renderer.primaryLightmapUniform = primaryLightmapUniform;
    g_renderer.primaryLightFalloffPlacementUniform =
        primaryLightFalloffPlacementUniform;
    g_renderer.primaryLightEnabledUniform = primaryLightEnabledUniform;
    g_renderer.primaryLightPositionRadiusUniform =
        primaryLightPositionRadiusUniform;
    g_renderer.primaryLightDiffuseUniform = primaryLightDiffuseUniform;
    g_renderer.primaryLightSpotDirectionUniform =
        primaryLightSpotDirectionUniform;
    g_renderer.primaryLightSpotFactorsUniform =
        primaryLightSpotFactorsUniform;
    g_renderer.spotShadowMapUniform = spotShadowMapUniform;
    g_renderer.spotShadowMatrixUniform = spotShadowMatrixUniform;
    g_renderer.spotShadowEnabledUniform = spotShadowEnabledUniform;
    g_renderer.waterMapUniform = waterMapUniform;
    g_renderer.reflectionProbeUniform = reflectionProbeUniform;
    g_renderer.envMapParmsUniform = envMapParmsUniform;
    g_renderer.waterColorUniform = waterColorUniform;
    g_renderer.shadowDepthMatrixUniform = shadowDepthMatrixUniform;
    g_renderer.shadowDepthTextureUniform = shadowDepthTextureUniform;
    g_renderer.shadowDepthTextureEnabledUniform =
        shadowDepthTextureEnabledUniform;
    g_renderer.shadowDepthAlphaTestUniform = shadowDepthAlphaTestUniform;
    g_renderer.shadowDepthInstanceEnabledUniform =
        shadowDepthInstanceEnabledUniform;
    g_renderer.skyTextureUniform = skyTextureUniform;
    g_renderer.skyTanHalfFovUniform = skyTanHalfFovUniform;
    g_renderer.skyForwardUniform = skyForwardUniform;
    g_renderer.skyRightUniform = skyRightUniform;
    g_renderer.skyUpUniform = skyUpUniform;
    g_renderer.postProcessTextureUniform = postProcessTextureUniform;
    g_renderer.postProcessFilmEnabledUniform =
        postProcessFilmEnabledUniform;
    g_renderer.postProcessColorBiasUniform = postProcessColorBiasUniform;
    g_renderer.postProcessColorTintBaseUniform =
        postProcessColorTintBaseUniform;
    g_renderer.postProcessColorTintDeltaUniform =
        postProcessColorTintDeltaUniform;
    g_renderer.postProcessGammaExponentUniform =
        postProcessGammaExponentUniform;
    g_renderer.postProcessBlurScaleUniform = postProcessBlurScaleUniform;
    g_renderer.postProcessBlindDarkenUniform =
        postProcessBlindDarkenUniform;
    g_renderer.postProcessGlareLightenUniform =
        postProcessGlareLightenUniform;
    g_renderer.postProcessGlowTextureUniform =
        postProcessGlowTextureUniform;
    g_renderer.postProcessGlowIntensityUniform =
        postProcessGlowIntensityUniform;
    g_renderer.postProcessDepthTextureUniform =
        postProcessDepthTextureUniform;
    g_renderer.postProcessDofEnabledUniform =
        postProcessDofEnabledUniform;
    g_renderer.postProcessDofViewModelUniform =
        postProcessDofViewModelUniform;
    g_renderer.postProcessDofNearUniform = postProcessDofNearUniform;
    g_renderer.postProcessDofFarUniform = postProcessDofFarUniform;
    g_renderer.postProcessDofDepthUniform = postProcessDofDepthUniform;
    g_renderer.glowTextureUniform = glowTextureUniform;
    g_renderer.glowModeUniform = glowModeUniform;
    g_renderer.glowTexelDeltaUniform = glowTexelDeltaUniform;
    g_renderer.glowWeightsUniform = glowWeightsUniform;
    g_renderer.glowSetupUniform = glowSetupUniform;
    g_renderer.compatibilityViewProjectionUniform = compatibilityViewProjection;
    g_renderer.compatibilityWorldUniform = compatibilityWorld;
    g_renderer.compatibilityTextureUniform = compatibilityTexture;
    ++g_renderer.rebuildGeneration;
    if (g_renderer.surfaceActive)
    {
        ++g_renderer.surfaceResourceGeneration;
    }
    if (g_renderer.compatibilityActive)
    {
        ++g_renderer.compatibilityResourceGeneration;
    }
    return true;
}

bool HandleWebGLContextLost(int, const void *, void *)
{
    if (!g_renderer.initialized || g_renderer.context <= 0)
    {
        return false;
    }

    g_renderer.contextLost = true;
#if KISAK_WEB_DIAGNOSTICS
    ResetFrameProfileGpuQueries(false);
#endif
    iassert(g_renderer.contextLost && g_renderer.context > 0);
    DispatchRendererAaLifecycle(
        "lost",
        "The multisample scene target was lost with the WebGL2 context",
        g_renderer.aaConfiguredSamples,
        g_renderer.aaRequestedSamples,
        g_renderer.aaActiveSamples,
        g_renderer.aaMaxSamples,
        g_renderer.multisampleWidth,
        g_renderer.multisampleHeight,
        g_renderer.aaResourceGeneration,
        false);
    ResetGpuHandles();
    EmitSurfaceLifecycle(
        "lost",
        "The renderer retained the indexed surface description while WebGL2 was lost");
    EmitTextureLifecycle(
        "lost",
        "The renderer retained bounded pixels while the WebGL2 context was lost");
    if (g_renderer.compatibilityActive)
    {
        EmitShaderLifecycle(
            "lost",
            "The renderer retained the selected WebGL2 shader contract while the context was lost");
    }
    Web_Log(WebLogLevel::Info, "[kisakcod-web] WebGL2 context lost; rendering paused.\n");
    Web_EmitRuntimeState(
        "renderer-lost",
        "WebGL2 context lost; waiting for the browser to restore it");
    EmitRendererMemory("context-lost");

    // Returning true allows the browser to later deliver a restoration event.
    return true;
}

void InitializeTextureFilteringCapabilities()
{
    g_renderer.maxTextureAnisotropy = 1.0f;
    g_renderer.textureAnisotropySupported =
        emscripten_webgl_enable_extension(
            g_renderer.context, "EXT_texture_filter_anisotropic");
    if (g_renderer.textureAnisotropySupported)
    {
        GLfloat maximum = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximum);
        if (glGetError() == GL_NO_ERROR && std::isfinite(maximum))
            g_renderer.maxTextureAnisotropy = std::max(maximum, 1.0f);
        else
            g_renderer.textureAnisotropySupported = false;
    }
}

bool HandleWebGLContextRestored(int, const void *, void *)
{
    if (!g_renderer.initialized || g_renderer.context <= 0)
    {
        return false;
    }

    if (emscripten_webgl_make_context_current(g_renderer.context) !=
        EMSCRIPTEN_RESULT_SUCCESS)
    {
        EmitSurfaceLifecycle(
            "failed",
            "The restored WebGL2 context could not be made current for surface recovery");
        EmitTextureLifecycle(
            "failed",
            "The restored WebGL2 context could not be made current for texture recovery");
        if (g_renderer.compatibilityActive)
        {
            EmitShaderLifecycle(
                "failed",
                "The restored WebGL2 context could not be made current for shader recovery");
        }
        Web_EmitRuntimeState("failed", "The restored WebGL2 context could not be made current");
        return true;
    }
    InitializeTextureFilteringCapabilities();

    // Every WebGL object became invalid at context loss. Rebuild the pipeline,
    // indexed surface, and texture from renderer-owned, bounded descriptions.
    ResetGpuHandles();
    if (!CreateRendererResources())
    {
        EmitSurfaceLifecycle(
            "failed",
            "The renderer could not recreate the retained indexed surface after context loss");
        EmitTextureLifecycle(
            "failed",
            "The renderer could not recreate the retained texture after context loss");
        if (g_renderer.compatibilityActive)
        {
            EmitShaderLifecycle(
                "failed",
                "The renderer could not recreate the selected WebGL2 shader program after context loss");
        }
        Web_EmitRuntimeState("failed", "The WebGL2 renderer could not recover from context loss");
        return true;
    }

    ++g_renderer.contextGeneration;
#if KISAK_WEB_DIAGNOSTICS
    InitializeFrameProfileGpuQueries();
#endif
    g_renderer.contextLost = false;
    iassert(g_renderer.initialized && !g_renderer.contextLost);
    if (g_renderer.surfaceActive)
    {
        ++g_renderer.surfaceRecoveryCount;
    }
    if (g_renderer.sourceTextureActive)
    {
        ++g_renderer.recoveryCount;
    }
    if (g_renderer.compatibilityActive)
    {
        ++g_renderer.compatibilityRecoveryCount;
    }
    EmitSurfaceLifecycle(
        "ready",
        "The renderer recreated the indexed surface from its retained description");
    EmitTextureLifecycle(
        "ready",
        "The renderer recreated the texture from bounded recovery pixels");
    if (g_renderer.compatibilityActive)
    {
        EmitShaderLifecycle(
            "ready",
            "The renderer recreated the selected WebGL2 shader program after context loss");
    }
    Web_Log(WebLogLevel::Info, "[kisakcod-web] WebGL2 context restored; renderer rebuilt.\n");
    Web_EmitRuntimeState("running", "WebGL2 context restored and rendering resumed");
    EmitRendererMemory("context-restored");
    return true;
}

bool CreateWebGLContext()
{
    const bool created = WebRendererContext_Create(
        g_renderer.context,
        {HandleWebGLContextLost, HandleWebGLContextRestored});
    if (!created)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Unable to create a WebGL2 context (%lu).\n",
            static_cast<unsigned long>(g_renderer.context));
        return false;
    }
    iassert(g_renderer.context > 0);
    InitializeTextureFilteringCapabilities();
#if KISAK_WEB_DIAGNOSTICS
    InitializeFrameProfileGpuQueries();
#endif
    return true;
}

WebRendererTextureResult ValidateTextureDesc(
    const WebRendererRgba8TextureDesc &texture,
    std::size_t &expectedByteLength)
{
    if (texture.pixels == nullptr || texture.width == 0u || texture.height == 0u)
    {
        Web_Log(WebLogLevel::Error, "[kisakcod-web] Invalid empty RGBA8 texture.\n");
        return WebRendererTextureResult::InvalidDescriptor;
    }
    if (texture.width > WEB_RENDERER_MAX_RGBA8_DIMENSION ||
        texture.height > WEB_RENDERER_MAX_RGBA8_DIMENSION)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] RGBA8 texture dimensions exceed the renderer limit (%ux%u).\n",
            WEB_RENDERER_MAX_RGBA8_DIMENSION,
            WEB_RENDERER_MAX_RGBA8_DIMENSION);
        return WebRendererTextureResult::UnsupportedDimensions;
    }

    expectedByteLength = static_cast<std::size_t>(texture.width) *
        static_cast<std::size_t>(texture.height) * 4u;
    if (expectedByteLength > WEB_RENDERER_MAX_RETAINED_TEXTURE_BYTES)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] RGBA8 texture exceeds the recovery limit.\n");
        return WebRendererTextureResult::OutputTooLarge;
    }
    if (texture.byteLength != expectedByteLength)
    {
        Web_Log(WebLogLevel::Error, "[kisakcod-web] RGBA8 texture byte length is invalid.\n");
        return WebRendererTextureResult::InvalidDescriptor;
    }
    return WebRendererTextureResult::Success;
}

bool DecodeExternalCanonicalImage(
    const GfxImage *canonical,
    kisak::iwi::Rgba8Image &decoded,
    kisak::iwi::Error &decodeError,
    std::vector<std::uint8_t> &encodedSource)
{
    if (!canonical || canonical->mapType != MAPTYPE_2D ||
        !canonical->name || !canonical->name[0] ||
        canonical->name[0] == '$' || std::strlen(canonical->name) > 240u)
    {
        return false;
    }

    const std::string path =
        std::string("images/") + canonical->name + ".iwi";
    int file = 0;
    const int fileSize = FS_FOpenFileReadDatabase(path.c_str(), &file);
    if (fileSize < 0) return false;

    if (fileSize > 0 && static_cast<std::size_t>(fileSize) <=
            kisak::iwi::MAX_TEXTURE_MEMBER_BYTES)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
        const std::uint32_t read = FS_Read(bytes.data(),
            static_cast<std::uint32_t>(bytes.size()), file);
        decodeError = read == bytes.size()
            ? kisak::iwi::DecodeRgba8(bytes, decoded)
            : kisak::iwi::Error::InvalidFileSize;
        if (decodeError == kisak::iwi::Error::None)
            encodedSource = std::move(bytes);
    }
    else
    {
        decodeError = kisak::iwi::Error::InvalidFileSize;
    }
    FS_FCloseFile(file);
    return true;
}

bool DecodeCanonicalCubeImage(
    const GfxImage *canonical,
    kisak::iwi::Rgba8Cube &decoded,
    kisak::iwi::Error &decodeError)
{
    if (!canonical || canonical->mapType != MAPTYPE_CUBE ||
        !canonical->name || !canonical->name[0] ||
        std::strlen(canonical->name) > 240u)
    {
        return false;
    }
    WebDbImageLoadDef loadDef{};
    if (DB_WebGetImageLoadDef(canonical, loadDef) &&
        loadDef.byteLength > 0u && loadDef.dimensions[0] > 0 &&
        loadDef.dimensions[0] == loadDef.dimensions[1] &&
        loadDef.dimensions[2] == 1)
    {
        decodeError = kisak::iwi::DecodeLoadDefCubeRgba8(
            loadDef.format, loadDef.flags,
            static_cast<std::uint16_t>(loadDef.dimensions[0]),
            static_cast<std::uint16_t>(loadDef.dimensions[1]),
            static_cast<std::uint16_t>(loadDef.dimensions[2]),
            std::span<const std::uint8_t>(loadDef.data,
                loadDef.byteLength), decoded);
        return true;
    }

    const std::string path =
        std::string("images/") + canonical->name + ".iwi";
    int file = 0;
    const int fileSize = FS_FOpenFileReadDatabase(path.c_str(), &file);
    if (fileSize < 0) return false;
    if (fileSize > 0 && static_cast<std::size_t>(fileSize) <=
            kisak::iwi::MAX_TEXTURE_MEMBER_BYTES)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
        const std::uint32_t read = FS_Read(bytes.data(),
            static_cast<std::uint32_t>(bytes.size()), file);
        decodeError = read == bytes.size()
            ? kisak::iwi::DecodeCubeRgba8(bytes, decoded)
            : kisak::iwi::Error::InvalidFileSize;
    }
    else
    {
        decodeError = kisak::iwi::Error::InvalidFileSize;
    }
    FS_FCloseFile(file);
    return true;
}

std::uint32_t RetainCanonicalWorldImage(
    const GfxImage *canonical,
    std::vector<WebRendererRetainedWorldImage> &images,
    std::size_t &retainedPixelBytes,
    bool retainAuthoredMipChain = false)
{
    if (!canonical) return INVALID_WORLD_IMAGE;
    for (std::uint32_t index = 0u; index < images.size(); ++index)
        if (images[index].canonicalIdentity == canonical) return index;

    WebRendererRetainedWorldImage retained;
    retained.canonicalIdentity = canonical;
    retained.canonicalName = canonical->name ? canonical->name : "<unnamed-image>";
    WebDbImageLoadDef loadDef{};
    const bool hasLoadDef = DB_WebGetImageLoadDef(canonical, loadDef);
    retained.mipmapsAllowed = !hasLoadDef ||
        (loadDef.flags & kisak::iwi::FLAG_NO_MIPMAPS) == 0u;
    kisak::iwi::Rgba8Image decoded;
    kisak::iwi::Error decodeError = kisak::iwi::Error::None;
    WebRendererImageRecoverySource recoverySource =
        WebRendererImageRecoverySource::DecodedRgba8;
    std::vector<std::uint8_t> externalSource;
    bool attemptedDecode = false;
    if (retained.canonicalName == ",$white" ||
        retained.canonicalName == "$white")
    {
        decoded.width = 1u;
        decoded.height = 1u;
        decoded.pixels = {255u, 255u, 255u, 255u};
        retained.mipmapsAllowed = false;
        attemptedDecode = true;
    }
    else if (canonical->mapType == MAPTYPE_2D && hasLoadDef &&
        loadDef.byteLength > 0 && loadDef.dimensions[0] > 0 &&
        loadDef.dimensions[1] > 0 && loadDef.dimensions[2] == 1)
    {
        attemptedDecode = true;
        decodeError = kisak::iwi::DecodeLoadDefRgba8(
            loadDef.format,
            loadDef.flags,
            static_cast<std::uint16_t>(loadDef.dimensions[0]),
            static_cast<std::uint16_t>(loadDef.dimensions[1]),
            static_cast<std::uint16_t>(loadDef.dimensions[2]),
            std::span<const std::uint8_t>(
                loadDef.data,
                loadDef.byteLength),
            decoded);
        if (decodeError == kisak::iwi::Error::None)
            recoverySource = WebRendererImageRecoverySource::LoadDef;
    }
    if ((!attemptedDecode || decodeError != kisak::iwi::Error::None) &&
        retained.canonicalName != ",$white" &&
        retained.canonicalName != "$white")
    {
        kisak::iwi::Rgba8Image externalDecoded;
        kisak::iwi::Error externalError = decodeError;
        if (DecodeExternalCanonicalImage(
                canonical, externalDecoded, externalError, externalSource))
        {
            attemptedDecode = true;
            decodeError = externalError;
            if (decodeError == kisak::iwi::Error::None)
            {
                decoded = std::move(externalDecoded);
                recoverySource = WebRendererImageRecoverySource::IwiMember;
            }
        }
    }

    std::size_t decodedBytes = decoded.pixels.size();
    std::size_t uploadBytes = decodedBytes;
    for (const std::vector<std::uint8_t> &mip : decoded.mipPixels)
    {
        uploadBytes += mip.size();
    }
    if (retainAuthoredMipChain)
    {
        for (const std::vector<std::uint8_t> &mip : decoded.mipPixels)
        {
            if (mip.size() > std::numeric_limits<std::size_t>::max() -
                    decodedBytes)
            {
                decodedBytes = std::numeric_limits<std::size_t>::max();
                break;
            }
            decodedBytes += mip.size();
        }
    }
    if (attemptedDecode && decodeError == kisak::iwi::Error::None &&
        decodedBytes <=
            WEB_RENDERER_MAX_DECODED_TEXTURE_BYTES -
                std::min(retainedPixelBytes,
                    WEB_RENDERER_MAX_DECODED_TEXTURE_BYTES))
    {
        retained.width = decoded.width;
        retained.height = decoded.height;
        if (retained.width <= 1u && retained.height <= 1u)
            retained.mipmapsAllowed = false;
        retainedPixelBytes += decodedBytes;
        retained.recoverySource = recoverySource;
        retained.authoredMipChain = retainAuthoredMipChain;
        retained.decodedByteLength = decodedBytes;
        retained.uploadByteLength = uploadBytes;
        if (recoverySource == WebRendererImageRecoverySource::LoadDef)
        {
            retained.sourceFlags = loadDef.flags;
            std::copy_n(loadDef.dimensions, 3u, retained.sourceDimensions);
            retained.sourceFormat = loadDef.format;
            retained.encodedSource.assign(
                loadDef.data, loadDef.data + loadDef.byteLength);
        }
        else if (recoverySource ==
            WebRendererImageRecoverySource::IwiMember)
        {
            retained.encodedSource = std::move(externalSource);
        }
        else
        {
            retained.pixels = std::move(decoded.pixels);
            if (retainAuthoredMipChain)
                retained.mipPixels = std::move(decoded.mipPixels);
        }
        retained.supported = true;
    }
    else if (attemptedDecode)
    {
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical image '%s' uses backend fallback: %s "
            "(format=0x%08x flags=0x%02x dimensions=%dx%dx%d bytes=%zu).\n",
            retained.canonicalName.c_str(),
            decodeError == kisak::iwi::Error::None
                ? "world texture recovery budget exceeded"
                : kisak::iwi::ErrorString(decodeError),
            static_cast<unsigned int>(loadDef.format),
            static_cast<unsigned int>(loadDef.flags),
            loadDef.dimensions[0], loadDef.dimensions[1],
            loadDef.dimensions[2], loadDef.byteLength);
    }
    else
    {
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical image '%s' has no supported 2D load "
            "definition (mapType=%d loadDef=%p resourceSize=%d dimensions=%dx%dx%d "
            "format=0x%02x).\n",
            retained.canonicalName.c_str(),
            static_cast<int>(canonical->mapType),
            static_cast<const void *>(loadDef.data),
            static_cast<int>(loadDef.byteLength),
            loadDef.dimensions[0],
            loadDef.dimensions[1],
            loadDef.dimensions[2],
            static_cast<unsigned int>(loadDef.format));
    }
    images.push_back(std::move(retained));
    return static_cast<std::uint32_t>(images.size() - 1u);
}

GLuint FindRetainedWorldReflectionTexture(
    std::uint8_t reflectionProbeIndex) noexcept
{
    const auto found = std::find_if(
        g_renderer.retainedWorldBatches.begin(),
        g_renderer.retainedWorldBatches.end(),
        [reflectionProbeIndex](const WebRendererRetainedWorldBatch &batch)
        {
            return batch.reflectionProbeIndex == reflectionProbeIndex &&
                batch.reflectionTexture != 0u;
        });
    return found != g_renderer.retainedWorldBatches.end()
        ? found->reflectionTexture : 0u;
}

void AttachRetainedWorldReflectionTextures() noexcept
{
    for (WebRendererRetainedStaticModelBatch &batch :
         g_renderer.retainedStaticModelBatches)
    {
        batch.draw.reflectionTexture = FindRetainedWorldReflectionTexture(
            batch.draw.reflectionProbeIndex);
    }
    for (WebRendererRetainedWorldBatch &batch :
         g_renderer.retainedDynamicModelBatches)
    {
        if (WebRenderer_UsesModelEnvironmentSpecular(batch.technique))
            batch.reflectionTexture = FindRetainedWorldReflectionTexture(
                batch.reflectionProbeIndex);
    }
}

WebRendererSurfaceResult CopyWorldCommand(
    const WebRendererWorldSurfaceDesc &surface,
    std::vector<WebRendererSurfaceVertex> &vertices,
    std::vector<std::uint32_t> &indices,
    std::vector<WebRendererRetainedWorldBatch> &batches,
    std::vector<WebRendererRetainedWorldImage> &images,
    std::vector<WebRendererRetainedPrimaryLight> &primaryLights,
    std::uint32_t &sunPrimaryLightIndex,
    std::uint32_t inheritedPrimaryLightCount,
    std::vector<WebRendererRetainedSpotShadowCaster> *spotShadowCasters,
    std::vector<WebRendererRetainedSpotShadowStaticModel> *
        spotShadowStaticModels)
{
    if (!surface.vertices || !surface.indices || !surface.batches ||
        surface.vertexCount == 0u || surface.indexCount == 0u ||
        surface.batchCount == 0u ||
        surface.vertexCount > WEB_RENDERER_MAX_WORLD_VERTICES ||
        surface.indexCount > WEB_RENDERER_MAX_WORLD_INDICES)
    {
        Web_Log(WebLogLevel::Error,
            "[kisakcod-web] Invalid canonical world descriptor header: "
            "vertices=%u/%u indices=%u/%u batches=%u.\n",
            surface.vertexCount, WEB_RENDERER_MAX_WORLD_VERTICES,
            surface.indexCount, WEB_RENDERER_MAX_WORLD_INDICES,
            surface.batchCount);
        return WebRendererSurfaceResult::InvalidDescriptor;
    }
    if (surface.primaryLightCount > WEB_RENDERER_MAX_PRIMARY_LIGHTS ||
        (surface.primaryLightCount != 0u && !surface.primaryLights) ||
        (surface.primaryLightCount == 0u && surface.primaryLights) ||
        (surface.primaryLightCount != 0u &&
            surface.sunPrimaryLightIndex >= surface.primaryLightCount))
    {
        Web_Log(WebLogLevel::Error,
            "[kisakcod-web] Invalid canonical world primary-light header: "
            "lights=%u/%u sun=%u.\n",
            surface.primaryLightCount, WEB_RENDERER_MAX_PRIMARY_LIGHTS,
            surface.sunPrimaryLightIndex);
        return WebRendererSurfaceResult::InvalidDescriptor;
    }
    if ((surface.spotShadowCasterCount != 0u &&
            !surface.spotShadowCasters) ||
        (surface.spotShadowCasterCount == 0u &&
            surface.spotShadowCasters))
    {
        Web_Log(WebLogLevel::Error,
            "[kisakcod-web] Invalid canonical world spot-shadow caster "
            "header: count=%u.\n", surface.spotShadowCasterCount);
        return WebRendererSurfaceResult::InvalidDescriptor;
    }
    if ((surface.spotShadowStaticModelCount != 0u &&
            !surface.spotShadowStaticModels) ||
        (surface.spotShadowStaticModelCount == 0u &&
            surface.spotShadowStaticModels))
    {
        Web_Log(WebLogLevel::Error,
            "[kisakcod-web] Invalid canonical world spot-shadow static-model "
            "header: count=%u.\n", surface.spotShadowStaticModelCount);
        return WebRendererSurfaceResult::InvalidDescriptor;
    }
    // Moving brush-model batches keep the GfxWorld primary-light indices
    // selected by the canonical frontend. Dynamic commands do not duplicate
    // that immutable table, so validate those references against the world
    // table already retained by the backend.
    const std::uint32_t primaryLightReferenceCount =
        surface.primaryLightCount != 0u
        ? surface.primaryLightCount
        : inheritedPrimaryLightCount;
    for (std::uint32_t vertexIndex = 0u;
         vertexIndex < surface.vertexCount; ++vertexIndex)
    {
        const WebRendererSurfaceVertex &vertex = surface.vertices[vertexIndex];
        for (const float component : vertex.position)
            if (!std::isfinite(component))
                return WebRendererSurfaceResult::NonFiniteVertex;
        for (const float component : vertex.color)
            if (!std::isfinite(component))
                return WebRendererSurfaceResult::NonFiniteVertex;
        for (const float component : vertex.textureCoordinate)
            if (!std::isfinite(component))
                return WebRendererSurfaceResult::NonFiniteVertex;
        for (const float component : vertex.lightmapCoordinate)
            if (!std::isfinite(component))
                return WebRendererSurfaceResult::NonFiniteVertex;
        for (const float component : vertex.normal)
            if (!std::isfinite(component))
                return WebRendererSurfaceResult::NonFiniteVertex;
        for (const float component : vertex.tangent)
            if (!std::isfinite(component))
                return WebRendererSurfaceResult::NonFiniteVertex;
        if (!std::isfinite(vertex.binormalSign))
            return WebRendererSurfaceResult::NonFiniteVertex;
    }
    for (std::uint32_t index = 0u; index < surface.indexCount; ++index)
        if (surface.indices[index] >= surface.vertexCount)
            return WebRendererSurfaceResult::IndexOutOfRange;

    // Dynamic scenes reuse this vector across frames. Count authored mip
    // levels already in the pool as well as base pixels before admitting a
    // new image against the per-pool recovery allowance.
    std::size_t retainedPixelBytes = RetainedImageStats(images).decodedBytes;
    try
    {
        vertices.assign(surface.vertices, surface.vertices + surface.vertexCount);
        indices.assign(surface.indices, surface.indices + surface.indexCount);
        batches.reserve(surface.batchCount);
        images.reserve(surface.batchCount * 3u);
        primaryLights.reserve(surface.primaryLightCount);
        for (std::uint32_t index = 0u;
             index < surface.primaryLightCount; ++index)
        {
            const WebRendererPrimaryLightDesc &source =
                surface.primaryLights[index];
            WebRendererRetainedPrimaryLight light;
            light.type = source.type;
            light.exponent = source.exponent;
            light.canUseShadowMap = source.canUseShadowMap != 0u;
            light.radius = source.radius;
            light.cosHalfFovOuter = source.cosHalfFovOuter;
            light.cosHalfFovInner = source.cosHalfFovInner;
            light.falloffScale = source.falloffScale;
            light.falloffShift = source.falloffShift;
            std::copy_n(source.color, 3u, light.color);
            std::copy_n(source.direction, 3u, light.direction);
            std::copy_n(source.origin, 3u, light.origin);
            const bool localLight = light.type == 2u || light.type == 3u;
            for (std::size_t component = 0u; component < 3u; ++component)
            {
                if (!std::isfinite(light.color[component]) ||
                    !std::isfinite(light.direction[component]) ||
                    !std::isfinite(light.origin[component]))
                {
                    return WebRendererSurfaceResult::NonFiniteVertex;
                }
            }
            if (!std::isfinite(light.radius) ||
                !std::isfinite(light.cosHalfFovOuter) ||
                !std::isfinite(light.cosHalfFovInner) ||
                !std::isfinite(light.falloffScale) ||
                !std::isfinite(light.falloffShift) ||
                (localLight && (light.radius <= 0.0f ||
                    light.falloffScale <= 0.0f ||
                    light.falloffShift < 0.0f ||
                    light.falloffShift + light.falloffScale > 1.0f)) ||
                (light.type == 2u && light.cosHalfFovInner <=
                    light.cosHalfFovOuter))
            {
                Web_Log(WebLogLevel::Error,
                    "[kisakcod-web] Invalid canonical primary light %u: "
                    "type=%u radius=%g fov=(%g %g) falloff=(%g %g).\n",
                    index, static_cast<unsigned int>(light.type), light.radius,
                    light.cosHalfFovOuter, light.cosHalfFovInner,
                    light.falloffScale, light.falloffShift);
                return WebRendererSurfaceResult::InvalidDescriptor;
            }
            primaryLights.push_back(light);
        }
        sunPrimaryLightIndex = surface.sunPrimaryLightIndex;
        std::uint32_t expectedFirstIndex = 0u;
        for (std::uint32_t index = 0u; index < surface.batchCount; ++index)
        {
            const WebRendererWorldBatchDesc &source = surface.batches[index];
            if (source.indexCount == 0u || source.surfaceCount == 0u ||
                (source.firstIndex % 3u) != 0u ||
                (source.indexCount % 3u) != 0u ||
                source.firstIndex != expectedFirstIndex ||
                source.firstIndex > surface.indexCount ||
                source.indexCount > surface.indexCount - source.firstIndex ||
                source.firstSurfaceIndex > source.lastSurfaceIndex ||
                source.technique >
                    WebRendererWorldTechnique::NativeTechniqueUnavailable ||
                source.lightingMode >
                    WebRendererWorldLightingMode::ModelLightGrid ||
                (primaryLightReferenceCount == 0u
                    ? source.primaryLightIndex != 0u
                    : source.primaryLightIndex >=
                        primaryLightReferenceCount))
            {
                Web_Log(WebLogLevel::Error,
                    "[kisakcod-web] Invalid canonical world batch %u: "
                    "indices=%u+%u expected=%u/%u surfaces=%u[%u,%u] "
                    "technique=%u lighting=%u primaryLight=%u/%u.\n",
                    index, source.firstIndex, source.indexCount,
                    expectedFirstIndex, surface.indexCount,
                    source.surfaceCount, source.firstSurfaceIndex,
                    source.lastSurfaceIndex,
                    static_cast<unsigned int>(source.technique),
                    static_cast<unsigned int>(source.lightingMode),
                    source.primaryLightIndex, primaryLightReferenceCount);
                return WebRendererSurfaceResult::InvalidDescriptor;
            }
            expectedFirstIndex += source.indexCount;
            WebRendererRetainedWorldBatch batch;
            batch.firstIndex = source.firstIndex;
            batch.indexCount = source.indexCount;
            batch.surfaceCount = source.surfaceCount;
            batch.firstSurfaceIndex = source.firstSurfaceIndex;
            batch.lastSurfaceIndex = source.lastSurfaceIndex;
            batch.materialIdentity = source.materialIdentity;
            batch.materialName = source.materialName
                ? source.materialName : "<null-material>";
            batch.modelIdentity = source.modelIdentity;
            batch.modelName = source.modelName
                ? source.modelName : "<world>";
            batch.firstInstanceIndex = source.firstInstanceIndex;
            batch.lastInstanceIndex = source.lastInstanceIndex;
            batch.stateBits[0] = source.stateBits[0];
            batch.stateBits[1] = source.stateBits[1];
            batch.samplerState = source.samplerState;
            batch.detailSamplerState = source.detailSamplerState;
            batch.normalSamplerState = source.normalSamplerState;
            batch.specularSamplerState = source.specularSamplerState;
            batch.lightmapIndex = source.lightmapIndex;
            batch.primaryLightIndex = source.primaryLightIndex;
            batch.sourceKind = source.sourceKind;
            batch.technique = source.technique;
            batch.lightingMode = source.lightingMode;
            batch.techniqueName = source.techniqueName
                ? source.techniqueName : "<unsupported-technique>";
            batch.techniqueType = source.techniqueType;
            batch.customSamplerFlags = source.customSamplerFlags;
            batch.techniqueFlags = source.techniqueFlags;
            batch.cameraRegion = source.cameraRegion;
            batch.depthHack = source.depthHack;
            batch.ambientProbeLighting = source.ambientProbeLighting;
            batch.castsSunShadow = source.castsSunShadow;
            batch.shadowStateBits0 = source.shadowStateBits0;
            batch.vertexShaderName = source.vertexShaderName
                ? source.vertexShaderName : "<unavailable-vertex-shader>";
            batch.vertexShaderProgramHash = source.vertexShaderProgramHash;
            batch.pixelShaderName = source.pixelShaderName
                ? source.pixelShaderName : "<unavailable-pixel-shader>";
            batch.pixelShaderProgramHash = source.pixelShaderProgramHash;
            std::copy_n(source.modelLightingCoordinates, 3u,
                batch.modelLightingCoordinates);
            batch.waterSamplerState = source.waterSamplerState;
            batch.reflectionProbeIndex = source.reflectionProbeIndex;
            std::copy_n(source.envMapParms, 4u, batch.envMapParms);
            std::copy_n(source.detailScale, 4u, batch.detailScale);
            std::copy_n(source.waterColor, 4u, batch.waterColor);
            std::copy_n(source.falloffParms, 4u, batch.falloffParms);
            std::copy_n(source.falloffBeginColor, 4u,
                batch.falloffBeginColor);
            std::copy_n(source.falloffEndColor, 4u,
                batch.falloffEndColor);
            for (const float component : batch.modelLightingCoordinates)
                if (!std::isfinite(component))
                    return WebRendererSurfaceResult::NonFiniteVertex;
            for (const float component : batch.envMapParms)
                if (!std::isfinite(component))
                    return WebRendererSurfaceResult::NonFiniteVertex;
            for (const float component : batch.waterColor)
                if (!std::isfinite(component))
                    return WebRendererSurfaceResult::NonFiniteVertex;
            for (const float component : batch.falloffParms)
                if (!std::isfinite(component))
                    return WebRendererSurfaceResult::NonFiniteVertex;
            for (const float component : batch.falloffBeginColor)
                if (!std::isfinite(component))
                    return WebRendererSurfaceResult::NonFiniteVertex;
            for (const float component : batch.falloffEndColor)
                if (!std::isfinite(component))
                    return WebRendererSurfaceResult::NonFiniteVertex;
            bool reflectionSupported =
                !WebRenderer_UsesWorldSpecularMap(source.technique);
            bool waterSupported = false;
            if (source.technique == WebRendererWorldTechnique::WaterLitSun)
            {
                if (!source.water || !source.water->H0 ||
                    !source.water->wTerm || source.water->M < 4 ||
                    source.water->M > 64 ||
                    source.water->N != source.water->M ||
                    (source.water->M & (source.water->M - 1)) != 0 ||
                    !source.reflectionProbeImage)
                {
                    Web_Log(WebLogLevel::Error,
                        "[kisakcod-web] Invalid canonical water batch %u: "
                        "grid=%dx%d.\n", index,
                        source.water ? source.water->M : 0,
                        source.water ? source.water->N : 0);
                    return WebRendererSurfaceResult::InvalidDescriptor;
                }
                const std::size_t waterCount =
                    static_cast<std::size_t>(source.water->M) *
                    static_cast<std::size_t>(source.water->N);
                batch.waterM = source.water->M;
                batch.waterN = source.water->N;
                batch.waterH0.assign(
                    source.water->H0, source.water->H0 + waterCount);
                batch.waterWTerm.assign(
                    source.water->wTerm, source.water->wTerm + waterCount);
                batch.waterPixels.resize(waterCount);
                waterSupported = true;
            }
            if (WebRenderer_UsesModelEnvironmentSpecular(source.technique))
            {
                batch.reflectionTexture = FindRetainedWorldReflectionTexture(
                    source.reflectionProbeIndex);
                reflectionSupported = batch.reflectionTexture != 0u;
            }
            else if ((source.technique == WebRendererWorldTechnique::WaterLitSun ||
                    WebRenderer_UsesWorldSpecularMap(source.technique)) &&
                source.reflectionProbeImage)
            {
                const auto retainedProbe = std::find_if(
                    batches.begin(), batches.end(),
                    [&source](const WebRendererRetainedWorldBatch &candidate)
                    {
                        return candidate.reflectionProbeIndex ==
                                source.reflectionProbeIndex &&
                            candidate.reflectionCube.edgeLength != 0u;
                    });
                reflectionSupported = retainedProbe != batches.end();
                if (!reflectionSupported)
                {
                    kisak::iwi::Error reflectionError =
                        kisak::iwi::Error::None;
                    reflectionSupported = DecodeCanonicalCubeImage(
                            source.reflectionProbeImage, batch.reflectionCube,
                            reflectionError) &&
                        reflectionError == kisak::iwi::Error::None;
                    if (!reflectionSupported)
                    {
                        Web_Log(WebLogLevel::Info,
                            "[kisakcod-web] Canonical material reflection "
                            "probe '%s' uses backend fallback: %s.\n",
                            source.reflectionProbeImage->name
                                ? source.reflectionProbeImage->name
                                : "<unnamed>",
                            kisak::iwi::ErrorString(reflectionError));
                    }
                }
            }
            waterSupported = waterSupported && reflectionSupported;
            batch.baseImageIndex = RetainCanonicalWorldImage(
                source.baseImage, images, retainedPixelBytes,
                (source.stateBits[0] & 0x800u) == 0u &&
                    (source.stateBits[0] & 0x3000u) != 0u &&
                    (source.samplerState & 0x18u) != 0u);
            batch.detailImageIndex = RetainCanonicalWorldImage(
                source.detailImage, images, retainedPixelBytes);
            if (source.lightingMode ==
                    WebRendererWorldLightingMode::ModelLightGrid ||
                WebRenderer_UsesWorldNormalMap(source.technique))
            {
                batch.normalImageIndex = RetainCanonicalWorldImage(
                    source.normalImage, images, retainedPixelBytes);
            }
            if (WebRenderer_UsesWorldSpecularMap(source.technique))
            {
                batch.specularImageIndex = RetainCanonicalWorldImage(
                    source.specularImage, images, retainedPixelBytes);
            }
            batch.lightmapImageIndex = RetainCanonicalWorldImage(
                source.lightmapImage, images, retainedPixelBytes);
            batch.secondaryLightmapImageIndex = RetainCanonicalWorldImage(
                source.secondaryLightmapImage, images, retainedPixelBytes);
            const bool baseSupported =
                batch.baseImageIndex != INVALID_WORLD_IMAGE &&
                images[batch.baseImageIndex].supported;
            const bool secondaryLightmapSupported =
                batch.secondaryLightmapImageIndex != INVALID_WORLD_IMAGE &&
                images[batch.secondaryLightmapImageIndex].supported;
            const bool normalMapSupported =
                batch.normalImageIndex != INVALID_WORLD_IMAGE &&
                images[batch.normalImageIndex].supported;
            const bool specularMapSupported =
                batch.specularImageIndex != INVALID_WORLD_IMAGE &&
                images[batch.specularImageIndex].supported;
            if (WebRenderer_SkipsNativeDraw(source.technique))
            {
                // Keep native R_SetupMaterial failure as an intentional skip.
                // Missing image recovery must never turn it into a draw.
            }
            else if (source.technique ==
                    WebRendererWorldTechnique::WaterLitSun &&
                !waterSupported)
            {
                batch.technique = WebRendererWorldTechnique::BackendFallback;
            }
            else if (source.technique !=
                    WebRendererWorldTechnique::WaterLitSun &&
                !baseSupported)
                batch.technique = WebRendererWorldTechnique::BackendFallback;
            else if (WebRenderer_UsesSecondaryDirectionalLightmap(
                    batch.technique) &&
                (batch.lightingMode !=
                        WebRendererWorldLightingMode::SecondaryDirectional ||
                    !secondaryLightmapSupported))
                batch.technique = WebRendererWorldTechnique::BaseTexture;
            else if (WebRenderer_UsesWorldNormalMap(batch.technique) &&
                !normalMapSupported)
                batch.technique = WebRenderer_UsesModelEnvironmentSpecular(
                        batch.technique)
                    ? WebRendererWorldTechnique::BaseTexture
                    : (WebRenderer_UsesWorldSpecularMap(batch.technique) &&
                            specularMapSupported && reflectionSupported
                        ? WebRendererWorldTechnique::BaseTextureLightmapSpecular
                        : WebRendererWorldTechnique::BaseTextureLightmap);
            else if (WebRenderer_UsesWorldSpecularMap(batch.technique) &&
                (!specularMapSupported || !reflectionSupported))
                batch.technique = WebRenderer_UsesModelEnvironmentSpecular(
                        batch.technique)
                    ? WebRendererWorldTechnique::BaseTexture
                    : (WebRenderer_UsesWorldNormalMap(batch.technique)
                        ? WebRendererWorldTechnique::BaseTextureLightmapNormal
                        : WebRendererWorldTechnique::BaseTextureLightmap);
            batches.push_back(std::move(batch));
        }
        if (spotShadowCasters)
        {
            spotShadowCasters->reserve(surface.spotShadowCasterCount);
            for (std::uint32_t index = 0u;
                 index < surface.spotShadowCasterCount; ++index)
            {
                const WebRendererSpotShadowCasterDesc &caster =
                    surface.spotShadowCasters[index];
                if (caster.primaryLightIndex >= primaryLightReferenceCount ||
                    caster.batchIndex >= batches.size() ||
                    caster.indexCount == 0u ||
                    (caster.firstIndex % 3u) != 0u ||
                    (caster.indexCount % 3u) != 0u ||
                    caster.firstIndex > surface.indexCount ||
                    caster.indexCount >
                        surface.indexCount - caster.firstIndex)
                {
                    Web_Log(WebLogLevel::Error,
                        "[kisakcod-web] Invalid canonical spot-shadow caster "
                        "%u: light=%u/%u batch=%u/%zu indices=%u+%u/%u.\n",
                        index, caster.primaryLightIndex,
                        primaryLightReferenceCount, caster.batchIndex,
                        batches.size(), caster.firstIndex, caster.indexCount,
                        surface.indexCount);
                    return WebRendererSurfaceResult::InvalidDescriptor;
                }
                const WebRendererRetainedWorldBatch &batch =
                    batches[caster.batchIndex];
                if (caster.firstIndex < batch.firstIndex ||
                    caster.firstIndex + caster.indexCount >
                        batch.firstIndex + batch.indexCount)
                {
                    Web_Log(WebLogLevel::Error,
                        "[kisakcod-web] Canonical spot-shadow caster %u lies "
                        "outside batch %u.\n", index, caster.batchIndex);
                    return WebRendererSurfaceResult::InvalidDescriptor;
                }
                spotShadowCasters->push_back(caster);
            }
        }
        else if (surface.spotShadowCasterCount != 0u)
        {
            return WebRendererSurfaceResult::InvalidDescriptor;
        }
        if (spotShadowStaticModels)
        {
            spotShadowStaticModels->reserve(
                surface.spotShadowStaticModelCount);
            for (std::uint32_t index = 0u;
                 index < surface.spotShadowStaticModelCount; ++index)
            {
                const WebRendererSpotShadowStaticModelDesc &model =
                    surface.spotShadowStaticModels[index];
                if (model.primaryLightIndex >= primaryLightReferenceCount ||
                    model.canonicalInstanceIndex >=
                        WEB_RENDERER_MAX_STATIC_MODEL_INSTANCES)
                {
                    Web_Log(WebLogLevel::Error,
                        "[kisakcod-web] Invalid canonical spot-shadow static "
                        "model %u: light=%u/%u instance=%u/%u.\n",
                        index, model.primaryLightIndex,
                        primaryLightReferenceCount,
                        model.canonicalInstanceIndex,
                        WEB_RENDERER_MAX_STATIC_MODEL_INSTANCES);
                    return WebRendererSurfaceResult::InvalidDescriptor;
                }
                spotShadowStaticModels->push_back(model);
            }
            std::sort(spotShadowStaticModels->begin(),
                spotShadowStaticModels->end(),
                [](const WebRendererRetainedSpotShadowStaticModel &left,
                   const WebRendererRetainedSpotShadowStaticModel &right)
                {
                    if (left.primaryLightIndex != right.primaryLightIndex)
                        return left.primaryLightIndex < right.primaryLightIndex;
                    return left.canonicalInstanceIndex <
                        right.canonicalInstanceIndex;
                });
        }
        else if (surface.spotShadowStaticModelCount != 0u)
        {
            return WebRendererSurfaceResult::InvalidDescriptor;
        }
        if (expectedFirstIndex != surface.indexCount)
        {
            Web_Log(WebLogLevel::Error,
                "[kisakcod-web] Invalid canonical world index coverage: "
                "batches=%u indices=%u/%u.\n", surface.batchCount,
                expectedFirstIndex, surface.indexCount);
            return WebRendererSurfaceResult::InvalidDescriptor;
        }
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererSurfaceResult::AllocationFailed;
    }
    return WebRendererSurfaceResult::Success;
}

WebRendererSurfaceResult CopyStaticModelCommand(
    const WebRendererStaticModelSceneDesc &scene,
    std::vector<WebRendererSurfaceVertex> &vertices,
    std::vector<std::uint32_t> &indices,
    std::vector<WebRendererStaticModelInstanceDesc> &instances,
    std::vector<WebRendererRetainedStaticModelBatch> &batches,
    std::vector<WebRendererRetainedWorldImage> &images)
{
    if (!scene.vertices || !scene.indices || !scene.instances ||
        !scene.batches || scene.vertexCount == 0u || scene.indexCount == 0u ||
        scene.instanceCount == 0u || scene.batchCount == 0u ||
        scene.modelCount == 0u || scene.surfaceCount == 0u ||
        scene.vertexCount > WEB_RENDERER_MAX_STATIC_MODEL_VERTICES ||
        scene.indexCount > WEB_RENDERER_MAX_STATIC_MODEL_INDICES ||
        scene.instanceCount > WEB_RENDERER_MAX_STATIC_MODEL_INSTANCES)
    {
        return WebRendererSurfaceResult::InvalidDescriptor;
    }
    for (std::uint32_t vertexIndex = 0u;
         vertexIndex < scene.vertexCount; ++vertexIndex)
    {
        const WebRendererSurfaceVertex &vertex = scene.vertices[vertexIndex];
        const float *components = &vertex.position[0];
        for (std::size_t component = 0u; component < 18u; ++component)
            if (!std::isfinite(components[component]))
                return WebRendererSurfaceResult::NonFiniteVertex;
    }
    for (std::uint32_t index = 0u; index < scene.indexCount; ++index)
        if (scene.indices[index] >= scene.vertexCount)
            return WebRendererSurfaceResult::IndexOutOfRange;
    for (std::uint32_t instanceIndex = 0u;
         instanceIndex < scene.instanceCount; ++instanceIndex)
    {
        const WebRendererStaticModelInstanceDesc &instance =
            scene.instances[instanceIndex];
        const float *axis = &instance.axis[0][0];
        for (std::size_t component = 0u; component < 9u; ++component)
            if (!std::isfinite(axis[component]))
                return WebRendererSurfaceResult::NonFiniteVertex;
        for (const float component : instance.origin)
            if (!std::isfinite(component))
                return WebRendererSurfaceResult::NonFiniteVertex;
        for (const float component : instance.modelLightingCoordinates)
            if (!std::isfinite(component))
                return WebRendererSurfaceResult::NonFiniteVertex;
        if (!std::isfinite(instance.modelScale) || instance.modelScale <= 0.0f ||
            !std::isfinite(instance.modelCullDistance))
            return WebRendererSurfaceResult::InvalidDescriptor;
    }

    std::size_t retainedPixelBytes = 0u;
    try
    {
        vertices.assign(scene.vertices, scene.vertices + scene.vertexCount);
        indices.assign(scene.indices, scene.indices + scene.indexCount);
        instances.assign(
            scene.instances, scene.instances + scene.instanceCount);
        batches.reserve(scene.batchCount);
        images.reserve(scene.batchCount);
        for (std::uint32_t index = 0u; index < scene.batchCount; ++index)
        {
            const WebRendererStaticModelBatchDesc &source = scene.batches[index];
            const WebRendererWorldBatchDesc &draw = source.draw;
            const bool staticModelTechnique =
                draw.technique == WebRendererWorldTechnique::BackendFallback ||
                draw.technique == WebRendererWorldTechnique::BaseTexture ||
                draw.technique ==
                    WebRendererWorldTechnique::BaseTextureSpecular ||
                draw.technique ==
                    WebRendererWorldTechnique::BaseTextureNormalSpecular;
            if (draw.sourceKind != WebRendererSceneBatchKind::StaticXModel ||
                !draw.modelIdentity || draw.indexCount == 0u ||
                draw.surfaceCount == 0u || source.instanceCount == 0u ||
                (draw.firstIndex % 3u) != 0u ||
                (draw.indexCount % 3u) != 0u ||
                draw.firstIndex > scene.indexCount ||
                draw.indexCount > scene.indexCount - draw.firstIndex ||
                source.instanceOffset > scene.instanceCount ||
                source.instanceCount >
                    scene.instanceCount - source.instanceOffset ||
                source.lodIndex >= draw.modelIdentity->numLods ||
                source.lodIndex >= MAX_LODS ||
                !staticModelTechnique ||
                draw.lightingMode >
                    WebRendererWorldLightingMode::ModelLightGrid)
            {
                return WebRendererSurfaceResult::InvalidDescriptor;
            }
            WebRendererRetainedStaticModelBatch batch;
            batch.sourceInstanceOffset = source.instanceOffset;
            batch.sourceInstanceCount = source.instanceCount;
            batch.instanceOffset = source.instanceOffset;
            batch.instanceCount = source.instanceCount;
            batch.lodIndex = source.lodIndex;
            batch.draw.firstIndex = draw.firstIndex;
            batch.draw.indexCount = draw.indexCount;
            batch.draw.surfaceCount = draw.surfaceCount;
            batch.draw.firstSurfaceIndex = draw.firstSurfaceIndex;
            batch.draw.lastSurfaceIndex = draw.lastSurfaceIndex;
            batch.draw.materialIdentity = draw.materialIdentity;
            batch.draw.materialName = draw.materialName
                ? draw.materialName : "<null-material>";
            batch.draw.modelIdentity = draw.modelIdentity;
            batch.draw.modelName = draw.modelName
                ? draw.modelName : "<unnamed-xmodel>";
            batch.draw.firstInstanceIndex = draw.firstInstanceIndex;
            batch.draw.lastInstanceIndex = draw.lastInstanceIndex;
            batch.draw.stateBits[0] = draw.stateBits[0];
            batch.draw.stateBits[1] = draw.stateBits[1];
            batch.draw.samplerState = draw.samplerState;
            batch.draw.detailSamplerState = draw.detailSamplerState;
            batch.draw.normalSamplerState = draw.normalSamplerState;
            batch.draw.specularSamplerState = draw.specularSamplerState;
            batch.draw.lightmapIndex = 31u;
            batch.draw.sourceKind = draw.sourceKind;
            batch.draw.technique = draw.technique;
            batch.draw.lightingMode = draw.lightingMode;
            batch.draw.techniqueName = draw.techniqueName
                ? draw.techniqueName : "<unsupported-technique>";
            batch.draw.techniqueType = draw.techniqueType;
            batch.draw.customSamplerFlags = draw.customSamplerFlags;
            batch.draw.techniqueFlags = draw.techniqueFlags;
            batch.draw.cameraRegion = draw.cameraRegion;
            batch.draw.ambientProbeLighting = draw.ambientProbeLighting;
            batch.draw.castsSunShadow = draw.castsSunShadow;
            batch.draw.shadowStateBits0 = draw.shadowStateBits0;
            batch.draw.vertexShaderName = draw.vertexShaderName
                ? draw.vertexShaderName : "<unavailable-vertex-shader>";
            batch.draw.vertexShaderProgramHash =
                draw.vertexShaderProgramHash;
            batch.draw.pixelShaderName = draw.pixelShaderName
                ? draw.pixelShaderName : "<unavailable-pixel-shader>";
            batch.draw.pixelShaderProgramHash = draw.pixelShaderProgramHash;
            batch.draw.reflectionProbeIndex = draw.reflectionProbeIndex;
            batch.draw.reflectionTexture =
                FindRetainedWorldReflectionTexture(
                    draw.reflectionProbeIndex);
            std::copy_n(draw.envMapParms, 4u, batch.draw.envMapParms);
            std::copy_n(draw.detailScale, 4u, batch.draw.detailScale);
            std::copy_n(draw.falloffParms, 4u,
                batch.draw.falloffParms);
            std::copy_n(draw.falloffBeginColor, 4u,
                batch.draw.falloffBeginColor);
            std::copy_n(draw.falloffEndColor, 4u,
                batch.draw.falloffEndColor);
            batch.draw.baseImageIndex = RetainCanonicalWorldImage(
                draw.baseImage, images, retainedPixelBytes,
                (draw.stateBits[0] & 0x800u) == 0u &&
                    (draw.stateBits[0] & 0x3000u) != 0u &&
                    (draw.samplerState & 0x18u) != 0u);
            batch.draw.detailImageIndex = RetainCanonicalWorldImage(
                draw.detailImage, images, retainedPixelBytes);
            const bool baseSupported =
                batch.draw.baseImageIndex != INVALID_WORLD_IMAGE &&
                images[batch.draw.baseImageIndex].supported;
            if (!baseSupported)
                batch.draw.technique =
                    WebRendererWorldTechnique::BackendFallback;
            batches.push_back(std::move(batch));
        }
        // Preserve base-color and authored normal coverage before spending the
        // bounded recovery allowance on the new SM3 specular tier.
        for (std::uint32_t index = 0u; index < scene.batchCount; ++index)
        {
            const WebRendererWorldBatchDesc &draw = scene.batches[index].draw;
            if (draw.lightingMode ==
                WebRendererWorldLightingMode::ModelLightGrid)
            {
                batches[index].draw.normalImageIndex =
                    RetainCanonicalWorldImage(
                        draw.normalImage, images, retainedPixelBytes);
            }
        }
        for (std::uint32_t index = 0u; index < scene.batchCount; ++index)
        {
            const WebRendererWorldBatchDesc &draw = scene.batches[index].draw;
            if (WebRenderer_UsesModelEnvironmentSpecular(draw.technique))
            {
                batches[index].draw.specularImageIndex =
                    RetainCanonicalWorldImage(
                        draw.specularImage, images, retainedPixelBytes);
                const std::uint32_t specularIndex =
                    batches[index].draw.specularImageIndex;
                if (specularIndex == INVALID_WORLD_IMAGE ||
                    !images[specularIndex].supported ||
                    (g_renderer.initialized && !g_renderer.contextLost &&
                        batches[index].draw.reflectionTexture == 0u))
                {
                    batches[index].draw.technique =
                        WebRendererWorldTechnique::BaseTexture;
                }
            }
        }
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererSurfaceResult::AllocationFailed;
    }
    return WebRendererSurfaceResult::Success;
}
} // namespace

const char *WebRenderer_TextureResultString(WebRendererTextureResult result) noexcept
{
    switch (result)
    {
    case WebRendererTextureResult::Success: return "success";
    case WebRendererTextureResult::InvalidDescriptor:
        return "invalid RGBA8 texture descriptor";
    case WebRendererTextureResult::UnsupportedDimensions:
        return "RGBA8 texture dimensions exceed the backend limit";
    case WebRendererTextureResult::OutputTooLarge:
        return "RGBA8 texture exceeds the renderer recovery limit";
    case WebRendererTextureResult::AllocationFailed:
        return "renderer recovery allocation failed";
    case WebRendererTextureResult::BackendFailure:
        return "WebGL2 texture upload failed";
    }
    return "unknown renderer texture error";
}

const char *WebRenderer_ShaderResultString(WebRendererShaderResult result) noexcept
{
    switch (result)
    {
    case WebRendererShaderResult::Success: return "success";
    case WebRendererShaderResult::InvalidDescriptor:
        return "invalid WebGL2 shader compatibility descriptor";
    case WebRendererShaderResult::UnsupportedSubstitution:
        return "WebGL2 shader substitution is not registry-owned";
    case WebRendererShaderResult::AllocationFailed:
        return "renderer shader recovery allocation failed";
    case WebRendererShaderResult::BackendFailure:
        return "WebGL2 shader compilation, link, or binding validation failed";
    }
    return "unknown renderer shader error";
}

WebRendererTextureResult WebRenderer_SetSkyImage(
    const GfxImage *canonical,
    std::uint8_t samplerState)
{
    if (!canonical)
    {
        if (g_renderer.initialized && !g_renderer.contextLost &&
            g_renderer.retainedSky.texture != 0u)
        {
            glDeleteTextures(1, &g_renderer.retainedSky.texture);
        }
        g_renderer.retainedSky = {};
        return WebRendererTextureResult::Success;
    }
    if (canonical->mapType != MAPTYPE_CUBE || !canonical->name ||
        !canonical->name[0] || std::strlen(canonical->name) > 240u)
    {
        return WebRendererTextureResult::InvalidDescriptor;
    }

    WebRendererRetainedSkyImage replacement;
    replacement.canonicalIdentity = canonical;
    replacement.canonicalName = canonical->name;
    replacement.samplerState = samplerState;
    WebDbImageLoadDef loadDef{};
    (void)DB_WebGetImageLoadDef(canonical, loadDef);
    kisak::iwi::Error decodeError = kisak::iwi::Error::None;
    bool attemptedDecode = false;
    try
    {
        attemptedDecode = DecodeCanonicalCubeImage(
            canonical, replacement.cube, decodeError);
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererTextureResult::AllocationFailed;
    }
    if (!attemptedDecode || decodeError != kisak::iwi::Error::None)
    {
        Web_Log(WebLogLevel::Error,
            "[kisakcod-web] Canonical sky '%s' could not cross the cube "
            "image boundary: %s (mapType=%d format=0x%08x flags=0x%02x "
            "dimensions=%dx%dx%d bytes=%zu).\n",
            replacement.canonicalName.c_str(), attemptedDecode
                ? kisak::iwi::ErrorString(decodeError)
                : "no canonical load definition or IWI member",
            static_cast<int>(canonical->mapType),
            static_cast<unsigned int>(loadDef.format),
            static_cast<unsigned int>(loadDef.flags),
            loadDef.dimensions[0], loadDef.dimensions[1],
            loadDef.dimensions[2], loadDef.byteLength);
        return WebRendererTextureResult::InvalidDescriptor;
    }
    replacement.active = true;
    if (g_renderer.initialized && !g_renderer.contextLost &&
        !CreateSkyTextureObject(replacement))
    {
        return WebRendererTextureResult::BackendFailure;
    }
    if (g_renderer.initialized && !g_renderer.contextLost &&
        g_renderer.retainedSky.texture != 0u)
    {
        glDeleteTextures(1, &g_renderer.retainedSky.texture);
    }
    g_renderer.retainedSky = std::move(replacement);
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Renderer retained canonical sky cubemap '%s' "
        "(%ux%u, six RGBA8 faces, %zu authored levels, sampler=0x%02x).\n",
        g_renderer.retainedSky.canonicalName.c_str(),
        g_renderer.retainedSky.cube.edgeLength,
        g_renderer.retainedSky.cube.edgeLength,
        1u + g_renderer.retainedSky.cube.mipFaces.size(),
        static_cast<unsigned int>(g_renderer.retainedSky.samplerState));
    return WebRendererTextureResult::Success;
}

WebRendererSurfaceResult WebRenderer_SetSurface(
    const WebRendererSurfaceDesc &surface,
    const WebRendererDrawDesc &draw)
{
    WebRendererOwnedSurface retainedSurface;
    const WebRendererSurfaceResult copyResult =
        WebRenderer_CopySurface(surface, draw, retainedSurface);
    if (copyResult != WebRendererSurfaceResult::Success)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Rejected engine surface: %s.\n",
            WebRenderer_SurfaceResultString(copyResult));
        return copyResult;
    }

    GLuint replacementVertexArray = 0;
    GLuint replacementVertexBuffer = 0;
    GLuint replacementIndexBuffer = 0;
    if (g_renderer.initialized && !g_renderer.contextLost &&
        !CreateSurfaceObjects(
            retainedSurface.vertices,
            retainedSurface.indices,
            replacementVertexArray,
            replacementVertexBuffer,
            replacementIndexBuffer))
    {
        return WebRendererSurfaceResult::BackendFailure;
    }

    if (replacementVertexArray != 0)
    {
        DeleteWorldTextureObjects(g_renderer.retainedWorldImages);
        DeleteWaterTextureObjects(g_renderer.retainedWorldBatches);
        DeleteSurfaceObjects(
            g_renderer.vertexArray,
            g_renderer.vertexBuffer,
            g_renderer.indexBuffer);
        g_renderer.vertexArray = replacementVertexArray;
        g_renderer.vertexBuffer = replacementVertexBuffer;
        g_renderer.indexBuffer = replacementIndexBuffer;
        ++g_renderer.surfaceResourceGeneration;
    }
    g_renderer.retainedVertices = std::move(retainedSurface.vertices);
    g_renderer.retainedIndices = std::move(retainedSurface.indices);
    g_renderer.retainedWorldIndices.clear();
    g_renderer.retainedWorldBatches.clear();
    g_renderer.retainedWorldSpotShadowCasters.clear();
    g_renderer.retainedWorldSpotShadowStaticModels.clear();
    g_renderer.retainedWorldImages.clear();
    g_renderer.retainedPrimaryLights.clear();
    g_renderer.retainedSunPrimaryLightIndex = 0u;
    g_renderer.draw = retainedSurface.draw;
    g_renderer.surfaceActive = true;
    g_renderer.worldSurfaceActive = false;
    ++g_renderer.surfaceSubmissionGeneration;

    const std::size_t retainedBytes =
        g_renderer.retainedVertices.size() * sizeof(WebRendererSurfaceVertex) +
        g_renderer.retainedIndices.size() * sizeof(std::uint16_t);
    Web_Log(
        WebLogLevel::Info,
        "[kisakcod-web] Renderer retained indexed surface (%u vertices, %u indices, %zu bytes).\n",
        surface.vertexCount,
        surface.indexCount,
        retainedBytes);
    EmitSurfaceLifecycle(
        g_renderer.initialized && !g_renderer.contextLost ? "ready" : "retained",
        g_renderer.initialized && !g_renderer.contextLost
            ? "The engine-owned indexed surface is resident in the graphics backend"
            : "The renderer retained the engine-owned surface for backend initialization");
    return WebRendererSurfaceResult::Success;
}

WebRendererSurfaceResult WebRenderer_SetWorldSurface(
    const WebRendererWorldSurfaceDesc &surface)
{
    std::vector<WebRendererSurfaceVertex> retainedVertices;
    std::vector<std::uint32_t> retainedIndices;
    std::vector<WebRendererRetainedWorldBatch> retainedBatches;
    std::vector<WebRendererRetainedWorldImage> retainedImages;
    std::vector<WebRendererRetainedPrimaryLight> retainedPrimaryLights;
    std::vector<WebRendererRetainedSpotShadowCaster>
        retainedSpotShadowCasters;
    std::vector<WebRendererRetainedSpotShadowStaticModel>
        retainedSpotShadowStaticModels;
    std::uint32_t retainedSunPrimaryLightIndex = 0u;
    const WebRendererSurfaceResult copy = CopyWorldCommand(
        surface, retainedVertices, retainedIndices, retainedBatches,
        retainedImages, retainedPrimaryLights,
        retainedSunPrimaryLightIndex, 0u, &retainedSpotShadowCasters,
        &retainedSpotShadowStaticModels);
    if (copy != WebRendererSurfaceResult::Success) return copy;

    GLuint replacementVertexArray = 0;
    GLuint replacementVertexBuffer = 0;
    GLuint replacementIndexBuffer = 0;
    const bool hasContext = g_renderer.initialized && !g_renderer.contextLost;
    if (hasContext && !CreateSurfaceObjects(
        retainedVertices, retainedIndices, replacementVertexArray,
        replacementVertexBuffer, replacementIndexBuffer))
    {
        return WebRendererSurfaceResult::BackendFailure;
    }
    if (hasContext && !CreateWorldTextureObjects(retainedImages))
    {
        DeleteSurfaceObjects(replacementVertexArray, replacementVertexBuffer,
            replacementIndexBuffer);
        return WebRendererSurfaceResult::BackendFailure;
    }
    if (hasContext && !CreateWaterTextureObjects(retainedBatches))
    {
        DeleteWorldTextureObjects(retainedImages);
        DeleteSurfaceObjects(replacementVertexArray, replacementVertexBuffer,
            replacementIndexBuffer);
        return WebRendererSurfaceResult::BackendFailure;
    }
    if (replacementVertexArray != 0)
    {
        DeleteWorldTextureObjects(g_renderer.retainedWorldImages);
        DeleteWaterTextureObjects(g_renderer.retainedWorldBatches);
        DeleteSurfaceObjects(
            g_renderer.vertexArray,
            g_renderer.vertexBuffer,
            g_renderer.indexBuffer);
        g_renderer.vertexArray = replacementVertexArray;
        g_renderer.vertexBuffer = replacementVertexBuffer;
        g_renderer.indexBuffer = replacementIndexBuffer;
        ++g_renderer.surfaceResourceGeneration;
    }
    g_renderer.retainedVertices = std::move(retainedVertices);
    g_renderer.retainedWorldIndices = std::move(retainedIndices);
    g_renderer.retainedWorldBatches = std::move(retainedBatches);
    g_renderer.retainedWorldSpotShadowCasters =
        std::move(retainedSpotShadowCasters);
    g_renderer.retainedWorldSpotShadowStaticModels =
        std::move(retainedSpotShadowStaticModels);
    g_renderer.retainedWorldImages = std::move(retainedImages);
    g_renderer.retainedPrimaryLights = std::move(retainedPrimaryLights);
    g_renderer.retainedSunPrimaryLightIndex =
        retainedSunPrimaryLightIndex;
    AttachRetainedWorldReflectionTextures();
    g_renderer.retainedIndices.clear();
    g_renderer.draw = {
        WebRendererPrimitiveTopology::TriangleList,
        0u,
        surface.indexCount,
        WebRendererTextureBinding::None,
    };
    g_renderer.surfaceActive = true;
    g_renderer.worldSurfaceActive = true;
    iassert(g_renderer.surfaceActive && g_renderer.worldSurfaceActive);
    ++g_renderer.surfaceSubmissionGeneration;

    const std::size_t retainedBytes =
        g_renderer.retainedVertices.size() * sizeof(WebRendererSurfaceVertex) +
        g_renderer.retainedWorldIndices.size() * sizeof(std::uint32_t);
    const std::size_t supportedImages = static_cast<std::size_t>(std::count_if(
        g_renderer.retainedWorldImages.begin(),
        g_renderer.retainedWorldImages.end(),
        [](const WebRendererRetainedWorldImage &image) {
            return image.supported;
        }));
    const std::size_t lightmappedBatches = static_cast<std::size_t>(std::count_if(
        g_renderer.retainedWorldBatches.begin(),
        g_renderer.retainedWorldBatches.end(),
        [](const WebRendererRetainedWorldBatch &batch) {
            return WebRenderer_UsesSecondaryDirectionalLightmap(
                batch.technique);
        }));
    const std::size_t specularBatches = static_cast<std::size_t>(std::count_if(
        g_renderer.retainedWorldBatches.begin(),
        g_renderer.retainedWorldBatches.end(),
        [](const WebRendererRetainedWorldBatch &batch) {
            return WebRenderer_UsesWorldSpecularMap(batch.technique);
        }));
    const std::size_t baseTextureBatches = static_cast<std::size_t>(std::count_if(
        g_renderer.retainedWorldBatches.begin(),
        g_renderer.retainedWorldBatches.end(),
        [](const WebRendererRetainedWorldBatch &batch) {
            return batch.technique == WebRendererWorldTechnique::BaseTexture;
        }));
    std::size_t reflectionProbeCount = 0u;
    std::size_t reflectionProbeLevelCount = 0u;
    for (const WebRendererRetainedWorldBatch &batch :
         g_renderer.retainedWorldBatches)
    {
        if (batch.reflectionCube.edgeLength == 0u) continue;
        ++reflectionProbeCount;
        reflectionProbeLevelCount += 1u + batch.reflectionCube.mipFaces.size();
    }
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Renderer retained canonical material world command "
        "(%u vertices, %u indices, %u batches: %zu lightmapped, "
        "%zu SM3 specular, %zu base-only; "
        "%zu/%zu images, %zu reflection probes/%zu authored levels, "
        "%zu geometry bytes).\n",
        surface.vertexCount, surface.indexCount, surface.batchCount,
        lightmappedBatches, specularBatches, baseTextureBatches, supportedImages,
        g_renderer.retainedWorldImages.size(), reflectionProbeCount,
        reflectionProbeLevelCount, retainedBytes);
    const std::size_t retainedLocalLights = static_cast<std::size_t>(
        std::count_if(g_renderer.retainedPrimaryLights.begin(),
            g_renderer.retainedPrimaryLights.end(),
            [](const WebRendererRetainedPrimaryLight &light) {
                return light.type == 2u || light.type == 3u;
            }));
    const std::size_t retainedPrimaryLitBatches = static_cast<std::size_t>(
        std::count_if(g_renderer.retainedWorldBatches.begin(),
            g_renderer.retainedWorldBatches.end(),
            [](const WebRendererRetainedWorldBatch &batch) {
                return batch.primaryLightIndex != 0u &&
                    batch.primaryLightIndex !=
                        g_renderer.retainedSunPrimaryLightIndex;
            }));
    const std::size_t retainedNativeSpotBatches = static_cast<std::size_t>(
        std::count_if(g_renderer.retainedWorldBatches.begin(),
            g_renderer.retainedWorldBatches.end(),
            [](const WebRendererRetainedWorldBatch &batch) {
                if (batch.techniqueType != 10u ||
                    batch.pixelShaderName.rfind("lm_spot_", 0u) != 0u ||
                    batch.lightmapImageIndex == INVALID_WORLD_IMAGE ||
                    batch.secondaryLightmapImageIndex ==
                        INVALID_WORLD_IMAGE ||
                    batch.primaryLightIndex >=
                        g_renderer.retainedPrimaryLights.size())
                {
                    return false;
                }
                const WebRendererRetainedPrimaryLight &light =
                    g_renderer.retainedPrimaryLights[
                        batch.primaryLightIndex];
                return light.type == 2u && light.falloffScale > 0.0f;
            }));
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Renderer retained %zu canonical local primary lights "
        "for %zu native-identity world batches, including %zu translated "
        "lm_spot batches (sun=%u).\n",
        retainedLocalLights, retainedPrimaryLitBatches,
        retainedNativeSpotBatches,
        g_renderer.retainedSunPrimaryLightIndex);
    const std::size_t recoveryBytes = EmitRendererMemory("world-submitted");
    DispatchRendererLifecycle(
        "newWorldPublished", 0.0,
        g_renderer.contextGeneration, g_renderer.contextGeneration,
        static_cast<double>(recoveryBytes));
    return WebRendererSurfaceResult::Success;
}

// The canonical frontend calls this backend seam before retiring a GfxWorld.
// Drop the retained world command and GPU objects without touching context
// ownership; the next map publication re-submits them.
void WebRenderer_UnloadWorldResources()
{
    const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE contextBefore = g_renderer.context;
    const bool initializedBefore = g_renderer.initialized;
    const std::uint32_t contextGenerationBefore = g_renderer.contextGeneration;
    const std::size_t recoveryBytesBefore =
        EmitRendererMemory("world-unload-begin");
    DispatchRendererLifecycle(
        "worldUnloadBegin", 0.0,
        contextGenerationBefore, g_renderer.contextGeneration,
        static_cast<double>(recoveryBytesBefore));
    if (g_renderer.initialized && !g_renderer.contextLost)
    {
        DeleteWorldTextureObjects(g_renderer.retainedWorldImages);
        DeleteWaterTextureObjects(g_renderer.retainedWorldBatches);
        DeleteWorldTextureObjects(g_renderer.retainedStaticModelImages);
        DeleteWorldTextureObjects(g_renderer.retainedDynamicModelImages);
        DeleteWorldTextureObjects(g_renderer.retainedUiImages);
        if (g_renderer.retainedSky.texture != 0u)
            glDeleteTextures(1, &g_renderer.retainedSky.texture);
        DeleteSurfaceObjects(
            g_renderer.vertexArray, g_renderer.vertexBuffer,
            g_renderer.indexBuffer);
        DeleteStaticModelObjects(
            g_renderer.staticModelVertexArray,
            g_renderer.staticModelVertexBuffer,
            g_renderer.staticModelIndexBuffer,
            g_renderer.staticModelInstanceBuffer);
        DeleteSurfaceObjects(
            g_renderer.dynamicModelVertexArray,
            g_renderer.dynamicModelVertexBuffer,
            g_renderer.dynamicModelIndexBuffer);
        DeleteSurfaceObjects(
            g_renderer.uiVertexArray,
            g_renderer.uiVertexBuffer,
            g_renderer.uiIndexBuffer);
        DeleteModelLightingTexture(g_renderer.retainedStaticModelLighting);
        DeleteModelLightingTexture(g_renderer.retainedDynamicModelLighting);
        if (g_renderer.sunVisibilityQueries[0] != 0u ||
            g_renderer.sunVisibilityQueries[1] != 0u)
        {
            glDeleteQueries(2, g_renderer.sunVisibilityQueries);
        }
    }
    g_renderer.vertexArray = 0u;
    g_renderer.vertexBuffer = 0u;
    g_renderer.indexBuffer = 0u;
    g_renderer.staticModelVertexArray = 0u;
    g_renderer.staticModelVertexBuffer = 0u;
    g_renderer.staticModelIndexBuffer = 0u;
    g_renderer.staticModelInstanceBuffer = 0u;
    g_renderer.dynamicModelVertexArray = 0u;
    g_renderer.dynamicModelVertexBuffer = 0u;
    g_renderer.dynamicModelIndexBuffer = 0u;
    g_renderer.uiVertexArray = 0u;
    g_renderer.uiVertexBuffer = 0u;
    g_renderer.uiIndexBuffer = 0u;

    // These retained commands contain canonical asset identities and decoded
    // recovery pixels owned by the retiring zone. Release their allocations at
    // the native world-unload boundary. Keeping the previous static-XModel
    // image set until its replacement was ready made a map transition require
    // almost two full max-graphics texture sets at once; the second decode then
    // ran out of Wasm memory and prevented the new scene view from publishing.
    decltype(g_renderer.retainedVertices){}.swap(
        g_renderer.retainedVertices);
    decltype(g_renderer.retainedIndices){}.swap(
        g_renderer.retainedIndices);
    decltype(g_renderer.retainedWorldIndices){}.swap(
        g_renderer.retainedWorldIndices);
    decltype(g_renderer.retainedWorldBatches){}.swap(
        g_renderer.retainedWorldBatches);
    decltype(g_renderer.retainedWorldSpotShadowCasters){}.swap(
        g_renderer.retainedWorldSpotShadowCasters);
    decltype(g_renderer.retainedWorldSpotShadowStaticModels){}.swap(
        g_renderer.retainedWorldSpotShadowStaticModels);
    decltype(g_renderer.retainedWorldImages){}.swap(
        g_renderer.retainedWorldImages);
    decltype(g_renderer.retainedPrimaryLights){}.swap(
        g_renderer.retainedPrimaryLights);
    decltype(g_renderer.retainedStaticModelVertices){}.swap(
        g_renderer.retainedStaticModelVertices);
    decltype(g_renderer.retainedStaticModelIndices){}.swap(
        g_renderer.retainedStaticModelIndices);
    decltype(g_renderer.retainedStaticModelSourceInstances){}.swap(
        g_renderer.retainedStaticModelSourceInstances);
    decltype(g_renderer.retainedStaticModelInstances){}.swap(
        g_renderer.retainedStaticModelInstances);
    decltype(g_renderer.retainedStaticModelSelectedLods){}.swap(
        g_renderer.retainedStaticModelSelectedLods);
    decltype(g_renderer.retainedStaticModelBatches){}.swap(
        g_renderer.retainedStaticModelBatches);
    decltype(g_renderer.retainedStaticModelImages){}.swap(
        g_renderer.retainedStaticModelImages);
    decltype(g_renderer.retainedDynamicModelVertices){}.swap(
        g_renderer.retainedDynamicModelVertices);
    decltype(g_renderer.retainedDynamicModelIndices){}.swap(
        g_renderer.retainedDynamicModelIndices);
    decltype(g_renderer.retainedDynamicModelBatches){}.swap(
        g_renderer.retainedDynamicModelBatches);
    decltype(g_renderer.retainedDynamicModelImages){}.swap(
        g_renderer.retainedDynamicModelImages);
    decltype(g_renderer.retainedUiVertices){}.swap(
        g_renderer.retainedUiVertices);
    decltype(g_renderer.retainedUiIndices){}.swap(
        g_renderer.retainedUiIndices);
    decltype(g_renderer.retainedUiBatches){}.swap(
        g_renderer.retainedUiBatches);
    decltype(g_renderer.retainedUiImages){}.swap(
        g_renderer.retainedUiImages);
    g_renderer.retainedSunPrimaryLightIndex = 0u;
    g_renderer.retainedSky = {};
    g_renderer.retainedStaticModelLighting = {};
    g_renderer.retainedDynamicModelLighting = {};
    g_renderer.staticModelCount = 0u;
    g_renderer.staticModelSurfaceCount = 0u;
    g_renderer.staticModelSceneActive = false;
    g_renderer.dynamicModelSceneActive = false;
    g_renderer.dynamicModelFirstSubmissionReported = false;
    std::fill_n(g_renderer.dynamicFxSourceReported, 4u, false);
    g_renderer.uiSceneActive = false;
    g_renderer.surfaceActive = false;
    g_renderer.worldSurfaceActive = false;
    g_renderer.sceneViewActive = false;
    g_renderer.sceneViewGeometrySubmitted = false;
    g_renderer.sceneViewWorldName.clear();
    g_renderer.sceneViewSurfaceCount = 0u;
    g_renderer.sceneViewVertexCount = 0u;
    g_renderer.sceneViewIndexCount = 0u;
    g_renderer.sceneViewSurfaceSubmissionGeneration = 0u;
    g_renderer.sceneViewDrawnSubmissionGeneration = 0u;
    g_renderer.sceneViewFirstDrawCompleted = false;
    g_renderer.sceneViewWaitReported = false;
    g_renderer.sceneViewX = 0u;
    g_renderer.sceneViewY = 0u;
    g_renderer.sceneViewWidth = 0u;
    g_renderer.sceneViewHeight = 0u;
    g_renderer.sceneTanHalfFov = {};
    g_renderer.sceneViewAxis = {};
    g_renderer.sceneViewOrigin = {};
    g_renderer.sceneFogColor = {};
    g_renderer.sceneFogParams = {};
    g_renderer.sceneFogEnabled = false;
    g_renderer.sceneColorBias = {};
    g_renderer.sceneColorTintBase = {};
    g_renderer.sceneColorTintDelta = {};
    g_renderer.sceneDisplayGammaExponent = 1.0f;
    g_renderer.sceneBlurRadius = 0.0f;
    g_renderer.sceneGlowBloomCutoff = 0.0f;
    g_renderer.sceneGlowBloomCutoffRescale = 0.0f;
    g_renderer.sceneGlowBloomDesaturation = 0.0f;
    g_renderer.sceneGlowBloomIntensity = 0.0f;
    g_renderer.sceneGlowRadius = 0.0f;
    g_renderer.sceneDepthOfField = {};
    g_renderer.sceneZNear = 4.0f;
    g_renderer.sceneDepthHackZNear = 0.1f;
    g_renderer.sceneFilmEnabled = false;
    g_renderer.sceneGlowEnabled = false;
    g_renderer.sceneSunShadowEnabled = false;
    std::fill_n(g_renderer.sunVisibilityQueries, 2u, 0u);
    std::fill_n(g_renderer.sunVisibilityQueryIssued, 2u, false);
    g_renderer.sunVisibilityQueryIndex = 0u;
    g_renderer.sunVisibility = 0.0f;
    g_renderer.sunFlareIntensity = 0.0f;
    g_renderer.sunBlindIntensity = 0.0f;
    g_renderer.sunBlindDarken = 0.0f;
    g_renderer.sunGlareIntensity = 0.0f;
    g_renderer.sunGlareLighten = 0.0f;
    g_renderer.sunEffectLastMilliseconds = 0.0;
    g_renderer.sunFlareFadeInMilliseconds = 0.0f;
    g_renderer.sunFlareFadeOutMilliseconds = 0.0f;
    g_renderer.sunBlindLerp = 0.0f;
    g_renderer.sunBlindMaxDarken = 0.0f;
    g_renderer.sunBlindFadeInMilliseconds = 0.0f;
    g_renderer.sunBlindFadeOutMilliseconds = 0.0f;
    g_renderer.sunGlareLerp = 0.0f;
    g_renderer.sunGlareMaxLighten = 0.0f;
    g_renderer.sunGlareFadeInMilliseconds = 0.0f;
    g_renderer.sunGlareFadeOutMilliseconds = 0.0f;
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Renderer released world-owned retained commands "
        "before canonical zone retirement.\n");
    iassert(!g_renderer.surfaceActive && !g_renderer.worldSurfaceActive &&
        g_renderer.retainedWorldImages.empty());
    iassert(g_renderer.context == contextBefore &&
        g_renderer.initialized == initializedBefore);
    const std::size_t recoveryBytesAfter = EmitRendererMemory("world-unloaded");
    const std::size_t oldMapBytesReleased = recoveryBytesBefore > recoveryBytesAfter
        ? recoveryBytesBefore - recoveryBytesAfter
        : 0u;
    DispatchRendererLifecycle(
        "worldUnloadEnd", static_cast<double>(oldMapBytesReleased),
        contextGenerationBefore, g_renderer.contextGeneration,
        static_cast<double>(recoveryBytesAfter));
}

WebRendererSurfaceResult WebRenderer_SetStaticModelScene(
    const WebRendererStaticModelSceneDesc &scene)
{
    std::vector<WebRendererSurfaceVertex> retainedVertices;
    std::vector<std::uint32_t> retainedIndices;
    std::vector<WebRendererStaticModelInstanceDesc> retainedInstances;
    std::vector<WebRendererRetainedStaticModelBatch> retainedBatches;
    std::vector<WebRendererRetainedWorldImage> retainedImages;
    WebRendererRetainedModelLightingAtlas retainedLighting;
    const WebRendererSurfaceResult copy = CopyStaticModelCommand(
        scene,
        retainedVertices,
        retainedIndices,
        retainedInstances,
        retainedBatches,
        retainedImages);
    if (copy != WebRendererSurfaceResult::Success) return copy;
    std::vector<WebRendererStaticModelInstanceDesc> sourceInstances;
    std::vector<std::int8_t> selectedLods;
    try
    {
        sourceInstances = retainedInstances;
        selectedLods.assign(retainedInstances.size(), -2);
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererSurfaceResult::AllocationFailed;
    }
    if (!CopyModelLightingAtlas(
            scene.modelLightingAtlas, retainedLighting))
        return WebRendererSurfaceResult::InvalidDescriptor;
    const bool staticNeedsLighting = std::any_of(
        retainedBatches.begin(), retainedBatches.end(),
        [](const WebRendererRetainedStaticModelBatch &batch) {
            return batch.draw.lightingMode ==
                WebRendererWorldLightingMode::ModelLightGrid;
        });
    if (staticNeedsLighting && retainedLighting.pixels.empty())
        return WebRendererSurfaceResult::InvalidDescriptor;

    GLuint vertexArray = 0u;
    GLuint vertexBuffer = 0u;
    GLuint indexBuffer = 0u;
    GLuint instanceBuffer = 0u;
    const bool hasContext = g_renderer.initialized && !g_renderer.contextLost;
    if (hasContext && !CreateStaticModelObjects(
        retainedVertices,
        retainedIndices,
        retainedInstances,
        vertexArray,
        vertexBuffer,
        indexBuffer,
        instanceBuffer))
    {
        return WebRendererSurfaceResult::BackendFailure;
    }
    if (hasContext && !CreateWorldTextureObjects(retainedImages))
    {
        DeleteStaticModelObjects(
            vertexArray, vertexBuffer, indexBuffer, instanceBuffer);
        return WebRendererSurfaceResult::BackendFailure;
    }
    if (hasContext && !CreateModelLightingTexture(retainedLighting))
    {
        DeleteWorldTextureObjects(retainedImages);
        DeleteStaticModelObjects(
            vertexArray, vertexBuffer, indexBuffer, instanceBuffer);
        return WebRendererSurfaceResult::BackendFailure;
    }
    if (hasContext)
    {
        DeleteWorldTextureObjects(g_renderer.retainedStaticModelImages);
        DeleteStaticModelObjects(
            g_renderer.staticModelVertexArray,
            g_renderer.staticModelVertexBuffer,
            g_renderer.staticModelIndexBuffer,
            g_renderer.staticModelInstanceBuffer);
        DeleteModelLightingTexture(
            g_renderer.retainedStaticModelLighting);
        g_renderer.staticModelVertexArray = vertexArray;
        g_renderer.staticModelVertexBuffer = vertexBuffer;
        g_renderer.staticModelIndexBuffer = indexBuffer;
        g_renderer.staticModelInstanceBuffer = instanceBuffer;
    }
    g_renderer.retainedStaticModelVertices = std::move(retainedVertices);
    g_renderer.retainedStaticModelIndices = std::move(retainedIndices);
    g_renderer.retainedStaticModelSourceInstances =
        std::move(sourceInstances);
    g_renderer.retainedStaticModelInstances = std::move(retainedInstances);
    g_renderer.retainedStaticModelSelectedLods = std::move(selectedLods);
    g_renderer.retainedStaticModelBatches = std::move(retainedBatches);
    g_renderer.retainedStaticModelImages = std::move(retainedImages);
    g_renderer.retainedStaticModelLighting = std::move(retainedLighting);
    g_renderer.staticModelCount = scene.modelCount;
    g_renderer.staticModelSurfaceCount = scene.surfaceCount;
    g_renderer.staticModelSceneActive = true;

    const std::size_t supportedImages = static_cast<std::size_t>(std::count_if(
        g_renderer.retainedStaticModelImages.begin(),
        g_renderer.retainedStaticModelImages.end(),
        [](const WebRendererRetainedWorldImage &image) {
            return image.supported;
        }));
    const auto isFallbackBatch =
        [&](const WebRendererRetainedStaticModelBatch &batch) {
            return batch.draw.technique ==
                    WebRendererWorldTechnique::BackendFallback ||
                batch.draw.baseImageIndex == INVALID_WORLD_IMAGE ||
                batch.draw.baseImageIndex >=
                    g_renderer.retainedStaticModelImages.size() ||
                !g_renderer.retainedStaticModelImages[
                    batch.draw.baseImageIndex].supported;
        };
    const std::size_t fallbackBatches = static_cast<std::size_t>(std::count_if(
        g_renderer.retainedStaticModelBatches.begin(),
        g_renderer.retainedStaticModelBatches.end(), isFallbackBatch));
    const std::size_t shadowCasterBatches = static_cast<std::size_t>(
        std::count_if(
            g_renderer.retainedStaticModelBatches.begin(),
            g_renderer.retainedStaticModelBatches.end(),
            [](const WebRendererRetainedStaticModelBatch &batch) {
                return batch.draw.castsSunShadow;
            }));
    const std::size_t specularBatches = static_cast<std::size_t>(
        std::count_if(g_renderer.retainedStaticModelBatches.begin(),
            g_renderer.retainedStaticModelBatches.end(),
            [](const WebRendererRetainedStaticModelBatch &batch) {
                return WebRenderer_UsesModelEnvironmentSpecular(
                    batch.draw.technique);
            }));
    const auto firstFallback = std::find_if(
        g_renderer.retainedStaticModelBatches.begin(),
        g_renderer.retainedStaticModelBatches.end(), isFallbackBatch);
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Renderer retained canonical static XModel command "
        "(%u models, %u surfaces, %u shared vertices, %u indices, %u "
        "instances, %u batches, %zu SM3 specular, %zu fallback, %zu "
        "shadow-caster; "
        "%zu/%zu images; "
        "model-lighting=%ux%ux%u entries=%u).\n",
        scene.modelCount,
        scene.surfaceCount,
        scene.vertexCount,
        scene.indexCount,
        scene.instanceCount,
        scene.batchCount,
        specularBatches,
        fallbackBatches,
        shadowCasterBatches,
        supportedImages,
        g_renderer.retainedStaticModelImages.size(),
        g_renderer.retainedStaticModelLighting.width,
        g_renderer.retainedStaticModelLighting.height,
        g_renderer.retainedStaticModelLighting.depth,
        g_renderer.retainedStaticModelLighting.entryCount);
    if (firstFallback != g_renderer.retainedStaticModelBatches.end())
    {
        const WebRendererRetainedWorldBatch &draw = firstFallback->draw;
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] First canonical static XModel fallback: "
            "model='%s' material='%s' technique='%s' imageIndex=%u.\n",
            draw.modelName.c_str(), draw.materialName.c_str(),
            draw.techniqueName.c_str(), draw.baseImageIndex);
    }
    EmitRendererMemory("static-models-submitted");
    return WebRendererSurfaceResult::Success;
}

WebRendererSurfaceResult WebRenderer_SetDynamicModelScene(
    const WebRendererWorldSurfaceDesc &scene)
{
    if (scene.vertexCount == 0u && scene.indexCount == 0u &&
        scene.batchCount == 0u)
    {
        if (scene.vertices || scene.indices || scene.batches ||
            scene.modelLightingAtlas)
            return WebRendererSurfaceResult::InvalidDescriptor;
        if (g_renderer.initialized && !g_renderer.contextLost)
            DeleteModelLightingTexture(
                g_renderer.retainedDynamicModelLighting);
        g_renderer.retainedDynamicModelLighting = {};
        g_renderer.dynamicModelSceneActive = false;
        return WebRendererSurfaceResult::Success;
    }
    if (scene.vertexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_VERTICES ||
        scene.indexCount > WEB_RENDERER_MAX_DYNAMIC_MODEL_INDICES)
    {
        return WebRendererSurfaceResult::InvalidDescriptor;
    }

    std::vector<WebRendererSurfaceVertex> retainedVertices;
    std::vector<std::uint32_t> retainedIndices;
    std::vector<WebRendererRetainedWorldBatch> retainedBatches;
    std::vector<WebRendererRetainedPrimaryLight> ignoredPrimaryLights;
    std::uint32_t ignoredSunPrimaryLightIndex = 0u;
    WebRendererRetainedModelLightingAtlas retainedLighting;
    // Dynamic image ownership is persistent across frames. CopyWorldCommand
    // finds canonical identities already present here, so an animated weapon
    // uploads geometry each frame without re-reading or re-decoding its IWI.
    const WebRendererSurfaceResult copy = CopyWorldCommand(
        scene, retainedVertices, retainedIndices, retainedBatches,
        g_renderer.retainedDynamicModelImages, ignoredPrimaryLights,
        ignoredSunPrimaryLightIndex,
        static_cast<std::uint32_t>(
            g_renderer.retainedPrimaryLights.size()), nullptr, nullptr);
    if (copy != WebRendererSurfaceResult::Success) return copy;
    if (!CopyModelLightingAtlas(
            scene.modelLightingAtlas, retainedLighting))
        return WebRendererSurfaceResult::InvalidDescriptor;
    const bool dynamicNeedsLighting = std::any_of(
        retainedBatches.begin(), retainedBatches.end(),
        [](const WebRendererRetainedWorldBatch &batch) {
            return batch.lightingMode ==
                WebRendererWorldLightingMode::ModelLightGrid;
        });
    if (dynamicNeedsLighting && retainedLighting.pixels.empty())
        return WebRendererSurfaceResult::InvalidDescriptor;

    GLuint vertexArray = 0u;
    GLuint vertexBuffer = 0u;
    GLuint indexBuffer = 0u;
    const bool hasContext = g_renderer.initialized && !g_renderer.contextLost;
    if (hasContext && !CreateSurfaceObjects(
        retainedVertices, retainedIndices,
        vertexArray, vertexBuffer, indexBuffer))
    {
        return WebRendererSurfaceResult::BackendFailure;
    }
    if (hasContext && !CreateWorldTextureObjects(
        g_renderer.retainedDynamicModelImages))
    {
        DeleteSurfaceObjects(vertexArray, vertexBuffer, indexBuffer);
        return WebRendererSurfaceResult::BackendFailure;
    }
    if (hasContext && !CreateModelLightingTexture(retainedLighting))
    {
        DeleteSurfaceObjects(vertexArray, vertexBuffer, indexBuffer);
        return WebRendererSurfaceResult::BackendFailure;
    }
    if (hasContext)
    {
        DeleteSurfaceObjects(
            g_renderer.dynamicModelVertexArray,
            g_renderer.dynamicModelVertexBuffer,
            g_renderer.dynamicModelIndexBuffer);
        DeleteModelLightingTexture(
            g_renderer.retainedDynamicModelLighting);
        g_renderer.dynamicModelVertexArray = vertexArray;
        g_renderer.dynamicModelVertexBuffer = vertexBuffer;
        g_renderer.dynamicModelIndexBuffer = indexBuffer;
    }
    g_renderer.retainedDynamicModelVertices = std::move(retainedVertices);
    g_renderer.retainedDynamicModelIndices = std::move(retainedIndices);
    g_renderer.retainedDynamicModelBatches = std::move(retainedBatches);
    g_renderer.retainedDynamicModelLighting = std::move(retainedLighting);
    g_renderer.dynamicModelSceneActive = true;

    if (!g_renderer.dynamicModelFirstSubmissionReported)
    {
        g_renderer.dynamicModelFirstSubmissionReported = true;
        const std::size_t supportedImages =
            static_cast<std::size_t>(std::count_if(
                g_renderer.retainedDynamicModelImages.begin(),
                g_renderer.retainedDynamicModelImages.end(),
                [](const WebRendererRetainedWorldImage &image) {
                    return image.supported;
                }));
        const std::size_t shadowCasterBatches = static_cast<std::size_t>(
            std::count_if(
                g_renderer.retainedDynamicModelBatches.begin(),
                g_renderer.retainedDynamicModelBatches.end(),
                [](const WebRendererRetainedWorldBatch &batch) {
                    return batch.castsSunShadow && !batch.depthHack &&
                        !WebRenderer_IsFxVertexColorBatch(batch.sourceKind);
                }));
        const std::size_t specularBatches = static_cast<std::size_t>(
            std::count_if(g_renderer.retainedDynamicModelBatches.begin(),
                g_renderer.retainedDynamicModelBatches.end(),
                [](const WebRendererRetainedWorldBatch &batch) {
                    return WebRenderer_UsesModelEnvironmentSpecular(
                        batch.technique);
                }));
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Renderer retained first canonical dynamic DObj "
            "command (%u vertices, %u indices, %u batches, %zu SM3 "
            "specular, %zu shadow-caster; %zu/%zu images; "
            "model-lighting=%ux%ux%u entries=%u).\n",
            scene.vertexCount, scene.indexCount, scene.batchCount,
            specularBatches,
            shadowCasterBatches,
            supportedImages,
            g_renderer.retainedDynamicModelImages.size(),
            g_renderer.retainedDynamicModelLighting.width,
            g_renderer.retainedDynamicModelLighting.height,
            g_renderer.retainedDynamicModelLighting.depth,
            g_renderer.retainedDynamicModelLighting.entryCount);
    }
#if KISAK_WEB_DIAGNOSTICS
    ReportRetainedDynamicFx();
#endif
    return WebRendererSurfaceResult::Success;
}

WebRendererSurfaceResult WebRenderer_SetUiScene(
    const WebRendererUiSceneDesc &scene)
{
    if (scene.vertexCount == 0u && scene.indexCount == 0u &&
        scene.batchCount == 0u)
    {
        if (scene.vertices || scene.indices || scene.batches)
            return WebRendererSurfaceResult::InvalidDescriptor;
        g_renderer.uiSceneActive = false;
        return WebRendererSurfaceResult::Success;
    }
    if (!scene.vertices || !scene.indices || !scene.batches ||
        scene.vertexCount > WEB_RENDERER_MAX_UI_VERTICES ||
        scene.indexCount > WEB_RENDERER_MAX_UI_INDICES)
    {
        return WebRendererSurfaceResult::InvalidDescriptor;
    }
    for (std::uint32_t vertexIndex = 0u;
         vertexIndex < scene.vertexCount; ++vertexIndex)
    {
        const WebRendererSurfaceVertex &vertex = scene.vertices[vertexIndex];
        for (const float component : vertex.position)
            if (!std::isfinite(component))
                return WebRendererSurfaceResult::NonFiniteVertex;
        for (const float component : vertex.textureCoordinate)
            if (!std::isfinite(component))
                return WebRendererSurfaceResult::NonFiniteVertex;
    }
    for (std::uint32_t index = 0u; index < scene.indexCount; ++index)
        if (scene.indices[index] >= scene.vertexCount)
            return WebRendererSurfaceResult::IndexOutOfRange;

    std::vector<WebRendererSurfaceVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<WebRendererRetainedUiBatch> batches;
    std::size_t retainedPixelBytes =
        RetainedImageStats(g_renderer.retainedUiImages).decodedBytes;
    try
    {
        vertices.assign(scene.vertices, scene.vertices + scene.vertexCount);
        indices.assign(scene.indices, scene.indices + scene.indexCount);
        batches.reserve(scene.batchCount);
        std::uint32_t expectedFirstIndex = 0u;
        for (std::uint32_t index = 0u; index < scene.batchCount; ++index)
        {
            const WebRendererUiBatchDesc &source = scene.batches[index];
            if (source.indexCount == 0u ||
                source.firstIndex != expectedFirstIndex ||
                source.firstIndex > scene.indexCount ||
                source.indexCount > scene.indexCount - source.firstIndex)
            {
                return WebRendererSurfaceResult::InvalidDescriptor;
            }
            WebRendererRetainedUiBatch batch;
            batch.firstIndex = source.firstIndex;
            batch.indexCount = source.indexCount;
            batch.materialIdentity = source.materialIdentity;
            batch.materialName = source.materialName
                ? source.materialName : "<null-ui-material>";
            batch.imageIndex = RetainCanonicalWorldImage(source.image,
                g_renderer.retainedUiImages, retainedPixelBytes);
            batch.samplerState = source.samplerState;
            batch.hasMaterialState = source.hasMaterialState;
            batch.stateBits[0] = source.stateBits[0];
            batch.stateBits[1] = source.stateBits[1];
            for (std::size_t component = 0u; component < 4u; ++component)
            {
                if (!std::isfinite(source.color[component]))
                    return WebRendererSurfaceResult::NonFiniteVertex;
                batch.color[component] = source.color[component];
            }
            batches.push_back(std::move(batch));
            expectedFirstIndex += source.indexCount;
        }
        if (expectedFirstIndex != scene.indexCount)
            return WebRendererSurfaceResult::InvalidDescriptor;
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererSurfaceResult::AllocationFailed;
    }

    GLuint vertexArray = 0u;
    GLuint vertexBuffer = 0u;
    GLuint indexBuffer = 0u;
    const bool hasContext = g_renderer.initialized && !g_renderer.contextLost;
    if (hasContext && !CreateSurfaceObjects(vertices, indices,
        vertexArray, vertexBuffer, indexBuffer))
        return WebRendererSurfaceResult::BackendFailure;
    if (hasContext && !CreateWorldTextureObjects(g_renderer.retainedUiImages))
    {
        DeleteSurfaceObjects(vertexArray, vertexBuffer, indexBuffer);
        return WebRendererSurfaceResult::BackendFailure;
    }
    if (hasContext)
    {
        DeleteSurfaceObjects(g_renderer.uiVertexArray,
            g_renderer.uiVertexBuffer, g_renderer.uiIndexBuffer);
        g_renderer.uiVertexArray = vertexArray;
        g_renderer.uiVertexBuffer = vertexBuffer;
        g_renderer.uiIndexBuffer = indexBuffer;
    }
    g_renderer.retainedUiVertices = std::move(vertices);
    g_renderer.retainedUiIndices = std::move(indices);
    g_renderer.retainedUiBatches = std::move(batches);
    g_renderer.uiSceneActive = true;
    EmitRendererMemory("ui-submitted", false);
    return WebRendererSurfaceResult::Success;
}

bool WebRenderer_Initialize()
{
    if (g_renderer.initialized)
    {
        return true;
    }

    // The bootstrap surface is rendered before canonical R_BeginRegistration
    // is reached. Register the same archived/latched renderer dvars here so
    // r_aaSamples owns both bootstrap and gameplay from the first frame.
    R_RegisterDvars();

    if (!CreateWebGLContext())
    {
        DestroyWebGLContext();
        Web_EmitRuntimeState("failed", "This browser could not create a WebGL2 context");
        return false;
    }
    Web_Log(
        WebLogLevel::Info,
        "[kisakcod-web] Renderer: %s\n",
        reinterpret_cast<const char *>(glGetString(GL_VERSION)));

    if (!CreateRendererResources())
    {
        EmitSurfaceLifecycle(
            "failed",
            "The graphics backend could not create the retained indexed surface");
        DestroyWebGLContext();
        Web_EmitRuntimeState(
            "failed",
            "The WebGL2 pipeline or retained renderer resources could not be created");
        return false;
    }

    ++g_renderer.contextGeneration;
    g_renderer.initialized = true;
    iassert(g_renderer.context > 0 && !g_renderer.contextLost);
    if (g_renderer.surfaceActive)
    {
        EmitSurfaceLifecycle(
            "ready",
            "The engine-owned indexed surface is resident in the graphics backend");
    }
    EmitTextureLifecycle(
        "ready",
        "The renderer uploaded its retained texture during initialization");
    if (g_renderer.compatibilityActive)
    {
        EmitShaderLifecycle(
            "ready",
            "The renderer compiled the retained WebGL2 shader contract during initialization");
    }
    EmitRendererMemory("initialized");
    return true;
}

void WebRenderer_Shutdown()
{
    WebRenderer_UnloadWorldResources();
    DestroyWebGLContext();
    iassert(g_renderer.context == 0 && !g_renderer.initialized);
    EmitRendererMemory("shutdown");
}

WebRendererTextureResult WebRenderer_SetBootstrapTexture(
    const WebRendererRgba8TextureDesc &texture)
{
    std::size_t expectedByteLength = 0;
    const WebRendererTextureResult validation =
        ValidateTextureDesc(texture, expectedByteLength);
    if (validation != WebRendererTextureResult::Success)
    {
        return validation;
    }

    std::vector<std::uint8_t> retainedCopy;
    try
    {
        retainedCopy.assign(texture.pixels, texture.pixels + expectedByteLength);
    }
    catch (const std::bad_alloc &)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Renderer could not retain the bounded RGBA8 texture.\n");
        return WebRendererTextureResult::AllocationFailed;
    }

    GLuint replacementTexture = 0;
    if (g_renderer.initialized && !g_renderer.contextLost)
    {
        if (!CreateTextureObject(
                retainedCopy.data(), texture.width, texture.height,
                texture.samplerState, replacementTexture))
        {
            return WebRendererTextureResult::BackendFailure;
        }
    }

    if (replacementTexture != 0)
    {
        glDeleteTextures(1, &g_renderer.texture);
        g_renderer.texture = replacementTexture;
        ++g_renderer.rebuildGeneration;
    }
    g_renderer.retainedPixels = std::move(retainedCopy);
    g_renderer.textureWidth = texture.width;
    g_renderer.textureHeight = texture.height;
    g_renderer.textureSamplerState = texture.samplerState;
    g_renderer.sourceTextureActive = true;
    ++g_renderer.uploadGeneration;

    Web_Log(
        WebLogLevel::Info,
        "[kisakcod-web] Renderer retained RGBA8 texture %ux%u (%zu bytes).\n",
        texture.width,
        texture.height,
        expectedByteLength);
    return WebRendererTextureResult::Success;
}

bool WebRenderer_ClearBootstrapTexture()
{
    if (!g_renderer.sourceTextureActive)
    {
        if (!g_renderer.initialized || g_renderer.contextLost || g_renderer.texture != 0)
        {
            return true;
        }
        GLuint fallbackTexture = 0;
        if (!CreateTextureObject(
                FALLBACK_TEXTURE_RGBA, 1u, 1u, 0u, fallbackTexture))
        {
            return false;
        }
        g_renderer.texture = fallbackTexture;
        ++g_renderer.rebuildGeneration;
        return true;
    }

    GLuint replacementTexture = 0;
    bool backendReady = true;
    if (g_renderer.initialized && !g_renderer.contextLost)
    {
        backendReady = CreateTextureObject(
            FALLBACK_TEXTURE_RGBA, 1u, 1u, 0u, replacementTexture);
    }

    if (replacementTexture != 0)
    {
        glDeleteTextures(1, &g_renderer.texture);
        g_renderer.texture = replacementTexture;
        ++g_renderer.rebuildGeneration;
    }
    else if (g_renderer.initialized && !g_renderer.contextLost)
    {
        // Even if the fallback upload failed, never leave an old imported GPU
        // object addressable after its source generation has been retired.
        glDeleteTextures(1, &g_renderer.texture);
        g_renderer.texture = 0;
    }
    std::vector<std::uint8_t>().swap(g_renderer.retainedPixels);
    g_renderer.textureWidth = 0u;
    g_renderer.textureHeight = 0u;
    g_renderer.textureSamplerState = 0u;
    g_renderer.sourceTextureActive = false;
    ++g_renderer.uploadGeneration;
    Web_Log(WebLogLevel::Info, "[kisakcod-web] Renderer released the imported texture.\n");
    return backendReady;
}

WebRendererTextureState WebRenderer_GetBootstrapTextureState()
{
    if (!g_renderer.sourceTextureActive)
    {
        return {
            0u,
            0u,
            0u,
            g_renderer.uploadGeneration,
            g_renderer.rebuildGeneration,
            g_renderer.recoveryCount,
            false,
            false,
        };
    }
    return {
        g_renderer.textureWidth,
        g_renderer.textureHeight,
        g_renderer.retainedPixels.size(),
        g_renderer.uploadGeneration,
        g_renderer.rebuildGeneration,
        g_renderer.recoveryCount,
        true,
        g_renderer.initialized && !g_renderer.contextLost && g_renderer.texture != 0,
    };
}

WebRendererShaderResult WebRenderer_SetShaderCompatibility(
    const kisak::web::WebGL2ShaderSubstitution &substitution)
{
    if (!substitution.id || !substitution.vertexSource ||
        !substitution.fragmentSource || substitution.vertexSourceHash == 0u ||
        substitution.fragmentSourceHash == 0u)
        return WebRendererShaderResult::InvalidDescriptor;

    kisak::web::WebGL2ShaderSubstitution registered;
    if (!kisak::web::LookupWebGL2ShaderSubstitution(substitution.id, registered) ||
        registered.vertexSourceHash != substitution.vertexSourceHash ||
        registered.fragmentSourceHash != substitution.fragmentSourceHash ||
        std::strcmp(registered.vertexSource, substitution.vertexSource) != 0 ||
        std::strcmp(registered.fragmentSource, substitution.fragmentSource) != 0)
        return WebRendererShaderResult::UnsupportedSubstitution;

    constexpr std::size_t MAX_SOURCE_BYTES = 16u * 1024u;
    const std::size_t vertexLength = std::strlen(substitution.vertexSource);
    const std::size_t fragmentLength = std::strlen(substitution.fragmentSource);
    if (vertexLength == 0u || fragmentLength == 0u ||
        vertexLength >= MAX_SOURCE_BYTES || fragmentLength >= MAX_SOURCE_BYTES)
        return WebRendererShaderResult::InvalidDescriptor;

    std::string retainedId;
    std::string retainedVertex;
    std::string retainedFragment;
    try
    {
        retainedId = substitution.id;
        retainedVertex = substitution.vertexSource;
        retainedFragment = substitution.fragmentSource;
    }
    catch (const std::bad_alloc &)
    {
        return WebRendererShaderResult::AllocationFailed;
    }

    GLuint replacementProgram = 0;
    GLint replacementViewProjection = -1;
    GLint replacementWorld = -1;
    GLint replacementTexture = -1;
    if (g_renderer.initialized && !g_renderer.contextLost &&
        !CreateCompatibilityProgram(
            retainedVertex.c_str(),
            retainedFragment.c_str(),
            replacementProgram,
            replacementViewProjection,
            replacementWorld,
            replacementTexture))
    {
        DispatchRendererShaderLifecycle(
            "failed",
            "The selected compatibility source did not compile, link, and bind atomically",
            substitution.id,
            substitution.vertexSourceHash,
            substitution.fragmentSourceHash,
            g_renderer.compatibilitySubmissionGeneration + 1u,
            g_renderer.compatibilityResourceGeneration,
            g_renderer.compatibilityRecoveryCount,
            0u,
            false,
            false,
            false);
        return WebRendererShaderResult::BackendFailure;
    }

    if (replacementProgram != 0)
    {
        glDeleteProgram(g_renderer.compatibilityProgram);
        g_renderer.compatibilityProgram = replacementProgram;
        g_renderer.compatibilityViewProjectionUniform = replacementViewProjection;
        g_renderer.compatibilityWorldUniform = replacementWorld;
        g_renderer.compatibilityTextureUniform = replacementTexture;
        ++g_renderer.compatibilityResourceGeneration;
    }
    g_renderer.compatibilityId = std::move(retainedId);
    g_renderer.compatibilityVertexSource = std::move(retainedVertex);
    g_renderer.compatibilityFragmentSource = std::move(retainedFragment);
    g_renderer.compatibilityVertexSourceHash = substitution.vertexSourceHash;
    g_renderer.compatibilityFragmentSourceHash = substitution.fragmentSourceHash;
    g_renderer.compatibilityActive = true;
    g_renderer.compatibilityDrawCount = 0u;
    g_renderer.compatibilityFirstDrawCompleted = false;
    ++g_renderer.compatibilitySubmissionGeneration;
    Web_Log(
        WebLogLevel::Info,
        "[kisakcod-web] Renderer selected WebGL2 shader compatibility program %s.\n",
        g_renderer.compatibilityId.c_str());
    EmitShaderLifecycle(
        replacementProgram != 0 ? "ready" : "retained",
        replacementProgram != 0
            ? "The selected WebGL2 shader program is resident with validated bindings"
            : "The selected WebGL2 shader contract is retained for context recovery");
    return WebRendererShaderResult::Success;
}

bool WebRenderer_ClearShaderCompatibility()
{
    if (!g_renderer.compatibilityActive) return true;
    if (g_renderer.initialized && !g_renderer.contextLost &&
        g_renderer.compatibilityProgram != 0)
        glDeleteProgram(g_renderer.compatibilityProgram);
    g_renderer.compatibilityProgram = 0;
    g_renderer.compatibilityViewProjectionUniform = -1;
    g_renderer.compatibilityWorldUniform = -1;
    g_renderer.compatibilityTextureUniform = -1;
    g_renderer.compatibilityActive = false;
    g_renderer.compatibilityDrawCount = 0u;
    g_renderer.compatibilityFirstDrawCompleted = false;
    ++g_renderer.compatibilitySubmissionGeneration;
    EmitShaderLifecycle(
        "cleared",
        "The imported shader generation was retired; drawing returned to the bootstrap pipeline");
    g_renderer.compatibilityId.clear();
    g_renderer.compatibilityVertexSource.clear();
    g_renderer.compatibilityFragmentSource.clear();
    g_renderer.compatibilityVertexSourceHash = 0u;
    g_renderer.compatibilityFragmentSourceHash = 0u;
    return true;
}

WebRendererShaderState WebRenderer_GetShaderCompatibilityState()
{
    return {
        g_renderer.compatibilityId.c_str(),
        g_renderer.compatibilityVertexSourceHash,
        g_renderer.compatibilityFragmentSourceHash,
        g_renderer.compatibilitySubmissionGeneration,
        g_renderer.compatibilityResourceGeneration,
        g_renderer.compatibilityRecoveryCount,
        g_renderer.compatibilityDrawCount,
        g_renderer.compatibilityActive,
        g_renderer.initialized && !g_renderer.contextLost &&
            g_renderer.compatibilityProgram != 0,
        g_renderer.compatibilityFirstDrawCompleted,
    };
}

namespace
{
bool BuildSunShadowMatrix(
    const WebRendererSceneViewDesc &view,
    float sampleSize,
    std::array<float, 16> &matrix) noexcept
{
    auto normalize = [](std::array<float, 3> value) {
        const float length = std::sqrt(value[0] * value[0] +
            value[1] * value[1] + value[2] * value[2]);
        if (!(length > 0.000001f) || !std::isfinite(length))
            return std::array<float, 3>{};
        for (float &component : value) component /= length;
        return value;
    };
    auto cross = [](const std::array<float, 3> &a,
                    const std::array<float, 3> &b) {
        return std::array<float, 3>{
            a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
    };
    auto dot = [](const float *a, const std::array<float, 3> &b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };

    const std::array<float, 3> forward = normalize({
        -view.sunDirection[0], -view.sunDirection[1],
        -view.sunDirection[2]});
    if (forward == std::array<float, 3>{}) return false;
    const float horizontal = forward[0] * forward[0] +
        forward[1] * forward[1];
    const std::array<float, 3> upSeed = horizontal >= 0.1f
        ? std::array<float, 3>{0.0f, 0.0f, 1.0f}
        : std::array<float, 3>{1.0f, 0.0f, 0.0f};
    const std::array<float, 3> right = normalize(cross(upSeed, forward));
    const std::array<float, 3> up = cross(forward, right);

    // Native uses 0.25 world units per texel for the near partition and
    // multiplies it by the default partition ratio of four for the far map.
    // Snap each viewer-centred projection to its own texel grid, matching
    // R_SetupSunShadowMapProjection's anti-crawling rule.
    if (!(sampleSize > 0.0f) || !std::isfinite(sampleSize)) return false;
    const float halfExtent = sampleSize * 1024.0f * 0.5f;
    const float centerRight = std::round(dot(view.viewOrigin, right) /
        sampleSize) * sampleSize;
    const float centerUp = std::round(dot(view.viewOrigin, up) /
        sampleSize) * sampleSize;
    float minDepth = std::numeric_limits<float>::max();
    float maxDepth = -std::numeric_limits<float>::max();
    for (std::uint32_t corner = 0u; corner < 8u; ++corner)
    {
        const float point[3] = {
            view.worldMins[0] +
                ((corner & 1u) ? view.worldMaxs[0] - view.worldMins[0] : 0.0f),
            view.worldMins[1] +
                ((corner & 2u) ? view.worldMaxs[1] - view.worldMins[1] : 0.0f),
            view.worldMins[2] +
                ((corner & 4u) ? view.worldMaxs[2] - view.worldMins[2] : 0.0f)};
        const float depth = dot(point, forward);
        minDepth = std::min(minDepth, depth);
        maxDepth = std::max(maxDepth, depth);
    }
    minDepth -= 1.0f;
    maxDepth += 1.0f;
    const float depthRange = maxDepth - minDepth;
    if (!(depthRange > 0.0f) || !std::isfinite(depthRange)) return false;

    matrix.fill(0.0f);
    matrix[0] = right[0] / halfExtent;
    matrix[4] = right[1] / halfExtent;
    matrix[8] = right[2] / halfExtent;
    matrix[12] = -centerRight / halfExtent;
    matrix[1] = up[0] / halfExtent;
    matrix[5] = up[1] / halfExtent;
    matrix[9] = up[2] / halfExtent;
    matrix[13] = -centerUp / halfExtent;
    matrix[2] = forward[0] * 2.0f / depthRange;
    matrix[6] = forward[1] * 2.0f / depthRange;
    matrix[10] = forward[2] * 2.0f / depthRange;
    matrix[14] = -1.0f - 2.0f * minDepth / depthRange;
    matrix[15] = 1.0f;
    return true;
}

bool BuildSpotShadowMatrix(
    const WebRendererRetainedPrimaryLight &light,
    std::array<float, 16> &matrix) noexcept
{
    auto normalize = [](std::array<float, 3> value) {
        const float length = std::sqrt(value[0] * value[0] +
            value[1] * value[1] + value[2] * value[2]);
        if (!(length > 0.000001f) || !std::isfinite(length))
            return std::array<float, 3>{};
        for (float &component : value) component /= length;
        return value;
    };
    auto cross = [](const std::array<float, 3> &a,
                    const std::array<float, 3> &b) {
        return std::array<float, 3>{
            a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
    };
    const std::array<float, 3> axis0 = normalize({
        -light.direction[0], -light.direction[1], -light.direction[2]});
    if (axis0 == std::array<float, 3>{} ||
        !(light.cosHalfFovOuter > 0.0f &&
          light.cosHalfFovOuter < 1.0f) || light.radius <= 1.0f)
        return false;
    std::size_t perpendicularComponent =
        axis0[0] * axis0[0] > axis0[1] * axis0[1] ? 1u : 0u;
    if (axis0[perpendicularComponent] * axis0[perpendicularComponent] >
        axis0[2] * axis0[2])
        perpendicularComponent = 2u;
    std::array<float, 3> axis2{
        -axis0[perpendicularComponent] * axis0[0],
        -axis0[perpendicularComponent] * axis0[1],
        -axis0[perpendicularComponent] * axis0[2]};
    axis2[perpendicularComponent] += 1.0f;
    axis2 = normalize(axis2);
    const std::array<float, 3> axis1 = cross(axis2, axis0);
    const float tangent = std::sqrt(std::max(
        0.0f, 1.0f - light.cosHalfFovOuter * light.cosHalfFovOuter)) /
        light.cosHalfFovOuter;
    if (!(tangent > 0.0f) || !std::isfinite(tangent)) return false;

    // Preserve the canonical MatrixForViewer axis convention. The row-major
    // Kisak matrix is uploaded unchanged; WebGL's column-major interpretation
    // supplies the transpose needed by GLSL column-vector multiplication.
    std::array<float, 16> view{};
    view[0] = -axis1[0]; view[4] = -axis1[1]; view[8] = -axis1[2];
    view[1] = axis2[0]; view[5] = axis2[1]; view[9] = axis2[2];
    view[2] = axis0[0]; view[6] = axis0[1]; view[10] = axis0[2];
    view[12] = -(light.origin[0] * view[0] +
        light.origin[1] * view[4] + light.origin[2] * view[8]);
    view[13] = -(light.origin[0] * view[1] +
        light.origin[1] * view[5] + light.origin[2] * view[9]);
    view[14] = -(light.origin[0] * view[2] +
        light.origin[1] * view[6] + light.origin[2] * view[10]);
    view[15] = 1.0f;

    constexpr float nearPlane = 1.0f;
    std::array<float, 16> projection{};
    projection[0] = 1.0f / tangent;
    projection[5] = 1.0f / tangent;
    // Native uses D3D's [0,1] finite perspective. Convert only its depth
    // coefficients to WebGL's [-1,1] clip range at this backend boundary.
    projection[10] = (light.radius + nearPlane) /
        (light.radius - nearPlane);
    projection[11] = 1.0f;
    projection[14] = (-2.0f * light.radius * nearPlane) /
        (light.radius - nearPlane);

    matrix.fill(0.0f);
    for (std::size_t column = 0u; column < 4u; ++column)
        for (std::size_t row = 0u; row < 4u; ++row)
            for (std::size_t inner = 0u; inner < 4u; ++inner)
                matrix[row * 4u + column] +=
                    view[row * 4u + inner] *
                    projection[inner * 4u + column];
    return std::all_of(matrix.begin(), matrix.end(),
        [](float value) { return std::isfinite(value); });
}

void SelectSpotShadowLights() noexcept
{
    struct Candidate { std::uint32_t lightIndex; float score; };
    std::array<bool, WEB_RENDERER_MAX_PRIMARY_LIGHTS> used{};
    for (const WebRendererRetainedWorldBatch &batch :
         g_renderer.retainedWorldBatches)
        if (batch.primaryLightIndex < used.size())
            used[batch.primaryLightIndex] = true;
    for (const WebRendererRetainedWorldBatch &batch :
         g_renderer.retainedDynamicModelBatches)
        if (batch.primaryLightIndex < used.size())
            used[batch.primaryLightIndex] = true;

    std::vector<Candidate> candidates;
    candidates.reserve(g_renderer.retainedPrimaryLights.size());
    const float eyeProjectDistance = sm_lightScore_eyeProjectDist
        ? sm_lightScore_eyeProjectDist->current.value : 64.0f;
    const float spotProjectFraction = sm_lightScore_spotProjectFrac
        ? sm_lightScore_spotProjectFrac->current.value : 0.125f;
    const float eyeReference[3]{
        g_renderer.sceneViewOrigin[0] + eyeProjectDistance *
            g_renderer.sceneViewAxis[0],
        g_renderer.sceneViewOrigin[1] + eyeProjectDistance *
            g_renderer.sceneViewAxis[1],
        g_renderer.sceneViewOrigin[2] + eyeProjectDistance *
            g_renderer.sceneViewAxis[2]};
    for (std::uint32_t index = 1u;
         index < g_renderer.retainedPrimaryLights.size() &&
             index < used.size(); ++index)
    {
        const WebRendererRetainedPrimaryLight &light =
            g_renderer.retainedPrimaryLights[index];
        if (!used[index] || light.type != 2u || !light.canUseShadowMap ||
            light.radius <= 1.0f ||
            (sm_spotEnable && !sm_spotEnable->current.enabled))
            continue;
        float focusDelta[3]{};
        for (std::size_t component = 0u; component < 3u; ++component)
            focusDelta[component] = light.origin[component] -
                eyeReference[component] - light.radius *
                spotProjectFraction * light.direction[component];
        const float distance = std::sqrt(focusDelta[0] * focusDelta[0] +
            focusDelta[1] * focusDelta[1] +
            focusDelta[2] * focusDelta[2]);
        const float intensity = light.color[0] * 0.2989f +
            light.color[1] * 0.587f + light.color[2] * 0.114f;
        candidates.push_back({index,
            light.radius * intensity / (distance + 1.0f)});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const Candidate &left, const Candidate &right) {
            return left.score > right.score;
        });
    const std::uint32_t configuredLimit = sm_maxLights
        ? static_cast<std::uint32_t>(std::clamp(
            sm_maxLights->current.integer, 0,
            static_cast<int>(MAX_SPOT_SHADOWS)))
        : static_cast<std::uint32_t>(MAX_SPOT_SHADOWS);
    g_renderer.sceneSpotShadowCount = 0u;
    for (const Candidate &candidate : candidates)
    {
        if (g_renderer.sceneSpotShadowCount >= configuredLimit) break;
        const std::size_t slot = g_renderer.sceneSpotShadowCount;
        if (!BuildSpotShadowMatrix(
                g_renderer.retainedPrimaryLights[candidate.lightIndex],
                g_renderer.sceneSpotShadowMatrices[slot]))
            continue;
        g_renderer.sceneSpotShadowLightIndices[slot] = candidate.lightIndex;
        ++g_renderer.sceneSpotShadowCount;
    }
    static std::uint32_t reportedCount = UINT32_MAX;
    if (reportedCount != g_renderer.sceneSpotShadowCount)
    {
        reportedCount = g_renderer.sceneSpotShadowCount;
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Selected %u/%zu eligible primary spot shadow "
            "maps (configured limit %u, indices=%u/%u/%u/%u).\n",
            g_renderer.sceneSpotShadowCount, candidates.size(),
            configuredLimit,
            g_renderer.sceneSpotShadowCount > 0u
                ? g_renderer.sceneSpotShadowLightIndices[0] : 0u,
            g_renderer.sceneSpotShadowCount > 1u
                ? g_renderer.sceneSpotShadowLightIndices[1] : 0u,
            g_renderer.sceneSpotShadowCount > 2u
                ? g_renderer.sceneSpotShadowLightIndices[2] : 0u,
            g_renderer.sceneSpotShadowCount > 3u
                ? g_renderer.sceneSpotShadowLightIndices[3] : 0u);
    }
}
} // namespace

bool WebRenderer_SubmitSceneView(const WebRendererSceneViewDesc &view)
{
    if (!view.worldName || !*view.worldName || view.width == 0u ||
        view.height == 0u || view.tanHalfFovX <= 0.0f ||
        view.tanHalfFovY <= 0.0f || view.localClientNum != 0)
    {
        return false;
    }

    const float *const axis = &view.viewAxis[0][0];
    for (const float component : view.viewOrigin)
    {
        if (!std::isfinite(component)) return false;
    }
    for (std::size_t index = 0; index < 9u; ++index)
    {
        if (!std::isfinite(axis[index])) return false;
    }
    const float *const viewProjection = &view.viewProjectionMatrix[0][0];
    const float *const depthHackViewProjection =
        &view.depthHackViewProjectionMatrix[0][0];
    for (std::size_t index = 0; index < 16u; ++index)
    {
        if (!std::isfinite(viewProjection[index]) ||
            !std::isfinite(depthHackViewProjection[index])) return false;
    }
    if (!std::isfinite(view.tanHalfFovX) ||
        !std::isfinite(view.tanHalfFovY) || !std::isfinite(view.zNear) ||
        view.zNear <= 0.0f)
    {
        return false;
    }
    if (view.fogEnabled)
    {
        if (!std::isfinite(view.fogStart) ||
            !std::isfinite(view.fogDensity) || view.fogDensity <= 0.0f)
        {
            return false;
        }
        for (const float component : view.fogColor)
        {
            if (!std::isfinite(component)) return false;
        }
    }
    if (!std::isfinite(view.displayGammaExponent) ||
        view.displayGammaExponent <= 0.0f ||
        !std::isfinite(view.blurRadius) || view.blurRadius < 0.0f ||
        !std::isfinite(view.depthHackZNear) || view.depthHackZNear <= 0.0f ||
        !WebRenderer_ValidateDepthOfFieldSettings(view.depthOfField))
    {
        return false;
    }
    if (!std::isfinite(view.glowBloomCutoff) ||
        !std::isfinite(view.glowBloomCutoffRescale) ||
        !std::isfinite(view.glowBloomDesaturation) ||
        !std::isfinite(view.glowBloomIntensity) ||
        !std::isfinite(view.glowRadius) ||
        view.glowBloomCutoff < 0.0f || view.glowBloomCutoff > 1.0f ||
        view.glowBloomCutoffRescale < 0.0f ||
        view.glowBloomDesaturation < 0.0f ||
        view.glowBloomDesaturation > 1.0f ||
        view.glowBloomIntensity < 0.0f || view.glowRadius < 0.0f ||
        (view.glowEnabled &&
            (view.glowBloomCutoff >= 1.0f ||
             view.glowBloomCutoffRescale <= 0.0f ||
             view.glowBloomIntensity <= 0.0f || view.glowRadius <= 0.0f)))
    {
        return false;
    }
    if (view.sunShadowEnabled)
    {
        for (std::size_t component = 0u; component < 3u; ++component)
        {
            if (!std::isfinite(view.sunDirection[component]) ||
                !std::isfinite(view.sunColor[component]) ||
                !std::isfinite(view.worldMins[component]) ||
                !std::isfinite(view.worldMaxs[component]) ||
                view.worldMins[component] > view.worldMaxs[component])
                return false;
        }
    }
    for (std::size_t channel = 0u; channel < 4u; ++channel)
    {
        if (!std::isfinite(view.colorBias[channel]) ||
            !std::isfinite(view.colorTintBase[channel]) ||
            !std::isfinite(view.colorTintDelta[channel]))
            return false;
    }
    if (view.geometrySubmitted &&
        (view.worldSurfaceCount == 0u || view.worldVertexCount == 0u ||
         view.worldIndexCount == 0u || !g_renderer.surfaceActive ||
         g_renderer.surfaceSubmissionGeneration == 0u))
    {
        return false;
    }
    if (!WebRenderer_ValidatePrimaryLightFrame(
            view.primaryLights, view.primaryLightCount) ||
        view.primaryLightCount != g_renderer.retainedPrimaryLights.size())
    {
        return false;
    }
    for (std::uint32_t index = 0u;
         index < view.primaryLightCount; ++index)
    {
        const WebRendererPrimaryLightDesc &source =
            view.primaryLights[index];
        WebRendererRetainedPrimaryLight &destination =
            g_renderer.retainedPrimaryLights[index];
        // Primary-light type and attenuation definition are authored world
        // identity. Cgame animates the other GfxLight fields, and native picks
        // the material technique from the same stable indexed light.
        if (source.type != destination.type) return false;
        destination.exponent = source.exponent;
        destination.canUseShadowMap = source.canUseShadowMap != 0u;
        destination.radius = source.radius;
        destination.cosHalfFovOuter = source.cosHalfFovOuter;
        destination.cosHalfFovInner = source.cosHalfFovInner;
        destination.falloffScale = source.falloffScale;
        destination.falloffShift = source.falloffShift;
        std::copy_n(source.color, 3u, destination.color);
        std::copy_n(source.direction, 3u, destination.direction);
        std::copy_n(source.origin, 3u, destination.origin);
    }

    const bool worldChanged = g_renderer.sceneViewWorldName != view.worldName;
    ++g_renderer.sceneViewSubmissionGeneration;
    g_renderer.sceneViewActive = true;
    g_renderer.sceneViewGeometrySubmitted = view.geometrySubmitted;
    g_renderer.sceneViewSurfaceCount = view.worldSurfaceCount;
    g_renderer.sceneViewVertexCount = view.worldVertexCount;
    g_renderer.sceneViewIndexCount = view.worldIndexCount;
    g_renderer.sceneViewX = view.x;
    g_renderer.sceneViewY = view.y;
    g_renderer.sceneViewWidth = view.width;
    g_renderer.sceneViewHeight = view.height;
    g_renderer.sceneViewTimeSeconds =
        static_cast<float>(view.time) * 0.001f;
    std::copy(viewProjection, viewProjection + 16u,
        g_renderer.sceneViewProjection.begin());
    std::copy(depthHackViewProjection, depthHackViewProjection + 16u,
        g_renderer.sceneDepthHackViewProjection.begin());
    g_renderer.sceneTanHalfFov = {view.tanHalfFovX, view.tanHalfFovY};
    std::copy(axis, axis + 9u, g_renderer.sceneViewAxis.begin());
    std::copy_n(view.viewOrigin, 3u, g_renderer.sceneViewOrigin.begin());
    SelectSpotShadowLights();
    std::copy_n(view.fogColor, 4u, g_renderer.sceneFogColor.begin());
    g_renderer.sceneFogParams = {view.fogStart, view.fogDensity};
    g_renderer.sceneFogEnabled = view.fogEnabled;
    g_renderer.sceneSunShadowEnabled = view.sunShadowEnabled &&
        BuildSunShadowMatrix(
            view, 0.25f, g_renderer.sceneSunShadowMatrix) &&
        BuildSunShadowMatrix(
            view, 1.0f, g_renderer.sceneSunShadowFarMatrix);
    std::copy_n(view.sunDirection, 3u,
        g_renderer.sceneSunDirection.begin());
    std::copy_n(view.sunColor, 3u, g_renderer.sceneSunColor.begin());
    std::copy_n(view.worldMins, 3u, g_renderer.sceneWorldMins.begin());
    std::copy_n(view.worldMaxs, 3u, g_renderer.sceneWorldMaxs.begin());
    std::copy_n(view.colorBias, 4u, g_renderer.sceneColorBias.begin());
    std::copy_n(view.colorTintBase, 4u,
        g_renderer.sceneColorTintBase.begin());
    std::copy_n(view.colorTintDelta, 4u,
        g_renderer.sceneColorTintDelta.begin());
    g_renderer.sceneDisplayGammaExponent = view.displayGammaExponent;
    g_renderer.sceneBlurRadius = view.blurRadius;
    g_renderer.sceneGlowBloomCutoff = view.glowBloomCutoff;
    g_renderer.sceneGlowBloomCutoffRescale =
        view.glowBloomCutoffRescale;
    g_renderer.sceneGlowBloomDesaturation = view.glowBloomDesaturation;
    g_renderer.sceneGlowBloomIntensity = view.glowBloomIntensity;
    g_renderer.sceneGlowRadius = view.glowRadius;
    g_renderer.sceneDepthOfField = view.depthOfField;
    g_renderer.sceneZNear = view.zNear;
    g_renderer.sceneDepthHackZNear = view.depthHackZNear;
    g_renderer.sceneFilmEnabled = view.filmEnabled;
    g_renderer.sceneGlowEnabled = view.glowEnabled;
    g_renderer.sceneViewWorldName = view.worldName;
    if (view.geometrySubmitted)
    {
        g_renderer.sceneViewSurfaceSubmissionGeneration =
            g_renderer.surfaceSubmissionGeneration;
    }
    if (worldChanged)
    {
        g_renderer.sceneViewFirstDrawCompleted = false;
        g_renderer.sceneViewWaitReported = false;
        g_renderer.sceneViewDrawnSubmissionGeneration = 0u;
    }
    const bool firstSceneViewSubmission =
        g_renderer.sceneViewSubmissionGeneration == 1u;
    const bool firstGeometryViewSubmission =
        view.geometrySubmitted && !g_renderer.sceneViewFirstDrawCompleted;
    const bool settledSceneViewSubmission =
#if KISAK_WEB_DIAGNOSTICS
        true;
#else
        g_renderer.sceneViewSubmissionGeneration == 30u ||
        g_renderer.sceneViewSubmissionGeneration % 60u == 0u;
#endif
    if (firstSceneViewSubmission || firstGeometryViewSubmission ||
        settledSceneViewSubmission)
    {
        DispatchRendererSceneView(
            view.worldName,
            view.x,
            view.y,
            view.width,
            view.height,
            view.tanHalfFovX,
            view.tanHalfFovY,
            view.viewOrigin[0],
            view.viewOrigin[1],
            view.viewOrigin[2],
            view.viewAxis[0][0],
            view.viewAxis[0][1],
            view.viewAxis[0][2],
            view.time,
            view.zNear,
            g_renderer.sceneViewSubmissionGeneration,
            view.worldSurfaceCount,
            view.worldVertexCount,
            view.worldIndexCount,
            view.geometrySubmitted);
        if (firstSceneViewSubmission || firstGeometryViewSubmission)
        {
            Web_Log(
                WebLogLevel::Info,
                "[kisakcod-web] Canonical cgame view reached R_RenderScene "
                "for %s (view=%u, geometry=%u, viewSurfaceGeneration=%u, "
                "surfaceGeneration=%u).\n",
                view.worldName,
                g_renderer.sceneViewSubmissionGeneration,
                view.geometrySubmitted ? 1u : 0u,
                g_renderer.sceneViewSurfaceSubmissionGeneration,
                g_renderer.surfaceSubmissionGeneration);
        }
    }
    return true;
}

namespace
{
GLenum WorldBlendFactor(std::uint32_t factor) noexcept
{
    switch (factor)
    {
    case 1u: return GL_ZERO;
    case 2u: return GL_ONE;
    case 3u: return GL_SRC_COLOR;
    case 4u: return GL_ONE_MINUS_SRC_COLOR;
    case 5u: return GL_SRC_ALPHA;
    case 6u: return GL_ONE_MINUS_SRC_ALPHA;
    case 7u: return GL_DST_ALPHA;
    case 8u: return GL_ONE_MINUS_DST_ALPHA;
    case 9u: return GL_DST_COLOR;
    case 10u: return GL_ONE_MINUS_DST_COLOR;
    default: return GL_ONE;
    }
}

GLenum WorldBlendEquation(std::uint32_t operation) noexcept
{
    switch (operation)
    {
    case 2u: return GL_FUNC_SUBTRACT;
    case 3u: return GL_FUNC_REVERSE_SUBTRACT;
    case 4u: return GL_MIN;
    case 5u: return GL_MAX;
    default: return GL_FUNC_ADD;
    }
}

std::int32_t WorldAlphaTestMode(std::uint32_t state0) noexcept
{
    if ((state0 & 0x800u) != 0u) return 0;
    switch (state0 & 0x3000u)
    {
    case 0x1000u: return 1;
    case 0x2000u: return 2;
    case 0x3000u: return 3;
    default: return 0;
    }
}

void ApplyWorldMaterialState(const WebRendererRetainedWorldBatch &batch)
{
    const std::uint32_t state0 = batch.stateBits[0];
    const std::uint32_t state1 = batch.stateBits[1];
    const bool hasCanonicalState = batch.materialIdentity != nullptr &&
        (state0 != 0u || state1 != 0u);
    // R_SetAlphaAntiAliasingState enables the selected transparency-AA mode
    // only for opaque alpha-tested state when the scene target is
    // multisampled. SAMPLE_ALPHA_TO_COVERAGE is the WebGL2 boundary for that
    // D3D9 coverage-mask behavior.
    const bool alphaToCoverage = g_renderer.aaActiveSamples > 1 &&
        r_aaAlpha && r_aaAlpha->current.integer != 0 &&
        (state0 & 0x0f00u) == 0u;
    if (alphaToCoverage)
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    else
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    switch (state0 & 0xc000u)
    {
    case 0x8000u:
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CW);
        glCullFace(GL_BACK);
        break;
    case 0xc000u:
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CW);
        glCullFace(GL_FRONT);
        break;
    default:
        glDisable(GL_CULL_FACE);
        break;
    }

    const bool rgbWrite = !hasCanonicalState ||
        (state0 & 0x08000000u) != 0u;
    const bool alphaWrite = !hasCanonicalState ||
        (state0 & 0x10000000u) != 0u;
    glColorMask(rgbWrite, rgbWrite, rgbWrite, alphaWrite);
    if ((state0 & 0x700u) != 0u)
    {
        glEnable(GL_BLEND);
        glBlendEquationSeparate(
            WorldBlendEquation((state0 >> 8u) & 7u),
            WorldBlendEquation((state0 >> 24u) & 7u));
        glBlendFuncSeparate(
            WorldBlendFactor(state0 & 0xfu),
            WorldBlendFactor((state0 >> 4u) & 0xfu),
            WorldBlendFactor((state0 >> 16u) & 0xfu),
            WorldBlendFactor((state0 >> 20u) & 0xfu));
    }
    else
    {
        glDisable(GL_BLEND);
    }
    const bool shaderPremultipliesAlpha =
        batch.technique == WebRendererWorldTechnique::VertexColorAdditive ||
        ((state0 & 0x700u) != 0u &&
            (state0 & 0xfu) == 2u &&
            ((state0 >> 4u) & 0xfu) == 6u);
    glUniform1f(g_renderer.premultiplyAlphaUniform,
        shaderPremultipliesAlpha ? 1.0f : 0.0f);
    glUniform1f(g_renderer.colorIntensityAlphaUniform,
        WebRenderer_UsesColorIntensityOpacity(batch.technique) ? 1.0f : 0.0f);
    glUniform4fv(g_renderer.falloffParmsUniform, 1, batch.falloffParms);
    glUniform4fv(g_renderer.falloffBeginColorUniform, 1,
        batch.falloffBeginColor);
    glUniform4fv(g_renderer.falloffEndColorUniform, 1,
        batch.falloffEndColor);
    glUniform1i(g_renderer.materialModeUniform,
        batch.sourceKind == WebRendererSceneBatchKind::SunFlare
            ? 5
            : batch.ambientProbeLighting
            ? 6
            : batch.technique == WebRendererWorldTechnique::VertexColorMultiply
            ? 1
            : (batch.technique ==
                    WebRendererWorldTechnique::VertexColorAdditive
                ? 2
                : (batch.technique ==
                        WebRendererWorldTechnique::WaterLitSun
                    ? 3
                    : (batch.technique == WebRendererWorldTechnique::
                            VertexColorDistanceFalloff
                        ? 4 : 0))));

    if (hasCanonicalState && (state1 & 2u) != 0u)
    {
        glDisable(GL_DEPTH_TEST);
    }
    else
    {
        glEnable(GL_DEPTH_TEST);
        switch (hasCanonicalState ? (state1 & 0xcu) : 12u)
        {
        case 4u: glDepthFunc(GL_LESS); break;
        case 8u: glDepthFunc(GL_EQUAL); break;
        case 12u: glDepthFunc(GL_LEQUAL); break;
        default: glDepthFunc(GL_ALWAYS); break;
        }
    }
    glDepthMask(!hasCanonicalState || (state1 & 1u) != 0u
        ? GL_TRUE : GL_FALSE);

    glUniform1i(g_renderer.alphaTestUniform, WorldAlphaTestMode(state0));
}

void ApplyUiMaterialState(const WebRendererRetainedUiBatch &batch)
{
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glUniform1f(g_renderer.detailMapEnabledUniform, 0.0f);
    glUniform1f(g_renderer.colorIntensityAlphaUniform, 0.0f);
    glUniform1i(g_renderer.materialModeUniform, 0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    if (!batch.hasMaterialState)
    {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUniform1i(g_renderer.alphaTestUniform, 0);
        glUniform1f(g_renderer.premultiplyAlphaUniform, 0.0f);
        return;
    }

    const std::uint32_t state0 = batch.stateBits[0];
    const bool rgbWrite = (state0 & 0x08000000u) != 0u;
    glColorMask(rgbWrite, rgbWrite, rgbWrite,
        (state0 & 0x10000000u) != 0u);
    if ((state0 & 0x700u) != 0u)
    {
        glEnable(GL_BLEND);
        glBlendEquationSeparate(
            WorldBlendEquation((state0 >> 8u) & 7u),
            WorldBlendEquation((state0 >> 24u) & 7u));
        glBlendFuncSeparate(
            WorldBlendFactor(state0 & 0xfu),
            WorldBlendFactor((state0 >> 4u) & 0xfu),
            WorldBlendFactor((state0 >> 16u) & 0xfu),
            WorldBlendFactor((state0 >> 20u) & 0xfu));
    }
    else
    {
        glDisable(GL_BLEND);
    }
    const bool shaderPremultipliesAlpha =
        (state0 & 0x700u) != 0u &&
        (state0 & 0xfu) == 2u &&
        ((state0 >> 4u) & 0xfu) == 6u;
    glUniform1f(g_renderer.premultiplyAlphaUniform,
        shaderPremultipliesAlpha ? 1.0f : 0.0f);

    std::int32_t alphaTest = 0;
    if ((state0 & 0x800u) == 0u)
    {
        switch (state0 & 0x3000u)
        {
        case 0x1000u: alphaTest = 1; break;
        case 0x2000u: alphaTest = 2; break;
        case 0x3000u: alphaTest = 3; break;
        default: break;
        }
    }
    glUniform1i(g_renderer.alphaTestUniform, alphaTest);
}

void BindWorldTexture(
    GLenum unit,
    GLuint texture,
    std::uint8_t samplerState,
    bool mipmapsAvailable = true)
{
    glActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, texture);
    const std::uint8_t filter = samplerState & 7u;
    const bool linear = filter == 2u || filter == 3u || filter == 4u;
    const std::uint8_t mipMode = mipmapsAvailable
        ? samplerState & 0x18u : 0u;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
        linear ? GL_LINEAR : GL_NEAREST);
    GLint minFilter = linear ? GL_LINEAR : GL_NEAREST;
    if (mipMode == 0x08u)
        minFilter = linear ? GL_LINEAR_MIPMAP_NEAREST
                           : GL_NEAREST_MIPMAP_NEAREST;
    else if (mipMode == 0x10u)
        minFilter = linear ? GL_LINEAR_MIPMAP_LINEAR
                           : GL_NEAREST_MIPMAP_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    if (g_renderer.textureAnisotropySupported)
    {
        const float requested = filter == 4u ? 4.0f :
            (filter == 3u ? 2.0f : 1.0f);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
            std::min(requested, g_renderer.maxTextureAnisotropy));
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
        (samplerState & 0x20u) != 0u ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
        (samplerState & 0x40u) != 0u ? GL_CLAMP_TO_EDGE : GL_REPEAT);
}

bool BindWaterTextures(WebRendererRetainedWorldBatch &batch, float floatTime)
{
    if (batch.waterTexture == 0u || batch.reflectionTexture == 0u)
        return false;
    if (batch.waterTextureTime != floatTime)
    {
        water_t water{};
        water.H0 = batch.waterH0.data();
        water.wTerm = batch.waterWTerm.data();
        water.M = batch.waterM;
        water.N = batch.waterN;
        if (!R_GenerateWaterPixelsR8(&water, floatTime,
                batch.waterPixels.data(), batch.waterPixels.size()))
            return false;
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, batch.waterTexture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
            batch.waterM, batch.waterN, GL_RED, GL_UNSIGNED_BYTE,
            batch.waterPixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        batch.waterTextureTime = floatTime;
    }
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, batch.waterTexture);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_CUBE_MAP, batch.reflectionTexture);
    glActiveTexture(GL_TEXTURE0);
    return glGetError() == GL_NO_ERROR;
}

void BindModelLightingTexture(
    const WebRendererRetainedModelLightingAtlas &atlas)
{
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, atlas.texture);
    glUniform3f(g_renderer.modelLightingLookupScaleUniform,
        atlas.width ? 1.5f / static_cast<float>(atlas.width) : 0.0f,
        atlas.height ? 1.5f / static_cast<float>(atlas.height) : 0.0f,
        atlas.depth ? 1.5f / static_cast<float>(atlas.depth) : 0.0f);
}

const WebRendererRetainedWorldImage *RetainedImage(
    const std::vector<WebRendererRetainedWorldImage> &images,
    std::uint32_t index) noexcept
{
    if (index == INVALID_WORLD_IMAGE ||
        index >= images.size())
        return nullptr;
    const WebRendererRetainedWorldImage &image = images[index];
    return image.supported && image.texture != 0u ? &image : nullptr;
}

const WebRendererRetainedWorldImage *WorldImage(
    std::uint32_t index) noexcept
{
    return RetainedImage(g_renderer.retainedWorldImages, index);
}

bool DrawShadowPartition(
    GLuint framebuffer,
    const std::array<float, 16> &matrix,
    GLsizei shadowSize,
    bool requireSunCaster,
    std::uint32_t spotPrimaryLightIndex = UINT32_MAX)
{
    if (g_renderer.shadowProgram == 0u ||
        framebuffer == 0u ||
        !g_renderer.worldSurfaceActive)
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, shadowSize, shadowSize);
    glUseProgram(g_renderer.shadowProgram);
    glUniformMatrix4fv(g_renderer.shadowDepthMatrixUniform, 1, GL_FALSE,
        matrix.data());
    glUniform1i(g_renderer.shadowDepthTextureUniform, 0);
    glUniform1f(g_renderer.shadowDepthInstanceEnabledUniform, 0.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    glBindVertexArray(g_renderer.vertexArray);
    const auto applySpotShadowCull = [](std::uint32_t stateBits0)
    {
        switch (stateBits0 & 0xc000u)
        {
        case 0x8000u:
            glEnable(GL_CULL_FACE);
            glFrontFace(GL_CW);
            glCullFace(GL_BACK);
            break;
        case 0xc000u:
            glEnable(GL_CULL_FACE);
            glFrontFace(GL_CW);
            glCullFace(GL_FRONT);
            break;
        default:
            glDisable(GL_CULL_FACE);
            break;
        }
    };
    const auto drawWorldRange =
        [requireSunCaster, &applySpotShadowCull](
        const WebRendererRetainedWorldBatch &batch,
        std::uint32_t firstIndex,
        std::uint32_t indexCount,
        std::uint32_t shadowStateBits0)
    {
        // Native shadow-map draws use TECHNIQUE_BUILD_SHADOWMAP_DEPTH state,
        // not the receiver's lit state. In particular, preserving its cull
        // mode prevents coplanar back faces from self-shadowing the floor.
        if (!requireSunCaster)
            applySpotShadowCull(shadowStateBits0);
        const WebRendererRetainedWorldImage *base =
            WorldImage(batch.baseImageIndex);
        const std::int32_t alphaTest = WorldAlphaTestMode(
            requireSunCaster ? batch.stateBits[0] : shadowStateBits0);
        glUniform1f(g_renderer.shadowDepthTextureEnabledUniform,
            base ? 1.0f : 0.0f);
        glUniform1i(g_renderer.shadowDepthAlphaTestUniform, alphaTest);
        BindWorldTexture(GL_TEXTURE0,
            base ? base->texture : g_renderer.texture,
            batch.samplerState);
        const std::uintptr_t indexOffset =
            static_cast<std::uintptr_t>(firstIndex) *
            sizeof(std::uint32_t);
        glDrawElements(GL_TRIANGLES,
            static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT,
            reinterpret_cast<const void *>(indexOffset));
    };
    if (requireSunCaster)
    {
        for (const WebRendererRetainedWorldBatch &batch :
             g_renderer.retainedWorldBatches)
        {
            if (!batch.castsSunShadow) continue;
            drawWorldRange(batch, batch.firstIndex, batch.indexCount,
                batch.stateBits[0]);
        }
    }
    else
    {
        for (const WebRendererRetainedSpotShadowCaster &caster :
             g_renderer.retainedWorldSpotShadowCasters)
        {
            if (caster.primaryLightIndex != spotPrimaryLightIndex) continue;
            const WebRendererRetainedWorldBatch &batch =
                g_renderer.retainedWorldBatches[caster.batchIndex];
            drawWorldRange(batch, caster.firstIndex, caster.indexCount,
                caster.stateBits0);
        }
    }
    if (g_renderer.staticModelSceneActive &&
        g_renderer.staticModelVertexArray != 0u &&
        g_renderer.staticModelInstanceBuffer != 0u)
    {
        glBindVertexArray(g_renderer.staticModelVertexArray);
        glUniform1f(g_renderer.shadowDepthInstanceEnabledUniform, 1.0f);
        for (const WebRendererRetainedStaticModelBatch &batch :
             g_renderer.retainedStaticModelBatches)
        {
            if (requireSunCaster && !batch.draw.castsSunShadow) continue;
            if (!requireSunCaster)
                applySpotShadowCull(batch.draw.shadowStateBits0);
            const WebRendererRetainedWorldImage *base = RetainedImage(
                g_renderer.retainedStaticModelImages,
                batch.draw.baseImageIndex);
            glUniform1f(g_renderer.shadowDepthTextureEnabledUniform,
                base ? 1.0f : 0.0f);
            glUniform1i(g_renderer.shadowDepthAlphaTestUniform,
                WorldAlphaTestMode(requireSunCaster
                    ? batch.draw.stateBits[0]
                    : batch.draw.shadowStateBits0));
            BindWorldTexture(GL_TEXTURE0,
                base ? base->texture : g_renderer.texture,
                batch.draw.samplerState);
            if (batch.instanceCount == 0u) continue;
            const std::uintptr_t indexOffset =
                static_cast<std::uintptr_t>(batch.draw.firstIndex) *
                sizeof(std::uint32_t);
            if (requireSunCaster)
            {
                BindStaticModelInstanceRange(batch.instanceOffset);
                glDrawElementsInstanced(GL_TRIANGLES,
                    static_cast<GLsizei>(batch.draw.indexCount),
                    GL_UNSIGNED_INT,
                    reinterpret_cast<const void *>(indexOffset),
                    static_cast<GLsizei>(batch.instanceCount));
                continue;
            }

            const auto castsForSpot = [spotPrimaryLightIndex](
                std::uint32_t canonicalInstanceIndex)
            {
                const WebRendererRetainedSpotShadowStaticModel target{
                    spotPrimaryLightIndex,
                    canonicalInstanceIndex,
                };
                return std::binary_search(
                    g_renderer.retainedWorldSpotShadowStaticModels.begin(),
                    g_renderer.retainedWorldSpotShadowStaticModels.end(),
                    target,
                    [](const WebRendererRetainedSpotShadowStaticModel &left,
                       const WebRendererRetainedSpotShadowStaticModel &right)
                    {
                        if (left.primaryLightIndex != right.primaryLightIndex)
                            return left.primaryLightIndex <
                                right.primaryLightIndex;
                        return left.canonicalInstanceIndex <
                            right.canonicalInstanceIndex;
                    });
            };
            const std::uint32_t instanceEnd =
                batch.instanceOffset + batch.instanceCount;
            std::uint32_t instanceIndex = batch.instanceOffset;
            while (instanceIndex < instanceEnd)
            {
                while (instanceIndex < instanceEnd &&
                    !castsForSpot(g_renderer.retainedStaticModelInstances[
                        instanceIndex].canonicalInstanceIndex))
                {
                    ++instanceIndex;
                }
                const std::uint32_t runBegin = instanceIndex;
                while (instanceIndex < instanceEnd &&
                    castsForSpot(g_renderer.retainedStaticModelInstances[
                        instanceIndex].canonicalInstanceIndex))
                {
                    ++instanceIndex;
                }
                if (instanceIndex == runBegin) continue;
                BindStaticModelInstanceRange(runBegin);
                glDrawElementsInstanced(GL_TRIANGLES,
                    static_cast<GLsizei>(batch.draw.indexCount),
                    GL_UNSIGNED_INT,
                    reinterpret_cast<const void *>(indexOffset),
                    static_cast<GLsizei>(instanceIndex - runBegin));
            }
        }
    }
    if (requireSunCaster && g_renderer.dynamicModelSceneActive &&
        g_renderer.dynamicModelVertexArray != 0u)
    {
        glBindVertexArray(g_renderer.dynamicModelVertexArray);
        glUniform1f(g_renderer.shadowDepthInstanceEnabledUniform, 0.0f);
        for (const WebRendererRetainedWorldBatch &batch :
             g_renderer.retainedDynamicModelBatches)
        {
            if ((requireSunCaster && !batch.castsSunShadow) ||
                batch.depthHack ||
                WebRenderer_IsFxVertexColorBatch(batch.sourceKind))
                continue;
            if (!requireSunCaster)
                applySpotShadowCull(batch.stateBits[0]);
            const WebRendererRetainedWorldImage *base = RetainedImage(
                g_renderer.retainedDynamicModelImages,
                batch.baseImageIndex);
            glUniform1f(g_renderer.shadowDepthTextureEnabledUniform,
                base ? 1.0f : 0.0f);
            glUniform1i(g_renderer.shadowDepthAlphaTestUniform,
                WorldAlphaTestMode(batch.stateBits[0]));
            BindWorldTexture(GL_TEXTURE0,
                base ? base->texture : g_renderer.texture,
                batch.samplerState);
            const std::uintptr_t indexOffset =
                static_cast<std::uintptr_t>(batch.firstIndex) *
                sizeof(std::uint32_t);
            glDrawElements(GL_TRIANGLES,
                static_cast<GLsizei>(batch.indexCount), GL_UNSIGNED_INT,
                reinterpret_cast<const void *>(indexOffset));
        }
    }
    glUniform1f(g_renderer.shadowDepthInstanceEnabledUniform, 0.0f);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    return glGetError() == GL_NO_ERROR;
}

bool DrawSunShadowMaps()
{
    if (!g_renderer.sceneSunShadowEnabled ||
        g_renderer.shadowDepthTexture == 0u ||
        g_renderer.shadowFarDepthTexture == 0u)
        return false;
    return DrawShadowPartition(
            g_renderer.shadowFramebuffer,
            g_renderer.sceneSunShadowMatrix, SUN_SHADOW_SIZE, true) &&
        DrawShadowPartition(
            g_renderer.shadowFarFramebuffer,
            g_renderer.sceneSunShadowFarMatrix, SUN_SHADOW_SIZE, true);
}

bool DrawSpotShadowMaps()
{
    if (g_renderer.sceneSpotShadowCount == 0u) return false;
    for (std::size_t slot = 0u;
         slot < g_renderer.sceneSpotShadowCount; ++slot)
    {
        if (g_renderer.spotShadowDepthTextures[slot] == 0u ||
            !DrawShadowPartition(g_renderer.spotShadowFramebuffers[slot],
                g_renderer.sceneSpotShadowMatrices[slot],
                SPOT_SHADOW_SIZE, false,
                g_renderer.sceneSpotShadowLightIndices[slot]))
            return false;
    }
    return true;
}

void BindSpotShadowForPrimaryLight(
    std::uint8_t primaryLightIndex,
    bool primaryLit,
    bool spotShadowMapsDrawn)
{
    if (primaryLit && spotShadowMapsDrawn)
    {
        for (std::size_t slot = 0u;
             slot < g_renderer.sceneSpotShadowCount; ++slot)
        {
            if (g_renderer.sceneSpotShadowLightIndices[slot] !=
                primaryLightIndex)
                continue;
            glUniform1f(g_renderer.spotShadowEnabledUniform, 1.0f);
            glUniformMatrix4fv(g_renderer.spotShadowMatrixUniform, 1,
                GL_FALSE, g_renderer.sceneSpotShadowMatrices[slot].data());
            glActiveTexture(GL_TEXTURE14);
            glBindTexture(GL_TEXTURE_2D,
                g_renderer.spotShadowDepthTextures[slot]);
            glActiveTexture(GL_TEXTURE0);
            return;
        }
    }
    glUniform1f(g_renderer.spotShadowEnabledUniform, 0.0f);
}

void BindStaticModelInstanceRange(std::uint32_t instanceOffset)
{
    glBindBuffer(GL_ARRAY_BUFFER, g_renderer.staticModelInstanceBuffer);
    const std::size_t base = static_cast<std::size_t>(instanceOffset) *
        sizeof(WebRendererStaticModelInstanceDesc);
    constexpr std::size_t AXIS_OFFSET =
        offsetof(WebRendererStaticModelInstanceDesc, axis);
    for (GLuint row = 0u; row < 3u; ++row)
    {
        glVertexAttribPointer(
            4u + row,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(WebRendererStaticModelInstanceDesc),
            reinterpret_cast<const void *>(
                base + AXIS_OFFSET + row * 3u * sizeof(float)));
    }
    glVertexAttribPointer(
        7u,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(WebRendererStaticModelInstanceDesc),
        reinterpret_cast<const void *>(
            base + offsetof(WebRendererStaticModelInstanceDesc, origin)));
    glVertexAttribPointer(
        9u,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(WebRendererStaticModelInstanceDesc),
        reinterpret_cast<const void *>(base + offsetof(
            WebRendererStaticModelInstanceDesc,
            modelLightingCoordinates)));
}

bool UpdateStaticModelLods()
{
    if (!g_renderer.staticModelSceneActive ||
        g_renderer.staticModelInstanceBuffer == 0u ||
        g_renderer.retainedStaticModelSourceInstances.empty())
        return true;

    if (g_renderer.retainedStaticModelInstances.size() !=
            g_renderer.retainedStaticModelSourceInstances.size() ||
        g_renderer.retainedStaticModelSelectedLods.size() !=
            g_renderer.retainedStaticModelSourceInstances.size())
        return false;

    WebRendererLodParms parms{};
    if (!WebRenderer_BuildLodParms(
            g_renderer.sceneViewOrigin.data(),
            g_renderer.sceneTanHalfFov[1],
            r_lodScaleRigid ? r_lodScaleRigid->current.value : 1.0f,
            r_lodBiasRigid ? r_lodBiasRigid->current.value : 0.0f,
            r_lodScaleSkinned ? r_lodScaleSkinned->current.value : 1.0f,
            r_lodBiasSkinned ? r_lodBiasSkinned->current.value : 0.0f,
            parms))
        return false;

    bool changed = false;
#if KISAK_WEB_DIAGNOSTICS
    std::uint64_t changedLodCount = 0u;
#endif
    std::size_t firstBatch = 0u;
    while (firstBatch < g_renderer.retainedStaticModelBatches.size())
    {
        WebRendererRetainedStaticModelBatch &first =
            g_renderer.retainedStaticModelBatches[firstBatch];
        const std::uint32_t sourceOffset = first.sourceInstanceOffset;
        const std::uint32_t sourceCount = first.sourceInstanceCount;
        const XModel *model = first.draw.modelIdentity;
        if (!model || sourceOffset >
                g_renderer.retainedStaticModelSourceInstances.size() ||
            sourceCount >
                g_renderer.retainedStaticModelSourceInstances.size() -
                    sourceOffset)
            return false;

        std::size_t endBatch = firstBatch + 1u;
        while (endBatch < g_renderer.retainedStaticModelBatches.size())
        {
            const WebRendererRetainedStaticModelBatch &next =
                g_renderer.retainedStaticModelBatches[endBatch];
            if (next.sourceInstanceOffset != sourceOffset ||
                next.sourceInstanceCount != sourceCount ||
                next.draw.modelIdentity != model)
                break;
            ++endBatch;
        }

        std::array<std::uint32_t, MAX_LODS> lodCounts{};
        for (std::uint32_t index = 0u; index < sourceCount; ++index)
        {
            const std::size_t sourceIndex = sourceOffset + index;
            const WebRendererStaticModelInstanceDesc &instance =
                g_renderer.retainedStaticModelSourceInstances[sourceIndex];
            const int selectedLod = WebRenderer_SelectStaticModelLod(
                model, instance.origin, instance.modelScale,
                instance.modelCullDistance, parms);
            if (selectedLod < -1 || selectedLod >= MAX_LODS)
                return false;
            if (g_renderer.retainedStaticModelSelectedLods[sourceIndex] !=
                    selectedLod)
            {
                g_renderer.retainedStaticModelSelectedLods[sourceIndex] =
                    static_cast<std::int8_t>(selectedLod);
                changed = true;
#if KISAK_WEB_DIAGNOSTICS
                ++changedLodCount;
#endif
            }
            if (selectedLod >= 0)
                ++lodCounts[static_cast<std::size_t>(selectedLod)];
        }

        std::array<std::uint32_t, MAX_LODS> lodOffsets{};
        std::uint32_t writeOffset = sourceOffset;
        for (std::size_t lod = 0u; lod < MAX_LODS; ++lod)
        {
            lodOffsets[lod] = writeOffset;
            for (std::uint32_t index = 0u; index < sourceCount; ++index)
            {
                const std::size_t sourceIndex = sourceOffset + index;
                if (g_renderer.retainedStaticModelSelectedLods[sourceIndex] ==
                        static_cast<std::int8_t>(lod))
                {
                    g_renderer.retainedStaticModelInstances[writeOffset++] =
                        g_renderer.retainedStaticModelSourceInstances[
                            sourceIndex];
                }
            }
        }
        for (std::uint32_t index = 0u; index < sourceCount; ++index)
        {
            const std::size_t sourceIndex = sourceOffset + index;
            if (g_renderer.retainedStaticModelSelectedLods[sourceIndex] < 0)
            {
                g_renderer.retainedStaticModelInstances[writeOffset++] =
                    g_renderer.retainedStaticModelSourceInstances[sourceIndex];
            }
        }
        if (writeOffset != sourceOffset + sourceCount)
            return false;

        for (std::size_t batchIndex = firstBatch;
             batchIndex < endBatch; ++batchIndex)
        {
            WebRendererRetainedStaticModelBatch &batch =
                g_renderer.retainedStaticModelBatches[batchIndex];
            batch.instanceOffset = lodOffsets[batch.lodIndex];
            batch.instanceCount = lodCounts[batch.lodIndex];
        }
        firstBatch = endBatch;
    }
    if (!changed)
        return true;

#if KISAK_WEB_DIAGNOSTICS
    if (WebFrameProfileSample *const profile = WebFrameProfile_Current())
        profile->lodChanges += changedLodCount;
#endif

    glBindBuffer(GL_ARRAY_BUFFER, g_renderer.staticModelInstanceBuffer);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(
            g_renderer.retainedStaticModelInstances.size() *
            sizeof(WebRendererStaticModelInstanceDesc)),
        g_renderer.retainedStaticModelInstances.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0u);
    return glGetError() == GL_NO_ERROR;
}

float UpdateSunEffectOverTime(float current, float goal,
    float fadeInMilliseconds, float fadeOutMilliseconds,
    float frameMilliseconds) noexcept
{
    if (goal > current)
    {
        if (fadeInMilliseconds <= 0.0f) return goal;
        return std::min(goal,
            current + frameMilliseconds / fadeInMilliseconds);
    }
    if (goal < current)
    {
        if (fadeOutMilliseconds <= 0.0f) return goal;
        return std::max(goal,
            current - frameMilliseconds / fadeOutMilliseconds);
    }
    return current;
}

void UpdateSunPostEffectState() noexcept
{
    const WebRendererRetainedWorldBatch *flare = nullptr;
    for (const WebRendererRetainedWorldBatch &batch :
         g_renderer.retainedDynamicModelBatches)
    {
        if (batch.sourceKind == WebRendererSceneBatchKind::SunFlare)
        {
            flare = &batch;
            break;
        }
    }
    if (flare)
    {
        g_renderer.sunFlareFadeInMilliseconds = std::max(
            0.0f, flare->falloffBeginColor[0]);
        g_renderer.sunFlareFadeOutMilliseconds = std::max(
            0.0f, flare->falloffBeginColor[1]);
        g_renderer.sunBlindLerp = std::clamp(
            flare->envMapParms[0], 0.0f, 1.0f);
        g_renderer.sunBlindMaxDarken = std::max(
            0.0f, flare->envMapParms[1]);
        g_renderer.sunBlindFadeInMilliseconds = std::max(
            0.0f, flare->envMapParms[2]);
        g_renderer.sunBlindFadeOutMilliseconds = std::max(
            0.0f, flare->envMapParms[3]);
        g_renderer.sunGlareLerp = std::clamp(
            flare->waterColor[0], 0.0f, 1.0f);
        g_renderer.sunGlareMaxLighten = std::max(
            0.0f, flare->waterColor[1]);
        g_renderer.sunGlareFadeInMilliseconds = std::max(
            0.0f, flare->waterColor[2]);
        g_renderer.sunGlareFadeOutMilliseconds = std::max(
            0.0f, flare->waterColor[3]);
    }
    const double now = static_cast<double>(
        g_renderer.sceneViewTimeSeconds) * 1000.0;
    float frameMilliseconds = 10.0f;
    if (g_renderer.sunEffectLastMilliseconds > 0.0 &&
        now >= g_renderer.sunEffectLastMilliseconds)
    {
        frameMilliseconds = static_cast<float>(
            now - g_renderer.sunEffectLastMilliseconds);
    }
    g_renderer.sunEffectLastMilliseconds = now;
    const float visibilityGoal = flare
        ? std::clamp(g_renderer.sunVisibility, 0.0f, 1.0f) : 0.0f;
    g_renderer.sunFlareIntensity = UpdateSunEffectOverTime(
        g_renderer.sunFlareIntensity,
        visibilityGoal,
        g_renderer.sunFlareFadeInMilliseconds,
        g_renderer.sunFlareFadeOutMilliseconds,
        frameMilliseconds);
    g_renderer.sunBlindIntensity = UpdateSunEffectOverTime(
        g_renderer.sunBlindIntensity,
        visibilityGoal * g_renderer.sunBlindLerp,
        g_renderer.sunBlindFadeInMilliseconds,
        g_renderer.sunBlindFadeOutMilliseconds,
        frameMilliseconds);
    g_renderer.sunBlindDarken = g_renderer.sunBlindIntensity *
        g_renderer.sunBlindMaxDarken;
    g_renderer.sunGlareIntensity = UpdateSunEffectOverTime(
        g_renderer.sunGlareIntensity,
        visibilityGoal * g_renderer.sunGlareLerp,
        g_renderer.sunGlareFadeInMilliseconds,
        g_renderer.sunGlareFadeOutMilliseconds,
        frameMilliseconds);
    g_renderer.sunGlareLighten = g_renderer.sunGlareIntensity *
        g_renderer.sunGlareMaxLighten;
}

GLuint DrawGlowImage(int width, int height)
{
    if (!g_renderer.sceneGlowEnabled || g_renderer.glowProgram == 0u ||
        g_renderer.glowFramebuffers[0] == 0u ||
        g_renderer.glowFramebuffers[1] == 0u)
        return 0u;

    const int glowWidth = std::max(1, width / 4);
    const int glowHeight = std::max(1, height / 4);
    glUseProgram(g_renderer.glowProgram);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUniform1i(g_renderer.glowTextureUniform, 5);
    glBindVertexArray(g_renderer.vertexArray);

    // glow_consistent_setup thresholds the resolved scene while reducing it
    // to one quarter in each dimension. Four symmetric source taps keep the
    // reduction stable across native scene resolutions.
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.glowFramebuffers[0]);
    glViewport(0, 0, glowWidth, glowHeight);
    glUniform1i(g_renderer.glowModeUniform, 0);
    glUniform2f(g_renderer.glowTexelDeltaUniform,
        width > 0 ? 1.5f / static_cast<float>(width) : 0.0f,
        height > 0 ? 1.5f / static_cast<float>(height) : 0.0f);
    glUniform4f(g_renderer.glowSetupUniform,
        g_renderer.sceneGlowBloomCutoff,
        g_renderer.sceneGlowBloomCutoffRescale,
        0.0f,
        g_renderer.sceneGlowBloomDesaturation);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, g_renderer.sceneColorTexture);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    const float filterRadius = g_renderer.sceneGlowRadius *
        static_cast<float>(glowHeight) / 480.0f;
    const float sigma = std::max(filterRadius / 3.0f, 0.5f);
    std::array<float, 9> weights{};
    weights[0] = 1.0f;
    float weightSum = weights[0];
    for (std::size_t tap = 1u; tap < weights.size(); ++tap)
    {
        const float distance = static_cast<float>(tap);
        weights[tap] = std::exp(
            -(distance * distance) / (2.0f * sigma * sigma));
        weightSum += 2.0f * weights[tap];
    }
    for (float &weight : weights) weight /= weightSum;
    glUniform1fv(g_renderer.glowWeightsUniform,
        static_cast<GLsizei>(weights.size()), weights.data());
    glUniform1i(g_renderer.glowModeUniform, 1);

    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.glowFramebuffers[1]);
    glUniform2f(g_renderer.glowTexelDeltaUniform,
        1.0f / static_cast<float>(glowWidth), 0.0f);
    glBindTexture(GL_TEXTURE_2D, g_renderer.glowColorTextures[0]);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.glowFramebuffers[0]);
    glUniform2f(g_renderer.glowTexelDeltaUniform,
        0.0f, 1.0f / static_cast<float>(glowHeight));
    glBindTexture(GL_TEXTURE_2D, g_renderer.glowColorTextures[1]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    static bool glowDrawReported = false;
    if (!glowDrawReported)
    {
        glowDrawReported = true;
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Canonical glow draw: source=%dx%d "
            "filter=%dx%d cutoff=%.6f rescale=%.6f "
            "desaturation=%.6f intensity=%.6f radius=%.6f "
            "sigma=%.6f.\n",
            width, height, glowWidth, glowHeight,
            g_renderer.sceneGlowBloomCutoff,
            g_renderer.sceneGlowBloomCutoffRescale,
            g_renderer.sceneGlowBloomDesaturation,
            g_renderer.sceneGlowBloomIntensity,
            g_renderer.sceneGlowRadius, sigma);
    }
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
    return g_renderer.glowColorTextures[0];
}

void DrawPostProcessPass(
    GLuint sourceTexture,
    GLuint glowTexture,
    GLuint destinationFramebuffer,
    int width,
    int height,
    bool filmEnabled,
    bool depthOfFieldEnabled,
    float gammaExponent,
    float blurRadius,
    float blindDarken,
    float glareLighten,
    float glowIntensity)
{
    glBindFramebuffer(GL_FRAMEBUFFER, destinationFramebuffer);
    glViewport(0, 0, width, height);
    glUseProgram(g_renderer.postProcessProgram);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUniform1i(g_renderer.postProcessTextureUniform, 5);
    glUniform1f(g_renderer.postProcessFilmEnabledUniform,
        filmEnabled ? 1.0f : 0.0f);
    glUniform4fv(g_renderer.postProcessColorBiasUniform, 1,
        g_renderer.sceneColorBias.data());
    glUniform4fv(g_renderer.postProcessColorTintBaseUniform, 1,
        g_renderer.sceneColorTintBase.data());
    glUniform4fv(g_renderer.postProcessColorTintDeltaUniform, 1,
        g_renderer.sceneColorTintDelta.data());
    glUniform1f(g_renderer.postProcessGammaExponentUniform,
        gammaExponent);
    const float scaledBlurRadius = blurRadius *
        static_cast<float>(height) / 480.0f;
    glUniform2f(g_renderer.postProcessBlurScaleUniform,
        width > 0 ? scaledBlurRadius / static_cast<float>(width) : 0.0f,
        height > 0 ? scaledBlurRadius / static_cast<float>(height) : 0.0f);
    glUniform1f(g_renderer.postProcessBlindDarkenUniform, blindDarken);
    glUniform1f(g_renderer.postProcessGlareLightenUniform, glareLighten);
    glUniform1i(g_renderer.postProcessGlowTextureUniform, 6);
    glUniform1f(g_renderer.postProcessGlowIntensityUniform, glowIntensity);
    glUniform1i(g_renderer.postProcessDepthTextureUniform, 7);
    glUniform1f(g_renderer.postProcessDofEnabledUniform,
        depthOfFieldEnabled ? 1.0f : 0.0f);
    glUniform4f(g_renderer.postProcessDofViewModelUniform,
        g_renderer.sceneDepthOfField.viewModelStart,
        g_renderer.sceneDepthOfField.viewModelEnd,
        g_renderer.sceneDepthOfField.nearBlur, 0.0f);
    glUniform4f(g_renderer.postProcessDofNearUniform,
        g_renderer.sceneDepthOfField.nearStart,
        g_renderer.sceneDepthOfField.nearEnd,
        g_renderer.sceneDepthOfField.nearBlur, 0.0f);
    glUniform4f(g_renderer.postProcessDofFarUniform,
        g_renderer.sceneDepthOfField.farStart,
        g_renderer.sceneDepthOfField.farEnd,
        g_renderer.sceneDepthOfField.farBlur, 0.0f);
    glUniform4f(g_renderer.postProcessDofDepthUniform,
        g_renderer.sceneZNear,
        g_renderer.sceneDepthHackZNear,
        width > 0
            ? static_cast<float>(height) /
                (480.0f * static_cast<float>(width)) : 0.0f,
        1.0f / 480.0f);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D,
        glowTexture != 0u ? glowTexture : sourceTexture);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D,
        depthOfFieldEnabled
            ? g_renderer.sceneDepthTexture : sourceTexture);
    glBindVertexArray(g_renderer.vertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
}
} // namespace

void WebRenderer_DrawFrame(const WebFrameInfo &frame)
{
    if (!g_renderer.initialized || g_renderer.contextLost ||
        !g_renderer.surfaceActive || g_renderer.vertexArray == 0 ||
        g_renderer.vertexBuffer == 0 || g_renderer.indexBuffer == 0)
    {
        return;
    }

#if KISAK_WEB_DIAGNOSTICS
    PollFrameProfileGpuQueries();
    WebFrameProfileSample *const frameProfile = WebFrameProfile_Current();
    g_frameProfileDrawBucket = FrameProfileDrawBucket::None;
    if (frameProfile)
    {
        frameProfile->contextGeneration = g_renderer.contextGeneration;
        frameProfile->viewSubmissionGeneration =
            g_renderer.sceneViewSubmissionGeneration;
        frameProfile->worldSurfacesSubmitted =
            g_renderer.sceneViewSurfaceCount;
        frameProfile->staticModelInstancesRetained =
            g_renderer.retainedStaticModelSourceInstances.size();
        BeginFrameProfileGpuQuery(*frameProfile);
    }
    const double setupProfileStarted = frameProfile
        ? WebFrameProfile_Now() : 0.0;
#endif

    int width = 0;
    int height = 0;
    emscripten_get_canvas_element_size("#canvas", &width, &height);
    if (width != g_renderer.canvasWidth || height != g_renderer.canvasHeight)
    {
        g_renderer.canvasWidth = width;
        g_renderer.canvasHeight = height;
        glViewport(0, 0, width, height);
    }

    const bool sceneGeometryDraw = g_renderer.sceneViewActive &&
        g_renderer.sceneViewGeometrySubmitted &&
        g_renderer.sceneViewSurfaceSubmissionGeneration ==
            g_renderer.surfaceSubmissionGeneration;
    if (g_renderer.sceneViewActive && !sceneGeometryDraw &&
        !g_renderer.sceneViewFirstDrawCompleted &&
        !g_renderer.sceneViewWaitReported &&
        g_renderer.sceneViewSubmissionGeneration != 0u)
    {
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Cgame scene draw is waiting: active=%u "
            "geometry=%u viewSurfaceGeneration=%u surfaceGeneration=%u "
            "surfaceActive=%u.\n",
            g_renderer.sceneViewActive ? 1u : 0u,
            g_renderer.sceneViewGeometrySubmitted ? 1u : 0u,
            g_renderer.sceneViewSurfaceSubmissionGeneration,
            g_renderer.surfaceSubmissionGeneration,
            g_renderer.surfaceActive ? 1u : 0u);
        g_renderer.sceneViewWaitReported = true;
    }
    const bool multisampleDraw = CreateMultisampleTarget(width, height);
    const bool postProcessTargetsReady =
        g_renderer.postProcessProgram != 0u &&
        (sceneGeometryDraw || multisampleDraw) &&
        CreatePostProcessTargets(width, height);
    const bool postProcessDraw = sceneGeometryDraw &&
        postProcessTargetsReady;
    const bool directAaResolveDraw = multisampleDraw &&
        !postProcessDraw && postProcessTargetsReady;
#if KISAK_WEB_DIAGNOSTICS
    double rendererStageStarted = frameProfile
        ? WebFrameProfile_Now() : 0.0;
    if (frameProfile)
        frameProfile->rendererSetupMs =
            rendererStageStarted - setupProfileStarted;
#endif
    const bool staticModelLodsReady = !sceneGeometryDraw ||
        UpdateStaticModelLods();
#if KISAK_WEB_DIAGNOSTICS
    if (frameProfile)
    {
        frameProfile->lodMs = WebFrameProfile_Now() - rendererStageStarted;
        rendererStageStarted = WebFrameProfile_Now();
        g_frameProfileDrawBucket = FrameProfileDrawBucket::Shadow;
    }
#endif
    static bool staticModelLodFailureReported = false;
    if (!staticModelLodsReady && !staticModelLodFailureReported)
    {
        Web_Log(WebLogLevel::Error,
            "[kisakcod-web] Static XModel LOD selection or instance-buffer "
            "update failed; static models are suppressed.\n");
        staticModelLodFailureReported = true;
    }
    else if (staticModelLodsReady)
    {
        staticModelLodFailureReported = false;
    }
    const bool shadowMapDrawn = sceneGeometryDraw && staticModelLodsReady &&
        DrawSunShadowMaps();
#if KISAK_WEB_DIAGNOSTICS
    if (frameProfile)
    {
        frameProfile->sunShadowMs =
            WebFrameProfile_Now() - rendererStageStarted;
        rendererStageStarted = WebFrameProfile_Now();
    }
#endif
    const bool spotShadowMapsDrawn = sceneGeometryDraw &&
        staticModelLodsReady && DrawSpotShadowMaps();
#if KISAK_WEB_DIAGNOSTICS
    if (frameProfile)
        frameProfile->spotShadowMs =
            WebFrameProfile_Now() - rendererStageStarted;
    g_frameProfileDrawBucket = FrameProfileDrawBucket::None;
#endif
    glBindFramebuffer(GL_FRAMEBUFFER,
        multisampleDraw
            ? g_renderer.multisampleFramebuffer
            : postProcessDraw ? g_renderer.sceneFramebuffer : 0u);

    const double elapsed = static_cast<double>(frame.monotonicMilliseconds) / 1000.0;
    const float wave = static_cast<float>(0.5 + 0.5 * std::sin(elapsed * 0.35));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glClearColor(0.025f + wave * 0.012f, 0.03f, 0.035f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (sceneGeometryDraw)
    {
        const GLint viewportX = static_cast<GLint>(g_renderer.sceneViewX);
        const GLint viewportY = std::max(
            0,
            height - static_cast<GLint>(g_renderer.sceneViewY) -
                static_cast<GLint>(g_renderer.sceneViewHeight));
        glViewport(
            viewportX,
            viewportY,
            static_cast<GLsizei>(g_renderer.sceneViewWidth),
            static_cast<GLsizei>(g_renderer.sceneViewHeight));
    }
    if (sceneGeometryDraw && g_renderer.retainedSky.active &&
        g_renderer.retainedSky.texture != 0u &&
        g_renderer.skyProgram != 0u)
    {
#if KISAK_WEB_DIAGNOSTICS
        const double skyProfileStarted = frameProfile
            ? WebFrameProfile_Now() : 0.0;
        g_frameProfileDrawBucket = FrameProfileDrawBucket::None;
#endif
        glUseProgram(g_renderer.skyProgram);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glUniform1i(g_renderer.skyTextureUniform, 4);
        glUniform2fv(g_renderer.skyTanHalfFovUniform, 1,
            g_renderer.sceneTanHalfFov.data());
        glUniform3fv(g_renderer.skyForwardUniform, 1,
            g_renderer.sceneViewAxis.data());
        glUniform3fv(g_renderer.skyRightUniform, 1,
            g_renderer.sceneViewAxis.data() + 3u);
        glUniform3fv(g_renderer.skyUpUniform, 1,
            g_renderer.sceneViewAxis.data() + 6u);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_CUBE_MAP,
            g_renderer.retainedSky.texture);
        glBindVertexArray(g_renderer.vertexArray);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
#if KISAK_WEB_DIAGNOSTICS
        if (frameProfile)
            frameProfile->skyMs += WebFrameProfile_Now() - skyProfileStarted;
#endif
    }
    // D3D9 and WebGL disagree on front-face conventions. The canonical
    // surface order is preserved, but culling stays disabled for this initial
    // untextured backend until material state owns the intended cull mode.
    glDisable(GL_CULL_FACE);
    // The retained retail substitution expects an engine image sampler. A
    // canonical world batch that deliberately carries no texture binding must
    // stay on the vertex-color pipeline; sampling the backend's 1x1 recovery
    // fallback would turn valid geometry into a uniform white frame.
    const bool compatibilityDraw = g_renderer.compatibilityActive &&
        g_renderer.compatibilityProgram != 0 &&
        g_renderer.draw.textureBinding == WebRendererTextureBinding::EngineImage;
    constexpr GLfloat IDENTITY_MATRIX[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    const GLfloat *const viewProjection = sceneGeometryDraw
        ? g_renderer.sceneViewProjection.data()
        : IDENTITY_MATRIX;
    if (compatibilityDraw)
    {
        while (glGetError() != GL_NO_ERROR)
        {
        }
        glUseProgram(g_renderer.compatibilityProgram);
        glUniformMatrix4fv(
            g_renderer.compatibilityViewProjectionUniform,
            1,
            GL_FALSE,
            viewProjection);
        glUniformMatrix4fv(
            g_renderer.compatibilityWorldUniform,
            1,
            GL_FALSE,
            IDENTITY_MATRIX);
        glUniform1i(g_renderer.compatibilityTextureUniform, 0);
    }
    else
    {
        glUseProgram(g_renderer.program);
        glUniform1f(
            g_renderer.aspectUniform,
            sceneGeometryDraw
                ? 1.0f
                : (height > 0
                    ? static_cast<float>(width) / static_cast<float>(height)
                    : 1.0f));
        glUniformMatrix4fv(
            g_renderer.viewProjectionUniform,
            1,
            GL_FALSE,
            viewProjection);
        glUniform1f(
            g_renderer.sceneFallbackUniform,
            sceneGeometryDraw ? 1.0f : 0.0f);
        glUniform1i(g_renderer.textureUniform, 0);
        glUniform1i(g_renderer.detailMapUniform, 4);
        glUniform1i(g_renderer.normalMapUniform, 1);
        glUniform1i(g_renderer.specularMapUniform, 5);
        glUniform1i(g_renderer.secondaryLightmapUniform, 2);
        glUniform1i(g_renderer.modelLightingUniform, 3);
        glUniform1i(g_renderer.shadowMapUniform, 12);
        glUniform1i(g_renderer.shadowFarMapUniform, 13);
        glUniform1i(g_renderer.waterMapUniform, 7);
        glUniform1i(g_renderer.reflectionProbeUniform, 8);
        glUniform1i(g_renderer.primaryLightmapUniform, 9);
        glUniform1i(g_renderer.spotShadowMapUniform, 14);
        glUniformMatrix4fv(g_renderer.shadowMatrixUniform, 1, GL_FALSE,
            g_renderer.sceneSunShadowMatrix.data());
        glUniformMatrix4fv(g_renderer.shadowFarMatrixUniform, 1, GL_FALSE,
            g_renderer.sceneSunShadowFarMatrix.data());
        glUniform3fv(g_renderer.sunDirectionUniform, 1,
            g_renderer.sceneSunDirection.data());
        glUniform3fv(g_renderer.sunColorUniform, 1,
            g_renderer.sceneSunColor.data());
        glUniform1f(g_renderer.sunShadowEnabledUniform, 0.0f);
        glUniform1f(g_renderer.primaryLightEnabledUniform, 0.0f);
        glUniform1f(g_renderer.spotShadowEnabledUniform, 0.0f);
        glActiveTexture(GL_TEXTURE12);
        glBindTexture(GL_TEXTURE_2D,
            g_renderer.shadowDepthTexture);
        glActiveTexture(GL_TEXTURE13);
        glBindTexture(GL_TEXTURE_2D,
            g_renderer.shadowFarDepthTexture);
        // sampler2DShadow units must remain depth-comparison complete even
        // while the corresponding branch is disabled. A color fallback (or
        // an unbound spot unit) makes every WebGL draw invalid before dynamic
        // branching is considered.
        glActiveTexture(GL_TEXTURE14);
        glBindTexture(GL_TEXTURE_2D,
            g_renderer.spotShadowDepthTextures[0]);
        glActiveTexture(GL_TEXTURE0);
        glUniform1f(g_renderer.secondaryLightmapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.detailMapEnabledUniform, 0.0f);
        glUniform4f(g_renderer.detailScaleUniform,
            0.0f, 0.0f, 0.0f, 0.0f);
        glUniform1f(g_renderer.modelLightingEnabledUniform, 0.0f);
        glUniform1f(g_renderer.normalMapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.specularMapEnabledUniform, 0.0f);
        glUniform3f(g_renderer.modelLightingBaseCoordinatesUniform,
            0.0f, 0.0f, 0.0f);
        glUniform3f(g_renderer.modelLightingLookupScaleUniform,
            0.0f, 0.0f, 0.0f);
        glUniform1f(g_renderer.premultiplyAlphaUniform, 0.0f);
        glUniform1f(g_renderer.colorIntensityAlphaUniform, 0.0f);
        glUniform1i(g_renderer.materialModeUniform, 0);
        glUniform4f(g_renderer.envMapParmsUniform,
            0.0f, 0.0f, 0.0f, 0.0f);
        glUniform4f(g_renderer.waterColorUniform,
            0.0f, 0.0f, 0.0f, 1.0f);
        glUniform1f(g_renderer.instanceEnabledUniform, 0.0f);
        glUniform4f(g_renderer.uiColorUniform, 1.0f, 1.0f, 1.0f, 1.0f);
        glUniform1f(g_renderer.fogEnabledUniform,
            sceneGeometryDraw && g_renderer.sceneFogEnabled ? 1.0f : 0.0f);
        glUniform3fv(g_renderer.viewOriginUniform, 1,
            g_renderer.sceneViewOrigin.data());
        glUniform3fv(g_renderer.fogColorUniform, 1,
            g_renderer.sceneFogColor.data());
        glUniform2fv(g_renderer.fogParamsUniform, 1,
            g_renderer.sceneFogParams.data());
    }
    glBindVertexArray(g_renderer.vertexArray);
    const bool firstSceneDrawPending = sceneGeometryDraw &&
        !g_renderer.sceneViewFirstDrawCompleted;
    if (firstSceneDrawPending)
    {
        while (glGetError() != GL_NO_ERROR)
        {
        }
    }
    std::uint32_t completedDraws = 0u;
    const bool worldBatchDraw = sceneGeometryDraw &&
        g_renderer.worldSurfaceActive && !compatibilityDraw;
#if KISAK_WEB_DIAGNOSTICS
    const double worldProfileStarted = frameProfile
        ? WebFrameProfile_Now() : 0.0;
    g_frameProfileDrawBucket = FrameProfileDrawBucket::World;
#endif
    if (worldBatchDraw)
    {
        for (WebRendererRetainedWorldBatch &batch :
             g_renderer.retainedWorldBatches)
        {
            if (WebRenderer_SkipsNativeDraw(batch.technique))
                continue;
            ApplyWorldMaterialState(batch);
            const WebRendererRetainedWorldImage *base =
                WorldImage(batch.baseImageIndex);
            const WebRendererRetainedWorldImage *detail =
                WorldImage(batch.detailImageIndex);
            const WebRendererRetainedWorldImage *secondaryLightmap =
                WorldImage(batch.secondaryLightmapImageIndex);
            const WebRendererRetainedWorldImage *primaryLightmap =
                WorldImage(batch.lightmapImageIndex);
            const WebRendererRetainedWorldImage *normal =
                WorldImage(batch.normalImageIndex);
            const WebRendererRetainedWorldImage *specular =
                WorldImage(batch.specularImageIndex);
            const bool water = batch.technique ==
                WebRendererWorldTechnique::WaterLitSun;
            const bool waterReady = water && BindWaterTextures(
                batch, g_renderer.sceneViewTimeSeconds);
            const bool fallback = water
                ? !waterReady
                : batch.technique ==
                        WebRendererWorldTechnique::BackendFallback || !base;
            const bool detailMapped = !fallback && detail &&
                detail->texture != 0u;
            const bool lightmapped = !fallback && secondaryLightmap &&
                WebRenderer_UsesSecondaryDirectionalLightmap(
                    batch.technique) &&
                batch.lightingMode ==
                    WebRendererWorldLightingMode::SecondaryDirectional;
            const bool normalMapped = lightmapped && normal &&
                WebRenderer_UsesWorldNormalMap(batch.technique);
            const bool specularMapped = lightmapped && specular &&
                batch.reflectionTexture != 0u &&
                WebRenderer_UsesWorldSpecularMap(batch.technique);
            const WebRendererRetainedPrimaryLight *primaryLight =
                batch.primaryLightIndex <
                        g_renderer.retainedPrimaryLights.size()
                ? &g_renderer.retainedPrimaryLights[
                    batch.primaryLightIndex]
                : nullptr;
            const bool primaryLit = lightmapped && primaryLightmap &&
                primaryLight && primaryLight->type == 2u &&
                primaryLight->falloffScale > 0.0f &&
                batch.techniqueType == 10u &&
                batch.pixelShaderName.rfind("lm_spot_", 0u) == 0u;
            glUniform1f(g_renderer.sceneFallbackUniform,
                fallback ? 1.0f : 0.0f);
            glUniform1f(g_renderer.textureEnabledUniform,
                fallback || water ? 0.0f : 1.0f);
            if (waterReady)
            {
                glUniform4fv(g_renderer.envMapParmsUniform, 1,
                    batch.envMapParms);
                glUniform4fv(g_renderer.waterColorUniform, 1,
                    batch.waterColor);
            }
            else if (water)
            {
                glUniform1i(g_renderer.materialModeUniform, 0);
            }
            else if (specularMapped)
            {
                glUniform4fv(g_renderer.envMapParmsUniform, 1,
                    batch.envMapParms);
                glActiveTexture(GL_TEXTURE8);
                glBindTexture(GL_TEXTURE_CUBE_MAP,
                    batch.reflectionTexture);
                glActiveTexture(GL_TEXTURE0);
            }
            else
            {
                glUniform4f(g_renderer.envMapParmsUniform,
                    0.0f, 0.0f, 0.0f, 0.0f);
            }
            glUniform1f(g_renderer.lightmapEnabledUniform,
                lightmapped ? 1.0f : 0.0f);
            glUniform1f(g_renderer.secondaryLightmapEnabledUniform,
                lightmapped ? 1.0f : 0.0f);
            glUniform1f(g_renderer.modelLightingEnabledUniform, 0.0f);
            glUniform1f(g_renderer.detailMapEnabledUniform,
                detailMapped ? 1.0f : 0.0f);
            glUniform4fv(g_renderer.detailScaleUniform, 1,
                batch.detailScale);
            glUniform1f(g_renderer.normalMapEnabledUniform,
                normalMapped ? 1.0f : 0.0f);
            glUniform1f(g_renderer.specularMapEnabledUniform,
                specularMapped ? 1.0f : 0.0f);
            glUniform1f(g_renderer.sunShadowEnabledUniform,
                shadowMapDrawn && lightmapped &&
                        batch.techniqueType == 9u
                    ? 1.0f : 0.0f);
            glUniform1f(g_renderer.primaryLightEnabledUniform,
                primaryLit ? 1.0f : 0.0f);
            if (primaryLit)
            {
                glUniform4f(g_renderer.primaryLightPositionRadiusUniform,
                    primaryLight->origin[0], primaryLight->origin[1],
                    primaryLight->origin[2], 1.0f / primaryLight->radius);
                glUniform3fv(g_renderer.primaryLightDiffuseUniform, 1,
                    primaryLight->color);
                glUniform3fv(g_renderer.primaryLightSpotDirectionUniform, 1,
                    primaryLight->direction);
                const float spotScale = 1.0f /
                    (primaryLight->cosHalfFovInner -
                        primaryLight->cosHalfFovOuter);
                glUniform3f(g_renderer.primaryLightSpotFactorsUniform,
                    spotScale,
                    -spotScale * primaryLight->cosHalfFovOuter,
                    static_cast<float>(primaryLight->exponent));
                glUniform2f(
                    g_renderer.primaryLightFalloffPlacementUniform,
                    primaryLight->falloffScale,
                    primaryLight->falloffShift);
            }
            BindSpotShadowForPrimaryLight(batch.primaryLightIndex,
                primaryLit, spotShadowMapsDrawn);
            BindWorldTexture(
                GL_TEXTURE0,
                base ? base->texture : g_renderer.texture,
                batch.samplerState);
            BindWorldTexture(
                GL_TEXTURE1,
                normal ? normal->texture : g_renderer.texture,
                batch.normalSamplerState);
            BindWorldTexture(
                GL_TEXTURE4,
                detail ? detail->texture : g_renderer.texture,
                batch.detailSamplerState);
            BindWorldTexture(
                GL_TEXTURE2,
                secondaryLightmap
                    ? secondaryLightmap->texture : g_renderer.texture,
                0x62u);
            BindWorldTexture(
                GL_TEXTURE5,
                specular ? specular->texture : g_renderer.texture,
                batch.specularSamplerState);
            BindWorldTexture(
                GL_TEXTURE9,
                primaryLightmap
                    ? primaryLightmap->texture : g_renderer.texture,
                0x62u);
            const std::uintptr_t indexOffset =
                static_cast<std::uintptr_t>(batch.firstIndex) *
                sizeof(std::uint32_t);
            glDrawElements(
                GL_TRIANGLES,
                static_cast<GLsizei>(batch.indexCount),
                GL_UNSIGNED_INT,
                reinterpret_cast<const void *>(indexOffset));
#if KISAK_WEB_DIAGNOSTICS
            if (frameProfile)
                frameProfile->worldSurfacesDrawn += batch.surfaceCount;
#endif
            ++completedDraws;
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
    }
    else
    {
        glActiveTexture(GL_TEXTURE0);
        if (!compatibilityDraw)
        {
            glUniform1f(
                g_renderer.textureEnabledUniform,
                g_renderer.sourceTextureActive &&
                        g_renderer.draw.textureBinding ==
                            WebRendererTextureBinding::EngineImage
                    ? 1.0f : 0.0f);
            glUniform1f(g_renderer.lightmapEnabledUniform, 0.0f);
            glUniform1f(
                g_renderer.secondaryLightmapEnabledUniform, 0.0f);
            glUniform1f(g_renderer.modelLightingEnabledUniform, 0.0f);
            glUniform1f(g_renderer.detailMapEnabledUniform, 0.0f);
            glUniform1f(g_renderer.normalMapEnabledUniform, 0.0f);
            glUniform1f(g_renderer.specularMapEnabledUniform, 0.0f);
            glUniform1i(g_renderer.alphaTestUniform, 0);
            glUniform1f(g_renderer.premultiplyAlphaUniform, 0.0f);
            glUniform1i(g_renderer.materialModeUniform, 0);
        }
        glBindTexture(GL_TEXTURE_2D, g_renderer.texture);
        const std::uintptr_t indexOffset =
            static_cast<std::uintptr_t>(g_renderer.draw.firstIndex) *
            (g_renderer.worldSurfaceActive
                ? sizeof(std::uint32_t)
                : sizeof(std::uint16_t));
        glDrawElements(
            GL_TRIANGLES,
            static_cast<GLsizei>(g_renderer.draw.indexCount),
            g_renderer.worldSurfaceActive ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
            reinterpret_cast<const void *>(indexOffset));
#if KISAK_WEB_DIAGNOSTICS
        if (frameProfile && sceneGeometryDraw && g_renderer.worldSurfaceActive)
            frameProfile->worldSurfacesDrawn +=
                g_renderer.sceneViewSurfaceCount;
#endif
        completedDraws = 1u;
    }
#if KISAK_WEB_DIAGNOSTICS
    if (frameProfile)
        frameProfile->worldMs = WebFrameProfile_Now() - worldProfileStarted;
    const double staticProfileStarted = frameProfile
        ? WebFrameProfile_Now() : 0.0;
    g_frameProfileDrawBucket = FrameProfileDrawBucket::StaticModel;
#endif
    if (sceneGeometryDraw && staticModelLodsReady && !compatibilityDraw &&
        g_renderer.staticModelSceneActive &&
        g_renderer.staticModelVertexArray != 0u &&
        g_renderer.staticModelInstanceBuffer != 0u)
    {
        glBindVertexArray(g_renderer.staticModelVertexArray);
        glUniform1f(g_renderer.sunShadowEnabledUniform, 0.0f);
        glUniform1f(g_renderer.instanceEnabledUniform, 1.0f);
        glUniform1f(g_renderer.lightmapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.secondaryLightmapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.specularMapEnabledUniform, 0.0f);
        BindModelLightingTexture(g_renderer.retainedStaticModelLighting);
        for (const WebRendererRetainedStaticModelBatch &batch :
             g_renderer.retainedStaticModelBatches)
        {
            // R_AddXModelSurfacesCamera reserves camera region 3 for geometry
            // that participates in shadow passes only (tree shadow facades
            // are the common retail example).
            if (!WebRenderer_IsCameraVisibleXModelSurface(
                    batch.draw.sourceKind, batch.draw.cameraRegion))
                continue;
            ApplyWorldMaterialState(batch.draw);
            const WebRendererRetainedWorldImage *base = RetainedImage(
                g_renderer.retainedStaticModelImages,
                batch.draw.baseImageIndex);
            const WebRendererRetainedWorldImage *detail = RetainedImage(
                g_renderer.retainedStaticModelImages,
                batch.draw.detailImageIndex);
            const WebRendererRetainedWorldImage *normal = RetainedImage(
                g_renderer.retainedStaticModelImages,
                batch.draw.normalImageIndex);
            const WebRendererRetainedWorldImage *specular = RetainedImage(
                g_renderer.retainedStaticModelImages,
                batch.draw.specularImageIndex);
            const bool fallback = batch.draw.technique ==
                    WebRendererWorldTechnique::BackendFallback ||
                !base;
            const bool modelLit = !fallback &&
                batch.draw.lightingMode ==
                    WebRendererWorldLightingMode::ModelLightGrid &&
                g_renderer.retainedStaticModelLighting.texture != 0u;
            const bool detailMapped = !fallback && detail &&
                detail->texture != 0u;
            const bool normalMapped = modelLit && normal &&
                normal->texture != 0u;
            const bool specularMapped = modelLit && specular &&
                specular->texture != 0u &&
                batch.draw.reflectionTexture != 0u &&
                WebRenderer_UsesModelEnvironmentSpecular(
                    batch.draw.technique);
            const WebRendererRetainedPrimaryLight *primaryLight =
                batch.draw.primaryLightIndex <
                        g_renderer.retainedPrimaryLights.size()
                ? &g_renderer.retainedPrimaryLights[
                    batch.draw.primaryLightIndex]
                : nullptr;
            const bool directionalPrimaryLit = modelLit && primaryLight &&
                primaryLight->type == 1u;
            glUniform1f(g_renderer.sceneFallbackUniform,
                fallback ? 1.0f : 0.0f);
            glUniform1f(g_renderer.textureEnabledUniform,
                fallback ? 0.0f : 1.0f);
            glUniform1f(g_renderer.modelLightingEnabledUniform,
                modelLit ? 1.0f : 0.0f);
            glUniform1f(g_renderer.detailMapEnabledUniform,
                detailMapped ? 1.0f : 0.0f);
            glUniform4fv(g_renderer.detailScaleUniform, 1,
                batch.draw.detailScale);
            glUniform1f(g_renderer.normalMapEnabledUniform,
                normalMapped ? 1.0f : 0.0f);
            glUniform1f(g_renderer.specularMapEnabledUniform,
                specularMapped ? 1.0f : 0.0f);
            glUniform1f(g_renderer.primaryLightEnabledUniform,
                directionalPrimaryLit ? 1.0f : 0.0f);
            if (specularMapped)
            {
                glUniform4fv(g_renderer.envMapParmsUniform, 1,
                    batch.draw.envMapParms);
                glActiveTexture(GL_TEXTURE8);
                glBindTexture(GL_TEXTURE_CUBE_MAP,
                    batch.draw.reflectionTexture);
                glActiveTexture(GL_TEXTURE0);
            }
            else
                glUniform4f(g_renderer.envMapParmsUniform,
                    0.0f, 0.0f, 0.0f, 0.0f);
            BindWorldTexture(
                GL_TEXTURE0,
                base ? base->texture : g_renderer.texture,
                batch.draw.samplerState,
                base && base->mipmapsAllowed);
            BindWorldTexture(
                GL_TEXTURE1,
                normal ? normal->texture : g_renderer.texture,
                batch.draw.normalSamplerState);
            BindWorldTexture(
                GL_TEXTURE4,
                detail ? detail->texture : g_renderer.texture,
                batch.draw.detailSamplerState);
            BindWorldTexture(
                GL_TEXTURE5,
                specular ? specular->texture : g_renderer.texture,
                batch.draw.specularSamplerState);
            if (batch.instanceCount == 0u) continue;
            BindStaticModelInstanceRange(batch.instanceOffset);
            const std::uintptr_t indexOffset =
                static_cast<std::uintptr_t>(batch.draw.firstIndex) *
                sizeof(std::uint32_t);
            glDrawElementsInstanced(
                GL_TRIANGLES,
                static_cast<GLsizei>(batch.draw.indexCount),
                GL_UNSIGNED_INT,
                reinterpret_cast<const void *>(indexOffset),
                static_cast<GLsizei>(batch.instanceCount));
            ++completedDraws;
        }
        glUniform1f(g_renderer.instanceEnabledUniform, 0.0f);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
    }
#if KISAK_WEB_DIAGNOSTICS
    if (frameProfile)
        frameProfile->staticModelsMs =
            WebFrameProfile_Now() - staticProfileStarted;
    g_frameProfileDrawBucket = FrameProfileDrawBucket::DynamicModel;
#endif
    if (sceneGeometryDraw && !compatibilityDraw &&
        g_renderer.dynamicModelSceneActive &&
        g_renderer.dynamicModelVertexArray != 0u)
    {
        glBindVertexArray(g_renderer.dynamicModelVertexArray);
        glUniform1f(g_renderer.sunShadowEnabledUniform, 0.0f);
        glUniform1f(g_renderer.instanceEnabledUniform, 0.0f);
        glUniform1f(g_renderer.lightmapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.secondaryLightmapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.specularMapEnabledUniform, 0.0f);
        BindModelLightingTexture(g_renderer.retainedDynamicModelLighting);
        // The portable command combines ordinary DObjs, moving brush models,
        // DynEnts, and first-person DObjs in one buffer. Native draw-surf
        // generation keeps depth-hacked first-person surfaces in their camera
        // pass; drawing the append order directly lets a later moving brush
        // overwrite transparent viewmodel surfaces such as the G36C reflex
        // dot (which intentionally does not write depth). Draw ordinary scene
        // geometry first, then the reserved-depth first-person pass.
        for (std::uint32_t cameraPass = 0u; cameraPass < 2u; ++cameraPass)
        {
            const bool depthHackPass = cameraPass != 0u;
            for (const WebRendererRetainedWorldBatch &batch :
                 g_renderer.retainedDynamicModelBatches)
            {
                if (batch.depthHack != depthHackPass) continue;
                if (!WebRenderer_IsCameraVisibleXModelSurface(
                        batch.sourceKind, batch.cameraRegion))
                    continue;
#if KISAK_WEB_DIAGNOSTICS
                const double dynamicBatchProfileStarted = frameProfile
                    ? WebFrameProfile_Now() : 0.0;
                g_frameProfileDrawBucket = ProfileBucketForKind(
                    batch.sourceKind);
#endif
                if (batch.sourceKind == WebRendererSceneBatchKind::SunFlare)
                {
                    if (depthHackPass) continue;
                    if (g_renderer.sunVisibilityQueries[0] == 0u &&
                        g_renderer.sunVisibilityQueries[1] == 0u)
                    {
                        glGenQueries(2, g_renderer.sunVisibilityQueries);
                    }
                    if (g_renderer.sunVisibilityQueries[0] == 0u &&
                        g_renderer.sunVisibilityQueries[1] == 0u)
                    {
                        // Match RB_UpdateSunVisibilityWithoutQuery when a
                        // backend cannot supply asynchronous query objects.
                        g_renderer.sunVisibility =
                            batch.falloffParms[3] > 0.5f ? 1.0f : 0.0f;
                        continue;
                    }
                    const std::uint32_t queryIndex =
                        g_renderer.sunVisibilityQueryIndex++ & 1u;
                    const GLuint query =
                        g_renderer.sunVisibilityQueries[queryIndex];
                    bool canIssue = query != 0u;
                    if (canIssue &&
                        g_renderer.sunVisibilityQueryIssued[queryIndex])
                    {
                        GLuint available = GL_FALSE;
                        glGetQueryObjectuiv(query,
                            GL_QUERY_RESULT_AVAILABLE, &available);
                        if (available == GL_TRUE)
                        {
                            GLuint passed = GL_FALSE;
                            glGetQueryObjectuiv(query,
                                GL_QUERY_RESULT, &passed);
                            const int queryVisibility =
                                passed != GL_FALSE ? 1 : 0;
                            const int collisionVisibility =
                                batch.falloffParms[3] > 0.5f ? 1 : 0;
                            g_renderer.sunVisibility =
                                queryVisibility ? 1.0f : 0.0f;
                            static int reportedSunVisibility = -1;
                            const int visibility =
                                g_renderer.sunVisibility > 0.5f ? 1 : 0;
                            if (reportedSunVisibility != visibility)
                            {
                                reportedSunVisibility = visibility;
                                Web_Log(WebLogLevel::Info,
                                    "[kisakcod-web] Canonical asynchronous "
                                    "sun visibility: visible=%d "
                                    "query=%d collision=%d.\n",
                                    visibility, queryVisibility,
                                    collisionVisibility);
                            }
                            g_renderer.sunVisibilityQueryIssued[queryIndex] =
                                false;
                        }
                        else
                            canIssue = false;
                    }
                    if (canIssue)
                    {
                        glUniformMatrix4fv(g_renderer.viewProjectionUniform,
                            1, GL_FALSE, IDENTITY_MATRIX);
                        glUniform1f(g_renderer.aspectUniform, 1.0f);
                        glUniform1f(g_renderer.sceneFallbackUniform, 0.0f);
                        glUniform1f(g_renderer.textureEnabledUniform, 0.0f);
                        glUniform1f(g_renderer.fogEnabledUniform, 0.0f);
                        glUniform1i(g_renderer.materialModeUniform, 0);
                        glUniform1i(g_renderer.alphaTestUniform, 0);
                        glEnable(GL_DEPTH_TEST);
                        glDepthFunc(GL_LEQUAL);
                        glDepthMask(GL_FALSE);
                        glDisable(GL_CULL_FACE);
                        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                        const int centerX = static_cast<int>(
                            batch.falloffParms[0] *
                            static_cast<float>(width));
                        const int centerY = static_cast<int>(
                            batch.falloffParms[1] *
                            static_cast<float>(height));
                        const int queryX = std::clamp(centerX - 8,
                            0, std::max(0, width - 16));
                        const int queryY = std::clamp(centerY - 8,
                            0, std::max(0, height - 16));
                        glEnable(GL_SCISSOR_TEST);
                        glScissor(queryX, queryY,
                            std::min(16, width), std::min(16, height));
                        glBeginQuery(
                            GL_ANY_SAMPLES_PASSED_CONSERVATIVE, query);
#if KISAK_WEB_DIAGNOSTICS
                        g_frameProfileDrawBucket =
                            FrameProfileDrawBucket::Query;
#endif
                        const std::uintptr_t queryOffset =
                            static_cast<std::uintptr_t>(batch.firstIndex) *
                            sizeof(std::uint32_t);
                        glDrawElements(GL_TRIANGLES,
                            static_cast<GLsizei>(batch.indexCount),
                            GL_UNSIGNED_INT,
                            reinterpret_cast<const void *>(queryOffset));
                        glEndQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE);
                        g_renderer.sunVisibilityQueryIssued[queryIndex] = true;
                        glDisable(GL_SCISSOR_TEST);
                        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                        glDepthMask(GL_TRUE);
                        ++completedDraws;
                    }
#if KISAK_WEB_DIAGNOSTICS
                    AddProfileDynamicTime(batch.sourceKind,
                        WebFrameProfile_Now() - dynamicBatchProfileStarted);
#endif
                    continue;
                }
                const bool sunSprite = batch.sourceKind ==
                    WebRendererSceneBatchKind::SunSprite;
                glUniformMatrix4fv(g_renderer.viewProjectionUniform, 1,
                    GL_FALSE,
                    sunSprite
                        ? IDENTITY_MATRIX
                        : batch.depthHack
                        ? g_renderer.sceneDepthHackViewProjection.data()
                        : g_renderer.sceneViewProjection.data());
                glDepthRangef(0.0f, batch.depthHack ? 0.015625f : 1.0f);
                ApplyWorldMaterialState(batch);
                const WebRendererRetainedWorldImage *base = RetainedImage(
                    g_renderer.retainedDynamicModelImages,
                    batch.baseImageIndex);
                const WebRendererRetainedWorldImage *detail = RetainedImage(
                    g_renderer.retainedDynamicModelImages,
                    batch.detailImageIndex);
                const WebRendererRetainedWorldImage *secondaryLightmap =
                    RetainedImage(g_renderer.retainedDynamicModelImages,
                        batch.secondaryLightmapImageIndex);
                const WebRendererRetainedWorldImage *primaryLightmap =
                    RetainedImage(g_renderer.retainedDynamicModelImages,
                        batch.lightmapImageIndex);
                const WebRendererRetainedWorldImage *normal = RetainedImage(
                    g_renderer.retainedDynamicModelImages,
                    batch.normalImageIndex);
                const WebRendererRetainedWorldImage *specular = RetainedImage(
                    g_renderer.retainedDynamicModelImages,
                    batch.specularImageIndex);
                const WebRendererRetainedPrimaryLight *primaryLight =
                    batch.primaryLightIndex <
                            g_renderer.retainedPrimaryLights.size()
                    ? &g_renderer.retainedPrimaryLights[
                        batch.primaryLightIndex]
                    : nullptr;
                const bool fxSceneGeometry =
                    WebRenderer_IsFxVertexColorBatch(batch.sourceKind);
                const bool fallback = batch.technique ==
                        WebRendererWorldTechnique::BackendFallback ||
                    !base;
                const bool dynamicLightmapped = !fallback &&
                    secondaryLightmap &&
                    (batch.sourceKind ==
                            WebRendererSceneBatchKind::FxMarkMesh ||
                     batch.sourceKind ==
                            WebRendererSceneBatchKind::DynamicBModel) &&
                    WebRenderer_UsesSecondaryDirectionalLightmap(
                        batch.technique) &&
                    batch.lightingMode ==
                        WebRendererWorldLightingMode::SecondaryDirectional;
                const bool modelLit = !fallback && !fxSceneGeometry &&
                    batch.lightingMode ==
                        WebRendererWorldLightingMode::ModelLightGrid &&
                    g_renderer.retainedDynamicModelLighting.texture != 0u;
                const bool detailMapped = !fallback && detail &&
                    detail->texture != 0u;
                const bool normalMapped = normal && normal->texture != 0u &&
                    (modelLit || (dynamicLightmapped &&
                        WebRenderer_UsesWorldNormalMap(batch.technique)));
                const bool specularMapped = modelLit && specular &&
                    specular->texture != 0u &&
                    batch.reflectionTexture != 0u &&
                    WebRenderer_UsesModelEnvironmentSpecular(
                        batch.technique);
                const bool primaryLit = dynamicLightmapped &&
                    primaryLightmap && primaryLight &&
                    primaryLight->type == 2u &&
                    primaryLight->falloffScale > 0.0f &&
                    batch.techniqueType == 10u &&
                    batch.pixelShaderName.rfind("lm_spot_", 0u) == 0u;
                const bool directionalPrimaryLit = modelLit && primaryLight &&
                    primaryLight->type == 1u;
                glUniform1f(g_renderer.fogEnabledUniform,
                    g_renderer.sceneFogEnabled && !fxSceneGeometry &&
                        !sunSprite
                        ? 1.0f : 0.0f);
                glUniform1f(g_renderer.sceneFallbackUniform,
                    fallback && !fxSceneGeometry ? 1.0f : 0.0f);
                glUniform1f(g_renderer.textureEnabledUniform,
                    fallback ? 0.0f : 1.0f);
                glUniform1f(g_renderer.lightmapEnabledUniform,
                    dynamicLightmapped ? 1.0f : 0.0f);
                glUniform1f(g_renderer.secondaryLightmapEnabledUniform,
                    dynamicLightmapped ? 1.0f : 0.0f);
                glUniform1f(g_renderer.modelLightingEnabledUniform,
                    modelLit ? 1.0f : 0.0f);
                glUniform1f(g_renderer.detailMapEnabledUniform,
                    detailMapped ? 1.0f : 0.0f);
                glUniform4fv(g_renderer.detailScaleUniform, 1,
                    batch.detailScale);
                glUniform1f(g_renderer.normalMapEnabledUniform,
                    normalMapped ? 1.0f : 0.0f);
                glUniform1f(g_renderer.specularMapEnabledUniform,
                    specularMapped ? 1.0f : 0.0f);
                if (specularMapped)
                {
                    glUniform4fv(g_renderer.envMapParmsUniform, 1,
                        batch.envMapParms);
                    glActiveTexture(GL_TEXTURE8);
                    glBindTexture(GL_TEXTURE_CUBE_MAP,
                        batch.reflectionTexture);
                    glActiveTexture(GL_TEXTURE0);
                }
                else
                    glUniform4f(g_renderer.envMapParmsUniform,
                        0.0f, 0.0f, 0.0f, 0.0f);
                glUniform1f(g_renderer.primaryLightEnabledUniform,
                    primaryLit || directionalPrimaryLit ? 1.0f : 0.0f);
                if (primaryLit)
                {
                    glUniform4f(
                        g_renderer.primaryLightPositionRadiusUniform,
                        primaryLight->origin[0], primaryLight->origin[1],
                        primaryLight->origin[2],
                        1.0f / primaryLight->radius);
                    glUniform3fv(g_renderer.primaryLightDiffuseUniform, 1,
                        primaryLight->color);
                    glUniform3fv(
                        g_renderer.primaryLightSpotDirectionUniform, 1,
                        primaryLight->direction);
                    const float spotScale = 1.0f /
                        (primaryLight->cosHalfFovInner -
                            primaryLight->cosHalfFovOuter);
                    glUniform3f(g_renderer.primaryLightSpotFactorsUniform,
                        spotScale,
                        -spotScale * primaryLight->cosHalfFovOuter,
                        static_cast<float>(primaryLight->exponent));
                    glUniform2f(
                        g_renderer.primaryLightFalloffPlacementUniform,
                        primaryLight->falloffScale,
                        primaryLight->falloffShift);
                }
                BindSpotShadowForPrimaryLight(batch.primaryLightIndex,
                    primaryLit, spotShadowMapsDrawn);
                glUniform3fv(g_renderer.modelLightingBaseCoordinatesUniform,
                    1, batch.modelLightingCoordinates);
                if (sunSprite)
                {
                    // Native visibility is derived from an occlusion query.
                    // The portable command sits at D3D far depth, so the
                    // already-populated WebGL depth buffer provides the same
                    // receiver test without exposing a query object across
                    // the renderer boundary.
                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LEQUAL);
                    glDepthMask(GL_FALSE);
                }
                BindWorldTexture(GL_TEXTURE0,
                    base ? base->texture : g_renderer.texture,
                    batch.samplerState);
                BindWorldTexture(GL_TEXTURE1,
                    normal ? normal->texture : g_renderer.texture,
                    batch.normalSamplerState);
                BindWorldTexture(GL_TEXTURE4,
                    detail ? detail->texture : g_renderer.texture,
                    batch.detailSamplerState);
                BindWorldTexture(GL_TEXTURE5,
                    specular ? specular->texture : g_renderer.texture,
                    batch.specularSamplerState);
                BindWorldTexture(GL_TEXTURE2,
                    secondaryLightmap
                        ? secondaryLightmap->texture : g_renderer.texture,
                    0x62u);
                BindWorldTexture(GL_TEXTURE9,
                    primaryLightmap
                        ? primaryLightmap->texture : g_renderer.texture,
                    0x62u);
                const std::uintptr_t indexOffset =
                    static_cast<std::uintptr_t>(batch.firstIndex) *
                    sizeof(std::uint32_t);
                glDrawElements(GL_TRIANGLES,
                    static_cast<GLsizei>(batch.indexCount),
                    GL_UNSIGNED_INT,
                    reinterpret_cast<const void *>(indexOffset));
#if KISAK_WEB_DIAGNOSTICS
                AddProfileDynamicTime(batch.sourceKind,
                    WebFrameProfile_Now() - dynamicBatchProfileStarted);
#endif
                if (sunSprite) glDepthMask(GL_TRUE);
                ++completedDraws;
            }
        }
        glDepthRangef(0.0f, 1.0f);
        glUniformMatrix4fv(g_renderer.viewProjectionUniform, 1, GL_FALSE,
            g_renderer.sceneViewProjection.data());
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
    }
    if (sceneGeometryDraw && !compatibilityDraw)
        UpdateSunPostEffectState();
#if KISAK_WEB_DIAGNOSTICS
    const double postProfileStarted = frameProfile
        ? WebFrameProfile_Now() : 0.0;
    g_frameProfileDrawBucket = FrameProfileDrawBucket::PostProcess;
#endif
    if (multisampleDraw)
    {
        // Native COD4 resolves the multisampled 3D scene into a texture before
        // post effects and draws 2D afterward. Preserve depth for the DOF pass.
        ResolveMultisampleTarget(
            (postProcessDraw || directAaResolveDraw)
                ? g_renderer.sceneFramebuffer : 0u,
            width,
            height,
            postProcessDraw);
    }
    if (directAaResolveDraw)
    {
        // Chromium's opaque default framebuffer is not required to have the
        // RGBA8 format needed for a legal multisample resolve. Native COD4
        // likewise resolves to an A8R8G8B8 scene texture before presentation.
        DrawPostProcessPass(
            g_renderer.sceneColorTexture,
            0u,
            0u,
            width,
            height,
            false,
            false,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f);
    }
    GLuint glowImage = 0u;
    if (postProcessDraw)
    {
        glowImage = DrawGlowImage(width, height);
        // COD4 resolves and color-manipulates the 3D scene before any 2D
        // commands. Keep the pass separate from the final display gamma so
        // campaign vision tint never contaminates HUD colors.
        DrawPostProcessPass(
            g_renderer.sceneColorTexture,
            glowImage,
            g_renderer.compositeFramebuffer,
            width,
            height,
            g_renderer.sceneFilmEnabled,
            g_renderer.sceneDepthOfField.enabled,
            1.0f,
            g_renderer.sceneBlurRadius,
            g_renderer.sunBlindDarken,
            g_renderer.sunGlareLighten,
            glowImage != 0u
                ? g_renderer.sceneGlowBloomIntensity : 0.0f);
    }
    if (postProcessDraw && sceneGeometryDraw && !compatibilityDraw &&
        g_renderer.dynamicModelSceneActive &&
        g_renderer.dynamicModelVertexArray != 0u)
    {
        glUseProgram(g_renderer.program);
        glUniformMatrix4fv(g_renderer.viewProjectionUniform, 1, GL_FALSE,
            IDENTITY_MATRIX);
        glUniform1f(g_renderer.aspectUniform, 1.0f);
        glUniform1f(g_renderer.sceneFallbackUniform, 0.0f);
        glUniform1f(g_renderer.fogEnabledUniform, 0.0f);
        glUniform1f(g_renderer.lightmapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.secondaryLightmapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.modelLightingEnabledUniform, 0.0f);
        glUniform1f(g_renderer.detailMapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.normalMapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.specularMapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.sunShadowEnabledUniform, 0.0f);
        glUniform1f(g_renderer.primaryLightEnabledUniform, 0.0f);
        glUniform1f(g_renderer.spotShadowEnabledUniform, 0.0f);
        glUniform1f(g_renderer.instanceEnabledUniform, 0.0f);
        glUniform4f(g_renderer.uiColorUniform, 1.0f, 1.0f, 1.0f, 1.0f);
        glBindVertexArray(g_renderer.dynamicModelVertexArray);
        static bool sunFlareDrawReported = false;
        for (const WebRendererRetainedWorldBatch &batch :
             g_renderer.retainedDynamicModelBatches)
        {
            if (batch.sourceKind != WebRendererSceneBatchKind::SunFlare)
                continue;
            ApplyWorldMaterialState(batch);
            const WebRendererRetainedWorldImage *base = RetainedImage(
                g_renderer.retainedDynamicModelImages,
                batch.baseImageIndex);
            glUniform1f(g_renderer.textureEnabledUniform,
                base ? 1.0f : 0.0f);
            glUniform4f(g_renderer.uiColorUniform,
                g_renderer.sunFlareIntensity,
                g_renderer.sunFlareIntensity,
                g_renderer.sunFlareIntensity,
                1.0f);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            BindWorldTexture(GL_TEXTURE0,
                base ? base->texture : g_renderer.texture,
                batch.samplerState);
            const std::uintptr_t indexOffset =
                static_cast<std::uintptr_t>(batch.firstIndex) *
                sizeof(std::uint32_t);
            glDrawElements(GL_TRIANGLES,
                static_cast<GLsizei>(batch.indexCount),
                GL_UNSIGNED_INT,
                reinterpret_cast<const void *>(indexOffset));
            glDepthMask(GL_TRUE);
            if (!sunFlareDrawReported &&
                g_renderer.sunFlareIntensity > 0.0f)
            {
                sunFlareDrawReported = true;
                Web_Log(WebLogLevel::Info,
                    "[kisakcod-web] Canonical sun flare draw: "
                    "material='%s' center=(%.5f %.5f) queryDepth=%.6f "
                    "intensity=%.5f blind=%.5f glare=%.5f "
                    "state=0x%08x/0x%08x.\n",
                    batch.materialName.c_str(),
                    batch.falloffParms[0], batch.falloffParms[1],
                    batch.falloffParms[2],
                    g_renderer.sunFlareIntensity,
                    g_renderer.sunBlindDarken,
                    g_renderer.sunGlareLighten,
                    batch.stateBits[0], batch.stateBits[1]);
            }
            ++completedDraws;
        }
        glActiveTexture(GL_TEXTURE0);
    }
#if KISAK_WEB_DIAGNOSTICS
    if (frameProfile)
        frameProfile->postProcessMs +=
            WebFrameProfile_Now() - postProfileStarted;
    const double uiProfileStarted = frameProfile
        ? WebFrameProfile_Now() : 0.0;
    g_frameProfileDrawBucket = FrameProfileDrawBucket::Ui;
#endif
    if (sceneGeometryDraw && !compatibilityDraw &&
        g_renderer.uiSceneActive && g_renderer.uiVertexArray != 0u)
    {
        glUseProgram(g_renderer.program);
        glUniformMatrix4fv(g_renderer.viewProjectionUniform, 1, GL_FALSE,
            IDENTITY_MATRIX);
        glUniform1f(g_renderer.aspectUniform, 1.0f);
        glUniform1f(g_renderer.sceneFallbackUniform, 0.0f);
        glUniform1f(g_renderer.fogEnabledUniform, 0.0f);
        glUniform1f(g_renderer.lightmapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.secondaryLightmapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.modelLightingEnabledUniform, 0.0f);
        glUniform1f(g_renderer.detailMapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.normalMapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.specularMapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.sunShadowEnabledUniform, 0.0f);
        glUniform1f(g_renderer.instanceEnabledUniform, 0.0f);
        glBindVertexArray(g_renderer.uiVertexArray);
        for (const WebRendererRetainedUiBatch &batch :
             g_renderer.retainedUiBatches)
        {
            ApplyUiMaterialState(batch);
            const WebRendererRetainedWorldImage *image = RetainedImage(
                g_renderer.retainedUiImages, batch.imageIndex);
            glUniform1f(g_renderer.textureEnabledUniform,
                image ? 1.0f : 0.0f);
            glUniform4fv(g_renderer.uiColorUniform, 1, batch.color);
            BindWorldTexture(GL_TEXTURE0,
                image ? image->texture : g_renderer.texture,
                batch.samplerState);
            const std::uintptr_t indexOffset =
                static_cast<std::uintptr_t>(batch.firstIndex) *
                sizeof(std::uint32_t);
            glDrawElements(GL_TRIANGLES,
                static_cast<GLsizei>(batch.indexCount), GL_UNSIGNED_INT,
                reinterpret_cast<const void *>(indexOffset));
            ++completedDraws;
        }
        glUniform4f(g_renderer.uiColorUniform, 1.0f, 1.0f, 1.0f, 1.0f);
        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
    }
#if KISAK_WEB_DIAGNOSTICS
    if (frameProfile)
        frameProfile->uiMs = WebFrameProfile_Now() - uiProfileStarted;
    rendererStageStarted = frameProfile ? WebFrameProfile_Now() : 0.0;
    g_frameProfileDrawBucket = FrameProfileDrawBucket::PostProcess;
#endif
    if (postProcessDraw)
    {
        // Native R_CalcGammaRamp affects the finished frame, including 2D.
        // The browser has no D3D hardware ramp, so reproduce it only at this
        // final framebuffer boundary.
        DrawPostProcessPass(
            g_renderer.compositeColorTexture,
            0u,
            0u,
            width,
            height,
            false,
            false,
            g_renderer.sceneDisplayGammaExponent,
            0.0f,
            0.0f,
            0.0f,
            0.0f);
    }
#if KISAK_WEB_DIAGNOSTICS
    if (frameProfile)
        frameProfile->postProcessMs +=
            WebFrameProfile_Now() - rendererStageStarted;
    EndFrameProfileGpuQuery();
    g_frameProfileDrawBucket = FrameProfileDrawBucket::None;
#endif
    GLenum sceneDrawError = GL_NO_ERROR;
    if (firstSceneDrawPending)
    {
        sceneDrawError = glGetError();
        if (sceneDrawError == GL_NO_ERROR)
        {
            g_renderer.sceneViewFirstDrawCompleted = true;
            g_renderer.sceneViewDrawnSubmissionGeneration =
                g_renderer.sceneViewSubmissionGeneration;
            DispatchRendererSceneFrame(
                g_renderer.sceneViewWorldName.c_str(),
                g_renderer.sceneViewDrawnSubmissionGeneration,
                g_renderer.sceneViewSurfaceSubmissionGeneration,
                g_renderer.surfaceResourceGeneration,
                g_renderer.sceneViewSurfaceCount,
                g_renderer.sceneViewVertexCount,
                g_renderer.sceneViewIndexCount);
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] First cgame-driven %s frame rendered "
                "through WebGL2 (%u surfaces, %u indices).\n",
                g_renderer.sceneViewWorldName.c_str(),
                g_renderer.sceneViewSurfaceCount,
                g_renderer.sceneViewIndexCount);
        }
        else
        {
            Web_Log(WebLogLevel::Error,
                "[kisakcod-web] Cgame-driven WebGL2 draw failed (0x%x).\n",
                static_cast<unsigned int>(sceneDrawError));
        }
    }
#if KISAK_WEB_DIAGNOSTICS
    else if (sceneGeometryDraw &&
        g_renderer.sceneViewDrawnSubmissionGeneration !=
            g_renderer.sceneViewSubmissionGeneration)
    {
        // Retail validation needs one event per newly submitted canonical view
        // to measure sustained frames. Keep that Worker-message traffic out of
        // the production artifact; production retains the first-draw proof.
        g_renderer.sceneViewDrawnSubmissionGeneration =
            g_renderer.sceneViewSubmissionGeneration;
        DispatchRendererSceneFrame(
            g_renderer.sceneViewWorldName.c_str(),
            g_renderer.sceneViewDrawnSubmissionGeneration,
            g_renderer.sceneViewSurfaceSubmissionGeneration,
            g_renderer.surfaceResourceGeneration,
            g_renderer.sceneViewSurfaceCount,
            g_renderer.sceneViewVertexCount,
            g_renderer.sceneViewIndexCount);
    }
#endif

    if (g_renderer.surfaceDrawnSubmissionGeneration !=
        g_renderer.surfaceSubmissionGeneration)
    {
        g_renderer.surfaceDrawnSubmissionGeneration =
            g_renderer.surfaceSubmissionGeneration;
        EmitSurfaceDraw();
    }

    if (compatibilityDraw)
    {
        if (UINT32_MAX - g_renderer.compatibilityDrawCount < completedDraws)
            g_renderer.compatibilityDrawCount = UINT32_MAX;
        else
            g_renderer.compatibilityDrawCount += completedDraws;
        if (!g_renderer.compatibilityFirstDrawCompleted && completedDraws != 0u)
        {
            const GLenum drawError = firstSceneDrawPending
                ? sceneDrawError
                : glGetError();
            if (drawError == GL_NO_ERROR)
            {
                g_renderer.compatibilityFirstDrawCompleted = true;
                Web_Log(
                    WebLogLevel::Info,
                    "[kisakcod-web] First draw completed through %s.\n",
                    g_renderer.compatibilityId.c_str());
                EmitShaderLifecycle(
                    "ready",
                    "The deterministic indexed draw completed through the selected COD4 shader contract");
            }
            else
            {
                Web_Log(
                    WebLogLevel::Error,
                    "[kisakcod-web] WebGL2 compatibility draw failed (0x%x).\n",
                    static_cast<unsigned int>(drawError));
                glDeleteProgram(g_renderer.compatibilityProgram);
                g_renderer.compatibilityProgram = 0;
                g_renderer.compatibilityViewProjectionUniform = -1;
                g_renderer.compatibilityWorldUniform = -1;
                g_renderer.compatibilityTextureUniform = -1;
                EmitShaderLifecycle(
                    "failed",
                    "The selected shader program was resident but its first indexed draw failed");
            }
        }
    }

    ++g_renderer.frameNumber;
    if (g_renderer.frameNumber == 1)
    {
        Web_Log(
            WebLogLevel::Info,
            "[kisakcod-web] First converted engine world surface rendered.\n");
        Web_EmitRuntimeState(
            "running",
            "The engine is submitting a converted world-surface slice through WebGL2");
    }
    if (g_renderer.frameNumber == 1 || g_renderer.frameNumber % 30 == 0)
    {
        Web_EmitFrameStats(g_renderer.frameNumber, width, height, elapsed);
    }
}
