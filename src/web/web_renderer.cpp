#include <web/web_renderer.h>
#include <web/web_renderer_comparison.h>

#include <web/web_renderer_surface_storage.h>
#include <web/web_system.h>

#include <gfx_d3d/gfx_image_types.h>
#include <universal/q_shared.h>
#include <gfx_d3d/r_water.h>
#include <database/db_generated_image_platform.h>
#include <qcommon/iwi_image.h>
#include <universal/com_files.h>

#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>

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
// Retail Killhouse's static XModel material set expands beyond the original
// 256 MiB bootstrap recovery allowance. Keep the aggregate bounded, but large
// enough to retain the complete encountered base-color set instead of turning
// late canonical models into backend-fallback geometry.
constexpr std::size_t WEB_RENDERER_MAX_WORLD_TEXTURE_BYTES =
    512u * 1024u * 1024u;

struct WebRendererRetainedWorldImage
{
    const GfxImage *canonicalIdentity = nullptr;
    std::string canonicalName;
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    GLuint texture = 0u;
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
    std::uint32_t normalImageIndex = INVALID_WORLD_IMAGE;
    std::uint32_t lightmapImageIndex = INVALID_WORLD_IMAGE;
    std::uint32_t secondaryLightmapImageIndex = INVALID_WORLD_IMAGE;
    std::uint32_t stateBits[2]{};
    std::uint8_t samplerState = 0u;
    std::uint8_t normalSamplerState = 0u;
    std::uint8_t lightmapIndex = 31u;
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
    bool depthHack = false;
    bool castsSunShadow = false;
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
    float waterColor[4]{};
    kisak::iwi::Rgba8Cube reflectionCube;
    GLuint waterTexture = 0u;
    GLuint reflectionTexture = 0u;
    float waterTextureTime = std::numeric_limits<float>::quiet_NaN();
};

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
    std::uint32_t instanceOffset = 0u;
    std::uint32_t instanceCount = 0u;
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
    GLuint program = 0;
    GLuint skyProgram = 0;
    GLuint postProcessProgram = 0;
    GLuint shadowProgram = 0;
    GLuint compatibilityProgram = 0;
    GLuint sceneFramebuffer = 0;
    GLuint sceneColorTexture = 0;
    GLuint sceneDepthRenderbuffer = 0;
    GLuint compositeFramebuffer = 0;
    GLuint compositeColorTexture = 0;
    GLuint shadowFramebuffer = 0;
    GLuint shadowDepthTexture = 0;
    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;
    GLuint indexBuffer = 0;
    GLuint texture = 0;
    GLint aspectUniform = -1;
    GLint textureUniform = -1;
    GLint textureEnabledUniform = -1;
    GLint normalMapUniform = -1;
    GLint normalMapEnabledUniform = -1;
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
    GLint alphaTestUniform = -1;
    GLint instanceEnabledUniform = -1;
    GLint uiColorUniform = -1;
    GLint fogEnabledUniform = -1;
    GLint viewOriginUniform = -1;
    GLint fogColorUniform = -1;
    GLint fogParamsUniform = -1;
    GLint shadowMapUniform = -1;
    GLint shadowMatrixUniform = -1;
    GLint sunShadowEnabledUniform = -1;
    GLint sunDirectionUniform = -1;
    GLint sunColorUniform = -1;
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
    GLint compatibilityViewProjectionUniform = -1;
    GLint compatibilityWorldUniform = -1;
    GLint compatibilityTextureUniform = -1;
    int frameNumber = 0;
    int canvasWidth = 0;
    int canvasHeight = 0;
    int postProcessWidth = 0;
    int postProcessHeight = 0;
    bool contextLost = false;
    bool initialized = false;
    std::vector<WebRendererSurfaceVertex> retainedVertices;
    std::vector<std::uint16_t> retainedIndices;
    std::vector<std::uint32_t> retainedWorldIndices;
    std::vector<WebRendererRetainedWorldBatch> retainedWorldBatches;
    std::vector<WebRendererRetainedWorldImage> retainedWorldImages;
    WebRendererRetainedSkyImage retainedSky;
    GLuint staticModelVertexArray = 0u;
    GLuint staticModelVertexBuffer = 0u;
    GLuint staticModelIndexBuffer = 0u;
    GLuint staticModelInstanceBuffer = 0u;
    std::vector<WebRendererSurfaceVertex> retainedStaticModelVertices;
    std::vector<std::uint32_t> retainedStaticModelIndices;
    std::vector<WebRendererStaticModelInstanceDesc> retainedStaticModelInstances;
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
    std::array<float, 4> sceneColorBias{};
    std::array<float, 4> sceneColorTintBase{};
    std::array<float, 4> sceneColorTintDelta{};
    float sceneDisplayGammaExponent = 1.0f;
    float sceneBlurRadius = 0.0f;
    std::uint32_t sceneViewX = 0u;
    std::uint32_t sceneViewY = 0u;
    std::uint32_t sceneViewWidth = 0u;
    std::uint32_t sceneViewHeight = 0u;
    std::string sceneViewWorldName;
    bool sceneViewActive = false;
    bool sceneViewGeometrySubmitted = false;
    bool sceneFogEnabled = false;
    bool sceneFilmEnabled = false;
    bool sceneSunShadowEnabled = false;
    bool sceneViewFirstDrawCompleted = false;
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

EM_JS(
    void,
    DispatchRendererComparisonSummary,
    (std::uint32_t intendedDrawCount,
     std::uint32_t actualDrawCount,
     std::uint32_t surfaceCount,
     std::uint32_t lightmappedDrawCount,
     std::uint32_t fallbackDrawCount,
     std::uint32_t alphaTestedDrawCount,
     std::uint32_t blendedDrawCount,
     std::uint32_t divergentDrawCount),
    {
        globalThis.dispatchEvent(new CustomEvent(
            "kisakcod:renderer-comparison", { detail: {
                state: "captured",
                source: "canonical-frontend-vs-webgl-backend",
                intendedDrawCount: intendedDrawCount >>> 0,
                actualDrawCount: actualDrawCount >>> 0,
                surfaceCount: surfaceCount >>> 0,
                lightmappedDrawCount: lightmappedDrawCount >>> 0,
                fallbackDrawCount: fallbackDrawCount >>> 0,
                alphaTestedDrawCount: alphaTestedDrawCount >>> 0,
                blendedDrawCount: blendedDrawCount >>> 0,
                divergentDrawCount: divergentDrawCount >>> 0,
                normalized: true,
                containsGpuHandles: false,
                containsObjectAddresses: false
            }}));
    });

EM_JS(
    void,
    DispatchRendererComparisonRecord,
    (std::uint32_t drawOrder,
     const char *sourceKind,
     const char *materialName,
     const char *techniqueName,
     std::uint32_t techniqueType,
     const char *intendedPortableTechnique,
     const char *actualPortableTechnique,
     const char *lightingMode,
     std::uint32_t customSamplerFlags,
     std::uint32_t techniqueFlags,
     const char *pixelShaderName,
     std::uint32_t pixelShaderProgramHash,
     std::uint32_t surfaceCount,
     std::uint32_t firstSurfaceIndex,
     std::uint32_t lastSurfaceIndex,
     std::uint32_t stateBits0,
     std::uint32_t stateBits1,
     const char *baseImageName,
     const char *normalImageName,
     const char *lightmapImageName,
     const char *secondaryLightmapImageName,
     std::uint32_t lightmapIndex,
     const char *alphaTest,
     const char *cull,
     const char *depthFunction,
     bool blendEnabled,
     bool depthTestEnabled,
     bool depthWriteEnabled,
     bool baseImageUsed,
     bool normalImageUsed,
     bool lightmapUsed,
     bool secondaryLightmapUsed,
     std::uint32_t samplerState,
     const char *composition,
     std::uint32_t divergenceFields),
    {
        globalThis.dispatchEvent(new CustomEvent(
            "kisakcod:renderer-comparison-record", { detail: {
                drawOrder: drawOrder >>> 0,
                sourceKind: UTF8ToString(sourceKind),
                materialName: UTF8ToString(materialName),
                techniqueName: UTF8ToString(techniqueName),
                techniqueType: techniqueType >>> 0,
                intendedPortableTechnique: UTF8ToString(intendedPortableTechnique),
                actualPortableTechnique: UTF8ToString(actualPortableTechnique),
                lightingMode: UTF8ToString(lightingMode),
                customSamplerFlags: customSamplerFlags >>> 0,
                techniqueFlags: techniqueFlags >>> 0,
                pixelShaderName: UTF8ToString(pixelShaderName),
                pixelShaderProgramHash: pixelShaderProgramHash >>> 0,
                lightingInputs: {
                    colorSpace: "normalized-rgba8-direct",
                    baseColor: "base-texture*vertex-color",
                    secondaryUvTransforms: [
                        { scale: [1.0, 0.5], bias: [0.0, 0.0] },
                        { scale: [1.0, 0.5], bias: [0.0, 0.5] }
                    ],
                    composition: UTF8ToString(composition)
                },
                surfaceCount: surfaceCount >>> 0,
                firstSurfaceIndex: firstSurfaceIndex >>> 0,
                lastSurfaceIndex: lastSurfaceIndex >>> 0,
                stateBits: [stateBits0 >>> 0, stateBits1 >>> 0],
                baseImageName: UTF8ToString(baseImageName),
                normalImageName: UTF8ToString(normalImageName),
                lightmapImageName: UTF8ToString(lightmapImageName),
                secondaryLightmapImageName: UTF8ToString(secondaryLightmapImageName),
                lightmapIndex: lightmapIndex >>> 0,
                alphaTest: UTF8ToString(alphaTest),
                cull: UTF8ToString(cull),
                depthFunction: UTF8ToString(depthFunction),
                blendEnabled: Boolean(blendEnabled),
                depthTestEnabled: Boolean(depthTestEnabled),
                depthWriteEnabled: Boolean(depthWriteEnabled),
                baseImageUsed: Boolean(baseImageUsed),
                normalImageUsed: Boolean(normalImageUsed),
                lightmapUsed: Boolean(lightmapUsed),
                secondaryLightmapUsed: Boolean(secondaryLightmapUsed),
                samplerState: samplerState >>> 0,
                divergenceFields: divergenceFields >>> 0
            }}));
    });

const char *FxSourceKindName(WebRendererSceneBatchKind kind) noexcept
{
    switch (kind)
    {
    case WebRendererSceneBatchKind::FxCodeMesh: return "FxCodeMesh";
    case WebRendererSceneBatchKind::FxXModel: return "FxXModel";
    case WebRendererSceneBatchKind::FxParticleCloud: return "FxParticleCloud";
    case WebRendererSceneBatchKind::FxMarkMesh: return "FxMarkMesh";
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

const char *ComparisonSourceKindName(WebRendererSceneBatchKind kind) noexcept
{
    switch (kind)
    {
    case WebRendererSceneBatchKind::WorldSurface: return "WorldSurface";
    case WebRendererSceneBatchKind::StaticXModel: return "StaticXModel";
    case WebRendererSceneBatchKind::DynamicDObj: return "DynamicDObj";
    case WebRendererSceneBatchKind::DynamicXModel: return "DynamicXModel";
    case WebRendererSceneBatchKind::DynamicBModel: return "DynamicBModel";
    case WebRendererSceneBatchKind::FxCodeMesh: return "FxCodeMesh";
    case WebRendererSceneBatchKind::FxXModel: return "FxXModel";
    case WebRendererSceneBatchKind::FxParticleCloud: return "FxParticleCloud";
    case WebRendererSceneBatchKind::FxMarkMesh: return "FxMarkMesh";
    }
    return "Unknown";
}

const char *ComparisonPortableTechniqueName(
    WebRendererWorldTechnique technique) noexcept
{
    switch (technique)
    {
    case WebRendererWorldTechnique::BackendFallback: return "backend-fallback";
    case WebRendererWorldTechnique::BaseTexture: return "base-texture";
    case WebRendererWorldTechnique::BaseTextureLightmap:
        return "base-texture-lightmap";
    case WebRendererWorldTechnique::BaseTextureLightmapNormal:
        return "base-texture-lightmap-normal";
    case WebRendererWorldTechnique::VertexColorMultiply:
        return "vertex-color-multiply";
    case WebRendererWorldTechnique::VertexColorAdditive:
        return "vertex-color-additive";
    case WebRendererWorldTechnique::WaterLitSun: return "water-lit-sun";
    case WebRendererWorldTechnique::ReflexSight: return "reflex-sight";
    }
    return "unknown";
}

const char *ComparisonLightingModeName(
    WebRendererWorldLightingMode mode) noexcept
{
    switch (mode)
    {
    case WebRendererWorldLightingMode::None: return "none";
    case WebRendererWorldLightingMode::SecondaryDirectional:
        return "secondary-directional";
    case WebRendererWorldLightingMode::ModelLightGrid:
        return "model-light-grid";
    }
    return "unknown";
}

const char *ComparisonCompositionName(
    WebRendererWorldTechnique technique,
    std::uint8_t techniqueType) noexcept
{
    if (techniqueType == 9u &&
        WebRenderer_UsesSecondaryDirectionalLightmap(technique))
        return "base*vertex*(directionalBaked+pcfShadow*ndotl*sunDiffuse)";
    switch (technique)
    {
    case WebRendererWorldTechnique::BaseTextureLightmapNormal:
        return "base*vertex*(secondary0*rsqrt(normalSlopeDot+1)+"
            "secondary1*saturate(dot(lightSlope,normalSlope)+1))";
    case WebRendererWorldTechnique::BaseTextureLightmap:
        return "base*vertex*(secondary0+secondary1*"
            "rsqrt(lightSlopeDot+1))";
    case WebRendererWorldTechnique::VertexColorMultiply:
        return "mix(white,base*vertexRgb,vertexAlpha), ZERO/SRC_COLOR blend";
    case WebRendererWorldTechnique::VertexColorAdditive:
        return "fog(base*vertex)*baseAlpha*vertexAlpha, ONE/ONE blend";
    case WebRendererWorldTechnique::WaterLitSun:
        return "fresnel(waterColor*normalZ,reflectionProbe)+sunSpecular";
    default:
        return "base*vertex";
    }
}

void LogNormalizedWorldLightingTrace(
    const WebRendererWorldSurfaceDesc &surface)
{
    for (std::uint32_t index = 0u; index < surface.batchCount; ++index)
    {
        const WebRendererWorldBatchDesc &batch = surface.batches[index];
        if (batch.lightingMode !=
                WebRendererWorldLightingMode::SecondaryDirectional)
            continue;
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Normalized lighting trace: material='%s' "
            "technique='%s' type=%u flags=0x%04x shader='%s' "
            "shaderHash=0x%08x samplerFlags=0x%02x lightmapIndex=%u "
            "primary='%s' secondary='%s' secondaryUv="
            "[(1,0.5)+(0,0),(1,0.5)+(0,0.5)] colorSpace="
            "normalized-rgba8-direct inputs=base*vertex "
            "composition=secondary0+secondary1*rsqrt(encodedAlphaDot+1) "
            "output=pre-fog-linear-rgb.\n",
            batch.materialName ? batch.materialName : "<null-material>",
            batch.techniqueName ? batch.techniqueName :
                "<unsupported-technique>",
            static_cast<unsigned int>(batch.techniqueType),
            static_cast<unsigned int>(batch.techniqueFlags),
            batch.pixelShaderName ? batch.pixelShaderName :
                "<unavailable-pixel-shader>",
            static_cast<unsigned int>(batch.pixelShaderProgramHash),
            static_cast<unsigned int>(batch.customSamplerFlags),
            static_cast<unsigned int>(batch.lightmapIndex),
            batch.lightmapImage && batch.lightmapImage->name
                ? batch.lightmapImage->name : "<not-sampled>",
            batch.secondaryLightmapImage &&
                    batch.secondaryLightmapImage->name
                ? batch.secondaryLightmapImage->name : "<missing>");
        return;
    }
}

const WebRendererRetainedWorldImage *RetainedWorldImageIdentity(
    std::uint32_t index) noexcept
{
    if (index == INVALID_WORLD_IMAGE ||
        index >= g_renderer.retainedWorldImages.size())
        return nullptr;
    return &g_renderer.retainedWorldImages[index];
}

void EmitWorldComparison(
    const WebRendererComparisonCapture &intended)
{
    WebRendererComparisonCapture actual;
    try
    {
        const std::size_t count = g_renderer.retainedWorldBatches.size();
        std::vector<GfxImage> baseIdentities(count);
        std::vector<GfxImage> normalIdentities(count);
        std::vector<GfxImage> lightmapIdentities(count);
        std::vector<GfxImage> secondaryLightmapIdentities(count);
        std::vector<WebRendererWorldBatchDesc> descriptors(count);
        for (std::size_t index = 0u; index < count; ++index)
        {
            const WebRendererRetainedWorldBatch &source =
                g_renderer.retainedWorldBatches[index];
            WebRendererWorldBatchDesc &destination = descriptors[index];
            destination.firstIndex = source.firstIndex;
            destination.indexCount = source.indexCount;
            destination.surfaceCount = source.surfaceCount;
            destination.firstSurfaceIndex = source.firstSurfaceIndex;
            destination.lastSurfaceIndex = source.lastSurfaceIndex;
            destination.materialName = source.materialName.c_str();
            destination.modelName = source.modelName.c_str();
            destination.firstInstanceIndex = source.firstInstanceIndex;
            destination.lastInstanceIndex = source.lastInstanceIndex;
            destination.stateBits[0] = source.stateBits[0];
            destination.stateBits[1] = source.stateBits[1];
            destination.samplerState = source.samplerState;
            destination.lightmapIndex = source.lightmapIndex;
            destination.sourceKind = source.sourceKind;
            destination.technique = source.technique;
            destination.lightingMode = source.lightingMode;
            destination.techniqueName = source.techniqueName.c_str();
            destination.techniqueType = source.techniqueType;
            destination.customSamplerFlags = source.customSamplerFlags;
            destination.techniqueFlags = source.techniqueFlags;
            destination.depthHack = source.depthHack;
            destination.pixelShaderName = source.pixelShaderName.c_str();
            destination.pixelShaderProgramHash =
                source.pixelShaderProgramHash;
            if (const WebRendererRetainedWorldImage *image =
                    RetainedWorldImageIdentity(source.baseImageIndex))
            {
                baseIdentities[index].name = image->canonicalName.c_str();
                destination.baseImage = &baseIdentities[index];
            }
            if (const WebRendererRetainedWorldImage *image =
                    RetainedWorldImageIdentity(source.normalImageIndex))
            {
                normalIdentities[index].name = image->canonicalName.c_str();
                destination.normalImage = &normalIdentities[index];
            }
            if (const WebRendererRetainedWorldImage *image =
                    RetainedWorldImageIdentity(source.lightmapImageIndex))
            {
                lightmapIdentities[index].name = image->canonicalName.c_str();
                destination.lightmapImage = &lightmapIdentities[index];
            }
            if (const WebRendererRetainedWorldImage *image =
                    RetainedWorldImageIdentity(
                        source.secondaryLightmapImageIndex))
            {
                secondaryLightmapIdentities[index].name =
                    image->canonicalName.c_str();
                destination.secondaryLightmapImage =
                    &secondaryLightmapIdentities[index];
            }
        }
        const WebRendererWorldSurfaceDesc comparisonSurface{
            nullptr, 0u, nullptr, 0u, descriptors.data(),
            static_cast<std::uint32_t>(descriptors.size())};
        if (!WebRenderer_CaptureComparison(comparisonSurface, actual))
            return;
    }
    catch (const std::bad_alloc &)
    {
        return;
    }

    const std::vector<WebRendererComparisonDelta> deltas =
        WebRenderer_CompareCaptures(intended, actual);
    std::vector<std::uint32_t> differenceFields(
        std::max(intended.records.size(), actual.records.size()), 0u);
    for (const WebRendererComparisonDelta &delta : deltas)
    {
        if (delta.drawOrder < differenceFields.size())
            differenceFields[delta.drawOrder] |= delta.fields;
    }
    DispatchRendererComparisonSummary(
        static_cast<std::uint32_t>(intended.records.size()),
        static_cast<std::uint32_t>(actual.records.size()),
        actual.surfaceCount,
        actual.lightmappedDrawCount,
        actual.fallbackDrawCount,
        actual.alphaTestedDrawCount,
        actual.blendedDrawCount,
        static_cast<std::uint32_t>(deltas.size()));
    const std::size_t count = std::min(
        intended.records.size(), actual.records.size());
    for (std::size_t index = 0u; index < count; ++index)
    {
        const WebRendererComparisonRecord &expected = intended.records[index];
        const WebRendererComparisonRecord &retained = actual.records[index];
        DispatchRendererComparisonRecord(
            expected.drawOrder,
            ComparisonSourceKindName(expected.sourceKind),
            expected.materialName.c_str(),
            expected.techniqueName.c_str(),
            expected.techniqueType,
            ComparisonPortableTechniqueName(expected.portableTechnique),
            ComparisonPortableTechniqueName(retained.portableTechnique),
            ComparisonLightingModeName(expected.lightingMode),
            expected.customSamplerFlags,
            expected.techniqueFlags,
            expected.pixelShaderName.c_str(),
            expected.pixelShaderProgramHash,
            expected.surfaceCount,
            expected.firstSurfaceIndex,
            expected.lastSurfaceIndex,
            expected.stateBits[0],
            expected.stateBits[1],
            expected.baseImageName.c_str(),
            expected.normalImageName.c_str(),
            expected.lightmapImageName.c_str(),
            expected.secondaryLightmapImageName.c_str(),
            expected.lightmapIndex,
            WebRenderer_ComparisonAlphaTestString(expected.alphaTest),
            WebRenderer_ComparisonCullString(expected.cull),
            WebRenderer_ComparisonDepthFunctionString(expected.depthFunction),
            expected.blendEnabled,
            expected.depthTestEnabled,
            expected.depthWriteEnabled,
            retained.baseImageUsed,
            retained.normalImageUsed,
            retained.lightmapUsed,
            retained.secondaryLightmapUsed,
            expected.samplerState,
            ComparisonCompositionName(
                expected.portableTechnique, expected.techniqueType),
            differenceFields[index]);
    }
}

void LogWorldVertexColorInventory(
    const WebRendererWorldSurfaceDesc &surface)
{
    struct ColorStats
    {
        double sum[3]{};
        float minimum[3]{1.0f, 1.0f, 1.0f};
        float maximum[3]{};
        std::uint64_t samples = 0u;
    };
    constexpr std::size_t techniqueCount =
        static_cast<std::size_t>(WebRendererWorldTechnique::ReflexSight) + 1u;
    std::array<ColorStats, techniqueCount> stats{};
    for (std::uint32_t batchIndex = 0u;
         batchIndex < surface.batchCount; ++batchIndex)
    {
        const WebRendererWorldBatchDesc &batch = surface.batches[batchIndex];
        const std::size_t techniqueIndex =
            static_cast<std::size_t>(batch.technique);
        if (techniqueIndex >= stats.size() ||
            batch.firstIndex > surface.indexCount ||
            batch.indexCount > surface.indexCount - batch.firstIndex)
            continue;
        ColorStats &entry = stats[techniqueIndex];
        for (std::uint32_t offset = 0u; offset < batch.indexCount; ++offset)
        {
            const std::uint32_t vertexIndex =
                surface.indices[batch.firstIndex + offset];
            if (vertexIndex >= surface.vertexCount)
                continue;
            const WebRendererSurfaceVertex &vertex =
                surface.vertices[vertexIndex];
            for (std::size_t channel = 0u; channel < 3u; ++channel)
            {
                entry.sum[channel] += vertex.color[channel];
                entry.minimum[channel] = std::min(
                    entry.minimum[channel], vertex.color[channel]);
                entry.maximum[channel] = std::max(
                    entry.maximum[channel], vertex.color[channel]);
            }
            ++entry.samples;
        }
    }
    constexpr std::array<const char *, techniqueCount> techniqueNames{{
        "fallback", "base", "lightmapped", "lightmapped-normal",
        "multiply", "additive", "water-lit-sun", "reflex-sight"}};
    for (std::size_t index = 0u; index < stats.size(); ++index)
    {
        const ColorStats &entry = stats[index];
        if (entry.samples == 0u)
            continue;
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Renderer comparison vertex colors %s: "
            "samples=%llu avg=(%.4f %.4f %.4f) min=(%.4f %.4f %.4f) "
            "max=(%.4f %.4f %.4f).\n",
            techniqueNames[index],
            static_cast<unsigned long long>(entry.samples),
            entry.sum[0] / entry.samples,
            entry.sum[1] / entry.samples,
            entry.sum[2] / entry.samples,
            entry.minimum[0], entry.minimum[1], entry.minimum[2],
            entry.maximum[0], entry.maximum[1], entry.maximum[2]);
    }
}

void LogRetainedLightmapInventory(
    const WebRendererWorldSurfaceDesc &surface)
{
    std::vector<std::uint32_t> indices;
    for (const WebRendererRetainedWorldBatch &batch :
         g_renderer.retainedWorldBatches)
    {
        if (batch.lightmapImageIndex != INVALID_WORLD_IMAGE &&
            std::find(indices.begin(), indices.end(),
                batch.lightmapImageIndex) == indices.end())
            indices.push_back(batch.lightmapImageIndex);
        if (batch.secondaryLightmapImageIndex != INVALID_WORLD_IMAGE &&
            std::find(indices.begin(), indices.end(),
                batch.secondaryLightmapImageIndex) == indices.end())
            indices.push_back(batch.secondaryLightmapImageIndex);
    }
    for (const std::uint32_t index : indices)
    {
        const WebRendererRetainedWorldImage *image =
            RetainedWorldImageIdentity(index);
        if (!image || !image->supported || image->pixels.size() < 4u)
            continue;
        std::uint8_t minimum[4]{255u, 255u, 255u, 255u};
        std::uint8_t maximum[4]{};
        std::uint64_t sum[4]{};
        std::uint64_t samples = 0u;
        for (std::size_t offset = 0u; offset + 3u < image->pixels.size();
             offset += 4u)
        {
            for (std::size_t channel = 0u; channel < 4u; ++channel)
            {
                const std::uint8_t value = image->pixels[offset + channel];
                minimum[channel] = std::min(minimum[channel], value);
                maximum[channel] = std::max(maximum[channel], value);
                sum[channel] += value;
            }
            ++samples;
        }
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Renderer comparison lightmap '%s': "
            "%ux%u RGBA8 samples=%llu avg=(%.2f %.2f %.2f %.2f) "
            "min=(%u %u %u %u) max=(%u %u %u %u).\n",
            image->canonicalName.c_str(), image->width, image->height,
            static_cast<unsigned long long>(samples),
            samples ? static_cast<double>(sum[0]) / samples : 0.0,
            samples ? static_cast<double>(sum[1]) / samples : 0.0,
            samples ? static_cast<double>(sum[2]) / samples : 0.0,
            samples ? static_cast<double>(sum[3]) / samples : 0.0,
            static_cast<unsigned int>(minimum[0]),
            static_cast<unsigned int>(minimum[1]),
            static_cast<unsigned int>(minimum[2]),
            static_cast<unsigned int>(minimum[3]),
            static_cast<unsigned int>(maximum[0]),
            static_cast<unsigned int>(maximum[1]),
            static_cast<unsigned int>(maximum[2]),
            static_cast<unsigned int>(maximum[3]));
    }

    std::uint64_t currentSum = 0u;
    std::uint64_t flippedSum = 0u;
    std::uint64_t currentBlack = 0u;
    std::uint64_t flippedBlack = 0u;
    std::uint64_t secondarySum[2][4]{};
    std::uint64_t secondaryBlack[2]{};
    std::uint64_t secondarySamples = 0u;
    std::uint64_t coordinateSamples = 0u;
    for (std::uint32_t batchIndex = 0u;
         batchIndex < surface.batchCount; ++batchIndex)
    {
        const WebRendererWorldBatchDesc &batch = surface.batches[batchIndex];
        if (!WebRenderer_UsesSecondaryDirectionalLightmap(batch.technique) ||
            batch.firstIndex > surface.indexCount ||
            batch.indexCount > surface.indexCount - batch.firstIndex)
            continue;
        const WebRendererRetainedWorldBatch &retained =
            g_renderer.retainedWorldBatches[batchIndex];
        const WebRendererRetainedWorldImage *image =
            RetainedWorldImageIdentity(retained.lightmapImageIndex);
        const WebRendererRetainedWorldImage *secondary =
            RetainedWorldImageIdentity(
                retained.secondaryLightmapImageIndex);
        if (!image || !image->supported || image->width == 0u ||
            image->height == 0u)
            continue;
        for (std::uint32_t offset = 0u; offset < batch.indexCount; ++offset)
        {
            const std::uint32_t vertexIndex =
                surface.indices[batch.firstIndex + offset];
            if (vertexIndex >= surface.vertexCount)
                continue;
            const WebRendererSurfaceVertex &vertex =
                surface.vertices[vertexIndex];
            const float u = std::clamp(
                vertex.lightmapCoordinate[0], 0.0f, 1.0f);
            const float v = std::clamp(
                vertex.lightmapCoordinate[1], 0.0f, 1.0f);
            const std::uint32_t x = static_cast<std::uint32_t>(
                u * static_cast<float>(image->width - 1u) + 0.5f);
            const std::uint32_t currentY = static_cast<std::uint32_t>(
                v * static_cast<float>(image->height - 1u) + 0.5f);
            const std::uint32_t flippedY = image->height - 1u - currentY;
            const std::uint8_t current = image->pixels[
                (static_cast<std::size_t>(currentY) * image->width + x) * 4u];
            const std::uint8_t flipped = image->pixels[
                (static_cast<std::size_t>(flippedY) * image->width + x) * 4u];
            currentSum += current;
            flippedSum += flipped;
            currentBlack += current <= 4u ? 1u : 0u;
            flippedBlack += flipped <= 4u ? 1u : 0u;
            if (secondary && secondary->supported &&
                secondary->width != 0u && secondary->height != 0u)
            {
                const std::uint32_t secondaryX =
                    static_cast<std::uint32_t>(u * static_cast<float>(
                        secondary->width - 1u) + 0.5f);
                const std::uint32_t secondaryY[2]{
                    static_cast<std::uint32_t>(v * 0.5f *
                        static_cast<float>(secondary->height - 1u) + 0.5f),
                    static_cast<std::uint32_t>((0.5f + v * 0.5f) *
                        static_cast<float>(secondary->height - 1u) + 0.5f),
                };
                for (std::size_t lobe = 0u; lobe < 2u; ++lobe)
                {
                    const std::size_t pixel =
                        (static_cast<std::size_t>(secondaryY[lobe]) *
                            secondary->width + secondaryX) * 4u;
                    for (std::size_t channel = 0u; channel < 4u; ++channel)
                        secondarySum[lobe][channel] +=
                            secondary->pixels[pixel + channel];
                    secondaryBlack[lobe] +=
                        secondary->pixels[pixel] <= 4u &&
                        secondary->pixels[pixel + 1u] <= 4u &&
                        secondary->pixels[pixel + 2u] <= 4u ? 1u : 0u;
                }
                ++secondarySamples;
            }
            ++coordinateSamples;
        }
    }
    if (coordinateSamples != 0u)
    {
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Renderer comparison lightmap vertex samples: "
            "count=%llu current(avg=%.2f black<=4=%.2f%%) "
            "v-flipped(avg=%.2f black<=4=%.2f%%).\n",
            static_cast<unsigned long long>(coordinateSamples),
            static_cast<double>(currentSum) / coordinateSamples,
            100.0 * static_cast<double>(currentBlack) / coordinateSamples,
            static_cast<double>(flippedSum) / coordinateSamples,
            100.0 * static_cast<double>(flippedBlack) / coordinateSamples);
    }
    if (secondarySamples != 0u)
    {
        for (std::size_t lobe = 0u; lobe < 2u; ++lobe)
        {
            Web_Log(WebLogLevel::Info,
                "[kisakcod-web] Renderer comparison secondary lightmap "
                "lobe %zu vertex samples: count=%llu avg=(%.2f %.2f %.2f "
                "%.2f) blackRGB<=4=%.2f%%.\n",
                lobe,
                static_cast<unsigned long long>(secondarySamples),
                static_cast<double>(secondarySum[lobe][0]) / secondarySamples,
                static_cast<double>(secondarySum[lobe][1]) / secondarySamples,
                static_cast<double>(secondarySum[lobe][2]) / secondarySamples,
                static_cast<double>(secondarySum[lobe][3]) / secondarySamples,
                100.0 * static_cast<double>(secondaryBlack[lobe]) /
                    secondarySamples);
        }
    }
}

void LogWorldColorImageInventory()
{
    std::vector<std::uint32_t> indices;
    for (const WebRendererRetainedWorldBatch &batch :
         g_renderer.retainedWorldBatches)
    {
        if (!batch.techniqueName.starts_with(",wc_l_") ||
            batch.baseImageIndex == INVALID_WORLD_IMAGE ||
            std::find(indices.begin(), indices.end(), batch.baseImageIndex) !=
                indices.end())
        {
            continue;
        }
        indices.push_back(batch.baseImageIndex);
    }
    for (const std::uint32_t index : indices)
    {
        const WebRendererRetainedWorldImage *image =
            RetainedWorldImageIdentity(index);
        if (!image || !image->supported || image->pixels.size() < 4u)
            continue;
        std::uint64_t alphaSum = 0u;
        std::uint64_t transparent = 0u;
        std::uint64_t opaque = 0u;
        std::uint8_t alphaMin = 255u;
        std::uint8_t alphaMax = 0u;
        const std::size_t pixelCount = image->pixels.size() / 4u;
        for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel)
        {
            const std::uint8_t alpha = image->pixels[pixel * 4u + 3u];
            alphaSum += alpha;
            alphaMin = std::min(alphaMin, alpha);
            alphaMax = std::max(alphaMax, alpha);
            if (alpha <= 4u) ++transparent;
            if (alpha >= 250u) ++opaque;
        }
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Renderer comparison world-color image '%s': "
            "%ux%u alpha(avg=%.2f min=%u max=%u transparent<=4=%.2f%% "
            "opaque>=250=%.2f%%).\n",
            image->canonicalName.c_str(), image->width, image->height,
            pixelCount > 0u
                ? static_cast<double>(alphaSum) /
                    static_cast<double>(pixelCount) : 0.0,
            static_cast<unsigned int>(alphaMin),
            static_cast<unsigned int>(alphaMax),
            pixelCount > 0u
                ? 100.0 * static_cast<double>(transparent) /
                    static_cast<double>(pixelCount) : 0.0,
            pixelCount > 0u
                ? 100.0 * static_cast<double>(opaque) /
                    static_cast<double>(pixelCount) : 0.0);
    }
}

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

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestLoseWebGLContext()
{
    return HandleWebGLContextLost(0, nullptr, nullptr) ? 1 : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int KisakWeb_TestRestoreWebGLContext()
{
    return HandleWebGLContextRestored(0, nullptr, nullptr) ? 1 : 0;
}

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

void ResetGpuHandles()
{
    g_renderer.program = 0;
    g_renderer.skyProgram = 0;
    g_renderer.postProcessProgram = 0;
    g_renderer.shadowProgram = 0;
    g_renderer.compatibilityProgram = 0;
    g_renderer.sceneFramebuffer = 0;
    g_renderer.sceneColorTexture = 0;
    g_renderer.sceneDepthRenderbuffer = 0;
    g_renderer.compositeFramebuffer = 0;
    g_renderer.compositeColorTexture = 0;
    g_renderer.shadowFramebuffer = 0;
    g_renderer.shadowDepthTexture = 0;
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
    g_renderer.normalMapUniform = -1;
    g_renderer.normalMapEnabledUniform = -1;
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
    g_renderer.alphaTestUniform = -1;
    g_renderer.instanceEnabledUniform = -1;
    g_renderer.uiColorUniform = -1;
    g_renderer.fogEnabledUniform = -1;
    g_renderer.viewOriginUniform = -1;
    g_renderer.fogColorUniform = -1;
    g_renderer.fogParamsUniform = -1;
    g_renderer.shadowMapUniform = -1;
    g_renderer.shadowMatrixUniform = -1;
    g_renderer.sunShadowEnabledUniform = -1;
    g_renderer.sunDirectionUniform = -1;
    g_renderer.sunColorUniform = -1;
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
    g_renderer.compatibilityViewProjectionUniform = -1;
    g_renderer.compatibilityWorldUniform = -1;
    g_renderer.compatibilityTextureUniform = -1;
    g_renderer.canvasWidth = 0;
    g_renderer.canvasHeight = 0;
    g_renderer.postProcessWidth = 0;
    g_renderer.postProcessHeight = 0;
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
        batch.waterTextureTime = std::numeric_limits<float>::quiet_NaN();
    }
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
    if (compatibilityProgram != 0)
    {
        glDeleteProgram(compatibilityProgram);
    }
}

void DeletePostProcessTargetObjects(
    GLuint sceneFramebuffer,
    GLuint sceneColorTexture,
    GLuint sceneDepthRenderbuffer,
    GLuint compositeFramebuffer,
    GLuint compositeColorTexture)
{
    if (sceneFramebuffer != 0u)
        glDeleteFramebuffers(1, &sceneFramebuffer);
    if (compositeFramebuffer != 0u)
        glDeleteFramebuffers(1, &compositeFramebuffer);
    if (sceneDepthRenderbuffer != 0u)
        glDeleteRenderbuffers(1, &sceneDepthRenderbuffer);
    if (sceneColorTexture != 0u)
        glDeleteTextures(1, &sceneColorTexture);
    if (compositeColorTexture != 0u)
        glDeleteTextures(1, &compositeColorTexture);
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
    (void)emscripten_set_webglcontextlost_callback(
        "#canvas", nullptr, EM_TRUE, nullptr);
    (void)emscripten_set_webglcontextrestored_callback(
        "#canvas", nullptr, EM_TRUE, nullptr);

    if (emscripten_webgl_make_context_current(g_renderer.context) ==
        EMSCRIPTEN_RESULT_SUCCESS)
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
            g_renderer.sceneDepthRenderbuffer,
            g_renderer.compositeFramebuffer,
            g_renderer.compositeColorTexture);
        DeleteShadowObjects(
            g_renderer.shadowFramebuffer,
            g_renderer.shadowDepthTexture,
            g_renderer.shadowProgram);
        DeletePipelineObjects(
            g_renderer.program,
            g_renderer.skyProgram,
            g_renderer.postProcessProgram,
            g_renderer.compatibilityProgram,
            g_renderer.vertexArray,
            g_renderer.vertexBuffer,
            g_renderer.indexBuffer,
            g_renderer.texture);
    }
    ResetGpuHandles();
    (void)emscripten_webgl_destroy_context(g_renderer.context);
    g_renderer.context = 0;
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
        if (batch.reflectionTexture != 0u)
            glDeleteTextures(1, &batch.reflectionTexture);
        batch.waterTexture = 0u;
        batch.reflectionTexture = 0u;
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
        GL_STATIC_DRAW);
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

    while (glGetError() != GL_NO_ERROR)
    {
    }
    GLuint texture = 0u;
    glGenTextures(1, &texture);
    glActiveTexture(textureUnit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    const std::uint8_t filter = samplerState & 0x07u;
    const GLint glFilter = filter == 1u ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, glFilter);
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
    for (WebRendererRetainedWorldBatch &batch : batches)
    {
        if (batch.technique != WebRendererWorldTechnique::WaterLitSun)
            continue;
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
        if (!CreateCubeTextureObject(batch.reflectionCube, 2u, GL_TEXTURE8,
                "reflection-probe", batch.reflectionTexture) ||
            batch.waterTexture == 0u || glGetError() != GL_NO_ERROR)
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
        if (!CreateTextureObject(
                image.pixels.data(), image.width, image.height, 2u,
                image.texture))
        {
            DeleteWorldTextureObjects(images);
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, image.texture);
        glGenerateMipmap(GL_TEXTURE_2D);
        if (glGetError() != GL_NO_ERROR)
        {
            DeleteWorldTextureObjects(images);
            return false;
        }
    }
    return true;
}

bool CreatePostProcessTargets(int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    if (g_renderer.sceneFramebuffer != 0u &&
        g_renderer.compositeFramebuffer != 0u &&
        g_renderer.postProcessWidth == width &&
        g_renderer.postProcessHeight == height)
        return true;

    DeletePostProcessTargetObjects(
        g_renderer.sceneFramebuffer,
        g_renderer.sceneColorTexture,
        g_renderer.sceneDepthRenderbuffer,
        g_renderer.compositeFramebuffer,
        g_renderer.compositeColorTexture);
    g_renderer.sceneFramebuffer = 0u;
    g_renderer.sceneColorTexture = 0u;
    g_renderer.sceneDepthRenderbuffer = 0u;
    g_renderer.compositeFramebuffer = 0u;
    g_renderer.compositeColorTexture = 0u;
    g_renderer.postProcessWidth = 0;
    g_renderer.postProcessHeight = 0;

    GLuint sceneFramebuffer = 0u;
    GLuint sceneColorTexture = 0u;
    GLuint sceneDepthRenderbuffer = 0u;
    GLuint compositeFramebuffer = 0u;
    GLuint compositeColorTexture = 0u;
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

    glGenRenderbuffers(1, &sceneDepthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneDepthRenderbuffer);
    glRenderbufferStorage(
        GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glGenFramebuffers(1, &sceneFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, sceneColorTexture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER, sceneDepthRenderbuffer);
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
    const GLenum error = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0u);
    glBindRenderbuffer(GL_RENDERBUFFER, 0u);
    glBindTexture(GL_TEXTURE_2D, 0u);

    if (sceneFramebuffer == 0u || sceneColorTexture == 0u ||
        sceneDepthRenderbuffer == 0u || compositeFramebuffer == 0u ||
        compositeColorTexture == 0u ||
        sceneStatus != GL_FRAMEBUFFER_COMPLETE ||
        compositeStatus != GL_FRAMEBUFFER_COMPLETE ||
        error != GL_NO_ERROR)
    {
        DeletePostProcessTargetObjects(sceneFramebuffer, sceneColorTexture,
            sceneDepthRenderbuffer, compositeFramebuffer,
            compositeColorTexture);
        Web_Log(WebLogLevel::Error,
            "[kisakcod-web] WebGL2 post-effect target creation failed "
            "(scene=0x%x composite=0x%x error=0x%x).\n",
            static_cast<unsigned int>(sceneStatus),
            static_cast<unsigned int>(compositeStatus),
            static_cast<unsigned int>(error));
        return false;
    }

    g_renderer.sceneFramebuffer = sceneFramebuffer;
    g_renderer.sceneColorTexture = sceneColorTexture;
    g_renderer.sceneDepthRenderbuffer = sceneDepthRenderbuffer;
    g_renderer.compositeFramebuffer = compositeFramebuffer;
    g_renderer.compositeColorTexture = compositeColorTexture;
    g_renderer.postProcessWidth = width;
    g_renderer.postProcessHeight = height;
    return true;
}

bool CreateSunShadowTarget(GLuint &framebufferOut, GLuint &depthTextureOut)
{
    constexpr GLsizei SHADOW_SIZE = 1024;
    GLuint framebuffer = 0u;
    GLuint depthTexture = 0u;
    while (glGetError() != GL_NO_ERROR)
    {
    }
    glGenTextures(1, &depthTexture);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
        SHADOW_SIZE, SHADOW_SIZE, 0, GL_DEPTH_COMPONENT,
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
            "[kisakcod-web] WebGL2 sun-shadow target creation failed "
            "(status=0x%x error=0x%x).\n",
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
    if (!g_renderer.surfaceActive)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Renderer initialization requires an engine surface.\n");
        return false;
    }

    constexpr const char *vertexSource = R"glsl(#version 300 es
        precision highp float;
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
        uniform sampler2D u_secondary_lightmap;
        uniform sampler3D u_model_lighting;
        uniform sampler2D u_normal_map;
        uniform float u_texture_enabled;
        uniform float u_lightmap_enabled;
        uniform float u_secondary_lightmap_enabled;
        uniform float u_model_lighting_enabled;
        uniform float u_normal_map_enabled;
        uniform vec3 u_model_lighting_lookup_scale;
        uniform float u_premultiply_alpha;
        uniform float u_color_intensity_alpha;
        uniform int u_material_mode;
        uniform float u_scene_fallback;
        uniform int u_alpha_test;
        uniform vec4 u_ui_color;
        uniform float u_fog_enabled;
        uniform vec3 u_view_origin;
        uniform vec3 u_fog_color;
        uniform vec2 u_fog_params;
        uniform sampler2D u_shadow_map;
        uniform sampler2D u_water_map;
        uniform samplerCube u_reflection_probe;
        uniform mat4 u_shadow_matrix;
        uniform float u_sun_shadow_enabled;
        uniform vec3 u_sun_direction;
        uniform vec3 u_sun_color;
        uniform vec4 u_env_map_parms;
        uniform vec4 u_water_color;
        out vec4 out_color;

        float water_height(vec2 uv)
        {
            return texture(u_water_map, uv).r +
                texture(u_water_map, uv * 3.7).r * 0.6 +
                texture(u_water_map, uv * 13.69).r * 0.36;
        }

        float sample_sun_shadow(vec3 world_position)
        {
            vec4 clip = u_shadow_matrix * vec4(world_position, 1.0);
            vec3 projected = clip.xyz / clip.w;
            vec2 uv = projected.xy * 0.5 + 0.5;
            float receiver_depth = projected.z * 0.5 + 0.5;
            if (uv.x <= 0.0 || uv.x >= 1.0 ||
                uv.y <= 0.0 || uv.y >= 1.0 ||
                receiver_depth <= 0.0 || receiver_depth >= 1.0)
                return 1.0;
            // lm_sm_sun_* performs four manual depth comparisons. Preserve
            // that 2x2 PCF shape at the WebGL texture boundary.
            const vec2 texel = vec2(1.0 / 1024.0);
            float biased_depth = receiver_depth - 0.0015;
            float visibility = 0.0;
            visibility += biased_depth <= texture(
                u_shadow_map, uv + texel * vec2(-0.5, -0.5)).r ? 1.0 : 0.0;
            visibility += biased_depth <= texture(
                u_shadow_map, uv + texel * vec2( 0.5, -0.5)).r ? 1.0 : 0.0;
            visibility += biased_depth <= texture(
                u_shadow_map, uv + texel * vec2(-0.5,  0.5)).r ? 1.0 : 0.0;
            visibility += biased_depth <= texture(
                u_shadow_map, uv + texel * vec2( 0.5,  0.5)).r ? 1.0 : 0.0;
            return visibility * 0.25;
        }

        void main()
        {
            vec4 texel = texture(u_texture, v_texcoord);
            float source_alpha = u_color_intensity_alpha > 0.5
                ? max(texel.r, max(texel.g, texel.b))
                : texel.a;
            vec4 bootstrap_color = v_color;
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
                        lighting += u_sun_color * sun_amount *
                            sample_sun_shadow(v_world_position);
                    }
                    bootstrap_color.rgb *= lighting;
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
                    vec3 model_lighting = texture(
                        u_model_lighting,
                        v_model_lighting_coords + lookup_direction *
                            u_model_lighting_lookup_scale).rgb;
                    bootstrap_color.rgb *= model_lighting * 2.0;
                }
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
        out vec4 out_color;
        void main()
        {
            vec2 uv = v_ndc * 0.5 + 0.5;
            vec4 source = texture(u_source, uv);
            if (max(u_blur_scale.x, u_blur_scale.y) > 0.0)
            {
                // Native RB_BlurScreen applies a Gaussian filter to the
                // resolved 3D scene before 2D. A bounded disk kernel keeps
                // that ownership/order in one WebGL2 pass while scaling the
                // authored 640x480 radius to the actual scene target.
                source *= 0.38;
                source += texture(u_source,
                    uv + vec2( u_blur_scale.x * 0.5, 0.0)) * 0.09;
                source += texture(u_source,
                    uv + vec2(-u_blur_scale.x * 0.5, 0.0)) * 0.09;
                source += texture(u_source,
                    uv + vec2(0.0,  u_blur_scale.y * 0.5)) * 0.09;
                source += texture(u_source,
                    uv + vec2(0.0, -u_blur_scale.y * 0.5)) * 0.09;
                source += texture(u_source,
                    uv + vec2( u_blur_scale.x, 0.0)) * 0.04;
                source += texture(u_source,
                    uv + vec2(-u_blur_scale.x, 0.0)) * 0.04;
                source += texture(u_source,
                    uv + vec2(0.0,  u_blur_scale.y)) * 0.04;
                source += texture(u_source,
                    uv + vec2(0.0, -u_blur_scale.y)) * 0.04;
                source += texture(u_source,
                    uv + vec2( u_blur_scale.x,  u_blur_scale.y) * 0.7) * 0.025;
                source += texture(u_source,
                    uv + vec2(-u_blur_scale.x,  u_blur_scale.y) * 0.7) * 0.025;
                source += texture(u_source,
                    uv + vec2( u_blur_scale.x, -u_blur_scale.y) * 0.7) * 0.025;
                source += texture(u_source,
                    uv + vec2(-u_blur_scale.x, -u_blur_scale.y) * 0.7) * 0.025;
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
            color = pow(clamp(color, 0.0, 1.0),
                vec3(u_gamma_exponent));
            out_color = vec4(color, source.a);
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

    while (glGetError() != GL_NO_ERROR)
    {
    }

    const GLint aspectUniform = glGetUniformLocation(program, "u_aspect");
    const GLint textureUniform = glGetUniformLocation(program, "u_texture");
    const GLint textureEnabledUniform =
        glGetUniformLocation(program, "u_texture_enabled");
    const GLint normalMapUniform =
        glGetUniformLocation(program, "u_normal_map");
    const GLint normalMapEnabledUniform =
        glGetUniformLocation(program, "u_normal_map_enabled");
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
    const GLint shadowMatrixUniform =
        glGetUniformLocation(program, "u_shadow_matrix");
    const GLint sunShadowEnabledUniform =
        glGetUniformLocation(program, "u_sun_shadow_enabled");
    const GLint sunDirectionUniform =
        glGetUniformLocation(program, "u_sun_direction");
    const GLint sunColorUniform =
        glGetUniformLocation(program, "u_sun_color");
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
    const bool surfaceReady = g_renderer.worldSurfaceActive
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
            indexBuffer);

    GLuint texture = 0;
    const bool textureReady = surfaceReady && CreateTextureObject(
        texturePixels, textureWidth, textureHeight, textureSamplerState, texture);
    const bool worldTexturesReady = textureReady &&
        (!g_renderer.worldSurfaceActive ||
         CreateWorldTextureObjects(g_renderer.retainedWorldImages));
    const bool waterTexturesReady = worldTexturesReady &&
        (!g_renderer.worldSurfaceActive ||
         CreateWaterTextureObjects(g_renderer.retainedWorldBatches));
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
    const bool shadowTargetReady = CreateSunShadowTarget(
        shadowFramebuffer, shadowDepthTexture);
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
        normalMapUniform < 0 || normalMapEnabledUniform < 0 ||
        viewProjectionUniform < 0 || sceneFallbackUniform < 0 ||
        lightmapEnabledUniform < 0 ||
        secondaryLightmapUniform < 0 ||
        secondaryLightmapEnabledUniform < 0 ||
        modelLightingUniform < 0 || modelLightingEnabledUniform < 0 ||
        modelLightingBaseCoordinatesUniform < 0 ||
        modelLightingLookupScaleUniform < 0 ||
        premultiplyAlphaUniform < 0 || colorIntensityAlphaUniform < 0 ||
        materialModeUniform < 0 ||
        alphaTestUniform < 0 || instanceEnabledUniform < 0 ||
        uiColorUniform < 0 || fogEnabledUniform < 0 ||
        viewOriginUniform < 0 || fogColorUniform < 0 ||
        fogParamsUniform < 0 || shadowMapUniform < 0 ||
        shadowMatrixUniform < 0 || sunShadowEnabledUniform < 0 ||
        sunDirectionUniform < 0 || sunColorUniform < 0 ||
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
        pipelineError != GL_NO_ERROR ||
        !surfaceReady || !textureReady || !worldTexturesReady ||
        !waterTexturesReady ||
        !skyTextureReady ||
        !staticModelObjectsReady || !staticModelTexturesReady ||
        !staticModelLightingReady ||
        !dynamicModelObjectsReady || !dynamicModelTexturesReady ||
        !dynamicModelLightingReady ||
        !uiObjectsReady || !uiTexturesReady ||
        !shadowTargetReady ||
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
            compatibilityProgram,
            vertexArray,
            vertexBuffer,
            indexBuffer,
            texture);
        DeleteShadowObjects(
            shadowFramebuffer, shadowDepthTexture, shadowProgram);
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
    g_renderer.aspectUniform = aspectUniform;
    g_renderer.textureUniform = textureUniform;
    g_renderer.textureEnabledUniform = textureEnabledUniform;
    g_renderer.normalMapUniform = normalMapUniform;
    g_renderer.normalMapEnabledUniform = normalMapEnabledUniform;
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
    g_renderer.alphaTestUniform = alphaTestUniform;
    g_renderer.instanceEnabledUniform = instanceEnabledUniform;
    g_renderer.uiColorUniform = uiColorUniform;
    g_renderer.fogEnabledUniform = fogEnabledUniform;
    g_renderer.viewOriginUniform = viewOriginUniform;
    g_renderer.fogColorUniform = fogColorUniform;
    g_renderer.fogParamsUniform = fogParamsUniform;
    g_renderer.shadowMapUniform = shadowMapUniform;
    g_renderer.shadowMatrixUniform = shadowMatrixUniform;
    g_renderer.sunShadowEnabledUniform = sunShadowEnabledUniform;
    g_renderer.sunDirectionUniform = sunDirectionUniform;
    g_renderer.sunColorUniform = sunColorUniform;
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

    // Returning true allows the browser to later deliver a restoration event.
    return true;
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

    g_renderer.contextLost = false;
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
    return true;
}

bool CreateWebGLContext()
{
    EM_ASM({
        const canvas = globalThis.__KISAKCOD_OFFSCREEN_CANVAS__;
        if (canvas && typeof GL === "object" && GL.offscreenCanvases) {
            GL.offscreenCanvases.canvas = canvas;
        }
    });
    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.alpha = EM_FALSE;
    attributes.depth = EM_TRUE;
    attributes.stencil = EM_FALSE;
    attributes.antialias = EM_TRUE;
    attributes.premultipliedAlpha = EM_FALSE;
    attributes.preserveDrawingBuffer = EM_FALSE;
    attributes.enableExtensionsByDefault = EM_TRUE;
    attributes.majorVersion = 2;
    attributes.minorVersion = 0;

    g_renderer.context = emscripten_webgl_create_context("#canvas", &attributes);
    if (g_renderer.context <= 0)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Unable to create a WebGL2 context (%lu).\n",
            static_cast<unsigned long>(g_renderer.context));
        return false;
    }

    if (emscripten_webgl_make_context_current(g_renderer.context) !=
        EMSCRIPTEN_RESULT_SUCCESS)
    {
        return false;
    }

    const EMSCRIPTEN_RESULT lostCallbackResult = emscripten_set_webglcontextlost_callback(
        "#canvas", nullptr, EM_TRUE, HandleWebGLContextLost);
    const EMSCRIPTEN_RESULT restoredCallbackResult =
        emscripten_set_webglcontextrestored_callback(
            "#canvas", nullptr, EM_TRUE, HandleWebGLContextRestored);
    return lostCallbackResult == EMSCRIPTEN_RESULT_SUCCESS &&
        restoredCallbackResult == EMSCRIPTEN_RESULT_SUCCESS;
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
    kisak::iwi::Error &decodeError)
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
    std::size_t &retainedPixelBytes)
{
    if (!canonical) return INVALID_WORLD_IMAGE;
    for (std::uint32_t index = 0u; index < images.size(); ++index)
        if (images[index].canonicalIdentity == canonical) return index;

    WebRendererRetainedWorldImage retained;
    retained.canonicalIdentity = canonical;
    retained.canonicalName = canonical->name ? canonical->name : "<unnamed-image>";
    WebDbImageLoadDef loadDef{};
    const bool hasLoadDef = DB_WebGetImageLoadDef(canonical, loadDef);
    kisak::iwi::Rgba8Image decoded;
    kisak::iwi::Error decodeError = kisak::iwi::Error::None;
    bool attemptedDecode = false;
    if (retained.canonicalName == ",$white" ||
        retained.canonicalName == "$white")
    {
        decoded.width = 1u;
        decoded.height = 1u;
        decoded.pixels = {255u, 255u, 255u, 255u};
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
    }
    if ((!attemptedDecode || decodeError != kisak::iwi::Error::None) &&
        retained.canonicalName != ",$white" &&
        retained.canonicalName != "$white")
    {
        kisak::iwi::Rgba8Image externalDecoded;
        kisak::iwi::Error externalError = decodeError;
        if (DecodeExternalCanonicalImage(
                canonical, externalDecoded, externalError))
        {
            attemptedDecode = true;
            decodeError = externalError;
            if (decodeError == kisak::iwi::Error::None)
            {
                decoded = std::move(externalDecoded);
            }
        }
    }

    if (attemptedDecode && decodeError == kisak::iwi::Error::None &&
        decoded.pixels.size() <=
            WEB_RENDERER_MAX_WORLD_TEXTURE_BYTES -
                std::min(retainedPixelBytes,
                    WEB_RENDERER_MAX_WORLD_TEXTURE_BYTES))
    {
        retained.width = decoded.width;
        retained.height = decoded.height;
        retainedPixelBytes += decoded.pixels.size();
        retained.pixels = std::move(decoded.pixels);
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

WebRendererSurfaceResult CopyWorldCommand(
    const WebRendererWorldSurfaceDesc &surface,
    std::vector<WebRendererSurfaceVertex> &vertices,
    std::vector<std::uint32_t> &indices,
    std::vector<WebRendererRetainedWorldBatch> &batches,
    std::vector<WebRendererRetainedWorldImage> &images)
{
    if (!surface.vertices || !surface.indices || !surface.batches ||
        surface.vertexCount == 0u || surface.indexCount == 0u ||
        surface.batchCount == 0u ||
        surface.vertexCount > WEB_RENDERER_MAX_WORLD_VERTICES ||
        surface.indexCount > WEB_RENDERER_MAX_WORLD_INDICES)
    {
        return WebRendererSurfaceResult::InvalidDescriptor;
    }
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

    std::size_t retainedPixelBytes = 0u;
    for (const WebRendererRetainedWorldImage &image : images)
        retainedPixelBytes += image.pixels.size();
    try
    {
        vertices.assign(surface.vertices, surface.vertices + surface.vertexCount);
        indices.assign(surface.indices, surface.indices + surface.indexCount);
        batches.reserve(surface.batchCount);
        images.reserve(surface.batchCount * 3u);
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
                source.technique > WebRendererWorldTechnique::ReflexSight ||
                source.lightingMode >
                    WebRendererWorldLightingMode::ModelLightGrid)
            {
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
            batch.normalSamplerState = source.normalSamplerState;
            batch.lightmapIndex = source.lightmapIndex;
            batch.sourceKind = source.sourceKind;
            batch.technique = source.technique;
            batch.lightingMode = source.lightingMode;
            batch.techniqueName = source.techniqueName
                ? source.techniqueName : "<unsupported-technique>";
            batch.techniqueType = source.techniqueType;
            batch.customSamplerFlags = source.customSamplerFlags;
            batch.techniqueFlags = source.techniqueFlags;
            batch.depthHack = source.depthHack;
            batch.castsSunShadow = source.castsSunShadow;
            batch.pixelShaderName = source.pixelShaderName
                ? source.pixelShaderName : "<unavailable-pixel-shader>";
            batch.pixelShaderProgramHash = source.pixelShaderProgramHash;
            std::copy_n(source.modelLightingCoordinates, 3u,
                batch.modelLightingCoordinates);
            batch.waterSamplerState = source.waterSamplerState;
            batch.reflectionProbeIndex = source.reflectionProbeIndex;
            std::copy_n(source.envMapParms, 4u, batch.envMapParms);
            std::copy_n(source.waterColor, 4u, batch.waterColor);
            for (const float component : batch.modelLightingCoordinates)
                if (!std::isfinite(component))
                    return WebRendererSurfaceResult::NonFiniteVertex;
            for (const float component : batch.envMapParms)
                if (!std::isfinite(component))
                    return WebRendererSurfaceResult::NonFiniteVertex;
            for (const float component : batch.waterColor)
                if (!std::isfinite(component))
                    return WebRendererSurfaceResult::NonFiniteVertex;
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
                kisak::iwi::Error reflectionError =
                    kisak::iwi::Error::None;
                waterSupported = DecodeCanonicalCubeImage(
                    source.reflectionProbeImage, batch.reflectionCube,
                    reflectionError) &&
                    reflectionError == kisak::iwi::Error::None;
                if (!waterSupported)
                {
                    Web_Log(WebLogLevel::Info,
                        "[kisakcod-web] Canonical water reflection probe "
                        "'%s' uses backend fallback: %s.\n",
                        source.reflectionProbeImage->name
                            ? source.reflectionProbeImage->name : "<unnamed>",
                        kisak::iwi::ErrorString(reflectionError));
                }
            }
            batch.baseImageIndex = RetainCanonicalWorldImage(
                source.baseImage, images, retainedPixelBytes);
            if (source.lightingMode ==
                    WebRendererWorldLightingMode::ModelLightGrid ||
                WebRenderer_UsesWorldNormalMap(source.technique))
            {
                batch.normalImageIndex = RetainCanonicalWorldImage(
                    source.normalImage, images, retainedPixelBytes);
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
            if (source.technique == WebRendererWorldTechnique::WaterLitSun &&
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
                batch.technique =
                    WebRendererWorldTechnique::BaseTextureLightmap;
            batches.push_back(std::move(batch));
        }
        if (expectedFirstIndex != surface.indexCount)
            return WebRendererSurfaceResult::InvalidDescriptor;
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
        std::uint32_t expectedFirstIndex = 0u;
        for (std::uint32_t index = 0u; index < scene.batchCount; ++index)
        {
            const WebRendererStaticModelBatchDesc &source = scene.batches[index];
            const WebRendererWorldBatchDesc &draw = source.draw;
            if (draw.sourceKind != WebRendererSceneBatchKind::StaticXModel ||
                !draw.modelIdentity || draw.indexCount == 0u ||
                draw.surfaceCount == 0u || source.instanceCount == 0u ||
                draw.firstIndex != expectedFirstIndex ||
                (draw.firstIndex % 3u) != 0u ||
                (draw.indexCount % 3u) != 0u ||
                draw.firstIndex > scene.indexCount ||
                draw.indexCount > scene.indexCount - draw.firstIndex ||
                source.instanceOffset > scene.instanceCount ||
                source.instanceCount >
                    scene.instanceCount - source.instanceOffset ||
                draw.technique > WebRendererWorldTechnique::BaseTexture ||
                draw.lightingMode >
                    WebRendererWorldLightingMode::ModelLightGrid)
            {
                return WebRendererSurfaceResult::InvalidDescriptor;
            }
            expectedFirstIndex += draw.indexCount;
            WebRendererRetainedStaticModelBatch batch;
            batch.instanceOffset = source.instanceOffset;
            batch.instanceCount = source.instanceCount;
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
            batch.draw.normalSamplerState = draw.normalSamplerState;
            batch.draw.lightmapIndex = 31u;
            batch.draw.sourceKind = draw.sourceKind;
            batch.draw.technique = draw.technique;
            batch.draw.lightingMode = draw.lightingMode;
            batch.draw.techniqueName = draw.techniqueName
                ? draw.techniqueName : "<unsupported-technique>";
            batch.draw.techniqueType = draw.techniqueType;
            batch.draw.customSamplerFlags = draw.customSamplerFlags;
            batch.draw.techniqueFlags = draw.techniqueFlags;
            batch.draw.castsSunShadow = draw.castsSunShadow;
            batch.draw.pixelShaderName = draw.pixelShaderName
                ? draw.pixelShaderName : "<unavailable-pixel-shader>";
            batch.draw.pixelShaderProgramHash = draw.pixelShaderProgramHash;
            batch.draw.baseImageIndex = RetainCanonicalWorldImage(
                draw.baseImage, images, retainedPixelBytes);
            const bool baseSupported =
                batch.draw.baseImageIndex != INVALID_WORLD_IMAGE &&
                images[batch.draw.baseImageIndex].supported;
            if (!baseSupported)
                batch.draw.technique =
                    WebRendererWorldTechnique::BackendFallback;
            batches.push_back(std::move(batch));
        }
        if (expectedFirstIndex != scene.indexCount)
            return WebRendererSurfaceResult::InvalidDescriptor;
        // Preserve base-color coverage under the bounded recovery allowance.
        // Native n0 textures are an optional refinement, so retain them only
        // after every canonical base image has had a chance to publish.
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
        "(%ux%u, six RGBA8 faces, sampler=0x%02x).\n",
        g_renderer.retainedSky.canonicalName.c_str(),
        g_renderer.retainedSky.cube.edgeLength,
        g_renderer.retainedSky.cube.edgeLength,
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
    g_renderer.retainedWorldImages.clear();
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
    WebRendererComparisonCapture intendedComparison;
    const bool comparisonCaptured = WebRenderer_CaptureComparison(
        surface, intendedComparison);
    if (!comparisonCaptured)
    {
        Web_Log(WebLogLevel::Error,
            "[kisakcod-web] Renderer comparison capture was unavailable for "
            "this world command.\n");
    }
    std::vector<WebRendererSurfaceVertex> retainedVertices;
    std::vector<std::uint32_t> retainedIndices;
    std::vector<WebRendererRetainedWorldBatch> retainedBatches;
    std::vector<WebRendererRetainedWorldImage> retainedImages;
    const WebRendererSurfaceResult copy = CopyWorldCommand(
        surface, retainedVertices, retainedIndices, retainedBatches,
        retainedImages);
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
    g_renderer.retainedWorldImages = std::move(retainedImages);
    g_renderer.retainedIndices.clear();
    g_renderer.draw = {
        WebRendererPrimitiveTopology::TriangleList,
        0u,
        surface.indexCount,
        WebRendererTextureBinding::None,
    };
    g_renderer.surfaceActive = true;
    g_renderer.worldSurfaceActive = true;
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
    const std::size_t baseTextureBatches = static_cast<std::size_t>(std::count_if(
        g_renderer.retainedWorldBatches.begin(),
        g_renderer.retainedWorldBatches.end(),
        [](const WebRendererRetainedWorldBatch &batch) {
            return batch.technique == WebRendererWorldTechnique::BaseTexture;
        }));
    if (comparisonCaptured)
    {
        LogNormalizedWorldLightingTrace(surface);
        LogWorldVertexColorInventory(surface);
        LogRetainedLightmapInventory(surface);
        LogWorldColorImageInventory();
    }
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Renderer retained canonical material world command "
        "(%u vertices, %u indices, %u batches: %zu lightmapped, %zu base-only; "
        "%zu/%zu images, %zu geometry bytes).\n",
        surface.vertexCount, surface.indexCount, surface.batchCount,
        lightmappedBatches, baseTextureBatches, supportedImages,
        g_renderer.retainedWorldImages.size(), retainedBytes);
    if (comparisonCaptured)
        EmitWorldComparison(intendedComparison);
    return WebRendererSurfaceResult::Success;
}

// Canonical DB unload calls this native renderer hook before retiring a
// GfxWorld. The web backend keeps the same invariant by dropping the retained
// world command and its GPU objects; the next map publication re-submits it.
void __cdecl R_UnloadWorld()
{
    if (g_renderer.initialized && !g_renderer.contextLost)
    {
        DeleteWorldTextureObjects(g_renderer.retainedWorldImages);
        DeleteWaterTextureObjects(g_renderer.retainedWorldBatches);
        if (g_renderer.retainedSky.texture != 0u)
            glDeleteTextures(1, &g_renderer.retainedSky.texture);
        DeleteSurfaceObjects(
            g_renderer.vertexArray, g_renderer.vertexBuffer,
            g_renderer.indexBuffer);
    }
    g_renderer.vertexArray = 0u;
    g_renderer.vertexBuffer = 0u;
    g_renderer.indexBuffer = 0u;
    g_renderer.retainedVertices.clear();
    g_renderer.retainedIndices.clear();
    g_renderer.retainedWorldIndices.clear();
    g_renderer.retainedWorldBatches.clear();
    g_renderer.retainedWorldImages.clear();
    g_renderer.retainedSky = {};
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
    g_renderer.sceneFilmEnabled = false;
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
    g_renderer.retainedStaticModelInstances = std::move(retainedInstances);
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
    const auto firstFallback = std::find_if(
        g_renderer.retainedStaticModelBatches.begin(),
        g_renderer.retainedStaticModelBatches.end(), isFallbackBatch);
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Renderer retained canonical static XModel command "
        "(%u models, %u surfaces, %u shared vertices, %u indices, %u "
        "instances, %u batches, %zu fallback, %zu shadow-caster; "
        "%zu/%zu images; "
        "model-lighting=%ux%ux%u entries=%u).\n",
        scene.modelCount,
        scene.surfaceCount,
        scene.vertexCount,
        scene.indexCount,
        scene.instanceCount,
        scene.batchCount,
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
    WebRendererRetainedModelLightingAtlas retainedLighting;
    // Dynamic image ownership is persistent across frames. CopyWorldCommand
    // finds canonical identities already present here, so an animated weapon
    // uploads geometry each frame without re-reading or re-decoding its IWI.
    const WebRendererSurfaceResult copy = CopyWorldCommand(
        scene, retainedVertices, retainedIndices, retainedBatches,
        g_renderer.retainedDynamicModelImages);
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
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Renderer retained first canonical dynamic DObj "
            "command (%u vertices, %u indices, %u batches, %zu "
            "shadow-caster; %zu/%zu images; "
            "model-lighting=%ux%ux%u entries=%u).\n",
            scene.vertexCount, scene.indexCount, scene.batchCount,
            shadowCasterBatches,
            supportedImages,
            g_renderer.retainedDynamicModelImages.size(),
            g_renderer.retainedDynamicModelLighting.width,
            g_renderer.retainedDynamicModelLighting.height,
            g_renderer.retainedDynamicModelLighting.depth,
            g_renderer.retainedDynamicModelLighting.entryCount);
    }
    ReportRetainedDynamicFx();
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
    std::size_t retainedPixelBytes = 0u;
    for (const WebRendererRetainedWorldImage &image :
         g_renderer.retainedUiImages)
        retainedPixelBytes += image.pixels.size();
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
    return WebRendererSurfaceResult::Success;
}

bool WebRenderer_Initialize()
{
    if (g_renderer.initialized)
    {
        return true;
    }

    if (!g_renderer.surfaceActive)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] Renderer initialization requires an engine surface.\n");
        Web_EmitRuntimeState(
            "failed",
            "The renderer cannot initialize without an engine surface");
        return false;
    }

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

    g_renderer.initialized = true;
    EmitSurfaceLifecycle(
        "ready",
        "The engine-owned indexed surface is resident in the graphics backend");
    EmitTextureLifecycle(
        "ready",
        "The renderer uploaded its retained texture during initialization");
    if (g_renderer.compatibilityActive)
    {
        EmitShaderLifecycle(
            "ready",
            "The renderer compiled the retained WebGL2 shader contract during initialization");
    }
    return true;
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
bool BuildNearSunShadowMatrix(
    const WebRendererSceneViewDesc &view,
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

    // sm_sunSampleSizeNear defaults to 0.25 world units per texel on a
    // 1024 map. Snap the viewer-centred projection to that texel grid, as
    // R_SetupSunShadowMapProjection does to prevent crawling.
    constexpr float SAMPLE_SIZE = 0.25f;
    constexpr float HALF_EXTENT = SAMPLE_SIZE * 1024.0f * 0.5f;
    const float centerRight = std::round(dot(view.viewOrigin, right) /
        SAMPLE_SIZE) * SAMPLE_SIZE;
    const float centerUp = std::round(dot(view.viewOrigin, up) /
        SAMPLE_SIZE) * SAMPLE_SIZE;
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
    matrix[0] = right[0] / HALF_EXTENT;
    matrix[4] = right[1] / HALF_EXTENT;
    matrix[8] = right[2] / HALF_EXTENT;
    matrix[12] = -centerRight / HALF_EXTENT;
    matrix[1] = up[0] / HALF_EXTENT;
    matrix[5] = up[1] / HALF_EXTENT;
    matrix[9] = up[2] / HALF_EXTENT;
    matrix[13] = -centerUp / HALF_EXTENT;
    matrix[2] = forward[0] * 2.0f / depthRange;
    matrix[6] = forward[1] * 2.0f / depthRange;
    matrix[10] = forward[2] * 2.0f / depthRange;
    matrix[14] = -1.0f - 2.0f * minDepth / depthRange;
    matrix[15] = 1.0f;
    return true;
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
        !std::isfinite(view.blurRadius) || view.blurRadius < 0.0f)
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
    std::copy_n(view.fogColor, 4u, g_renderer.sceneFogColor.begin());
    g_renderer.sceneFogParams = {view.fogStart, view.fogDensity};
    g_renderer.sceneFogEnabled = view.fogEnabled;
    g_renderer.sceneSunShadowEnabled = view.sunShadowEnabled &&
        BuildNearSunShadowMatrix(view, g_renderer.sceneSunShadowMatrix);
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
    g_renderer.sceneFilmEnabled = view.filmEnabled;
    g_renderer.sceneViewWorldName = view.worldName;
    if (view.geometrySubmitted)
    {
        g_renderer.sceneViewSurfaceSubmissionGeneration =
            g_renderer.surfaceSubmissionGeneration;
    }
    if (worldChanged)
    {
        g_renderer.sceneViewFirstDrawCompleted = false;
        g_renderer.sceneViewDrawnSubmissionGeneration = 0u;
    }
    const bool firstSceneViewSubmission =
        g_renderer.sceneViewSubmissionGeneration == 1u;
    const bool firstGeometryViewSubmission =
        view.geometrySubmitted && !g_renderer.sceneViewFirstDrawCompleted;
    const bool settledSceneViewSubmission =
        g_renderer.sceneViewSubmissionGeneration == 30u ||
        g_renderer.sceneViewSubmissionGeneration % 60u == 0u;
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
        if (firstSceneViewSubmission)
        {
            Web_Log(
                WebLogLevel::Info,
                "[kisakcod-web] Canonical cgame view reached R_RenderScene for %s.\n",
                view.worldName);
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
    glUniform1i(g_renderer.materialModeUniform,
        batch.technique == WebRendererWorldTechnique::VertexColorMultiply
            ? 1
            : (batch.technique ==
                    WebRendererWorldTechnique::VertexColorAdditive
                ? 2
                : (batch.technique ==
                        WebRendererWorldTechnique::WaterLitSun
                    ? 3 : 0)));

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
    std::uint8_t samplerState)
{
    glActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, texture);
    const std::uint8_t filter = samplerState & 7u;
    const bool linear = filter == 2u || filter == 3u || filter == 4u;
    const bool mipped = (samplerState & 0x18u) != 0u;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
        linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
        mipped
            ? (linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST)
            : (linear ? GL_LINEAR : GL_NEAREST));
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

bool DrawNearSunShadowMap()
{
    if (!g_renderer.sceneSunShadowEnabled ||
        g_renderer.shadowProgram == 0u ||
        g_renderer.shadowFramebuffer == 0u ||
        g_renderer.shadowDepthTexture == 0u ||
        !g_renderer.worldSurfaceActive)
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.shadowFramebuffer);
    glViewport(0, 0, 1024, 1024);
    glUseProgram(g_renderer.shadowProgram);
    glUniformMatrix4fv(g_renderer.shadowDepthMatrixUniform, 1, GL_FALSE,
        g_renderer.sceneSunShadowMatrix.data());
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
    for (const WebRendererRetainedWorldBatch &batch :
         g_renderer.retainedWorldBatches)
    {
        if (!batch.castsSunShadow || (batch.stateBits[0] & 0x700u) != 0u)
            continue;
        const WebRendererRetainedWorldImage *base =
            WorldImage(batch.baseImageIndex);
        const std::int32_t alphaTest = WorldAlphaTestMode(batch.stateBits[0]);
        glUniform1f(g_renderer.shadowDepthTextureEnabledUniform,
            base ? 1.0f : 0.0f);
        glUniform1i(g_renderer.shadowDepthAlphaTestUniform, alphaTest);
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
    if (g_renderer.staticModelSceneActive &&
        g_renderer.staticModelVertexArray != 0u &&
        g_renderer.staticModelInstanceBuffer != 0u)
    {
        glBindVertexArray(g_renderer.staticModelVertexArray);
        glUniform1f(g_renderer.shadowDepthInstanceEnabledUniform, 1.0f);
        for (const WebRendererRetainedStaticModelBatch &batch :
             g_renderer.retainedStaticModelBatches)
        {
            if (!batch.draw.castsSunShadow) continue;
            const WebRendererRetainedWorldImage *base = RetainedImage(
                g_renderer.retainedStaticModelImages,
                batch.draw.baseImageIndex);
            glUniform1f(g_renderer.shadowDepthTextureEnabledUniform,
                base ? 1.0f : 0.0f);
            glUniform1i(g_renderer.shadowDepthAlphaTestUniform,
                WorldAlphaTestMode(batch.draw.stateBits[0]));
            BindWorldTexture(GL_TEXTURE0,
                base ? base->texture : g_renderer.texture,
                batch.draw.samplerState);
            BindStaticModelInstanceRange(batch.instanceOffset);
            const std::uintptr_t indexOffset =
                static_cast<std::uintptr_t>(batch.draw.firstIndex) *
                sizeof(std::uint32_t);
            glDrawElementsInstanced(GL_TRIANGLES,
                static_cast<GLsizei>(batch.draw.indexCount),
                GL_UNSIGNED_INT,
                reinterpret_cast<const void *>(indexOffset),
                static_cast<GLsizei>(batch.instanceCount));
        }
    }
    if (g_renderer.dynamicModelSceneActive &&
        g_renderer.dynamicModelVertexArray != 0u)
    {
        glBindVertexArray(g_renderer.dynamicModelVertexArray);
        glUniform1f(g_renderer.shadowDepthInstanceEnabledUniform, 0.0f);
        for (const WebRendererRetainedWorldBatch &batch :
             g_renderer.retainedDynamicModelBatches)
        {
            if (!batch.castsSunShadow || batch.depthHack ||
                WebRenderer_IsFxVertexColorBatch(batch.sourceKind))
                continue;
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

void DrawPostProcessPass(
    GLuint sourceTexture,
    GLuint destinationFramebuffer,
    int width,
    int height,
    bool filmEnabled,
    float gammaExponent,
    float blurRadius)
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
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
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
    const bool postProcessDraw = sceneGeometryDraw &&
        g_renderer.postProcessProgram != 0u &&
        CreatePostProcessTargets(width, height);
    const bool shadowMapDrawn = sceneGeometryDraw &&
        DrawNearSunShadowMap();
    glBindFramebuffer(GL_FRAMEBUFFER,
        postProcessDraw ? g_renderer.sceneFramebuffer : 0u);

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
        glUniform1i(g_renderer.normalMapUniform, 1);
        glUniform1i(g_renderer.secondaryLightmapUniform, 2);
        glUniform1i(g_renderer.modelLightingUniform, 3);
        glUniform1i(g_renderer.shadowMapUniform, 6);
        glUniform1i(g_renderer.waterMapUniform, 7);
        glUniform1i(g_renderer.reflectionProbeUniform, 8);
        glUniformMatrix4fv(g_renderer.shadowMatrixUniform, 1, GL_FALSE,
            g_renderer.sceneSunShadowMatrix.data());
        glUniform3fv(g_renderer.sunDirectionUniform, 1,
            g_renderer.sceneSunDirection.data());
        glUniform3fv(g_renderer.sunColorUniform, 1,
            g_renderer.sceneSunColor.data());
        glUniform1f(g_renderer.sunShadowEnabledUniform, 0.0f);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D,
            shadowMapDrawn ? g_renderer.shadowDepthTexture :
                g_renderer.texture);
        glActiveTexture(GL_TEXTURE0);
        glUniform1f(g_renderer.secondaryLightmapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.modelLightingEnabledUniform, 0.0f);
        glUniform1f(g_renderer.normalMapEnabledUniform, 0.0f);
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
    if (worldBatchDraw)
    {
        for (WebRendererRetainedWorldBatch &batch :
             g_renderer.retainedWorldBatches)
        {
            ApplyWorldMaterialState(batch);
            const WebRendererRetainedWorldImage *base =
                WorldImage(batch.baseImageIndex);
            const WebRendererRetainedWorldImage *secondaryLightmap =
                WorldImage(batch.secondaryLightmapImageIndex);
            const WebRendererRetainedWorldImage *normal =
                WorldImage(batch.normalImageIndex);
            const bool water = batch.technique ==
                WebRendererWorldTechnique::WaterLitSun;
            const bool waterReady = water && BindWaterTextures(
                batch, g_renderer.sceneViewTimeSeconds);
            const bool fallback = water
                ? !waterReady
                : batch.technique ==
                        WebRendererWorldTechnique::BackendFallback || !base;
            const bool lightmapped = !fallback && secondaryLightmap &&
                WebRenderer_UsesSecondaryDirectionalLightmap(
                    batch.technique) &&
                batch.lightingMode ==
                    WebRendererWorldLightingMode::SecondaryDirectional;
            const bool normalMapped = lightmapped && normal &&
                WebRenderer_UsesWorldNormalMap(batch.technique);
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
            glUniform1f(g_renderer.lightmapEnabledUniform,
                lightmapped ? 1.0f : 0.0f);
            glUniform1f(g_renderer.secondaryLightmapEnabledUniform,
                lightmapped ? 1.0f : 0.0f);
            glUniform1f(g_renderer.modelLightingEnabledUniform, 0.0f);
            glUniform1f(g_renderer.normalMapEnabledUniform,
                normalMapped ? 1.0f : 0.0f);
            glUniform1f(g_renderer.sunShadowEnabledUniform,
                shadowMapDrawn && lightmapped &&
                        batch.techniqueType == 9u
                    ? 1.0f : 0.0f);
            BindWorldTexture(
                GL_TEXTURE0,
                base ? base->texture : g_renderer.texture,
                batch.samplerState);
            BindWorldTexture(
                GL_TEXTURE1,
                normal ? normal->texture : g_renderer.texture,
                batch.normalSamplerState);
            BindWorldTexture(
                GL_TEXTURE2,
                secondaryLightmap
                    ? secondaryLightmap->texture : g_renderer.texture,
                0x62u);
            const std::uintptr_t indexOffset =
                static_cast<std::uintptr_t>(batch.firstIndex) *
                sizeof(std::uint32_t);
            glDrawElements(
                GL_TRIANGLES,
                static_cast<GLsizei>(batch.indexCount),
                GL_UNSIGNED_INT,
                reinterpret_cast<const void *>(indexOffset));
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
            glUniform1f(g_renderer.normalMapEnabledUniform, 0.0f);
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
        completedDraws = 1u;
    }
    if (sceneGeometryDraw && !compatibilityDraw &&
        g_renderer.staticModelSceneActive &&
        g_renderer.staticModelVertexArray != 0u &&
        g_renderer.staticModelInstanceBuffer != 0u)
    {
        glBindVertexArray(g_renderer.staticModelVertexArray);
        glUniform1f(g_renderer.sunShadowEnabledUniform, 0.0f);
        glUniform1f(g_renderer.instanceEnabledUniform, 1.0f);
        glUniform1f(g_renderer.lightmapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.secondaryLightmapEnabledUniform, 0.0f);
        BindModelLightingTexture(g_renderer.retainedStaticModelLighting);
        for (const WebRendererRetainedStaticModelBatch &batch :
             g_renderer.retainedStaticModelBatches)
        {
            ApplyWorldMaterialState(batch.draw);
            const WebRendererRetainedWorldImage *base = RetainedImage(
                g_renderer.retainedStaticModelImages,
                batch.draw.baseImageIndex);
            const WebRendererRetainedWorldImage *normal = RetainedImage(
                g_renderer.retainedStaticModelImages,
                batch.draw.normalImageIndex);
            const bool fallback = batch.draw.technique ==
                    WebRendererWorldTechnique::BackendFallback ||
                !base;
            const bool modelLit = !fallback &&
                batch.draw.lightingMode ==
                    WebRendererWorldLightingMode::ModelLightGrid &&
                g_renderer.retainedStaticModelLighting.texture != 0u;
            const bool normalMapped = modelLit && normal &&
                normal->texture != 0u;
            glUniform1f(g_renderer.sceneFallbackUniform,
                fallback ? 1.0f : 0.0f);
            glUniform1f(g_renderer.textureEnabledUniform,
                fallback ? 0.0f : 1.0f);
            glUniform1f(g_renderer.modelLightingEnabledUniform,
                modelLit ? 1.0f : 0.0f);
            glUniform1f(g_renderer.normalMapEnabledUniform,
                normalMapped ? 1.0f : 0.0f);
            BindWorldTexture(
                GL_TEXTURE0,
                base ? base->texture : g_renderer.texture,
                batch.draw.samplerState);
            BindWorldTexture(
                GL_TEXTURE1,
                normal ? normal->texture : g_renderer.texture,
                batch.draw.normalSamplerState);
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
    if (sceneGeometryDraw && !compatibilityDraw &&
        g_renderer.dynamicModelSceneActive &&
        g_renderer.dynamicModelVertexArray != 0u)
    {
        glBindVertexArray(g_renderer.dynamicModelVertexArray);
        glUniform1f(g_renderer.sunShadowEnabledUniform, 0.0f);
        glUniform1f(g_renderer.instanceEnabledUniform, 0.0f);
        glUniform1f(g_renderer.lightmapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.secondaryLightmapEnabledUniform, 0.0f);
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
                glUniformMatrix4fv(g_renderer.viewProjectionUniform, 1,
                    GL_FALSE,
                    batch.depthHack
                        ? g_renderer.sceneDepthHackViewProjection.data()
                        : g_renderer.sceneViewProjection.data());
                glDepthRangef(0.0f, batch.depthHack ? 0.015625f : 1.0f);
                ApplyWorldMaterialState(batch);
                const WebRendererRetainedWorldImage *base = RetainedImage(
                    g_renderer.retainedDynamicModelImages,
                    batch.baseImageIndex);
                const WebRendererRetainedWorldImage *secondaryLightmap =
                    RetainedImage(g_renderer.retainedDynamicModelImages,
                        batch.secondaryLightmapImageIndex);
                const WebRendererRetainedWorldImage *normal = RetainedImage(
                    g_renderer.retainedDynamicModelImages,
                    batch.normalImageIndex);
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
                const bool normalMapped = modelLit && normal &&
                    normal->texture != 0u;
                glUniform1f(g_renderer.fogEnabledUniform,
                    g_renderer.sceneFogEnabled && !fxSceneGeometry
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
                glUniform1f(g_renderer.normalMapEnabledUniform,
                    normalMapped ? 1.0f : 0.0f);
                glUniform3fv(g_renderer.modelLightingBaseCoordinatesUniform,
                    1, batch.modelLightingCoordinates);
                BindWorldTexture(GL_TEXTURE0,
                    base ? base->texture : g_renderer.texture,
                    batch.samplerState);
                BindWorldTexture(GL_TEXTURE1,
                    normal ? normal->texture : g_renderer.texture,
                    batch.normalSamplerState);
                BindWorldTexture(GL_TEXTURE2,
                    secondaryLightmap
                        ? secondaryLightmap->texture : g_renderer.texture,
                    0x62u);
                const std::uintptr_t indexOffset =
                    static_cast<std::uintptr_t>(batch.firstIndex) *
                    sizeof(std::uint32_t);
                glDrawElements(GL_TRIANGLES,
                    static_cast<GLsizei>(batch.indexCount),
                    GL_UNSIGNED_INT,
                    reinterpret_cast<const void *>(indexOffset));
                ++completedDraws;
            }
        }
        glDepthRangef(0.0f, 1.0f);
        glUniformMatrix4fv(g_renderer.viewProjectionUniform, 1, GL_FALSE,
            g_renderer.sceneViewProjection.data());
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
    }
    if (postProcessDraw)
    {
        // COD4 resolves and color-manipulates the 3D scene before any 2D
        // commands. Keep the pass separate from the final display gamma so
        // campaign vision tint never contaminates HUD colors.
        DrawPostProcessPass(
            g_renderer.sceneColorTexture,
            g_renderer.compositeFramebuffer,
            width,
            height,
            g_renderer.sceneFilmEnabled,
            1.0f,
            g_renderer.sceneBlurRadius);
    }
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
        glUniform1f(g_renderer.normalMapEnabledUniform, 0.0f);
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
    if (postProcessDraw)
    {
        // Native R_CalcGammaRamp affects the finished frame, including 2D.
        // The browser has no D3D hardware ramp, so reproduce it only at this
        // final framebuffer boundary.
        DrawPostProcessPass(
            g_renderer.compositeColorTexture,
            0u,
            width,
            height,
            false,
            g_renderer.sceneDisplayGammaExponent,
            0.0f);
    }
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
