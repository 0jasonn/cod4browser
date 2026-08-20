#include <web/web_renderer.h>

#include <web/web_renderer_surface_storage.h>
#include <web/web_system.h>

#include <gfx_d3d/gfx_image_types.h>
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
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint32_t INVALID_WORLD_IMAGE = UINT32_MAX;
constexpr std::size_t WEB_RENDERER_MAX_WORLD_TEXTURE_BYTES =
    256u * 1024u * 1024u;

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
    std::uint32_t lightmapImageIndex = INVALID_WORLD_IMAGE;
    std::uint32_t stateBits[2]{};
    std::uint8_t samplerState = 0u;
    std::uint8_t lightmapIndex = 31u;
    WebRendererSceneBatchKind sourceKind =
        WebRendererSceneBatchKind::WorldSurface;
    WebRendererWorldTechnique technique =
        WebRendererWorldTechnique::BackendFallback;
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
    float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
};

struct WebRendererState
{
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
    GLuint program = 0;
    GLuint compatibilityProgram = 0;
    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;
    GLuint indexBuffer = 0;
    GLuint texture = 0;
    GLint aspectUniform = -1;
    GLint textureUniform = -1;
    GLint textureEnabledUniform = -1;
    GLint viewProjectionUniform = -1;
    GLint sceneFallbackUniform = -1;
    GLint lightmapUniform = -1;
    GLint lightmapEnabledUniform = -1;
    GLint alphaTestUniform = -1;
    GLint instanceEnabledUniform = -1;
    GLint uiColorUniform = -1;
    GLint compatibilityViewProjectionUniform = -1;
    GLint compatibilityWorldUniform = -1;
    GLint compatibilityTextureUniform = -1;
    int frameNumber = 0;
    int canvasWidth = 0;
    int canvasHeight = 0;
    bool contextLost = false;
    bool initialized = false;
    std::vector<WebRendererSurfaceVertex> retainedVertices;
    std::vector<std::uint16_t> retainedIndices;
    std::vector<std::uint32_t> retainedWorldIndices;
    std::vector<WebRendererRetainedWorldBatch> retainedWorldBatches;
    std::vector<WebRendererRetainedWorldImage> retainedWorldImages;
    GLuint staticModelVertexArray = 0u;
    GLuint staticModelVertexBuffer = 0u;
    GLuint staticModelIndexBuffer = 0u;
    GLuint staticModelInstanceBuffer = 0u;
    std::vector<WebRendererSurfaceVertex> retainedStaticModelVertices;
    std::vector<std::uint32_t> retainedStaticModelIndices;
    std::vector<WebRendererStaticModelInstanceDesc> retainedStaticModelInstances;
    std::vector<WebRendererRetainedStaticModelBatch> retainedStaticModelBatches;
    std::vector<WebRendererRetainedWorldImage> retainedStaticModelImages;
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
    bool dynamicModelSceneActive = false;
    bool dynamicModelFirstSubmissionReported = false;
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
    std::uint32_t sceneViewX = 0u;
    std::uint32_t sceneViewY = 0u;
    std::uint32_t sceneViewWidth = 0u;
    std::uint32_t sceneViewHeight = 0u;
    std::string sceneViewWorldName;
    bool sceneViewActive = false;
    bool sceneViewGeometrySubmitted = false;
    bool sceneViewFirstDrawCompleted = false;
};

WebRendererState g_renderer;
constexpr std::uint8_t FALLBACK_TEXTURE_RGBA[] = {255u, 255u, 255u, 255u};
bool HandleWebGLContextLost(int, const void *, void *);
bool HandleWebGLContextRestored(int, const void *, void *);
void DeleteWorldTextureObjects(
    std::vector<WebRendererRetainedWorldImage> &images);
void DeleteSurfaceObjects(
    GLuint vertexArray, GLuint vertexBuffer, GLuint indexBuffer);
void DeleteStaticModelObjects(
    GLuint vertexArray, GLuint vertexBuffer, GLuint indexBuffer,
    GLuint instanceBuffer);

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
    g_renderer.compatibilityProgram = 0;
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
    g_renderer.viewProjectionUniform = -1;
    g_renderer.sceneFallbackUniform = -1;
    g_renderer.lightmapUniform = -1;
    g_renderer.lightmapEnabledUniform = -1;
    g_renderer.alphaTestUniform = -1;
    g_renderer.instanceEnabledUniform = -1;
    g_renderer.uiColorUniform = -1;
    g_renderer.compatibilityViewProjectionUniform = -1;
    g_renderer.compatibilityWorldUniform = -1;
    g_renderer.compatibilityTextureUniform = -1;
    g_renderer.canvasWidth = 0;
    g_renderer.canvasHeight = 0;
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

void DeletePipelineObjects(
    GLuint program,
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
    if (compatibilityProgram != 0)
    {
        glDeleteProgram(compatibilityProgram);
    }
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
        DeleteWorldTextureObjects(g_renderer.retainedStaticModelImages);
        DeleteWorldTextureObjects(g_renderer.retainedDynamicModelImages);
        DeleteWorldTextureObjects(g_renderer.retainedUiImages);
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
        DeletePipelineObjects(
            g_renderer.program,
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
        uniform float u_aspect;
        uniform mat4 u_view_projection;
        uniform float u_scene_fallback;
        uniform float u_instance_enabled;
        out vec4 v_color;
        out vec2 v_texcoord;
        out vec3 v_world_position;
        out vec2 v_lightmap_coord;

        void main()
        {
            vec3 position = a_position;
            if (u_instance_enabled > 0.5)
            {
                position = a_instance_origin +
                    a_position.x * a_instance_axis0 +
                    a_position.y * a_instance_axis1 +
                    a_position.z * a_instance_axis2;
            }
            position.x *= min(1.0, u_aspect);
            position.y *= min(1.0, 1.0 / u_aspect);
            gl_Position = u_view_projection * vec4(position, 1.0);
            v_color = a_color;
            v_texcoord = a_texcoord;
            v_world_position = a_position;
            v_lightmap_coord = a_lightmap_coord;
        }
    )glsl";

    constexpr const char *fragmentSource = R"glsl(#version 300 es
        precision highp float;
        in vec4 v_color;
        in vec2 v_texcoord;
        in vec3 v_world_position;
        in vec2 v_lightmap_coord;
        uniform sampler2D u_texture;
        uniform sampler2D u_lightmap;
        uniform float u_texture_enabled;
        uniform float u_lightmap_enabled;
        uniform float u_scene_fallback;
        uniform int u_alpha_test;
        uniform vec4 u_ui_color;
        out vec4 out_color;

        void main()
        {
            vec4 texel = texture(u_texture, v_texcoord);
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
            else if (u_texture_enabled > 0.5)
            {
                if ((u_alpha_test == 1 && texel.a <= 0.0) ||
                    (u_alpha_test == 2 && texel.a >= (128.0 / 255.0)) ||
                    (u_alpha_test == 3 && texel.a < (128.0 / 255.0)))
                    discard;
                bootstrap_color = texel * v_color;
                if (u_lightmap_enabled > 0.5)
                {
                    vec3 lightmap = texture(u_lightmap, v_lightmap_coord).rgb;
                    bootstrap_color.rgb *= min(lightmap * 2.0, vec3(1.5));
                }
            }
            out_color = bootstrap_color * u_ui_color;
        }
    )glsl";

    GLuint program = 0;
    if (!LinkProgram("bootstrap", vertexSource, fragmentSource, program)) return false;

    while (glGetError() != GL_NO_ERROR)
    {
    }

    const GLint aspectUniform = glGetUniformLocation(program, "u_aspect");
    const GLint textureUniform = glGetUniformLocation(program, "u_texture");
    const GLint textureEnabledUniform =
        glGetUniformLocation(program, "u_texture_enabled");
    const GLint viewProjectionUniform =
        glGetUniformLocation(program, "u_view_projection");
    const GLint sceneFallbackUniform =
        glGetUniformLocation(program, "u_scene_fallback");
    const GLint lightmapUniform = glGetUniformLocation(program, "u_lightmap");
    const GLint lightmapEnabledUniform =
        glGetUniformLocation(program, "u_lightmap_enabled");
    const GLint alphaTestUniform = glGetUniformLocation(program, "u_alpha_test");
    const GLint instanceEnabledUniform =
        glGetUniformLocation(program, "u_instance_enabled");
    const GLint uiColorUniform = glGetUniformLocation(program, "u_ui_color");
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
        viewProjectionUniform < 0 || sceneFallbackUniform < 0 ||
        lightmapUniform < 0 || lightmapEnabledUniform < 0 ||
        alphaTestUniform < 0 || instanceEnabledUniform < 0 ||
        uiColorUniform < 0 ||
        pipelineError != GL_NO_ERROR ||
        !surfaceReady || !textureReady || !worldTexturesReady ||
        !staticModelObjectsReady || !staticModelTexturesReady ||
        !dynamicModelObjectsReady || !dynamicModelTexturesReady ||
        !uiObjectsReady || !uiTexturesReady ||
        !compatibilityReady)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] WebGL2 resource creation failed (0x%x).\n",
            static_cast<unsigned int>(pipelineError));
        DeletePipelineObjects(
            program,
            compatibilityProgram,
            vertexArray,
            vertexBuffer,
            indexBuffer,
            texture);
        DeleteWorldTextureObjects(g_renderer.retainedWorldImages);
        DeleteWorldTextureObjects(g_renderer.retainedStaticModelImages);
        DeleteWorldTextureObjects(g_renderer.retainedDynamicModelImages);
        DeleteWorldTextureObjects(g_renderer.retainedUiImages);
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
    g_renderer.aspectUniform = aspectUniform;
    g_renderer.textureUniform = textureUniform;
    g_renderer.textureEnabledUniform = textureEnabledUniform;
    g_renderer.viewProjectionUniform = viewProjectionUniform;
    g_renderer.sceneFallbackUniform = sceneFallbackUniform;
    g_renderer.lightmapUniform = lightmapUniform;
    g_renderer.lightmapEnabledUniform = lightmapEnabledUniform;
    g_renderer.alphaTestUniform = alphaTestUniform;
    g_renderer.instanceEnabledUniform = instanceEnabledUniform;
    g_renderer.uiColorUniform = uiColorUniform;
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
    if (canonical->mapType == MAPTYPE_2D && hasLoadDef &&
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
    else if (canonical->mapType == MAPTYPE_2D && canonical->name &&
        canonical->name[0] && canonical->name[0] != '$' &&
        std::strlen(canonical->name) <= 240u)
    {
        const std::string path = std::string("images/") + canonical->name + ".iwi";
        int file = 0;
        const int fileSize = FS_FOpenFileReadDatabase(path.c_str(), &file);
        if (fileSize >= 0)
        {
            attemptedDecode = true;
            if (fileSize > 0 &&
                static_cast<std::size_t>(fileSize) <=
                    kisak::iwi::MAX_TEXTURE_MEMBER_BYTES)
            {
                std::vector<std::uint8_t> bytes(
                    static_cast<std::size_t>(fileSize));
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
        images.reserve(surface.batchCount * 2u);
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
                source.technique > WebRendererWorldTechnique::BaseTextureLightmap)
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
            batch.lightmapIndex = source.lightmapIndex;
            batch.sourceKind = source.sourceKind;
            batch.technique = source.technique;
            batch.baseImageIndex = RetainCanonicalWorldImage(
                source.baseImage, images, retainedPixelBytes);
            batch.lightmapImageIndex = RetainCanonicalWorldImage(
                source.lightmapImage, images, retainedPixelBytes);
            const bool baseSupported =
                batch.baseImageIndex != INVALID_WORLD_IMAGE &&
                images[batch.baseImageIndex].supported;
            const bool lightmapSupported =
                batch.lightmapImageIndex != INVALID_WORLD_IMAGE &&
                images[batch.lightmapImageIndex].supported;
            if (!baseSupported)
                batch.technique = WebRendererWorldTechnique::BackendFallback;
            else if (batch.technique ==
                    WebRendererWorldTechnique::BaseTextureLightmap &&
                !lightmapSupported)
                batch.technique = WebRendererWorldTechnique::BaseTexture;
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
        for (std::size_t component = 0u; component < 11u; ++component)
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
                draw.technique > WebRendererWorldTechnique::BaseTexture)
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
            batch.draw.lightmapIndex = 31u;
            batch.draw.sourceKind = draw.sourceKind;
            batch.draw.technique = draw.technique;
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
    if (replacementVertexArray != 0)
    {
        DeleteWorldTextureObjects(g_renderer.retainedWorldImages);
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
            return batch.technique ==
                WebRendererWorldTechnique::BaseTextureLightmap;
        }));
    const std::size_t baseTextureBatches = static_cast<std::size_t>(std::count_if(
        g_renderer.retainedWorldBatches.begin(),
        g_renderer.retainedWorldBatches.end(),
        [](const WebRendererRetainedWorldBatch &batch) {
            return batch.technique == WebRendererWorldTechnique::BaseTexture;
        }));
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Renderer retained canonical material world command "
        "(%u vertices, %u indices, %u batches: %zu lightmapped, %zu base-only; "
        "%zu/%zu images, %zu geometry bytes).\n",
        surface.vertexCount, surface.indexCount, surface.batchCount,
        lightmappedBatches, baseTextureBatches, supportedImages,
        g_renderer.retainedWorldImages.size(), retainedBytes);
    return WebRendererSurfaceResult::Success;
}

WebRendererSurfaceResult WebRenderer_SetStaticModelScene(
    const WebRendererStaticModelSceneDesc &scene)
{
    std::vector<WebRendererSurfaceVertex> retainedVertices;
    std::vector<std::uint32_t> retainedIndices;
    std::vector<WebRendererStaticModelInstanceDesc> retainedInstances;
    std::vector<WebRendererRetainedStaticModelBatch> retainedBatches;
    std::vector<WebRendererRetainedWorldImage> retainedImages;
    const WebRendererSurfaceResult copy = CopyStaticModelCommand(
        scene,
        retainedVertices,
        retainedIndices,
        retainedInstances,
        retainedBatches,
        retainedImages);
    if (copy != WebRendererSurfaceResult::Success) return copy;

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
    if (hasContext)
    {
        DeleteWorldTextureObjects(g_renderer.retainedStaticModelImages);
        DeleteStaticModelObjects(
            g_renderer.staticModelVertexArray,
            g_renderer.staticModelVertexBuffer,
            g_renderer.staticModelIndexBuffer,
            g_renderer.staticModelInstanceBuffer);
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
    g_renderer.staticModelCount = scene.modelCount;
    g_renderer.staticModelSurfaceCount = scene.surfaceCount;
    g_renderer.staticModelSceneActive = true;

    const std::size_t supportedImages = static_cast<std::size_t>(std::count_if(
        g_renderer.retainedStaticModelImages.begin(),
        g_renderer.retainedStaticModelImages.end(),
        [](const WebRendererRetainedWorldImage &image) {
            return image.supported;
        }));
    Web_Log(WebLogLevel::Info,
        "[kisakcod-web] Renderer retained canonical static XModel command "
        "(%u models, %u surfaces, %u shared vertices, %u indices, %u "
        "instances, %u batches; %zu/%zu images).\n",
        scene.modelCount,
        scene.surfaceCount,
        scene.vertexCount,
        scene.indexCount,
        scene.instanceCount,
        scene.batchCount,
        supportedImages,
        g_renderer.retainedStaticModelImages.size());
    return WebRendererSurfaceResult::Success;
}

WebRendererSurfaceResult WebRenderer_SetDynamicModelScene(
    const WebRendererWorldSurfaceDesc &scene)
{
    if (scene.vertexCount == 0u && scene.indexCount == 0u &&
        scene.batchCount == 0u)
    {
        if (scene.vertices || scene.indices || scene.batches)
            return WebRendererSurfaceResult::InvalidDescriptor;
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
    // Dynamic image ownership is persistent across frames. CopyWorldCommand
    // finds canonical identities already present here, so an animated weapon
    // uploads geometry each frame without re-reading or re-decoding its IWI.
    const WebRendererSurfaceResult copy = CopyWorldCommand(
        scene, retainedVertices, retainedIndices, retainedBatches,
        g_renderer.retainedDynamicModelImages);
    if (copy != WebRendererSurfaceResult::Success) return copy;

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
    if (hasContext)
    {
        DeleteSurfaceObjects(
            g_renderer.dynamicModelVertexArray,
            g_renderer.dynamicModelVertexBuffer,
            g_renderer.dynamicModelIndexBuffer);
        g_renderer.dynamicModelVertexArray = vertexArray;
        g_renderer.dynamicModelVertexBuffer = vertexBuffer;
        g_renderer.dynamicModelIndexBuffer = indexBuffer;
    }
    g_renderer.retainedDynamicModelVertices = std::move(retainedVertices);
    g_renderer.retainedDynamicModelIndices = std::move(retainedIndices);
    g_renderer.retainedDynamicModelBatches = std::move(retainedBatches);
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
        Web_Log(WebLogLevel::Info,
            "[kisakcod-web] Renderer retained first canonical dynamic DObj "
            "command (%u vertices, %u indices, %u batches; %zu/%zu images).\n",
            scene.vertexCount, scene.indexCount, scene.batchCount,
            supportedImages,
            g_renderer.retainedDynamicModelImages.size());
    }
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
    for (std::size_t index = 0; index < 16u; ++index)
    {
        if (!std::isfinite(viewProjection[index])) return false;
    }
    if (!std::isfinite(view.tanHalfFovX) ||
        !std::isfinite(view.tanHalfFovY) || !std::isfinite(view.zNear) ||
        view.zNear <= 0.0f)
    {
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
    std::copy(viewProjection, viewProjection + 16u,
        g_renderer.sceneViewProjection.begin());
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

    const double elapsed = static_cast<double>(frame.monotonicMilliseconds) / 1000.0;
    const float wave = static_cast<float>(0.5 + 0.5 * std::sin(elapsed * 0.35));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glClearColor(0.025f + wave * 0.012f, 0.03f, 0.035f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const bool sceneGeometryDraw = g_renderer.sceneViewActive &&
        g_renderer.sceneViewGeometrySubmitted &&
        g_renderer.sceneViewSurfaceSubmissionGeneration ==
            g_renderer.surfaceSubmissionGeneration;
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
        glUniform1i(g_renderer.lightmapUniform, 1);
        glUniform1f(g_renderer.instanceEnabledUniform, 0.0f);
        glUniform4f(g_renderer.uiColorUniform, 1.0f, 1.0f, 1.0f, 1.0f);
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
        for (const WebRendererRetainedWorldBatch &batch :
             g_renderer.retainedWorldBatches)
        {
            ApplyWorldMaterialState(batch);
            const WebRendererRetainedWorldImage *base =
                WorldImage(batch.baseImageIndex);
            const WebRendererRetainedWorldImage *lightmap =
                WorldImage(batch.lightmapImageIndex);
            const bool fallback = batch.technique ==
                    WebRendererWorldTechnique::BackendFallback ||
                !base;
            const bool lightmapped = !fallback && lightmap &&
                batch.technique ==
                    WebRendererWorldTechnique::BaseTextureLightmap;
            glUniform1f(g_renderer.sceneFallbackUniform,
                fallback ? 1.0f : 0.0f);
            glUniform1f(g_renderer.textureEnabledUniform,
                fallback ? 0.0f : 1.0f);
            glUniform1f(g_renderer.lightmapEnabledUniform,
                lightmapped ? 1.0f : 0.0f);
            BindWorldTexture(
                GL_TEXTURE0,
                base ? base->texture : g_renderer.texture,
                batch.samplerState);
            BindWorldTexture(
                GL_TEXTURE1,
                lightmap ? lightmap->texture : g_renderer.texture,
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
            glUniform1i(g_renderer.alphaTestUniform, 0);
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
        glUniform1f(g_renderer.instanceEnabledUniform, 1.0f);
        glUniform1f(g_renderer.lightmapEnabledUniform, 0.0f);
        for (const WebRendererRetainedStaticModelBatch &batch :
             g_renderer.retainedStaticModelBatches)
        {
            ApplyWorldMaterialState(batch.draw);
            const WebRendererRetainedWorldImage *base = RetainedImage(
                g_renderer.retainedStaticModelImages,
                batch.draw.baseImageIndex);
            const bool fallback = batch.draw.technique ==
                    WebRendererWorldTechnique::BackendFallback ||
                !base;
            glUniform1f(g_renderer.sceneFallbackUniform,
                fallback ? 1.0f : 0.0f);
            glUniform1f(g_renderer.textureEnabledUniform,
                fallback ? 0.0f : 1.0f);
            BindWorldTexture(
                GL_TEXTURE0,
                base ? base->texture : g_renderer.texture,
                batch.draw.samplerState);
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
        glUniform1f(g_renderer.instanceEnabledUniform, 0.0f);
        glUniform1f(g_renderer.lightmapEnabledUniform, 0.0f);
        for (const WebRendererRetainedWorldBatch &batch :
             g_renderer.retainedDynamicModelBatches)
        {
            ApplyWorldMaterialState(batch);
            const WebRendererRetainedWorldImage *base = RetainedImage(
                g_renderer.retainedDynamicModelImages,
                batch.baseImageIndex);
            const bool fxSceneGeometry = batch.sourceKind ==
                    WebRendererSceneBatchKind::FxCodeMesh ||
                batch.sourceKind == WebRendererSceneBatchKind::FxXModel;
            const bool fallback = batch.technique ==
                    WebRendererWorldTechnique::BackendFallback ||
                !base;
            glUniform1f(g_renderer.sceneFallbackUniform,
                fallback && !fxSceneGeometry ? 1.0f : 0.0f);
            glUniform1f(g_renderer.textureEnabledUniform,
                fallback ? 0.0f : 1.0f);
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
            ++completedDraws;
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
    }
    if (sceneGeometryDraw && !compatibilityDraw &&
        g_renderer.uiSceneActive && g_renderer.uiVertexArray != 0u)
    {
        glUseProgram(g_renderer.program);
        glUniformMatrix4fv(g_renderer.viewProjectionUniform, 1, GL_FALSE,
            IDENTITY_MATRIX);
        glUniform1f(g_renderer.aspectUniform, 1.0f);
        glUniform1f(g_renderer.sceneFallbackUniform, 0.0f);
        glUniform1f(g_renderer.lightmapEnabledUniform, 0.0f);
        glUniform1f(g_renderer.instanceEnabledUniform, 0.0f);
        glUniform1i(g_renderer.alphaTestUniform, 0);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBindVertexArray(g_renderer.uiVertexArray);
        for (const WebRendererRetainedUiBatch &batch :
             g_renderer.retainedUiBatches)
        {
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
