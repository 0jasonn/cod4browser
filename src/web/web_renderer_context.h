#pragma once

#include <emscripten/html5.h>

struct WebRendererContextCallbacks
{
    em_webgl_context_callback lost = nullptr;
    em_webgl_context_callback restored = nullptr;
};

bool WebRendererContext_Create(
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE &context,
    const WebRendererContextCallbacks &callbacks);
bool WebRendererContext_MakeCurrent(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context);
void WebRendererContext_UnregisterCallbacks();
void WebRendererContext_Destroy(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE &context);
