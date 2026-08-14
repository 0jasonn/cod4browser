#include <web/web_renderer.h>

#include <web/web_renderer_surface_storage.h>
#include <web/web_system.h>

#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <vector>

namespace
{
struct WebRendererState
{
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
    GLuint program = 0;
    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;
    GLuint indexBuffer = 0;
    GLuint texture = 0;
    GLint aspectUniform = -1;
    GLint textureUniform = -1;
    GLint textureEnabledUniform = -1;
    int frameNumber = 0;
    int canvasWidth = 0;
    int canvasHeight = 0;
    bool contextLost = false;
    bool initialized = false;
    std::vector<WebRendererSurfaceVertex> retainedVertices;
    std::vector<std::uint16_t> retainedIndices;
    WebRendererDrawDesc draw{
        WebRendererPrimitiveTopology::TriangleList,
        0u,
        0u,
        WebRendererTextureBinding::None,
    };
    std::uint32_t surfaceSubmissionGeneration = 0;
    std::uint32_t surfaceResourceGeneration = 0;
    std::uint32_t surfaceRecoveryCount = 0;
    bool surfaceActive = false;
    std::vector<std::uint8_t> retainedPixels;
    std::uint32_t textureWidth = 0;
    std::uint32_t textureHeight = 0;
    std::uint32_t uploadGeneration = 0;
    std::uint32_t rebuildGeneration = 0;
    std::uint32_t recoveryCount = 0;
    bool sourceTextureActive = false;
};

WebRendererState g_renderer;
constexpr std::uint8_t FALLBACK_TEXTURE_RGBA[] = {255u, 255u, 255u, 255u};

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
                resident: Boolean(resident)
            }
        }));
    });

const char *SurfaceTopologyString(WebRendererPrimitiveTopology topology) noexcept
{
    return topology == WebRendererPrimitiveTopology::TriangleList
        ? "triangle-list"
        : "unsupported";
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
        g_renderer.retainedIndices.size() * sizeof(std::uint16_t);
    if (vertexBytes > UINT32_MAX || indexBytes > UINT32_MAX)
    {
        return;
    }
    DispatchRendererSurfaceLifecycle(
        state,
        message,
        static_cast<std::uint32_t>(g_renderer.retainedVertices.size()),
        static_cast<std::uint32_t>(g_renderer.retainedIndices.size()),
        g_renderer.draw.firstIndex,
        g_renderer.draw.indexCount,
        static_cast<std::uint32_t>(vertexBytes),
        static_cast<std::uint32_t>(indexBytes),
        SurfaceTopologyString(g_renderer.draw.topology),
        SurfaceTextureBindingString(g_renderer.draw.textureBinding),
        g_renderer.surfaceSubmissionGeneration,
        g_renderer.surfaceResourceGeneration,
        g_renderer.surfaceRecoveryCount,
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

void ResetGpuHandles()
{
    g_renderer.program = 0;
    g_renderer.vertexArray = 0;
    g_renderer.vertexBuffer = 0;
    g_renderer.indexBuffer = 0;
    g_renderer.texture = 0;
    g_renderer.aspectUniform = -1;
    g_renderer.textureUniform = -1;
    g_renderer.textureEnabledUniform = -1;
    g_renderer.canvasWidth = 0;
    g_renderer.canvasHeight = 0;
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

void DeletePipelineObjects(
    GLuint program,
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
        "#game-canvas", nullptr, EM_TRUE, nullptr);
    (void)emscripten_set_webglcontextrestored_callback(
        "#game-canvas", nullptr, EM_TRUE, nullptr);

    if (emscripten_webgl_make_context_current(g_renderer.context) ==
        EMSCRIPTEN_RESULT_SUCCESS)
    {
        DeletePipelineObjects(
            g_renderer.program,
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

bool CreateSurfaceObjects(
    const std::vector<WebRendererSurfaceVertex> &vertices,
    const std::vector<std::uint16_t> &indices,
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
        static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint16_t)),
        indices.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(WebRendererSurfaceVertex),
        reinterpret_cast<const void *>(offsetof(WebRendererSurfaceVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        3,
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

bool CreateTextureObject(
    const std::uint8_t *pixels,
    std::uint32_t width,
    std::uint32_t height,
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
        layout(location = 0) in vec2 a_position;
        layout(location = 1) in vec3 a_color;
        layout(location = 2) in vec2 a_texcoord;
        uniform float u_aspect;
        out vec3 v_color;
        out vec2 v_texcoord;

        void main()
        {
            vec2 position = a_position;
            position.x *= min(1.0, u_aspect);
            position.y *= min(1.0, 1.0 / u_aspect);
            gl_Position = vec4(position, 0.0, 1.0);
            v_color = a_color;
            v_texcoord = a_texcoord;
        }
    )glsl";

    constexpr const char *fragmentSource = R"glsl(#version 300 es
        precision highp float;
        in vec3 v_color;
        in vec2 v_texcoord;
        uniform sampler2D u_texture;
        uniform float u_texture_enabled;
        out vec4 out_color;

        void main()
        {
            vec4 texel = texture(u_texture, v_texcoord);
            vec4 bootstrap_color = vec4(v_color, 1.0);
            out_color = mix(bootstrap_color, texel, u_texture_enabled);
        }
    )glsl";

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
        Web_Log(WebLogLevel::Error, "[kisakcod-web] Program link failed: %s\n", log);
        glDeleteProgram(program);
        return false;
    }

    while (glGetError() != GL_NO_ERROR)
    {
    }

    const GLint aspectUniform = glGetUniformLocation(program, "u_aspect");
    const GLint textureUniform = glGetUniformLocation(program, "u_texture");
    const GLint textureEnabledUniform =
        glGetUniformLocation(program, "u_texture_enabled");
    const GLenum pipelineError = glGetError();

    const std::uint8_t *texturePixels = FALLBACK_TEXTURE_RGBA;
    std::uint32_t textureWidth = 1u;
    std::uint32_t textureHeight = 1u;
    if (g_renderer.sourceTextureActive)
    {
        texturePixels = g_renderer.retainedPixels.data();
        textureWidth = g_renderer.textureWidth;
        textureHeight = g_renderer.textureHeight;
    }

    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;
    GLuint indexBuffer = 0;
    const bool surfaceReady = CreateSurfaceObjects(
        g_renderer.retainedVertices,
        g_renderer.retainedIndices,
        vertexArray,
        vertexBuffer,
        indexBuffer);

    GLuint texture = 0;
    const bool textureReady = surfaceReady && CreateTextureObject(
        texturePixels, textureWidth, textureHeight, texture);
    if (aspectUniform < 0 || textureUniform < 0 || textureEnabledUniform < 0 ||
        pipelineError != GL_NO_ERROR || !surfaceReady || !textureReady)
    {
        Web_Log(
            WebLogLevel::Error,
            "[kisakcod-web] WebGL2 resource creation failed (0x%x).\n",
            static_cast<unsigned int>(pipelineError));
        DeletePipelineObjects(
            program, vertexArray, vertexBuffer, indexBuffer, texture);
        return false;
    }

    g_renderer.program = program;
    g_renderer.vertexArray = vertexArray;
    g_renderer.vertexBuffer = vertexBuffer;
    g_renderer.indexBuffer = indexBuffer;
    g_renderer.texture = texture;
    g_renderer.aspectUniform = aspectUniform;
    g_renderer.textureUniform = textureUniform;
    g_renderer.textureEnabledUniform = textureEnabledUniform;
    ++g_renderer.rebuildGeneration;
    if (g_renderer.surfaceActive)
    {
        ++g_renderer.surfaceResourceGeneration;
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
    EmitSurfaceLifecycle(
        "ready",
        "The renderer recreated the indexed surface from its retained description");
    EmitTextureLifecycle(
        "ready",
        "The renderer recreated the texture from bounded recovery pixels");
    Web_Log(WebLogLevel::Info, "[kisakcod-web] WebGL2 context restored; renderer rebuilt.\n");
    Web_EmitRuntimeState("running", "WebGL2 context restored and rendering resumed");
    return true;
}

bool CreateWebGLContext()
{
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

    g_renderer.context = emscripten_webgl_create_context("#game-canvas", &attributes);
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
        "#game-canvas", nullptr, EM_TRUE, HandleWebGLContextLost);
    const EMSCRIPTEN_RESULT restoredCallbackResult =
        emscripten_set_webglcontextrestored_callback(
            "#game-canvas", nullptr, EM_TRUE, HandleWebGLContextRestored);
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
    g_renderer.draw = retainedSurface.draw;
    g_renderer.surfaceActive = true;
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
                retainedCopy.data(), texture.width, texture.height, replacementTexture))
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
                FALLBACK_TEXTURE_RGBA, 1u, 1u, fallbackTexture))
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
            FALLBACK_TEXTURE_RGBA, 1u, 1u, replacementTexture);
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
    emscripten_get_canvas_element_size("#game-canvas", &width, &height);
    if (width != g_renderer.canvasWidth || height != g_renderer.canvasHeight)
    {
        g_renderer.canvasWidth = width;
        g_renderer.canvasHeight = height;
        glViewport(0, 0, width, height);
    }

    const double elapsed = static_cast<double>(frame.monotonicMilliseconds) / 1000.0;
    const float wave = static_cast<float>(0.5 + 0.5 * std::sin(elapsed * 0.35));
    glClearColor(0.025f + wave * 0.012f, 0.03f, 0.035f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(g_renderer.program);
    glUniform1f(
        g_renderer.aspectUniform,
        height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f);
    glUniform1i(g_renderer.textureUniform, 0);
    glUniform1f(
        g_renderer.textureEnabledUniform,
        g_renderer.sourceTextureActive &&
                g_renderer.draw.textureBinding == WebRendererTextureBinding::EngineImage
            ? 1.0f
            : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_renderer.texture);
    glBindVertexArray(g_renderer.vertexArray);
    const std::uintptr_t indexOffset =
        static_cast<std::uintptr_t>(g_renderer.draw.firstIndex) * sizeof(std::uint16_t);
    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(g_renderer.draw.indexCount),
        GL_UNSIGNED_SHORT,
        reinterpret_cast<const void *>(indexOffset));

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
