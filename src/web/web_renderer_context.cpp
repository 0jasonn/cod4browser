#include <web/web_renderer_context.h>

#include <emscripten.h>

namespace
{
constexpr const char *CANVAS_SELECTOR = "#canvas";
}

bool WebRendererContext_Create(
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE &context,
    const WebRendererContextCallbacks &callbacks)
{
    if (context > 0 || callbacks.lost == nullptr || callbacks.restored == nullptr)
        return false;

    EM_ASM({
        const canvas = globalThis.__KISAKCOD_OFFSCREEN_CANVAS__;
        if (canvas && typeof GL === "object" && GL.offscreenCanvases) {
            GL.offscreenCanvases.canvas = canvas;
            // Emscripten's context creation resolves offscreenCanvases, but
            // its context-event registration uses the separate event table.
            specialHTMLTargets["#canvas"] = canvas;
        }
    });

    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.alpha = EM_FALSE;
    attributes.depth = EM_TRUE;
    attributes.stencil = EM_FALSE;
    // COD4 resolves its own scene target; default-framebuffer AA would not
    // cover that target and would make r_aaSamples = 1 dishonest.
    attributes.antialias = EM_FALSE;
    attributes.premultipliedAlpha = EM_FALSE;
    attributes.preserveDrawingBuffer = EM_FALSE;
    attributes.enableExtensionsByDefault = EM_TRUE;
    attributes.majorVersion = 2;
    attributes.minorVersion = 0;

    context = emscripten_webgl_create_context(CANVAS_SELECTOR, &attributes);
    if (context <= 0 || !WebRendererContext_MakeCurrent(context))
        return false;

    const EMSCRIPTEN_RESULT lostResult = emscripten_set_webglcontextlost_callback(
        CANVAS_SELECTOR, nullptr, EM_TRUE, callbacks.lost);
    const EMSCRIPTEN_RESULT restoredResult =
        emscripten_set_webglcontextrestored_callback(
            CANVAS_SELECTOR, nullptr, EM_TRUE, callbacks.restored);
    return lostResult == EMSCRIPTEN_RESULT_SUCCESS &&
        restoredResult == EMSCRIPTEN_RESULT_SUCCESS;
}

bool WebRendererContext_MakeCurrent(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context)
{
    return context > 0 &&
        emscripten_webgl_make_context_current(context) == EMSCRIPTEN_RESULT_SUCCESS;
}

void WebRendererContext_UnregisterCallbacks()
{
    (void)emscripten_set_webglcontextlost_callback(
        CANVAS_SELECTOR, nullptr, EM_TRUE, nullptr);
    (void)emscripten_set_webglcontextrestored_callback(
        CANVAS_SELECTOR, nullptr, EM_TRUE, nullptr);
}

void WebRendererContext_Destroy(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE &context)
{
    if (context > 0)
        (void)emscripten_webgl_destroy_context(context);
    context = 0;
    EM_ASM({ delete specialHTMLTargets["#canvas"]; });
}
